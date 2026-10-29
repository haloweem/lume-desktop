#include "apps/shell/music_background_session.h"
#include "platform/windows/local_settings_file_store.h"
#include "vendor/nlohmann/json.hpp"
#include "core/music/music_background_store.h"
#include "core/wallpaper/library.h"
#include "platform/windows/wallpaper/download.h"

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <winioctl.h>
#include <wincodec.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace lume::shell::background_preview {
std::shared_ptr<std::vector<unsigned char>> VideoFrameThumbnail(
    UINT width, UINT height, UINT displayWidth, UINT displayHeight,
    LONG signedStride, BYTE* pixels, DWORD bytes);
}

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;
using lume::shell::MusicBackgroundSession;
using lume::shell::MusicBackgroundSessionOptions;
using lume::ui::MusicBackgroundAction;
using lume::ui::MusicBackgroundCommand;

void Check(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

Json State(const std::wstring& value) { return Json::parse(value); }

Json Wait(MusicBackgroundSession& session) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (const auto message = session.DrainNotification()) return State(*message);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("background worker timed out");
}

Json Execute(MusicBackgroundSession& session, const MusicBackgroundCommand& command) {
    auto state = State(session.HandlePage(command));
    return state["status"] == "busy" ? Wait(session) : state;
}

MusicBackgroundCommand Command(MusicBackgroundAction action, std::uint64_t request,
    std::uint64_t revision) {
    MusicBackgroundCommand command;
    command.action = action; command.request = request; command.revision = revision;
    return command;
}

struct Fixture {
    fs::path root = fs::temp_directory_path() /
        ("lume-music-background-session-" + std::to_string(GetCurrentProcessId()));
    fs::path source = root / "source.png";
    fs::path second = root / "second.webm";
    std::wstring selected;
    std::atomic<unsigned> wakes{0};
    lume::platform::windows::LocalSettingsFileStore files;
    MusicBackgroundSessionOptions options;

    Fixture() {
        std::error_code error;
        fs::remove_all(root, error);
        fs::create_directories(root, error);
        std::ofstream(source, std::ios::binary) << "png-fixture";
        std::ofstream(second, std::ios::binary) << "webm-fixture";
        options.root = root.u8string() + "/store";
        options.statePath = (fs::u8path(options.root) / "music-background.json").u8string();
        options.verification = true;
        options.chooseMedia = [this](HWND) { return selected; };
        options.wallpaperLibraryRoot = (root / "wallpapers").u8string();
        options.wake = [this] { ++wakes; };
    }

    ~Fixture() {
        std::error_code error;
        fs::remove_all(root, error);
    }

    MusicBackgroundSession Open() { return MusicBackgroundSession(options, files); }
};

void ImportAndClear() {
    Fixture fixture;
    auto session = fixture.Open();
    Check(session.Restore(), "fresh session restores");
    auto state = State(session.State());
    Check(state["revision"] == 0 && state["media"].is_null() && state["status"] == "ready",
        "fresh session exposes an empty ready state");

    fixture.selected = fixture.source.wstring();
    state = Execute(session, Command(MusicBackgroundAction::Choose, 1, 0));
    Check(state["request"] == 1 && state["revision"] == 1 && state["status"] == "ready",
        "choose commits the selected background");
    Check(state["media"]["kind"] == "image" && state["media"]["mimeType"] == "image/png" &&
        state["media"]["name"] == "source.png", "choose preserves media metadata");
    const auto id = state["media"]["id"].get<std::string>();
    Check(fs::exists(fs::u8path(fixture.options.root) / "media" /
        fs::u8path(id + ".png")), "choose copies media into the isolated directory");

    auto cleared = State(session.HandlePage(Command(MusicBackgroundAction::Clear, 2, 1)));
    Check(cleared["media"].is_null() && cleared["revision"] == 2,
        "clear removes the first committed background");
}

void ReplacementAndConflict() {
    Fixture fixture;
    auto session = fixture.Open();
    Check(session.Restore(), "replacement session restores");

    fixture.selected = fixture.source.wstring();
    auto first = Execute(session, Command(MusicBackgroundAction::Choose, 1, 0));
    const auto firstId = first["media"]["id"].get<std::string>();
    const auto firstPath = fs::u8path(fixture.options.root) / "media" /
        fs::u8path(firstId + ".png");
    Check(fs::exists(firstPath), "first media is copied into the isolated directory");

    fixture.selected = fixture.second.wstring();
    auto conflict = Execute(session, Command(MusicBackgroundAction::Choose, 2, 0));
    Check(conflict["error"] == "conflict" && conflict["revision"] == 1 &&
        conflict["media"]["id"] == firstId, "stale replacement leaves committed media intact");

    auto second = Execute(session, Command(MusicBackgroundAction::Choose, 3, 1));
    const auto secondId = second["media"]["id"].get<std::string>();
    Check(second["media"]["kind"] == "video" && secondId != firstId,
        "replacement accepts a current revision");
    Check(fs::exists(firstPath), "replacement retains backup media");

    auto cleared = State(session.HandlePage(Command(MusicBackgroundAction::Clear, 4, 2)));
    Check(cleared["media"].is_null() && cleared["revision"] == 3, "clear commits an empty background");
    Check(fs::exists(fs::u8path(fixture.options.root) / "media" /
        fs::u8path(secondId + ".webm")) && !fs::exists(firstPath),
        "clear retains its backup and prunes superseded media");
}

void InvalidAndMissingRecovery() {
    Fixture fixture;
    const auto invalid = fixture.root / "bad.txt";
    std::ofstream(invalid, std::ios::binary) << "not-media";
    auto session = fixture.Open();
    Check(session.Restore(), "invalid fixture session restores");
    fixture.selected = invalid.wstring();
    auto rejected = Execute(session, Command(MusicBackgroundAction::Choose, 1, 0));
    Check(rejected["error"] == "unsupported" && rejected["revision"] == 0,
        "unsupported extensions are rejected without changing state");

    fixture.selected = fixture.source.wstring();
    const auto committed = Execute(session, Command(MusicBackgroundAction::Choose, 2, 0));
    const auto id = committed["media"]["id"].get<std::string>();
    session.SetVisible(false);
    fs::remove(fs::u8path(fixture.options.root) / "media" / fs::u8path(id + ".png"));

    auto reopened = fixture.Open();
    Check(reopened.Restore(), "missing media does not block session restore");
    const auto recovered = State(reopened.State());
    Check(recovered["media"].is_null() && recovered["revision"] == 1 && recovered["error"] == "load",
        "missing media is not served or silently erased from persisted state");
}

void CancelAndShutdown() {
    Fixture fixture;
    auto session = fixture.Open();
    Check(session.Restore(), "cancel fixture restores");
    auto state = State(session.HandlePage(Command(MusicBackgroundAction::Choose, 1, 0)));
    Check(state["request"] == 1 && state["revision"] == 0 && state["status"] == "ready",
        "cancelled picker acknowledges without mutating state");
    session.Shutdown();
    Check(!session.HasWork(), "shutdown has no pending work");
    Check(State(session.State())["error"] == "load", "shutdown blocks future state access");
}

void EmptyRootDoesNotExposeRelativeMediaDirectory() {
    lume::platform::windows::LocalSettingsFileStore files;
    MusicBackgroundSessionOptions options;
    auto session = MusicBackgroundSession(options, files);
    Check(session.MediaDirectory().empty(),
        "disabled music background must not expose a relative media directory");
}

std::string Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void Write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Check(bool(output), "fixture write");
}

using Storage = lume::music::MusicBackgroundStorage;
using Media = lume::music::MusicBackgroundMedia;
using Store = lume::music::MusicBackgroundStore;
using Result = lume::music::MusicBackgroundResult;
using Reader = lume::platform::windows::wallpaper::EncryptedMediaReader;

struct FailingStore final : lume::settings::IFileStore {
    lume::platform::windows::LocalSettingsFileStore files;
    std::string failRenameTo;
    bool Read(const std::string& path, std::string& bytes) const override { return files.Read(path, bytes); }
    bool ReadBounded(const std::string& path, std::size_t size, std::string& bytes) const override {
        return files.ReadBounded(path, size, bytes);
    }
    bool Exists(const std::string& path) const override { return files.Exists(path); }
    bool Write(const std::string& path, const std::string& bytes) override { return files.Write(path, bytes); }
    bool Rename(const std::string& from, const std::string& to) override {
        return to != failRenameTo && files.Rename(from, to);
    }
    bool Remove(const std::string& path) override { return files.Remove(path); }
};

MusicBackgroundCommand Catalog(std::uint64_t request, std::uint64_t offset = 0, std::uint64_t limit = 120) {
    auto command = Command(MusicBackgroundAction::Catalog, request, 0);
    command.offset = offset; command.limit = limit;
    return command;
}

