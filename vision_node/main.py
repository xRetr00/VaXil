from __future__ import annotations

import argparse
import asyncio
import logging
import signal

from service import VisionNodeConfig, VisionNodeService


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
    parser.add_argument("--process-width", type=int, default=640, help="Inference frame width")
    parser.add_argument("--process-height", type=int, default=480, help="Inference frame height")
    parser.add_argument("--display-width", type=int, default=1280, help="Debug window frame width")
    parser.add_argument("--display-height", type=int, default=720, help="Debug window frame height")
    parser.add_argument("--fullscreen", action="store_true", help="Show debug window fullscreen")
    parser.add_argument("--debug-skip-frames", type=int, default=1, help="Render every Nth debug frame")
    parser.add_argument("--debug-ui", action="store_true", help="Show lightweight local debug overlays")
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
        process_width=args.process_width,
        process_height=args.process_height,
        display_width=args.display_width,
        display_height=args.display_height,
        fullscreen=args.fullscreen,
        debug_skip_frames=args.debug_skip_frames,
        debug_ui=args.debug_ui,
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
