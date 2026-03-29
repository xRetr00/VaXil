# Vaxil Architecture Overview

## Runtime Model

Vaxil is a tray-first desktop assistant with three main windows loaded by a single QQmlApplicationEngine:

- OverlayWindow: ambient orb, status, toast feedback
- SettingsWindow: full runtime configuration
- SetupWizard: first-run workflow

Core orchestration starts in src/app/JarvisApplication.cpp and wires all services through dependency injection.

## High-Level Flow

1. App bootstrap
  - Loads settings (AppSettings)
  - Loads identity/profile (IdentityProfileService)
  - Initializes logging (LoggingService)
2. Core controller startup
  - AssistantController initializes AI, STT, TTS, wake, memory, skills, and routing services
3. Runtime threading startup
  - VoicePipelineRuntime starts dedicated workers on separate QThreads:
    - SpeechInputWorker
    - SpeechIoWorker
    - AiBackendWorker
4. UI binding
  - BackendFacade exposes controller/settings/profile properties and invokables to QML
5. Runtime interaction
  - Wake engine or manual trigger starts capture
  - STT transcribes audio
  - Intent router decides local response vs AI request
  - PromptAdapter builds contextual prompt
  - AI backend streams response
  - StreamAssembler emits sentence chunks
  - TTS speaks output and overlay reflects state

## Core Components

### Application Layer

- JarvisApplication:
  - Loads QML windows
  - Configures tray icon and hotkey
  - Coordinates setup-complete transition into wake monitoring

### Controller Layer

- AssistantController:
  - Central state machine for idle/listening/processing/speaking
  - Manages duplex rules and wake resume timing
  - Routes local responses and AI conversations
  - Routes agent-mode requests and background tool tasks
  - Persists settings changes through AppSettings

### AI Layer

- PromptAdapter:
  - Builds conversation and command extraction prompts
  - Injects identity, profile, preferences, and runtime context
- RuntimeAiBackendClient / OpenAiCompatibleClient / LmStudioClient:
  - OpenAI-compatible API handling
  - Model fetch and chat completions (streaming and non-streaming)
- ReasoningRouter:
  - Chooses fast/balanced/deep mode from input
- StreamAssembler:
  - Chunks stream into sentence-ready segments

### Agent and Task Layer

- AgentToolbox:
  - Builds the available tool schema for provider requests
- TaskDispatcher / ToolWorker:
  - Executes background tasks emitted from agent outputs
  - Tracks task lifecycle (pending, running, finished, canceled, expired)
- MemoryManager / MemoryStore:
  - Stores lightweight memory records and preference-like context
- SkillStore:
  - Loads and tracks local installed skill manifests

### Voice Layer

- SherpaWakeWordEngine:
  - Default wake engine path
  - Starts `jarvis_sherpa_wake_helper` and consumes partial/final transcript events
- SherpaWakeWordEngine:
  - Only wake path used by the application
  - Uses streaming Sherpa transcription plus transcript-based wake matching
- AudioInputService:
  - Captures PCM audio for transcription with VAD framing
- AudioProcessingChain:
  - Applies configurable processing stages (AEC/RNNoise toggles and VAD thresholding)
- WhisperSttEngine:
  - Invokes whisper.cpp for transcription
- RuntimeSpeechRecognizer:
  - Bridges SpeechRecognizer calls into worker runtime
- PiperTtsEngine / WorkerTtsEngine:
  - Local speech synthesis and output handling through runtime worker path

### Settings and Identity

- AppSettings:
  - Reads/writes settings.json in Qt AppDataLocation
  - Applies clamps for sensitivity, threshold, temperature, token, and voice controls
- IdentityProfileService:
  - Reads/writes config/identity.json and config/user_profile.json
  - Supports assistant identity fields and username preferences

### Tooling Layer

- ToolManager:
  - Scans installed runtime tools/models
  - Downloads known assets and reports progress
  - Tracks tool state for runtime settings panel

## UI Binding

BackendFacade is the only QML bridge surface. It exposes:

- Reactive properties (state, transcript, models, settings, agent status, task status)
- Runtime actions (start listening, refresh models, save settings, cancel requests)
- Setup actions (completeInitialSetup, runSetupScenario, evaluateSetupRequirements)
- Tool and skill actions (rescan/download/install/create)

This keeps QML declarative and pushes behavior to C++.

## Logging and Observability

- startup.log: bootstrap and Qt log handler output
- vaxil.log: rotating structured app logs via spdlog
- logs/AI/*.log: one file per prompt/response exchange

Agent traces and background-task results are also surfaced through BackendFacade for UI diagnostics.

## Test Coverage Snapshot

- ReasoningRouterTests: route selection behavior
- AiServicesTests: prompt, stream assembly, spoken reply sanitation
- IdentityProfileTests: identity/profile loading
- LocalResponseEngineTests: intent mapping and response quality guards

Build-time test targets are created under CMake as jarvis_reasoning_tests, jarvis_ai_services_tests, jarvis_identity_tests, and jarvis_local_response_tests.
