cmake_minimum_required(VERSION 3.27)

project(Vaxil VERSION 1.0.0 LANGUAGES C CXX)

include(FetchContent)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    set(JARVIS_PATH_ENV_SEPARATOR ";")
else()
    set(JARVIS_PATH_ENV_SEPARATOR ":")
endif()

set(JARVIS_ONNXRUNTIME_SHARED_LIBRARY "")
set(JARVIS_SHERPA_C_API_SHARED_LIBRARY "")
set(JARVIS_SHERPA_CXX_API_SHARED_LIBRARY "")
set(JARVIS_ONNXRUNTIME_PROVIDER_SHARED_LIBRARY "")

option(JARVIS_BUILD_TESTS "Build Vaxil tests" ON)
set(JARVIS_BUNDLE_PYTHON_RUNTIME_DEFAULT OFF)
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set(JARVIS_BUNDLE_PYTHON_RUNTIME_DEFAULT ON)
endif()
option(JARVIS_BUNDLE_PYTHON_RUNTIME "Bundle the Python tool runtime as a frozen Windows sidecar executable" ${JARVIS_BUNDLE_PYTHON_RUNTIME_DEFAULT})
set(JARVIS_ONNXRUNTIME_ROOT "" CACHE PATH "Path to a local ONNX Runtime package root")
set(JARVIS_SHERPA_ROOT "" CACHE PATH "Path to a local sherpa-onnx package root")
set(JARVIS_SENTENCEPIECE_ROOT "${CMAKE_SOURCE_DIR}/third_party/sentencepiece" CACHE PATH "Path to a local sentencepiece source tree")
set(JARVIS_SPEEXDSP_ROOT "${CMAKE_SOURCE_DIR}/third_party/speexdsp" CACHE PATH "Path to a local SpeexDSP source tree")
set(JARVIS_RNNOISE_ROOT "${CMAKE_SOURCE_DIR}/third_party/rnnoise/rnnoise-main" CACHE PATH "Path to a local RNNoise source tree")

find_package(Qt6 6.6 REQUIRED COMPONENTS
    Concurrent
    Core
    Gui
    Network
    Multimedia
    Quick
    QuickControls2
    Sql
    Svg
    Test
    WebSockets
    Widgets
)
find_package(Qt6ShaderToolsTools REQUIRED)
find_package(Python3 COMPONENTS Interpreter QUIET)
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
    FetchContent_Declare(
        spdlog
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.zip
    )
    FetchContent_MakeAvailable(spdlog)
endif()

FetchContent_Declare(
    libfvad
    GIT_REPOSITORY https://github.com/dpirch/libfvad.git
    GIT_TAG v1.0
)
FetchContent_MakeAvailable(libfvad)

add_library(fvad STATIC
    ${libfvad_SOURCE_DIR}/src/fvad.c
    ${libfvad_SOURCE_DIR}/src/signal_processing/division_operations.c
    ${libfvad_SOURCE_DIR}/src/signal_processing/energy.c
    ${libfvad_SOURCE_DIR}/src/signal_processing/get_scaling_square.c
    ${libfvad_SOURCE_DIR}/src/signal_processing/resample_48khz.c
    ${libfvad_SOURCE_DIR}/src/signal_processing/resample_by_2_internal.c
    ${libfvad_SOURCE_DIR}/src/signal_processing/resample_fractional.c
    ${libfvad_SOURCE_DIR}/src/signal_processing/spl_inl.c
    ${libfvad_SOURCE_DIR}/src/vad/vad_core.c
    ${libfvad_SOURCE_DIR}/src/vad/vad_filterbank.c
    ${libfvad_SOURCE_DIR}/src/vad/vad_gmm.c
    ${libfvad_SOURCE_DIR}/src/vad/vad_sp.c
)

target_include_directories(fvad
    PUBLIC
        ${libfvad_SOURCE_DIR}/include
    PRIVATE
        ${libfvad_SOURCE_DIR}/src
)

if(EXISTS "${JARVIS_SPEEXDSP_ROOT}/include/speex/speex_echo.h")
    set(INCLUDE_STDINT "#include <stdint.h>")
    set(SIZE16 "int16_t")
    set(USIZE16 "uint16_t")
    set(SIZE32 "int32_t")
    set(USIZE32 "uint32_t")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/third_party/speexdsp/include/speex")
    configure_file(
        "${JARVIS_SPEEXDSP_ROOT}/include/speex/speexdsp_config_types.h.in"
        "${CMAKE_BINARY_DIR}/third_party/speexdsp/include/speexdsp_config_types.h"
        @ONLY
    )
    configure_file(
        "${JARVIS_SPEEXDSP_ROOT}/include/speex/speexdsp_config_types.h.in"
        "${CMAKE_BINARY_DIR}/third_party/speexdsp/include/speex/speexdsp_config_types.h"
        @ONLY
    )

    add_library(speexdsp STATIC
        ${JARVIS_SPEEXDSP_ROOT}/libspeexdsp/fftwrap.c
        ${JARVIS_SPEEXDSP_ROOT}/libspeexdsp/filterbank.c
        ${JARVIS_SPEEXDSP_ROOT}/libspeexdsp/kiss_fft.c
        ${JARVIS_SPEEXDSP_ROOT}/libspeexdsp/kiss_fftr.c
        ${JARVIS_SPEEXDSP_ROOT}/libspeexdsp/mdf.c
        ${JARVIS_SPEEXDSP_ROOT}/libspeexdsp/preprocess.c
        ${JARVIS_SPEEXDSP_ROOT}/libspeexdsp/resample.c
        ${JARVIS_SPEEXDSP_ROOT}/libspeexdsp/smallft.c
    )
    target_include_directories(speexdsp
        PUBLIC
            ${CMAKE_BINARY_DIR}/third_party/speexdsp/include
            ${JARVIS_SPEEXDSP_ROOT}/include
        PRIVATE
            ${JARVIS_SPEEXDSP_ROOT}/libspeexdsp
    )
    target_compile_definitions(speexdsp PRIVATE FLOATING_POINT USE_SMALLFT EXPORT=)
endif()

if(EXISTS "${JARVIS_RNNOISE_ROOT}/include/rnnoise.h"
    AND EXISTS "${JARVIS_RNNOISE_ROOT}/src/denoise.c"
    AND EXISTS "${JARVIS_RNNOISE_ROOT}/src/rnnoise_data.h")
    set(JARVIS_RNNOISE_SOURCES
        ${JARVIS_RNNOISE_ROOT}/src/celt_lpc.c
        ${JARVIS_RNNOISE_ROOT}/src/denoise.c
        ${JARVIS_RNNOISE_ROOT}/src/kiss_fft.c
        ${JARVIS_RNNOISE_ROOT}/src/nnet.c
        ${JARVIS_RNNOISE_ROOT}/src/nnet_default.c
        ${JARVIS_RNNOISE_ROOT}/src/parse_lpcnet_weights.c
        ${JARVIS_RNNOISE_ROOT}/src/pitch.c
        ${JARVIS_RNNOISE_ROOT}/src/rnn.c
        ${JARVIS_RNNOISE_ROOT}/src/rnnoise_tables.c
    )
    if(EXISTS "${JARVIS_RNNOISE_ROOT}/src/rnnoise_data.c")
        list(APPEND JARVIS_RNNOISE_SOURCES
            ${JARVIS_RNNOISE_ROOT}/src/rnnoise_data.c)
    endif()
    add_library(rnnoise STATIC
        ${JARVIS_RNNOISE_SOURCES}
    )
    target_include_directories(rnnoise
        PUBLIC
            ${JARVIS_RNNOISE_ROOT}/include
        PRIVATE
            ${JARVIS_RNNOISE_ROOT}/src
    )
endif()

qt_standard_project_setup(REQUIRES 6.6)

