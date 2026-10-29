include_guard(GLOBAL)
get_filename_component(_lume_account_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(lume-account-session STATIC
  "${_lume_account_root}/apps/shell/account_session.cpp"
  "${_lume_account_root}/core/account/security.cpp"
  "${_lume_account_root}/core/account/jwt.cpp"
  "${_lume_account_root}/core/account/backend.cpp"
  "${_lume_account_root}/core/ui/account_page_protocol.cpp"
  "${_lume_account_root}/platform/windows/account/crypto.cpp"
  "${_lume_account_root}/platform/windows/account/credentials.cpp"
  "${_lume_account_root}/platform/windows/account/avatar.cpp"
  "${_lume_account_root}/platform/windows/account/http.cpp"
  "${_lume_account_root}/platform/windows/account/clock.cpp"
  "${_lume_account_root}/platform/windows/account/protocol_registration.cpp"
)
target_include_directories(lume-account-session PUBLIC "${_lume_account_root}")
target_compile_features(lume-account-session PUBLIC cxx_std_17)
target_compile_definitions(lume-account-session PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-account-session PRIVATE bcrypt crypt32 winhttp windowscodecs ole32 shell32 uuid user32 advapi32)
if(MSVC)
  target_compile_options(lume-account-session PRIVATE /W4 /WX)
else()
  target_compile_options(lume-account-session PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(BUILD_TESTING)
  foreach(suite IN ITEMS security jwt storage avatar http backend session protocol rotation background engagement)
    add_executable(lume-account-${suite}-test "${_lume_account_root}/tests/unit/account_${suite}_test.cpp")
    target_link_libraries(lume-account-${suite}-test PRIVATE lume-account-session)
    target_compile_definitions(lume-account-${suite}-test PRIVATE
      LUME_ACCOUNT_TEST_ARTIFACTS="${CMAKE_CURRENT_BINARY_DIR}/account-test-data")
    if(MINGW)
      target_link_options(lume-account-${suite}-test PRIVATE -static)
    endif()
    add_test(NAME lume-account-${suite} COMMAND lume-account-${suite}-test)
    set_tests_properties(lume-account-${suite} PROPERTIES TIMEOUT 30 LABELS "account;isolated")
  endforeach()
endif()
