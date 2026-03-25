cmake_minimum_required(VERSION 3.27)

project(JARVIS VERSION 1.0.0 LANGUAGES C CXX)

include(FetchContent)
enable_language(RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(JARVIS_BUILD_TESTS "Build JARVIS tests" ON)
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
            ${JARVIS_SPEEXDSP_ROOT}/include
        PRIVATE
            ${JARVIS_SPEEXDSP_ROOT}/libspeexdsp
    )
    target_compile_definitions(speexdsp PRIVATE FLOATING_POINT USE_SMALLFT EXPORT=)
endif()

if(EXISTS "${JARVIS_RNNOISE_ROOT}/include/rnnoise.h"
    AND EXISTS "${JARVIS_RNNOISE_ROOT}/src/rnnoise_data.c")
    add_library(rnnoise STATIC
        ${JARVIS_RNNOISE_ROOT}/src/celt_lpc.c
        ${JARVIS_RNNOISE_ROOT}/src/denoise.c
        ${JARVIS_RNNOISE_ROOT}/src/kiss_fft.c
        ${JARVIS_RNNOISE_ROOT}/src/nnet.c
        ${JARVIS_RNNOISE_ROOT}/src/nnet_default.c
        ${JARVIS_RNNOISE_ROOT}/src/parse_lpcnet_weights.c
        ${JARVIS_RNNOISE_ROOT}/src/pitch.c
        ${JARVIS_RNNOISE_ROOT}/src/rnn.c
        ${JARVIS_RNNOISE_ROOT}/src/rnnoise_data.c
        ${JARVIS_RNNOISE_ROOT}/src/rnnoise_tables.c
    )
    target_include_directories(rnnoise
        PUBLIC
            ${JARVIS_RNNOISE_ROOT}/include
        PRIVATE
            ${JARVIS_RNNOISE_ROOT}/src
    )
endif()

qt_standard_project_setup(REQUIRES 6.6)

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
    src/core/AssistantController.cpp
    src/core/IntentRouter.cpp
    src/core/LocalResponseEngine.cpp
    src/devices/DeviceManager.cpp
    src/devices/MockDevices.cpp
    src/gui/BackendFacade.cpp
    src/logging/LoggingService.cpp
    src/memory/MemoryStore.cpp
    src/overlay/OverlayController.cpp
    src/settings/AppSettings.cpp
    src/settings/IdentityProfileService.cpp
    src/skills/SkillStore.cpp
    src/stt/WhisperSttEngine.cpp
    src/stt/RuntimeSpeechRecognizer.cpp
    src/tools/ToolManager.cpp
    src/tts/PiperTtsEngine.cpp
    src/tts/WorkerTtsEngine.cpp
    src/wakeword/SherpaWakeWordEngine.cpp
    src/wakeword/WakeWordEnginePrecise.cpp
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
    src/core/AssistantController.h
    src/core/AssistantTypes.h
    src/core/IntentRouter.h
    src/core/LocalResponseEngine.h
    src/devices/Device.h
    src/devices/DeviceManager.h
    src/devices/MockDevices.h
    src/gui/BackendFacade.h
    src/logging/LoggingService.h
    src/memory/MemoryStore.h
    src/overlay/OverlayController.h
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
    src/wakeword/SherpaWakeWordEngine.h
    src/wakeword/WakeWordEngine.h
    src/wakeword/WakeWordEnginePrecise.h
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
        "/qt/qml/JARVIS/gui/shaders"
    FILES
        src/gui/shaders/orb.frag
)

