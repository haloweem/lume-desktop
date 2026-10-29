#include <cassert>

int RunLauncher(int argc, wchar_t** argv);
int RunShell(int argc, wchar_t** argv);
int RunMusicHost(int argc, wchar_t** argv);

int main() {
    wchar_t* argv[] = {nullptr};
    (void)argv;
    assert(RunLauncher(0, argv) == 0);
    assert(RunShell(0, argv) == 0);
    assert(RunMusicHost(0, argv) == 0);
    return 0;
}
