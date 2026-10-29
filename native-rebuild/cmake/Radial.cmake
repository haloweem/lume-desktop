add_library(lume-screenshot STATIC
  platform/windows/screenshot/screenshot_image.cpp
  platform/windows/screenshot/screenshot_render.cpp
  platform/windows/screenshot/screenshot_editor.cpp)
target_include_directories(lume-screenshot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-screenshot PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-screenshot PUBLIC user32 gdi32 gdiplus comdlg32 comctl32 ole32 dwmapi PRIVATE lume-widget-ui)
lume_enable_native_warnings(lume-screenshot)
add_library(lume-radial-runtime STATIC
  platform/windows/radial/radial_shortcut.cpp
  platform/windows/radial/radial_window.cpp)
target_include_directories(lume-radial-runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-radial-runtime PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-radial-runtime PUBLIC lume-core lume-widget-ui lume-widget-material PRIVATE user32 gdi32 gdiplus)
lume_enable_native_warnings(lume-radial-runtime)
add_library(lume-radial-session STATIC apps/shell/radial_session.cpp)
target_include_directories(lume-radial-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-radial-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-radial-session PUBLIC lume-radial-runtime lume-platform-windows lume-screenshot)
lume_enable_native_warnings(lume-radial-session)
