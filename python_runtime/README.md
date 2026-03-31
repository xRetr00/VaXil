# Vaxil Python Runtime

Python sidecar for tool execution, skills, and future MCP integration.

## Dev Bootstrap

Windows:

```powershell
./scripts/bootstrap_python_runtime.ps1
```

The bootstrap script creates `python_runtime/.venv`, installs the locked runtime requirements when available, and installs Playwright Chromium into `python_runtime/ms-playwright`.

## Windows Release Bundle

Release Windows builds can freeze the runtime into a bundled sidecar executable:

```powershell
cmake --build build-release --target vaxil_python_runtime_bundle --parallel
cmake --build build-release --target vaxil_windows_portable_zip --parallel
```

The bundled output is staged next to `vaxil.exe` as:

- `python_runtime/vaxil_python_runtime.exe`
- `python_runtime/ms-playwright/`

## Current Status

- JSON-RPC 2.0 over `stdio`
- C++ runtime manager integration for `AgentToolbox`, `ToolWorker`, and `SkillStore`
- Initial action catalog for files, logs, shell, web, weather, Playwright-backed browser actions, coding generation, app launch, and skill scaffolding
- Windows release packaging via a frozen sidecar executable
- Graceful fallback to existing C++ implementations when the Python runtime is unavailable or an action is not implemented there yet
