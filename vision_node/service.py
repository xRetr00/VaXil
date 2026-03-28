from __future__ import annotations

import asyncio
from dataclasses import dataclass
import logging
from urllib.parse import urlparse
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
SNAPSHOT_KEEPALIVE_SEC = 1.5
MIN_HAND_SCALE = 0.10
EXTENDED_FLAG_THRESHOLD = 0.55
FOLDED_FLAG_THRESHOLD = 0.55
SCENE_ANCHOR_CLASSES = {
    "person",
    "man",
    "woman",
    "boy",
    "girl",
}
HANDHELD_OBJECT_CLASSES = {
    "bottle",
    "book",
    "can",
    "cell phone",
    "cup",
    "fork",
    "keyboard",
    "knife",
    "laptop",
    "mouse",
    "remote",
    "scissors",
    "spoon",
    "sports ball",
    "toothbrush",
    "ciggarette",
}

try:
    MP_SOLUTIONS = mp.solutions
except AttributeError:
    try:
        from mediapipe.python import solutions as MP_SOLUTIONS
    except ImportError as exc:  # pragma: no cover - environment-specific dependency path
        raise RuntimeError(
            "Installed mediapipe package does not expose the Hands solutions API. "
            "Use a mediapipe build that includes solutions or reinstall the vision node dependencies."
        ) from exc


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
    finger_count: int = -1
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

    drawing_utils = MP_SOLUTIONS.drawing_utils
    drawing_styles = MP_SOLUTIONS.drawing_styles
    for landmarks in hand_landmarks or []:
        drawing_utils.draw_landmarks(
            output,
            landmarks,
            MP_SOLUTIONS.hands.HAND_CONNECTIONS,
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
        f"Fingers: {stats.finger_count if stats.finger_count >= 0 else 'unknown'}",
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
    websocket_open_timeout_sec: float = 20.0
    model_name: str = "yolov8n.pt"
    pinch_distance_threshold: float = 0.09
    min_detection_confidence: float = 0.45
    min_tracking_confidence: float = 0.45
    max_objects_per_snapshot: int = 4
    objects_min_confidence: float = 0.60
    gestures_min_confidence: float = 0.70
    delta_threshold: float = 0.12
    process_width: int = 640
    process_height: int = 480
    display_width: int = 1280
    display_height: int = 720
    fullscreen: bool = False
    debug_skip_frames: int = 1
    debug_ui: bool = False


class VisionNodeService:
    def __init__(self, config: VisionNodeConfig) -> None:
        self.config = config
        self._stop_event = asyncio.Event()
        self._hands = MP_SOLUTIONS.hands.Hands(
            static_image_mode=False,
            max_num_hands=2,
            min_detection_confidence=config.min_detection_confidence,
            min_tracking_confidence=config.min_tracking_confidence,
        )
        self._model = YOLO(config.model_name)
        self._capture: cv2.VideoCapture | None = None
        self._last_sent_snapshot: VisionSnapshotMessage | None = None
        self._last_log_times: Dict[str, float] = {}
        self._debug_window_initialized = False

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
        LOGGER.info(
            "Vision pipeline configuration: process=%dx%d display=%dx%d fullscreen=%s debug_skip_frames=%d",
            self.config.process_width,
            self.config.process_height,
            self.config.display_width,
            self.config.display_height,
            "true" if self.config.fullscreen else "false",
            max(1, self.config.debug_skip_frames),
        )
        LOGGER.info("Connecting to %s", self.config.server_url)
        await self._preflight_endpoint_check()
        async with websockets.connect(
            self.config.server_url,
            open_timeout=self.config.websocket_open_timeout_sec,
            ping_interval=20,
            ping_timeout=20,
            max_queue=1,
        ) as websocket:
            LOGGER.info("Connected to %s", self.config.server_url)
            await self._capture_loop(websocket)

    async def _preflight_endpoint_check(self) -> None:
        parsed = urlparse(self.config.server_url)
        host = parsed.hostname
        if not host:
            raise RuntimeError(f"Invalid server URL (missing host): {self.config.server_url}")

        if parsed.scheme not in {"ws", "wss"}:
            raise RuntimeError(
                f"Invalid server URL scheme '{parsed.scheme}'. Use ws:// or wss:// in {self.config.server_url}"
            )

        default_port = 443 if parsed.scheme == "wss" else 80
        port = parsed.port or default_port

        writer: asyncio.StreamWriter | None = None
        try:
            connect_timeout = min(5.0, max(1.0, self.config.websocket_open_timeout_sec * 0.5))
            _, writer = await asyncio.wait_for(
                asyncio.open_connection(host, port),
                timeout=connect_timeout,
            )
        except Exception as exc:
            raise RuntimeError(
                "TCP preflight to WebSocket endpoint failed "
                f"({host}:{port}). Verify IP, port, firewall, and that the main PC is listening on /vision."
            ) from exc
        finally:
            if writer is not None:
                writer.close()
                try:
                    await writer.wait_closed()
                except Exception:
                    pass

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
            self._debug_window_initialized = False

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
            original_frame = self.capture_frame()
            if original_frame is None:
                await asyncio.sleep(0.2)
                continue

            process_frame = self.prepare_process_frame(original_frame)
            latest_objects, latest_detections, gestures, hand_landmarks, finger_count, yolo_ms, hands_ms = self.run_inference(
                process_frame=process_frame,
                frame_index=frame_index,
                latest_objects=latest_objects,
                latest_detections=latest_detections,
            )
            if yolo_ms > 0.0:
                last_yolo_ms = yolo_ms
            snapshot = self._build_snapshot(latest_objects, gestures, finger_count)
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
            elif self._should_send_keepalive(snapshot, now, last_sent_at):
                try:
                    send_started_at = time.monotonic()
                    await websocket.send(snapshot.to_json())
                    last_send_latency_ms = (time.monotonic() - send_started_at) * 1000.0
                    last_sent_at = now
                    self._last_sent_snapshot = snapshot
                    self._log_rate_limited(
                        "snapshot_keepalive_sent",
                        logging.INFO,
                        'vision_sent trace="%s" reason="keepalive" summary="%s"',
                        snapshot.trace_id,
                        snapshot.summary,
                        interval_sec=1.0,
                    )
                except ConnectionClosed:
                    LOGGER.warning("WebSocket connection closed during keepalive send")
                    raise
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
                finger_count=finger_count,
                summary=snapshot.summary,
            )

            self._log_rate_limited(
                "debug_runtime_fps",
                logging.INFO,
                "runtime_fps=%.1f process=%dx%d display=%dx%d",
                stats.fps,
                process_frame.shape[1],
                process_frame.shape[0],
                self.config.display_width,
                self.config.display_height,
                interval_sec=5.0,
            )

            if self.config.debug_ui and frame_index % max(1, self.config.debug_skip_frames) == 0:
                display_frame = self.prepare_display_frame(original_frame)
                debug_frame = self.render_overlays(
                    display_frame=display_frame,
                    detections=self._scale_debug_detections(
                        latest_detections,
                        process_frame.shape[1],
                        process_frame.shape[0],
                        display_frame.shape[1],
                        display_frame.shape[0],
                    ),
                    hand_landmarks=hand_landmarks,
                    stats=stats,
                )
                if not self.show_debug_window(debug_frame):
                    LOGGER.info("ESC pressed, closing debug UI")
                    self.request_stop()
                    break

            await asyncio.sleep(max(0.0, frame_interval - elapsed))

    def capture_frame(self):
        assert self._capture is not None
        ok, frame = self._capture.read()
        if not ok:
            LOGGER.warning("Camera frame capture failed")
            return None
        return frame

    def prepare_process_frame(self, frame):
        target_size = (max(1, self.config.process_width), max(1, self.config.process_height))
        if frame.shape[1] == target_size[0] and frame.shape[0] == target_size[1]:
            return frame
        return cv2.resize(frame, target_size, interpolation=cv2.INTER_AREA)

    def prepare_display_frame(self, frame):
        target_size = (max(1, self.config.display_width), max(1, self.config.display_height))
        if frame.shape[1] == target_size[0] and frame.shape[0] == target_size[1]:
            return frame.copy()
        return cv2.resize(frame, target_size, interpolation=cv2.INTER_LINEAR)

    def run_inference(self,
                      process_frame,
                      frame_index: int,
                      latest_objects: List[VisionObject],
                      latest_detections: List[DebugDetection]):
        updated_objects = latest_objects
        updated_detections = latest_detections
        yolo_ms = 0.0

        if frame_index % max(1, self.config.yolo_every_n_frames) == 0:
            yolo_started_at = time.monotonic()
            updated_objects, updated_detections = self._detect_objects(process_frame)
            yolo_ms = (time.monotonic() - yolo_started_at) * 1000.0

        hands_started_at = time.monotonic()
        gestures, hand_landmarks, finger_count = self._detect_gestures(process_frame)
        hands_ms = (time.monotonic() - hands_started_at) * 1000.0
        return updated_objects, updated_detections, gestures, hand_landmarks, finger_count, yolo_ms, hands_ms

    def render_overlays(self,
                        display_frame,
                        detections: Sequence[DebugDetection],
                        hand_landmarks,
                        stats: DebugStats):
        return render_debug_frame(display_frame, detections, hand_landmarks, stats)

    def show_debug_window(self, frame) -> bool:
        self._ensure_debug_window()
        cv2.imshow(DEBUG_WINDOW_NAME, frame)
        return (cv2.waitKey(1) & 0xFF) != 27

    def _ensure_debug_window(self) -> None:
        if self._debug_window_initialized:
            return

        cv2.namedWindow(DEBUG_WINDOW_NAME, cv2.WINDOW_NORMAL)
        if self.config.fullscreen:
            cv2.setWindowProperty(
                DEBUG_WINDOW_NAME,
                cv2.WND_PROP_FULLSCREEN,
                cv2.WINDOW_FULLSCREEN,
            )
        else:
            cv2.resizeWindow(
                DEBUG_WINDOW_NAME,
                max(1, self.config.display_width),
                max(1, self.config.display_height),
            )
        self._debug_window_initialized = True

    @staticmethod
    def _scale_debug_detections(detections: Sequence[DebugDetection],
                                source_width: int,
                                source_height: int,
                                target_width: int,
                                target_height: int) -> List[DebugDetection]:
        if source_width <= 0 or source_height <= 0:
            return list(detections)

        scale_x = target_width / float(source_width)
        scale_y = target_height / float(source_height)
        return [
            DebugDetection(
                class_name=detection.class_name,
                confidence=detection.confidence,
                box=(
                    int(detection.box[0] * scale_x),
                    int(detection.box[1] * scale_y),
                    int(detection.box[2] * scale_x),
                    int(detection.box[3] * scale_y),
                ),
            )
            for detection in detections
        ]

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

    def _detect_gestures(self, frame) -> tuple[List[VisionGesture], Sequence, int]:
        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        results = self._hands.process(rgb_frame)
        if not results.multi_hand_landmarks:
            return [], [], -1

        gestures: List[VisionGesture] = []
        confidence_filtered = 0
        best_finger_count = -1
        handedness_entries = list(results.multi_handedness or [])
        for index, hand_landmarks in enumerate(results.multi_hand_landmarks):
            handedness_label = ""
            if index < len(handedness_entries) and handedness_entries[index].classification:
                handedness_label = handedness_entries[index].classification[0].label.lower()

            thumb_tip = hand_landmarks.landmark[4]
            index_tip = hand_landmarks.landmark[8]
            hand_scale = self._estimate_hand_scale(hand_landmarks)
            if hand_scale < MIN_HAND_SCALE:
                confidence_filtered += 1
                continue

            pinch_distance = self._distance_2d(thumb_tip, index_tip)
            pinch_ratio = pinch_distance / max(hand_scale, 1e-6)
            absolute_pinch_confidence = self._clamp_unit(1.0 - (pinch_distance / self.config.pinch_distance_threshold))
            relative_pinch_confidence = self._clamp_unit(1.0 - (pinch_ratio / 0.55))
            pinch_confidence = max(absolute_pinch_confidence, relative_pinch_confidence)

            index_extended_conf = self._finger_extended_confidence(hand_landmarks, 8, 6, 5, hand_scale)
            middle_extended_conf = self._finger_extended_confidence(hand_landmarks, 12, 10, 9, hand_scale)
            ring_extended_conf = self._finger_extended_confidence(hand_landmarks, 16, 14, 13, hand_scale)
            pinky_extended_conf = self._finger_extended_confidence(hand_landmarks, 20, 18, 17, hand_scale)
            thumb_extended_conf = self._thumb_extended_confidence(hand_landmarks, handedness_label, hand_scale)
            thumb_up_conf = self._thumb_vertical_confidence(hand_landmarks, hand_scale, upward=True)
            thumb_down_conf = self._thumb_vertical_confidence(hand_landmarks, hand_scale, upward=False)

            index_folded_conf = self._finger_folded_confidence(hand_landmarks, 8, 6, hand_scale)
            middle_folded_conf = self._finger_folded_confidence(hand_landmarks, 12, 10, hand_scale)
            ring_folded_conf = self._finger_folded_confidence(hand_landmarks, 16, 14, hand_scale)
            pinky_folded_conf = self._finger_folded_confidence(hand_landmarks, 20, 18, hand_scale)

            index_retracted_conf = max(index_folded_conf, 1.0 - index_extended_conf)
            ring_retracted_conf = max(ring_folded_conf, 1.0 - ring_extended_conf)
            pinky_retracted_conf = max(pinky_folded_conf, 1.0 - pinky_extended_conf)
            folded_scores = [
                index_folded_conf,
                middle_folded_conf,
                ring_folded_conf,
                pinky_folded_conf,
            ]
            extended_scores = [
                index_extended_conf,
                middle_extended_conf,
                ring_extended_conf,
                pinky_extended_conf,
            ]
            folded_count = sum(1 for value in folded_scores if value >= FOLDED_FLAG_THRESHOLD)
            extended_count = sum(1 for value in extended_scores if value >= EXTENDED_FLAG_THRESHOLD)
            folded_average = sum(folded_scores) / len(folded_scores)
            extended_average = sum(extended_scores) / len(extended_scores)

            middle_dominance_conf = min(
                self._clamp_unit((middle_extended_conf - index_extended_conf + 0.12) / 0.35),
                self._clamp_unit((middle_extended_conf - ring_extended_conf + 0.12) / 0.35),
                self._clamp_unit((middle_extended_conf - pinky_extended_conf + 0.12) / 0.35),
            )
            middle_finger_confidence = 0.0
            if middle_extended_conf >= 0.58 and middle_dominance_conf >= 0.45:
                middle_finger_confidence = max(
                    0.45 * middle_extended_conf
                    + 0.30 * middle_dominance_conf
                    + 0.25 * ((index_retracted_conf + ring_retracted_conf + pinky_retracted_conf) / 3.0),
                    0.78,
                )

            thumbs_up_confidence = 0.0
            if thumb_up_conf >= 0.45 and thumb_extended_conf >= 0.35 and folded_count >= 3:
                thumbs_up_confidence = max(
                    0.40 * thumb_up_conf
                    + 0.35 * thumb_extended_conf
                    + 0.25 * folded_average,
                    0.80,
                )

            thumbs_down_confidence = 0.0
            if thumb_down_conf >= 0.45 and thumb_extended_conf >= 0.35 and folded_count >= 3:
                thumbs_down_confidence = max(
                    0.40 * thumb_down_conf
                    + 0.35 * thumb_extended_conf
                    + 0.25 * folded_average,
                    0.80,
                )

            two_fingers_confidence = 0.0
            if index_extended_conf >= 0.55 and middle_extended_conf >= 0.55 and ring_retracted_conf >= 0.45 and pinky_retracted_conf >= 0.45:
                two_fingers_confidence = max(
                    0.40 * ((index_extended_conf + middle_extended_conf) / 2.0)
                    + 0.35 * ((ring_retracted_conf + pinky_retracted_conf) / 2.0)
                    + 0.25 * self._clamp_unit((2.0 - abs(extended_count - 2.0)) / 2.0),
                    0.78,
                )

            open_hand_confidence = 0.0
            if extended_count >= 3 and extended_average >= 0.52:
                open_hand_confidence = max(
                    0.70 * extended_average
                    + 0.15 * self._clamp_unit((extended_count - 2.0) / 2.0)
                    + 0.15 * thumb_extended_conf,
                    0.82 if extended_count >= 4 else 0.74,
                )

            closed_hand_confidence = 0.0
            if folded_count >= 3 and folded_average >= 0.50:
                closed_hand_confidence = max(
                    0.75 * folded_average
                    + 0.25 * self._clamp_unit((folded_count - 2.0) / 2.0),
                    0.80 if folded_count >= 4 and pinch_confidence < 0.55 else 0.72,
                )

            extended_flags = [
                thumb_extended_conf >= EXTENDED_FLAG_THRESHOLD,
                index_extended_conf >= EXTENDED_FLAG_THRESHOLD,
                middle_extended_conf >= EXTENDED_FLAG_THRESHOLD,
                ring_extended_conf >= EXTENDED_FLAG_THRESHOLD,
                pinky_extended_conf >= EXTENDED_FLAG_THRESHOLD,
            ]
            best_finger_count = max(best_finger_count, sum(1 for flag in extended_flags if flag))

            if middle_finger_confidence >= self.config.gestures_min_confidence:
                gestures.append(VisionGesture(name="middle_finger", confidence=middle_finger_confidence))
                continue

            if thumbs_up_confidence >= self.config.gestures_min_confidence:
                gestures.append(VisionGesture(name="thumbs_up", confidence=thumbs_up_confidence))
                continue

            if thumbs_down_confidence >= self.config.gestures_min_confidence:
                gestures.append(VisionGesture(name="thumbs_down", confidence=thumbs_down_confidence))
                continue

            if pinch_confidence >= self.config.gestures_min_confidence:
                gestures.append(VisionGesture(name="pinch", confidence=pinch_confidence))
                continue

            if two_fingers_confidence >= self.config.gestures_min_confidence:
                gestures.append(VisionGesture(name="two_fingers", confidence=two_fingers_confidence))
                continue

            if open_hand_confidence >= self.config.gestures_min_confidence:
                gestures.append(VisionGesture(name="open_hand", confidence=open_hand_confidence))
                continue

            if closed_hand_confidence >= self.config.gestures_min_confidence:
                gestures.append(VisionGesture(name="closed_hand", confidence=closed_hand_confidence))
                continue

            confidence_filtered += 1

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

        return [VisionGesture(name=name, confidence=confidence) for name, confidence in deduped.items()], results.multi_hand_landmarks, best_finger_count

    def _build_snapshot(self, objects: List[VisionObject], gestures: List[VisionGesture], finger_count: int) -> VisionSnapshotMessage:
        summary = self._build_scene_summary(objects, gestures, finger_count)
        return VisionSnapshotMessage(
            node_id=self.config.node_id,
            objects=objects,
            gestures=gestures,
            finger_count=finger_count if finger_count >= 0 else None,
            summary=summary,
            trace_id=uuid4().hex,
        )

    def _build_scene_summary(self, objects: Sequence[VisionObject], gestures: Sequence[VisionGesture], finger_count: int) -> str:
        object_names = [item.class_name for item in objects]
        gesture_names = [item.name for item in gestures]
        held_object = self._pick_held_object(objects)
        reference_object = self._pick_reference_object(objects)
        visible_objects = self._visible_scene_objects(objects)

        if "middle_finger" in gesture_names:
            return "A middle finger gesture is visible"
        if "thumbs_up" in gesture_names:
            return "A thumbs up gesture is visible"
        if "thumbs_down" in gesture_names:
            return "A thumbs down gesture is visible"
        if "pinch" in gesture_names:
            if held_object is not None:
                return f"You appear to be holding {self._with_article(held_object.class_name)}"
            return "A pinch gesture is visible"
        if "open_hand" in gesture_names:
            if held_object is not None:
                return f"An open hand is visible near {self._with_article(held_object.class_name)}"
            return "An open hand is visible"
        if "closed_hand" in gesture_names:
            return "A closed hand is visible"
        if "two_fingers" in gesture_names:
            if reference_object is not None:
                return f"You are pointing at {self._with_article(reference_object.class_name)} with two fingers"
            return "A two-finger gesture is visible"
        if finger_count > 0 and not visible_objects:
            return f"I can see {finger_count} finger{'s' if finger_count != 1 else ''} extended"
        if len(visible_objects) == 1:
            return f"I can see {self._with_article(visible_objects[0])}"
        if len(visible_objects) > 1:
            return f"I can see {self._join_object_names(visible_objects[:2])}"
        if len(object_names) == 1:
            return f"I can see {self._with_article(object_names[0])}"
        if len(object_names) > 1:
            return f"I can see {self._join_object_names(object_names[:2])}"
        if gesture_names:
            return f"Detected gesture: {gesture_names[0]}"
        return "No strong visual event detected"

    @staticmethod
    def _pick_held_object(objects: Sequence[VisionObject]) -> VisionObject | None:
        if not objects:
            return None

        for object_ in objects:
            if object_.class_name in HANDHELD_OBJECT_CLASSES:
                return object_

        return None

    @staticmethod
    def _pick_reference_object(objects: Sequence[VisionObject]) -> VisionObject | None:
        held_object = VisionNodeService._pick_held_object(objects)
        if held_object is not None:
            return held_object

        for object_ in objects:
            if object_.class_name not in SCENE_ANCHOR_CLASSES:
                return object_

        return None

    @staticmethod
    def _visible_scene_objects(objects: Sequence[VisionObject]) -> List[str]:
        preferred = [item.class_name for item in objects if item.class_name not in SCENE_ANCHOR_CLASSES]
        if preferred:
            return preferred
        return [item.class_name for item in objects]

    @classmethod
    def _join_object_names(cls, object_names: Sequence[str]) -> str:
        names = [cls._with_article(name) for name in object_names if name]
        if not names:
            return "something"
        if len(names) == 1:
            return names[0]
        if len(names) == 2:
            return f"{names[0]} and {names[1]}"
        return ", ".join(names[:-1]) + f", and {names[-1]}"

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

    def _should_send_keepalive(self,
                               snapshot: VisionSnapshotMessage,
                               now: float,
                               last_sent_at: float) -> bool:
        if last_sent_at <= 0.0 or (now - last_sent_at) < SNAPSHOT_KEEPALIVE_SEC:
            return False
        if snapshot.objects or snapshot.gestures:
            return True
        return snapshot.summary != "No strong visual event detected"

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
    def _distance_2d(first, second) -> float:
        return ((first.x - second.x) ** 2 + (first.y - second.y) ** 2) ** 0.5

    @classmethod
    def _estimate_hand_scale(cls, hand_landmarks) -> float:
        wrist = hand_landmarks.landmark[0]
        middle_mcp = hand_landmarks.landmark[9]
        index_mcp = hand_landmarks.landmark[5]
        pinky_mcp = hand_landmarks.landmark[17]
        return max(
            cls._distance_2d(wrist, middle_mcp),
            cls._distance_2d(index_mcp, pinky_mcp),
            1e-6,
        )

    @classmethod
    def _thumb_extended_confidence(cls, hand_landmarks, handedness_label: str, hand_scale: float) -> float:
        thumb_tip = hand_landmarks.landmark[4]
        thumb_ip = hand_landmarks.landmark[3]
        thumb_mcp = hand_landmarks.landmark[2]
        index_mcp = hand_landmarks.landmark[5]

        radial_distance = cls._distance_2d(thumb_tip, index_mcp) / max(hand_scale, 1e-6)
        if handedness_label == "right":
            horizontal_extension = (thumb_ip.x - thumb_tip.x) / max(hand_scale, 1e-6)
        elif handedness_label == "left":
            horizontal_extension = (thumb_tip.x - thumb_ip.x) / max(hand_scale, 1e-6)
        else:
            horizontal_extension = abs(thumb_tip.x - thumb_ip.x) / max(hand_scale, 1e-6)

        bend_distance = cls._distance_2d(thumb_tip, thumb_mcp) / max(hand_scale, 1e-6)
        return cls._clamp_unit(min(
            (radial_distance - 0.28) / 0.45,
            (horizontal_extension - 0.03) / 0.18,
            (bend_distance - 0.20) / 0.30,
        ))

    @classmethod
    def _thumb_vertical_confidence(cls, hand_landmarks, hand_scale: float, upward: bool) -> float:
        thumb_tip = hand_landmarks.landmark[4]
        thumb_ip = hand_landmarks.landmark[3]
        thumb_mcp = hand_landmarks.landmark[2]
        if upward:
            tip_offset = (thumb_ip.y - thumb_tip.y) / max(hand_scale, 1e-6)
            palm_offset = (thumb_mcp.y - thumb_tip.y) / max(hand_scale, 1e-6)
        else:
            tip_offset = (thumb_tip.y - thumb_ip.y) / max(hand_scale, 1e-6)
            palm_offset = (thumb_tip.y - thumb_mcp.y) / max(hand_scale, 1e-6)
        return cls._clamp_unit(min(
            (tip_offset - 0.05) / 0.20,
            (palm_offset - 0.05) / 0.24,
        ))

    @staticmethod
    def _clamp_unit(value: float) -> float:
        return max(0.0, min(1.0, value))

    @classmethod
    def _finger_extended_confidence(cls, hand_landmarks, tip_idx: int, pip_idx: int, mcp_idx: int, hand_scale: float) -> float:
        tip = hand_landmarks.landmark[tip_idx]
        pip = hand_landmarks.landmark[pip_idx]
        mcp = hand_landmarks.landmark[mcp_idx]
        if not (tip.y < pip.y < mcp.y):
            return 0.0

        vertical_extension = (mcp.y - tip.y) / max(hand_scale, 1e-6)
        tip_gap = (pip.y - tip.y) / max(hand_scale, 1e-6)
        return cls._clamp_unit(min(
            (vertical_extension - 0.18) / 0.45,
            (tip_gap - 0.05) / 0.20,
        ))

    @classmethod
    def _finger_folded_confidence(cls, hand_landmarks, tip_idx: int, pip_idx: int, hand_scale: float) -> float:
        tip = hand_landmarks.landmark[tip_idx]
        pip = hand_landmarks.landmark[pip_idx]
        curl = (tip.y - pip.y) / max(hand_scale, 1e-6)
        return cls._clamp_unit((curl + 0.02) / 0.20)

    @staticmethod
    def _with_article(noun: str) -> str:
        if not noun:
            return "something"
        return f"an {noun}" if noun[0].lower() in {"a", "e", "i", "o", "u"} else f"a {noun}"
