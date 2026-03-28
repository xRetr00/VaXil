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
pip install --index-url https://download.pytorch.org/whl/cpu torch torchvision
pip install -r requirements.txt
```

The vision node intentionally uses a CPU-only PyTorch install by default, and `requirements.txt` pins MediaPipe to a legacy-solutions-compatible version for the current hand-tracking code path.

## Run

```bash
python main.py --server-url ws://MAIN-PC-IP:8765/vision --node-id laptop-vision-node
```

Interactive launcher:

```bash
./run_vision_node.sh
```

Non-interactive launcher:

```bash
./run_vision_node.sh --server-url ws://MAIN-PC-IP:8765/vision --debug-ui
```

Debug mode with overlays:

```bash
python main.py --server-url ws://MAIN-PC-IP:8765/vision --node-id laptop-vision-node --debug-ui
```

Fullscreen debug mode with decoupled inference/display resolutions:

```bash
python main.py --server-url ws://MAIN-PC-IP:8765/vision --debug-ui --fullscreen --process-width 640 --process-height 480 --display-width 1280 --display-height 720
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
- `--process-width 640`
- `--process-height 480`
- `--display-width 1280`
- `--display-height 720`
- `--fullscreen`
- `--debug-skip-frames 1`
- `--pip-index-url https://pypi.org/simple`
- `--torch-index-url https://download.pytorch.org/whl/cpu`
- `--debug-ui`
- `./run_vision_node.sh --non-interactive --server-url ws://MAIN-PC-IP:8765/vision`

## Notes

- The Windows main PC hosts the WebSocket server.
- The laptop node reconnects automatically if the network drops or the main PC restarts.
- Semantic snapshots are confidence-filtered, delta-filtered, and rate-limited before send; this node does not stream raw frames.
- The debug UI is optional and headless mode remains the default.
- YOLO and MediaPipe always run on the low-resolution process frame. The debug window can scale independently, including fullscreen, without increasing inference resolution.
- `run_vision_node.sh` installs a CPU-only PyTorch stack by default before `ultralytics`, even if CUDA is present on the machine. Override `--torch-index-url` only if you explicitly want a GPU build.
- `run_vision_node.sh` can create a local virtualenv, install missing requirements, retry failed installs with a safer PyPI fallback, and prompt for missing required inputs such as the server URL.
- Current server-side raw gesture outputs include `open_hand`, `closed_hand`, `pinch`, `two_fingers`, `middle_finger`, `thumbs_up`, and `thumbs_down`.
- Snapshots also include optional `finger_count` when the hand estimate is reliable enough.
