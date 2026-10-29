if(NOT DEFINED TASKBAR_SOURCE OR NOT DEFINED TASKBAR_DESTINATION)
  message(FATAL_ERROR "Explicit taskbar source and destination are required")
endif()
set(payload_files
  LICENSE.md NOTICE.md SOURCE.md lume-taskbar-core.patch settings.json SHA256SUMS.txt
  MSVC-RUNTIME-LICENSE.docx MSVC-RUNTIME-SOURCES.md MSVC-RUNTIME-DEPENDENCIES.json)
foreach(architecture x64 arm64)
  foreach(file LumeTaskbarCore.exe ExplorerHooks.dll ExplorerTAP.dll ProgramLog.dll Xaml.dll
               resources.pri Assets/SplashScreen.jpeg Assets/SplashScreen.scale-400.jpeg
               msvcp140.dll msvcp140_atomic_wait.dll vcruntime140.dll)
    list(APPEND payload_files "${architecture}/${file}")
  endforeach()
endforeach()
list(APPEND payload_files "x64/vcruntime140_1.dll")
foreach(relative IN LISTS payload_files)
  if(NOT EXISTS "${TASKBAR_SOURCE}/${relative}")
    message(FATAL_ERROR "Missing pinned taskbar dependency: ${relative}")
  endif()
  get_filename_component(parent "${relative}" DIRECTORY)
  file(MAKE_DIRECTORY "${TASKBAR_DESTINATION}/${parent}")
  file(COPY_FILE "${TASKBAR_SOURCE}/${relative}" "${TASKBAR_DESTINATION}/${relative}" ONLY_IF_DIFFERENT)
endforeach()
