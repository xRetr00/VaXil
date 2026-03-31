# Build Vaxil

## Prerequisites

- Qt 6.6+ with `Qt Quick`, `Qt Multimedia`, and `Qt SVG`
- CMake 3.27+
- Ninja
- A Qt kit that matches your compiler toolchain:
  - Windows: MSVC x64 with a matching Qt `msvc` desktop kit
  - Linux: `gcc`/`clang` with a matching Qt Linux desktop kit
- Runtime tools configured in Settings for full voice pipeline:
  - `whisper.cpp` executable
  - `Piper` executable and voice model
  - `ffmpeg` executable for post-processing
- Windows release packaging for the Python tools runtime requires a local Python interpreter on the build machine

Optional local speech stack dependencies (for full native wake/audio pipeline features):

- ONNX Runtime (set `JARVIS_ONNXRUNTIME_ROOT` or place under `third_party/onnxruntime`)
- sherpa-onnx runtime (set `JARVIS_SHERPA_ROOT` or place under `third_party/sherpa-onnx`)
- sentencepiece source tree (`third_party/sentencepiece`)
- SpeexDSP source tree (`third_party/speexdsp`)
- RNNoise source tree (`third_party/rnnoise/rnnoise-main`)

`VCPKG_ROOT` is optional and only required if using the `vcpkg` configure preset.

## Configure

```powershell
cmake --preset default
```

If you are using `vcpkg` for dependencies, set `VCPKG_ROOT` first and use:

```powershell
cmake --preset vcpkg
```

Release configure preset:

```powershell
cmake --preset release
```

For the Qt kit currently installed on this machine, this working MSVC configure flow is:

```powershell
cmd /c "call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && cmake -S d:\Vaxil -B d:\Vaxil\build-msvc -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.10.2\msvc2022_64"
```

Linux configure presets:

```bash
cmake --preset linux-debug
cmake --preset linux-release
```

## Build

```powershell
cmake --build --preset default
```

From the repo root on Windows, the fastest local workflow is now:

```bat
build.bat
```

Useful variants:

```bat
build.bat clean
build.bat release clean
build.bat notest
build.bat debug
build.bat package
```

Linux helper script:

```bash
./build.sh
./build.sh debug
./build.sh release clean
./build.sh notest
```

Notes:

- `build.bat` defaults to Release in `build-release`.
- `build.bat debug` uses `build`.
- `build.bat notest` skips `ctest` execution after build.
- Release Windows builds bundle the Python tools sidecar as `bin/python_runtime/vaxil_python_runtime.exe`.
- `build.bat package` additionally creates `build-release/vaxil-windows-portable.zip`.

For the verified local MSVC build directory:

```powershell
cmd /c "call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && cmake --build d:\Vaxil\build-msvc --parallel"
```

Linux build via preset:

```bash
cmake --build --preset linux-release --parallel
```

## Test

```powershell
ctest --preset default
```

For Release build directory:

```powershell
ctest --test-dir build-release --output-on-failure
```

For the verified local MSVC build directory:

```powershell
cmd /c "call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && ctest --test-dir d:\Vaxil\build-msvc --output-on-failure"
```

Linux test example:

```bash
ctest --test-dir build-linux-release --output-on-failure
```

## Runtime notes

- LM Studio should expose the OpenAI-compatible API on `http://localhost:1234`
- The app discovers models from `/v1/models`
- Voice generation starts only when sentence boundaries are detected from streamed output
- The application builds and deploys `vaxil_wake_helper` next to the main `vaxil` executable
- `windeployqt` runs as a post-build step on Windows to stage Qt runtime files
- Release Windows builds also stage the frozen Python tools runtime and bundled Playwright Chromium under `bin/python_runtime`
- On Linux/X11, the Qt xcb platform plugin requires runtime packages such as `libxcb-cursor0`; `build.sh` installs these via apt
- On Linux, `build.sh` also attempts to auto-install `whisper.cpp` and `piper` executables from distro packages when missing
- If `whisper.cpp` is unavailable in distro repos, `build.sh` falls back to source build and installs `whisper-cli` to `~/.local/share/vaxil/tools/whisper/bin/whisper-cli`
- Automatic tool downloads are supported on managed platforms; if unavailable in your environment, configure existing `whisper`, `piper`, `ffmpeg`, and optional wake assets manually
- The default premium voice profile targets a calm English delivery:
  - preferred Piper voice families: `en_GB-*` medium voices, especially `en_GB-alba-medium`
  - enforced speed range: `0.85` to `0.92`
  - enforced lowered pitch range: `0.90` to `0.97`
  - FFmpeg post-processing adds mild EQ, light reverb, compression, and limiting
- Identity is loaded from `config/identity.json`
- User profile is loaded from `config/user_profile.json`

## Windows Portable Package

Create the portable Windows zip from an existing release build:

```powershell
cmake --build build-release --target vaxil_windows_portable_zip --parallel
```

Output:

- `build-release/vaxil-windows-portable.zip`

## Linux AppImage

The repo includes an Ubuntu-focused AppImage packaging helper:

```bash
./scripts/package_linux_appimage.sh
```

Requirements:

- `linuxdeployqt` available on `PATH` or exposed via `LINUXDEPLOYQT_BIN`
- a Qt 6 runtime/toolkit compatible with the target Ubuntu environment
