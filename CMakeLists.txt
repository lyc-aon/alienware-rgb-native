cmake_minimum_required(VERSION 3.20)
project(AlienwareRGB VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED hidapi-hidraw)

message(STATUS "Found HIDAPI: ${HIDAPI_LIBRARIES} include: ${HIDAPI_INCLUDE_DIRS}")

# ── Core (GUI-free): HID + model, shared by GUI binary and CLI binary.
add_library(alienware_rgb_core STATIC
    src/transport/hid_transport.cpp
    src/transport/hid_lock.cpp
    src/controller/awelc_controller.cpp
    src/models/zone_map.cpp
    src/models/config_paths.cpp
    src/models/profile.cpp
    src/models/event_preset.cpp
    src/animator/effects.cpp
    src/animator/animator.cpp
)
target_link_libraries(alienware_rgb_core PUBLIC
    Qt6::Core
    ${HIDAPI_LIBRARIES}
)
target_include_directories(alienware_rgb_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${HIDAPI_INCLUDE_DIRS}
)

# ── GUI executable
add_executable(alienware_rgb
    resources/alienware_rgb.qrc
    src/main.cpp
    src/worker/hid_worker.cpp
    src/gui/main_window.cpp
    src/gui/zone_list_model.cpp
    src/gui/zone_filter_proxy.cpp
    src/gui/zone_card_delegate.cpp
    src/gui/zone_accordion.cpp
    src/gui/discovery_wizard.cpp
    src/gui/event_dialog.cpp
    src/gui/diagnostics_dialog.cpp
)
target_link_libraries(alienware_rgb PRIVATE
    alienware_rgb_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# ── CLI executable (headless, Core only)
add_executable(alienware_rgb_cli
    src/cli/main.cpp
    src/cli/commands.cpp
)
target_link_libraries(alienware_rgb_cli PRIVATE
    alienware_rgb_core
    Qt6::Core
)

install(TARGETS alienware_rgb alienware_rgb_cli DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(alienware_rgb_core_tests
        tests/core_tests.cpp
    )
    target_link_libraries(alienware_rgb_core_tests PRIVATE
        alienware_rgb_core
        Qt6::Core
    )
    target_compile_definitions(alienware_rgb_core_tests PRIVATE
        ALIENWARE_RGB_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME alienware_rgb_core_tests COMMAND alienware_rgb_core_tests)
endif()
