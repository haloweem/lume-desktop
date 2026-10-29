#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "apps/shell/music_library_worker.h"
#include "tests/unit/unique_test_directory.h"
#include "vendor/nlohmann/json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;
using lume::shell::MusicLibraryWorker;
using lume::shell::MusicLibraryWorkerResult;
using Action = lume::shell::MusicLibraryPlaybackAction;
using Json = nlohmann::json;
using Microsoft::WRL::ComPtr;

void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void Require(HRESULT result) { Check(SUCCEEDED(result), "WIC fixture"); }
void Write(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Check(out.good(), "fixture write");
}
std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
template<class Predicate> void Wait(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!predicate()) {
        Check(std::chrono::steady_clock::now() < deadline, "worker timeout");
        std::this_thread::sleep_for(5ms);
    }
}
std::vector<MusicLibraryWorkerResult> Idle(MusicLibraryWorker& worker) {
    std::vector<MusicLibraryWorkerResult> results;
    Wait([&] {
        for (auto& item : worker.Drain()) results.push_back(std::move(item));
        return !worker.HasWork();
    });
    for (auto& item : worker.Drain()) results.push_back(std::move(item));
    return results;
}
std::string Png(bool noisy = false) {
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
    const UINT edge = noisy ? 256 : 2;
    Require(frame->SetSize(edge, edge));
    auto format = GUID_WICPixelFormat24bppBGR;
    Require(frame->SetPixelFormat(&format));
    std::vector<BYTE> pixels(edge * edge * 3, 80);
    std::uint32_t random = 17;
    if (noisy) for (auto& pixel : pixels) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        pixel = static_cast<BYTE>(random);
    }
    Require(frame->WritePixels(edge, edge * 3, static_cast<UINT>(pixels.size()), pixels.data()));
    Require(frame->Commit());
    Require(encoder->Commit());
    STATSTG stat{};
    Require(stream->Stat(&stat, STATFLAG_NONAME));
    LARGE_INTEGER zero{};
    Require(stream->Seek(zero, STREAM_SEEK_SET, nullptr));
    std::string bytes(static_cast<std::size_t>(stat.cbSize.QuadPart), '\0');
    ULONG read = 0;
    Require(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read));
    return bytes;
}
void Mp3(const fs::path& path, const std::string& image = {}) {
    std::string payload(1, '\0');
    payload += "image/png";
    payload.append("\0\3\0", 3);
    payload += image.empty() ? Png() : image;
    std::string tag = "APIC";
    for (int shift = 24; shift >= 0; shift -= 8)
        tag += static_cast<char>((payload.size() >> shift) & 255);
    tag.append(2, '\0');
    tag += payload;
    std::string bytes("ID3\3\0\0", 6);
    for (int shift = 21; shift >= 0; shift -= 7)
        bytes += static_cast<char>((tag.size() >> shift) & 127);
    bytes += tag;
    for (unsigned i = 0; i < 40; ++i) {
        bytes.append("\xff\xfb\x90\x64", 4);
        bytes.append(413, '\0');
    }
    Write(path, bytes);
}

