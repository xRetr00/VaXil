# Build JARVIS

## Prerequisites

- Qt 6.6+ with `Qt Quick`, `Qt Multimedia`, and `Qt SVG`
- CMake 3.27+
- Ninja
- A working `VCPKG_ROOT`
- Windows toolchain for Qt builds (`MinGW` or `MSVC`)
- Runtime tools configured in Settings for full voice pipeline:
  - `whisper.cpp` executable
  - `Piper` executable and voice model
  - `ffmpeg` executable for post-processing

## Configure

```powershell
cmake --preset default
```

If you are using `vcpkg` for dependencies, set `VCPKG_ROOT` first and use:

```powershell
cmake --preset vcpkg
```

## Build

```powershell
cmake --build --preset default
```

## Test

```powershell
ctest --preset default
```

## Runtime notes

- LM Studio should expose the OpenAI-compatible API on `http://localhost:1234`
- The app discovers models from `/v1/models`
- Voice generation starts only when sentence boundaries are detected from streamed output
- Identity is loaded from `config/identity.json`
- User profile is loaded from `config/user_profile.json`
