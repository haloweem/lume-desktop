#include "platform/windows/music_local_files.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
bool passed = true;
void Check(bool value, const char* message) {
    if (!value) {
        passed = false;
        std::cerr << "FAIL: " << message << '\n';
    }
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        (L"lume-music-local-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto wav = root / L"测试歌曲.wav";
    const auto txt = root / L"not-a-song.txt";
    { std::ofstream file(wav, std::ios::binary); file << "RIFF"; }
    { std::ofstream file(txt, std::ios::binary); file << "text"; }

    const auto valid = lume::music::ImportLocalMusicFiles({wav.wstring()});
    Check(valid.error.empty() && valid.tracks.size() == 1, "existing audio file imports");
    if (!valid.tracks.empty()) {
        Check(valid.tracks.front().title == "测试歌曲", "filename becomes the track title");
        Check(!valid.tracks.front().source.empty(), "track exposes a normalized source path");
    }
    Check(lume::music::ImportLocalMusicFiles({txt.wstring()}).error == "unsupported-type",
        "non-audio extensions are rejected");
    Check(lume::music::ImportLocalMusicFiles({(root / L"missing.mp3").wstring()}).error == "missing",
        "missing paths are rejected");
    std::vector<std::wstring> tooMany(33, wav.wstring());
    Check(lume::music::ImportLocalMusicFiles(tooMany).error == "too-many-files",
        "more than 32 files are rejected");
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    if (!passed) return 1;
    std::cout << "music local file bounds passed\n";
    return 0;
}
