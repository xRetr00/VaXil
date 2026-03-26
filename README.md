# J.A.R.V.I.S

<p align="center">
  <img src="assets/logo.png" alt="J.A.R.V.I.S logo" width="300" />
</p>


Local-first desktop voice assistant built with Qt 6 and modern C++.

Jarvis runs in the system tray, listens for a wake phrase, transcribes speech locally, routes requests to a local OpenAI-compatible backend, and speaks replies through local TTS.

## Highlights

- Local-first voice pipeline:
  - Wake word detection with Sherpa ONNX keyword spotting (default) or Mycroft Precise
  - STT via whisper.cpp (runtime worker path)
  - TTS via Piper with worker-based playback
- OpenAI-compatible AI backend support (LM Studio style endpoint)
- Agent mode controls for tool-aware conversations and background task dispatch
- Tool and model discovery/download workflow in Settings
- Animated overlay UI with tray-first workflow
- First-run setup wizard for identity, AI, voice, and wake tuning
- Runtime settings window with live requirement checks
- Per-exchange AI logs in bin/logs/AI
- Windows global hotkey: Ctrl+Alt+J

## Tech Stack

- C++20
- Qt 6 (Concurrent, Core, Gui, Network, Multimedia, Quick, QuickControls2, Svg, Widgets, Test)
- CMake + Ninja
- nlohmann/json
- spdlog
- libfvad
- Optional local speech stack components:
  - ONNX Runtime
  - sherpa-onnx + sentencepiece
  - SpeexDSP
  - RNNoise

## Project Layout

- src/app: app bootstrap, tray lifecycle, window orchestration
- src/core: assistant state machine, intent routing, local response engine, task dispatch
- src/agent: tool schema/registry for agent-capable requests
- src/ai: prompt building, model discovery, streaming assembly, backend clients
- src/audio: microphone capture, processing chain, VAD integration surface
- src/stt: speech recognizer interfaces and runtime recognizer
- src/tts: speech synthesis engines and worker TTS integration
- src/wakeword: wake phrase engines (Sherpa + Precise)
- src/workers: threaded runtime workers for speech I/O and AI backend
- src/gui: Qt/QML facade and overlay/settings/setup windows
- src/settings: runtime settings and identity/profile persistence
- src/tools: external tool and model discovery/download workflow
- src/memory: local memory store and manager
- src/skills: local skill manifest store
- tests: Qt unit tests
- docs: build and architecture documentation


## Requirements

- Windows 10/11
- Qt 6.6+ (tested with Qt 6.10.2 msvc2022_64)
- CMake 3.27+
- Ninja
- Visual Studio 2022 Build Tools (MSVC x64)

For full build details, see docs/BUILD.md.

## Quick Start (Windows)

1. Configure environment:
   - Set QT_DIR to your Qt MSVC kit path (optional if installed in default path).
   - Set VC_VARS_BAT if your vcvars64.bat is in a non-default location.
2. Build and test:

```bat
build.bat
```

3. Run:

- Executable: bin/jarvis.exe
- Logs: bin/logs

## First Run

On first launch, the Setup wizard opens automatically.

1. Profile:
  - user_name used in UI text and voice replies
2. AI Core:
   - endpoint (for example http://localhost:1234)
   - model selection
3. Voice Pipeline:
   - whisper executable + model
   - piper executable + voice model
   - ffmpeg path
4. Wake Word:
  - wake engine kind (sherpa-onnx or precise)
  - precise-engine executable + wake model path (.pb) when using Precise
  - sherpa keyword-spotting model root when using sherpa-onnx
   - threshold and cooldown tuning
5. Final Check:
   - run setup tests and complete setup

## Runtime Data and Paths

- Settings file:
  - %APPDATA%/../Local/<Org>/<App>/settings.json (Qt AppDataLocation)
- Local tools root:
  - %APPDATA%/../Local/<Org>/<App>/third_party
- Download cache root:
  - %APPDATA%/../Local/<Org>/<App>/tools
- Identity:
  - config/identity.json
- User profile:
  - config/user_profile.json
- Main logs:
  - bin/logs/jarvis.log
- Startup logs:
  - bin/logs/startup.log
- AI exchange logs:
  - bin/logs/AI/*.log

## Testing

Run tests through build script:

```bat
build.bat
```

Or directly:

```powershell
ctest --test-dir build --output-on-failure
```

Current test suites:

- tests/ReasoningRouterTests.cpp
- tests/AiServicesTests.cpp
- tests/IdentityProfileTests.cpp
- tests/LocalResponseEngineTests.cpp

## Current Defaults

- Wake engine: sherpa-onnx
- TTS engine: piper
- Audio processing:
  - AEC enabled
  - RNNoise disabled
  - VAD sensitivity 0.55
- Request timeout: 12000 ms
- Reasoning mode: balanced (with auto-routing enabled)

## Documentation

- docs/BUILD.md
- docs/ARCHITECTURE.md
- docs/CONFIGURATION.md
- docs/RELEASE_v1.0.0.md
- CHANGELOG.md
