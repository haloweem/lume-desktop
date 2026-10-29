include_guard(GLOBAL)
get_filename_component(_application_settings_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_package(Threads REQUIRED)

add_library(lume-application-settings-session STATIC
  "${_application_settings_root}/core/application_settings/preferences_codec.cpp"
  "${_application_settings_root}/core/application_settings/service.cpp"
  "${_application_settings_root}/core/application_settings/update_policy.cpp"
  "${_application_settings_root}/core/ui/application_settings_page_protocol.cpp"
  "${_application_settings_root}/platform/windows/application_settings/preferences_file.cpp"
  "${_application_settings_root}/platform/windows/application_settings/startup_registration.cpp"
  "${_application_settings_root}/platform/windows/application_settings/startup_task.cpp"
  "${_application_settings_root}/platform/windows/application_settings/update_transport.cpp"
  "${_application_settings_root}/apps/shell/application_settings_session.cpp"
)
target_include_directories(lume-application-settings-session PUBLIC "${_application_settings_root}")
target_compile_features(lume-application-settings-session PUBLIC cxx_std_17)
target_compile_definitions(lume-application-settings-session PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-application-settings-session PRIVATE Threads::Threads winhttp advapi32 user32 bcrypt shell32 ole32 oleaut32 taskschd)
if(COMMAND lume_enable_native_warnings)
  lume_enable_native_warnings(lume-application-settings-session)
endif()

if(BUILD_TESTING)
  foreach(_test IN ITEMS protocol service update file session installer)
    set(_target "lume-application-settings-${_test}-test")
    add_executable(${_target} "${_application_settings_root}/tests/unit/application_settings_${_test}_test.cpp")
    target_link_libraries(${_target} PRIVATE lume-application-settings-session Threads::Threads)
    target_compile_definitions(${_target} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MINGW)
      target_link_options(${_target} PRIVATE -static)
    endif()
    if(COMMAND lume_enable_native_warnings)
      lume_enable_native_warnings(${_target})
    endif()
    add_test(NAME ${_target} COMMAND ${_target} "${_application_settings_root}/../build-artifacts/application-settings-tests")
    set_tests_properties(${_target} PROPERTIES TIMEOUT 20 LABELS "application-settings;isolated")
  endforeach()
endif()