target_include_directories(jarvis_lib
    PUBLIC
        src
    PRIVATE
        ${libfvad_SOURCE_DIR}/include
)
target_compile_definitions(jarvis_lib PUBLIC JARVIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(jarvis_lib PUBLIC JARVIS_HAS_ONNXRUNTIME=0)
target_compile_definitions(jarvis_lib PUBLIC JARVIS_HAS_SHERPA_ONNX=0)

if(NOT JARVIS_ONNXRUNTIME_ROOT)
    file(GLOB JARVIS_ONNXRUNTIME_CANDIDATES RELATIVE "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/onnxruntime-win-x64-*")
    list(LENGTH JARVIS_ONNXRUNTIME_CANDIDATES JARVIS_ONNXRUNTIME_CANDIDATE_COUNT)
    if(JARVIS_ONNXRUNTIME_CANDIDATE_COUNT GREATER 0)
        list(GET JARVIS_ONNXRUNTIME_CANDIDATES 0 JARVIS_ONNXRUNTIME_ROOT)
        set(JARVIS_ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/${JARVIS_ONNXRUNTIME_ROOT}")
    endif()
endif()

if(JARVIS_ONNXRUNTIME_ROOT
    AND EXISTS "${JARVIS_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h"
    AND EXISTS "${JARVIS_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    add_library(onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_IMPLIB "${JARVIS_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
        IMPORTED_LOCATION "${JARVIS_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
    )
    target_include_directories(jarvis_lib PRIVATE "${JARVIS_ONNXRUNTIME_ROOT}/include")
    target_link_libraries(jarvis_lib PUBLIC onnxruntime)
    target_compile_definitions(jarvis_lib PUBLIC JARVIS_HAS_ONNXRUNTIME=1)
endif()

if(NOT JARVIS_SHERPA_ROOT)
    file(GLOB JARVIS_SHERPA_CANDIDATES RELATIVE "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/third_party/sherpa-onnx/sherpa-onnx-v*-win-x64-shared-MD-Release-no-tts")
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
    AND EXISTS "${JARVIS_SHERPA_ROOT}/include/sherpa-onnx/c-api/cxx-api.h"
    AND EXISTS "${JARVIS_SHERPA_ROOT}/lib/sherpa-onnx-cxx-api.lib"
    AND EXISTS "${JARVIS_SHERPA_ROOT}/lib/sherpa-onnx-c-api.lib"
    AND TARGET sentencepiece-static)
    add_library(jarvis_sherpa_onnx_c_api SHARED IMPORTED)
    set_target_properties(jarvis_sherpa_onnx_c_api PROPERTIES
        IMPORTED_IMPLIB "${JARVIS_SHERPA_ROOT}/lib/sherpa-onnx-c-api.lib"
        IMPORTED_LOCATION "${JARVIS_SHERPA_ROOT}/lib/sherpa-onnx-c-api.dll"
    )
    add_library(jarvis_sherpa_onnx_cxx_api SHARED IMPORTED)
    set_target_properties(jarvis_sherpa_onnx_cxx_api PROPERTIES
        IMPORTED_IMPLIB "${JARVIS_SHERPA_ROOT}/lib/sherpa-onnx-cxx-api.lib"
        IMPORTED_LOCATION "${JARVIS_SHERPA_ROOT}/lib/sherpa-onnx-cxx-api.dll"
    )
    target_include_directories(jarvis_lib PRIVATE "${JARVIS_SHERPA_ROOT}/include")
    target_link_libraries(jarvis_lib PUBLIC
        jarvis_sherpa_onnx_c_api
        jarvis_sherpa_onnx_cxx_api
        sentencepiece-static
    )
    target_compile_definitions(jarvis_lib PUBLIC JARVIS_HAS_SHERPA_ONNX=1)
endif()

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

function(jarvis_copy_optional_runtime_dlls target_name)
    if(TARGET onnxruntime)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${JARVIS_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
                "$<TARGET_FILE_DIR:${target_name}>/onnxruntime.dll"
        )
    endif()

    if(TARGET jarvis_sherpa_onnx_cxx_api)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${JARVIS_SHERPA_ROOT}/lib/sherpa-onnx-c-api.dll"
                "$<TARGET_FILE_DIR:${target_name}>/sherpa-onnx-c-api.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${JARVIS_SHERPA_ROOT}/lib/sherpa-onnx-cxx-api.dll"
                "$<TARGET_FILE_DIR:${target_name}>/sherpa-onnx-cxx-api.dll"
        )
        if(EXISTS "${JARVIS_SHERPA_ROOT}/lib/onnxruntime_providers_shared.dll")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${JARVIS_SHERPA_ROOT}/lib/onnxruntime_providers_shared.dll"
                    "$<TARGET_FILE_DIR:${target_name}>/onnxruntime_providers_shared.dll"
            )
        endif()
    endif()
