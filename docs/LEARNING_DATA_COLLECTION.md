# Learning Data Collection Layer (Local-First)

## Overview

This module adds a passive, local-only learning data foundation for future model training workflows.

- No cloud upload
- No external telemetry endpoints
- No training pipeline code
- No speaker identification

The implementation is organized under the `learning_data` ownership boundary and integrates into existing authoritative decision points in the orchestration path.

## Ownership and Architecture

Primary module files:

- `src/learning_data/LearningDataCollector.h/.cpp`
- `src/learning_data/LearningDataSettings.h/.cpp`
- `src/learning_data/LearningDataStorage.h/.cpp`
- `src/learning_data/LearningDataTypes.h`

Responsibilities:

- `LearningDataCollector`: asynchronous queueing, feature gating, and orchestration-safe recording API.
- `LearningDataSettings`: snapshot of user-configurable controls from `AppSettings`.
- `LearningDataStorage`: filesystem layout, append-only JSONL writes, WAV persistence, retention, export manifests, diagnostics.
- `LearningDataTypes`: schema-versioned typed events and JSON serialization helpers.

### Behavior Preservation

The collector is passive. Existing routing and reasoning behavior is unchanged. If collection is disabled, all record operations are no-op.

## Local Storage Layout

All data is stored under the app-local root:

`<AppData>/learning/`

Structure:

```text
learning/
  schema_version.json
  index/
    sessions/YYYY/MM/DD.jsonl
    audio_index/YYYY/MM/DD.jsonl
    asr_events/YYYY/MM/DD.jsonl
    tool_decision_events/YYYY/MM/DD.jsonl
    tool_execution_events/YYYY/MM/DD.jsonl
    behavior_events/YYYY/MM/DD.jsonl
    memory_events/YYYY/MM/DD.jsonl
    feedback_events/YYYY/MM/DD.jsonl
  audio/
    YYYY/
      MM/
        session_<id>/
          turn_<id>/
            <audio_role>_<session>_<timestamp>_<event>.wav
  exports/
    export_YYYYMMDD_HHMMSS/
      export_audio_manifest.jsonl
      export_tool_policy_manifest.jsonl
      export_behavior_policy_manifest.jsonl
      export_memory_policy_manifest.jsonl
  quarantine/
    write_failures.jsonl
  retention/
    tombstones.jsonl
  snapshots/
```

## Settings and Controls

Added `AppSettings` controls:

- `enable_learning_data_collection`
- `enable_audio_collection`
- `enable_transcript_collection`
- `enable_tool_logging`
- `enable_behavior_logging`
- `enable_memory_logging`
- `max_audio_storage_gb`
- `max_days_to_keep_audio`
- `max_days_to_keep_structured_logs`
- `allow_export_prepared_datasets`

Defaults are privacy-safe (all collection flags disabled by default).

## Event Schemas (JSON Examples)

All records include `schema_version`.

### SessionEvent

```json
{
  "schema_version": "1.0.0",
  "event_kind": "session",
  "session_id": "session_1713430000000_abc123",
  "started_at": "2026-04-18T20:01:12.334Z",
  "ended_at": "",
  "app_version": "1.0.0",
  "device_info": {
    "cpu_arch": "x86_64",
    "kernel_type": "winnt"
  },
  "collection_enabled": true,
  "audio_collection_enabled": true,
  "transcript_collection_enabled": true,
  "tool_logging_enabled": true,
  "behavior_logging_enabled": true,
  "memory_logging_enabled": true
}
```

### AudioCaptureEvent

```json
{
  "schema_version": "1.0.0",
  "event_kind": "audio_capture",
  "session_id": "session_...",
  "turn_id": "14",
  "event_id": "audio_1713430011111_a1b2c3d4",
  "timestamp": "2026-04-18T20:01:51.110Z",
  "audio_role": "command_raw",
  "file_path": "audio/2026/04/session_session_.../turn_14/command_raw_....wav",
  "duration_ms": 1630,
  "sample_rate": 16000,
  "channels": 1,
  "sample_format": "pcm_s16le",
  "capture_source": "default_mic",
  "voice_activity_detected": true,
  "wake_word_detected": true,
  "collection_reason": "direct_command_capture",
  "label_status": "assumed_owner",
  "notes": "capture_mode=direct",
  "file_hash_sha256": "..."
}
```

### AsrEvent

```json
{
  "schema_version": "1.0.0",
  "event_kind": "asr",
  "session_id": "session_...",
  "turn_id": "14",
  "event_id": "asr_1713430014000_d1e2f3g4",
  "timestamp": "2026-04-18T20:01:54.000Z",
  "stt_engine": "whisper_cpp",
  "source_audio_event_id": "audio_1713430011111_a1b2c3d4",
  "raw_transcript": "open calendar",
  "normalized_transcript": "open calendar",
  "final_transcript": "open calendar",
  "transcript_source": "raw_asr",
  "language": "en",
  "confidence": 0.94,
  "was_user_edited": false,
  "transcript_changed": false
}
```

