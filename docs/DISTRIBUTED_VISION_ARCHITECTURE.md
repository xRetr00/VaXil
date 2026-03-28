# Distributed Vision Architecture

## Text Diagram

```text
+-----------------------------+         WebSocket semantic stream         +-----------------------------+
| Laptop / Linux              |  ------------------------------------->  | Main PC / Windows           |
|                             |                                           |                             |
| vision_node/main.py         |                                           | AssistantController         |
|  - OpenCV camera capture    |                                           |  - existing wake/STT/LLM/TTS|
|  - YOLOv8n every N frames   |                                           |  - optional vision context  |
|  - MediaPipe Hands/frame    |                                           |                             |
|  - semantic fusion          |                                           | VisionIngestService         |
|  - throttled snapshot send  |                                           |  - threaded WS server       |
|  - reconnect on failure     |                                           |  - stale-drop validation    |
+-----------------------------+                                           |  - latest-only dispatch     |
                                                                          |                             |
                                                                          | WorldStateCache             |
                                                                          |  - recent snapshot window   |
                                                                          |  - freshness checks         |
                                                                          |  - filtered summary         |
                                                                          +-----------------------------+
```

## Design Choice

The main PC hosts the WebSocket server.

Reasons:

- The assistant core is the long-lived authority, so it is the stable endpoint.
- The laptop vision node can reconnect autonomously after network or process failures.
- This keeps the Windows assistant passive when vision is disabled or offline.
- `vision.endpoint` on the C++ side maps naturally to a listen address such as `ws://0.0.0.0:8765/vision`.

## File And Module Changes

### C++ / Qt6

- `src/core/AssistantTypes.h`
  - added `VisionObjectDetection`
  - added `VisionGestureDetection`
  - added `VisionSnapshot`
- `src/vision/VisionIngestService.h`
- `src/vision/VisionIngestService.cpp`
- `src/vision/GestureInterpreter.h`
- `src/vision/GestureInterpreter.cpp`
- `src/vision/VisionContextGate.h`
- `src/vision/VisionContextGate.cpp`
- `src/vision/WorldStateCache.h`
- `src/vision/WorldStateCache.cpp`
- `src/core/AssistantController.h`
- `src/core/AssistantController.cpp`
- `src/ai/PromptAdapter.h`
- `src/ai/PromptAdapter.cpp`
- `src/settings/AppSettings.h`
- `src/settings/AppSettings.cpp`
- `src/logging/LoggingService.h`
- `src/logging/LoggingService.cpp`
- `tests/VisionStateTests.cpp`

### Python / Laptop Node

- `vision_node/main.py`
- `vision_node/service.py`
- `vision_node/schema.py`
- `vision_node/requirements.txt`
- `vision_node/README.md`

### Schema / Docs

- `docs/VISION_MESSAGE_SCHEMA.json`
- `docs/DISTRIBUTED_VISION_ARCHITECTURE.md`

## Message Schema

Primary message:

```json
{
  "type": "vision.snapshot",
  "version": "1.0",
  "ts": "2026-03-28T10:15:30.123Z",
  "trace_id": "74b4f47b5e8c42c8a3af3f6ce9a3d365",
  "node_id": "laptop-vision-node",
  "objects": [
    { "class": "can", "confidence": 0.87 }
  ],
  "gestures": [
    { "name": "pinch", "confidence": 0.92 }
  ],
  "summary": "User appears to be holding a can"
}
```

Contract notes:

- `type` is fixed to `vision.snapshot`
- `version` is fixed to `1.0`
- `ts` is UTC ISO-8601
- `trace_id` is optional but recommended for correlation
- only semantic outputs cross the network, never raw frames

## C++ Class Definitions

### `VisionSnapshot`

- location: `src/core/AssistantTypes.h`
- role: strongly typed equivalent of the wire schema

Fields:

- `type`
- `schemaVersion`
- `timestamp`
- `nodeId`
- `traceId`
- `objects`
- `gestures`
- `summary`

### `VisionIngestService`

- location: `src/vision/VisionIngestService.*`
- role: optional threaded ingest boundary

Responsibilities:

- host a WebSocket server in its own thread
- validate JSON payloads
- drop stale snapshots
- confidence-filter weak detections at ingest as a safety net
- coalesce multiple inbound messages into latest-only dispatch
- emit `visionSnapshotReceived(VisionSnapshot)`

### `GestureInterpreter`

- location: `src/vision/GestureInterpreter.*`
- role: map raw gestures to semantic actions without changing transport schema

Responsibilities:

