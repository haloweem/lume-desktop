#include "apps/music-host/music_host_runtime.h"
#include "platform/windows/music_audio_engine.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <string>

namespace {
bool ReadExact(HANDLE pipe, void* buffer, DWORD bytes) {
    auto* output = static_cast<std::uint8_t*>(buffer);
    while (bytes > 0) {
        DWORD read = 0;
        if (!ReadFile(pipe, output, bytes, &read, nullptr) || read == 0) return false;
        output += read;
        bytes -= read;
    }
    return true;
}

bool WriteExact(HANDLE pipe, const void* buffer, DWORD bytes) {
    const auto* input = static_cast<const std::uint8_t*>(buffer);
    while (bytes > 0) {
        DWORD written = 0;
        if (!WriteFile(pipe, input, bytes, &written, nullptr) || written == 0) return false;
        input += written;
        bytes -= written;
    }
    return true;
}

int RunPipe(const wchar_t* pipeName) {
    if (!pipeName || !*pipeName) return 2;
    HANDLE pipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return 2;
    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    lume::music::WindowsAudioEngine audio;
    lume::music::MusicHostRuntime runtime(audio);
    for (;;) {
        std::uint32_t size = 0;
        if (!ReadExact(pipe, &size, sizeof(size))) break;
        if (size == 0 || size > lume::music::kMaxMusicHostFrameBytes) break;
        std::string frame(size, '\0');
        if (!ReadExact(pipe, frame.data(), size)) break;
        const auto response = runtime.Handle(frame);
        if (response) {
            const auto responseSize = static_cast<std::uint32_t>(response->size());
            if (responseSize > lume::music::kMaxMusicHostFrameBytes ||
                !WriteExact(pipe, &responseSize, sizeof(responseSize)) ||
                !WriteExact(pipe, response->data(), responseSize)) break;
        }
        if (runtime.Closed()) break;
    }
    CloseHandle(pipe);
    audio.Stop();
    return 0;
}
}

int RunMusicHost(int argc, wchar_t** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (argv[index] && wcscmp(argv[index], L"--pipe") == 0) return RunPipe(argv[index + 1]);
    }
    return 0;
}

#ifndef LUME_NATIVE_TESTING
#if defined(_MSC_VER)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return RunMusicHost(__argc, __wargv);
}
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    auto* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const int result = RunMusicHost(argc, argv);
    LocalFree(argv);
    return result;
}
#endif
#endif
