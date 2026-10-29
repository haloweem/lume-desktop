add_library(lume-dock-runtime STATIC
  platform/windows/dock/dock_sources.cpp
  platform/windows/dock/dock_desktop_layer.cpp
  platform/windows/dock/dock_window.cpp
  platform/windows/dock/launchpad_window.cpp)
target_include_directories(lume-dock-runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-dock-runtime PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-dock-runtime PUBLIC lume-core lume-widget-ui lume-widget-material lume-organizer-view
  PRIVATE user32 gdi32 shell32 ole32 dwmapi version comctl32 shcore)
lume_enable_native_warnings(lume-dock-runtime)
add_library(lume-dock-session STATIC apps/shell/dock_session.cpp)
target_include_directories(lume-dock-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-dock-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-dock-session PUBLIC lume-dock-runtime lume-platform-windows PRIVATE gdiplus shell32 ole32)
lume_enable_native_warnings(lume-dock-session)
