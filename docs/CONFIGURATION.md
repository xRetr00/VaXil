# Configuration Guide

## Identity Files

### config/identity.json

Defines assistant persona:

- assistant_name
- personality
- tone
- addressing_style

### config/user_profile.json

Defines user identity and preferences:

- user_name: used for UI text and spoken addressing
- preferences: free-form object

## Runtime Settings File

AppSettings persists to:

- Qt AppDataLocation/settings.json

Key fields include:

- chatBackendEndpoint / lmStudioEndpoint
- selectedModel
- defaultReasoningMode
- autoRoutingEnabled
- streamingEnabled
- requestTimeoutMs
- whisperExecutable / whisperModelPath
- piperExecutable / piperVoiceModel
- ffmpegExecutable
- wakeEngineKind
- preciseEngineExecutable / preciseModelPath
- preciseTriggerThreshold
- preciseTriggerCooldownMs
- vadSensitivity
- micSensitivity
- selectedAudioInputDeviceId / selectedAudioOutputDeviceId
- clickThroughEnabled
- initialSetupCompleted

## Guardrails and Value Ranges

Current clamps in code:

- preciseTriggerThreshold: 0.30 to 0.85
- preciseTriggerCooldownMs: 600 to 900 ms
- vadSensitivity: 0.05 to 0.95
- voiceSpeed: 0.85 to 0.92
- voicePitch: 0.90 to 0.97

## Recommended Local Backend Setup

For LM Studio compatible mode:

- Endpoint: http://localhost:1234
- API paths used:
  - /v1/models
  - /v1/chat/completions

## Runtime Logs

- bin/logs/startup.log
- bin/logs/jarvis.log
- bin/logs/AI/*.log

If behavior looks wrong, inspect these logs first.
