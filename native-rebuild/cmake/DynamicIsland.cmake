add_library(lume-dynamic-island STATIC
  core/dynamic_island/island_model.cpp
  apps/shell/dynamic_island_session.cpp
  platform/windows/dynamic_island/island_window.cpp
  platform/windows/dynamic_island/island_weather_page.cpp
  platform/windows/dynamic_island/system_media_session.cpp
  platform/windows/dynamic_island/player_position.cpp)
target_include_directories(lume-dynamic-island PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-dynamic-island PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-dynamic-island PUBLIC lume-core lume-platform-windows lume-widget-ui lume-widget-material lume-native-calendar lume-performance-runtime lume-performance-view PRIVATE user32 gdi32 gdiplus windowscodecs crypt32 runtimeobject shell32 dwmapi ole32 oleaut32 uiautomationcore)
lume_enable_native_warnings(lume-dynamic-island)
