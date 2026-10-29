cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(LumeMinimizeAnimation LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  include(CTest)
  set(LUME_MINIMIZE_STANDALONE ON)
endif()
get_filename_component(LUME_MINIMIZE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

add_library(lume-minimize-animation-core STATIC
  "${LUME_MINIMIZE_ROOT}/core/minimize_animation/minimize_animation.cpp"
  "${LUME_MINIMIZE_ROOT}/core/minimize_animation/minimize_animation_store.cpp")
target_include_directories(lume-minimize-animation-core PUBLIC "${LUME_MINIMIZE_ROOT}")
target_compile_features(lume-minimize-animation-core PUBLIC cxx_std_17)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(LUME_MINIMIZE_BITS 64)
else()
  set(LUME_MINIMIZE_BITS 32)
endif()
add_library(LumeMinimizeHook SHARED minimize_animation_hook.cpp minimize_animation_hook.def)
set_target_properties(LumeMinimizeHook PROPERTIES PREFIX "" OUTPUT_NAME "LumeMinimizeHook${LUME_MINIMIZE_BITS}"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(LumeMinimizeHook PRIVATE lume-minimize-animation-core user32 gdi32 dwmapi d2d1 shell32 advapi32 ole32 oleaut32 uuid)
add_executable(LumeMinimizeHost WIN32 minimize_animation_host.cpp)
set_target_properties(LumeMinimizeHost PROPERTIES OUTPUT_NAME "LumeMinimizeHost${LUME_MINIMIZE_BITS}"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(LumeMinimizeHost PRIVATE lume-minimize-animation-core user32)
add_dependencies(LumeMinimizeHost LumeMinimizeHook)
add_library(lume-minimize-animation-runtime STATIC minimize_animation_runtime.cpp)
target_link_libraries(lume-minimize-animation-runtime PUBLIC lume-minimize-animation-core PRIVATE user32)
add_library(lume-minimize-animation-session STATIC
  "${LUME_MINIMIZE_ROOT}/apps/shell/minimize_animation_session.cpp")
target_link_libraries(lume-minimize-animation-session PUBLIC lume-minimize-animation-runtime)
target_include_directories(lume-minimize-animation-session PUBLIC "${LUME_MINIMIZE_ROOT}")
target_compile_definitions(lume-minimize-animation-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(LUME_MINIMIZE_STANDALONE)
  target_sources(lume-minimize-animation-session PRIVATE "${LUME_MINIMIZE_ROOT}/platform/windows/local_settings_file_store.cpp")
  target_link_libraries(lume-minimize-animation-session PRIVATE shell32 ole32)
else()
  target_link_libraries(lume-minimize-animation-session PUBLIC lume-platform-windows)
endif()
foreach(target LumeMinimizeHook LumeMinimizeHost lume-minimize-animation-runtime)
  target_include_directories(${target} PUBLIC "${LUME_MINIMIZE_ROOT}")
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00)
endforeach()
if(MINGW)
  target_link_options(LumeMinimizeHook PRIVATE -static-libgcc -static-libstdc++ -static)
  target_link_options(LumeMinimizeHost PRIVATE -static -municode)
endif()
if(MSVC)
  foreach(target lume-minimize-animation-core LumeMinimizeHook LumeMinimizeHost lume-minimize-animation-runtime lume-minimize-animation-session)
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(${target} PRIVATE /utf-8)
  endforeach()
endif()

if(NOT LUME_MINIMIZE_STANDALONE)
  # The main project compiles its native architecture normally. Build only the
  # companion architecture here, so no two build systems write the native DLL.
  if(LUME_MINIMIZE_BITS EQUAL 64)
    set(LUME_MINIMIZE_COMPANION 32)
  else()
    set(LUME_MINIMIZE_COMPANION 64)
  endif()
  find_program(LUME_MINIMIZE_POWERSHELL NAMES pwsh powershell REQUIRED)
  set(LUME_MINIMIZE_COMPANION_HOST "${CMAKE_BINARY_DIR}/LumeMinimizeHost${LUME_MINIMIZE_COMPANION}.exe")
  set(LUME_MINIMIZE_COMPANION_HOOK "${CMAKE_BINARY_DIR}/LumeMinimizeHook${LUME_MINIMIZE_COMPANION}.dll")
  set(LUME_MINIMIZE_NOTICE "${CMAKE_BINARY_DIR}/LumeMinimize.NOTICE.md")
  set(LUME_MINIMIZE_SOURCES "${CMAKE_BINARY_DIR}/LumeMinimize.SOURCES.md")
  add_custom_command(
    OUTPUT "${LUME_MINIMIZE_COMPANION_HOST}" "${LUME_MINIMIZE_COMPANION_HOOK}"
           "${LUME_MINIMIZE_NOTICE}" "${LUME_MINIMIZE_SOURCES}"
    COMMAND "${LUME_MINIMIZE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File
      "${CMAKE_CURRENT_SOURCE_DIR}/build-minimize-hosts.ps1"
      -OutputDirectory "${CMAKE_BINARY_DIR}" -Architecture "${LUME_MINIMIZE_COMPANION}"
    DEPENDS build-minimize-hosts.ps1 CMakeLists.txt
      minimize_animation_hook.cpp minimize_animation_hook.def minimize_animation_host.cpp
      minimize_animation_ipc.h NOTICE.md SOURCES.md
      "${LUME_MINIMIZE_ROOT}/core/minimize_animation/minimize_animation.h"
      "${LUME_MINIMIZE_ROOT}/core/minimize_animation/minimize_animation.cpp"
      "${LUME_MINIMIZE_ROOT}/core/minimize_animation/minimize_animation_store.h"
      "${LUME_MINIMIZE_ROOT}/core/minimize_animation/minimize_animation_store.cpp"
      "${LUME_MINIMIZE_ROOT}/core/settings/fixed_json_reader.h"
      "${LUME_MINIMIZE_ROOT}/core/settings/file_store.h"
    VERBATIM)
  add_custom_target(lume-minimize-animation-payload ALL
    DEPENDS LumeMinimizeHost "${LUME_MINIMIZE_COMPANION_HOST}" "${LUME_MINIMIZE_COMPANION_HOOK}"
            "${LUME_MINIMIZE_NOTICE}" "${LUME_MINIMIZE_SOURCES}")
  add_dependencies(lume-minimize-animation-session lume-minimize-animation-payload)
endif()

if(LUME_MINIMIZE_STANDALONE AND BUILD_TESTING)
  add_executable(lume-minimize-animation-test "${LUME_MINIMIZE_ROOT}/tests/unit/minimize_animation_test.cpp")
  target_link_libraries(lume-minimize-animation-test PRIVATE lume-minimize-animation-core)
  add_executable(lume-minimize-animation-runtime-test
    "${LUME_MINIMIZE_ROOT}/tests/unit/minimize_animation_runtime_test.cpp")
  target_link_libraries(lume-minimize-animation-runtime-test PRIVATE lume-minimize-animation-runtime dwmapi)
  target_compile_definitions(lume-minimize-animation-runtime-test PRIVATE UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  add_dependencies(lume-minimize-animation-runtime-test LumeMinimizeHost)
  add_executable(lume-minimize-animation-session-test "${LUME_MINIMIZE_ROOT}/tests/unit/minimize_animation_session_test.cpp")
  target_link_libraries(lume-minimize-animation-session-test PRIVATE lume-minimize-animation-session)
  target_compile_definitions(lume-minimize-animation-session-test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  add_dependencies(lume-minimize-animation-session-test LumeMinimizeHost)
  if(MINGW)
    target_link_options(lume-minimize-animation-test PRIVATE -static)
    target_link_options(lume-minimize-animation-runtime-test PRIVATE -static -municode)
    target_link_options(lume-minimize-animation-session-test PRIVATE -static)
  endif()
  if(MSVC)
    foreach(target lume-minimize-animation-test lume-minimize-animation-runtime-test lume-minimize-animation-session-test)
      set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
      target_compile_options(${target} PRIVATE /utf-8)
    endforeach()
  endif()
  add_test(NAME lume-minimize-animation-test COMMAND lume-minimize-animation-test)
  add_test(NAME lume-minimize-animation-runtime-test COMMAND lume-minimize-animation-runtime-test)
  add_test(NAME lume-minimize-animation-session-test COMMAND lume-minimize-animation-session-test)
  set_tests_properties(lume-minimize-animation-runtime-test PROPERTIES TIMEOUT 70 RUN_SERIAL TRUE)
  set_tests_properties(lume-minimize-animation-session-test PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE)
endif()