### ToolDecisionEvent

```json
{
  "schema_version": "1.0.0",
  "event_kind": "tool_decision",
  "session_id": "session_...",
  "turn_id": "14",
  "event_id": "tool_decision_...",
  "timestamp": "2026-04-18T20:01:55.100Z",
  "user_input_text": "open calendar",
  "input_mode": "voice",
  "available_tools": ["calendar", "notes"],
  "selected_tool": "calendar",
  "candidate_tools_with_scores": [
    {"tool_name": "calendar", "score": 0.95},
    {"tool_name": "notes", "score": 0.22}
  ],
  "decision_source": "heuristic_policy",
  "expected_confirmation_level": "none",
  "no_tool_option_considered": false,
  "notes": "route_kind=deterministic_tasks"
}
```

### ToolExecutionEvent

```json
{
  "schema_version": "1.0.0",
  "event_kind": "tool_execution",
  "session_id": "session_...",
  "turn_id": "14",
  "event_id": "tool_exec_...",
  "timestamp": "2026-04-18T20:01:56.000Z",
  "selected_tool": "calendar",
  "tool_args_redacted": {"query": "tomorrow"},
  "execution_started_at": "2026-04-18T20:01:56.000Z",
  "execution_finished_at": "2026-04-18T20:01:56.245Z",
  "latency_ms": 245,
  "succeeded": true,
  "failure_type": "none",
  "retried": false,
  "retry_count": 0,
  "user_corrected_tool_choice": false,
  "final_outcome_label": "good"
}
```

### BehaviorDecisionEvent

```json
{
  "schema_version": "1.0.0",
  "event_kind": "behavior_decision",
  "session_id": "session_...",
  "turn_id": "14",
  "event_id": "behavior_...",
  "timestamp": "2026-04-18T20:01:55.100Z",
  "response_mode": "concise_report",
  "why_selected": "low risk deterministic task",
  "interrupted_user": false,
  "follow_up_attempted": true,
  "follow_up_helpful_label": "unknown",
  "verbosity_level": "concise",
  "speaking_duration_ms": 0
}
```

### MemoryDecisionEvent

```json
{
  "schema_version": "1.0.0",
  "event_kind": "memory_decision",
  "session_id": "session_...",
  "turn_id": "14",
  "event_id": "memory_...",
  "timestamp": "2026-04-18T20:01:55.700Z",
  "memory_candidate_present": true,
  "memory_action": "save",
  "memory_type": "preference",
  "confidence": 0.88,
  "privacy_risk_level": "low",
  "was_user_confirmed": true,
  "outcome_label": "saved"
}
```

### UserFeedbackEvent

```json
{
  "schema_version": "1.0.0",
  "event_kind": "user_feedback",
  "session_id": "session_...",
  "turn_id": "14",
  "event_id": "feedback_...",
  "timestamp": "2026-04-18T20:02:05.100Z",
  "feedback_type": "too_verbose",
  "linked_event_ids": ["behavior_...", "tool_decision_..."],
  "freeform_text": "Shorter answers please",
  "severity": "normal"
}
```

## Integration Points (Authoritative Existing Flow)

The implementation uses existing decision points in `AssistantController` with no duplicate policy logic:

- command audio finalized: `inputCaptureFinished` callback
- ASR completion: `SpeechRecognizer::transcriptionReady`
- tool decision resolved: route execution path after `buildToolPlan`/trust evaluation
- tool execution result: `ToolCoordinator` observer and background result handling
- behavior mode chosen: action session response mode selection path
- memory decision: `MemoryPolicyHandler` decision callback
- feedback/correction: proactive feedback and confirmation handling paths

## Retention and Safety

Retention runs are best-effort and non-fatal:

- delete old audio files by age
- enforce audio storage cap by deleting oldest clips first
- delete old structured log shards by age
- append tombstones to `retention/tombstones.jsonl`
- append write failures to `quarantine/write_failures.jsonl`

No deletion failure crashes the assistant.

## Diagnostics and Reviewability

`LearningDataStorage::collectDiagnostics()` provides:

- sessions
- audio clip count
- tool decision count
- tool execution count
- behavior decision count
- memory decision count
- feedback count
- approximate disk usage bytes

Maintenance logs diagnostics via the existing logging channel.

## Limitations

- Cross-event joins in exports are currently turn/session based and intentionally lightweight.
- No UI dashboard is added yet; review is file-based and log-based.
- Speaker labels remain non-ground-truth (`unlabeled`, `assumed_owner`, etc.) by design.

## Recommended Next Steps

1. Add optional internal debug command in UI/backend to surface diagnostics and latest export path.
2. Add optional transcript correction hook from UI to emit `wrong_transcript` and corrected text payloads.
3. Add schema migration utility for future `schema_version` upgrades.
4. Add unit tests for more edge WAV formats if additional capture formats are introduced.
