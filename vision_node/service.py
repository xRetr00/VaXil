from __future__ import annotations

import asyncio
from dataclasses import dataclass
import logging
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


DEBUG_WINDOW_NAME = "JARVIS Vision Debug"


@dataclass(slots=True)
class DebugDetection:
    class_name: str
    confidence: float
    box: tuple[int, int, int, int]


@dataclass(slots=True)
class DebugStats:
    fps: float = 0.0
    yolo_ms: float = 0.0
    hands_ms: float = 0.0
    send_latency_ms: float = 0.0
    objects_count: int = 0
    gesture_name: str = "none"
    summary: str = "No strong visual event detected"


def render_debug_frame(frame,
                       detections: Sequence[DebugDetection],
                       hand_landmarks,
                       stats: DebugStats):
    output = frame.copy()

    for detection in detections:
        x1, y1, x2, y2 = detection.box
        cv2.rectangle(output, (x1, y1), (x2, y2), (80, 220, 80), 2)
        label = f"{detection.class_name} ({detection.confidence:.2f})"
        cv2.putText(output,
                    label,
                    (x1, max(20, y1 - 8)),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.55,
                    (80, 220, 80),
                    2,
                    cv2.LINE_AA)

    drawing_utils = mp.solutions.drawing_utils
    drawing_styles = mp.solutions.drawing_styles
    for landmarks in hand_landmarks or []:
        drawing_utils.draw_landmarks(
            output,
            landmarks,
            mp.solutions.hands.HAND_CONNECTIONS,
            drawing_styles.get_default_hand_landmarks_style(),
            drawing_styles.get_default_hand_connections_style(),
        )

    stats_lines = [
        f"FPS: {stats.fps:.1f}",
        f"YOLO: {stats.yolo_ms:.1f}ms",
        f"Hands: {stats.hands_ms:.1f}ms",
        f"Send: {stats.send_latency_ms:.1f}ms",
        f"Objects: {stats.objects_count}",
        f"Gesture: {stats.gesture_name}",
    ]

    panel_height = 26 + (len(stats_lines) * 24)
    cv2.rectangle(output, (12, 12), (250, panel_height), (18, 18, 18), -1)
    cv2.rectangle(output, (12, 12), (250, panel_height), (70, 70, 70), 1)

    for index, line in enumerate(stats_lines):
        color = (220, 220, 220)
        if line.startswith("Gesture:") and stats.gesture_name != "none":
            color = (70, 90, 240)
        cv2.putText(output,
                    line,
                    (22, 36 + (index * 22)),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.55,
                    color,
                    1,
                    cv2.LINE_AA)

    cv2.putText(output,
                stats.summary[:90],
                (18, max(40, output.shape[0] - 18)),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.6,
                (255, 255, 255),
                2,
                cv2.LINE_AA)
    return output


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
    debug_ui: bool = False


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
        if self.config.debug_ui:
            cv2.destroyAllWindows()

    async def _capture_loop(self, websocket: websockets.WebSocketClientProtocol) -> None:
        assert self._capture is not None
        frame_index = 0
        last_sent_at = 0.0
        latest_objects: List[VisionObject] = []
        latest_detections: List[DebugDetection] = []
        frame_interval = 1.0 / max(1.0, self.config.target_fps)
        send_interval = self._effective_send_interval_sec()
        last_yolo_ms = 0.0
        last_send_latency_ms = 0.0
        stats = DebugStats()

        while not self._stop_event.is_set():
            frame_started_at = time.monotonic()
            ok, frame = self._capture.read()
            if not ok:
                LOGGER.warning("Camera frame capture failed")
                await asyncio.sleep(0.2)
                continue

            hand_landmarks = []
            if frame_index % max(1, self.config.yolo_every_n_frames) == 0:
                yolo_started_at = time.monotonic()
                latest_objects, latest_detections = self._detect_objects(frame)
                last_yolo_ms = (time.monotonic() - yolo_started_at) * 1000.0

            hands_started_at = time.monotonic()
            gestures, hand_landmarks = self._detect_gestures(frame)
            hands_ms = (time.monotonic() - hands_started_at) * 1000.0
            snapshot = self._build_snapshot(latest_objects, gestures)
            should_send, drop_reason = self._should_send_snapshot(snapshot)

            now = time.monotonic()
            rate_limited = (now - last_sent_at) < send_interval
            if should_send and not rate_limited:
                try:
                    send_started_at = time.monotonic()
                    await websocket.send(snapshot.to_json())
                    last_send_latency_ms = (time.monotonic() - send_started_at) * 1000.0
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
            stats = DebugStats(
                fps=1.0 / max(elapsed, 0.0001),
                yolo_ms=last_yolo_ms,
                hands_ms=hands_ms,
                send_latency_ms=last_send_latency_ms,
                objects_count=len(latest_objects),
                gesture_name=gestures[0].name if gestures else "none",
                summary=snapshot.summary,
            )

            if self.config.debug_ui:
                debug_frame = render_debug_frame(frame, latest_detections, hand_landmarks, stats)
                cv2.imshow(DEBUG_WINDOW_NAME, debug_frame)
                if (cv2.waitKey(1) & 0xFF) == 27:
                    LOGGER.info("ESC pressed, closing debug UI")
                    self.request_stop()
                    break

            await asyncio.sleep(max(0.0, frame_interval - elapsed))

    def _detect_objects(self, frame) -> tuple[List[VisionObject], List[DebugDetection]]:
        results = self._model(frame, verbose=False)
        if not results:
            return [], []

        result = results[0]
        if result.boxes is None:
            return [], []

        by_label: dict[str, float] = {}
        debug_detections: List[DebugDetection] = []
        confidence_filtered = 0
        for box in result.boxes:
            confidence = float(box.conf[0].item())
            class_index = int(box.cls[0].item())
            label = result.names.get(class_index, str(class_index))
            xyxy = box.xyxy[0].tolist()
            debug_detections.append(
                DebugDetection(
                    class_name=label,
                    confidence=confidence,
                    box=(int(xyxy[0]), int(xyxy[1]), int(xyxy[2]), int(xyxy[3])),
                )
            )
            if confidence < self.config.objects_min_confidence:
                confidence_filtered += 1
                continue

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
        return (
            [
                VisionObject(class_name=label, confidence=confidence)
                for label, confidence in sorted_objects[: self.config.max_objects_per_snapshot]
            ],
            debug_detections,
        )

    def _detect_gestures(self, frame) -> tuple[List[VisionGesture], Sequence]:
        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        results = self._hands.process(rgb_frame)
        if not results.multi_hand_landmarks:
            return [], []

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

        return [VisionGesture(name=name, confidence=confidence) for name, confidence in deduped.items()], results.multi_hand_landmarks

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
