from __future__ import annotations

import argparse
import asyncio
from dataclasses import dataclass
import logging
import signal
import time
from typing import Dict, List, Sequence
from uuid import uuid4

import cv2
import mediapipe as mp
from ultralytics import YOLO
import websockets
from websockets.exceptions import ConnectionClosed

from schema import VisionGesture, VisionObject, VisionSnapshotMessage


LOGGER = logging.getLogger("jarvis.vision_node")


@dataclass(slots=True)
class VisionNodeConfig:
    server_url: str
    node_id: str
    camera_index: int = 0
    target_fps: float = 12.0
    send_interval_ms: int = 120
    max_snapshots_per_second: float = 6.0
    yolo_every_n_frames: int = 4
    reconnect_delay_sec: float = 3.0
    websocket_open_timeout_sec: float = 8.0
    model_name: str = "yolov8n.pt"
    pinch_distance_threshold: float = 0.09
    min_detection_confidence: float = 0.45
    min_tracking_confidence: float = 0.45
    max_objects_per_snapshot: int = 4
    objects_min_confidence: float = 0.60
    gestures_min_confidence: float = 0.70
    delta_threshold: float = 0.12


class VisionNodeService:
    def __init__(self, config: VisionNodeConfig) -> None:
        self.config = config
        self._stop_event = asyncio.Event()
        self._hands = mp.solutions.hands.Hands(
            static_image_mode=False,
            max_num_hands=2,
            min_detection_confidence=config.min_detection_confidence,
            min_tracking_confidence=config.min_tracking_confidence,
        )
        self._model = YOLO(config.model_name)
        self._capture: cv2.VideoCapture | None = None
        self._last_sent_snapshot: VisionSnapshotMessage | None = None
        self._last_log_times: Dict[str, float] = {}

    async def run_forever(self) -> None:
        while not self._stop_event.is_set():
            try:
                await self._run_session()
            except asyncio.CancelledError:
                raise
            except Exception as exc:  # pragma: no cover - defensive runtime path
                LOGGER.exception("Vision session failed: %s", exc)

            if self._stop_event.is_set():
                break

            LOGGER.info("Reconnecting in %.1f seconds", self.config.reconnect_delay_sec)
            await asyncio.sleep(self.config.reconnect_delay_sec)

        self._close_capture()
        self._hands.close()

    def request_stop(self) -> None:
        self._stop_event.set()

    async def _run_session(self) -> None:
        self._open_capture()
        LOGGER.info("Connecting to %s", self.config.server_url)
        async with websockets.connect(
            self.config.server_url,
            open_timeout=self.config.websocket_open_timeout_sec,
            ping_interval=20,
            ping_timeout=20,
            max_queue=1,
        ) as websocket:
            LOGGER.info("Connected to %s", self.config.server_url)
            await self._capture_loop(websocket)

    def _open_capture(self) -> None:
        self._close_capture()
        self._capture = cv2.VideoCapture(self.config.camera_index)
        if not self._capture.isOpened():
            raise RuntimeError(f"Failed to open camera index {self.config.camera_index}")

    def _close_capture(self) -> None:
        if self._capture is not None:
            self._capture.release()
            self._capture = None

    async def _capture_loop(self, websocket: websockets.WebSocketClientProtocol) -> None:
        assert self._capture is not None
        frame_index = 0
        last_sent_at = 0.0
        latest_objects: List[VisionObject] = []
        frame_interval = 1.0 / max(1.0, self.config.target_fps)
        send_interval = self._effective_send_interval_sec()

        while not self._stop_event.is_set():
            frame_started_at = time.monotonic()
            ok, frame = self._capture.read()
            if not ok:
                LOGGER.warning("Camera frame capture failed")
                await asyncio.sleep(0.2)
                continue

            if frame_index % max(1, self.config.yolo_every_n_frames) == 0:
                latest_objects = self._detect_objects(frame)

            gestures = self._detect_gestures(frame)
            snapshot = self._build_snapshot(latest_objects, gestures)
            should_send, drop_reason = self._should_send_snapshot(snapshot)

            now = time.monotonic()
            rate_limited = (now - last_sent_at) < send_interval
            if should_send and not rate_limited:
                try:
                    await websocket.send(snapshot.to_json())
                    last_sent_at = now
                    self._last_sent_snapshot = snapshot
                    self._log_rate_limited(
                        "snapshot_sent",
                        logging.INFO,
                        'vision_sent trace="%s" summary="%s"',
                        snapshot.trace_id,
                        snapshot.summary,
                        interval_sec=1.0,
                    )
                except ConnectionClosed:
                    LOGGER.warning("WebSocket connection closed during send")
                    raise
            elif should_send and rate_limited:
                self._log_rate_limited(
                    "snapshot_dropped_rate_limit",
                    logging.INFO,
                    'snapshot_dropped_reason="rate_limited" max_snapshots_per_second=%.2f',
                    self.config.max_snapshots_per_second,
                )
            else:
                self._log_rate_limited(
                    f"snapshot_dropped_{drop_reason}",
                    logging.INFO,
                    'snapshot_dropped_reason="%s"',
                    drop_reason,
                )

            frame_index += 1
            elapsed = time.monotonic() - frame_started_at
            await asyncio.sleep(max(0.0, frame_interval - elapsed))

    def _detect_objects(self, frame) -> List[VisionObject]:
        results = self._model(frame, verbose=False)
        if not results:
            return []

        result = results[0]
        if result.boxes is None:
            return []

        by_label: dict[str, float] = {}
        confidence_filtered = 0
        for box in result.boxes:
            confidence = float(box.conf[0].item())
            if confidence < self.config.objects_min_confidence:
                confidence_filtered += 1
                continue

            class_index = int(box.cls[0].item())
            label = result.names.get(class_index, str(class_index))
            existing = by_label.get(label, 0.0)
            if confidence > existing:
                by_label[label] = confidence

        if confidence_filtered > 0:
            self._log_rate_limited(
                "objects_confidence_filtered",
                logging.INFO,
                'snapshot_dropped_reason="confidence_filtered" objects_filtered=%d threshold=%.2f',
                confidence_filtered,
                self.config.objects_min_confidence,
            )

        sorted_objects = sorted(by_label.items(), key=lambda item: item[1], reverse=True)
        return [
            VisionObject(class_name=label, confidence=confidence)
            for label, confidence in sorted_objects[: self.config.max_objects_per_snapshot]
        ]

    def _detect_gestures(self, frame) -> List[VisionGesture]:
        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        results = self._hands.process(rgb_frame)
        if not results.multi_hand_landmarks:
            return []

        gestures: List[VisionGesture] = []
        confidence_filtered = 0
        for hand_landmarks in results.multi_hand_landmarks:
            thumb_tip = hand_landmarks.landmark[4]
            index_tip = hand_landmarks.landmark[8]
            middle_tip = hand_landmarks.landmark[12]
            ring_tip = hand_landmarks.landmark[16]
            pinky_tip = hand_landmarks.landmark[20]
            wrist = hand_landmarks.landmark[0]

            pinch_distance = ((thumb_tip.x - index_tip.x) ** 2 + (thumb_tip.y - index_tip.y) ** 2) ** 0.5
            pinch_confidence = max(0.0, min(1.0, 1.0 - (pinch_distance / self.config.pinch_distance_threshold)))
            if pinch_confidence >= self.config.gestures_min_confidence:
                gestures.append(VisionGesture(name="pinch", confidence=pinch_confidence))
            else:
                confidence_filtered += 1

            if wrist.y > index_tip.y and wrist.y > middle_tip.y and wrist.y > ring_tip.y and wrist.y > pinky_tip.y:
                gestures.append(VisionGesture(name="open_hand", confidence=0.95))

            fingers_extended = int(index_tip.y < wrist.y) + int(middle_tip.y < wrist.y) + int(ring_tip.y < wrist.y) + int(pinky_tip.y < wrist.y)
            if fingers_extended == 2 and index_tip.y < wrist.y and middle_tip.y < wrist.y:
                gestures.append(VisionGesture(name="two_fingers", confidence=0.85))

        deduped: dict[str, float] = {}
        for gesture in gestures:
            if gesture.confidence < self.config.gestures_min_confidence:
                confidence_filtered += 1
                continue
            deduped[gesture.name] = max(deduped.get(gesture.name, 0.0), gesture.confidence)

        if confidence_filtered > 0 or len(gestures) != len(deduped):
            self._log_rate_limited(
                "gestures_confidence_filtered",
                logging.INFO,
                'snapshot_dropped_reason="confidence_filtered" gestures_filtered=%d threshold=%.2f',
                max(confidence_filtered, len(gestures) - len(deduped)),
                self.config.gestures_min_confidence,
            )

        return [VisionGesture(name=name, confidence=confidence) for name, confidence in deduped.items()]

    def _build_snapshot(self, objects: List[VisionObject], gestures: List[VisionGesture]) -> VisionSnapshotMessage:
        summary = self._build_scene_summary(objects, gestures)
        return VisionSnapshotMessage(
            node_id=self.config.node_id,
            objects=objects,
            gestures=gestures,
            summary=summary,
            trace_id=uuid4().hex,
        )

    def _build_scene_summary(self, objects: Sequence[VisionObject], gestures: Sequence[VisionGesture]) -> str:
        object_names = [item.class_name for item in objects]
        gesture_names = [item.name for item in gestures]

        if object_names and "pinch" in gesture_names:
            return f"You are holding {self._with_article(object_names[0])}"
        if object_names and "open_hand" in gesture_names:
            return f"An open hand is visible near {self._with_article(object_names[0])}"
        if object_names and "two_fingers" in gesture_names:
            return f"You are pointing at {self._with_article(object_names[0])} with two fingers"
        if len(object_names) == 1:
            return f"I can see {self._with_article(object_names[0])}"
        if len(object_names) > 1:
            return f"I can see {', '.join(object_names[:2])}"
        if gesture_names:
            return f"Detected gesture: {gesture_names[0]}"
        return "No strong visual event detected"

    def _should_send_snapshot(self, snapshot: VisionSnapshotMessage) -> tuple[bool, str]:
        if self._last_sent_snapshot is None:
            return True, "initial"

        if self._labels_changed(snapshot.objects, self._last_sent_snapshot.objects, lambda item: item.class_name):
            return True, "objects_changed"
        if self._labels_changed(snapshot.gestures, self._last_sent_snapshot.gestures, lambda item: item.name):
            return True, "gestures_changed"
        if self._confidence_changed(snapshot.objects, self._last_sent_snapshot.objects, lambda item: item.class_name):
            return True, "objects_confidence_delta"
        if self._confidence_changed(snapshot.gestures, self._last_sent_snapshot.gestures, lambda item: item.name):
            return True, "gestures_confidence_delta"

        return False, "delta_unchanged"

    def _confidence_changed(self, current: Sequence, previous: Sequence, key_fn) -> bool:
        current_map = {key_fn(item): item.confidence for item in current}
        previous_map = {key_fn(item): item.confidence for item in previous}
        if set(current_map.keys()) != set(previous_map.keys()):
            return True

        for key, value in current_map.items():
            if abs(value - previous_map.get(key, 0.0)) >= self.config.delta_threshold:
                return True
        return False

    @staticmethod
    def _labels_changed(current: Sequence, previous: Sequence, key_fn) -> bool:
        return {key_fn(item) for item in current} != {key_fn(item) for item in previous}

    def _effective_send_interval_sec(self) -> float:
        rate_limit_interval = 1.0 / max(1.0, self.config.max_snapshots_per_second)
        configured_interval = max(0.01, self.config.send_interval_ms / 1000.0)
        return max(rate_limit_interval, configured_interval)

    def _log_rate_limited(self, key: str, level: int, message: str, *args, interval_sec: float = 2.0) -> None:
        now = time.monotonic()
        previous = self._last_log_times.get(key, 0.0)
        if previous > 0.0 and (now - previous) < interval_sec:
            return
        self._last_log_times[key] = now
        LOGGER.log(level, message, *args)

    @staticmethod
    def _with_article(noun: str) -> str:
        if not noun:
            return "something"
        return f"an {noun}" if noun[0].lower() in {"a", "e", "i", "o", "u"} else f"a {noun}"