- map `pinch -> click`
- map `open_hand/open_palm -> cancel`
- map `two_fingers -> scroll`
- emit Qt signals for interpreted actions

### `VisionContextGate`

- location: `src/vision/VisionContextGate.*`
- role: strict gate for whether vision reaches the LLM prompt

Rules:

- inject only for vision-relevant questions
- or when a recent gesture action happened
- or when explicit always-on vision context is enabled
- prefer scene summary over raw detections

### `WorldStateCache`

- location: `src/vision/WorldStateCache.*`
- role: short retention cache for recent visual state

Responsibilities:

- store snapshots for a bounded time window
- expose latest snapshot
- reject stale snapshots on ingest
- expose freshness check via `isFresh(max_age_ms)`
- expose filtered summary for prompt enrichment

### `AssistantController` integration

- subscribes to `VisionIngestService`
- appends snapshots into `WorldStateCache`
- logs incoming vision events
- optionally maps explicit gesture command names:
  - `start_listening`
  - `stop_listening`
  - `cancel_request`
- injects vision context into prompts only when relevant

### `PromptAdapter` integration

Extended methods:

- `buildConversationMessages(..., visionContext)`
- `buildHybridAgentMessages(..., visionContext)`
- `buildAgentInstructions(..., visionContext)`
- `buildAgentWorldContext(..., visionContext)`

Behavior:

- no vision context if cache is empty, stale, or irrelevant
- short sensory summary only, never raw detection dumps

## Python Class Definitions

### `VisionNodeConfig`

- runtime configuration for:
  - camera index
  - target FPS
  - send interval
  - YOLO frame interval
  - reconnect delay
  - server URL
  - model name

### `VisionNodeService`

Responsibilities:

- maintain camera lifecycle
- run YOLO every `N` frames
- run MediaPipe Hands every frame
- filter weak objects and gestures before send
- derive simple gestures such as `pinch`, `open_hand`, and `two_fingers`
- produce semantic scene summaries
- delta-filter unchanged snapshots
- hard-cap snapshot rate
- reconnect on transport failure

### `VisionSnapshotMessage`

- typed wire message builder
- inserts:
  - `type`
  - `version`
  - `ts`
  - `trace_id`
  - `node_id`
  - semantic arrays
  - summary

## Runtime Flow

1. Main PC starts `VisionIngestService`.
2. If `vision.enabled=false`, the service remains inactive and the assistant behaves exactly as before.
3. Laptop node connects to the WebSocket endpoint.
4. Laptop sends throttled `vision.snapshot` messages.
5. `VisionIngestService` validates payloads and drops stale messages.
6. Latest snapshot is emitted to `AssistantController`.
7. `AssistantController` updates `WorldStateCache`.
8. When the user asks about the environment, `PromptAdapter` receives a short `visionContext`.
9. Wake, STT, LLM, and TTS continue unchanged.

## Step-By-Step Implementation Plan

1. Add typed assistant-side snapshot structs and settings keys.
2. Add a dedicated vision ingest module with its own thread boundary.
3. Add a bounded cache so prompt construction reads state locally and cheaply.
4. Integrate optional prompt enrichment in `AssistantController` only at request build time.
5. Keep gesture-trigger support explicit and opt-in via semantic gesture names.
6. Add the Python laptop node with detection, fusion, throttling, and reconnect behavior.
7. Validate with build, unit tests, and live node-to-PC smoke testing.

## Risk Analysis

### 1. Network Instability

- Risk: intermittent Wi-Fi or node restarts.
- Mitigation: Python node reconnect loop, C++ server-side optional behavior, stale-drop policy.

### 2. Queue Buildup

- Risk: ingest thread overwhelms the UI or controller thread.
- Mitigation: latest-only coalescing and timed dispatch from `VisionIngestService`.

### 3. Prompt Pollution

- Risk: vision state gets injected into every request and degrades answers.
- Mitigation: `AssistantController::shouldUseVisionContext(...)` only attaches vision for relevant environment/object/gesture queries.

### 4. False Gesture Triggers

- Risk: accidental commands from noisy hand tracking.
- Mitigation: only explicit semantic command gestures trigger actions; generic gestures like `pinch` are context only.

### 5. Clock Skew / Stale Data

- Risk: snapshots arrive late or with bad timestamps.
- Mitigation: strict timestamp validation and `vision.stale_threshold_ms`.

### 6. Operational Complexity

- Risk: distributed deployment increases troubleshooting cost.
- Mitigation: structured schema, trace IDs, rate-limited vision logs, isolated laptop node process.