MusicBackgroundCommand Select(const std::string& id, std::uint64_t request, std::uint64_t revision) {
    auto command = Command(MusicBackgroundAction::Select, request, revision);
    command.id = id;
    return command;
}

std::string ProtectedFixture(Fixture& fixture, const std::string& id, const std::string& plain,
    bool image = false, bool remoteThumbnail = true) {
    const fs::path root = fs::u8path(fixture.options.wallpaperLibraryRoot);
    fs::create_directories(root / "media");
    const auto path = root / "media" / (id + ".apk");
    std::string error;
    auto writer = lume::platform::windows::wallpaper::MakeProtectedWriterFactory(root.u8string())(path.u8string(), error);
    Check(bool(writer), "isolated encrypted fixture writer");
    Check(writer->Append(reinterpret_cast<const std::uint8_t*>(plain.data()), plain.size(), error) &&
        writer->Finish(error), "isolated encrypted fixture sealed");
    writer.reset();
    lume::wallpaper::Library library(root.u8string());
    Check(library.Load().ok, "load fixture library");
    lume::wallpaper::CatalogItem item;
    item.id = id; item.title = "Protected Fixture";
    item.kind = image ? lume::wallpaper::MediaKind::Image : lume::wallpaper::MediaKind::Video;
    item.mediaUrl = image ? "https://static.lumedesktop.com/lume/wallpapers/official/fixture.png" :
        "https://static.lumedesktop.com/lume/wallpapers/official/fixture.mp4";
    if (remoteThumbnail)
        item.thumbnailUrl = "https://static.lumedesktop.com/lume/wallpapers/official/fixture.jpg";
    item.sizeBytes = plain.size(); item.width = 1920; item.height = 1080;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    Check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
        "fixture SHA256 provider");
    unsigned char digest[32]{};
    const auto hashed = BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(plain.data())), static_cast<ULONG>(plain.size()),
        digest, sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    Check(hashed >= 0, "fixture SHA256");
    for (const auto byte : digest) {
        item.sha256 += "0123456789abcdef"[byte >> 4];
        item.sha256 += "0123456789abcdef"[byte & 15];
    }
    Check(library.RegisterDownload(item, path.u8string()).ok, "register isolated protected fixture");
    return path.u8string();
}

std::string ReadPlain(const std::string& root, const std::string& path) {
    std::string error;
    auto reader = Reader::Open(root, path, error);
    Check(bool(reader), "native encrypted reader opens music asset");
    std::string plain(static_cast<std::size_t>(reader->Size()), '\0');
    std::size_t read = 0;
    Check(reader->Read(0, plain.data(), plain.size(), read) && read == plain.size(), "authenticated music asset bytes");
    return plain;
}

std::string UnwrapFixtureKey(const fs::path& path) {
    auto encoded = Read(path);
    DATA_BLOB input{static_cast<DWORD>(encoded.size()), reinterpret_cast<BYTE*>(encoded.data())}, output{};
    Check(CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output),
        "unwrap isolated test-only key");
    std::string key(reinterpret_cast<char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData); LocalFree(output.pbData);
    return key;
}

struct Response { DWORD status = 0; std::string body, cors; };
Response Get(const std::string& url, const std::string& range = {}, const wchar_t* method = L"GET") {
    // This test client only accepts the music session's loopback capabilities.
    Check(url.rfind("http://127.0.0.1:", 0) == 0, "test HTTP is loopback-only");
    const auto slash = url.find('/', 17);
    const auto port = static_cast<INTERNET_PORT>(std::stoi(url.substr(17, slash - 17)));
    const std::wstring path(url.begin() + static_cast<std::ptrdiff_t>(slash), url.end());
    struct Internet {
        HINTERNET handle = nullptr;
        ~Internet() { if (handle) WinHttpCloseHandle(handle); }
    };
    Internet http{WinHttpOpen(L"LumeBackgroundFixture", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    Check(http.handle != nullptr, "open test HTTP");
    WinHttpSetTimeouts(http.handle, 500, 500, 500, 500);
    Internet connection{WinHttpConnect(http.handle, L"127.0.0.1", port, 0)};
    if (!connection.handle) return {};
    Internet request{WinHttpOpenRequest(connection.handle, method, path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)};
    if (!request.handle) return {};
    const auto headers = std::wstring(L"Origin: https://lume.local\r\n") +
        (range.empty() ? std::wstring{} : L"Range: " + std::wstring(range.begin(), range.end()) + L"\r\n");
    if (!WinHttpSendRequest(request.handle, headers.c_str(), static_cast<DWORD>(headers.size()),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.handle, nullptr)) return {};
    Response response;
    DWORD size = sizeof(response.status);
    Check(WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &size, WINHTTP_NO_HEADER_INDEX), "test HTTP status");
    wchar_t origin[128]{};
    size = sizeof(origin);
    if (WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_CUSTOM, L"Access-Control-Allow-Origin",
        origin, &size, WINHTTP_NO_HEADER_INDEX))
        for (const wchar_t* character = origin; *character; ++character)
            response.cors.push_back(static_cast<char>(*character));
    char bytes[65536];
    for (;;) {
        DWORD count = 0;
        if (!WinHttpReadData(request.handle, bytes, sizeof(bytes), &count) || !count) break;
        response.body.append(bytes, count);
        Check(response.body.size() <= 1024 * 1024, "test response bounded");
    }
    return response;
}

void LocalThumbnails() {
    Fixture fixture;
    // A real PNG, not the extension-only fixtures used by copy/range tests.
    const unsigned char png[] = {
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,
        8,4,0,0,0,181,28,12,2,0,0,0,11,73,68,65,84,120,218,99,100,248,15,
        0,1,5,1,1,39,24,227,102,0,0,0,0,73,69,78,68,174,66,96,130};
    Write(fixture.source, {reinterpret_cast<const char*>(png), sizeof(png)});
    lume::wallpaper::Library library(fixture.options.wallpaperLibraryRoot);
    Check(library.Load().ok && library.ImportLocal(fixture.source.u8string()).ok, "import actual PNG fixture");
    const auto path = fs::u8path(library.Items().front().localPath);
    const auto manifestPath = fs::u8path(fixture.options.wallpaperLibraryRoot) / "library.json";
    const auto manifest = Read(manifestPath);
    auto session = fixture.Open();
    Check(session.Restore(), "thumbnail restore");
    auto catalog = Execute(session, Catalog(1));
    const auto url = catalog["items"][0]["thumbnailUrl"].get<std::string>();
    Check(url.rfind("http://127.0.0.1:", 0) == 0 && url.find(path.filename().u8string()) == std::string::npos,
        "local thumbnail uses path-free capability");
    const auto reply = Get(url);
    Check(reply.status == 200 && reply.cors == "https://lume.local" && reply.body.size() <= 256 * 1024 &&
        reply.body.substr(0, 8) == std::string(reinterpret_cast<const char*>(png), 8),
        "native image decoder emits bounded real PNG thumbnail");
    const auto be = [&](std::size_t offset) {
        unsigned value = 0;
        for (unsigned i = 0; i < 4; ++i) value = (value << 8) | static_cast<unsigned char>(reply.body[offset + i]);
        return value;
    };
    Check(reply.body.size() > 24 && be(16) <= 320 && be(20) <= 180 && be(16) && be(20),
        "thumbnail fits 320x180 bounds");
    Check(Get(url, "bytes=0-7").body == reply.body.substr(0, 8), "thumbnail range uses existing native pipeline");
    Check(Read(manifestPath) == manifest && Read(path) == Read(fixture.source) &&
        !fs::exists(fs::u8path(fixture.options.statePath)), "thumbnail read mutates neither library nor preferences");
    Check(DeleteFileW(path.c_str()), "thumbnail releases original handle after decoding");
    Check(Get(url).body == reply.body, "thumbnail is an immutable memory snapshot");
    Write(path, Read(fixture.source));
    const auto refreshed = Execute(session, Catalog(2));
    const auto next = refreshed["items"][0]["thumbnailUrl"].get<std::string>();
    Check(!next.empty() && next != url && Get(url).status != 200 && Get(next).status == 200,
        "catalog refresh revokes old thumbnail capabilities");
    session.SetVisible(false);
    Check(Get(next).status != 200, "hiding music releases thumbnails");
    session.SetVisible(true);
    const auto alias = fixture.root / "linked.png";
    Check(CreateHardLinkW(alias.c_str(), path.c_str(), nullptr), "isolated thumbnail hardlink fixture");
    Check(Execute(session, Catalog(3))["items"][0]["thumbnailUrl"] == "",
        "hardlinked local media cannot become a thumbnail");
    Check(DeleteFileW(alias.c_str()), "remove isolated thumbnail alias");
    Write(path, "not-a-png");
    Check(Execute(session, Catalog(4))["items"][0]["thumbnailUrl"] == "",
        "corrupt image retains catalog entry with empty thumbnail");
    Write(path, Read(fixture.source));
    const auto wakes = fixture.wakes.load();
    session.HandlePage(Catalog(5));
    const auto until = GetTickCount64() + 5000;
    while (fixture.wakes.load() == wakes && GetTickCount64() < until) Sleep(2);
    Check(fixture.wakes.load() > wakes, "thumbnail completed before cancelled drain");
    session.CancelActive();
    Check(Wait(session)["items"].empty(), "cancelled thumbnail cannot publish a capability");
    session.Shutdown();
}

