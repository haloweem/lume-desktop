cmake_minimum_required(VERSION 3.25)
project(LumeNativeRebuild VERSION 1.0.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

include(CTest)
include(cmake/NativeWarnings.cmake)
include(cmake/WebView2Sdk.cmake)

include(cmake/LayerCore.cmake)
include(cmake/LayerPlatform.cmake)
include(cmake/LayerSessions.cmake)
include(cmake/LayerApps.cmake)

add_subdirectory(tests/unit)

# Packaging consumes exact target paths from this configuration. Never select
# an acceptance executable by searching output directories for the first match.
set(lume_acceptance_probe_targets
  lume-wallpaper-session-test
  lume-dynamic-island-test lume-desktop-dynamic-island-test
  lume-today-todo-session-test lume-desktop-today-todo-test
  lume-dock-context-menu-test
  lume-dock-reflection-test
  lume-organizer-desktop-test
  lume-organizer-model-test
  lume-dock-application-layer-test
  lume-launchpad-collection-test
  lume-diagnostics-collector-test
  lume-webview-page-window-test lume-organizer-icon-fixture
  lume-application-settings-protocol-test lume-application-settings-service-test
  lume-application-settings-update-test lume-application-settings-file-test lume-application-settings-session-test
  lume-application-settings-installer-test
  lume-application-settings-update-observer-test
  lume-update-notification-policy-test lume-update-notification-session-test lume-update-presenter-test
  lume-desktop-update-notification-test
  lume-account-security-test lume-account-jwt-test lume-account-storage-test lume-account-avatar-test
  lume-account-http-test lume-account-backend-test lume-account-session-test lume-account-protocol-test
  lume-account-rotation-test lume-account-background-test lume-account-engagement-test
  lume-shell-activation-test lume-webview-application-commands-test lume-desktop-settings-account-test
  lume-installer-shutdown-test
  lume-music-model-test lume-music-cache-test lume-music-provider-test lume-music-local-files-test
  lume-music-host-protocol-test lume-music-host-runtime-test lume-music-audio-engine-test lume-music-http-stream-test
  lume-music-host-client-test lume-music-host-integration-test lume-music-session-test lume-music-lyrics-test
  lume-music-preferences-test lume-desktop-music-preferences-test lume-music-background-session-test
  lume-music-auth-account-test lume-music-auth-http-test lume-music-auth-credentials-test
  lume-music-auth-netease_account-test lume-music-auth-qq_account-test lume-music-auth-kugou_account-test
  lume-desktop-music-account-test lume-music-system-fonts-test
  lume-music-font-assets-test lume-music-webview-resources-test
  lume-music-library-test lume-music-library_session-test lume-music-library_worker-test
  lume-music-library_protocol-test lume-music-metadata-test lume-desktop-music-library-test
  lume-music-online_lyrics-test lume-music-online_provider-test lume-music-online_session-test
  lume-music-provider_credential_lease-test lume-music-desktop_controls-test lume-music-desktop_session-test
  lume-reminder-ledger-test lume-local_reminder_runtime-test lume-reminder_presenter-test
  lume-todo-session-test lume-desktop-reminder-test lume-native-material-test lume-material-shader-test
  lume-calendar-todo-editor-test lume-calendar-todo-material-test lume-calendar-todo-render-test
  lume-native-calendar-window-test lume-native-widget-window-test lume-native-clock-window-test lume-widget-layer-stress-test
  lume-weather-session-test lume-weather-view-test lume-almanac-session-test lume-performance-session-test lume-spectrum-session-test
  lume-organizer-session-test lume-organizer-icons-test lume-organizer-icon-host-test
  lume-organizer-interaction-test lume-organizer-motion-test lume-organizer-drop-target-test lume-widget-menu-native-test
  lume-desktop-icons-runtime-test lume-desktop-icons-session-test
  lume-taskbar-controller-test lume-taskbar-store-test lume-taskbar-runtime-test lume-taskbar-session-test lume-taskbar-payload-test
  lume-minimize-animation-test lume-minimize-animation-runtime-test lume-minimize-animation-session-test
  lume-desktop-taskbar-test)
# lume-desktop-minimize-test drives the minimize card, hidden while
# options.minimizeVisible is false; it still builds for when it returns.
set(lume_acceptance_probe_json "{\n")
set(lume_acceptance_probe_separator "")
foreach(probe IN LISTS lume_acceptance_probe_targets)
  string(APPEND lume_acceptance_probe_json
    "${lume_acceptance_probe_separator}  \"${probe}\": \"$<TARGET_FILE:${probe}>\"")
  set(lume_acceptance_probe_separator ",\n")
endforeach()
string(APPEND lume_acceptance_probe_json "\n}\n")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/acceptance-probes-$<CONFIG>.json"
  CONTENT "${lume_acceptance_probe_json}")
