#include "platform/windows/music_metadata.h"
#include "apps/shell/music_library_session.h"
#include "tests/unit/unique_test_directory.h"

#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
bool passed = true;
void Check(bool value, const char* message) {
    if (!value) { passed = false; std::cerr << "FAIL: " << message << '\n'; }
}
void Require(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("fixture WIC failure"); }
void Be32(std::string& bytes, unsigned value) {
    for (int i = 24; i >= 0; i -= 8) bytes += static_cast<char>((value >> i) & 255);
}
std::string Png(unsigned width, unsigned height) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    Require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    Require(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
    Require(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    Require(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    Require(encoder->CreateNewFrame(&frame, nullptr));
    Require(frame->Initialize(nullptr));
    Require(frame->SetSize(width, height));
    auto format = GUID_WICPixelFormat24bppBGR;
    Require(frame->SetPixelFormat(&format));
    std::vector<BYTE> pixels(static_cast<std::size_t>(width) * height * 3, 80);
    Require(frame->WritePixels(height, width * 3, static_cast<UINT>(pixels.size()), pixels.data()));
    Require(frame->Commit());
    Require(encoder->Commit());
    STATSTG stat{};
    Require(stream->Stat(&stat, STATFLAG_NONAME));
    LARGE_INTEGER zero{};
    Require(stream->Seek(zero, STREAM_SEEK_SET, nullptr));
    std::string result(static_cast<std::size_t>(stat.cbSize.QuadPart), '\0');
    ULONG read = 0;
    Require(stream->Read(result.data(), static_cast<ULONG>(result.size()), &read));
    return result;
}
void Frame(std::string& tag, const char* name, const std::string& payload) {
    tag += name;
    Be32(tag, static_cast<unsigned>(payload.size()));
    tag.append(2, '\0');
    tag += payload;
}
void Mp3(const std::filesystem::path& path, const std::string& cover) {
    std::string tag;
    Frame(tag, "TIT2", std::string(1, '\0') + "Native title");
    Frame(tag, "TPE1", std::string(1, '\0') + "Native artist");
    Frame(tag, "TALB", std::string(1, '\0') + "Native album");
    if (!cover.empty()) {
        std::string payload(1, '\0');
        payload += "image/png";
        payload += '\0'; payload += '\3'; payload += '\0';
        payload += cover;
        Frame(tag, "APIC", payload);
    }
    std::string bytes("ID3\3\0\0", 6);
    for (int i = 21; i >= 0; i -= 7) bytes += static_cast<char>((tag.size() >> i) & 127);
    bytes += tag;
    // MPEG-1 layer III, 128 kbps / 44.1 kHz. Enough frames for native metadata probing.
    for (unsigned i = 0; i < 40; ++i) {
        bytes.append("\xff\xfb\x90\x64", 4);
        bytes.append(413, '\0');
    }
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
}

int main() {
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto root = UniqueTestDirectory(std::filesystem::temp_directory_path(), "lume-metadata-");
    const auto path = root / "tagged.mp3";
    Mp3(path, Png(640, 320));
    const auto metadata = lume::music::ReadMusicMetadata(path.wstring());
    Check(metadata.title == "Native title" && metadata.artist == "Native artist" &&
        metadata.album == "Native album", "native property handler reads embedded ID3 tags");
    Check(metadata.duration > 0, "native property handler reads audio duration");
    Check(!metadata.artworkPng.empty() && metadata.artworkPng.size() <= lume::music::kMaxMusicArtworkBytes,
        "native embedded artwork is decoded and bounded");
    if (metadata.artworkPng.size() >= 24) {
        const auto& p = metadata.artworkPng;
        const auto width = (static_cast<unsigned char>(p[18]) << 8) | static_cast<unsigned char>(p[19]);
        const auto height = (static_cast<unsigned char>(p[22]) << 8) | static_cast<unsigned char>(p[23]);
        Check(width == 256 && height == 128, "cover resized to display dimensions, preserving aspect ratio");
    }
    Mp3(root / "bad-cover.mp3", "not an image");
    const auto badCover = lume::music::ReadMusicMetadata((root / "bad-cover.mp3").wstring());
    Check(badCover.title == "Native title" && badCover.artworkPng.empty(),
        "bad image does not discard valid tags");
    Mp3(root / "large-cover.mp3", Png(8193, 1));
    const auto largeCover = lume::music::ReadMusicMetadata((root / "large-cover.mp3").wstring());
    Check(largeCover.artworkPng.empty(), "oversize image dimensions rejected before decode");
    {
        std::ofstream malformed(root / "broken.mp3", std::ios::binary);
        malformed << "ID3";
    }
    Check(lume::music::ReadMusicMetadata((root / "broken.mp3").wstring()).artworkPng.empty(),
        "malformed audio safely falls back without invented metadata");
    {
        lume::shell::MusicLibrarySession session({root / "state"});
        Check(session.Import({path.wstring()}).added == 1, "tagged track imports through session");
        const auto track = session.Tracks().front();
        Check(track.title == metadata.title && track.artist == metadata.artist &&
            track.album == metadata.album && track.artwork == "music-artwork:" + track.id,
            "shared native track carries tags and opaque artwork identity");
        Check(session.Artwork(track.id) == metadata.artworkPng, "session exposes bounded image bytes, not a path");
        for (unsigned i = 0; i < 260; ++i) {
            const auto cached = root / "state" / "artwork" / ("local-" + std::to_string(i) + "-1-1.png");
            std::ofstream out(cached, std::ios::binary);
            out << metadata.artworkPng;
        }
    }
    {
        lume::shell::MusicLibrarySession restarted({root / "state"});
        Check(restarted.Tracks().size() == 1, "tagged track restores without re-extraction");
        Check(std::distance(std::filesystem::directory_iterator(root / "state" / "artwork"),
            std::filesystem::directory_iterator{}) <= 256, "disk artwork entry limit enforced on restore");
        const auto track = restarted.Tracks().front();
        Check(restarted.Artwork(track.id) == metadata.artworkPng,
            "artwork remains available after restart or cache eviction");
        Check(restarted.State().find(L"data:image") == std::wstring::npos,
            "bridge never duplicates embedded image bytes");
    }
    std::filesystem::remove_all(root);
    if (SUCCEEDED(com)) CoUninitialize();
    if (!passed) return 1;
    std::cout << "native music metadata/artwork checks passed\n";
}
