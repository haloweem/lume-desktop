#include "apps/shell/music_library_session.h"
#include "platform/windows/music_local_files.h"
#include "tests/unit/unique_test_directory.h"
#include "vendor/nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
namespace fs = std::filesystem;
bool passed = true;
void Check(bool value, const char* message) {
    if (!value) { passed = false; std::cerr << "FAIL: " << message << '\n'; }
}
void Write(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
}

int main() {
    using namespace lume::shell;
    const auto root = UniqueTestDirectory(fs::temp_directory_path(), "lume-library-");
    const auto storage = root / "state";
    const auto first = root / L"\u6d4b\u8bd5.mp3", second = root / "two.wav";
    Write(first, "ID3");
    Write(second, "RIFF");
    {
    MusicLibrarySession session({storage});
    Check(!fs::exists(storage) && session.State().find(L"\"loaded\":false") != std::wstring::npos,
        "construction and State do not touch disk");
    const auto imported = session.Import({first.wstring(), (root / "missing.mp3").wstring()});
    Check(imported.added == 1 && imported.failures.size() == 1 && imported.error.empty(),
        "per-file failures do not discard valid imports");
    Check(session.Import({second.wstring()}).added == 1 && session.Tracks().size() == 2,
        "imports accumulate");
    Check(session.Import({first.wstring(), first.wstring()}).added == 0 && session.Tracks().size() == 2,
        "repeat paths are deduplicated");
    const auto original = session.Tracks();
    Check(!original[0].source.empty(), "native tracks retain sources");
    const auto state = session.State();
    Check(state.find(root.wstring()) == std::wstring::npos &&
        state.find(L"\"source\":\"\"") != std::wstring::npos, "bridge tracks redact sources");
    lume::music::MusicSnapshot snapshot;
    snapshot.queue = original;
    snapshot.currentIndex = 1;
    snapshot.current = original[1];
    snapshot.position = 0.5;
    snapshot.shuffle = true;
    Check(session.SaveQueue(snapshot), "explicit queue save");
    {
        MusicLibrarySession restart({storage});
        Check(restart.Tracks().size() == 2, "index restores from disk");
        const auto queue = restart.RestoredQueue();
        Check(queue && queue->queue.size() == 2 && queue->currentIndex == 1 && !queue->playing,
            "queue restores separately without autoplay");
        Check(restart.Remove(original[0].id) && fs::exists(first), "library remove never deletes original");
    }
    {
        MusicLibrarySession restart({storage});
        Check(restart.Tracks().size() == 1 && restart.RestoredQueue()->queue.size() == 1,
            "removed entry stays removed despite old queue references");
    }
    fs::rename(second, root / "moved.wav");
    {
        MusicLibrarySession restart({storage});
        Check(restart.Tracks().empty(), "missing source is not authorized on restart");
        Check(restart.Import({(root / "moved.wav").wstring()}).updated == 1,
            "explicit moved file reimport repairs identity");
        Check(restart.Tracks().front().id == original[1].id, "move preserves saved queue ID");
    }
    {
        MusicLibrarySession restart({storage});
        const auto before = restart.Tracks();
        HANDLE lock = CreateFileW((storage / "library.json").c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(lock != INVALID_HANDLE_VALUE, "lock index fixture");
        const auto result = restart.Import({first.wstring()});
        Check(!result.error.empty() && restart.Tracks().size() == before.size(),
            "failed atomic replacement leaves memory and previous index intact");
        CloseHandle(lock);
    }
    Write(storage / "queue.json", "{broken");
    {
        MusicLibrarySession restart({storage});
        Check(restart.Tracks().size() == 1 && !restart.RestoredQueue(),
            "corrupt queue does not block library restore");
        Check(restart.SaveQueue({}), "explicit queue save repairs corrupt queue independently");
    }
    Write(storage / "library.json", "{broken");
    {
        MusicLibrarySession restart({storage});
        Check(!restart.Restore() && !restart.Import({first.wstring()}).error.empty(),
            "corrupt index is preserved rather than silently overwritten");
    }
    Check(lume::music::InspectLocalMusicFile(first.wstring() + L":hidden").error == "invalid-path",
        "alternate data streams rejected");
    Check(lume::music::InspectLocalMusicFile(L"\\\\server\\share\\song.mp3").error == "invalid-path",
        "network paths rejected before access");
    HANDLE lock = CreateFileW(first.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(!lume::music::InspectLocalMusicFile(first.wstring()).error.empty(),
        "read rights and sharing violations checked with an actual open");
    CloseHandle(lock);
    }
    const auto scan = root / "scan";
    fs::create_directories(scan / "nested");
    Write(scan / "one.mp3", "ID3");
    Write(scan / "nested" / "two.wav", "RIFF");
    Write(scan / "ignored.txt", "text");
    Check(CreateHardLinkW((scan / "alias.mp3").c_str(), (scan / "one.mp3").c_str(), nullptr),
        "create hardlink fixture");
    {
        MusicLibrarySession folder({root / "folder-state", 2});
        const auto result = folder.Import({scan.wstring()});
        Check(result.added == 2 && result.unchanged == 1 && folder.Tracks().size() == 2,
            "explicit bounded directory scan deduplicates hardlinks and ignores unrelated files");
        Check(folder.Import({first.wstring()}).failures.front().error == "library-full" &&
            folder.Tracks().size() == 2, "full library does not evict or grow");
        const auto state = folder.State(1, 1);
        Check(state.find(L"\"total\":2") != std::wstring::npos &&
            state.find(L"\"offset\":1") != std::wstring::npos, "bridge state is paginated");
        const auto id = folder.Tracks().front().id;
        Check(!folder.Artwork(id) && !folder.Artwork("../outside"), "cover misses and hostile IDs return no resource");
        Check(folder.Resolve(id).has_value() && !folder.Resolve("provider-track"),
            "native resolution authorizes only library identities");
    }
    {
        bool cancel = true;
        MusicLibrarySession cancelled({root / "cancel-state", 10, [&] { return cancel; }});
        Check(cancelled.Import({first.wstring()}).error == "cancelled" &&
            !fs::exists(root / "cancel-state" / "library.json"), "cancelled import never publishes an index");
        cancel = false;
        Check(cancelled.Import({first.wstring()}).added == 1, "session remains usable after cancellation");
    }
    {
        MusicLibrarySession left({root / "conflict-state"}), right({root / "conflict-state"});
        Check(left.Restore() && right.Restore(), "two independent index readers");
        Check(left.Import({first.wstring()}).added == 1, "first writer publishes");
        const auto result = right.Import({(root / "moved.wav").wstring()});
        Check(result.error == "index-conflict" && right.Tracks().empty(),
            "stale writer cannot overwrite another session's index");
    }
    {
        MusicLibrarySession changed({root / "changed-state"});
        Check(changed.Import({first.wstring()}).added == 1, "change detection fixture imported");
    }
    Write(first, "changed content");
    {
        MusicLibrarySession changed({root / "changed-state"});
        Check(changed.Tracks().empty() && changed.State().find(L"\"error\":\"changed\"") != std::wstring::npos,
            "changed source is unavailable until explicitly reimported");
        Check(changed.Import({first.wstring()}).updated == 1 && changed.Tracks().size() == 1,
            "explicit reimport refreshes changed file");
    }
    const auto large = root / "large.mp3";
    Write(large, "x");
    fs::resize_file(large, 512ull * 1024 * 1024 + 1);
    Check(lume::music::InspectLocalMusicFile(large.wstring()).error == "file-too-large",
        "oversized audio rejected before metadata");
    const auto indexRoot = root / "oversize-state";
    fs::create_directories(indexRoot);
    Write(indexRoot / "library.json", "x");
    fs::resize_file(indexRoot / "library.json", lume::music::kMaxLibraryIndexBytes + 1);
    {
        MusicLibrarySession oversize({indexRoot});
        Check(!oversize.Restore(), "oversized disk index rejected before allocation");
    }
    const auto linked = root / "linked.mp3";
    if (CreateSymbolicLinkW(linked.c_str(), first.c_str(), 2)) {
        Check(!lume::music::InspectLocalMusicFile(linked.wstring()).error.empty(), "file reparse points rejected");
    } else {
        std::cout << "SKIP: symbolic link fixture requires Windows developer mode or privilege\n";
    }
    // Every deletion is confined to the directory reserved by this test.
    fs::remove_all(root);
    if (!passed) return 1;
    std::cout << "music library session persistence/security checks passed\n";
}
