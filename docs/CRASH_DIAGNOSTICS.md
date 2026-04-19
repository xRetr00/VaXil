# Crash And Error Diagnostics

## What Was Added

Vaxil now has a focused crash diagnostics layer that extends the existing logging architecture.

Core additions:
- `src/diagnostics/CrashDiagnosticsService.*`
- `src/diagnostics/CrashReportWriter.*`
- `src/diagnostics/CrashBreadcrumbTrail.*`
- `src/diagnostics/VaxilErrorCodes.*`
- `src/diagnostics/StartupMilestones.*`

The logging system was extended (not replaced) to support explicit `FATAL` and `CRASH` severity paths, critical flush helpers, and breadcrumb/runtime-context forwarding.

## Crash Artifact Output Paths

Artifacts are written under the existing logs root (`bin/logs`):
- Crash reports: `bin/logs/crash/crash_YYYYMMDD_HHMMSS.log`
- Breadcrumb snapshots: `bin/logs/crash/breadcrumbs_YYYYMMDD_HHMMSS.log`
- Windows minidumps: `bin/logs/dumps/vaxil_YYYYMMDD_HHMMSS.dmp`
- Crash severity stream: `bin/logs/crash.log`

Wake helper process uses the same convention and writes into its own `logs` root relative to the helper executable.

## Error Code Format

Error codes follow this format:
- `VAXIL-<MODULE>-<NNNN>`

Examples:
- `VAXIL-CORE-0001`
- `VAXIL-LOG-0001`
- `VAXIL-TTS-0004`
- `VAXIL-WAKE-0009`
- `VAXIL-CRASH-0001`

Code registry and mapping are owned by `src/diagnostics/VaxilErrorCodes.*`.

Use from code:
- `VaxilErrorCodes::forKey(...)`
- `VaxilErrorCodes::compose("MODULE", 1)`
- `VaxilErrorCodes::fromToolErrorKindValue(...)`

Fatal and unhandled crash paths emit a stable crash error code.

## Breadcrumb Usage

Breadcrumbs are lightweight ring-buffer entries intended for major runtime transitions.

Use from existing logging service:
- `LoggingService::breadcrumb(module, event, detail, traceId, sessionId)`
- `LoggingService::setRuntimeContext(module, route, tool, traceId, sessionId, threadId)`

Recommended events:
- startup phases
- route decision begin/end
- tool call begin/end
- planner/gate decision points
- wake and voice critical transitions

Avoid high-frequency per-frame/per-token breadcrumb spam.

## Qt And Crash Integration

Qt diagnostics are integrated through `qInstallMessageHandler` in `src/app/main.cpp`.

On Qt fatal messages:
- crash diagnostics capture is invoked
- crash report and breadcrumb snapshot are persisted
- process abort behavior remains unchanged

Process-level handlers include:
- `std::terminate`
- fatal signal handling where applicable
- Windows unhandled exception filter (SEH)
- Windows minidump writing (`MiniDumpWriteDump`)

## How To Inspect A Windows .dmp

Common options:
- Visual Studio: `File -> Open -> File...` and open the `.dmp`.
- WinDbg: open dump, set symbols, then run `!analyze -v`.

Minimum practical setup:
1. Load the dump from `bin/logs/dumps/`.
2. Ensure symbols/PDBs for your build are available.
3. Capture stack, exception code, and faulting module.
4. Correlate with `bin/logs/crash/crash_*.log` and `bin/logs/crash.log`.

## Modules Touched

Primary:
- `src/logging/LoggingService.h`
- `src/logging/LoggingService.cpp`
- `src/app/main.cpp`
- `src/app/JarvisApplication.cpp`
- `src/workers/VoicePipelineRuntime.cpp`
- `src/core/ToolCoordinator.cpp`
- `src/core/AssistantController.cpp`
- `src/wakeword/SherpaWakeHelperMain.cpp`
- `CMakeLists.txt`

Tests:
- `tests/VaxilErrorCodesTests.cpp`
- `tests/CrashBreadcrumbTrailTests.cpp`
- `tests/CrashReportWriterTests.cpp`
- `tests/LoggingFatalFlushTests.cpp`
- `tests/StartupMilestonesTests.cpp`