file(GLOB_RECURSE JARVIS_PYTHON_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/python_runtime/vaxil_runtime/*.py"
    "${CMAKE_SOURCE_DIR}/python_runtime/vaxil_runtime/Actions/*.py"
)

set(JARVIS_PYTHON_RUNTIME_REQUIREMENTS_LOCK "${CMAKE_SOURCE_DIR}/python_runtime/requirements.lock")
set(JARVIS_PYTHON_RUNTIME_PACKAGING_REQUIREMENTS "${CMAKE_SOURCE_DIR}/python_runtime/requirements-packaging.txt")
set(JARVIS_PYTHON_RUNTIME_SPEC "${CMAKE_SOURCE_DIR}/python_runtime/vaxil_python_runtime.spec")
set(JARVIS_PYTHON_RUNTIME_BUNDLE_ROOT "${CMAKE_BINARY_DIR}/python_runtime_bundle")
set(JARVIS_PYTHON_RUNTIME_BROWSERS_DIR "${JARVIS_PYTHON_RUNTIME_BUNDLE_ROOT}/ms-playwright")
set(JARVIS_PYTHON_RUNTIME_DIST_DIR "${JARVIS_PYTHON_RUNTIME_BUNDLE_ROOT}/dist")
set(JARVIS_PYTHON_RUNTIME_WORK_DIR "${JARVIS_PYTHON_RUNTIME_BUNDLE_ROOT}/work")
set(JARVIS_PYTHON_RUNTIME_STAMP "${JARVIS_PYTHON_RUNTIME_BUNDLE_ROOT}/bundle.stamp")

if(WIN32 AND JARVIS_BUNDLE_PYTHON_RUNTIME)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    add_custom_command(
        OUTPUT "${JARVIS_PYTHON_RUNTIME_STAMP}"
        COMMAND "${CMAKE_COMMAND}"
            -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBUNDLE_ROOT=${JARVIS_PYTHON_RUNTIME_BUNDLE_ROOT}
            -DBROWSERS_DIR=${JARVIS_PYTHON_RUNTIME_BROWSERS_DIR}
            -DDIST_DIR=${JARVIS_PYTHON_RUNTIME_DIST_DIR}
            -DWORK_DIR=${JARVIS_PYTHON_RUNTIME_WORK_DIR}
            -DREQUIREMENTS_LOCK=${JARVIS_PYTHON_RUNTIME_REQUIREMENTS_LOCK}
            -DREQUIREMENTS_PACKAGING=${JARVIS_PYTHON_RUNTIME_PACKAGING_REQUIREMENTS}
            -DSPEC_FILE=${JARVIS_PYTHON_RUNTIME_SPEC}
            -DSTAMP_FILE=${JARVIS_PYTHON_RUNTIME_STAMP}
            -P "${CMAKE_SOURCE_DIR}/cmake/BundlePythonRuntime.cmake"
        DEPENDS
            ${JARVIS_PYTHON_RUNTIME_SOURCES}
            "${JARVIS_PYTHON_RUNTIME_REQUIREMENTS_LOCK}"
            "${JARVIS_PYTHON_RUNTIME_PACKAGING_REQUIREMENTS}"
            "${JARVIS_PYTHON_RUNTIME_SPEC}"
            "${CMAKE_SOURCE_DIR}/cmake/BundlePythonRuntime.cmake"
        BYPRODUCTS
            "${JARVIS_PYTHON_RUNTIME_DIST_DIR}/vaxil_python_runtime/vaxil_python_runtime.exe"
        COMMENT "Bundling Python runtime sidecar"
        VERBATIM
    )

    add_custom_target(vaxil_python_runtime_bundle ALL
        DEPENDS "${JARVIS_PYTHON_RUNTIME_STAMP}"
    )
endif()

function(jarvis_find_shared_library out_var root_path library_base_name)
    if(WIN32)
        set(_patterns "${root_path}/lib/${library_base_name}.dll")
    else()
        set(_patterns
            "${root_path}/lib/lib${library_base_name}.so"
            "${root_path}/lib/lib${library_base_name}.so.*")
    endif()

    file(GLOB _matches LIST_DIRECTORIES FALSE ${_patterns})
    list(LENGTH _matches _match_count)
    if(_match_count GREATER 0)
        list(GET _matches 0 _selected)
        set(${out_var} "${_selected}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

set(JARVIS_SOURCES
    src/app/JarvisApplication.cpp
    src/agent/AgentToolbox.cpp
    src/ai/OpenAiCompatibleClient.cpp
    src/ai/RuntimeAiBackendClient.cpp
    src/ai/SpokenReply.cpp
    src/ai/ModelCatalogService.cpp
    src/ai/PromptAdapter.cpp
    src/ai/ReasoningRouter.cpp
    src/ai/StreamAssembler.cpp
    src/audio/AudioInputService.cpp
    src/audio/AudioProcessingChain.cpp
    src/audio/SileroVadSession.cpp
    src/core/agent/IntentDetector.cpp
    src/core/agent/IntentEngine.cpp
    src/core/ActionRiskPermissionService.cpp
    src/core/AiRequestCoordinator.cpp
    src/core/AssistantBehaviorPolicy.cpp
    src/core/AssistantController.cpp
    src/core/DesktopActionContextPolicy.cpp
    src/core/ExecutionNarrator.cpp
    src/core/InputRouter.cpp
    src/core/IntentRouter.cpp
    src/core/MemoryPolicyHandler.cpp
    src/core/ComputerControl.cpp
    src/core/LocalResponseEngine.cpp
    src/core/ResponseFinalizer.cpp
    src/core/PermissionOverrideSettings.cpp
    src/core/ToolCoordinator.cpp
    src/core/ToolPermissionRegistry.cpp
    src/connectors/BrowserBookmarksEventBuilder.cpp
    src/connectors/BrowserBookmarksMonitor.cpp
    src/connectors/CalendarIcsEventBuilder.cpp
    src/connectors/CalendarIcsMonitor.cpp
    src/connectors/ConnectorSourceSpec.cpp
    src/connectors/ConnectorSnapshotEventBuilder.cpp
    src/connectors/ConnectorSnapshotMonitor.cpp
    src/connectors/InboxMaildropEventBuilder.cpp
    src/connectors/InboxMaildropMonitor.cpp
    src/connectors/NotesDirectoryEventBuilder.cpp
    src/connectors/NotesDirectoryMonitor.cpp
    src/behavior_tuning/CompiledContextPolicyTuningPromotionPolicy.cpp
    src/behavior_tuning/FeedbackSignalEventBuilder.cpp
    src/behavior_tuning/TuningEpisodeFeedbackScorer.cpp
    src/cognition/CooldownEngine.cpp
    src/cognition/CompiledContextDeltaTracker.cpp
    src/cognition/CompiledContextHistoryPolicy.cpp
    src/cognition/CompiledContextLayeredMemoryBuilder.cpp
    src/cognition/CompiledContextLayeredSignalBuilder.cpp
    src/cognition/CompiledContextPolicyEvolutionBuilder.cpp
    src/cognition/CompiledContextPolicySummaryBuilder.cpp
    src/cognition/CompiledContextPolicyTuningSignalBuilder.cpp
    src/cognition/CompiledContextHistorySummaryBuilder.cpp
    src/cognition/CompiledContextStabilityMemoryBuilder.cpp
    src/cognition/CompiledContextStabilityTracker.cpp
    src/cognition/PromptContextTemporalPolicy.cpp
    src/cognition/ConnectorContextCompiler.cpp
    src/cognition/ConnectorHistoryTracker.cpp
    src/cognition/ConnectorEventBuilder.cpp
    src/cognition/ConnectorResultSignal.cpp
    src/cognition/ProactiveCooldownTracker.cpp
    src/cognition/DesktopContextSelectionBuilder.cpp
    src/cognition/DesktopWorkMode.cpp
    src/cognition/FocusModePolicy.cpp
    src/cognition/ProactivePlannerInputEnricher.cpp
    src/cognition/ProactiveSuggestionGate.cpp
    src/cognition/ProactiveSuggestionPlanner.cpp
    src/cognition/ProactiveSurfaceGate.cpp
    src/cognition/SelectionContextCompiler.cpp
    src/cognition/SuggestionProposalBuilder.cpp
    src/cognition/SuggestionProposalPolicyScoring.cpp
    src/cognition/SuggestionProposalRanker.cpp
    src/core/tasks/TaskDispatcher.cpp
    src/core/tasks/ToolWorker.cpp
    src/core/tools/ToolExecutionService.cpp
    src/devices/DeviceManager.cpp
    src/devices/MockDevices.cpp
    src/gui/AgentViewModel.cpp
    src/gui/BackendFacade.cpp
    src/gui/SettingsViewModel.cpp
    src/gui/TaskViewModel.cpp
    src/learning_data/LearningDataCollector.cpp
    src/learning_data/LearningDataSettings.cpp
    src/learning_data/LearningDataStorage.cpp
    src/logging/LoggingService.cpp
    src/memory/MemoryManager.cpp
    src/memory/MemoryStore.cpp
    src/memory/MemoryStoreContextPolicies.cpp
    src/memory/MemoryStoreFeedbackSignals.cpp
    src/memory/MemoryStoreTuningFeedbackScores.cpp
    src/memory/MemoryStoreTuningEpisodes.cpp
    src/overlay/OverlayController.cpp
    src/perception/DesktopContextThreadBuilder.cpp
    src/perception/DesktopPerceptionMonitor.cpp
    src/perception/DesktopSourceContextAdapter.cpp
    src/perception/WindowsUiAutomationProbe.cpp
    src/platform/PlatformRuntime.cpp
    src/python/PythonRuntimeManager.cpp
    src/settings/AppSettings.cpp
    src/settings/IdentityProfileService.cpp
    src/skills/SkillStore.cpp
    src/stt/WhisperSttEngine.cpp
    src/stt/RuntimeSpeechRecognizer.cpp
    src/tools/ToolManager.cpp
    src/tts/PiperTtsEngine.cpp
    src/tts/WorkerTtsEngine.cpp
    src/telemetry/BehavioralEventLedger.cpp
    src/telemetry/SelectionTelemetryBuilder.cpp
    src/vision/VisionIngestService.cpp
    src/vision/GestureInterpreter.cpp
    src/vision/GestureStateMachine.cpp
    src/vision/GestureActionRouter.cpp
    src/vision/VisionContextGate.cpp
    src/vision/WorldStateCache.cpp
    src/wakeword/WakeWordDetector.cpp
    src/wakeword/SherpaWakeWordEngine.cpp
    src/workers/AiBackendWorker.cpp
    src/workers/SpeechInputWorker.cpp
    src/workers/SpeechIoWorker.cpp
    src/workers/VoicePipelineRuntime.cpp
)

set(JARVIS_HEADERS
    src/app/JarvisApplication.h
    src/agent/AgentToolbox.h
    src/ai/AiBackendClient.h
    src/ai/OpenAiCompatibleClient.h
    src/ai/RuntimeAiBackendClient.h
    src/ai/SpokenReply.h
    src/ai/ModelCatalogService.h
    src/ai/PromptAdapter.h
    src/ai/ReasoningRouter.h
    src/ai/StreamAssembler.h
    src/audio/AudioInputService.h
    src/audio/AudioProcessingChain.h
    src/audio/AudioProcessingTypes.h
    src/audio/SileroVadSession.h
    src/core/agent/IntentDetector.h
    src/core/agent/IntentEngine.h
    src/core/ActionRiskPermissionService.h
    src/core/AiRequestCoordinator.h
    src/core/AssistantBehaviorPolicy.h
    src/core/AssistantController.h
    src/core/AssistantTypes.h
    src/core/DesktopActionContextPolicy.h
    src/core/ComputerControl.h
    src/core/ExecutionNarrator.h
    src/core/InputRouter.h
    src/core/IntentRouter.h
    src/core/LocalResponseEngine.h
    src/core/MemoryPolicyHandler.h
    src/core/PermissionOverrideSettings.h
    src/core/ResponseFinalizer.h
    src/core/ToolCoordinator.h
    src/core/ToolPermissionRegistry.h
    src/connectors/BrowserBookmarksEventBuilder.h
    src/connectors/BrowserBookmarksMonitor.h
    src/connectors/CalendarIcsEventBuilder.h
    src/connectors/CalendarIcsMonitor.h
    src/connectors/ConnectorSourceSpec.h
    src/connectors/ConnectorSnapshotEventBuilder.h
    src/connectors/ConnectorSnapshotMonitor.h
    src/connectors/InboxMaildropEventBuilder.h
    src/connectors/InboxMaildropMonitor.h
    src/connectors/NotesDirectoryEventBuilder.h
    src/connectors/NotesDirectoryMonitor.h
    src/behavior_tuning/CompiledContextPolicyTuningPromotionPolicy.h
    src/behavior_tuning/FeedbackSignalEventBuilder.h
    src/behavior_tuning/TuningEpisodeFeedbackScorer.h
    src/cognition/CooldownEngine.h
    src/cognition/CompiledContextDeltaTracker.h
    src/cognition/CompiledContextHistoryPolicy.h
    src/cognition/CompiledContextPolicyTuningSignalBuilder.h
    src/cognition/CompiledContextPolicySummaryBuilder.h
    src/cognition/CompiledContextHistorySummaryBuilder.h
    src/cognition/CompiledContextStabilityMemoryBuilder.h
    src/cognition/CompiledContextStabilityTracker.h
    src/cognition/PromptContextTemporalPolicy.h
    src/cognition/ConnectorContextCompiler.h
    src/cognition/ConnectorHistoryTracker.h
    src/cognition/ConnectorEventBuilder.h
    src/cognition/DesktopContextSelectionBuilder.h
    src/cognition/DesktopWorkMode.h
    src/cognition/FocusModePolicy.h
    src/cognition/ProactivePlannerInputEnricher.h
    src/cognition/ProactiveSuggestionGate.h
    src/cognition/ProactiveSurfaceGate.h
    src/cognition/SelectionContextCompiler.h
    src/cognition/SuggestionProposalBuilder.h
    src/cognition/SuggestionProposalRanker.h
    src/core/tasks/TaskDispatcher.h
    src/core/tasks/ToolWorker.h
    src/core/tools/ToolExecutionService.h
    src/companion/contracts/ActionOutcome.h
    src/companion/contracts/ActionProposal.h
    src/companion/contracts/BehaviorDecision.h
    src/companion/contracts/BehaviorTraceEvent.h
    src/companion/contracts/CompanionContextSnapshot.h
    src/companion/contracts/ConnectorEvent.h
    src/companion/contracts/ContextThreadId.h
    src/companion/contracts/CooldownState.h
    src/companion/contracts/CriticalAlertPolicy.h
    src/companion/contracts/FeedbackSignal.h
    src/companion/contracts/FocusModeState.h
    src/companion/contracts/MemoryQuery.h
    src/companion/contracts/MemoryRecord.h
    src/companion/contracts/PerceptionEvent.h
    src/companion/contracts/PermissionGrant.h
    src/companion/contracts/PolicySnapshot.h
    src/companion/contracts/PromptAssembly.h
    src/companion/contracts/RiskAssessment.h
    src/companion/contracts/TaskRecord.h
    src/companion/contracts/ToolExposurePlan.h
    src/companion/contracts/TriggerRule.h
    src/devices/Device.h
    src/devices/DeviceManager.h
    src/devices/MockDevices.h
    src/gui/AgentViewModel.h
    src/gui/BackendFacade.h
    src/gui/SettingsViewModel.h
    src/learning_data/LearningDataCollector.h
    src/learning_data/LearningDataSettings.h
    src/learning_data/LearningDataStorage.h
    src/learning_data/LearningDataTypes.h
    src/logging/LoggingService.h
    src/memory/MemoryManager.h
    src/memory/MemoryStore.h
    src/gui/TaskViewModel.h
    src/overlay/OverlayController.h
    src/perception/DesktopContextThreadBuilder.h
    src/perception/DesktopPerceptionMonitor.h
    src/perception/DesktopSourceContextAdapter.h
    src/perception/WindowsUiAutomationProbe.h
    src/platform/ComputerControlBackend.h
    src/platform/PlatformCapabilities.h
    src/platform/PlatformRuntime.h
    src/python/PythonRuntimeManager.h
    src/settings/AppSettings.h
    src/settings/IdentityProfileService.h
    src/skills/SkillStore.h
    src/stt/WhisperSttEngine.h
    src/stt/SpeechRecognizer.h
    src/stt/RuntimeSpeechRecognizer.h
    src/telemetry/BehavioralEventLedger.h
    src/telemetry/SelectionTelemetryBuilder.h
    src/tools/ToolManager.h
    src/tts/TtsEngine.h
    src/tts/PiperTtsEngine.h
    src/tts/WorkerTtsEngine.h
    src/vision/VisionIngestService.h
    src/vision/GestureInterpreter.h
    src/vision/GestureStateMachine.h
    src/vision/GestureActionRouter.h
    src/vision/VisionContextGate.h
    src/vision/WorldStateCache.h
    src/wakeword/WakeWordDetector.h
    src/wakeword/SherpaWakeWordEngine.h
    src/wakeword/WakeWordEngine.h
    src/workers/AiBackendWorker.h
    src/workers/SpeechInputWorker.h
    src/workers/SpeechIoWorker.h
    src/workers/VoicePipelineRuntime.h
)

qt_add_resources(JARVIS_RESOURCES src/gui/resources.qrc)
set_source_files_properties(src/gui/shaders/orb.frag PROPERTIES QT_RESOURCE_ALIAS orb.frag)

add_library(jarvis_lib STATIC
    ${JARVIS_SOURCES}
    ${JARVIS_HEADERS}
    ${JARVIS_RESOURCES}
)

qt_add_shaders(jarvis_lib "jarvis_shaders"
    PREFIX
        "/qt/qml/VAXIL/gui/shaders"
    FILES
        src/gui/shaders/orb.frag
        src/gui/shaders/voice_wave.frag
        src/gui/shaders/text_shimmer.frag
)

target_include_directories(jarvis_lib
    PUBLIC
        src
    PRIVATE
        ${libfvad_SOURCE_DIR}/include
)
if(MSVC)
    target_compile_options(jarvis_lib PRIVATE /FS)
    set_target_properties(jarvis_lib PROPERTIES
        MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>"
    )
endif()
target_compile_definitions(jarvis_lib PUBLIC JARVIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set(JARVIS_HAS_ONNXRUNTIME 0)
set(JARVIS_HAS_SHERPA_ONNX 0)

if(NOT JARVIS_ONNXRUNTIME_ROOT)
    if(WIN32)
        file(GLOB JARVIS_ONNXRUNTIME_CANDIDATES RELATIVE "${CMAKE_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/onnxruntime-win-x64-*")
    else()
        file(GLOB JARVIS_ONNXRUNTIME_CANDIDATES RELATIVE "${CMAKE_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/onnxruntime-linux-x64-*")
    endif()
    list(LENGTH JARVIS_ONNXRUNTIME_CANDIDATES JARVIS_ONNXRUNTIME_CANDIDATE_COUNT)
    if(JARVIS_ONNXRUNTIME_CANDIDATE_COUNT GREATER 0)
        list(GET JARVIS_ONNXRUNTIME_CANDIDATES 0 JARVIS_ONNXRUNTIME_ROOT)
        set(JARVIS_ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/${JARVIS_ONNXRUNTIME_ROOT}")
    endif()
endif()

if(JARVIS_ONNXRUNTIME_ROOT
    AND EXISTS "${JARVIS_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    jarvis_find_shared_library(JARVIS_ONNXRUNTIME_SHARED_LIBRARY "${JARVIS_ONNXRUNTIME_ROOT}" "onnxruntime")
endif()

if(JARVIS_ONNXRUNTIME_ROOT
    AND EXISTS "${JARVIS_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h"
    AND JARVIS_ONNXRUNTIME_SHARED_LIBRARY)
    add_library(onnxruntime SHARED IMPORTED)
    if(WIN32)
        set_target_properties(onnxruntime PROPERTIES
            IMPORTED_IMPLIB "${JARVIS_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
            IMPORTED_LOCATION "${JARVIS_ONNXRUNTIME_SHARED_LIBRARY}"
        )
    else()
        set_target_properties(onnxruntime PROPERTIES
            IMPORTED_LOCATION "${JARVIS_ONNXRUNTIME_SHARED_LIBRARY}"
        )
    endif()
    target_include_directories(jarvis_lib PRIVATE "${JARVIS_ONNXRUNTIME_ROOT}/include")
    target_link_libraries(jarvis_lib PUBLIC onnxruntime)
    set(JARVIS_HAS_ONNXRUNTIME 1)
endif()

if(NOT JARVIS_SHERPA_ROOT)
    if(WIN32)
        file(GLOB JARVIS_SHERPA_CANDIDATES RELATIVE "${CMAKE_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/third_party/sherpa-onnx/sherpa-onnx-v*-win-x64-shared-MD-Release-no-tts")
    else()
        file(GLOB JARVIS_SHERPA_CANDIDATES RELATIVE "${CMAKE_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/third_party/sherpa-onnx/sherpa-onnx-v*-linux-x64-shared*")
    endif()
    list(LENGTH JARVIS_SHERPA_CANDIDATES JARVIS_SHERPA_CANDIDATE_COUNT)
    if(JARVIS_SHERPA_CANDIDATE_COUNT GREATER 0)
        list(GET JARVIS_SHERPA_CANDIDATES 0 JARVIS_SHERPA_ROOT)
        set(JARVIS_SHERPA_ROOT "${CMAKE_SOURCE_DIR}/${JARVIS_SHERPA_ROOT}")
    endif()
endif()

if(EXISTS "${JARVIS_SENTENCEPIECE_ROOT}/CMakeLists.txt")
    set(SPM_BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
    set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
    set(SPM_ENABLE_MSVC_MT_BUILD OFF CACHE BOOL "" FORCE)
    add_subdirectory("${JARVIS_SENTENCEPIECE_ROOT}" "${CMAKE_BINARY_DIR}/third_party/sentencepiece" EXCLUDE_FROM_ALL)
    target_include_directories(sentencepiece-static INTERFACE
        "${JARVIS_SENTENCEPIECE_ROOT}"
        "${JARVIS_SENTENCEPIECE_ROOT}/src"
        "${JARVIS_SENTENCEPIECE_ROOT}/third_party"
        "${CMAKE_BINARY_DIR}/third_party/sentencepiece"
        "${CMAKE_BINARY_DIR}/third_party/sentencepiece/src"
    )
endif()

if(JARVIS_SHERPA_ROOT
    AND EXISTS "${JARVIS_SHERPA_ROOT}/include/sherpa-onnx/c-api/cxx-api.h")
    jarvis_find_shared_library(JARVIS_SHERPA_C_API_SHARED_LIBRARY "${JARVIS_SHERPA_ROOT}" "sherpa-onnx-c-api")
    jarvis_find_shared_library(JARVIS_SHERPA_CXX_API_SHARED_LIBRARY "${JARVIS_SHERPA_ROOT}" "sherpa-onnx-cxx-api")
    if(WIN32)
        jarvis_find_shared_library(JARVIS_ONNXRUNTIME_PROVIDER_SHARED_LIBRARY "${JARVIS_SHERPA_ROOT}" "onnxruntime_providers_shared")
    endif()
endif()

if(JARVIS_SHERPA_ROOT
    AND EXISTS "${JARVIS_SHERPA_ROOT}/include/sherpa-onnx/c-api/cxx-api.h"
    AND JARVIS_SHERPA_C_API_SHARED_LIBRARY
    AND JARVIS_SHERPA_CXX_API_SHARED_LIBRARY
    AND TARGET sentencepiece-static)
    add_library(jarvis_sherpa_onnx_c_api SHARED IMPORTED)
    if(WIN32)
        set_target_properties(jarvis_sherpa_onnx_c_api PROPERTIES
            IMPORTED_IMPLIB "${JARVIS_SHERPA_ROOT}/lib/sherpa-onnx-c-api.lib"
            IMPORTED_LOCATION "${JARVIS_SHERPA_C_API_SHARED_LIBRARY}"
        )
    else()
        set_target_properties(jarvis_sherpa_onnx_c_api PROPERTIES
            IMPORTED_LOCATION "${JARVIS_SHERPA_C_API_SHARED_LIBRARY}"
        )
    endif()
    add_library(jarvis_sherpa_onnx_cxx_api SHARED IMPORTED)
    if(WIN32)
        set_target_properties(jarvis_sherpa_onnx_cxx_api PROPERTIES
            IMPORTED_IMPLIB "${JARVIS_SHERPA_ROOT}/lib/sherpa-onnx-cxx-api.lib"
            IMPORTED_LOCATION "${JARVIS_SHERPA_CXX_API_SHARED_LIBRARY}"
        )
    else()
        set_target_properties(jarvis_sherpa_onnx_cxx_api PROPERTIES
            IMPORTED_LOCATION "${JARVIS_SHERPA_CXX_API_SHARED_LIBRARY}"
        )
    endif()
    target_include_directories(jarvis_lib PRIVATE "${JARVIS_SHERPA_ROOT}/include")
    target_link_libraries(jarvis_lib PUBLIC
        jarvis_sherpa_onnx_c_api
        jarvis_sherpa_onnx_cxx_api
        sentencepiece-static
    )
    set(JARVIS_HAS_SHERPA_ONNX 1)
endif()

target_compile_definitions(jarvis_lib PUBLIC
    JARVIS_HAS_ONNXRUNTIME=${JARVIS_HAS_ONNXRUNTIME}
    JARVIS_HAS_SHERPA_ONNX=${JARVIS_HAS_SHERPA_ONNX}
)

target_link_libraries(jarvis_lib
    PUBLIC
        Qt6::Core
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Network
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Sql
        Qt6::Svg
        Qt6::WebSockets
        Qt6::Widgets
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        fvad
)

if(TARGET speexdsp)
    target_link_libraries(jarvis_lib PUBLIC speexdsp)
endif()

if(TARGET rnnoise)
    target_link_libraries(jarvis_lib PUBLIC rnnoise)
endif()

if(WIN32)
    target_link_libraries(jarvis_lib PUBLIC Ole32 OleAut32 Uiautomationcore)
endif()

if(Python3_EXECUTABLE)
    add_custom_target(jarvis_file_cap_check
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/check_file_caps.py
            --root ${CMAKE_SOURCE_DIR}
            --allowlist ${CMAKE_SOURCE_DIR}/config/file_caps_legacy_allowlist.txt
            --max-lines 500
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking authored file size caps"
    )
endif()

function(jarvis_copy_optional_runtime_libraries target_name)
    if(JARVIS_ONNXRUNTIME_SHARED_LIBRARY)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${JARVIS_ONNXRUNTIME_SHARED_LIBRARY}"
                "$<TARGET_FILE_DIR:${target_name}>"
        )
    endif()

    if(JARVIS_SHERPA_C_API_SHARED_LIBRARY AND JARVIS_SHERPA_CXX_API_SHARED_LIBRARY)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${JARVIS_SHERPA_C_API_SHARED_LIBRARY}"
                "$<TARGET_FILE_DIR:${target_name}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${JARVIS_SHERPA_CXX_API_SHARED_LIBRARY}"
                "$<TARGET_FILE_DIR:${target_name}>"
        )
        if(JARVIS_ONNXRUNTIME_PROVIDER_SHARED_LIBRARY)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${JARVIS_ONNXRUNTIME_PROVIDER_SHARED_LIBRARY}"
                    "$<TARGET_FILE_DIR:${target_name}>"
            )
        endif()
    endif()
endfunction()

qt_add_executable(jarvis_sherpa_wake_helper
    src/wakeword/SherpaWakeHelperMain.cpp
    src/wakeword/WakeWordDetector.cpp
    src/wakeword/WakeWordDetector.h
)

target_link_libraries(jarvis_sherpa_wake_helper
    PRIVATE
        Qt6::Core
        Qt6::Multimedia
)

target_compile_definitions(jarvis_sherpa_wake_helper PRIVATE JARVIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set(JARVIS_WAKE_HELPER_HAS_SHERPA_ONNX 0)
set_target_properties(jarvis_sherpa_wake_helper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    OUTPUT_NAME "vaxil_wake_helper"
)

if(TARGET jarvis_sherpa_onnx_cxx_api)
    target_include_directories(jarvis_sherpa_wake_helper PRIVATE "${JARVIS_SHERPA_ROOT}/include")
    target_link_libraries(jarvis_sherpa_wake_helper
        PRIVATE
            jarvis_sherpa_onnx_c_api
            jarvis_sherpa_onnx_cxx_api
    )
    set(JARVIS_WAKE_HELPER_HAS_SHERPA_ONNX 1)
endif()
target_compile_definitions(jarvis_sherpa_wake_helper PRIVATE JARVIS_HAS_SHERPA_ONNX=${JARVIS_WAKE_HELPER_HAS_SHERPA_ONNX})
jarvis_copy_optional_runtime_libraries(jarvis_sherpa_wake_helper)

set(JARVIS_APP_SOURCES
    src/app/main.cpp
)

if(WIN32)
    list(APPEND JARVIS_APP_SOURCES src/app/vaxil.rc)
endif()

qt_add_executable(jarvis
    ${JARVIS_APP_SOURCES}
)

target_link_libraries(jarvis PRIVATE jarvis_lib)
add_dependencies(jarvis jarvis_sherpa_wake_helper)
set_target_properties(jarvis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    OUTPUT_NAME "vaxil"
)

if(WIN32)
    # Build as a Windows GUI app to prevent an extra console window.
    set_target_properties(jarvis PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_DEBUG)
if(NOT QT_CORE_DLL)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
endif()
get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)
    add_custom_command(TARGET jarvis PRE_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DDEPLOY_DIR="$<TARGET_FILE_DIR:jarvis>"
            -DPRESERVE_ENTRIES="$<TARGET_FILE_NAME:jarvis_sherpa_wake_helper>"
            -P "${CMAKE_SOURCE_DIR}/cmake/CleanDeployDir.cmake"
        COMMENT "Cleaning deployed runtime directory"
    )
    add_custom_command(TARGET jarvis POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --verbose 0
            --no-translations
            --qmldir "${CMAKE_SOURCE_DIR}/src/gui/qml"
            "$<TARGET_FILE:jarvis>"
        COMMENT "Deploying Qt runtime for Vaxil"
    )
endif()
jarvis_copy_optional_runtime_libraries(jarvis)
if(WIN32 AND JARVIS_BUNDLE_PYTHON_RUNTIME)
    add_dependencies(jarvis vaxil_python_runtime_bundle)
    add_custom_command(TARGET jarvis POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "$<TARGET_FILE_DIR:jarvis>/python_runtime"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:jarvis>/python_runtime"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${JARVIS_PYTHON_RUNTIME_DIST_DIR}/vaxil_python_runtime"
            "$<TARGET_FILE_DIR:jarvis>/python_runtime"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${JARVIS_PYTHON_RUNTIME_BROWSERS_DIR}"
            "$<TARGET_FILE_DIR:jarvis>/python_runtime/ms-playwright"
        COMMENT "Copying bundled Python runtime"
    )
else()
    add_custom_command(TARGET jarvis POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/python_runtime"
            "$<TARGET_FILE_DIR:jarvis>/python_runtime"
        COMMENT "Copying Python runtime assets"
    )
endif()

if(WIN32)
    set(JARVIS_WINDOWS_PORTABLE_STAGE_DIR "${CMAKE_BINARY_DIR}/portable/vaxil")
    set(JARVIS_WINDOWS_PORTABLE_ZIP "${CMAKE_BINARY_DIR}/vaxil-windows-portable.zip")
    add_custom_target(vaxil_windows_portable_zip
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${JARVIS_WINDOWS_PORTABLE_STAGE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${JARVIS_WINDOWS_PORTABLE_STAGE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_FILE_DIR:jarvis>" "${JARVIS_WINDOWS_PORTABLE_STAGE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${JARVIS_WINDOWS_PORTABLE_ZIP}"
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${JARVIS_WINDOWS_PORTABLE_ZIP}" --format=zip portable/vaxil
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Packaging Vaxil Windows portable zip"
        VERBATIM
    )
    add_dependencies(vaxil_windows_portable_zip jarvis)
endif()

if(JARVIS_BUILD_TESTS)
    enable_testing()

    qt_add_executable(jarvis_reasoning_tests
        tests/ReasoningRouterTests.cpp
    )

    target_link_libraries(jarvis_reasoning_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_reasoning_tests COMMAND jarvis_reasoning_tests)
    set_tests_properties(jarvis_reasoning_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_reasoning_tests)

    qt_add_executable(jarvis_ai_services_tests
        tests/AiServicesTests.cpp
    )

    target_link_libraries(jarvis_ai_services_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_ai_services_tests COMMAND jarvis_ai_services_tests)
    set_tests_properties(jarvis_ai_services_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_ai_services_tests)

    qt_add_executable(jarvis_behavior_policy_tests
        tests/AssistantBehaviorPolicyTests.cpp
    )

    target_link_libraries(jarvis_behavior_policy_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_behavior_policy_tests COMMAND jarvis_behavior_policy_tests)
    set_tests_properties(jarvis_behavior_policy_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_behavior_policy_tests)

    qt_add_executable(jarvis_permission_override_scenario_tests
        tests/PermissionOverrideScenarioTests.cpp
        src/core/ActionRiskPermissionService.cpp
        src/core/ToolPermissionRegistry.cpp
        src/telemetry/BehavioralEventLedger.cpp
    )

    target_include_directories(jarvis_permission_override_scenario_tests PRIVATE src)
    target_link_libraries(jarvis_permission_override_scenario_tests
        PRIVATE
            Qt6::Core
            Qt6::Sql
            Qt6::Test
            nlohmann_json::nlohmann_json
    )

    add_test(NAME jarvis_permission_override_scenario_tests COMMAND jarvis_permission_override_scenario_tests)
    set_tests_properties(jarvis_permission_override_scenario_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_permission_override_scenario_tests)

    qt_add_executable(jarvis_execution_narrator_tests
        tests/ExecutionNarratorTests.cpp
    )

    target_link_libraries(jarvis_execution_narrator_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_execution_narrator_tests COMMAND jarvis_execution_narrator_tests)
    set_tests_properties(jarvis_execution_narrator_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_execution_narrator_tests)

    qt_add_executable(jarvis_tool_coordinator_tests
        tests/ToolCoordinatorTests.cpp
    )

    target_link_libraries(jarvis_tool_coordinator_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_tool_coordinator_tests COMMAND jarvis_tool_coordinator_tests)
    set_tests_properties(jarvis_tool_coordinator_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_tool_coordinator_tests)

    qt_add_executable(jarvis_local_response_tests
        tests/LocalResponseEngineTests.cpp
    )

    target_link_libraries(jarvis_local_response_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_local_response_tests COMMAND jarvis_local_response_tests)
    set_tests_properties(jarvis_local_response_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_local_response_tests)

    qt_add_executable(jarvis_identity_tests
        tests/IdentityProfileTests.cpp
    )

    target_link_libraries(jarvis_identity_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_identity_tests COMMAND jarvis_identity_tests)
    set_tests_properties(jarvis_identity_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_identity_tests)

    qt_add_executable(jarvis_platform_runtime_tests
        tests/PlatformRuntimeTests.cpp
    )

    target_link_libraries(jarvis_platform_runtime_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_platform_runtime_tests COMMAND jarvis_platform_runtime_tests)
    set_tests_properties(jarvis_platform_runtime_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_platform_runtime_tests)

    qt_add_executable(jarvis_vision_tests
        tests/VisionStateTests.cpp
    )

    target_link_libraries(jarvis_vision_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_vision_tests COMMAND jarvis_vision_tests)
    set_tests_properties(jarvis_vision_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_vision_tests)

    qt_add_executable(jarvis_memory_tests
        tests/MemoryManagerTests.cpp
    )

    target_link_libraries(jarvis_memory_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_memory_tests COMMAND jarvis_memory_tests)
    set_tests_properties(jarvis_memory_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_memory_tests)

    qt_add_executable(jarvis_memory_store_tests
        tests/MemoryStoreTests.cpp
    )

    target_link_libraries(jarvis_memory_store_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_memory_store_tests COMMAND jarvis_memory_store_tests)
    set_tests_properties(jarvis_memory_store_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_memory_store_tests)

    qt_add_executable(jarvis_memory_policy_tests
        tests/MemoryPolicyHandlerTests.cpp
    )

    target_link_libraries(jarvis_memory_policy_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_memory_policy_tests COMMAND jarvis_memory_policy_tests)
    set_tests_properties(jarvis_memory_policy_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_memory_policy_tests)

    qt_add_executable(jarvis_selection_context_tests
        tests/SelectionContextCompilerTests.cpp
    )

    target_link_libraries(jarvis_selection_context_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_selection_context_tests COMMAND jarvis_selection_context_tests)
    set_tests_properties(jarvis_selection_context_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_selection_context_tests)

    qt_add_executable(jarvis_selection_context_tuning_tests
        tests/SelectionContextCompilerTuningTests.cpp
    )

    target_link_libraries(jarvis_selection_context_tuning_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_selection_context_tuning_tests COMMAND jarvis_selection_context_tuning_tests)
    set_tests_properties(jarvis_selection_context_tuning_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_selection_context_tuning_tests)

    qt_add_executable(jarvis_behavior_tuning_policy_tests
        tests/CompiledContextPolicyTuningPromotionPolicyTests.cpp
        src/behavior_tuning/CompiledContextPolicyTuningPromotionPolicy.cpp
    )

    target_include_directories(jarvis_behavior_tuning_policy_tests PRIVATE src)
    target_link_libraries(jarvis_behavior_tuning_policy_tests
        PRIVATE
            Qt6::Core
            Qt6::Test
    )

    add_test(NAME jarvis_behavior_tuning_policy_tests COMMAND jarvis_behavior_tuning_policy_tests)
    set_tests_properties(jarvis_behavior_tuning_policy_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_behavior_tuning_policy_tests)

    qt_add_executable(jarvis_feedback_signal_tests
        tests/FeedbackSignalEventBuilderTests.cpp
        src/behavior_tuning/FeedbackSignalEventBuilder.cpp
    )

    target_include_directories(jarvis_feedback_signal_tests PRIVATE src)
    target_link_libraries(jarvis_feedback_signal_tests
        PRIVATE
            Qt6::Core
            Qt6::Test
    )

    add_test(NAME jarvis_feedback_signal_tests COMMAND jarvis_feedback_signal_tests)
    set_tests_properties(jarvis_feedback_signal_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_feedback_signal_tests)

    qt_add_executable(jarvis_tuning_episode_feedback_scorer_tests
        tests/TuningEpisodeFeedbackScorerTests.cpp
        src/behavior_tuning/TuningEpisodeFeedbackScorer.cpp
    )

    target_include_directories(jarvis_tuning_episode_feedback_scorer_tests PRIVATE src)
    target_link_libraries(jarvis_tuning_episode_feedback_scorer_tests
        PRIVATE
            Qt6::Core
            Qt6::Test
    )

    add_test(NAME jarvis_tuning_episode_feedback_scorer_tests COMMAND jarvis_tuning_episode_feedback_scorer_tests)
    set_tests_properties(jarvis_tuning_episode_feedback_scorer_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_tuning_episode_feedback_scorer_tests)

    qt_add_executable(jarvis_response_finalizer_tests
        tests/ResponseFinalizerTests.cpp
    )

    target_link_libraries(jarvis_response_finalizer_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_response_finalizer_tests COMMAND jarvis_response_finalizer_tests)
    set_tests_properties(jarvis_response_finalizer_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_response_finalizer_tests)

    qt_add_executable(jarvis_settings_tests
        tests/AppSettingsTests.cpp
        src/core/PermissionOverrideSettings.cpp
        src/core/ToolPermissionRegistry.cpp
        src/settings/AppSettings.cpp
    )

    target_include_directories(jarvis_settings_tests PRIVATE src)
    target_link_libraries(jarvis_settings_tests
        PRIVATE
            Qt6::Core
            Qt6::Test
            nlohmann_json::nlohmann_json
    )

    add_test(NAME jarvis_settings_tests COMMAND jarvis_settings_tests)
    set_tests_properties(jarvis_settings_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_settings_tests)

    qt_add_executable(jarvis_cooldown_tests
        tests/CooldownEngineTests.cpp
        src/cognition/CooldownEngine.cpp
    )

    target_include_directories(jarvis_cooldown_tests PRIVATE src)
    target_link_libraries(jarvis_cooldown_tests
        PRIVATE
            Qt6::Core
            Qt6::Test
    )

    add_test(NAME jarvis_cooldown_tests COMMAND jarvis_cooldown_tests)
    set_tests_properties(jarvis_cooldown_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_cooldown_tests)

    qt_add_executable(jarvis_behavioral_ledger_tests
        tests/BehavioralEventLedgerTests.cpp
        src/telemetry/BehavioralEventLedger.cpp
    )

    target_include_directories(jarvis_behavioral_ledger_tests PRIVATE src)
    target_link_libraries(jarvis_behavioral_ledger_tests
        PRIVATE
            Qt6::Core
            Qt6::Sql
            Qt6::Test
    )

    add_test(NAME jarvis_behavioral_ledger_tests COMMAND jarvis_behavioral_ledger_tests)
    set_tests_properties(jarvis_behavioral_ledger_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_behavioral_ledger_tests)

    qt_add_executable(jarvis_learning_data_tests
        tests/LearningDataCollectorTests.cpp
    )

    target_link_libraries(jarvis_learning_data_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_learning_data_tests COMMAND jarvis_learning_data_tests)
    set_tests_properties(jarvis_learning_data_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_learning_data_tests)

    if(Python3_EXECUTABLE)
        add_test(
            NAME jarvis_file_cap_check
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/scripts/check_file_caps.py
                --root ${CMAKE_SOURCE_DIR}
                --allowlist ${CMAKE_SOURCE_DIR}/config/file_caps_legacy_allowlist.txt
                --max-lines 500
        )
    endif()

    qt_add_executable(jarvis_wakeword_tests
        tests/WakeWordDetectorTests.cpp
    )

    target_link_libraries(jarvis_wakeword_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_wakeword_tests COMMAND jarvis_wakeword_tests)
    set_tests_properties(jarvis_wakeword_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_wakeword_tests)

    qt_add_executable(jarvis_skill_store_tests
        tests/SkillStoreTests.cpp
    )

    target_link_libraries(jarvis_skill_store_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_skill_store_tests COMMAND jarvis_skill_store_tests)
    set_tests_properties(jarvis_skill_store_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_skill_store_tests)

    qt_add_executable(jarvis_desktop_context_tests
        tests/DesktopContextThreadBuilderTests.cpp
        src/perception/DesktopContextThreadBuilder.cpp
    )

    target_include_directories(jarvis_desktop_context_tests PRIVATE src)
    target_link_libraries(jarvis_desktop_context_tests PRIVATE Qt6::Core Qt6::Test)

    add_test(NAME jarvis_desktop_context_tests COMMAND jarvis_desktop_context_tests)
    set_tests_properties(jarvis_desktop_context_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_desktop_context_tests)

    qt_add_executable(jarvis_desktop_source_adapter_tests
        tests/DesktopSourceContextAdapterTests.cpp
        src/perception/DesktopSourceContextAdapter.cpp
    )

    target_include_directories(jarvis_desktop_source_adapter_tests PRIVATE src)
    target_link_libraries(jarvis_desktop_source_adapter_tests PRIVATE Qt6::Core Qt6::Test)

    add_test(NAME jarvis_desktop_source_adapter_tests COMMAND jarvis_desktop_source_adapter_tests)
    set_tests_properties(jarvis_desktop_source_adapter_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_desktop_source_adapter_tests)

    qt_add_executable(jarvis_desktop_selection_tests
        tests/DesktopContextSelectionBuilderTests.cpp
        src/cognition/DesktopContextSelectionBuilder.cpp
    )

    target_include_directories(jarvis_desktop_selection_tests PRIVATE src)
    target_link_libraries(jarvis_desktop_selection_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_desktop_selection_tests COMMAND jarvis_desktop_selection_tests)
    set_tests_properties(jarvis_desktop_selection_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_desktop_selection_tests)

    qt_add_executable(jarvis_connector_signal_tests
        tests/ConnectorResultSignalTests.cpp
        src/cognition/ConnectorResultSignal.cpp
    )

    target_include_directories(jarvis_connector_signal_tests PRIVATE src)
    target_link_libraries(jarvis_connector_signal_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_connector_signal_tests COMMAND jarvis_connector_signal_tests)
    set_tests_properties(jarvis_connector_signal_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_connector_signal_tests)

    qt_add_executable(jarvis_connector_event_tests
        tests/ConnectorEventBuilderTests.cpp
        src/cognition/ConnectorEventBuilder.cpp
        src/cognition/ConnectorResultSignal.cpp
    )

    target_include_directories(jarvis_connector_event_tests PRIVATE src)
    target_link_libraries(jarvis_connector_event_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_connector_event_tests COMMAND jarvis_connector_event_tests)
    set_tests_properties(jarvis_connector_event_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_connector_event_tests)

    qt_add_executable(jarvis_connector_history_tests
        tests/ConnectorHistoryTrackerTests.cpp
        src/cognition/ConnectorHistoryTracker.cpp
        src/memory/MemoryStore.cpp
        src/memory/MemoryManager.cpp
    )

    target_include_directories(jarvis_connector_history_tests PRIVATE src)
    target_link_libraries(jarvis_connector_history_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_connector_history_tests COMMAND jarvis_connector_history_tests)
    set_tests_properties(jarvis_connector_history_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_connector_history_tests)

    qt_add_executable(jarvis_connector_context_tests
        tests/ConnectorContextCompilerTests.cpp
        src/cognition/ConnectorContextCompiler.cpp
    )

    target_include_directories(jarvis_connector_context_tests PRIVATE src)
    target_link_libraries(jarvis_connector_context_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_connector_context_tests COMMAND jarvis_connector_context_tests)
    set_tests_properties(jarvis_connector_context_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_connector_context_tests)

    qt_add_executable(jarvis_compiled_context_delta_tests
        tests/CompiledContextDeltaTrackerTests.cpp
        src/cognition/CompiledContextDeltaTracker.cpp
    )

    target_include_directories(jarvis_compiled_context_delta_tests PRIVATE src)
    target_link_libraries(jarvis_compiled_context_delta_tests PRIVATE Qt6::Core Qt6::Test)

    add_test(NAME jarvis_compiled_context_delta_tests COMMAND jarvis_compiled_context_delta_tests)
    set_tests_properties(jarvis_compiled_context_delta_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_compiled_context_delta_tests)

    qt_add_executable(jarvis_prompt_context_policy_tests
        tests/PromptContextTemporalPolicyTests.cpp
        src/cognition/PromptContextTemporalPolicy.cpp
    )

    target_include_directories(jarvis_prompt_context_policy_tests PRIVATE src)
    target_link_libraries(jarvis_prompt_context_policy_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_prompt_context_policy_tests COMMAND jarvis_prompt_context_policy_tests)
    set_tests_properties(jarvis_prompt_context_policy_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_prompt_context_policy_tests)

    qt_add_executable(jarvis_compiled_context_stability_tests
        tests/CompiledContextStabilityTrackerTests.cpp
        src/cognition/CompiledContextStabilityTracker.cpp
    )

    target_include_directories(jarvis_compiled_context_stability_tests PRIVATE src)
    target_link_libraries(jarvis_compiled_context_stability_tests PRIVATE Qt6::Core Qt6::Test)

    add_test(NAME jarvis_compiled_context_stability_tests COMMAND jarvis_compiled_context_stability_tests)
    set_tests_properties(jarvis_compiled_context_stability_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_compiled_context_stability_tests)

    qt_add_executable(jarvis_compiled_context_memory_tests
        tests/CompiledContextStabilityMemoryBuilderTests.cpp
        src/cognition/CompiledContextStabilityMemoryBuilder.cpp
    )

    target_include_directories(jarvis_compiled_context_memory_tests PRIVATE src)
    target_link_libraries(jarvis_compiled_context_memory_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_compiled_context_memory_tests COMMAND jarvis_compiled_context_memory_tests)
    set_tests_properties(jarvis_compiled_context_memory_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_compiled_context_memory_tests)

    qt_add_executable(jarvis_compiled_context_history_tests
        tests/CompiledContextHistorySummaryBuilderTests.cpp
        src/cognition/CompiledContextHistoryPolicy.cpp
        src/cognition/CompiledContextHistorySummaryBuilder.cpp
    )

    target_include_directories(jarvis_compiled_context_history_tests PRIVATE src)
    target_link_libraries(jarvis_compiled_context_history_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_compiled_context_history_tests COMMAND jarvis_compiled_context_history_tests)
    set_tests_properties(jarvis_compiled_context_history_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_compiled_context_history_tests)

    qt_add_executable(jarvis_connector_snapshot_tests
        tests/ConnectorSnapshotEventBuilderTests.cpp
        src/connectors/ConnectorSnapshotEventBuilder.cpp
    )

    target_include_directories(jarvis_connector_snapshot_tests PRIVATE src)
    target_link_libraries(jarvis_connector_snapshot_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_connector_snapshot_tests COMMAND jarvis_connector_snapshot_tests)
    set_tests_properties(jarvis_connector_snapshot_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_connector_snapshot_tests)

    qt_add_executable(jarvis_connector_source_spec_tests
        tests/ConnectorSourceSpecTests.cpp
        src/connectors/ConnectorSourceSpec.cpp
    )

    target_include_directories(jarvis_connector_source_spec_tests PRIVATE src)
    target_link_libraries(jarvis_connector_source_spec_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_connector_source_spec_tests COMMAND jarvis_connector_source_spec_tests)
    set_tests_properties(jarvis_connector_source_spec_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_connector_source_spec_tests)

    qt_add_executable(jarvis_browser_bookmarks_tests
        tests/BrowserBookmarksEventBuilderTests.cpp
        src/connectors/BrowserBookmarksEventBuilder.cpp
    )

    target_include_directories(jarvis_browser_bookmarks_tests PRIVATE src)
    target_link_libraries(jarvis_browser_bookmarks_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_browser_bookmarks_tests COMMAND jarvis_browser_bookmarks_tests)
    set_tests_properties(jarvis_browser_bookmarks_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_browser_bookmarks_tests)

    qt_add_executable(jarvis_calendar_ics_tests
        tests/CalendarIcsEventBuilderTests.cpp
        src/connectors/CalendarIcsEventBuilder.cpp
    )

    target_include_directories(jarvis_calendar_ics_tests PRIVATE src)
    target_link_libraries(jarvis_calendar_ics_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_calendar_ics_tests COMMAND jarvis_calendar_ics_tests)
    set_tests_properties(jarvis_calendar_ics_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_calendar_ics_tests)

    qt_add_executable(jarvis_inbox_maildrop_tests
        tests/InboxMaildropEventBuilderTests.cpp
        src/connectors/InboxMaildropEventBuilder.cpp
    )

    target_include_directories(jarvis_inbox_maildrop_tests PRIVATE src)
    target_link_libraries(jarvis_inbox_maildrop_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_inbox_maildrop_tests COMMAND jarvis_inbox_maildrop_tests)
    set_tests_properties(jarvis_inbox_maildrop_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_inbox_maildrop_tests)

    qt_add_executable(jarvis_notes_directory_tests
        tests/NotesDirectoryEventBuilderTests.cpp
        src/connectors/NotesDirectoryEventBuilder.cpp
    )

    target_include_directories(jarvis_notes_directory_tests PRIVATE src)
    target_link_libraries(jarvis_notes_directory_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_notes_directory_tests COMMAND jarvis_notes_directory_tests)
    set_tests_properties(jarvis_notes_directory_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_notes_directory_tests)

    qt_add_executable(jarvis_proactive_surface_tests
        tests/ProactiveSurfaceGateTests.cpp
        src/cognition/ProactiveSurfaceGate.cpp
    )

    target_include_directories(jarvis_proactive_surface_tests PRIVATE src)
    target_link_libraries(jarvis_proactive_surface_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_proactive_surface_tests COMMAND jarvis_proactive_surface_tests)
    set_tests_properties(jarvis_proactive_surface_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_proactive_surface_tests)

    qt_add_executable(jarvis_proactive_suggestion_tests
        tests/ProactiveSuggestionGateTests.cpp
        src/cognition/ProactiveSuggestionGate.cpp
    )

    target_include_directories(jarvis_proactive_suggestion_tests PRIVATE src)
    target_link_libraries(jarvis_proactive_suggestion_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_proactive_suggestion_tests COMMAND jarvis_proactive_suggestion_tests)
    set_tests_properties(jarvis_proactive_suggestion_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_proactive_suggestion_tests)

    qt_add_executable(jarvis_suggestion_planner_tests
        tests/SuggestionProposalPlannerTests.cpp
        src/cognition/CooldownEngine.cpp
        src/cognition/ProactivePlannerInputEnricher.cpp
        src/cognition/ProactiveSuggestionPlanner.cpp
        src/cognition/ProactiveSuggestionGate.cpp
        src/cognition/SuggestionProposalBuilder.cpp
        src/cognition/SuggestionProposalPolicyScoring.cpp
        src/cognition/DesktopWorkMode.cpp
        src/cognition/SuggestionProposalRanker.cpp
    )

    target_include_directories(jarvis_suggestion_planner_tests PRIVATE src)
    target_link_libraries(jarvis_suggestion_planner_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_suggestion_planner_tests COMMAND jarvis_suggestion_planner_tests)
    set_tests_properties(jarvis_suggestion_planner_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_suggestion_planner_tests)

    qt_add_executable(jarvis_suggestion_builder_metadata_tests
        tests/SuggestionProposalBuilderMetadataTests.cpp
        src/cognition/CooldownEngine.cpp
        src/cognition/ProactivePlannerInputEnricher.cpp
        src/cognition/ProactiveSuggestionPlanner.cpp
        src/cognition/ProactiveSuggestionGate.cpp
        src/cognition/SuggestionProposalBuilder.cpp
        src/cognition/SuggestionProposalPolicyScoring.cpp
        src/cognition/DesktopWorkMode.cpp
        src/cognition/SuggestionProposalRanker.cpp
    )

    target_include_directories(jarvis_suggestion_builder_metadata_tests PRIVATE src)
    target_link_libraries(jarvis_suggestion_builder_metadata_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_suggestion_builder_metadata_tests COMMAND jarvis_suggestion_builder_metadata_tests)
    set_tests_properties(jarvis_suggestion_builder_metadata_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_suggestion_builder_metadata_tests)

    qt_add_executable(jarvis_planner_input_enricher_tests
        tests/ProactivePlannerInputEnricherTests.cpp
        src/cognition/CooldownEngine.cpp
        src/cognition/ProactivePlannerInputEnricher.cpp
        src/cognition/ProactiveSuggestionPlanner.cpp
        src/cognition/ProactiveSuggestionGate.cpp
        src/cognition/SuggestionProposalBuilder.cpp
        src/cognition/SuggestionProposalPolicyScoring.cpp
        src/cognition/DesktopWorkMode.cpp
        src/cognition/SuggestionProposalRanker.cpp
    )

    target_include_directories(jarvis_planner_input_enricher_tests PRIVATE src)
    target_link_libraries(jarvis_planner_input_enricher_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_planner_input_enricher_tests COMMAND jarvis_planner_input_enricher_tests)
    set_tests_properties(jarvis_planner_input_enricher_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_planner_input_enricher_tests)

    qt_add_executable(jarvis_proactive_loop_scenario_tests
        tests/ProactiveSuggestionLoopScenarioTests.cpp
        src/cognition/CooldownEngine.cpp
        src/cognition/ProactiveCooldownTracker.cpp
        src/cognition/ProactivePlannerInputEnricher.cpp
        src/cognition/ProactiveSuggestionPlanner.cpp
        src/cognition/ProactiveSuggestionGate.cpp
        src/cognition/SuggestionProposalBuilder.cpp
        src/cognition/SuggestionProposalPolicyScoring.cpp
        src/cognition/DesktopWorkMode.cpp
        src/cognition/SuggestionProposalRanker.cpp
    )

    target_include_directories(jarvis_proactive_loop_scenario_tests PRIVATE src)
    target_link_libraries(jarvis_proactive_loop_scenario_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_proactive_loop_scenario_tests COMMAND jarvis_proactive_loop_scenario_tests)
    set_tests_properties(jarvis_proactive_loop_scenario_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_proactive_loop_scenario_tests)

    qt_add_executable(jarvis_suggestion_history_policy_tests
        tests/SuggestionProposalHistoryPolicyTests.cpp
        src/cognition/SuggestionProposalPolicyScoring.cpp
        src/cognition/DesktopWorkMode.cpp
        src/cognition/SuggestionProposalRanker.cpp
    )

    target_include_directories(jarvis_suggestion_history_policy_tests PRIVATE src)
    target_link_libraries(jarvis_suggestion_history_policy_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_suggestion_history_policy_tests COMMAND jarvis_suggestion_history_policy_tests)
    set_tests_properties(jarvis_suggestion_history_policy_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_suggestion_history_policy_tests)

    qt_add_executable(jarvis_suggestion_ranker_work_mode_tests
        tests/SuggestionProposalRankerWorkModeTests.cpp
        src/cognition/DesktopWorkMode.cpp
        src/cognition/SuggestionProposalPolicyScoring.cpp
        src/cognition/SuggestionProposalRanker.cpp
    )

    target_include_directories(jarvis_suggestion_ranker_work_mode_tests PRIVATE src)
    target_link_libraries(jarvis_suggestion_ranker_work_mode_tests PRIVATE Qt6::Core Qt6::Test nlohmann_json::nlohmann_json)

    add_test(NAME jarvis_suggestion_ranker_work_mode_tests COMMAND jarvis_suggestion_ranker_work_mode_tests)
    set_tests_properties(jarvis_suggestion_ranker_work_mode_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_suggestion_ranker_work_mode_tests)

    qt_add_executable(jarvis_python_runtime_tests
        tests/PythonRuntimeTests.cpp
    )

    target_link_libraries(jarvis_python_runtime_tests
        PRIVATE
            Qt6::Core
            Qt6::Test
            jarvis_lib
    )

    add_test(NAME jarvis_python_runtime_tests COMMAND jarvis_python_runtime_tests)
    set_tests_properties(jarvis_python_runtime_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_python_runtime_tests)
    if(WIN32 AND JARVIS_BUNDLE_PYTHON_RUNTIME)
        add_dependencies(jarvis_python_runtime_tests vaxil_python_runtime_bundle)
    endif()
endif()
