# J.A.R.V.I.S

Local-first desktop voice assistant built with Qt 6 and modern C++.

J.A.R.V.I.S runs in the system tray, listens for a wake phrase, transcribes speech locally, routes requests to a local OpenAI-compatible backend, and speaks replies through local TTS.

## Highlights

- Local-first voice pipeline:
  - Wake word detection with Mycroft Precise
  - STT via whisper.cpp
  - TTS via Piper (or qwen3-tts mode)
- OpenAI-compatible AI backend support (LM Studio style endpoint)
- Animated overlay UI with tray-first workflow
- First-run setup wizard for identity, AI, voice, and wake tuning
- Runtime settings window with live requirement checks
- Per-exchange AI logs in bin/logs/AI
- Windows global hotkey: Ctrl+Alt+J

## Tech Stack

- C++20
- Qt 6 (Core, Gui, Network, Multimedia, Quick, QuickControls2, Svg, Widgets)
- CMake + Ninja
- nlohmann/json
- spdlog
- libfvad

## Project Layout

- src/app: app bootstrap, tray lifecycle, window orchestration
- src/core: assistant state machine and orchestration
- src/ai: prompt building, model discovery, streaming assembly, backend clients
- src/audio: microphone capture and VAD preprocessing
- src/stt: speech recognition integration
- src/tts: speech synthesis engines
- src/wakeword: wake phrase engines
- src/gui: Qt/QML facade and overlay/settings/setup windows
- src/settings: runtime settings and identity/profile persistence
- src/tools: external tool and model discovery/download workflow
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
   - precise-engine executable
   - wake model path (.pb)
   - threshold and cooldown tuning
5. Final Check:
   - run setup tests and complete setup

## Runtime Data and Paths

- Settings file:
  - %APPDATA%/../Local/<Org>/<App>/settings.json (Qt AppDataLocation)
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

## Documentation

- docs/BUILD.md
- docs/ARCHITECTURE.md
- docs/CONFIGURATION.md
- docs/RELEASE_v1.0.0.md
- CHANGELOG.md

## Version

This repository is prepared for release v1.0.0.
