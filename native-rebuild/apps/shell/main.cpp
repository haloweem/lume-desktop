#include <windows.h>
#include "apps/shell/desktop_application.h"
int RunShell(int argc, wchar_t** argv) {
    (void)argc; (void)argv;
#ifdef LUME_NATIVE_TESTING
    return 0;
#else
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    return RunDesktopApplication(GetModuleHandleW(nullptr));
#endif
}
#ifndef LUME_NATIVE_TESTING
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return RunShell(0,nullptr); }
#endif
