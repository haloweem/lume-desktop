add_library(lume-dock-runtime STATIC
  platform/windows/dock/dock_sources.cpp
  platform/windows/dock/dock_desktop_layer.cpp
  platform/windows/dock/dock_window.cpp
  platform/windows/dock/launchpad_window.cpp)
target_include_directories(lume-dock-runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-dock-runtime PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-dock-runtime PUBLIC lume-core lume-widget-ui lume-widget-material lume-organizer-view
  PRIVATE user32 gdi32 shell32 ole32 oleaut32 uuid dwmapi version comctl32 shcore)
lume_enable_native_warnings(lume-dock-runtime)
add_library(lume-dock-session STATIC apps/shell/dock_session.cpp)
target_include_directories(lume-dock-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-dock-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-dock-session PUBLIC lume-dock-runtime lume-platform-windows PRIVATE lume-minimize-animation-runtime gdiplus shell32 ole32 uuid iphlpapi imm32)
lume_enable_native_warnings(lume-dock-session)

# Control center opened from the Dock's right end (native panel + system controls).
add_library(lume-control-center STATIC
  platform/windows/control_center/control_center_window.cpp
  platform/windows/control_center/system_controls.cpp
  platform/windows/control_center/network_controls.cpp)
target_include_directories(lume-control-center PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-control-center PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-control-center PRIVATE lume-core lume-window-surface lume-widget-ui lume-widget-material lume-native-calendar
  lume-platform-windows user32 ole32 oleaut32 shell32 dwmapi wlanapi dxva2 wbemuuid iphlpapi runtimeobject uuid)
lume_enable_native_warnings(lume-control-center)
target_link_libraries(lume-dock-session PRIVATE lume-control-center)
