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
    Svg
    Test
    WebSockets
    Widgets
)
find_package(Qt6ShaderToolsTools REQUIRED)
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
    src/core/AiRequestCoordinator.cpp
    src/core/AssistantController.cpp
    src/core/InputRouter.cpp
    src/core/IntentRouter.cpp
    src/core/MemoryPolicyHandler.cpp
    src/core/ComputerControl.cpp
    src/core/LocalResponseEngine.cpp
    src/core/ResponseFinalizer.cpp
    src/core/ToolCoordinator.cpp
    src/core/tasks/TaskDispatcher.cpp
    src/core/tasks/ToolWorker.cpp
    src/core/tools/ToolExecutionService.cpp
    src/devices/DeviceManager.cpp
    src/devices/MockDevices.cpp
    src/gui/AgentViewModel.cpp
    src/gui/BackendFacade.cpp
    src/gui/SettingsViewModel.cpp
    src/gui/TaskViewModel.cpp
    src/logging/LoggingService.cpp
    src/memory/MemoryManager.cpp
    src/memory/MemoryStore.cpp
    src/overlay/OverlayController.cpp
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
    src/core/AiRequestCoordinator.h
    src/core/AssistantController.h
    src/core/AssistantTypes.h
    src/core/ComputerControl.h
    src/core/InputRouter.h
    src/core/IntentRouter.h
    src/core/LocalResponseEngine.h
    src/core/MemoryPolicyHandler.h
    src/core/ResponseFinalizer.h
    src/core/ToolCoordinator.h
    src/core/tasks/TaskDispatcher.h
    src/core/tasks/ToolWorker.h
    src/core/tools/ToolExecutionService.h
    src/devices/Device.h
    src/devices/DeviceManager.h
    src/devices/MockDevices.h
    src/gui/AgentViewModel.h
    src/gui/BackendFacade.h
    src/gui/SettingsViewModel.h
    src/logging/LoggingService.h
    src/memory/MemoryManager.h
    src/memory/MemoryStore.h
    src/gui/TaskViewModel.h
    src/overlay/OverlayController.h
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

    qt_add_executable(jarvis_settings_tests
        tests/AppSettingsTests.cpp
    )

    target_link_libraries(jarvis_settings_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_settings_tests COMMAND jarvis_settings_tests)
    set_tests_properties(jarvis_settings_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR}${JARVIS_PATH_ENV_SEPARATOR}$ENV{PATH}")
    jarvis_copy_optional_runtime_libraries(jarvis_settings_tests)

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
