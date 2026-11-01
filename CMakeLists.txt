# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
cmake_minimum_required(VERSION 3.20)
project(msfs-linux-companion VERSION 0.8.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "msfs-linux-companion is Linux only (evdev + Wayland/X11).")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3>=3.4)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
if(NOT EXISTS ${IMGUI_DIR}/imgui.h)
  message(FATAL_ERROR "Dear ImGui submodule missing. Run: git submodule update --init --recursive")
endif()

# An object library: every function of Dear ImGui ends up in the program (a static archive would only bring in what the program itself
# calls), so a plugin can call any of them.
add_library(imgui OBJECT
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
  ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC PkgConfig::GLFW OpenGL::GL)

add_executable(msfs-linux-companion
  src/main.cpp
  src/sim/SimConnectClient.cpp
  src/sim/SimBridge.cpp
  src/input/Evdev.cpp
  src/haptics/WinwingHid.cpp
  src/haptics/Haptics.cpp
  src/app/Profile.cpp
  src/app/Catalog.cpp
  src/app/AxisTargets.cpp
  src/app/Guide.cpp
  src/app/CockpitWatch.cpp
  src/app/Keypad.cpp
  src/app/BindingView.cpp
  src/app/Engine.cpp
  src/app/Script.cpp
  src/app/Paths.cpp
  src/ui/Ui.cpp
  src/ui/AppContext.cpp
  src/ui/UiBindings.cpp
  src/ui/UiCalibration.cpp
  src/ui/UiCatalog.cpp
  src/ui/UiCockpit.cpp
  src/ui/UiDevices.cpp
  src/ui/UiGuides.cpp
  src/ui/UiSettings.cpp
  src/ui/UiVibration.cpp
  src/plugin/PluginManager.cpp
)
target_include_directories(msfs-linux-companion PRIVATE src third_party)
target_link_libraries(msfs-linux-companion PRIVATE imgui Threads::Threads ${CMAKE_DL_LIBS})
# Plugins (src/plugin/Plugin.hpp) are loaded into the program and use its functions: they are exported.
set_target_properties(msfs-linux-companion PROPERTIES ENABLE_EXPORTS ON)
target_compile_definitions(msfs-linux-companion PRIVATE
  COMPANION_VERSION="${PROJECT_VERSION}"
  COMPANION_SOURCE_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)
target_compile_options(msfs-linux-companion PRIVATE -Wall -Wextra -Wno-unused-parameter)

option(COMPANION_UI_LINT "Developer build: report Dear ImGui widget id conflicts and recoverable errors on stderr (src/ui/UiLint.hpp)" OFF)
if(COMPANION_UI_LINT)
  target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)
  target_compile_definitions(msfs-linux-companion PRIVATE COMPANION_UI_LINT)
  target_sources(msfs-linux-companion PRIVATE src/ui/UiLint.cpp)
endif()

option(COMPANION_STATIC_CXX "Link libstdc++ and libgcc statically (for portable AppImage builds)" OFF)
if(COMPANION_STATIC_CXX)
  target_link_options(msfs-linux-companion PRIVATE -static-libstdc++ -static-libgcc)
endif()

install(TARGETS msfs-linux-companion RUNTIME DESTINATION bin)
install(DIRECTORY data/ DESTINATION share/msfs-linux-companion/data)
install(DIRECTORY DESTINATION lib/msfs-linux-companion/plugins)
install(FILES packaging/msfs-linux-companion.desktop DESTINATION share/applications)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION share/licenses/msfs-linux-companion)
install(FILES third_party/imgui/LICENSE.txt DESTINATION share/licenses/msfs-linux-companion RENAME LICENSE.imgui)
install(FILES third_party/nlohmann/LICENSE.MIT DESTINATION share/licenses/msfs-linux-companion RENAME LICENSE.nlohmann-json)
install(FILES packaging/icons/msfs-linux-companion.svg DESTINATION share/icons/hicolor/scalable/apps)
foreach(size 16 32 48 64 128 256 512)
  install(FILES packaging/icons/msfs-linux-companion-${size}.png DESTINATION share/icons/hicolor/${size}x${size}/apps RENAME msfs-linux-companion.png)
endforeach()