void ResizedThumbnail() {
    Fixture fixture;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Check(SUCCEEDED(initialized), "fixture COM initialization");
    {
        IWICImagingFactory* factory = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapEncoder* encoder = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        struct Cleanup {
            IWICImagingFactory*& factory; IWICStream*& stream;
            IWICBitmapEncoder*& encoder; IWICBitmapFrameEncode*& frame;
            ~Cleanup() {
                if (frame) frame->Release();
                if (encoder) encoder->Release();
                if (stream) stream->Release();
                if (factory) factory->Release();
            }
        } cleanup{factory, stream, encoder, frame};
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        std::vector<unsigned char> pixels(640 * 480 * 4);
        for (unsigned y = 0; y < 480; ++y) for (unsigned x = 0; x < 640; ++x) {
            const auto offset = (y * 640 + x) * 4;
            pixels[offset] = static_cast<unsigned char>(x);
            pixels[offset + 1] = static_cast<unsigned char>(y);
            pixels[offset + 2] = 40; pixels[offset + 3] = 255;
        }
        Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromFilename(fixture.source.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
            SUCCEEDED(frame->SetSize(640, 480)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
            SUCCEEDED(frame->WritePixels(480, 640 * 4, static_cast<UINT>(pixels.size()), pixels.data())) &&
            SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()), "generate real 640x480 PNG");
    }
    CoUninitialize();
    // Valid PNG with a large trailing region: WIC must seek/read the picture,
    // not reject the source at the old 32 MiB threshold or load the whole file.
    fs::resize_file(fixture.source, 40ull * 1024 * 1024);
    lume::wallpaper::Library library(fixture.options.wallpaperLibraryRoot);
    Check(library.Load().ok && library.ImportLocal(fixture.source.u8string()).ok, "import resizable PNG");
    auto session = fixture.Open();
    Check(session.Restore(), "resize thumbnail restore");
    const auto catalog = Execute(session, Catalog(1));
    const auto png = Get(catalog["items"][0]["thumbnailUrl"].get<std::string>()).body;
    Check(png.size() > 24 && static_cast<unsigned char>(png[19]) == 240 &&
        png[16] == 0 && png[17] == 0 && png[18] == 0 &&
        static_cast<unsigned char>(png[23]) == 180 &&
        png[20] == 0 && png[21] == 0 && png[22] == 0,
        "640x480 image is really downsampled to 240x180 while retaining aspect ratio");
}

std::vector<unsigned char> DecodePreview(const std::string& png, UINT width, UINT height) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Check(SUCCEEDED(initialized), "preview decode COM");
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    struct Cleanup {
        IWICImagingFactory*& factory; IWICStream*& stream; IWICBitmapDecoder*& decoder;
        IWICBitmapFrameDecode*& frame; IWICFormatConverter*& converter;
        ~Cleanup() {
            if (converter) converter->Release();
            if (frame) frame->Release();
            if (decoder) decoder->Release();
            if (stream) stream->Release();
            if (factory) factory->Release();
            CoUninitialize();
        }
    } cleanup{factory, stream, decoder, frame, converter};
    UINT actualWidth = 0, actualHeight = 0;
    std::vector<unsigned char> pixels(width * height * 4);
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(png.data())),
            static_cast<DWORD>(png.size()))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&actualWidth, &actualHeight)) &&
        actualWidth == width && actualHeight == height &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
            nullptr, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())),
        "thumbnail decodes to expected real pixels");
    return pixels;
}

void VideoFrameOrientation() {
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "frame conversion COM");
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    using lume::shell::background_preview::VideoFrameThumbnail;
    // Two visible rows plus one alignment row, with four padding bytes per row.
    const std::vector<unsigned char> topDown{
        0,0,255,0, 0,0,255,0, 9,9,9,9,
        255,0,0,0, 255,0,0,0, 9,9,9,9,
        0,255,0,0, 0,255,0,0, 9,9,9,9};
    for (const LONG stride : {12L, -12L}) {
        auto source = topDown;
        if (stride < 0)
            for (unsigned i = 0; i < 12; ++i) std::swap(source[i], source[24 + i]);
        const auto png = VideoFrameThumbnail(2, 3, 2, 2, stride, source.data(),
            static_cast<DWORD>(source.size()));
        Check(bool(png), "positive and negative padded strides accepted");
        const auto pixels = DecodePreview(std::string(png->begin(), png->end()), 2, 2);
        for (unsigned x = 0; x < 2; ++x) {
            Check(pixels[x * 4] == 0 && pixels[x * 4 + 1] == 0 &&
                pixels[x * 4 + 2] == 255 && pixels[x * 4 + 3] == 255, "decoded upper row is opaque red");
            Check(pixels[8 + x * 4] == 255 && pixels[9 + x * 4] == 0 &&
                pixels[10 + x * 4] == 0 && pixels[11 + x * 4] == 255, "decoded lower row is opaque blue");
        }
        Check(!VideoFrameThumbnail(2, 3, 2, 2, stride, source.data(), 35),
            "truncated positive or negative stride buffer rejected");
    }
}

std::string PreviewPng() {
    const unsigned char bytes[] = {
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,
        8,4,0,0,0,181,28,12,2,0,0,0,11,73,68,65,84,120,218,99,100,248,15,
        0,1,5,1,1,39,24,227,102,0,0,0,0,73,69,78,68,174,66,96,130};
    return {reinterpret_cast<const char*>(bytes), sizeof(bytes)};
}

std::string PreviewMp4() {
    // Two generated red 320x180 H.264 frames, no external files or decoder tools needed.
    const std::string base64 =
        "AAAAIGZ0eXBpc29tAAACAGlzb21pc28yYXZjMW1wNDEAAAMubW9vdgAAAGxtdmhkAAAAAAAAAAAAAAAA"
        "AAAD6AAAA+gAAQAAAQAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAABAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgAAAlh0cmFrAAAAXHRraGQAAAADAAAAAAAAAAAAAAAB"
        "AAAAAAAAA+gAAAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAABAAAAA"
        "AUAAAAC0AAAAAAAkZWR0cwAAABxlbHN0AAAAAAAAAAEAAAPoAAAAAAABAAAAAAHQbWRpYQAAACBtZGhk"
        "AAAAAAAAAAAAAAAAAABAAAAAQABVxAAAAAAALWhkbHIAAAAAAAAAAHZpZGUAAAAAAAAAAAAAAABWaWRl"
        "b0hhbmRsZXIAAAABe21pbmYAAAAUdm1oZAAAAAEAAAAAAAAAAAAAACRkaW5mAAAAHGRyZWYAAAAAAAAA"
        "AQAAAAx1cmwgAAAAAQAAATtzdGJsAAAAu3N0c2QAAAAAAAAAAQAAAKthdmMxAAAAAAAAAAEAAAAAAAAA"
        "AAAAAAAAAAAAAUAAtABIAAAASAAAAAAAAAABFUxhdmM2Mi4yOC4xMDEgbGlieDI2NAAAAAAAAAAAAAAA"
        "GP//AAAAMWF2Y0MBQsAL/+EAGWdCwAvZAUGfnwEQAAADABAAAAMAQPFCpIABAAVoy4PLIAAAABBwYXNw"
        "AAAAAQAAAAEAAAAUYnRydAAAAAAAAAbYAAAAAAAAABhzdHRzAAAAAAAAAAEAAAACAAAgAAAAABRzdHNz"
        "AAAAAAAAAAEAAAABAAAAHHN0c2MAAAAAAAAAAQAAAAEAAAACAAAAAQAAABxzdHN6AAAAAAAAAAAAAAAC"
        "AAAA0AAAAAsAAAAUc3RjbwAAAAAAAAABAAADXgAAAGJ1ZHRhAAAAWm1ldGEAAAAAAAAAIWhkbHIAAAAA"
        "AAAAAG1kaXJhcHBsAAAAAAAAAAAAAAAALWlsc3QAAAAlqXRvbwAAAB1kYXRhAAAAAQAAAABMYXZmNjIu"
        "MTIuMTAxAAAACGZyZWUAAADjbWRhdAAAAMxliIQFPEYoAAtKxwABNJjgADQFJycnJycnJycnJycnJycn"
        "JycnJ111111111111111111111111111111111111111111111111111111111111111111111111111"
        "11111111111111111111111111111111111111111111111111111111111111111111111111111111"
        "111111111111111111111111111111111111111111111111111111111111111114AAAAAHQZo4CXgP"
        "GA==";
    DWORD size = 0;
    Check(CryptStringToBinaryA(base64.c_str(), static_cast<DWORD>(base64.size()), CRYPT_STRING_BASE64,
        nullptr, &size, nullptr, nullptr), "size embedded MP4 fixture");
    std::string bytes(size, '\0');
    Check(CryptStringToBinaryA(base64.c_str(), static_cast<DWORD>(base64.size()), CRYPT_STRING_BASE64,
        reinterpret_cast<BYTE*>(bytes.data()), &size, nullptr, nullptr), "decode embedded MP4 fixture");
    bytes.resize(size);
    return bytes;
}

