#include <windows.h>
#include <cstdlib>

int RunLauncher(int argc, wchar_t** argv) {
    (void)argc;
    (void)argv;
    return 0;
}

#ifndef LUME_NATIVE_TESTING
#if defined(_MSC_VER)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return RunLauncher(__argc, __wargv);
}
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return RunLauncher(0, nullptr);
}
#endif
#endif
