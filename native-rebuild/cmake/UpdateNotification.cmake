include_guard(GLOBAL)
get_filename_component(_update_notification_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(lume-update-notification-core STATIC
  "${_update_notification_root}/core/update_notification/update_notification.cpp")
target_include_directories(lume-update-notification-core PUBLIC "${_update_notification_root}")
target_link_libraries(lume-update-notification-core PUBLIC lume-application-settings-session)
lume_enable_native_warnings(lume-update-notification-core)
add_library(lume-update-presenter STATIC
  "${_update_notification_root}/platform/windows/update_notification/update_presenter.cpp")
target_include_directories(lume-update-presenter PUBLIC "${_update_notification_root}")
target_compile_definitions(lume-update-presenter PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-update-presenter PRIVATE lume-widget-material lume-widget-ui user32 gdi32 gdiplus)
lume_enable_native_warnings(lume-update-presenter)
add_library(lume-update-notification-session STATIC
  "${_update_notification_root}/apps/shell/update_notification_session.cpp")
target_include_directories(lume-update-notification-session PUBLIC "${_update_notification_root}")
target_compile_definitions(lume-update-notification-session PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lume-update-notification-session PUBLIC lume-update-notification-core
  PRIVATE lume-update-presenter user32 shell32)
lume_enable_native_warnings(lume-update-notification-session)
if(BUILD_TESTING)
  add_executable(lume-desktop-update-notification-test
    "${_update_notification_root}/tests/unit/desktop_update_notification_test.cpp")
  target_link_libraries(lume-desktop-update-notification-test PRIVATE lume-desktop-application)
  target_compile_definitions(lume-desktop-update-notification-test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(MINGW)
    target_link_options(lume-desktop-update-notification-test PRIVATE -static)
  endif()
  lume_enable_native_warnings(lume-desktop-update-notification-test)
  add_test(NAME lume-desktop-update-notification-test
    COMMAND lume-desktop-update-notification-test "${CMAKE_BINARY_DIR}/update-composition-test-data")
  set_tests_properties(lume-desktop-update-notification-test PROPERTIES TIMEOUT 25 RUN_SERIAL TRUE LABELS "update-notification;isolated")
  add_executable(lume-update-notification-policy-test
    "${_update_notification_root}/tests/unit/update_notification_policy_test.cpp")
  target_link_libraries(lume-update-notification-policy-test PRIVATE lume-update-notification-core)
  if(MINGW)
    target_link_options(lume-update-notification-policy-test PRIVATE -static)
  endif()
  lume_enable_native_warnings(lume-update-notification-policy-test)
  add_test(NAME lume-update-notification-policy-test COMMAND lume-update-notification-policy-test)
  set_tests_properties(lume-update-notification-policy-test PROPERTIES
    TIMEOUT 15 LABELS "update-notification;isolated")
  add_executable(lume-update-notification-session-test
    "${_update_notification_root}/tests/unit/update_notification_session_test.cpp")
  target_link_libraries(lume-update-notification-session-test PRIVATE lume-update-notification-session)
  target_compile_definitions(lume-update-notification-session-test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(MINGW)
    target_link_options(lume-update-notification-session-test PRIVATE -static)
  endif()
  lume_enable_native_warnings(lume-update-notification-session-test)
  add_test(NAME lume-update-notification-session-test
    COMMAND lume-update-notification-session-test "${CMAKE_BINARY_DIR}/update-notification-test-data")
  set_tests_properties(lume-update-notification-session-test PROPERTIES
    TIMEOUT 15 RUN_SERIAL TRUE LABELS "update-notification;isolated")
  add_executable(lume-application-settings-update-observer-test
    "${_update_notification_root}/tests/unit/application_settings_update_observer_test.cpp")
  target_link_libraries(lume-application-settings-update-observer-test PRIVATE lume-application-settings-session)
  target_compile_definitions(lume-application-settings-update-observer-test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  add_executable(lume-update-presenter-test "${_update_notification_root}/tests/unit/update_presenter_test.cpp")
  target_link_libraries(lume-update-presenter-test PRIVATE lume-update-presenter user32 gdi32)
  target_compile_definitions(lume-update-presenter-test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  # Keep the ABI fixture separate from production LumeGlass.dll and fallback tests.
  add_library(lume-update-material-fixture SHARED "${_update_notification_root}/tests/unit/update_presenter_test.cpp")
  target_include_directories(lume-update-material-fixture PRIVATE "${_update_notification_root}")
  target_compile_definitions(lume-update-material-fixture PRIVATE LUME_UPDATE_MATERIAL_FIXTURE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(lume-update-material-fixture PRIVATE user32)
  set_target_properties(lume-update-material-fixture PROPERTIES PREFIX "" OUTPUT_NAME "LumeGlass"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/update-presenter-fixture")
  foreach(_target IN ITEMS lume-application-settings-update-observer-test lume-update-presenter-test lume-update-material-fixture)
    if(MINGW)
      target_link_options(${_target} PRIVATE -static)
    endif()
    lume_enable_native_warnings(${_target})
  endforeach()
  set_target_properties(lume-update-presenter-test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/update-presenter-fallback")
  add_dependencies(lume-update-presenter-test lume-update-material-fixture)
  add_custom_command(TARGET lume-update-presenter-test POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:lume-update-presenter-test>" "$<TARGET_FILE_DIR:lume-update-material-fixture>/lume-update-presenter-test.exe")
  add_test(NAME lume-application-settings-update-observer-test COMMAND lume-application-settings-update-observer-test)
  set_tests_properties(lume-application-settings-update-observer-test PROPERTIES TIMEOUT 20 LABELS "update-notification;isolated")
  add_test(NAME lume-update-presenter-test COMMAND lume-update-presenter-test)
  add_test(NAME lume-update-presenter-fixture-test
    COMMAND "$<TARGET_FILE_DIR:lume-update-material-fixture>/lume-update-presenter-test.exe" --fixture)
  set_tests_properties(lume-update-presenter-test lume-update-presenter-fixture-test PROPERTIES
    TIMEOUT 30 RUN_SERIAL TRUE LABELS "update-notification;isolated")
endif()