struct HttpHandle {
    HINTERNET value = nullptr;
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};
std::string Fetch(const std::string& url, const std::wstring& origin, DWORD& status,
    std::wstring* allowedOrigin = nullptr) {
    status = 0;
    if (allowedOrigin) allowedOrigin->clear();
    const std::wstring wide(url.begin(), url.end());
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = static_cast<DWORD>(-1);
    Check(WinHttpCrackUrl(wide.c_str(), 0, 0, &parts), "parse lease URL");
    Check(std::wstring(parts.lpszHostName, parts.dwHostNameLength) == L"127.0.0.1",
        "lease binds loopback only");
    HttpHandle client{WinHttpOpen(L"isolated-music-worker-test", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    Check(client.value != nullptr, "HTTP fixture client");
    WinHttpSetTimeouts(client.value, 1000, 1000, 1000, 1000);
    HttpHandle connection{WinHttpConnect(client.value, L"127.0.0.1", parts.nPort, 0)};
    const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    HttpHandle request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)};
    const auto headers = L"Origin: " + origin + L"\r\n";
    if (!WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(headers.size()),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr)) return {};
    DWORD size = sizeof(status);
    Check(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX), "HTTP status");
    if (allowedOrigin) {
        wchar_t header[256]{};
        size = sizeof(header);
        if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CUSTOM, L"Access-Control-Allow-Origin",
            header, &size, WINHTTP_NO_HEADER_INDEX)) *allowedOrigin = header;
    }
    std::string body;
    char buffer[4096];
    DWORD read = 0;
    while (WinHttpReadData(request.value, buffer, sizeof(buffer), &read) && read) {
        body.append(buffer, read);
        Check(body.size() <= 1024 * 1024, "bounded artwork response");
    }
    return body;
}
}

