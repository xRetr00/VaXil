# Vision Node

Standalone laptop-side Python service for distributed JARVIS vision ingestion.

## Responsibilities

- capture frames from a local camera
- run YOLOv8n object detection every `N` frames
- run MediaPipe Hands every frame
- fuse detections into a semantic snapshot
- send snapshots over WebSocket to the main PC

## Install

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Run

```bash
python main.py --server-url ws://MAIN-PC-IP:8765/vision --node-id laptop-vision-node
```

Useful flags:

- `--camera-index 0`
- `--fps 12`
- `--send-interval-ms 120`
- `--max-snapshots-per-second 6`
- `--yolo-every-n-frames 4`
- `--model-name yolov8n.pt`
- `--objects-min-confidence 0.60`
- `--gestures-min-confidence 0.70`
- `--delta-threshold 0.12`

## Notes

- The Windows main PC hosts the WebSocket server.
- The laptop node reconnects automatically if the network drops or the main PC restarts.
- Semantic snapshots are confidence-filtered, delta-filtered, and rate-limited before send; this node does not stream raw frames.
