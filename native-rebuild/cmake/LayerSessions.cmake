# Sessions and feature modules that wire core and platform together (apps/shell).
# Statements keep their original relative order; a library sits in the highest
# layer any of its sources belong to.

add_library(lume-music-preferences STATIC
  core/music/music_preferences.cpp
  core/music/music_preferences_store.cpp
  core/ui/music_preferences_protocol.cpp
  apps/shell/music_preferences_session.cpp
)
target_include_directories(lume-music-preferences PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lume-music-preferences PUBLIC cxx_std_17)
lume_enable_native_warnings(lume-music-preferences)

add_library(lume-desktop-icons-session STATIC apps/shell/desktop_icons_session.cpp)
target_include_directories(lume-desktop-icons-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-desktop-icons-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-desktop-icons-session PUBLIC lume-desktop-icons-windows lume-platform-windows)
lume_enable_native_warnings(lume-desktop-icons-session)

add_library(lume-music-host-runtime STATIC apps/music-host/music_host_runtime.cpp)
target_include_directories(lume-music-host-runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lume-music-host-runtime PUBLIC cxx_std_17)
target_link_libraries(lume-music-host-runtime PUBLIC lume-core)
lume_enable_native_warnings(lume-music-host-runtime)

add_library(lume-music-library STATIC
  apps/shell/music_library_session.cpp
  apps/shell/music_library_worker.cpp
  platform/windows/music_metadata.cpp
)
target_include_directories(lume-music-library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-music-library PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-music-library PUBLIC lume-music-local PRIVATE lume-platform-windows ole32 shell32 windowscodecs uuid)
lume_enable_native_warnings(lume-music-library)

add_library(lume-music-accounts STATIC
  apps/shell/music_provider_account_session.cpp
  platform/windows/music_provider_http.cpp
  platform/windows/music_login_diagnosis.cpp
  platform/windows/music_provider_crypto.cpp
  platform/windows/music_provider_credentials.cpp
  platform/windows/music_providers/netease_account.cpp
  platform/windows/music_providers/qq_account.cpp
  platform/windows/music_providers/kugou_account.cpp
)
target_include_directories(lume-music-accounts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lume-music-accounts PUBLIC cxx_std_17)
target_compile_definitions(lume-music-accounts PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-music-accounts PRIVATE winhttp bcrypt crypt32 ws2_32)
lume_enable_native_warnings(lume-music-accounts)

add_library(lume-music-online STATIC
  platform/windows/music_online_provider.cpp
  apps/shell/music_online_session.cpp
  apps/shell/external_lyrics.cpp
)
target_include_directories(lume-music-online PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lume-music-online PUBLIC cxx_std_17)
target_compile_definitions(lume-music-online PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-music-online PUBLIC lume-music-accounts lume-core)
lume_enable_native_warnings(lume-music-online)

add_library(lume-music-desktop-session STATIC apps/shell/music_desktop_session.cpp)
target_include_directories(lume-music-desktop-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-music-desktop-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-music-desktop-session PUBLIC lume-music-desktop-controls lume-platform-windows)
lume_enable_native_warnings(lume-music-desktop-session)

add_library(lume-performance-session STATIC apps/shell/performance_session.cpp)

add_library(lume-weather-session STATIC apps/shell/weather_session.cpp platform/windows/weather/weather_tooltip.cpp)

add_library(lume-organizer-session STATIC apps/shell/organizer_session.cpp platform/windows/organizer/desktop_organizer_source.cpp)
target_include_directories(lume-organizer-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-organizer-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-organizer-session PUBLIC lume-organizer-view lume-widget-window lume-platform-windows PRIVATE shell32 ole32 uuid)
lume_enable_native_warnings(lume-organizer-session)
target_include_directories(lume-weather-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-weather-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-weather-session PUBLIC lume-weather-view lume-weather-runtime lume-widget-window lume-platform-windows PRIVATE comctl32 shell32 advapi32)
lume_enable_native_warnings(lume-weather-session)

add_library(lume-notes-session STATIC apps/shell/notes_session.cpp platform/windows/notes/notes_widget_view.cpp
  platform/windows/notes/notes_page_window.cpp)