int main() {
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto root = UniqueTestDirectory(fs::temp_directory_path(), "lume-library-worker-");
    try {
        {
            MusicLibraryWorker lazy({root / "unused", {}});
            Check(!lazy.HasWork() && lazy.Drain().empty() && !lazy.Resolve("local-1"),
                "construction, Drain and Resolve are lazy");
            Check(!lazy.Page(0, 0) && !lazy.HasWork() && !fs::exists(root / "unused"),
                "zero page limit is rejected without starting worker");
            lazy.SetVisible(true);
            lazy.SetVisible(false);
            Check(!fs::exists(root / "unused"), "visibility does not create storage");
            lazy.Shutdown();
            Check(!lazy.Restore() && !lazy.SaveQueue({}) && !fs::exists(root / "unused"),
                "shutdown does not start unused worker");
        }
        {
            MusicLibraryWorker invalidRoot({fs::path("relative-worker-invalid"), {}});
            Check(invalidRoot.Restore(), "invalid root validated on worker");
            const auto results = Idle(invalidRoot);
            Check(results.size() == 1 && results[0].error == "storage-unavailable" &&
                !results[0].restoredQueue, "relative root fails without UI-thread disk work");
        }
        const auto first = root / L"\u6d4b\u8bd5.mp3", second = root / "second.wav";
        Mp3(first);
        Write(second, "RIFF");
        std::atomic<unsigned> wakes{0};
        std::atomic<bool> wrongThread{false};
        const auto ui = std::this_thread::get_id();
        const auto storage = root / "state";
        MusicLibraryWorker worker({storage, [&] {
            if (std::this_thread::get_id() == ui) wrongThread = true;
            ++wakes;
        }});
        Check(!worker.Import({}) && !worker.Play("../escape") && !worker.Artwork("local-1"),
            "invalid input and hidden artwork rejected");
        Check(worker.Import({first.wstring(), second.wstring(), (root / "missing.mp3").wstring()}),
            "import scheduled");
        auto results = Idle(worker);
        Check(results.size() == 1 && results[0].imported && results[0].imported->added == 2 &&
            results[0].imported->failures.size() == 1, "isolated import retains per-file failures");
        const auto state = Json::parse(results[0].state);
        Check(state.at("total") == 2 && state.at("tracks")[0].at("source") == "" &&
            results[0].state.find(root.wstring()) == std::wstring::npos,
            "UI projection does not expose native source paths");
        const auto id = state.at("tracks")[0].at("id").get<std::string>();
        const auto other = state.at("tracks")[1].at("id").get<std::string>();
        const auto track = worker.Resolve(id);
        Check(track && !track->source.empty() && !worker.Resolve("local-999999999999999999"),
            "native memory cache resolves only imported identities");
        Wait([&] { return wakes > 0; });
        Check(!wrongThread, "wake comes from worker, not UI");
        Check(worker.Page(1, 1), "page scheduled");
        results = Idle(worker);
        Check(Json::parse(results.back().state).at("tracks").size() == 1 &&
            Json::parse(results.back().state).at("offset") == 1, "bounded paginated state");

        Check(worker.Play(id) && worker.Play(other, Action::Enqueue) && worker.Play(other) &&
            worker.Play(id, Action::InsertNext), "playback actions accepted");
        results = Idle(worker);
        unsigned plays = 0, enqueues = 0, inserts = 0;
        std::uint64_t last = 0;
        for (const auto& result : results) {
            if (result.action == Action::None) continue;
            Check(result.generation > last, "playback generations increase");
            last = result.generation;
            if (result.action == Action::Play) {
                ++plays;
                Check(result.playId == other && result.playTracks.size() == 2,
                    "latest play supplies native queue");
            } else if (result.action == Action::Enqueue) {
                ++enqueues;
                Check(result.playTracks.size() == 1 && result.playTracks[0].id == other,
                    "enqueue has only requested ID");
            } else if (result.action == Action::InsertNext) {
                ++inserts;
                Check(result.playTracks.size() == 1 && result.playTracks[0].id == id,
                    "insert-next has only requested ID");
            }
        }
        Check(plays == 1 && enqueues == 1 && inserts == 1, "stale play suppressed, ordered additions retained");

        lume::music::MusicSnapshot snapshot;
        snapshot.queue = {*track, *worker.Resolve(other)};
        snapshot.currentIndex = 0;
        snapshot.current = snapshot.queue[0];
        snapshot.position = 0.1;
        snapshot.playing = true;
        snapshot.volume = 0.4;
        snapshot.muted = true;
        snapshot.shuffle = true;
        Check(worker.SaveQueue(snapshot), "queue save accepted");
        Idle(worker);
        const auto savedBytes = Read(storage / "queue.json");
        const auto savedTime = fs::last_write_time(storage / "queue.json");
        for (unsigned i = 0; i < 5; ++i) {
            snapshot.revision++;
            snapshot.analysis.energy = i;
            Check(worker.SaveQueue(snapshot), "equivalent runtime snapshots accepted");
        }
        Idle(worker);
        Check(Read(storage / "queue.json") == savedBytes &&
            fs::last_write_time(storage / "queue.json") == savedTime, "identical queue is never rewritten");
        for (unsigned i = 1; i <= 5; ++i) {
            snapshot.position = 0.1 + i * 0.1;
            Check(worker.SaveQueue(snapshot), "progress snapshot accepted");
            std::this_thread::sleep_for(250ms);
            worker.Drain();
        }
        Check(Read(storage / "queue.json") == savedBytes &&
            fs::last_write_time(storage / "queue.json") == savedTime, "250 ms progress never writes at UI cadence");
        // Structural changes pull the pending progress save forward and merge all latest fields.
        snapshot.volume = 0.5;
        Check(worker.SaveQueue(snapshot), "structural change expedites pending progress");
        Idle(worker);
        Check(Json::parse(Read(storage / "queue.json")).at("position") == snapshot.position &&
            Json::parse(Read(storage / "queue.json")).at("volume") == 0.5, "latest queue fields coalesced");
        auto invalid = snapshot;
        invalid.queue[0].id = "online:forbidden";
        Check(!worker.SaveQueue(invalid), "nonlocal queue rejected");
        invalid = snapshot;
        invalid.current->id = other;
        Check(!worker.SaveQueue(invalid), "mismatched current identity rejected");
        invalid = snapshot;
        invalid.queue[0].id = "local-999999999999999999";
        invalid.current.reset();
        Check(worker.SaveQueue(invalid), "removed local ID filtered asynchronously");
        results = Idle(worker);
        const auto filteredQueue = Json::parse(Read(storage / "queue.json"));
        Check(std::all_of(results.begin(), results.end(), [](const auto& result) {
            return result.error.empty();
        }) && filteredQueue.at("ids") == Json::array({other}) &&
            filteredQueue.at("current") == other && filteredQueue.at("position") == 0,
            "removed item cannot block saving surviving queue entries");
        const auto beforeFailure = Read(storage / "queue.json");
        HANDLE locked = CreateFileW((storage / "queue.json").c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(locked != INVALID_HANDLE_VALUE, "lock isolated queue fixture");
        auto blocked = snapshot;
        blocked.volume = 0.75;
        Check(worker.SaveQueue(blocked), "blocked disk save accepted asynchronously");
        results = Idle(worker);
        CloseHandle(locked);
        Check(Read(storage / "queue.json") == beforeFailure &&
            std::any_of(results.begin(), results.end(), [](const auto& result) {
                return result.error == "queue-save-failed" && !result.state.empty() &&
                    Json::parse(result.state).at("error") == result.error;
            }), "atomic save failure preserves disk queue and reaches Drain");
        const auto failureTime = fs::last_write_time(storage / "queue.json");
        const auto wakeDeadline = std::chrono::steady_clock::now() + 1500ms;
        unsigned retryErrors = 0;
        while (std::chrono::steady_clock::now() < wakeDeadline) {
            for (const auto& result : worker.Drain())
                if (!result.error.empty()) ++retryErrors;
            blocked.revision++;
            blocked.analysis.energy += 0.01;
            Check(worker.SaveQueue(blocked), "Drain feedback supplies failed snapshot");
            std::this_thread::sleep_for(250ms);
        }
        Check(!worker.HasWork() && retryErrors == 0 &&
            fs::last_write_time(storage / "queue.json") == failureTime,
            "unchanged failed signature never requeues or wakes after Drain");
        blocked.position += 0.01;
        Check(worker.SaveQueue(blocked), "new progress after failed save accepted");
        std::this_thread::sleep_for(900ms);
        Check(fs::last_write_time(storage / "queue.json") == failureTime && worker.Drain().empty(),
            "position changes after failure retain 30-second backoff, not 750 ms retries");
        blocked.position = snapshot.position;
        Check(worker.SaveQueue(blocked, true), "explicit retry accepts unchanged failed snapshot");
        Idle(worker);
        Check(Json::parse(Read(storage / "queue.json")).at("volume") == blocked.volume,
            "explicit retry succeeds when storage becomes writable");
        Check(worker.SaveQueue(snapshot), "new persistent change accepted");
        Idle(worker);

        worker.SetVisible(true);
        Check(worker.Artwork(id), "artwork request");
        results = Idle(worker);
        const auto art = std::find_if(results.begin(), results.end(), [](const auto& result) {
            return !result.artworkUrl.empty();
        });
        Check(art != results.end() && art->artworkId == id &&
            art->artworkUrl.find(id) == std::string::npos, "single opaque artwork lease");
        const auto artworkUrl = art->artworkUrl;
        DWORD status = 0;
        std::wstring cors;
        const auto png = Fetch(artworkUrl, L"https://lume.local", status, &cors);
        Check(status == 200 && cors == L"https://lume.local" &&
            png.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0,
            "native embedded cover is served through restricted loopback");
        Fetch(artworkUrl, L"https://untrusted.invalid", status, &cors);
        Check(status == 200 && cors.empty(), "untrusted web origin receives no CORS permission");
        Check(worker.Artwork(other), "coverless replacement accepted");
        Idle(worker);
        Fetch(artworkUrl, L"https://lume.local", status);
        Check(status != 200, "cover replacement revokes previous single lease");
        Check(worker.Artwork(id), "replacement cover");
        results = Idle(worker);
        const auto nextUrl = results.back().artworkUrl;
        Check(!nextUrl.empty(), "replacement lease created");
        worker.SetVisible(false);
        Idle(worker);
        Fetch(nextUrl, L"https://lume.local", status);
        Check(status != 200 && !worker.Artwork(id), "hidden view releases lease and rejects requests");
        worker.SetVisible(true);
        const auto beforeArt = wakes.load();
        Check(worker.Artwork(id), "late cover fixture");
        Wait([&] { return wakes.load() > beforeArt; });
        worker.SetVisible(false);
        results = Idle(worker);
        Check(std::none_of(results.begin(), results.end(), [](const auto& result) {
            return !result.artworkUrl.empty();
        }), "hidden view never drains a late artwork URL");

        snapshot.position = 0.7;
        Check(worker.SaveQueue(snapshot), "latest progress queued before shutdown");
        worker.Shutdown();
        Check(Json::parse(Read(storage / "queue.json")).at("position") == 0.7 &&
            !worker.HasWork() && !worker.Resolve(id) && !worker.Play(id), "shutdown flushes latest and clears native cache");
        worker.Shutdown();
        {
            MusicLibraryWorker restored({storage, {}});
            Check(restored.Restore(), "restore scheduled");
            results = Idle(restored);
            Check(results.size() == 1 && results[0].restored && results[0].action == Action::Restore &&
                results[0].restoredQueue && !results[0].restoredQueue->playing &&
                results[0].restoredQueue->status == lume::music::PlaybackStatus::Paused &&
                results[0].restoredQueue->position == 0.7 &&
                !results[0].restoredQueue->queue[0].source.empty(), "queue restores paused with native sources");
            const auto restoredTime = fs::last_write_time(storage / "queue.json");
            Check(restored.SaveQueue(*results[0].restoredQueue), "restored snapshot accepted");
            Idle(restored);
            Check(fs::last_write_time(storage / "queue.json") == restoredTime,
                "unchanged restored queue is not rewritten");
            Check(restored.Restore() && restored.Play(other), "play supersedes in-flight restore");
            results = Idle(restored);
            Check(std::none_of(results.begin(), results.end(), [](const auto& result) {
                return result.restoredQueue || result.action == Action::Restore;
            }) && std::count_if(results.begin(), results.end(), [](const auto& result) {
                return result.restored && result.action == Action::None;
            }) == 1 && results.back().action == Action::Play,
                "late restore preserves completion marker without overwriting newer user play");
            Write(first, "changed candidate");
            Check(restored.Play(other), "candidate window revalidation");
            results = Idle(restored);
            Check(results.back().playTracks.size() == 1 && results.back().playTracks[0].id == other &&
                !restored.Resolve(id), "play validates nonselected candidates before publishing native queue");
            Check(restored.Remove(id), "remove scheduled");
            Idle(restored);
            Check(!restored.Resolve(id) && fs::exists(first), "remove invalidates cache but preserves source");
            Write(second, "changed after restore");
            Check(restored.Page(0, 1), "memory-only page after file change");
            results = Idle(restored);
            Check(restored.Resolve(other).has_value() &&
                Json::parse(results.back().state).at("tracks")[0].at("available") == true,
                "page and display cache are not new playback authorization");
            Check(restored.Play(other), "changed source revalidation scheduled");
            results = Idle(restored);
            Check(results.back().error == "track-unavailable" && results.back().playTracks.empty() &&
                !restored.Resolve(other) && Json::parse(results.back().state).at("error") == "track-unavailable",
                "changed file rejected, invalidated and visible in state.error");
        }
        {
            const auto corrupt = root / "corrupt-worker-state";
            fs::create_directory(corrupt);
            Write(corrupt / "queue.json", "{broken");
            MusicLibraryWorker protectedQueue({corrupt, {}});
            Check(protectedQueue.Restore(), "corrupt restore request");
            results = Idle(protectedQueue);
            Check(results[0].restored && !results[0].restoredQueue &&
                results[0].error == "queue-unreadable" &&
                Json::parse(results[0].state).at("error") == "queue-unreadable",
                "corrupt queue completes restore with visible failure");
            Check(!protectedQueue.SaveQueue({}), "default save cannot replace corrupt queue");
            protectedQueue.Shutdown();
            Check(Read(corrupt / "queue.json") == "{broken", "shutdown preserves corrupt queue bytes");
            MusicLibraryWorker earlySave({corrupt, {}});
            Check(earlySave.SaveQueue({}), "save before restore is inspected on worker");
            earlySave.Shutdown();
            results = earlySave.Drain();
            Check(Read(corrupt / "queue.json") == "{broken" && results.size() == 1 &&
                Json::parse(results[0].state).at("error") == "queue-unreadable",
                "shutdown flush before Restore cannot destroy corrupt queue");
            MusicLibraryWorker repair({corrupt, {}});
            Check(repair.SaveQueue({}, true), "explicit repair authorized");
            Idle(repair);
            Check(Json::parse(Read(corrupt / "queue.json")).at("ids").empty(),
                "explicit retry repairs corrupted queue");
        }
        {
            const auto galleryRoot = root / "gallery-state";
            const auto image = Png();
            const auto picture = root / "cover.mp3";
            Mp3(picture, image);
            std::vector<std::wstring> paths;
            for (unsigned i = 0; i < 102; ++i) {
                const auto copy = root / ("cover-" + std::to_string(i) + ".mp3");
                fs::copy_file(picture, copy);
                paths.push_back(copy.wstring());
            }
            MusicLibraryWorker gallery({galleryRoot, {}});
            Check(gallery.Import(paths), "gallery fixture imported");
            results = Idle(gallery);
            Check(results.back().state.find(L"music-artwork:") == std::wstring::npos,
                "hidden state never exposes artwork tokens");
            gallery.SetVisible(true);
            Check(gallery.Page(0, 256), "large page requested");
            results = Idle(gallery);
            const auto page = Json::parse(results.back().state);
            std::set<std::string> origins, urls;
            std::vector<std::string> ids;
            for (const auto& item : page.at("tracks")) {
                ids.push_back(item.at("id").get<std::string>());
                const auto url = item.at("artwork").get<std::string>();
                if (url.empty()) continue;
                Check(url.rfind("http://127.0.0.1:", 0) == 0, "state contains only loopback artwork URL");
                origins.insert(url.substr(0, url.find('/', 7)));
                urls.insert(url);
            }
            Check(urls.size() == 100 && origins.size() == 1,
                "current page shares one server and caps leases at 100");
            const auto firstUrl = page.at("tracks")[0].at("artwork").get<std::string>();
            const auto overlapUrl = page.at("tracks")[1].at("artwork").get<std::string>();
            DWORD galleryStatus = 0;
            Check(!Fetch(firstUrl, L"https://lume.local", galleryStatus).empty() && galleryStatus == 200,
                "page lease serves actual PNG bytes");
            Check(gallery.Page(1, 100), "overlapping next page");
            results = Idle(gallery);
            const auto next = Json::parse(results.back().state);
            Check(next.at("tracks")[0].at("artwork") == overlapUrl &&
                !next.at("tracks")[99].at("artwork").get<std::string>().empty(),
                "overlapping leases reused, freed budget serves newly visible item");
            Fetch(firstUrl, L"https://lume.local", galleryStatus);
            Check(galleryStatus != 200, "off-page lease revoked");
            Check(gallery.Page(1, 100), "queued visible state before hide");
            gallery.SetVisible(false);
            results = Idle(gallery);
            for (const auto& result : results) {
                Check(result.artworkUrl.empty(), "hidden explicit result has no URL");
                if (!result.state.empty()) {
                    const auto hidden = Json::parse(result.state);
                    for (const auto& item : hidden.at("tracks"))
                        Check(item.at("artwork") == "", "hidden state cannot refill previous page leases");
                }
            }
            Fetch(overlapUrl, L"https://lume.local", galleryStatus);
            Check(galleryStatus != 200, "hide closes shared page server");
            gallery.SetVisible(true);
            Check(gallery.Page(0, 2), "show reconstructs small page");
            results = Idle(gallery);
            const auto shown = Json::parse(results.back().state);
            Check(shown.at("tracks")[0].at("artwork") != firstUrl &&
                !shown.at("tracks")[0].at("artwork").get<std::string>().empty(),
                "show uses new opaque tokens");
            Check(gallery.Artwork(ids[101]), "off-page explicit compatibility request");
            results = Idle(gallery);
            const auto explicitResult = std::find_if(results.begin(), results.end(), [](const auto& result) {
                return !result.artworkUrl.empty();
            });
            Check(explicitResult != results.end() &&
                explicitResult->artworkUrl.substr(0, explicitResult->artworkUrl.find('/', 7)) ==
                    shown.at("tracks")[0].at("artwork").get<std::string>().substr(0,
                        shown.at("tracks")[0].at("artwork").get<std::string>().find('/', 7)),
                "single-item compatibility shares the page server");
            const auto noisy = Png(true);
            Mp3(picture, noisy);
            for (unsigned i = 0; i < 60; ++i)
                fs::copy_file(picture, fs::path(paths[i]), fs::copy_options::overwrite_existing);
            Check(gallery.Import(std::vector<std::wstring>(paths.begin(), paths.begin() + 60)),
                "large PNG budget fixture imported");
            Idle(gallery);
            Check(gallery.Page(0, 60), "large PNG page");
            results = Idle(gallery);
            const auto budgetPage = Json::parse(results.back().state);
            std::size_t pngBytes = 0, leaseCount = 0, emptyCount = 0;
            for (const auto& item : budgetPage.at("tracks")) {
                const auto url = item.at("artwork").get<std::string>();
                if (url.empty()) { ++emptyCount; continue; }
                const auto body = Fetch(url, L"https://lume.local", galleryStatus);
                Check(galleryStatus == 200, "budgeted image request succeeds");
                pngBytes += body.size();
                ++leaseCount;
            }
            Check(leaseCount > 0 && emptyCount > 0 && pngBytes <= 8 * 1024 * 1024,
                "8 MiB resident PNG budget enforced with blank overflow entries");
            gallery.SetVisible(false);
            Idle(gallery);
        }
        {
            const auto directory = root / "page-timing";
            fs::create_directory(directory);
            MusicLibraryWorker measured({root / "page-timing-state", {}});
            std::vector<std::wstring> paths;
            for (unsigned i = 0; i < 512; ++i) {
                const auto path = directory / (std::to_string(i) + ".wav");
                Write(path, "RIFF");
                paths.push_back(path.wstring());
                if (paths.size() == 256) {
                    Check(measured.Import(std::move(paths)), "page timing imports");
                    Idle(measured);
                    paths.clear();
                }
            }
            const auto begin = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < 8; ++i) {
                Check(measured.Page(i * 50, 50), "page timing request");
                Idle(measured);
            }
            std::cout << "512-track library, 8 pages: " <<
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - begin).count() << " ms\n";
        }
        {
            std::mutex mutex;
            std::condition_variable condition;
            bool entered = false, release = false;
            MusicLibraryWorker bounded({root / "bounded", [&] {
                std::unique_lock<std::mutex> lock(mutex);
                if (entered) return;
                entered = true;
                condition.notify_all();
                condition.wait(lock, [&] { return release; });
            }});
            Check(bounded.Restore(), "bounded queue starts");
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait_for(lock, 15s, [&] { return entered; });
            }
            unsigned accepted = 0;
            while (accepted < 40 && bounded.Remove("local-1")) ++accepted;
            {
                std::lock_guard<std::mutex> lock(mutex);
                release = true;
                condition.notify_all();
            }
            bounded.Shutdown();
            Check(entered && accepted <= 31, "unconsumed operations have bounded backpressure");
        }
        // The only recursive removal is the exact directory reserved by this process.
        fs::remove_all(root);
        if (SUCCEEDED(com)) CoUninitialize();
        std::cout << "music library worker isolated checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\nFixture retained: " << root << '\n';
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }
}