void ProtectedAndVideoThumbnails() {
    Fixture fixture;
    const auto png = PreviewPng(), mp4 = PreviewMp4();
    Write(fixture.source, png);
    const auto importedVideo = fixture.root / "preview.mp4";
    Write(importedVideo, mp4);
    lume::wallpaper::Library library(fixture.options.wallpaperLibraryRoot);
    Check(library.Load().ok, "preview library");
    const auto imported = library.ImportLocal(importedVideo.u8string());
    Check(imported.ok, "import real MP4");
    const auto imagePath = ProtectedFixture(fixture, "sealed-image", png, true, false);
    const auto videoPath = ProtectedFixture(fixture, "sealed-video", mp4, false, false);
    const auto posterVideoPath = ProtectedFixture(fixture, "poster-video", "protected-video-with-sidecar");
    const auto root = fs::u8path(fixture.options.wallpaperLibraryRoot);
    const auto poster = root / "posters" / "poster-video-poster.png";
    Write(poster, png);
    const auto manifest = Read(root / "library.json"), key = Read(root / "cache-key.bin");
    const auto sealedImage = Read(fs::u8path(imagePath)), sealedVideo = Read(fs::u8path(videoPath));
    const auto sealedPosterVideo = Read(fs::u8path(posterVideoPath));
    auto session = fixture.Open();
    Check(session.Restore(), "protected preview session");
    const auto catalog = Execute(session, Catalog(1));
    Check(catalog["items"].size() == 4 && catalog["total"] == 4,
        "catalog contains imported video, protected image/video and historical poster fixtures");
    for (const auto& item : catalog["items"]) {
        const auto id = item["id"].get<std::string>();
        const auto url = item["thumbnailUrl"].get<std::string>();
        Check(url.rfind("http://127.0.0.1:", 0) == 0, ("missing native thumbnail: " + id).c_str());
        const auto reply = Get(url);
        Check(reply.status == 200 && reply.body.size() <= 256 * 1024 &&
            reply.body.substr(0, 8) == png.substr(0, 8), "protected/image/video previews are real bounded PNGs");
        if (id == imported.id || id == "sealed-video") {
            Check(reply.body.size() > 24 && static_cast<unsigned char>(reply.body[18]) == 1 &&
                static_cast<unsigned char>(reply.body[19]) == 64 &&
                static_cast<unsigned char>(reply.body[23]) == 180, "MP4 first frame crops codec alignment padding");
            const auto pixels = DecodePreview(reply.body, 320, 180);
            for (std::size_t i = 0; i < pixels.size(); i += 4)
                Check(pixels[i] < 20 && pixels[i + 1] < 20 && pixels[i + 2] > 230 && pixels[i + 3] == 255,
                    "plain/protected MP4 thumbnail decodes to opaque red pixels");
        } else {
            Check(DecodePreview(reply.body, 1, 1) == std::vector<unsigned char>({0, 0, 0, 255}),
                "protected image and historical poster decode to opaque black pixels");
        }
    }
    Check(Read(root / "library.json") == manifest && Read(root / "cache-key.bin") == key &&
        Read(fs::u8path(imagePath)) == sealedImage && Read(fs::u8path(videoPath)) == sealedVideo &&
        Read(fs::u8path(posterVideoPath)) == sealedPosterVideo && Read(poster) == png,
        "native catalog previews never modify protected sources, key, manifest or poster");
    Check(!fs::exists(fs::u8path(fixture.options.statePath)) &&
        !fs::exists(root / "preview.mp4"), "catalog preview does not materialize media or preferences");
    const auto alias = fixture.root / "poster-link.png";
    Check(CreateHardLinkW(alias.c_str(), poster.c_str(), nullptr), "poster hardlink fixture");
    auto heroManifest = Json::parse(manifest);
    for (auto& record : heroManifest["items"]) {
        if (record["item"]["id"] != "poster-video") continue;
        record["item"]["heroThumbnailUrl"] = record["item"]["thumbnailUrl"];
        record["item"]["thumbnailUrl"] = "";
    }
    Write(root / "library.json", heroManifest.dump());
    const auto unsafe = Execute(session, Catalog(2));
    for (const auto& item : unsafe["items"])
        if (item["id"] == "poster-video")
            Check(item["thumbnailUrl"] == "https://static.lumedesktop.com/lume/wallpapers/official/fixture.jpg",
                "hardlinked poster rejected, validated hero-only manifest fallback retained");
    Check(DeleteFileW(alias.c_str()), "remove poster hardlink fixture");
    auto corrupted = sealedImage;
    corrupted.back() ^= 1;
    Write(fs::u8path(imagePath), corrupted);
    const auto damaged = Execute(session, Catalog(3));
    for (const auto& item : damaged["items"])
        if (item["id"] == "sealed-image") Check(item["thumbnailUrl"] == "",
            "unauthenticated protected image never becomes a thumbnail");
}

void LegacyMigration(const fs::path& inputRoot) {
    const auto original = Read(inputRoot / "sealed.apk");
    const auto unsealed = Read(inputRoot / "unsealed.apk");
    Check(original.substr(0, 8) == "LUMEWP01" && unsealed.substr(0, 8) == "LUMEWP01",
        "historical implementation supplied actual legacy ciphertext");
    Fixture fixture;
    std::string plain(300017, '\0');
    for (std::size_t i = 0; i < plain.size(); ++i) plain[i] = static_cast<char>('A' + i % 23);
    const auto source = fs::u8path(ProtectedFixture(fixture, "legacy-fixture", plain));
    const auto libraryRoot = fs::u8path(fixture.options.wallpaperLibraryRoot);
    const auto keyPath = libraryRoot / "cache-key.bin";
    std::array<BYTE, 32> key{};
    for (unsigned i = 0; i < key.size(); ++i) key[i] = static_cast<BYTE>(i + 1);
    DATA_BLOB input{static_cast<DWORD>(key.size()), key.data()}, output{};
    Check(CryptProtectData(&input, L"Isolated historical test key", nullptr, nullptr, nullptr,
        CRYPTPROTECT_UI_FORBIDDEN, &output), "protect synthetic legacy key through historical DPAPI pathway");
    Write(keyPath, {reinterpret_cast<const char*>(output.pbData), output.cbData});
    SecureZeroMemory(output.pbData, output.cbData); LocalFree(output.pbData);
    SecureZeroMemory(key.data(), key.size());
    Write(source, original);
    const auto originalKey = Read(keyPath);
    const auto manifest = Read(libraryRoot / "library.json");
    auto session = fixture.Open();
    Check(session.Restore(), "legacy migration restore");
    const auto state = Execute(session, Select("legacy-fixture", 1, 0));
    Check(state["status"] == "ready" && state["revision"] == 1, "authenticated v1 selected through catalog");
    const auto url = state["media"]["url"].get<std::string>();
    Check(Get(url).body == plain, "legacy unequal records re-encrypt without changing plaintext");
    const auto musicRoot = fs::u8path(fixture.options.root) / "protected";
    const auto owned = musicRoot / "media" / (state["media"]["id"].get<std::string>() + ".apk");
    Check(Read(owned).substr(0, 8) == "LUMEWP02" &&
        ReadPlain(musicRoot.u8string(), owned.u8string()) == plain, "music asset uses authenticated native v2");
    Check(UnwrapFixtureKey(musicRoot / "cache-key.bin") != UnwrapFixtureKey(keyPath),
        "music migration never adopts historical encryption key");
    Check(Read(source) == original && Read(keyPath) == originalKey &&
        Read(libraryRoot / "library.json") == manifest &&
        fs::is_empty(fs::u8path(fixture.options.root) / "media"),
        "migration preserves source and creates no plaintext media");
    const auto persisted = Read(fs::u8path(fixture.options.statePath));
    unsigned request = 2;
    auto reject = [&](const std::string& ciphertext) {
        Write(source, ciphertext);
        Check(Execute(session, Select("legacy-fixture", request++, 1))["status"] == "error",
            "damaged or unsealed historical input fails closed");
        Check(Read(fs::u8path(fixture.options.statePath)) == persisted && Get(url).body == plain &&
            std::distance(fs::directory_iterator(musicRoot / "media"), fs::directory_iterator{}) == 1,
            "legacy failure preserves old state and removes staging ciphertext");
    };
    reject(unsealed);
    auto changed = original; changed[96] ^= 1; reject(changed);
    changed = original; changed.back() ^= 1; reject(changed);
    changed = original; changed[112] ^= 1; reject(changed);
    reject(original.substr(0, original.size() - 1));
    reject(original + "trailing");
    Write(keyPath, Read(musicRoot / "cache-key.bin"));
    reject(original);
    Write(keyPath, originalKey);
    Check(DeleteFileW(keyPath.c_str()), "remove only synthetic legacy key");
    reject(original);
    Check(!fs::exists(keyPath), "legacy missing key is never regenerated");
    Write(keyPath, originalKey);
    const auto alias = fixture.root / "legacy-key-alias.bin";
    Check(CreateHardLinkW(alias.c_str(), keyPath.c_str(), nullptr), "legacy key hardlink fixture");
    reject(original);
    Check(DeleteFileW(alias.c_str()), "remove isolated legacy key alias");
    Write(source, original);
    const auto wakes = fixture.wakes.load();
    session.HandlePage(Select("legacy-fixture", request++, 1));
    const auto until = GetTickCount64() + 5000;
    while (fixture.wakes.load() == wakes && GetTickCount64() < until) Sleep(2);
    Check(fixture.wakes.load() > wakes, "legacy worker completed before cancelled commit");
    session.CancelActive(); Wait(session);
    Check(Read(fs::u8path(fixture.options.statePath)) == persisted &&
        std::distance(fs::directory_iterator(musicRoot / "media"), fs::directory_iterator{}) == 1,
        "cancelled migration discards re-encrypted candidate");
    session.Shutdown();
    Check(DeleteFileW(source.c_str()) && DeleteFileW(keyPath.c_str()),
        "remove isolated historical source before restart");
    auto reopened = fixture.Open();
    Check(reopened.Restore() && Get(State(reopened.State())["media"]["url"].get<std::string>()).body == plain,
        "migrated background restores independently of historical source");
}

