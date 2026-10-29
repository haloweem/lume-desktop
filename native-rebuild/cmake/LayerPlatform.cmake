# Windows platform layer: windows, runtimes, rendering. No apps/ sources.
# Statements keep their original relative order; a library sits in the highest
# layer any of its sources belong to.

add_library(lume-platform-windows STATIC
  platform/windows/win32_window_backend.cpp
  platform/windows/d3d11_glass_backend.cpp
  platform/windows/webview2_runtime.cpp
  platform/windows/mpv_process_controller.cpp
  platform/windows/diagnostics/runtime_log.cpp
  platform/windows/wallpaper/media_server.cpp
  platform/windows/wallpaper/static_image.cpp
  platform/windows/wallpaper/system_wallpaper.cpp
  platform/windows/native_home_window.cpp
  platform/windows/local_settings_file_store.cpp
  platform/windows/todo/windows_local_time.cpp
)
target_include_directories(lume-platform-windows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lume-platform-windows PUBLIC cxx_std_17)
target_compile_definitions(lume-platform-windows PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-platform-windows PRIVATE lume-core lume-widget-ui user32 gdi32 d3d11 dxgi dcomp uuid ws2_32 bcrypt dwmapi windowscodecs ole32 advapi32 dbghelp)
lume_enable_native_warnings(lume-platform-windows)

