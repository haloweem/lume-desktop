#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "platform/windows/wallpaper/download.h"
#include "vendor/nlohmann/json.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <iostream>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
using namespace lume::platform::windows::wallpaper;
using lume::wallpaper::CatalogItem;
void Expect(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
class Server {
public:
    explicit Server(std::vector<std::string> replies, int delayMs = 0) {
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{}; address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Expect(listener_ != INVALID_SOCKET &&
            bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
            listen(listener_, 5) == 0, "create isolated HTTP fixture");
        int length = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this, replies = std::move(replies), delayMs] {
            for (const auto& reply : replies) {
                auto client = accept(listener_, nullptr, nullptr);
                if (client == INVALID_SOCKET) break;
                char bytes[4096];
                DWORD timeout = 3000;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
                const auto got = recv(client, bytes, sizeof(bytes), 0);
                { std::lock_guard<std::mutex> lock(mutex_); headers_.emplace_back(bytes, got > 0 ? static_cast<std::size_t>(got) : 0); }
                ++requests;
                for (int elapsed = 0; elapsed < delayMs && !stopped_; elapsed += 10)
                    std::this_thread::sleep_for(10ms);
                std::size_t sent = 0;
                while (sent < reply.size() && !stopped_) {
                    const auto count = send(client, reply.data() + sent,
                        static_cast<int>(std::min<std::size_t>(16384, reply.size() - sent)), 0);
                    if (count <= 0) break;
                    sent += static_cast<std::size_t>(count);
                }
                shutdown(client, SD_BOTH); closesocket(client);
                if (stopped_) break;
            }
        });
    }
    ~Server() { stopped_ = true; closesocket(listener_); if (worker_.joinable()) worker_.join(); }
    std::string Url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/lume/test.mp4"; }
    std::atomic<unsigned> requests{0};
    std::vector<std::string> Headers() { std::lock_guard<std::mutex> lock(mutex_); return headers_; }