void ProtocolAndMigration() {
    const auto parse = [](const Json& json) {
        const auto text = json.dump();
        return lume::ui::ParseMusicBackgroundPageJson(std::wstring(text.begin(), text.end()));
    };
    Json catalog{{"version", 1}, {"scope", "music-background"}, {"action", "catalog"},
        {"request", 1}, {"offset", 0}, {"limit", 120}};
    Check(bool(parse(catalog)), "catalog exact contract");
    for (const auto invalid : {0, 121, -1}) {
        auto bad = catalog; bad["limit"] = invalid;
        Check(!parse(bad), "catalog rejects invalid page limit");
    }
    auto bad = catalog; bad["path"] = "C:/arbitrary";
    Check(!parse(bad), "catalog rejects extra path");
    bad = catalog; bad["offset"] = 20001;
    Check(!parse(bad), "catalog rejects out-of-bound offset");
    Json select{{"version", 1}, {"scope", "music-background"}, {"action", "select"},
        {"request", 2}, {"revision", 0}, {"id", "Mixed.Case_ID"}};
    Check(bool(parse(select)), "source IDs use wallpaper policy, not music asset policy");
    for (const auto* id : {"../escape", "CON.png", "a/b", "", "https://evil.invalid"}) {
        bad = select; bad["id"] = id;
        Check(!parse(bad), "select rejects unsafe identity");
    }
    Check(!lume::ui::ParseMusicBackgroundPageJson(
        L"{\"version\":1,\"scope\":\"music-background\",\"action\":\"snapshot\",\"action\":\"snapshot\"}"),
        "duplicate JSON keys rejected");

    Fixture fixture;
    fs::create_directories(fs::u8path(fixture.options.root));
    Media media{"background-0123456789abcdef", "Original", "png", lume::music::MusicBackgroundKind::Image, "image/png"};
    const Json legacy{{"version", 1}, {"revision", 7}, {"media", lume::music::EncodeMusicBackgroundMedia(media)}};
    Write(fs::u8path(fixture.options.statePath), legacy.dump());
    Store store(fs::u8path(fixture.options.statePath), fixture.files);
    Check(store.Load() == Result::Ready && store.storage() == Storage::Plain &&
        store.sourceWallpaperId().empty() && store.revision() == 7, "v1 interpreted as plain without writing");
    Check(Json::parse(Read(fs::u8path(fixture.options.statePath))) == legacy, "load leaves legacy bytes unchanged");
    Check(store.SaveAsset(media, Storage::Protected, "official-fixture", 7) == Result::Ready, "v2 commit");
    const auto v2 = Json::parse(Read(fs::u8path(fixture.options.statePath)));
    Check(v2["version"] == 2 && v2["storage"] == "protected" && v2["sourceWallpaperId"] == "official-fixture" &&
        !v2.contains("url") && !v2.contains("key"), "v2 stores identity and storage, never runtime capabilities");
    Check(Json::parse(Read(fs::u8path(fixture.options.statePath + ".bak"))) == legacy, "v1 backup retained");
    Store reopened(fs::u8path(fixture.options.statePath), fixture.files);
    Check(reopened.Load() == Result::Ready && reopened.storage() == Storage::Protected &&
        reopened.previousStorage() == Storage::Plain && reopened.previous() == media, "v2 and v1 backup restore");
    Write(fs::u8path(fixture.options.statePath), "{broken");
    Store recovered(fs::u8path(fixture.options.statePath), fixture.files);
    Check(recovered.Load() == Result::Ready && recovered.revision() == 7 && recovered.storage() == Storage::Plain,
        "corrupt primary restores v1 backup");
    Write(fs::u8path(fixture.options.statePath), R"({"version":3})");
    Check(recovered.Load() == Result::Load, "unknown future version never downgraded through backup");
}

void GalleryPlainAndPagination() {
    Fixture fixture;
    lume::wallpaper::Library library(fixture.options.wallpaperLibraryRoot);
    Check(library.Load().ok, "empty native library");
    const auto item = library.ImportLocal(fixture.source.u8string());
    Check(item.ok, "plain catalog fixture");
    const auto manifest = Read(fs::u8path(fixture.options.wallpaperLibraryRoot) / "library.json");
    auto session = fixture.Open();
    Check(session.Restore(), "gallery restore");
    auto catalog = Execute(session, Catalog(1));
    Check(catalog["type"] == "music-background-catalog" && catalog["total"] == 1 &&
        catalog["items"][0]["id"] == item.id && catalog["selectedId"] == "", "native local gallery DTO");
    Check(catalog["items"][0].size() == 4 && catalog.dump().find("localPath") == std::string::npos,
        "gallery exposes no native paths");
    auto selected = Execute(session, Select(item.id, 2, 0));
    Check(selected["status"] == "ready" && selected["media"]["id"] != item.id &&
        selected["media"]["url"].get<std::string>().rfind("http://127.0.0.1:", 0) == 0,
        "plain selection creates independent music identity and URL");
    const auto asset = fs::u8path(fixture.options.root) / "media" /
        (selected["media"]["id"].get<std::string>() + ".png");
    Check(Read(asset) == Read(fixture.source), "plain import copied into music-owned media");
    Check(Read(fs::u8path(fixture.options.wallpaperLibraryRoot) / "library.json") == manifest, "music never writes source catalog");
    catalog = Execute(session, Catalog(3));
    Check(catalog["selectedId"] == item.id, "selected catalog identity is separate from asset ID");
    Check(library.Remove(item.id).ok && fs::exists(asset), "source deletion leaves music copy intact");
    Check(Execute(session, Select(item.id, 4, 1))["error"] == "invalid", "stale catalog selection rechecks source");
    Check(Execute(session, Catalog(5))["total"] == 0, "fresh listing omits removed files");
    Check(State(session.State())["media"]["id"] == selected["media"]["id"], "failed selection preserves old background");

    // Build a bounded fixture manifest directly; avoid 122 redundant import transactions.
    Json rows = Json::array();
    for (int i = 0; i < 122; ++i) {
        const auto id = "custom-page-" + std::to_string(i);
        Write(fs::u8path(fixture.options.wallpaperLibraryRoot) / "media" / (id + ".png"), "fixture");
        rows.push_back({{"file", id + ".png"}, {"imported", true}, {"protectedMedia", false},
            {"item", {{"id", id}, {"title", id}, {"kind", "image"}, {"sizeBytes", 7}}}});
    }
    Write(fs::u8path(fixture.options.wallpaperLibraryRoot) / "library.json",
        Json{{"version", 1}, {"items", rows}}.dump());
    catalog = Execute(session, Catalog(6));
    Check(catalog["total"] == 122 && catalog["items"].size() == 120, "gallery bounded page");
    catalog = Execute(session, Catalog(7, 120, 120));
    Check(catalog["total"] == 122 && catalog["items"].size() == 2, "gallery next page");
    Check(Execute(session, Catalog(8, 0, 121))["error"] == "invalid", "direct native invalid limit rejected");
    session.SetVisible(false);
    Check(State(session.HandlePage(Catalog(9)))["type"] == "music-background-catalog",
        "hidden catalog preserves reply envelope");
    session.Shutdown();
    Check(State(session.HandlePage(Catalog(10)))["error"] == "load",
        "stopped catalog acknowledges unavailable state");
}