# mpv C plugin: reads protected wallpapers over a named pipe (lume:// URLs).
# mpv fills its API function pointers when loading it; nothing links to mpv.
add_library(lume-mpv-stream SHARED platform/windows/wallpaper/lume_mpv_stream.cpp)
set_target_properties(lume-mpv-stream PROPERTIES PREFIX "" OUTPUT_NAME "lume-mpv-stream"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_include_directories(lume-mpv-stream PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/vendor/mpv-include)
target_compile_definitions(lume-mpv-stream PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(lume-platform-windows lume-mpv-stream)

add_library(lume-desktop-icons-windows STATIC
  platform/windows/desktop_icons/desktop_icons_port.cpp
  platform/windows/desktop_icons/desktop_icons_runtime.cpp
)
target_include_directories(lume-desktop-icons-windows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-desktop-icons-windows PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-desktop-icons-windows PUBLIC lume-core PRIVATE user32 shell32 oleacc oleaut32 ole32 uuid)
lume_enable_native_warnings(lume-desktop-icons-windows)

add_library(lume-music-host-audio STATIC platform/windows/music_audio_engine.cpp platform/windows/music_http_stream.cpp platform/windows/music_http_flac_decoder.cpp)
target_include_directories(lume-music-host-audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-music-host-audio PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-music-host-audio PUBLIC lume-music-host-runtime PRIVATE winmm mfplat mfreadwrite mfuuid ole32 winhttp)
lume_enable_native_warnings(lume-music-host-audio)

add_library(lume-music-host-client STATIC platform/windows/music_host_client.cpp)
target_include_directories(lume-music-host-client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-music-host-client PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-music-host-client PUBLIC lume-music-host-runtime PRIVATE advapi32)
lume_enable_native_warnings(lume-music-host-client)

add_library(lume-music-local STATIC platform/windows/music_local_files.cpp platform/windows/music_local_lyrics.cpp)
target_include_directories(lume-music-local PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-music-local PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-music-local PUBLIC lume-core PRIVATE ole32 shell32 uuid)
lume_enable_native_warnings(lume-music-local)

add_library(lume-installer-shutdown STATIC platform/windows/installer_shutdown.cpp)
target_include_directories(lume-installer-shutdown PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lume-installer-shutdown PUBLIC cxx_std_17)
target_compile_definitions(lume-installer-shutdown PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-installer-shutdown PRIVATE user32)
lume_enable_native_warnings(lume-installer-shutdown)

add_library(lume-weather-runtime STATIC
  platform/windows/weather/weather_backend.cpp
  platform/windows/weather/weather_worker.cpp
  platform/windows/weather/weather_http.cpp
  platform/windows/weather/weather_timezone.cpp
)
target_include_directories(lume-weather-runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-weather-runtime PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-weather-runtime PUBLIC lume-core PRIVATE winhttp)
lume_enable_native_warnings(lume-weather-runtime)

add_library(lume-performance-runtime STATIC
  platform/windows/performance/performance_sampler.cpp
  platform/windows/performance/performance_monitor.cpp
)
target_include_directories(lume-performance-runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-performance-runtime PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-performance-runtime PUBLIC lume-core PRIVATE pdh iphlpapi)
lume_enable_native_warnings(lume-performance-runtime)

add_library(lume-spectrum-runtime STATIC platform/windows/spectrum/spectrum_capture.cpp)
target_include_directories(lume-spectrum-runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-spectrum-runtime PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-spectrum-runtime PUBLIC lume-core PRIVATE ole32)
lume_enable_native_warnings(lume-spectrum-runtime)

add_library(lume-window-surface STATIC platform/windows/rounded_window_surface.cpp)
target_include_directories(lume-window-surface PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-window-surface PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-window-surface PRIVATE dwmapi comctl32 user32 gdi32)
lume_enable_native_warnings(lume-window-surface)

add_library(lume-widget-ui STATIC
  platform/windows/widget_ui/widget_canvas.cpp
  platform/windows/widget_ui/icon_art.cpp
  platform/windows/widget_ui/direct_write_text.cpp
  platform/windows/widget_ui/direct_write_edit.cpp
  platform/windows/widget_ui/direct_write_tooltip.cpp
  platform/windows/widget_ui/LumeWidgetMenu.cpp
  platform/windows/widget_ui/LumeWidgetColorPicker.cpp
  platform/windows/widget_ui/LumeWidgetPositionEditor.cpp
)
target_include_directories(lume-widget-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-widget-ui PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-widget-ui PRIVATE lume-window-surface gdiplus dwmapi user32 gdi32 comctl32 uxtheme d2d1 dwrite windowscodecs ole32 uuid imm32 msimg32)
lume_enable_native_warnings(lume-widget-ui)

add_library(lume-native-clock STATIC
  platform/windows/clock/clock_renderer.cpp
  platform/windows/clock/native_clock_window.cpp
  platform/windows/clock_runtime.cpp
)
target_include_directories(lume-native-clock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-native-clock PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-native-clock PRIVATE lume-core lume-widget-ui d2d1 dwrite windowscodecs ole32 gdiplus user32 gdi32 uuid shell32)
lume_enable_native_warnings(lume-native-clock)
find_program(LUME_POWERSHELL NAMES pwsh powershell REQUIRED)
file(GLOB_RECURSE LUME_MATERIAL_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/material/*")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/LumeGlass.dll" "${CMAKE_BINARY_DIR}/LumeGlass.NOTICE"
  BYPRODUCTS "${CMAKE_BINARY_DIR}/material-shader-bytecode.h"
  COMMAND "${LUME_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/material/build-material.ps1" -OutputDirectory "${CMAKE_BINARY_DIR}"
  DEPENDS ${LUME_MATERIAL_SOURCES}
  VERBATIM)
add_custom_target(lume-material-dll ALL DEPENDS "${CMAKE_BINARY_DIR}/LumeGlass.dll" "${CMAKE_BINARY_DIR}/LumeGlass.NOTICE")

add_library(lume-widget-material STATIC platform/windows/material/widget_material_runtime.cpp)
target_include_directories(lume-widget-material PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-widget-material PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
lume_enable_native_warnings(lume-widget-material)

add_library(lume-widget-window STATIC platform/windows/widget_ui/native_widget_window.cpp)
target_include_directories(lume-widget-window PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-widget-window PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-widget-window PUBLIC lume-core lume-widget-ui lume-widget-material PRIVATE user32 gdi32 shell32 shcore)
lume_enable_native_warnings(lume-widget-window)

add_library(lume-music-desktop-controls STATIC platform/windows/music_desktop_controls.cpp)
target_include_directories(lume-music-desktop-controls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-music-desktop-controls PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-music-desktop-controls PUBLIC lume-widget-window
  PRIVATE lume-music-library lume-music-accounts runtimeobject shcore crypt32 windowscodecs ole32 uuid comctl32)
lume_enable_native_warnings(lume-music-desktop-controls)

add_library(lume-performance-view STATIC platform/windows/performance/performance_view.cpp)

add_library(lume-spectrum-view STATIC platform/windows/spectrum/spectrum_view.cpp)
target_include_directories(lume-spectrum-view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-spectrum-view PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-spectrum-view PUBLIC lume-widget-ui lume-core)
lume_enable_native_warnings(lume-spectrum-view)

add_library(lume-weather-view STATIC platform/windows/weather/weather_view.cpp)
target_sources(lume-weather-view INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/weather/weather_assets.rc")
target_include_directories(lume-weather-view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-weather-view PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-weather-view PUBLIC lume-widget-ui lume-core)
lume_enable_native_warnings(lume-weather-view)

add_library(lume-almanac-view STATIC platform/windows/almanac/almanac_view.cpp)
target_include_directories(lume-almanac-view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-almanac-view PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-almanac-view PUBLIC lume-widget-window)
lume_enable_native_warnings(lume-almanac-view)
target_include_directories(lume-performance-view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lume-performance-view PUBLIC lume-widget-window lume-performance-runtime)
lume_enable_native_warnings(lume-performance-view)

add_library(lume-organizer-view STATIC platform/windows/organizer/organizer_view.cpp platform/windows/organizer/organizer_motion.cpp platform/windows/organizer/organizer_text.cpp platform/windows/organizer/organizer_icons.cpp platform/windows/organizer/organizer_icon_client.cpp platform/windows/organizer/organizer_drop_target.cpp platform/windows/organizer/shell_drag.cpp)
target_include_directories(lume-organizer-view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-organizer-view PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-organizer-view PUBLIC lume-widget-ui lume-core PRIVATE lume-platform-windows normaliz shell32 windowscodecs ole32 uuid)
lume_enable_native_warnings(lume-organizer-view)
add_dependencies(lume-organizer-view LumeIconHost)

add_library(lume-native-calendar STATIC
  platform/windows/calendar/calendar_todo_editor.cpp
  platform/windows/calendar/calendar_todo_picker.cpp
  platform/windows/calendar/calendar_todo_draft.cpp
  platform/windows/calendar/calendar_todo_form_layout.cpp
  platform/windows/calendar/calendar_todo_ui.cpp
  platform/windows/calendar/native_calendar_todos.cpp
  platform/windows/calendar/calendar_todo_view.cpp
  platform/windows/calendar/calendar_layout.cpp
  platform/windows/calendar/calendar_snap.cpp
  platform/windows/calendar/calendar_renderer.cpp
  platform/windows/calendar/calendar_month_picker.cpp
  platform/windows/calendar/native_calendar_window.cpp
  platform/windows/calendar_runtime.cpp
)
target_include_directories(lume-native-calendar PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-native-calendar PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-native-calendar PUBLIC lume-core PRIVATE lume-widget-ui lume-widget-material d2d1 dwrite windowscodecs ole32 gdiplus user32 gdi32 uuid comctl32 imm32 shcore)
lume_enable_native_warnings(lume-native-calendar)

add_library(lume-page-window STATIC
  platform/windows/webview_page_window.cpp
  platform/windows/webview_process_memory.cpp
)
target_include_directories(lume-page-window PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(lume-page-window SYSTEM PRIVATE "${LUME_WEBVIEW2_PACKAGE}/build/native/include")
target_compile_definitions(lume-page-window PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(lume-page-window PUBLIC _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-page-window PRIVATE lume-core lume-window-surface lume-widget-ui user32 gdi32 ole32 shell32 uuid dwmapi psapi shcore)
lume_enable_native_warnings(lume-page-window)

add_library(lume-native-reminder STATIC
  platform/windows/reminder/reminder_attention.cpp
  platform/windows/reminder/reminder_presenter.cpp
  platform/windows/reminder/local_reminder_runtime.cpp
)
target_include_directories(lume-native-reminder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-native-reminder PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-native-reminder PUBLIC lume-core PRIVATE lume-widget-ui gdiplus user32 gdi32 shell32)
lume_enable_native_warnings(lume-native-reminder)

add_subdirectory(platform/windows/taskbar)

# Whole-song decode for the 音符律动 melody analysis; runs in the shell, apart
# from the playback host.
add_library(lume-music-score-source STATIC platform/windows/music_score_source.cpp platform/windows/music_http_stream.cpp platform/windows/music_http_flac_decoder.cpp)
target_include_directories(lume-music-score-source PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-music-score-source PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-music-score-source PUBLIC lume-core PRIVATE mfplat mfreadwrite mfuuid ole32 winhttp)
lume_enable_native_warnings(lume-music-score-source)