option(COMPANION_BUILD_TESTS "Build the config compatibility test (ctest)" ON)
if(COMPANION_BUILD_TESTS)
  enable_testing()
  add_executable(config_roundtrip tests/config_roundtrip.cpp src/app/Profile.cpp src/input/Evdev.cpp)
  target_include_directories(config_roundtrip PRIVATE src third_party)
  target_link_libraries(config_roundtrip PRIVATE Threads::Threads)
  target_compile_definitions(config_roundtrip PRIVATE COMPANION_TEST_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  add_test(NAME config_roundtrip COMMAND config_roundtrip)
  add_executable(axis_engine tests/axis_engine.cpp src/app/Engine.cpp src/app/Profile.cpp src/app/Paths.cpp src/input/Evdev.cpp
                 src/sim/SimBridge.cpp src/sim/SimConnectClient.cpp)
  target_include_directories(axis_engine PRIVATE src third_party)
  target_link_libraries(axis_engine PRIVATE Threads::Threads)
  add_test(NAME axis_engine COMMAND axis_engine)
  add_executable(live_catalog tests/live_catalog.cpp src/app/Catalog.cpp src/app/AxisTargets.cpp src/app/Profile.cpp src/input/Evdev.cpp src/sim/SimConnectClient.cpp)
  target_include_directories(live_catalog PRIVATE src third_party)
  target_link_libraries(live_catalog PRIVATE Threads::Threads)
  add_test(NAME live_catalog COMMAND live_catalog)
  add_executable(curated_catalogs tests/curated_catalogs.cpp src/app/Catalog.cpp src/app/AxisTargets.cpp src/app/Profile.cpp src/input/Evdev.cpp)
  target_include_directories(curated_catalogs PRIVATE src third_party)
  add_test(NAME curated_catalogs COMMAND curated_catalogs ${CMAKE_SOURCE_DIR})
  add_executable(binding_view tests/binding_view.cpp src/app/BindingView.cpp src/app/Catalog.cpp src/app/AxisTargets.cpp src/app/Profile.cpp src/input/Evdev.cpp)
  target_include_directories(binding_view PRIVATE src third_party)
  add_test(NAME binding_view COMMAND binding_view)
  add_executable(cockpit_watch tests/cockpit_watch.cpp src/app/CockpitWatch.cpp)
  target_include_directories(cockpit_watch PRIVATE src third_party)
  add_test(NAME cockpit_watch COMMAND cockpit_watch)
  add_executable(guide_flow tests/guide_flow.cpp src/app/Guide.cpp src/app/Keypad.cpp)
  target_include_directories(guide_flow PRIVATE src third_party)
  add_test(NAME guide_flow COMMAND guide_flow ${CMAKE_SOURCE_DIR})
  # end to end through the real binary against the mock simulator posing as the CJ4
  add_test(NAME live_controls COMMAND bash ${CMAKE_SOURCE_DIR}/tests/live_controls.sh $<TARGET_FILE:msfs-linux-companion> ${CMAKE_SOURCE_DIR})
  # Every documented command-line option must be accepted (exit code 2 = "unknown option"). Regression test for
  # a refactor that once silently dropped --config/--host/--port/--profile/--catalog/--motor-sweep.
  add_test(NAME probe_controls COMMAND python3 -I ${CMAKE_SOURCE_DIR}/tests/probe_controls.py ${CMAKE_SOURCE_DIR})
  add_test(NAME idle_cost COMMAND bash ${CMAKE_SOURCE_DIR}/tests/idle_cost.sh $<TARGET_FILE:msfs-linux-companion> ${CMAKE_SOURCE_DIR})
  add_test(NAME cli_options COMMAND bash ${CMAKE_SOURCE_DIR}/tests/cli_options.sh $<TARGET_FILE:msfs-linux-companion>
           ${CMAKE_SOURCE_DIR}/tests/fixtures/config_v1.json)
  # the example plugin and two broken ones, loaded by the real binary
  include(cmake/CompanionPlugin.cmake)
  companion_add_plugin(hello examples/plugins/hello/hello.cpp)
  companion_add_plugin(bad_api tests/bad_plugin.cpp)
  target_compile_definitions(bad_api PRIVATE BAD_API)
  companion_add_plugin(bad_load tests/bad_plugin.cpp)
  add_test(NAME plugins COMMAND bash ${CMAKE_SOURCE_DIR}/tests/plugins.sh $<TARGET_FILE:msfs-linux-companion> ${CMAKE_SOURCE_DIR}
           $<TARGET_FILE:hello> $<TARGET_FILE:bad_api> $<TARGET_FILE:bad_load>)
endif()
