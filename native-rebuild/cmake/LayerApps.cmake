# Executables, their link order and staged assets.
# Statements keep their original relative order; a library sits in the highest
# layer any of its sources belong to.

add_executable(LumeLauncher WIN32 apps/launcher/main.cpp platform/windows/lume_app.rc)

add_executable(LumeShell WIN32 apps/shell/main.cpp platform/windows/lume_app.rc)

# Metadata alone does not register identity. A trusted signed identity package
# can be supplied to the installer for normal per-user registration.
option(LUME_NOTIFICATION_IDENTITY "Build optional desktop notification identity metadata" ON)
if(WIN32 AND MSVC AND LUME_NOTIFICATION_IDENTITY)
  set(LUME_NOTIFICATION_IDENTITY_NAME "Lume.Desktop.NotificationIdentity")
  set(LUME_NOTIFICATION_IDENTITY_PUBLISHER "CN=LumeDesktop" CACHE STRING "Exact subject of the trusted notification identity signing certificate")
  string(STRIP "${LUME_NOTIFICATION_IDENTITY_PUBLISHER}" lume_identity_publisher_trimmed)
  if(lume_identity_publisher_trimmed STREQUAL "")
    message(FATAL_ERROR "LUME_NOTIFICATION_IDENTITY_PUBLISHER must not be empty")
  endif()
  set(LUME_NOTIFICATION_IDENTITY_PUBLISHER_XML "${LUME_NOTIFICATION_IDENTITY_PUBLISHER}")
  string(REPLACE "&" "&amp;" LUME_NOTIFICATION_IDENTITY_PUBLISHER_XML "${LUME_NOTIFICATION_IDENTITY_PUBLISHER_XML}")
  string(REPLACE "<" "&lt;" LUME_NOTIFICATION_IDENTITY_PUBLISHER_XML "${LUME_NOTIFICATION_IDENTITY_PUBLISHER_XML}")
  string(REPLACE ">" "&gt;" LUME_NOTIFICATION_IDENTITY_PUBLISHER_XML "${LUME_NOTIFICATION_IDENTITY_PUBLISHER_XML}")
  string(REPLACE "\"" "&quot;" LUME_NOTIFICATION_IDENTITY_PUBLISHER_XML "${LUME_NOTIFICATION_IDENTITY_PUBLISHER_XML}")
  set(LUME_NOTIFICATION_IDENTITY_PUBLISHER_JSON "${LUME_NOTIFICATION_IDENTITY_PUBLISHER}")
  string(REPLACE "\\" "\\\\" LUME_NOTIFICATION_IDENTITY_PUBLISHER_JSON "${LUME_NOTIFICATION_IDENTITY_PUBLISHER_JSON}")
  string(REPLACE "\"" "\\\"" LUME_NOTIFICATION_IDENTITY_PUBLISHER_JSON "${LUME_NOTIFICATION_IDENTITY_PUBLISHER_JSON}")
  set(LUME_NOTIFICATION_IDENTITY_APP_ID "LumeShell")
  set(LUME_NOTIFICATION_IDENTITY_VERSION "${PROJECT_VERSION}.0")
  # An unchanged, already signed sparse identity can be reused for an unsigned
  # local installer. This is separate from all product/installer version fields.
  set(LUME_NOTIFICATION_IDENTITY_REUSE_VERSION "" CACHE STRING "Validated signed identity version reused by the packaging script")
  if(NOT LUME_NOTIFICATION_IDENTITY_REUSE_VERSION STREQUAL "")
    if(NOT LUME_NOTIFICATION_IDENTITY_REUSE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
      message(FATAL_ERROR "Invalid notification identity reuse version")
    endif()
    set(LUME_NOTIFICATION_IDENTITY_VERSION "${LUME_NOTIFICATION_IDENTITY_REUSE_VERSION}")
  endif()
  set(lume_identity_source "${CMAKE_CURRENT_SOURCE_DIR}/packaging/notification-identity")
  set(lume_identity_output "${CMAKE_BINARY_DIR}/notification-identity")
  file(MAKE_DIRECTORY "${lume_identity_output}/Assets" "${CMAKE_BINARY_DIR}/Assets" "${CMAKE_BINARY_DIR}/generated")
  configure_file("${lume_identity_source}/LumeShell.exe.manifest.in"
    "${CMAKE_BINARY_DIR}/generated/LumeShell.notification-identity.manifest" @ONLY)
  target_sources(LumeShell PRIVATE "${CMAKE_BINARY_DIR}/generated/LumeShell.notification-identity.manifest")
  configure_file("${lume_identity_source}/AppxManifest.xml.in" "${lume_identity_output}/AppxManifest.xml" @ONLY)
  configure_file("${lume_identity_source}/identity-config.json.in" "${lume_identity_output}/identity-config.json" @ONLY)
  foreach(identity_file IN ITEMS README.md Identity-Common.ps1 Register-NotificationIdentity.ps1 Remove-NotificationIdentity.ps1)
    configure_file("${lume_identity_source}/${identity_file}" "${lume_identity_output}/${identity_file}" COPYONLY)
  endforeach()
  foreach(identity_asset IN ITEMS Square44x44Logo.png Square150x150Logo.png StoreLogo.png)
    configure_file("${lume_identity_source}/Assets/${identity_asset}" "${lume_identity_output}/Assets/${identity_asset}" COPYONLY)
    # Sparse application visual assets also resolve against ExternalLocation.
    # Keep the manifest's Assets paths present beside the application executable.
    configure_file("${lume_identity_source}/Assets/${identity_asset}" "${CMAKE_BINARY_DIR}/Assets/${identity_asset}" COPYONLY)
  endforeach()
endif()

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
target_link_libraries(LumeMusicHost PRIVATE lume-music-host-runtime lume-music-host-audio shell32 ole32)
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
  # Ninja must know who generates this included header, so a UI-only edit
  # rebuilds the compiled manifest in the same invocation after staging.
  BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/ui_integrity_manifest.h"
  COMMAND ${CMAKE_COMMAND} -DUI_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/ui -DUI_DESTINATION=${CMAKE_BINARY_DIR}/ui -DUI_INTEGRITY_HEADER=${CMAKE_BINARY_DIR}/generated/ui_integrity_manifest.h -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageUi.cmake
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LUME_WEBVIEW2_PACKAGE}/build/native/x64/WebView2Loader.dll" "${CMAKE_BINARY_DIR}/WebView2Loader.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LUME_WEBVIEW2_PACKAGE}/LICENSE.txt" "${CMAKE_BINARY_DIR}/WebView2-LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LUME_WEBVIEW2_PACKAGE}/NOTICE.txt" "${CMAKE_BINARY_DIR}/WebView2-NOTICE.txt")
add_dependencies(LumeShell lume-ui-assets)
add_dependencies(lume-ui-integrity lume-ui-assets)
add_dependencies(LumeShell LumeMinimizeHost)
add_custom_target(lume-taskbar-assets ALL
  COMMAND ${CMAKE_COMMAND}
    -DTASKBAR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/vendor/taskbar-runtime/2026.1
    -DTASKBAR_DESTINATION=${CMAKE_BINARY_DIR}/taskbar-runtime
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageTaskbar.cmake)
add_dependencies(LumeShell lume-taskbar-assets)