target_include_directories(lume-notes-session SYSTEM PRIVATE "${LUME_WEBVIEW2_PACKAGE}/build/native/include")
target_include_directories(lume-notes-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-notes-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-notes-session PUBLIC lume-widget-window lume-platform-windows PRIVATE lume-widget-material lume-window-surface dwmapi shell32 ole32)
lume_enable_native_warnings(lume-notes-session)

add_library(lume-spectrum-session STATIC apps/shell/spectrum_session.cpp)
target_include_directories(lume-spectrum-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-spectrum-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-spectrum-session PUBLIC lume-spectrum-view lume-spectrum-runtime lume-widget-window lume-platform-windows PRIVATE gdiplus)
lume_enable_native_warnings(lume-spectrum-session)

add_library(lume-almanac-session STATIC apps/shell/almanac_session.cpp)
target_include_directories(lume-almanac-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-almanac-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-almanac-session PUBLIC lume-almanac-view lume-platform-windows)
lume_enable_native_warnings(lume-almanac-session)
target_include_directories(lume-performance-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-performance-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-performance-session PUBLIC lume-performance-view lume-platform-windows)
lume_enable_native_warnings(lume-performance-session)

add_library(lume-calendar-session STATIC apps/shell/calendar_session.cpp apps/shell/todo_session.cpp)
target_include_directories(lume-calendar-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-calendar-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-calendar-session PUBLIC lume-native-calendar lume-platform-windows PRIVATE lume-widget-ui)
lume_enable_native_warnings(lume-calendar-session)

add_library(lume-taskbar-session STATIC apps/shell/taskbar_session.cpp)
target_include_directories(lume-taskbar-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-taskbar-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-taskbar-session PUBLIC lume-taskbar-windows)
lume_enable_native_warnings(lume-taskbar-session)

add_subdirectory(platform/windows/minimize_animation)

include(cmake/ApplicationSettings.cmake)

include(cmake/UpdateNotification.cmake)

include(cmake/Account.cmake)

include(cmake/Radial.cmake)

include(cmake/Dock.cmake)

include(cmake/DynamicIsland.cmake)

add_library(lume-today-todo-session STATIC apps/shell/today_todo_session.cpp platform/windows/today_todo/today_todo_view.cpp)
target_include_directories(lume-today-todo-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-today-todo-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-today-todo-session PUBLIC lume-native-calendar lume-widget-window lume-platform-windows)
lume_enable_native_warnings(lume-today-todo-session)

add_library(lume-desktop-application STATIC apps/shell/desktop_application.cpp apps/shell/diagnostic_operations.cpp apps/shell/diagnostic_report.cpp apps/shell/self_check.cpp apps/shell/island_adapter.cpp apps/shell/music_session.cpp apps/shell/music_background_session.cpp apps/shell/resource_snapshot.cpp platform/windows/application_tray.cpp
  platform/windows/shell_activation.cpp platform/windows/music_system_fonts.cpp apps/shell/music_font_asset_session.cpp)

add_library(lume-wallpaper-session STATIC apps/shell/wallpaper_session.cpp platform/windows/wallpaper/download.cpp)
target_include_directories(lume-wallpaper-session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-wallpaper-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-wallpaper-session PUBLIC lume-platform-windows PRIVATE ole32 shell32 wtsapi32 dwmapi winhttp bcrypt crypt32)
lume_enable_native_warnings(lume-wallpaper-session)
target_include_directories(lume-desktop-application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lume-desktop-application PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-desktop-application PUBLIC lume-calendar-session lume-today-todo-session lume-performance-session lume-spectrum-session lume-almanac-session lume-weather-session lume-organizer-session lume-desktop-icons-session lume-taskbar-session lume-minimize-animation-session lume-wallpaper-session lume-native-clock lume-page-window lume-native-reminder lume-application-settings-session lume-account-session lume-update-notification-session lume-music-host-client lume-music-local lume-music-preferences lume-music-accounts lume-music-score-source PRIVATE shell32 ole32 psapi dxgi d3d11 bcrypt shcore wevtapi version)
lume_enable_native_warnings(lume-desktop-application)
target_link_libraries(lume-desktop-application PUBLIC lume-music-online)
target_link_libraries(lume-desktop-application PUBLIC lume-radial-session lume-dock-session)
target_link_libraries(lume-desktop-application PUBLIC lume-dynamic-island)
target_link_libraries(lume-desktop-application PUBLIC lume-music-library lume-music-desktop-session lume-notes-session)
