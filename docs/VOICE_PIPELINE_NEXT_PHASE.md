# Voice Pipeline Next Phase

This document describes the current implementation added in this branch and the next integration steps for the local-first speech stack.

## New File Structure

```text
src/
  ai/
    RuntimeAiBackendClient.h
    RuntimeAiBackendClient.cpp
  audio/
    AudioProcessingTypes.h
    AudioProcessingChain.h
    AudioProcessingChain.cpp
  stt/
    SpeechRecognizer.h
    RuntimeSpeechRecognizer.h
    RuntimeSpeechRecognizer.cpp
  tools/
    ToolManager.h
    ToolManager.cpp
  workers/
    SpeechInputWorker.h
    SpeechInputWorker.cpp
    SpeechIoWorker.h
    SpeechIoWorker.cpp
    AiBackendWorker.h
    AiBackendWorker.cpp
    VoicePipelineRuntime.h
    VoicePipelineRuntime.cpp
  tts/
    TtsEngine.h
    PiperTtsEngine.h
    PiperTtsEngine.cpp
    WorkerTtsEngine.h
    WorkerTtsEngine.cpp
```

## What Is Implemented

- `AudioProcessingChain` provides a fixed-size float PCM processing surface that is safe to call repeatedly without per-frame heap allocation.
- `SpeechInputWorker`, `SpeechIoWorker`, and `AiBackendWorker` are isolated worker objects with generation-aware signals.
- `VoicePipelineRuntime` owns the workers on dedicated `QThread`s and wires them with queued connections.
- `AssistantController` now uses runtime-backed proxies for the OpenAI-compatible backend, Whisper STT, and Piper TTS paths.
- `PiperTtsEngine` now uses `QAudioSink` instead of `QMediaPlayer`, and emits far-end frames for future AEC integration.
- `ToolManager` scans the local tool root, downloads known source/model assets asynchronously over HTTPS, tracks progress, and verifies SHA-256 when a manifest entry provides one.
- Setup and settings QML now expose audio-processing toggles, wake/TTS engine selectors, and a tools status panel with install progress.

## Current Runtime Status

- The app still uses the existing `AssistantController` contracts and the current production flow remains active.
- The worker runtime is now the active execution path for:
  - OpenAI-compatible backend requests,
  - Whisper STT requests,
  - Piper synthesis/playback.
- Wake detection and direct microphone capture still use the current production path and should be migrated next.
- `AudioProcessingChain` currently uses:
  - far-end subtraction placeholder for AEC,
  - threshold-based suppression,
  - lightweight smoothing for the RNNoise toggle,
  - `libfvad` fallback for VAD.
- Real WebRTC APM, RNNoise, and ONNX Runtime Silero should be linked in behind the same interfaces next.

## Worker Wiring Example

`VoicePipelineRuntime` is the reference owner for thread-safe wiring:

```cpp
auto *runtime = new VoicePipelineRuntime(settings, loggingService, this);
runtime->start();

connect(runtime, &VoicePipelineRuntime::wakeDetected,
        this, &AssistantController::handleWakeDetected,
        Qt::QueuedConnection);

connect(runtime, &VoicePipelineRuntime::transcriptionReady,
        this, &AssistantController::handleTranscriptionReady,
        Qt::QueuedConnection);

connect(runtime, &VoicePipelineRuntime::requestFinished,
        this, &AssistantController::handleConversationFinished,
        Qt::QueuedConnection);

runtime->configureAudioProcessing({
    .aecEnabled = true,
    .noiseSuppressionEnabled = true,
    .agcEnabled = true,
    .rnnoiseEnabled = false,
    .vadSensitivity = 0.55f
});
```

Rules already enforced by this runtime:

- Worker objects live only on their owning `QThread`.
- Cross-thread calls use `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- Far-end audio from TTS is forwarded back into `SpeechInputWorker`.
- All emitted results carry a generation ID so stale results can be dropped by the caller.

## Audio Processing Chain Contract

```cpp
AudioProcessingChain chain;
chain.initialize({
    .aecEnabled = true,
    .noiseSuppressionEnabled = true,
    .agcEnabled = true,
    .rnnoiseEnabled = true,
    .vadSensitivity = 0.60f
});

AudioFrame micFrame;
micFrame.sampleRate = 16000;
micFrame.channels = 1;
micFrame.sampleCount = 320;

AudioFrame processed = chain.process(micFrame);
if (processed.speechDetected) {
    // forward to wake detector / speech collector
}
```

Internal policy:

- Float PCM is used inside the chain.
- `AudioFrame` uses fixed-size storage to avoid callback-time allocation.
- Far-end reference is injected through `setFarEnd()`.

## QAudioSink Playback Loop Example

The current `PiperTtsEngine` uses the `QIODevice` path. Qt 6.11 also supports callback-based playback, which is the lower-latency next step.

```cpp
QAudioFormat format;
format.setSampleRate(16000);
format.setChannelCount(1);
format.setSampleFormat(QAudioFormat::Float);