void ProtectedIsolationAndLifetime() {
    Fixture fixture;
    const std::string plain = std::string("MUSIC-PROTECTED-FIXTURE-") + std::string(150000, 'V');
    const auto original = ProtectedFixture(fixture, "official-fixture", plain);
    auto session = fixture.Open();
    Check(session.Restore(), "protected gallery restore");
    const auto catalog = Execute(session, Catalog(1));
    Check(catalog["total"] == 1 && catalog["items"][0]["thumbnailUrl"] ==
        "https://static.lumedesktop.com/lume/wallpapers/official/fixture.jpg", "official cover remains catalog metadata");
    const auto selected = Execute(session, Select("official-fixture", 2, 0));
    Check(selected["status"] == "ready" && selected["media"]["kind"] == "video", "protected selection committed");
    const auto id = selected["media"]["id"].get<std::string>();
    const auto url = selected["media"]["url"].get<std::string>();
    const auto targetRoot = fs::u8path(fixture.options.root) / "protected";
    const auto target = targetRoot / "media" / (id + ".apk");
    Check(ReadPlain(targetRoot.u8string(), target.u8string()) == plain, "re-encryption preserves authenticated bytes");
    Check(Read(target).substr(0, 8) == "LUMEWP02" && Read(target) != Read(fs::u8path(original)) &&
        Read(target).find(plain.substr(0, 64)) == std::string::npos, "music copy is new ciphertext, never plaintext");
    auto sourceKey = UnwrapFixtureKey(fs::u8path(fixture.options.wallpaperLibraryRoot) / "cache-key.bin");
    auto musicKey = UnwrapFixtureKey(targetRoot / "cache-key.bin");
    Check(sourceKey != musicKey, "music master key is cryptographically independent of wallpaper key");
    SecureZeroMemory(sourceKey.data(), sourceKey.size()); SecureZeroMemory(musicKey.data(), musicKey.size());
    const auto persisted = Read(fs::u8path(fixture.options.statePath));
    Check(persisted.find("127.0.0.1") == std::string::npos && persisted.find(url) == std::string::npos,
        "server capabilities never persisted");
    const auto response = Get(url);
    Check(response.status == 200 && response.body == plain && response.cors == "https://lume.local",
        "music server reads encrypted asset with exact CORS");
    const auto ranged = Get(url, "bytes=65530-65550");
    Check(ranged.status == 206 && ranged.body == plain.substr(65530, 21), "protected range crosses authenticated chunks");
    Check(fs::is_empty(fs::u8path(fixture.options.root) / "media"), "no decrypted files in WebView mapped directory");
    Check(fs::remove(fs::u8path(original)), "source reader released after import");
    Check(fs::remove(fs::u8path(fixture.options.wallpaperLibraryRoot) / "cache-key.bin"), "source key can disappear independently");
    session.SetVisible(false);
    Check(Get(url).status != 200 && State(session.State())["media"].is_null(), "hiding revokes runtime capability");
    session.SetVisible(true);
    const auto resumed = State(session.State())["media"]["url"].get<std::string>();
    Check(resumed != url && Get(resumed).body == plain, "showing reissues capability from music copy only");
    session.Shutdown();
    Check(Get(resumed).status != 200 && !session.HasWork(), "shutdown releases music server");
    auto reopened = fixture.Open();
    Check(reopened.Restore(), "protected record restart");
    const auto restored = State(reopened.State());
    Check(restored["media"]["id"] == id && restored["media"]["url"] != resumed &&
        Get(restored["media"]["url"].get<std::string>()).body == plain, "restart independent from source library");
}

void CancellationAndFailure() {
    Fixture fixture;
    ProtectedFixture(fixture, "official-fixture", std::string(150000, 'P'));
    auto session = fixture.Open();
    Check(session.Restore(), "cancellation restore");
    auto pending = State(session.HandlePage(Select("official-fixture", 1, 0)));
    Check(pending["status"] == "busy", "selection acknowledges asynchronously");
    const auto until = GetTickCount64() + 5000;
    while (!fixture.wakes && GetTickCount64() < until) Sleep(2);
    Check(fixture.wakes > 0, "worker only wakes main");
    Check(!fs::exists(fs::u8path(fixture.options.statePath)), "worker cannot persist before main-thread drain");
    session.CancelActive();
    Wait(session);
    Check(!fs::exists(fs::u8path(fixture.options.statePath)) &&
        fs::is_empty(fs::u8path(fixture.options.root) / "protected" / "media"),
        "cancelled completed import is discarded without commit");
    fixture.selected = fixture.source.wstring();
    const auto first = Execute(session, Command(MusicBackgroundAction::Choose, 2, 0));
    Check(first["status"] == "ready", "can retry after cancellation");
    auto selected = Select("official-fixture", 3, 1);
    session.HandlePage(selected);
    session.SetVisible(false);
    Wait(session);
    session.SetVisible(true);
    Check(State(session.State())["media"]["id"] == first["media"]["id"], "hidden worker never replaces prior media");

    // Header valid but data authentication fails during streaming re-encryption.
    auto file = fs::u8path(fixture.options.wallpaperLibraryRoot) / "media" / "official-fixture.apk";
    auto bytes = Read(file); bytes[100] ^= 1; Write(file, bytes);
    Check(Execute(session, Select("official-fixture", 4, 1))["status"] == "error",
        "corrupt ciphertext rejected before credential/state commit");
    Check(fs::is_empty(fs::u8path(fixture.options.root) / "protected" / "media"), "failed conversion leaves no staging ciphertext");
    Check(State(session.State())["media"]["id"] == first["media"]["id"], "failed protected selection leaves old state intact");

    // A magic prefix is not sufficient authentication for historical media.
    Write(file, "LUMEWP01" + std::string(200, 'L'));
    Check(Execute(session, Select("official-fixture", 5, 1))["status"] == "error",
        "malformed legacy encrypted format fails closed");
    session.HandlePage(Command(MusicBackgroundAction::Choose, 6, 1));
    session.Shutdown();
    Check(!session.HasWork(), "shutdown joins copy worker");
    Check(Json::parse(Read(fs::u8path(fixture.options.statePath)))["revision"] == 1,
        "shutdown cannot commit pending selection");
}

void BackupAssetsAndWrongKey() {
    Fixture fixture;
    ProtectedFixture(fixture, "official-fixture", std::string(100, 'Q'));
    auto session = fixture.Open();
    Check(session.Restore(), "backup asset restore");
    const auto protectedState = Execute(session, Select("official-fixture", 1, 0));
    const auto id = protectedState["media"]["id"].get<std::string>();
    const auto url = protectedState["media"]["url"].get<std::string>();
    fixture.selected = fixture.source.wstring();
    Check(Execute(session, Command(MusicBackgroundAction::Choose, 2, 1))["status"] == "ready", "replacement after protected");
    Check(Get(url).status == 200, "outgoing protected lease survives replacement for compositor");
    session.Shutdown();
    Write(fs::u8path(fixture.options.statePath), "{corrupt");
    auto recovered = fixture.Open();
    Check(recovered.Restore() && State(recovered.State())["media"]["id"] == id,
        "backup restores asset bytes as well as metadata");
    recovered.Shutdown();
    const auto musicRoot = fs::u8path(fixture.options.root) / "protected";
    Write(musicRoot / "cache-key.bin", Read(fs::u8path(fixture.options.wallpaperLibraryRoot) / "cache-key.bin"));
    auto wrongKey = fixture.Open();
    Check(wrongKey.Restore() && State(wrongKey.State())["media"].is_null() &&
        State(wrongKey.State())["error"] == "load", "wrong music key never issues a playable URL");
    Check(fs::exists(musicRoot / "media" / (id + ".apk")), "failed key validation never destroys recoverable assets");
}