def parse_args() -> VisionNodeConfig:
    parser = argparse.ArgumentParser(description="JARVIS distributed vision node")
    parser.add_argument("--server-url", required=True, help="WebSocket server URL exposed by the main PC")
    parser.add_argument("--node-id", default="laptop-vision-node", help="Stable node identifier")
    parser.add_argument("--camera-index", type=int, default=0, help="OpenCV camera index")
    parser.add_argument("--fps", type=float, default=12.0, help="Target processing FPS")
    parser.add_argument("--send-interval-ms", type=int, default=120, help="Minimum interval between semantic snapshots")
    parser.add_argument("--max-snapshots-per-second", type=float, default=6.0, help="Hard cap for snapshot send rate")
    parser.add_argument("--yolo-every-n-frames", type=int, default=4, help="Run YOLO every N frames")
    parser.add_argument("--reconnect-delay-sec", type=float, default=3.0, help="Reconnect delay after transport failure")
    parser.add_argument("--model-name", default="yolov8n.pt", help="Ultralytics model name or path")
    parser.add_argument("--objects-min-confidence", type=float, default=0.60, help="Minimum object confidence to include")
    parser.add_argument("--gestures-min-confidence", type=float, default=0.70, help="Minimum gesture confidence to include")
    parser.add_argument("--delta-threshold", type=float, default=0.12, help="Confidence delta threshold for resend")
    args = parser.parse_args()

    return VisionNodeConfig(
        server_url=args.server_url,
        node_id=args.node_id,
        camera_index=args.camera_index,
        target_fps=args.fps,
        send_interval_ms=args.send_interval_ms,
        max_snapshots_per_second=args.max_snapshots_per_second,
        yolo_every_n_frames=args.yolo_every_n_frames,
        reconnect_delay_sec=args.reconnect_delay_sec,
        model_name=args.model_name,
        objects_min_confidence=args.objects_min_confidence,
        gestures_min_confidence=args.gestures_min_confidence,
        delta_threshold=args.delta_threshold,
    )


async def _main() -> None:
    config = parse_args()
    service = VisionNodeService(config)
    loop = asyncio.get_running_loop()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, service.request_stop)
        except NotImplementedError:  # pragma: no cover - Windows fallback
            signal.signal(sig, lambda *_: service.request_stop())

    await service.run_forever()


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="[%(asctime)s] [%(levelname)s] %(name)s: %(message)s",
    )
    asyncio.run(_main())


if __name__ == "__main__":
    main()
