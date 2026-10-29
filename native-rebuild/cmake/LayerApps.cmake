# Executables, their link order and staged assets.
# Statements keep their original relative order; a library sits in the highest
# layer any of its sources belong to.

add_executable(LumeLauncher WIN32 apps/launcher/main.cpp platform/windows/lume_app.rc)

add_executable(LumeShell WIN32 apps/shell/main.cpp platform/windows/lume_app.rc)

add_executable(LumeMusicHost WIN32 apps/music-host/main.cpp platform/windows/lume_app.rc)

add_executable(LumeSetupShutdown WIN32 apps/setup-shutdown/main.cpp platform/windows/lume_app.rc)
target_link_libraries(LumeSetupShutdown PRIVATE lume-installer-shutdown shell32)
if(MINGW)
  target_link_options(LumeSetupShutdown PRIVATE -static)
endif()
lume_enable_native_warnings(LumeSetupShutdown)

foreach(target IN ITEMS LumeLauncher LumeShell LumeMusicHost)
  if(MINGW)
    target_link_options(${target} PRIVATE -static)
  endif()
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  lume_enable_native_warnings(${target})
endforeach()
target_link_libraries(LumeShell PRIVATE lume-core)
target_link_libraries(LumeShell PRIVATE lume-platform-windows)
target_link_libraries(LumeMusicHost PRIVATE lume-music-host-audio shell32 ole32)
target_link_libraries(LumeShell PRIVATE lume-native-clock)
add_dependencies(LumeShell lume-material-dll)

add_executable(LumeIconHost WIN32 platform/windows/organizer/organizer_icon_host.cpp)
target_include_directories(LumeIconHost PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(LumeIconHost PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(LumeIconHost PRIVATE shell32 windowscodecs ole32 uuid)
if(MINGW)
  target_link_options(LumeIconHost PRIVATE -static -municode)
endif()
lume_enable_native_warnings(LumeIconHost)
target_link_libraries(LumeShell PRIVATE lume-page-window)
target_link_libraries(LumeShell PRIVATE lume-desktop-application)
add_custom_target(lume-ui-assets ALL
  COMMAND ${CMAKE_COMMAND} -DUI_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/ui -DUI_DESTINATION=${CMAKE_BINARY_DIR}/ui -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageUi.cmake
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LUME_WEBVIEW2_PACKAGE}/build/native/x64/WebView2Loader.dll" "${CMAKE_BINARY_DIR}/WebView2Loader.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LUME_WEBVIEW2_PACKAGE}/LICENSE.txt" "${CMAKE_BINARY_DIR}/WebView2-LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LUME_WEBVIEW2_PACKAGE}/NOTICE.txt" "${CMAKE_BINARY_DIR}/WebView2-NOTICE.txt")
add_dependencies(LumeShell lume-ui-assets)
add_dependencies(LumeShell LumeMinimizeHost)
add_custom_target(lume-taskbar-assets ALL
  COMMAND ${CMAKE_COMMAND}
    -DTASKBAR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/vendor/taskbar-runtime/2026.1
    -DTASKBAR_DESTINATION=${CMAKE_BINARY_DIR}/taskbar-runtime
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageTaskbar.cmake)
add_dependencies(LumeShell lume-taskbar-assets)