void FailedCommitAndBoundedLease() {
    Fixture fixture;
    ProtectedFixture(fixture, "official-fixture", std::string(100, 'R'));
    FailingStore files;
    MusicBackgroundSession session(fixture.options, files);
    Check(session.Restore(), "failure injection restore");
    const auto first = Execute(session, Select("official-fixture", 1, 0));
    const auto firstUrl = first["media"]["url"].get<std::string>();
    const auto firstPath = fs::u8path(fixture.options.root) / "protected" / "media" /
        (first["media"]["id"].get<std::string>() + ".apk");
    const auto persisted = Read(fs::u8path(fixture.options.statePath));
    files.failRenameTo = fixture.options.statePath;
    Check(Execute(session, Select("official-fixture", 2, 1))["error"] == "save",
        "primary replacement failure is reported");
    Check(Read(fs::u8path(fixture.options.statePath)) == persisted &&
        Get(firstUrl).status == 200, "failed commit preserves published credential-free state and live lease");
    Check(std::distance(fs::directory_iterator(firstPath.parent_path()), fs::directory_iterator{}) == 1,
        "failed commit discards newly encrypted asset");
    files.failRenameTo.clear();
    const auto second = Execute(session, Select("official-fixture", 3, 1));
    const auto secondUrl = second["media"]["url"].get<std::string>();
    fixture.selected = fixture.source.wstring();
    const auto third = Execute(session, Command(MusicBackgroundAction::Choose, 4, 2));
    Check(third["status"] == "ready", "third selection commits");
    const auto thirdUrl = third["media"]["url"].get<std::string>();
    Check(Get(firstUrl).status != 200 && !fs::exists(firstPath) && Get(secondUrl).status == 200,
        "only one outgoing lease retained and older unreferenced ciphertext pruned");
    Check(State(session.HandlePage(Command(MusicBackgroundAction::Clear, 5, 3)))["status"] == "ready" &&
        Get(secondUrl).status != 200 && Get(thirdUrl).status == 200,
        "clearing retires plain media through the same bounded outgoing lease");

    // A real deadline checks the production wake path without adding a test-only clock API.
    const auto wakes = fixture.wakes.load();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(33);
    while (fixture.wakes.load() == wakes && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Check(fixture.wakes.load() > wakes, "outgoing expiry wakes main without another page command");
    Check(!session.DrainNotification(), "lease maintenance does not send a spurious operation reply");
    Check(Get(thirdUrl).status != 200, "expired plain outgoing capability revoked");
    const auto backup = fs::u8path(fixture.options.root) / "media" /
        (third["media"]["id"].get<std::string>() + ".png");
    Check(fs::exists(backup), "expired lease does not erase persisted backup asset");
}

void ModalChooserLifecycle() {
    for (const auto mode : {0, 1, 2, 3}) {
        Fixture fixture;
        MusicBackgroundSession* active = nullptr;
        unsigned calls = 0;
        bool modalGuardChecked = false;
        fixture.options.chooseMedia = [&](HWND) -> std::wstring {
            if (++calls > 1) return {};
            Check(active->HasWork() && State(active->State())["status"] == "busy",
                "modal chooser reserves operation before pumping messages");
            Check(State(active->HandlePage(Command(MusicBackgroundAction::Choose, 2, 0)))["status"] == "busy" &&
                State(active->HandlePage(Command(MusicBackgroundAction::Clear, 3, 0)))["status"] == "busy" &&
                State(active->HandlePage(Select("official-fixture", 4, 0)))["status"] == "busy" &&
                State(active->HandlePage(Catalog(5)))["status"] == "busy",
                "nested modal requests cannot enqueue work or mutate state");
            Check(!active->DrainNotification(), "modal drain cannot finish the picker");
            modalGuardChecked = true;
            if (mode == 0) active->CancelActive();
            if (mode == 1) { active->SetVisible(false); active->SetVisible(true); }
            if (mode == 2) active->Shutdown();
            if (mode == 3) throw std::runtime_error("fixture picker failure");
            return fixture.source.wstring();
        };
        auto session = fixture.Open();
        active = &session;
        Check(session.Restore(), "modal lifecycle restore");
        const auto reply = State(session.HandlePage(Command(MusicBackgroundAction::Choose, 1, 0)));
        Check(modalGuardChecked && calls == 1 && reply["request"] == 1 && reply["revision"] == 0 &&
            reply["media"].is_null() && reply["status"] != "busy" && !session.HasWork(),
            "cancelled, hidden, stopped or failed picker cannot queue stale selection");
        Check(!fs::exists(fs::u8path(fixture.options.statePath)) &&
            fs::is_empty(fs::u8path(fixture.options.root) / "media") && !session.DrainNotification(),
            "invalidated picker leaves no files, commit or late notification");
        if (mode != 2) {
            Check(State(session.HandlePage(Command(MusicBackgroundAction::Clear, 6, 0)))["revision"] == 1,
                "modal invalidation releases busy state");
        }
    }
}

unsigned ProcessThreads() {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    Check(snapshot != INVALID_HANDLE_VALUE, "test thread snapshot");
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    unsigned count = 0;
    if (Thread32First(snapshot, &entry)) do {
        if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++count;
    } while (Thread32Next(snapshot, &entry));
    CloseHandle(snapshot);
    Check(count != 0, "test process threads enumerated");
    return count;
}

void InitiallyHiddenRestore() {
    Fixture fixture;
    fixture.options.initiallyVisible = false;
    const auto musicRoot = fs::u8path(fixture.options.root);
    const auto protectedRoot = musicRoot / "protected";
    const Media media{"background-0123456789abcdef", "Hidden fixture", "mp4",
        lume::music::MusicBackgroundKind::Video, "video/mp4"};
    const auto path = protectedRoot / "media" / (media.id + ".apk");
    fs::create_directories(path.parent_path());
    std::string error;
    auto writer = lume::platform::windows::wallpaper::MakeProtectedWriterFactory(protectedRoot.u8string())(
        path.u8string(), error);
    const std::string plain = "isolated-hidden-media";
    Check(writer && writer->Append(reinterpret_cast<const std::uint8_t*>(plain.data()), plain.size(), error) &&
        writer->Finish(error), "seed isolated hidden protected asset");
    writer.reset();
    Store store(fs::u8path(fixture.options.statePath), fixture.files);
    Check(store.Load() == Result::Ready && store.SaveAsset(media, Storage::Protected, "official-fixture", 0) ==
        Result::Ready, "seed hidden metadata");
    const auto persisted = Read(fs::u8path(fixture.options.statePath));
    const auto before = ProcessThreads();
    auto session = fixture.Open();
    Check(session.Restore() && session.Restore(), "hidden restore is repeatable");
    Check(State(session.State())["media"].is_null() && State(session.State())["revision"] == 1 &&
        !session.HasWork() && fixture.wakes == 0 && ProcessThreads() == before,
        "hidden restore starts no worker, server or public lease");
    const auto exclusive = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(exclusive != INVALID_HANDLE_VALUE, "hidden restore retains no protected reader");
    CloseHandle(exclusive);
    session.HandlePage(Command(MusicBackgroundAction::Choose, 1, 1));
    session.HandlePage(Catalog(2));
    Check(!session.HasWork() && !session.DrainNotification() && ProcessThreads() == before &&
        Read(fs::u8path(fixture.options.statePath)) == persisted, "hidden requests cannot start work");
    session.SetVisible(true);
    const auto shown = State(session.State());
    Check(shown["media"]["id"] == media.id && Get(shown["media"]["url"].get<std::string>()).body == plain,
        "first visibility activates saved music asset");
    const auto url = shown["media"]["url"].get<std::string>();
    session.SetVisible(true);
    Check(State(session.State())["media"]["url"] == url, "repeat visibility does not replace runtime");
    fixture.selected = fixture.source.wstring();
    Check(Execute(session, Command(MusicBackgroundAction::Choose, 3, 1))["revision"] == 2,
        "first visibility starts a functioning worker");
    session.SetVisible(false);
    Check(State(session.State())["media"].is_null(), "hide clears runtime state after deferred start");
    session.Shutdown();
    Check(!session.HasWork(), "deferred runtime shuts down");
}

void MetadataHardLinksDoNotTruncateTargets() {
    Fixture fixture;
    auto session = fixture.Open();
    Check(session.Restore(), "hardlink fixture restore");
    fixture.selected = fixture.source.wstring();
    Check(Execute(session, Command(MusicBackgroundAction::Choose, 1, 0))["revision"] == 1,
        "hardlink fixture initial commit");
    const auto target = fixture.root / "unrelated-sentinel.txt";
    const std::string sentinel = "KEEP-THIS-ISOLATED-HARDLINK-TARGET-UNCHANGED";
    std::uint64_t revision = 1;
    for (const auto* suffix : {".tmp", ".bak.tmp"}) {
        Write(target, sentinel);
        const auto link = fs::u8path(fixture.options.statePath + suffix);
        Check(CreateHardLinkW(link.c_str(), target.c_str(), nullptr), "create isolated staging hardlink");
        const auto result = State(session.HandlePage(Command(MusicBackgroundAction::Clear, revision + 1, revision)));
        Check(Read(target) == sentinel, "staging hardlink cannot truncate unrelated file");
        Check(result["status"] == "ready", "safe replacement works despite pre-existing staging hardlink");
        ++revision;
    }
    Check(Json::parse(Read(fs::u8path(fixture.options.statePath)))["revision"] == revision,
        "hardlink-safe atomic commit remains readable");
}

void PlainCapabilityBoundary() {
    Fixture fixture;
    std::string plain(200000, '\0');
    for (std::size_t i = 0; i < plain.size(); ++i) plain[i] = static_cast<char>('A' + i % 23);
    Write(fixture.source, plain);
    fixture.selected = fixture.source.wstring();
    auto session = fixture.Open();
    Check(session.Restore(), "plain capability restore");
    const auto selected = Execute(session, Command(MusicBackgroundAction::Choose, 1, 0));
    const auto url = selected["media"]["url"].get<std::string>();
    Check(url.rfind("http://127.0.0.1:", 0) == 0 &&
        url.find(selected["media"]["id"].get<std::string>()) == std::string::npos,
        "plain media uses opaque runtime capability, not directory URL");
    const auto path = fs::path(session.MediaDirectory()) / (selected["media"]["id"].get<std::string>() + ".png");
    const auto response = Get(url);
    Check(response.status == 200 && response.body == plain && response.cors == "https://lume.local",
        "plain pinned reader serves bounded bytes and exact origin");
    const auto head = Get(url, {}, L"HEAD");
    Check(head.status == 200 && head.body.empty() && head.cors == "https://lume.local",
        "plain HEAD returns metadata without reading the entire file");
    const auto origin = url.substr(0, url.find('/', 17));
    Write(path.parent_path() / "unregistered.png", "unregistered");
    Check(Get(origin + "/unregistered.png").status == 404 &&
        Get(url.substr(0, url.rfind('/')) + "/unregistered.png").status == 404,
        "server never resolves unregistered directory paths");
    const auto write = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (write != INVALID_HANDLE_VALUE) CloseHandle(write);
    Check(write == INVALID_HANDLE_VALUE && !DeleteFileW(path.c_str()),
        "live plain lease prevents in-place writes and replacement");
    std::vector<std::future<bool>> requests;
    for (unsigned i = 0; i < 6; ++i) {
        requests.push_back(std::async(std::launch::async, [&, i] {
            const std::size_t offset = 101 + i * 12001;
            const auto reply = Get(url, "bytes=" + std::to_string(offset) + "-" + std::to_string(offset + 70000));
            return reply.status == 206 && reply.body == plain.substr(offset, 70001);
        }));
    }
    for (auto& request : requests) Check(request.get(), "concurrent plain ranges never race file offset");
    Check(Get(url, "bytes=200000-").status == 416, "plain range past EOF is rejected");
    const auto persisted = Read(fs::u8path(fixture.options.statePath));
    Check(persisted.find("127.0.0.1") == std::string::npos, "plain capability never persisted");
    session.SetVisible(false);
    Check(State(session.State())["media"].is_null() && Get(url).status != 200,
        "hidden plain lease is revoked");
    Check(DeleteFileW(path.c_str()) && CreateHardLinkW(path.c_str(), fixture.source.c_str(), nullptr),
        "prepare isolated hardlink substitution while hidden");
    session.SetVisible(true);
    Check(State(session.State())["error"] == "load" && State(session.State())["media"].is_null(),
        "hardlinked restored asset never gets a capability");
    session.SetVisible(false);
    Check(DeleteFileW(path.c_str()), "remove isolated hardlink");
    if (CreateSymbolicLinkW(path.c_str(), fixture.source.c_str(), 0x2)) {
        session.SetVisible(true);
        Check(State(session.State())["error"] == "load" && State(session.State())["media"].is_null(),
            "reparse substituted asset never gets a capability");
        session.SetVisible(false);
        Check(DeleteFileW(path.c_str()), "remove isolated symlink");
    } else {
        std::cout << "SKIP: file symlink fixture unavailable (Windows privilege)\n";
    }
    fs::copy_file(fixture.source, path);
    session.SetVisible(true);
    const auto fresh = State(session.State())["media"]["url"].get<std::string>();
    Check(fresh != url && Get(fresh).body == plain && Read(fs::u8path(fixture.options.statePath)) == persisted,
        "show reissues plain capability without changing durable identity");
    session.Shutdown();
    Check(Get(fresh).status != 200 && DeleteFileW(path.c_str()), "shutdown releases plain file and capability");
}

void LargePlainRange() {
    Fixture fixture;
    fixture.options.initiallyVisible = false;
    const Media media{"background-0123456789abcdef", "Sparse test", "mp4",
        lume::music::MusicBackgroundKind::Video, "video/mp4"};
    const auto path = fs::u8path(fixture.options.root) / "media" / (media.id + ".mp4");
    fs::create_directories(path.parent_path());
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "create isolated sparse large asset");
    DWORD count = 0;
    LARGE_INTEGER offset{};
    offset.QuadPart = 512ll * 1024 * 1024 - 3;
    const bool written = DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &count, nullptr) &&
        SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) && WriteFile(file, "END", 3, &count, nullptr) && count == 3;
    CloseHandle(file);
    Check(written, "sparse test fixture is bounded on disk");
    Store store(fs::u8path(fixture.options.statePath), fixture.files);
    Check(store.Load() == Result::Ready && store.Save(media, 0) == Result::Ready, "save large plain identity");
    auto session = fixture.Open();
    Check(session.Restore(), "large hidden restore");
    session.SetVisible(true);
    const auto url = State(session.State())["media"]["url"].get<std::string>();
    const auto range = Get(url, "bytes=536870909-536870911");
    Check(range.status == 206 && range.body == "END", "512 MiB plain file serves only requested tail range");
    session.Shutdown();
}

