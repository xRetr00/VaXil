cmake_minimum_required(VERSION 3.27)

project(JARVIS VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)
enable_language(RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(JARVIS_BUILD_TESTS "Build JARVIS tests" ON)

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

qt_standard_project_setup(REQUIRES 6.6)

set(JARVIS_SOURCES
    src/app/JarvisApplication.cpp
    src/ai/LmStudioClient.cpp
    src/ai/ModelCatalogService.cpp
    src/ai/PromptAdapter.cpp
    src/ai/ReasoningRouter.cpp
    src/ai/StreamAssembler.cpp
    src/audio/AudioInputService.cpp
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
    src/stt/WhisperSttEngine.cpp
    src/tts/PiperTtsEngine.cpp
)

set(JARVIS_HEADERS
    src/app/JarvisApplication.h
    src/ai/LmStudioClient.h
    src/ai/ModelCatalogService.h
    src/ai/PromptAdapter.h
    src/ai/ReasoningRouter.h
    src/ai/StreamAssembler.h
    src/audio/AudioInputService.h
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
    src/stt/WhisperSttEngine.h
    src/tts/PiperTtsEngine.h
)

qt_add_resources(JARVIS_RESOURCES src/gui/resources.qrc)

add_library(jarvis_lib STATIC
    ${JARVIS_SOURCES}
    ${JARVIS_HEADERS}
    ${JARVIS_RESOURCES}
)

target_include_directories(jarvis_lib PUBLIC src)
target_compile_definitions(jarvis_lib PUBLIC JARVIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

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
)

qt_add_executable(jarvis
    src/app/main.cpp
    src/app/jarvis.rc
)

target_link_libraries(jarvis PRIVATE jarvis_lib)
set_target_properties(jarvis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

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
        COMMENT "Deploying Qt runtime for jarvis.exe"
    )
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
endif()