private:
    std::mutex mutex_;
    std::vector<std::string> headers_;
    SOCKET listener_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::atomic<bool> stopped_{false};
    std::thread worker_;
};
std::string Response(const std::string& body, int status = 200, std::string type = "video/mp4") {
    return "HTTP/1.1 " + std::to_string(status) + " Fixture\r\nContent-Type: " + type +
        "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}
// This deliberately simple test writer verifies the download/encryption boundary.
// Production must provide authenticated encryption; this fixture is never linked.
class FixtureWriter final : public IProtectedDownloadWriter {
public:
    explicit FixtureWriter(const std::string& path) : file_(std::filesystem::u8path(path), std::ios::binary) {
        file_ << "LUMEWP02";
    }
    bool Append(const std::uint8_t* bytes, std::size_t size, std::string&) noexcept override {
        maxChunk = std::max(maxChunk, size);
        for (std::size_t i = 0; i < size; ++i) file_.put(static_cast<char>(bytes[i] ^ 0x5a));
        return static_cast<bool>(file_);
    }
    bool Finish(std::string&) noexcept override { file_.flush(); const auto ok = bool(file_); file_.close(); return ok; }
    static std::size_t maxChunk;
private:
    std::ofstream file_;
};
std::size_t FixtureWriter::maxChunk = 0;
DownloadOptions Options() {
    DownloadOptions result;
    result.allowLoopbackForTests = true;
    result.protectedWriter = [](const std::string& path, std::string&) {
        return std::make_unique<FixtureWriter>(path);
    };
    return result;
}
CatalogItem Item(const std::string& url) {
    CatalogItem item;
    item.id = "fixture"; item.title = "Fixture";
    item.kind = lume::wallpaper::MediaKind::Video;
    item.mediaUrl = url; item.sizeBytes = 3;
    item.sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    return item;
}
void SuccessAndRetry(const std::filesystem::path& base) {
    Server server({Response({}, 503), Response("abc")});
    std::atomic<bool> cancel{false};
    unsigned progress = 0;
    const auto destination = (base / "success.apk").u8string();
    const auto result = Download(Item(server.Url()), destination, cancel,
        [&progress](const DownloadProgress& value) { ++progress; Expect(value.receivedBytes <= 3, "bounded progress"); },
        Options());
    Expect(result.ok && result.receivedBytes == 3 && server.requests == 2, "retry then publish verified encrypted media");
    Expect(progress > 0 && FixtureWriter::maxChunk <= 64 * 1024, "stream bounded chunks into protected writer");
    std::ifstream input(std::filesystem::u8path(destination), std::ios::binary);
    const std::string encoded{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    Expect(encoded == std::string("LUMEWP02") + ";89", "download never writes plaintext around protected writer");
}
// A CDN slice that ends a response early: keep the good bytes and continue
// with a Range from where it stopped, including past an empty 206.
void ResumeShortBody(const std::filesystem::path& base) {
    const std::string head = "HTTP/1.1 206 Fixture\r\nContent-Type: video/mp4\r\nContent-Range: bytes 1-2/3\r\n"
        "Content-Length: 2\r\nConnection: close\r\n\r\n";
    Server server({"HTTP/1.1 200 Fixture\r\nContent-Type: video/mp4\r\nContent-Length: 3\r\nConnection: close\r\n\r\na",
        head, head + "bc"});
    std::atomic<bool> cancel{false};
    const auto destination = (base / "resumed.apk").u8string();
    const auto result = Download(Item(server.Url()), destination, cancel, {}, Options());
    Expect(result.ok && result.receivedBytes == 3 && server.requests == 3, "short bodies resume instead of restarting");
    const auto headers = server.Headers();
    Expect(headers.size() == 3 && headers[0].find("Range:") == std::string::npos &&
        headers[1].find("Range: bytes=1-2") != std::string::npos && headers[2].find("Range: bytes=1-2") != std::string::npos,
        "resume asks only for the missing bytes");
    std::ifstream input(std::filesystem::u8path(destination), std::ios::binary);
    const std::string encoded{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    Expect(encoded == std::string("LUMEWP02") + ";89", "resumed bytes form the verified file once");
}
void FailureDoesNotPublish(const std::filesystem::path& base) {
    Server server({Response("abd")});
    const auto target = base / "unchanged.apk";
    { std::ofstream output(target, std::ios::binary); output << "prior"; }
    std::atomic<bool> cancel{false};
    const auto result = Download(Item(server.Url()), target.u8string(), cancel, {}, Options());
    Expect(!result.ok && result.error == "WALLPAPER_SHA256_MISMATCH", "SHA256 mismatch is terminal");
    std::ifstream input(target, std::ios::binary);
    const std::string previous{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    Expect(previous == "prior", "failed verification preserves previous completed file");
    for (const auto& file : std::filesystem::directory_iterator(base))
        Expect(file.path().extension() != ".download", "failed transfer removes only its temporary file");
}
void Cancellation(const std::filesystem::path& base) {
    Server server({Response("abc")}, 4000);
    std::atomic<bool> cancel{false};
    const auto start = std::chrono::steady_clock::now();
    auto job = std::async(std::launch::async, [&] {
        return Download(Item(server.Url()), (base / "cancel.apk").u8string(), cancel, {}, Options());
    });
    while (!server.requests && std::chrono::steady_clock::now() - start < 2s)
        std::this_thread::sleep_for(5ms);
    Expect(server.requests == 1, "request actually entered a waiting network operation");
    const auto cancelledAt = std::chrono::steady_clock::now();
    cancel = true;
    Expect(job.wait_for(1s) == std::future_status::ready, "cancellation aborts waiting network handle promptly");
    const auto result = job.get();
    Expect(result.cancelled && !result.ok && std::chrono::steady_clock::now() - cancelledAt < 1s,
        "cancellation has a distinct terminal result");
    Expect(!std::filesystem::exists(base / "cancel.apk"), "cancelled transfer does not publish");
}
void ProtectionAndOrigin(const std::filesystem::path& base) {
    std::atomic<bool> cancel{false};
    auto item = Item("https://static.lumedesktop.com/lume/test.mp4");
    Expect(Download(item, (base / "never.apk").u8string(), cancel).error ==
        "WALLPAPER_PROTECTION_UNAVAILABLE", "production refuses plaintext fallback");
    item.mediaUrl = "https://static.lumedesktop.com.evil.invalid/lume/test.mp4";
    Expect(Download(item, (base / "never.apk").u8string(), cancel, {}, Options()).error ==
        "WALLPAPER_DOWNLOAD_URL_INVALID", "download independently validates catalog origin");
    item.mediaUrl = "http://127.0.0.1:9/lume/test.mp4";
    auto options = Options(); options.allowLoopbackForTests = false;
    Expect(Download(item, (base / "never.apk").u8string(), cancel, {}, options).error ==
        "WALLPAPER_DOWNLOAD_URL_INVALID", "production never enables fixture HTTP");
}
void HttpValidation(const std::filesystem::path& base) {
    std::atomic<bool> cancel{false};
    {
        Server server({"HTTP/1.1 200 Fixture\r\nContent-Type: video/mp4\r\nContent-Encoding: gzip\r\n"
            "Content-Length: 3\r\nConnection: close\r\n\r\nabc"});
        auto result = Download(Item(server.Url()), (base / "gzip.apk").u8string(), cancel, {}, Options());
        Expect(!result.ok && result.error == "WALLPAPER_CONTENT_ENCODING", "wallpaper media still refuses encoded bodies");
    }
    {
        Server server({Response("abc", 200, "text/html")});
        auto result = Download(Item(server.Url()), (base / "html.apk").u8string(), cancel, {}, Options());
        Expect(!result.ok && result.error == "WALLPAPER_CONTENT_TYPE_INVALID",
            "reject HTML disguised as media before encrypted publication");
    }
    {
        Server server({Response("abcd")});
        auto result = Download(Item(server.Url()), (base / "size.apk").u8string(), cancel, {}, Options());
        Expect(!result.ok && result.error == "WALLPAPER_SIZE_MISMATCH", "declared size must match signed catalog metadata");
    }
    {
        Server server({"HTTP/1.1 302 Found\r\nLocation: https://evil.invalid/steal.mp4\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"});
        auto result = Download(Item(server.Url()), (base / "redirect.apk").u8string(), cancel, {}, Options());
        Expect(!result.ok && result.error == "WALLPAPER_HTTP_REDIRECT", "every redirect is independently allowlisted");
    }
}
void NativeEncryptedRanges(const std::filesystem::path& base) {
    const auto readOnlyRoot = base / "reader-only";
    std::string readOnlyError;
    Expect(!EncryptedMediaReader::Open(readOnlyRoot.u8string(),
        (readOnlyRoot / "media" / "missing.apk").u8string(), readOnlyError) &&
        !std::filesystem::exists(readOnlyRoot / "cache-key.bin"),
        "opening media never provisions or replaces a library encryption key");
    const auto root = base / "native";
    const auto path = root / "media" / "native.apk";
    std::filesystem::create_directories(path.parent_path());
    std::string error;
    auto factory = MakeProtectedWriterFactory(root.u8string());
    auto writer = factory(path.u8string(), error);
    Expect(bool(writer), "create DPAPI-backed native AES writer");
    std::string plain(140000, 'a');
    for (std::size_t i = 0; i < plain.size(); ++i) plain[i] = static_cast<char>(i % 251);
    Expect(writer->Append(reinterpret_cast<const std::uint8_t*>(plain.data()), 37000, error) &&
        writer->Append(reinterpret_cast<const std::uint8_t*>(plain.data() + 37000), plain.size() - 37000, error) &&
        writer->Finish(error), "encrypt multiple authenticated records without plaintext staging");
    writer.reset();
    auto reader = EncryptedMediaReader::Open(root.u8string(), path.u8string(), error);
    Expect(reader && reader->Size() == plain.size(), "native header authenticates media size");
    std::string range(80000, '\0'); std::size_t read = 0;
    Expect(reader->Read(65000, range.data(), range.size(), read) && read == plain.size() - 65000 &&
        range.substr(0, read) == plain.substr(65000), "decrypt authenticated range across record boundaries");
    Expect(reader->Read(plain.size(), range.data(), range.size(), read) && read == 0,
        "EOF read returns zero bytes");
    reader.reset();
    {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        file.seekg(80 + 40); char byte = 0; file.read(&byte, 1); byte ^= 1;
        file.seekp(80 + 40); file.write(&byte, 1);
    }
    reader = EncryptedMediaReader::Open(root.u8string(), path.u8string(), error);
    Expect(reader && !reader->Read(0, range.data(), 8, read) && read == 0,
        "GCM tampering fails before unauthenticated bytes reach caller");
    reader.reset();
    std::filesystem::remove(root / "cache-key.bin");
    Expect(!EncryptedMediaReader::Open(root.u8string(), path.u8string(), error),
        "lost DPAPI key never silently replaces existing encrypted library key");
}
std::string Manifest(bool video) {
    using Json = nlohmann::json;
    return Json{{"wallpapers", Json::array({{
        {"id", video ? "video" : "image"}, {"title", "Fixture"},
        {"sizeBytes", 3}, {"sha256", Item({}).sha256},
        {"downloadUrl", video ? "https://static.lumedesktop.com/lume/video.mp4" :
            "https://static.lumedesktop.com/lume/image.jpg"}
    }})}}.dump();
}
void PartialCatalogs() {
    std::atomic<bool> cancel{false};
    {
        // {"wallpapers":[]} gzip encoded, as a compressing CDN returns it.
        const unsigned char gzip[] = {0x1f,0x8b,0x08,0x00,0x00,0x00,0x00,0x00,0x02,0x0a,0xab,0x56,0x2a,0x4f,0xcc,0xc9,
            0x29,0x48,0x2c,0x48,0x2d,0x2a,0x56,0xb2,0x8a,0x8e,0xad,0x05,0x00,0xbd,0xe9,0xe1,0x86,0x11,0x00,0x00,0x00};
        const std::string body(reinterpret_cast<const char*>(gzip), sizeof(gzip));
        Server videos({Response(Manifest(true), 200, "application/json")});
        Server images({"HTTP/1.1 200 Fixture\r\nContent-Type: application/json\r\nContent-Encoding: gzip\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body});
        const auto result = FetchCatalog(cancel, {videos.Url(), images.Url(), true});
        Expect(result.error.empty() && result.videoCatalogLoaded && result.imageCatalogLoaded && result.items.size() == 1,
            "a gzip encoded manifest is decoded and loads");
    }
    {
        Server videos({Response(Manifest(true), 200, "application/json")});
        Server images({Response("{", 200, "application/json")});
        CatalogFetchOptions options{videos.Url(), images.Url(), true};
        const auto result = FetchCatalog(cancel, options);
        Expect(result.items.size() == 1 && result.videoCatalogLoaded && !result.imageCatalogLoaded &&
            result.error.find("image:") == 0, "partial catalog success reports exactly the valid collection");
    }
    {
        Server videos({Response("{}", 200, "application/json")});
        Server images({Response("{\"wallpapers\":[]}", 200, "application/json")});
        CatalogFetchOptions options{videos.Url(), images.Url(), true};
        const auto result = FetchCatalog(cancel, options);
        Expect(result.items.empty() && !result.videoCatalogLoaded && result.imageCatalogLoaded,
            "valid empty catalog is distinguishable from a failed fetch");
    }
    {
        Server videos({Response(Manifest(false), 200, "application/json")});
        Server images({Response(Manifest(false), 200, "application/json")});
        CatalogFetchOptions options{videos.Url(), images.Url(), true};
        const auto result = FetchCatalog(cancel, options);
        Expect(!result.videoCatalogLoaded && result.imageCatalogLoaded && result.items.size() == 1,
            "wrong media kind cannot contaminate the independently refreshed collection");
    }
}
void OfficialCatalogPresentation() {
    using Json = nlohmann::json;
    auto manifest = Json::parse(Manifest(true));
    const auto fixture = manifest["wallpapers"][0];
    manifest["wallpapers"] = Json::array();
    for (const auto* id : {"forest-2", "eb-sunflower-girl", "media-player", "wlop-2-2",
             "hatsune-miku-night-beach", "3d-snowflakes", "wlop-2",
             "hatsune-miku-nostalgic-night"}) {
        auto record = fixture;
        record["id"] = id;
        manifest["wallpapers"].push_back(record);
    }
    manifest["wallpapers"][0]["title"] = "Forest 2";
    manifest["wallpapers"][1]["displayName"] = u8"EB\u5341 Sunflower Girl";
    std::atomic<bool> cancel{false};
    for (const auto* imagesBody : {"{\"wallpapers\":[]}", "{"}) {
        Server videos({Response(manifest.dump(), 200, "application/json")});
        Server images({Response(imagesBody, 200, "application/json")});
        const auto result = FetchCatalog(cancel, {videos.Url(), images.Url(), true});
        Expect(result.videoCatalogLoaded && result.items.size() == 4 &&
            result.items[0].id == "forest-2" && result.items[0].title == "Forest" &&
            result.items[1].id == "eb-sunflower-girl" && result.items[1].title == "Sunflower Girl" &&
            result.items[2].id == "wlop-2" && result.items[3].id == "hatsune-miku-nostalgic-night",
            "full and partial online refreshes apply the same exact official listing policy");
    }
    manifest["wallpapers"][2]["downloadUrl"] = "https://static.lumedesktop.com/lume/hidden.jpg";
    Server videos({Response(manifest.dump(), 200, "application/json")});
    Server images({Response("{\"wallpapers\":[]}", 200, "application/json")});
    const auto invalid = FetchCatalog(cancel, {videos.Url(), images.Url(), true});
    Expect(!invalid.videoCatalogLoaded && invalid.imageCatalogLoaded && invalid.items.empty() &&
        invalid.error == "video:WALLPAPER_CATALOG_COLLECTION",
        "hidden online items cannot bypass collection validation");
}
}
// Explicit network diagnostic only: downloads one real official wallpaper
// (multi-connection for large files) into a temporary library, verifies its
// SHA-256 and reports the speed. Usage: --live <url> <sha256> <bytes>
int Live(int argc, char** argv) {
    if (argc != 5) { std::cerr << "usage: --live <url> <sha256> <bytes>\n"; return 2; }
    lume::wallpaper::CatalogItem item;
    item.id = "live-download-probe";
    item.mediaUrl = argv[2];
    item.sha256 = argv[3];
    item.sizeBytes = std::stoull(argv[4]);
    item.kind = lume::wallpaper::MediaKind::Video;
    const auto root = std::filesystem::temp_directory_path() /
        ("lume-live-download-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    std::atomic<bool> cancel{false};
    DownloadOptions options;
    options.protectedWriter = MakeProtectedWriterFactory(root.u8string());
    const auto start = std::chrono::steady_clock::now();
    const auto result = Download(item, (root / "probe.apk").u8string(), cancel, {}, options);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "ok=" << result.ok << " error=" << result.error << " bytes=" << result.receivedBytes
              << " seconds=" << seconds << " speed=" << (result.receivedBytes / seconds / 1024 / 1024) << " MB/s\n";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return result.ok ? 0 : 1;
}
int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--live") return Live(argc, argv);
    WSADATA data{}; Expect(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Winsock fixture start");
    const auto fixtureRoot = std::filesystem::absolute(std::filesystem::current_path() / "build-artifacts" / "fixtures");
    const auto base = fixtureRoot /
        ("lume-wallpaper-download-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(base);
    SuccessAndRetry(base);
    ResumeShortBody(base);
    FailureDoesNotPublish(base);
    Cancellation(base);
    ProtectionAndOrigin(base);
    HttpValidation(base);
    NativeEncryptedRanges(base);
    PartialCatalogs();
    OfficialCatalogPresentation();
    Expect(std::filesystem::weakly_canonical(base).parent_path() ==
        std::filesystem::weakly_canonical(fixtureRoot), "fixture cleanup remains in explicitly owned directory");
    std::filesystem::remove_all(base);
    WSACleanup();
    std::cout << "wallpaper download tests passed\n";
}