endfunction()

qt_add_executable(jarvis_sherpa_wake_helper
    src/wakeword/SherpaWakeHelperMain.cpp
)

target_link_libraries(jarvis_sherpa_wake_helper
    PRIVATE
        Qt6::Core
        Qt6::Multimedia
)

target_compile_definitions(jarvis_sherpa_wake_helper PRIVATE JARVIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(jarvis_sherpa_wake_helper PRIVATE JARVIS_HAS_SHERPA_ONNX=0)

if(TARGET jarvis_sherpa_onnx_cxx_api)
    target_include_directories(jarvis_sherpa_wake_helper PRIVATE "${JARVIS_SHERPA_ROOT}/include")
    target_link_libraries(jarvis_sherpa_wake_helper
        PRIVATE
            jarvis_sherpa_onnx_c_api
            jarvis_sherpa_onnx_cxx_api
    )
    target_compile_definitions(jarvis_sherpa_wake_helper PRIVATE JARVIS_HAS_SHERPA_ONNX=1)
endif()

qt_add_executable(jarvis
    src/app/main.cpp
    src/app/jarvis.rc
)

target_link_libraries(jarvis PRIVATE jarvis_lib)
add_dependencies(jarvis jarvis_sherpa_wake_helper)
set_target_properties(jarvis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
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
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

if(WIN32)
    add_custom_command(TARGET jarvis PRE_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DDEPLOY_DIR="$<TARGET_FILE_DIR:jarvis>"
            -P "${CMAKE_SOURCE_DIR}/cmake/CleanDeployDir.cmake"
        COMMENT "Cleaning deployed runtime directory"
    )
    add_custom_command(TARGET jarvis POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --verbose 0
            --no-translations
            --qmldir "${CMAKE_SOURCE_DIR}/src/gui/qml"
            "$<TARGET_FILE:jarvis>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:jarvis_sherpa_wake_helper>"
            "$<TARGET_FILE_DIR:jarvis>/jarvis_sherpa_wake_helper.exe"
        COMMENT "Deploying Qt runtime for jarvis.exe"
    )
    jarvis_copy_optional_runtime_dlls(jarvis)
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
    set_tests_properties(jarvis_reasoning_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR};$ENV{PATH}")
    jarvis_copy_optional_runtime_dlls(jarvis_reasoning_tests)

    qt_add_executable(jarvis_ai_services_tests
        tests/AiServicesTests.cpp
    )

    target_link_libraries(jarvis_ai_services_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_ai_services_tests COMMAND jarvis_ai_services_tests)
    set_tests_properties(jarvis_ai_services_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR};$ENV{PATH}")
    jarvis_copy_optional_runtime_dlls(jarvis_ai_services_tests)

    qt_add_executable(jarvis_local_response_tests
        tests/LocalResponseEngineTests.cpp
    )

    target_link_libraries(jarvis_local_response_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_local_response_tests COMMAND jarvis_local_response_tests)
    set_tests_properties(jarvis_local_response_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR};$ENV{PATH}")
    jarvis_copy_optional_runtime_dlls(jarvis_local_response_tests)

    qt_add_executable(jarvis_identity_tests
        tests/IdentityProfileTests.cpp
    )

    target_link_libraries(jarvis_identity_tests
        PRIVATE
            jarvis_lib
            Qt6::Test
    )

    add_test(NAME jarvis_identity_tests COMMAND jarvis_identity_tests)
    set_tests_properties(jarvis_identity_tests PROPERTIES ENVIRONMENT "PATH=${QT_BIN_DIR};$ENV{PATH}")
    jarvis_copy_optional_runtime_dlls(jarvis_identity_tests)
endif()