void InputHardLinksRejected() {
    Fixture fixture;
    const auto alias = fixture.root / "alias.png";
    Check(CreateHardLinkW(alias.c_str(), fixture.source.c_str(), nullptr), "plain input hardlink fixture");
    fixture.selected = alias.wstring();
    auto session = fixture.Open();
    Check(session.Restore(), "input hardlink restore");
    Check(Execute(session, Command(MusicBackgroundAction::Choose, 1, 0))["status"] == "error" &&
        State(session.State())["revision"] == 0, "chooser rejects hardlinked input");
    const auto protectedPath = fs::u8path(ProtectedFixture(fixture, "official-fixture", "protected-input"));
    const auto protectedAlias = fixture.root / "alias.apk";
    Check(CreateHardLinkW(protectedAlias.c_str(), protectedPath.c_str(), nullptr), "protected input hardlink fixture");
    Check(Execute(session, Select("official-fixture", 2, 0))["status"] == "error" &&
        State(session.State())["revision"] == 0, "protected copying rejects hardlinked input");
    Check(fs::is_empty(fs::u8path(fixture.options.root) / "media") &&
        fs::is_empty(fs::u8path(fixture.options.root) / "protected" / "media"),
        "rejected aliases leave no copied media");
    Check(DeleteFileW(alias.c_str()), "remove isolated plain input alias");
    fixture.selected = fixture.source.wstring();
    const auto wakes = fixture.wakes.load();
    Check(State(session.HandlePage(Command(MusicBackgroundAction::Choose, 3, 0)))["status"] == "busy",
        "plain copy scheduled");
    const auto until = GetTickCount64() + 5000;
    while (fixture.wakes.load() == wakes && GetTickCount64() < until) Sleep(2);
    Check(fixture.wakes.load() > wakes, "plain copy completed before commit");
    session.CancelActive();
    Wait(session);
    Check(fs::is_empty(fs::u8path(fixture.options.root) / "media") &&
        !fs::exists(fs::u8path(fixture.options.statePath)), "cancelled plain copy releases reader before cleanup");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        LocalThumbnails();
        ResizedThumbnail();
        VideoFrameOrientation();
        ProtectedAndVideoThumbnails();
        if (argc == 2) LegacyMigration(fs::u8path(argv[1]));
        else std::cout << "SKIP: historical legacy fixture directory not supplied\n";
        ModalChooserLifecycle();
        InitiallyHiddenRestore();
        PlainCapabilityBoundary();
        LargePlainRange();
        InputHardLinksRejected();
        MetadataHardLinksDoNotTruncateTargets();
        EmptyRootDoesNotExposeRelativeMediaDirectory();
        ImportAndClear();
        ReplacementAndConflict();
        InvalidAndMissingRecovery();
        CancelAndShutdown();
        ProtocolAndMigration();
        GalleryPlainAndPagination();
        ProtectedIsolationAndLifetime();
        CancellationAndFailure();
        BackupAssetsAndWrongKey();
        FailedCommitAndBoundedLease();
        std::cout << "music background session checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
