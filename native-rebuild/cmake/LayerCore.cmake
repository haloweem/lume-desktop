# Platform-independent core: models, codecs, protocols.
# Statements keep their original relative order; a library sits in the highest
# layer any of its sources belong to.

add_library(lume-core STATIC
  core/diagnostics/diagnostics.cpp
  core/radial/radial_menu.cpp
  core/dock/dock_model.cpp
  core/desktop_icons/desktop_icons_policy.cpp
  core/desktop_icons/desktop_icons_store.cpp
  core/organizer/organizer_model.cpp
  core/organizer/desktop_organization.cpp
  core/organizer/organizer_interaction.cpp
  core/organizer/organizer_store.cpp
  core/weather/weather.cpp
  core/weather/weather_codec.cpp
  core/weather/qweather.cpp
  core/weather/weather_provider.cpp
  core/weather/weather_location.cpp
  core/weather/weather_places.cpp
  core/weather/weather_preferences.cpp
  core/almanac/almanac.cpp
  core/notes/notes_store.cpp
  core/widget/widget_preferences.cpp
  core/widget/widget_preferences_store.cpp
  core/performance/performance_metrics.cpp
  core/spectrum/spectrum.cpp
  core/reminder/reminder_ledger.cpp
  core/todo/todo_time.cpp
  core/todo/todo_model.cpp
  core/todo/todo_json_codec.cpp
  core/todo/todo_repository.cpp
  core/calendar/calendar_date.cpp
  core/calendar/lunar_calendar.cpp
  core/calendar/calendar_model.cpp
  core/calendar/calendar_settings.cpp
  core/calendar/calendar_controller.cpp
  core/calendar/calendar_preferences_codec.cpp
  core/calendar/calendar_preferences_store.cpp
  core/lifecycle/service_container.cpp
  core/window/window_geometry.cpp
  core/window/screen_fit.cpp
  core/window/window_service.cpp
  core/glass/glass_geometry.cpp
  core/glass/glass_service.cpp
  core/settings/settings_store.cpp
  core/settings/config_migrator.cpp
  core/settings/json_settings_codec.cpp
  core/widget/widget_registry.cpp
  core/widget/widget_service.cpp
  core/bridge/message_router.cpp
  core/webview/webview_host.cpp
  core/wallpaper/wallpaper_service.cpp
  core/wallpaper/playback_preferences.cpp
  core/wallpaper/catalog.cpp
  core/wallpaper/library.cpp
  core/wallpaper/favorites.cpp
  core/ui/home_screen_model.cpp
  core/ui/page_bridge.cpp
  core/ui/page_lifecycle_protocol.cpp
  core/ui/webview_lifecycle.cpp
  core/ui/wallpaper_page_protocol.cpp
  core/ui/original_window_layout.cpp
  core/clock/clock_controller.cpp
  core/ui/clock_page_state.cpp
  core/ui/calendar_page_state.cpp
  core/music/music_model.cpp
  core/music/music_library.cpp
  core/music/music_cache.cpp
  core/music/music_provider.cpp
  core/music/melody_analysis.cpp
  core/music/music_lyrics.cpp
  core/music/music_online_lyrics.cpp
  core/music/music_background.cpp
  core/music/music_background_store.cpp
  core/music/music_host_protocol.cpp
  core/ui/music_page_protocol.cpp
  core/ui/music_library_protocol.cpp
  core/ui/music_background_protocol.cpp
)
target_include_directories(lume-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lume-core PUBLIC cxx_std_17)
lume_enable_native_warnings(lume-core)

add_subdirectory(core/taskbar)
