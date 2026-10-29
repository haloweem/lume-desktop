# Fixed Microsoft SDK; runtime is the separately serviced Evergreen installation.
set(LUME_WEBVIEW2_VERSION "1.0.2903.40")
set(LUME_WEBVIEW2_SHA256 "ef128016dd1e51c59178c827ed5b8aa3322c57afa8675d930f8109505542ad74")
set(LUME_WEBVIEW2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../build-artifacts/dependencies/webview2-${LUME_WEBVIEW2_VERSION}")
set(LUME_WEBVIEW2_PACKAGE "${LUME_WEBVIEW2_ROOT}/package")
file(MAKE_DIRECTORY "${LUME_WEBVIEW2_ROOT}")
if(NOT EXISTS "${LUME_WEBVIEW2_ROOT}/sdk.zip")
  file(DOWNLOAD
    "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${LUME_WEBVIEW2_VERSION}/microsoft.web.webview2.${LUME_WEBVIEW2_VERSION}.nupkg"
    "${LUME_WEBVIEW2_ROOT}/sdk.zip"
    EXPECTED_HASH "SHA256=${LUME_WEBVIEW2_SHA256}" TLS_VERIFY ON)
endif()
file(SHA256 "${LUME_WEBVIEW2_ROOT}/sdk.zip" sdk_hash)
if(NOT sdk_hash STREQUAL LUME_WEBVIEW2_SHA256)
  message(FATAL_ERROR "WebView2 SDK hash mismatch")
endif()
if(NOT EXISTS "${LUME_WEBVIEW2_PACKAGE}/build/native/include/WebView2.h")
  file(ARCHIVE_EXTRACT INPUT "${LUME_WEBVIEW2_ROOT}/sdk.zip" DESTINATION "${LUME_WEBVIEW2_PACKAGE}")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This installer currently targets Windows x64")
endif()
