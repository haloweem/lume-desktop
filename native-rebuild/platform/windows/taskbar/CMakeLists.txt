add_library(lume-taskbar-windows STATIC
  taskbar_payload.cpp taskbar_core_process.cpp taskbar_port.cpp taskbar_visibility.cpp taskbar_watch.cpp taskbar_runtime.cpp)
target_include_directories(lume-taskbar-windows PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../../..")
target_compile_features(lume-taskbar-windows PUBLIC cxx_std_17)
target_compile_definitions(lume-taskbar-windows PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-taskbar-windows PUBLIC lume-taskbar-core lume-platform-windows PRIVATE user32 shell32 bcrypt)
if(COMMAND lume_enable_native_warnings)
  lume_enable_native_warnings(lume-taskbar-windows)
endif()
