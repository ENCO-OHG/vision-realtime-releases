cmake_minimum_required(VERSION 3.20)

set(VISION_REALTIME_VERSION "0.0.0")
set(VISION_REALTIME_FULL_VERSION "0.0.0")
set(VISION_REALTIME_ROOT_PACKAGE_JSON "${CMAKE_CURRENT_LIST_DIR}/../package.json")
if (EXISTS "${VISION_REALTIME_ROOT_PACKAGE_JSON}")
    file(READ "${VISION_REALTIME_ROOT_PACKAGE_JSON}" VISION_REALTIME_ROOT_PACKAGE_CONTENT)
    string(REGEX MATCH "\"version\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\"" _ "${VISION_REALTIME_ROOT_PACKAGE_CONTENT}")
    if (CMAKE_MATCH_1)
        set(VISION_REALTIME_FULL_VERSION "${CMAKE_MATCH_1}")
        string(REGEX MATCH "^([0-9]+\.[0-9]+\.[0-9]+)" _ "${VISION_REALTIME_FULL_VERSION}")
        if (CMAKE_MATCH_1)
            set(VISION_REALTIME_VERSION "${CMAKE_MATCH_1}")
        endif()
    endif()
endif()

project(vision-realtime VERSION ${VISION_REALTIME_VERSION} LANGUAGES CXX)

option(VISION_REALTIME_WITH_LIB60870 "Build with lib60870-C support" OFF)
set(VISION_REALTIME_LIB60870_DIR "" CACHE PATH "Path to the lib60870-C directory")

set(VISION_REALTIME_SOURCES
    src/api/json_utils.cpp
    src/app/main.cpp
    src/backend/command_tracker.cpp
    src/net/socket_utils.cpp
    src/net/websocket_broadcaster.cpp
    src/runtime/device_registry.cpp
    src/runtime/state_store.cpp
)

if (VISION_REALTIME_WITH_LIB60870)
    if (NOT VISION_REALTIME_LIB60870_DIR)
        message(FATAL_ERROR "VISION_REALTIME_LIB60870_DIR must point to lib60870-C when VISION_REALTIME_WITH_LIB60870=ON")
    endif()

    if (NOT EXISTS "${VISION_REALTIME_LIB60870_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "VISION_REALTIME_LIB60870_DIR does not contain CMakeLists.txt: ${VISION_REALTIME_LIB60870_DIR}")
    endif()

    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${VISION_REALTIME_LIB60870_DIR}" "${CMAKE_BINARY_DIR}/lib60870-C")

    list(APPEND VISION_REALTIME_SOURCES src/backend/lib60870_backend.cpp src/backend/lib60870_probe.cpp)
else()
    list(APPEND VISION_REALTIME_SOURCES src/backend/mock_backend.cpp)
endif()

if (WIN32)
    enable_language(RC)
    set(VISION_REALTIME_ICON_PATH "${CMAKE_CURRENT_LIST_DIR}/../assets/icon.ico")
    if (NOT EXISTS "${VISION_REALTIME_ICON_PATH}")
        message(FATAL_ERROR "Vision Realtime icon not found: ${VISION_REALTIME_ICON_PATH}")
    endif()
    configure_file(src/version/vision_realtime.rc.in generated/vision_realtime.rc @ONLY)
    list(APPEND VISION_REALTIME_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/generated/vision_realtime.rc")
endif()

add_executable(vision-realtime
    ${VISION_REALTIME_SOURCES}
)

configure_file(src/version/gateway_version.h.in generated/gateway_version.h @ONLY)

target_compile_features(vision-realtime PRIVATE cxx_std_20)
target_include_directories(vision-realtime PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_LIST_DIR}/src/api"
    "${CMAKE_CURRENT_LIST_DIR}/src/backend"
    "${CMAKE_CURRENT_LIST_DIR}/src/net"
    "${CMAKE_CURRENT_LIST_DIR}/src/runtime"
    "${CMAKE_CURRENT_LIST_DIR}/src/version"
)

if (MSVC)
    target_compile_options(vision-realtime PRIVATE /W4 /permissive-)
else()
    target_compile_options(vision-realtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (WIN32)
    target_link_libraries(vision-realtime PRIVATE ws2_32)
endif()

if (VISION_REALTIME_WITH_LIB60870)
    target_compile_definitions(vision-realtime PRIVATE VISION_REALTIME_WITH_LIB60870=1 VISION_ONE_IEC104_WITH_LIB60870=1)
    target_include_directories(vision-realtime PRIVATE
        "${VISION_REALTIME_LIB60870_DIR}/config"
        "${VISION_REALTIME_LIB60870_DIR}/src/inc/api"
        "${VISION_REALTIME_LIB60870_DIR}/src/hal/inc"
        "${VISION_REALTIME_LIB60870_DIR}/src/common/inc"
    )
    target_link_libraries(vision-realtime PRIVATE lib60870)

    if (TARGET lib60870-shared)
        add_custom_command(TARGET vision-realtime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:lib60870-shared>
                $<TARGET_FILE_DIR:vision-realtime>
        )
    endif()
endif()

install(TARGETS vision-realtime RUNTIME DESTINATION bin)

include(CTest)
if (BUILD_TESTING)
    add_executable(vision-realtime-command-tracker-tests
        tests/command_tracker_tests.cpp
        src/api/json_utils.cpp
        src/backend/command_tracker.cpp
    )
    target_compile_features(vision-realtime-command-tracker-tests PRIVATE cxx_std_20)
    target_include_directories(vision-realtime-command-tracker-tests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/api"
        "${CMAKE_CURRENT_LIST_DIR}/src/backend"
    )
    add_test(NAME vision-realtime-command-tracker-tests COMMAND vision-realtime-command-tracker-tests)

    add_executable(vision-realtime-state-store-tests
        src/tests/state_store_tests.cpp
        src/api/json_utils.cpp
        src/runtime/state_store.cpp
    )
    target_compile_features(vision-realtime-state-store-tests PRIVATE cxx_std_20)
    target_include_directories(vision-realtime-state-store-tests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/api"
        "${CMAKE_CURRENT_LIST_DIR}/src/runtime"
    )
    add_test(NAME vision-realtime-state-store-tests COMMAND vision-realtime-state-store-tests)
endif()
