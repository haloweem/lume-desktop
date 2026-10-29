#include "apps/shell/music_library_session.h"
#include "platform/windows/text_util.h"

#include "platform/windows/music_local_files.h"
#include "platform/windows/music_metadata.h"
#include "vendor/nlohmann/json.hpp"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <set>
#include <utility>

namespace lume::shell {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;

constexpr std::size_t kMaxImportPaths = 256;
constexpr std::size_t kMaxScanFiles = 1024;
constexpr std::size_t kMaxScanEntries = 16384;
constexpr std::size_t kMaxArtworkFiles = 256;
constexpr std::uint64_t kMaxArtworkDiskBytes = 64ull * 1024 * 1024;

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool Regular(bool singleLink = true) const {
        BY_HANDLE_FILE_INFORMATION info{};
        return value != INVALID_HANDLE_VALUE && GetFileType(value) == FILE_TYPE_DISK &&
            GetFileInformationByHandle(value, &info) &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
            (!singleLink || info.nNumberOfLinks == 1);
    }
};

struct Directories {
    std::vector<HANDLE> handles;
    ~Directories() { for (auto h : handles) CloseHandle(h); }
    bool Pin(const fs::path& path, bool create) {
        if (!music::IsLocalMusicPath(path.wstring())) return false;
        std::vector<fs::path> parts{path.root_path()};
        for (const auto& part : path.relative_path()) {
            if (part == L"." || part == L"..") return false;
            parts.push_back(parts.back() / part);
        }
        if (parts.size() > 128) return false;
        for (const auto& part : parts) {
            auto handle = CreateFileW(part.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE && create &&
                (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) {
                if (!CreateDirectoryW(part.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
                handle = CreateFileW(part.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            }
            if (handle == INVALID_HANDLE_VALUE) return false;
            handles.push_back(handle);
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(handle, &info) ||
                !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        }
        return true;
    }
};

enum class ReadResult { Missing, Ok, Error };

ReadResult Read(const fs::path& path, std::size_t limit, std::string& bytes) {
    bytes.clear();
    Handle file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file.value == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? ReadResult::Missing : ReadResult::Error;
    LARGE_INTEGER size{};
    if (!file.Regular() || !GetFileSizeEx(file.value, &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > limit) return ReadResult::Error;
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
        read != bytes.size()) { bytes.clear(); return ReadResult::Error; }
    return ReadResult::Ok;
}

bool Lock(const fs::path& root, Handle& lock) {
    lock.value = CreateFileW((root / "library.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    return lock.Regular();
}

bool AtomicWrite(const fs::path& target, const std::string& bytes, std::size_t limit) {
    if (bytes.size() > limit) return false;
    const auto attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) return false;
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return false;
    wchar_t name[40]{};
    if (!StringFromGUID2(guid, name, 40)) return false;
    const auto temporary = target.parent_path() / (std::wstring(name) + L".tmp");
    bool written = false;
    {
        Handle file;
        file.value = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file.value == INVALID_HANDLE_VALUE) return false;
        DWORD actual = 0;
        written = file.Regular() &&
            WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &actual, nullptr) &&
            actual == bytes.size() && FlushFileBuffers(file.value);
    }
    if (written && MoveFileExW(temporary.c_str(), target.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(temporary.c_str());
    return false;
}

bool AudioExtension(const fs::path& path) {
    auto extension = path.extension().wstring();
    for (auto& c : extension) c = static_cast<wchar_t>(towlower(c));
    return extension == L".mp3" || extension == L".wav" || extension == L".flac" ||
        extension == L".m4a" || extension == L".aac" || extension == L".ogg" || extension == L".wma";
}

using lume::text::Wide;

bool CacheName(const std::wstring& name) {
    if (name.rfind(L"local-", 0) != 0 || name.size() < 11 || name.size() > 256 ||
        name.substr(name.size() - 4) != L".png") return false;
    return name.find_first_not_of(L"0123456789-", 6) >= name.size() - 4;
}

fs::path ArtworkPath(const fs::path& root, const music::LibraryEntry& entry) {
    const auto name = entry.track.id + "-" + std::to_string(entry.bytes) + "-" +
        std::to_string(entry.modified) + ".png";
    return root / "artwork" / fs::u8path(name);
}

bool Png(const std::string& bytes) {
    return bytes.size() >= 8 && bytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0;
}
}

struct MusicLibrarySession::Impl {
    explicit Impl(MusicLibrarySessionOptions input)
        : options(std::move(input)), library(options.maxTracks) {}

    bool Initialize() {
        if (directories) return true;
        auto pins = std::make_unique<Directories>();
        if (!pins->Pin(options.root, true) || !pins->Pin(options.root / "artwork", true)) {
            error = "storage-unavailable"; return false;
        }
        directories = std::move(pins);
        return true;
    }

    void Revalidate() {
        for (const auto& entry : library.Entries()) {
            const auto file = music::InspectLocalMusicFile(Wide(entry.track.source));
            auto unavailable = file.error;
            if (unavailable.empty() && (file.identity != entry.identity ||
                file.bytes != entry.bytes || file.modified != entry.modified)) unavailable = "changed";
            library.SetAvailability(entry.track.id, std::move(unavailable));
        }
    }

    bool Restore() {
        if (loaded) return true;
        if (!Initialize()) return false;
        Handle lock;
        if (!Lock(options.root, lock)) { error = "storage-busy"; return false; }
        std::string bytes;
        const auto read = Read(options.root / "library.json", music::kMaxLibraryIndexBytes, bytes);
        if (read == ReadResult::Error || (read == ReadResult::Ok && !library.Decode(bytes))) {
            error = "index-unreadable"; return false;
        }
        indexBytes = std::move(bytes);
        indexExists = read == ReadResult::Ok;
        Revalidate();
        loaded = true;
        error.clear();
        TrimArtwork();
        return true;
    }

    bool Commit(music::MusicLibrary& next) {
        const auto bytes = next.Encode();
        if (bytes.size() > music::kMaxLibraryIndexBytes) { error = "index-too-large"; return false; }
        Handle lock;
        if (!Lock(options.root, lock)) { error = "storage-busy"; return false; }
        std::string current;
        const auto state = Read(options.root / "library.json", music::kMaxLibraryIndexBytes, current);
        if (state == ReadResult::Error) { error = "index-unreadable"; return false; }
        if ((state == ReadResult::Ok) != indexExists || current != indexBytes) {
            error = "index-conflict"; return false;
        }
        if (!AtomicWrite(options.root / "library.json", bytes, music::kMaxLibraryIndexBytes)) {
            error = "index-save-failed"; return false;
        }
        library = std::move(next);
        indexBytes = bytes;
        indexExists = true;
        error.clear();
        return true;
    }

    bool TrimArtwork(std::size_t reserveFiles = 0, std::uint64_t reserveBytes = 0) {
        struct Item { fs::path path; std::uint64_t bytes; fs::file_time_type time; };
        std::vector<Item> items;
        std::uint64_t total = 0;
        std::error_code ec;
        for (fs::directory_iterator it(options.root / "artwork", ec), end; !ec && it != end; it.increment(ec)) {
            if (!CacheName(it->path().filename().wstring())) continue;
            const auto attr = GetFileAttributesW(it->path().c_str());
            if (attr == INVALID_FILE_ATTRIBUTES || (attr & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
                continue;
            const auto bytes = it->file_size(ec);
            if (ec) break;
            const auto time = it->last_write_time(ec);
            if (ec) break;
            items.push_back({it->path(), bytes, time});
            total += bytes;
            // Bound enumeration even if an external actor fills this dedicated cache.
            if (items.size() == kMaxScanEntries) return false;
        }
        if (ec) return false;
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
        std::size_t remaining = items.size();
        const auto expiry = fs::file_time_type::clock::now() - std::chrono::hours(24 * 30);
        for (const auto& item : items) {
            if (remaining + reserveFiles <= kMaxArtworkFiles && total + reserveBytes <= kMaxArtworkDiskBytes &&
                item.bytes <= music::kMaxMusicArtworkBytes && item.time >= expiry) continue;
            if (DeleteFileW(item.path.c_str())) { total -= item.bytes; --remaining; }
        }
        return remaining + reserveFiles <= kMaxArtworkFiles && total + reserveBytes <= kMaxArtworkDiskBytes;
    }

    bool CacheArtwork(const music::LibraryEntry& entry, const std::string& png) {
        Handle lock;
        return Png(png) && png.size() <= music::kMaxMusicArtworkBytes && Lock(options.root, lock) &&
            TrimArtwork(1, png.size()) &&
            AtomicWrite(ArtworkPath(options.root, entry), png, music::kMaxMusicArtworkBytes);
    }

    MusicLibraryImportResult Import(const std::vector<std::wstring>& paths) {
        MusicLibraryImportResult result;
        if (paths.empty() || paths.size() > kMaxImportPaths) {
            result.error = paths.empty() ? "empty" : "too-many-paths"; return result;
        }
        if (!Restore()) { result.error = error; return result; }
        auto next = library;
        std::set<std::string> visited;
        std::size_t examined = 0, scanned = 0;
        bool cancelled = false, scanLimit = false;
        const auto stop = [&] {
            if (options.cancelled && options.cancelled()) cancelled = true;
            return cancelled || scanLimit;
        };
        const auto fail = [&](std::size_t input, const std::string& reason) {
            if (result.failures.size() < kMaxScanFiles) result.failures.push_back({input, reason});
        };
        const auto add = [&](const fs::path& path, std::size_t input) {
            if (stop()) return;
            if (++examined > kMaxScanFiles) { scanLimit = true; fail(input, "scan-limit"); return; }
            auto file = music::InspectLocalMusicFile(path.wstring());
            if (!file.error.empty()) { fail(input, file.error); return; }
            if (!visited.insert(file.identity).second) { ++result.unchanged; return; }
            const auto* existing = next.FindIdentity(file.identity);
            music::LibraryEntry entry;
            entry.track = file.track;
            entry.identity = file.identity; entry.bytes = file.bytes; entry.modified = file.modified;
            if (existing && existing->bytes == file.bytes && existing->modified == file.modified) {
                entry.track = existing->track;
                entry.track.source = file.track.source;
            } else {
                if (!existing && next.Entries().size() >= next.Capacity()) { fail(input, "library-full"); return; }
                if (existing) entry.track.id = existing->track.id;
                const auto metadata = music::ReadMusicMetadata(Wide(file.track.source));
                if (!metadata.warning.empty()) result.warnings.push_back({input, metadata.warning});
                if (!metadata.title.empty()) entry.track.title = metadata.title;
                entry.track.artist = metadata.artist;
                entry.track.album = metadata.album;
                entry.track.duration = metadata.duration;
                if (!metadata.artworkPng.empty()) {
                    if (CacheArtwork(entry, metadata.artworkPng)) entry.track.artwork = "music-artwork:" + entry.track.id;
                    else result.warnings.push_back({input, "artwork-save-failed"});
                }
            }
            switch (next.Upsert(std::move(entry))) {
            case music::LibraryChange::Added: ++result.added; break;
            case music::LibraryChange::Updated: ++result.updated; break;
            case music::LibraryChange::Unchanged: ++result.unchanged; break;
            case music::LibraryChange::Full: fail(input, "library-full"); break;
            case music::LibraryChange::Invalid: fail(input, "invalid-track"); break;
            }
        };
        for (std::size_t input = 0; input < paths.size() && !stop(); ++input) {
            if (!music::IsLocalMusicPath(paths[input])) { fail(input, "invalid-path"); continue; }
            const auto path = fs::path(paths[input]).lexically_normal();
            const auto attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                add(path, input); continue;
            }
            std::vector<std::pair<fs::path, unsigned>> pending{{path, 0}};
            while (!pending.empty() && !stop()) {
                const auto [directory, depth] = std::move(pending.back());
                pending.pop_back();
                Directories pins;
                if (!pins.Pin(directory, false)) { fail(input, "invalid-path"); continue; }
                std::error_code ec;
                for (fs::directory_iterator it(directory, ec), end; !ec && it != end && !stop(); it.increment(ec)) {
                    if (++scanned > kMaxScanEntries) { scanLimit = true; fail(input, "scan-limit"); break; }
                    const auto attr = GetFileAttributesW(it->path().c_str());
                    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
                        fail(input, "invalid-path");
                    } else if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                        if (depth >= 16) fail(input, "scan-depth");
                        else pending.emplace_back(it->path(), depth + 1);
                    } else if (AudioExtension(it->path())) {
                        add(it->path(), input);
                    }
                }
                if (ec) fail(input, "scan-access-denied");
            }
        }
        if (options.cancelled && options.cancelled()) cancelled = true;
        if (cancelled) { result.error = error = "cancelled"; result.added = result.updated = 0; return result; }
        if ((result.added || result.updated) && !Commit(next)) {
            result.error = error;
            result.added = result.updated = 0;
        } else error.clear();
        return result;
    }

    bool SaveQueue(const music::MusicSnapshot& snapshot) {
        if (!Restore()) return false;
        const auto bytes = library.EncodeQueue(snapshot);
        if (!bytes) { error = "invalid-queue"; return false; }
        Handle lock;
        if (!Lock(options.root, lock)) { error = "storage-busy"; return false; }
        if (!AtomicWrite(options.root / "queue.json", *bytes, music::kMaxLibraryQueueBytes)) {
            error = "queue-save-failed"; return false;
        }
        queueBytes = *bytes;
        queueRead = true;
        error.clear();
        return true;
    }

    std::optional<music::MusicSnapshot> RestoredQueue() {
        if (!Restore()) return {};
        if (!queueRead) {
            const auto state = Read(options.root / "queue.json", music::kMaxLibraryQueueBytes, queueBytes);
            if (state == ReadResult::Error) { error = "queue-unreadable"; return {}; }
            queueRead = true;
        }
        if (queueBytes.empty()) return {};
        Revalidate();
        const auto result = library.DecodeQueue(queueBytes);
        if (!result) error = "queue-unreadable";
        return result;
    }

    std::optional<std::string> Artwork(std::string_view id) {
        if (!Restore()) return {};
        const auto* entry = library.Find(id);
        if (!entry || !entry->unavailable.empty()) return {};
        const auto file = music::InspectLocalMusicFile(Wide(entry->track.source));
        if (!file.error.empty() || file.identity != entry->identity || file.bytes != entry->bytes ||
            file.modified != entry->modified) return {};
        const auto path = ArtworkPath(options.root, *entry);
        std::string png;
        if (Read(path, music::kMaxMusicArtworkBytes, png) == ReadResult::Ok && Png(png)) {
            Handle touch;
            touch.value = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr,
                OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            FILETIME now{};
            GetSystemTimeAsFileTime(&now);
            if (touch.Regular()) SetFileTime(touch.value, nullptr, nullptr, &now);
            return png;
        }
        const auto metadata = music::ReadMusicMetadata(Wide(entry->track.source));
        if (metadata.artworkPng.empty()) return {};
        CacheArtwork(*entry, metadata.artworkPng);
        return metadata.artworkPng;
    }

    std::wstring State(std::size_t offset, std::size_t limit) const {
        Json tracks = Json::array();
        const auto& entries = library.Entries();
        limit = std::min<std::size_t>(limit, 256);
        const auto begin = std::min(offset, entries.size()), end = begin + std::min(limit, entries.size() - begin);
        for (auto i = begin; i < end; ++i) {
            const auto& entry = entries[i];
            const auto& track = entry.track;
            tracks.push_back({{"id", track.id}, {"title", track.title}, {"artist", track.artist},
                {"album", track.album}, {"source", ""}, {"artwork", track.artwork},
                {"duration", track.duration}, {"available", entry.unavailable.empty()},
                {"error", entry.unavailable}});
        }
        return Wide(Json{{"version", 1}, {"scope", "musicLibrary"}, {"type", "state"}, {"loaded", loaded},
            {"error", error}, {"total", entries.size()}, {"offset", begin}, {"limit", limit},
            {"tracks", std::move(tracks)}}.dump());
    }

    MusicLibrarySessionOptions options;
    music::MusicLibrary library;
    std::unique_ptr<Directories> directories;
    std::string indexBytes, queueBytes, error;
    bool loaded = false, indexExists = false, queueRead = false;
};

MusicLibrarySession::MusicLibrarySession(MusicLibrarySessionOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
MusicLibrarySession::~MusicLibrarySession() = default;
bool MusicLibrarySession::Restore() {
    try { return impl_->Restore(); } catch (...) { impl_->error = "index-unreadable"; return false; }
}
MusicLibraryImportResult MusicLibrarySession::Import(const std::vector<std::wstring>& paths) {
    try { return impl_->Import(paths); }
    catch (...) {
        impl_->error = "import-failed";
        MusicLibraryImportResult result; result.error = impl_->error; return result;
    }
}
std::vector<music::Track> MusicLibrarySession::Tracks() {
    if (!Restore()) return {};
    impl_->Revalidate();
    return impl_->library.Tracks();
}
std::optional<music::Track> MusicLibrarySession::Resolve(std::string_view id) {
    if (!Restore()) return {};
    const auto* entry = impl_->library.Find(id);
    if (!entry) return {};
    const auto file = music::InspectLocalMusicFile(Wide(entry->track.source));
    auto unavailable = file.error;
    if (unavailable.empty() && (file.identity != entry->identity ||
        file.bytes != entry->bytes || file.modified != entry->modified)) unavailable = "changed";
    impl_->library.SetAvailability(id, unavailable);
    return unavailable.empty() ? std::optional<music::Track>(entry->track) : std::nullopt;
}
bool MusicLibrarySession::Remove(std::string_view id) {
    if (!Restore()) return false;
    auto next = impl_->library;
    if (!next.Remove(id)) { impl_->error = "not-found"; return false; }
    try { return impl_->Commit(next); } catch (...) { impl_->error = "index-save-failed"; return false; }
}
bool MusicLibrarySession::SaveQueue(const music::MusicSnapshot& snapshot) {
    try { return impl_->SaveQueue(snapshot); } catch (...) { impl_->error = "queue-save-failed"; return false; }
}
std::optional<music::MusicSnapshot> MusicLibrarySession::RestoredQueue() {
    try { return impl_->RestoredQueue(); } catch (...) { impl_->error = "queue-unreadable"; return {}; }
}
std::wstring MusicLibrarySession::State(std::size_t offset, std::size_t limit) const {
    return impl_->State(offset, limit);
}
std::optional<std::string> MusicLibrarySession::Artwork(std::string_view id) {
    try { return impl_->Artwork(id); } catch (...) { impl_->error = "artwork-unavailable"; return {}; }
}
const std::string& MusicLibrarySession::LastError() const noexcept { return impl_->error; }

}  // namespace lume::shell