auto *sink = new QAudioSink(device, format, this);
sink->start([this](QSpan<float> out) {
    // Audio callback: do not block, allocate, or lock here.
    const qsizetype frames = out.size();
    for (qsizetype i = 0; i < frames; ++i) {
        out[i] = m_ringBuffer.popOrSilence();
    }
});
```

Recommended follow-up:

- Keep PCM in a lock-free ring buffer.
- Push the same far-end PCM into `AudioProcessingChain::setFarEnd()`.
- Move TTS decode and resample work out of the audio callback.

## Silero VAD Integration Example

Silero's ONNX model fits the current `AudioProcessingChain` interface well once ONNX Runtime is linked:

```cpp
Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "jarvis-vad");
Ort::SessionOptions options;
options.SetIntraOpNumThreads(1);
Ort::Session session(env, vadModelPath.toStdWString().c_str(), options);

std::array<int64_t, 2> shape{1, sampleCount};
std::array<float, 512> input{};
auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
Ort::Value tensor = Ort::Value::CreateTensor<float>(
    memory,
    input.data(),
    input.size(),
    shape.data(),
    shape.size());

auto outputs = session.Run(
    Ort::RunOptions{nullptr},
    inputNames.data(),
    &tensor,
    1,
    outputNames.data(),
    1);

const float speechProbability = outputs.front().GetTensorMutableData<float>()[0];
const bool speechDetected = speechProbability >= vadThreshold;
```

Integration notes:

- Keep the session alive for the worker lifetime.
- Reuse input/output buffers.
- Use one thread in the VAD worker for stable latency.

## Windows Build Notes

### ONNX Runtime

Recommended path:

1. Open a Visual Studio 2022 Developer Command Prompt.
2. Clone ONNX Runtime recursively.
3. Run:

```bat
build.bat --config RelWithDebInfo --build_shared_lib --parallel --compile_no_warning_as_error --skip_submodule_sync
```

After the build, copy the produced headers, `onnxruntime.lib`, and `onnxruntime.dll` into your third-party staging area or add them to your CMake package path.

### RNNoise

The upstream project is autotools-based. On Windows the lowest-friction path is MSYS2 MinGW:

```bash
./autogen.sh
./configure
make
make install
```

Practical Windows packaging path for this app:

- build `librnnoise` in MSYS2,
- export a thin C wrapper if needed,
- copy the produced DLL and import library into the app's third-party root.

### WebRTC APM

Recommended path for production:

1. Fetch WebRTC sources with depot_tools.
2. Build the audio-processing module with the same MSVC toolchain used by the app.
3. Wrap the APM entry points behind a small local adapter class so the rest of the app still talks only to `AudioProcessingChain`.

Practical adapter surface:

```cpp
class WebRtcApmAdapter {
public:
    bool initialize(int sampleRate, int channels);
    void setFarEnd(const float *samples, int frameCount);
    void processCapture(float *samples, int frameCount);
};
```

### sherpa-onnx

Use the official keyword-spotting models and C API/C++-friendly runtime for the future wake-engine replacement. Keep the current `WakeWordEngine` interface and implement a `SherpaWakeWordEngine` behind it.

## ToolManager Integration

`ToolManager` now scans:

- `onnxruntime`
- `sherpa-onnx`
- `silero-vad-model`
- `rnnoise`
- `piper`
- `qwen3-tts`

`ToolManager` downloads to:

- `%AppData%/JARVIS/third_party`

Current manifest support:

- Silero VAD ONNX model
- ONNX Runtime source archive
- RNNoise source archive
- sherpa-onnx source archive

This is enough for setup visibility and one-click source/model staging without breaking the current runtime.

## AssistantController Migration Order

1. Move microphone capture and wake detection onto `SpeechInputWorker`.
2. Push TTS far-end frames into WebRTC APM.
3. Replace the VAD fallback with ONNX Runtime Silero.
4. Add a `SherpaWakeWordEngine` implementation and switch by settings value.
5. Keep the hard duplex rule: microphone input is disabled or ignored while TTS is playing.

## References

- ONNX Runtime build docs: https://onnxruntime.ai/docs/build/inferencing.html
- Qt `QAudioSink`: https://doc.qt.io/qt-6/qaudiosink.html
- Silero VAD: https://github.com/snakers4/silero-vad
- RNNoise: https://github.com/xiph/rnnoise
- WebRTC AEC overview: https://webrtc.github.io/webrtc-org/blog/2011/07/11/webrtc-improvement-optimized-aec-acoustic-echo-cancellation.html
- sherpa-onnx keyword spotting: https://k2-fsa.github.io/sherpa/onnx/kws/index.html
