cmake_minimum_required(VERSION 3.20)
project(Hitman2Enhanced VERSION 0.8.0 LANGUAGES CXX)  # keep in sync with src/version.h

# The game is a 32-bit process: native components must be built for Win32.
#   cmake -S . -B build -G "Visual Studio 18 2026" -A Win32
#   cmake --build build --config Release
#   ctest --test-dir build -C Release --output-on-failure
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Configure with -A Win32: the game loads only 32-bit DLLs")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the DLL must not depend on a VC++ runtime being installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# /Oy-: keep frame pointers so RtlCaptureStackBackTrace can walk past the shim layer.
add_compile_options(/W4 /permissive- /Oy-)

# Layout (D-009): src/common = shared helpers, src/fixes = one module per fix + registry,
# src/proxy = the d3d8.dll that hosts the fixes and the research logger.
add_library(h2e_common STATIC src/common/sha256.cpp src/common/code_patch.cpp)
add_library(h2e_fixes STATIC src/fixes/registry.cpp src/fixes/resolution.cpp src/fixes/horplus.cpp
            src/fixes/timer.cpp src/fixes/menuscale.cpp src/fixes/framelimit.cpp src/fixes/zwindows.cpp
            src/fixes/hdfonts.cpp src/fixes/exe_redirect.cpp src/fixes/settings.cpp)
target_link_libraries(h2e_fixes PUBLIC h2e_common)

add_library(h2e_proxy SHARED src/proxy/d3d8_proxy.cpp src/proxy/d3d8.def)
set_target_properties(h2e_proxy PROPERTIES OUTPUT_NAME d3d8)
target_link_libraries(h2e_proxy PRIVATE h2e_fixes)

enable_testing()
add_executable(proxy_smoke tests/native/proxy_smoke.cpp)
add_dependencies(proxy_smoke h2e_proxy)
add_test(NAME proxy_smoke COMMAND proxy_smoke $<TARGET_FILE:h2e_proxy> ${CMAKE_BINARY_DIR}/proxy_smoke.log)
# EXP-004 timer fix: isolated harness (runs the game's instruction sequence in the test process).
add_executable(timer_test tests/native/timer_test.cpp)
target_link_libraries(timer_test PRIVATE h2e_fixes)
add_test(NAME timer_test COMMAND timer_test)
# hdfonts end to end: texture hooks + fix through the real proxy and a real device.
add_executable(hdfonts_smoke tests/native/hdfonts_smoke.cpp)
target_link_libraries(hdfonts_smoke PRIVATE h2e_fixes)
add_dependencies(hdfonts_smoke h2e_proxy)
add_test(NAME hdfonts_smoke COMMAND hdfonts_smoke $<TARGET_FILE:h2e_proxy> ${CMAKE_BINARY_DIR}/hdfonts_smoke.log)
# settings page end to end: drawn over a real device, state restored, dinput8 hooked.
add_executable(settings_smoke tests/native/settings_smoke.cpp)
add_dependencies(settings_smoke h2e_proxy)
add_test(NAME settings_smoke COMMAND settings_smoke $<TARGET_FILE:h2e_proxy> ${CMAKE_BINARY_DIR}/settings_smoke.log)
set_tests_properties(proxy_smoke hdfonts_smoke settings_smoke PROPERTIES RUN_SERIAL TRUE)  # both write h2e.ini next to the DLL
add_executable(fixes_test tests/native/fixes_test.cpp)
target_link_libraries(fixes_test PRIVATE h2e_fixes)
add_test(NAME fixes_test COMMAND fixes_test)
