#include "core/music/music_lyrics.h"
#include "platform/windows/music_local_lyrics.h"
#include "core/ui/music_page_protocol.h"
#include "vendor/nlohmann/json.hpp"

#include <windows.h>
#include <winioctl.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <cstring>

namespace {

bool passed = true;

void Check(bool value, const char* message) {
    if (!value) {
        passed = false;
        std::cerr << "FAIL: " << message << "\n";
    }
}

std::filesystem::path FixturePath(const char* extension) {
    const auto root = std::filesystem::temp_directory_path() / "lume-music-lyrics-test";
    std::filesystem::create_directories(root);
    return root / (std::string("track") + extension);
}

void Write(const std::filesystem::path& path, const std::string& value) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << value;
}

lume::music::Track TrackFor(const std::filesystem::path& path) {
    lume::music::Track track;
    track.id = "local-test-track";
    track.source = path.u8string();
    track.duration = 42.0;
    return track;
}

void TestParsesTimelineAndBoundsLastLine() {
    const auto audio = FixturePath(".mp3");
    Write(audio, "audio fixture");
    Write(std::filesystem::path(audio).replace_extension(".lrc"),
        "[ar:Fixture]\n"
        "[00:01.20][00:03.500]First line\n"
        "[00:05.00]Second line\n");

    const auto result = lume::music::LoadLocalLyrics(TrackFor(audio));
    Check(result.error.empty(), "valid LRC has no error");
    Check(result.lines.size() == 3, "multiple timestamps become timeline lines");
    if (result.lines.size() != 3) return;
    Check(result.lines[0].startTime == 1.2, "timestamp uses seconds");
    Check(result.lines[0].endTime == 3.5, "line ends at the next timestamp");
    Check(result.lines[1].endTime == 5.0, "duplicate timestamp group ends at next distinct timestamp");
    Check(result.lines[2].endTime == 42.0, "last line ends at track duration");
    Check(result.lines[0].words.empty() && result.lines[0].translation.empty(),
        "local parser emits no unsupported word or translation data");
}

void TestRejectsFilesOutsideReadLimits() {
    const auto audio = FixturePath("-limits.mp3");
    Write(audio, "audio fixture");
    const auto lyric = std::filesystem::path(audio).replace_extension(".lrc");
    Write(lyric,
        std::string(lume::music::kLyricsMaxLineBytes + 1, 'x') + "\n");
    Check(lume::music::LoadLocalLyrics(TrackFor(audio)).lines.empty(),
        "overlong line is rejected");

    std::string manyLines;
    for (std::size_t index = 0; index <= lume::music::kLyricsMaxLines; ++index)
        manyLines += "[00:01.00]x\n";
    Write(lyric, manyLines);
    Check(lume::music::LoadLocalLyrics(TrackFor(audio)).lines.empty(),
        "excessive line count is rejected");

    Write(lyric, std::string(lume::music::kLyricsMaxFileBytes + 1, 'x'));
    Check(lume::music::LoadLocalLyrics(TrackFor(audio)).lines.empty(),
        "oversized file is rejected before parsing");
}

void TestOnlyCurrentImportedLocalTrackCanResolveSidecar() {
    const auto audio = FixturePath("-scope.mp3");
    Write(audio, "audio fixture");
    Write(std::filesystem::path(audio).replace_extension(".lrc"), "[00:00.10]local\n");

    auto providerTrack = TrackFor(audio);
    providerTrack.id = "remote-track";
    Check(lume::music::LoadLocalLyrics(providerTrack).lines.empty(),
        "non-local track cannot resolve a local sidecar");

    auto arbitraryPath = TrackFor(audio);
    arbitraryPath.source = (audio.parent_path() / "other.mp3").u8string();
    Check(lume::music::LoadLocalLyrics(arbitraryPath).lines.empty(),
        "missing current imported source cannot be redirected to another sidecar");
}

void TestParsingRejectsMalformedAndUnboundedInput() {
    using namespace lume::music;
    const auto sorted = ParseLrc("\xEF\xBB\xBF[00:05.100]later\r\n[00:01.2]first\n[offset:+100]\n", 10);
    Check(sorted.lines.size() == 2 && sorted.lines[0].startTime == 1.3 &&
        sorted.lines[1].startTime == 5.2, "UTF-8 BOM, CRLF, stable sorting and global millisecond offset");
    Check(ParseLrc("[00:61]bad\n[1440:00]too late\n[00:01.xxx]bad\n", 0).lines.empty(),
        "malformed and out of range timestamps are ignored");
    Check(ParseLrc("[00:01]bad\xFF", 10).lines.empty(), "invalid UTF-8 cannot reach JSON encoder");
    const auto negative = ParseLrc("[offset:-2000]\n[00:01]early\n[00:03]late\n", 10);
    Check(negative.lines.size() == 2 && negative.lines[0].startTime == 0 &&
        negative.lines[1].startTime == 1, "negative offsets clamp to zero");
    const auto bounded = ParseLrc("[00:01]first\n[00:05]at end\n[00:06]too late\n", 5);
    Check(bounded.lines.size() == 1 && bounded.lines[0].endTime == 5,
        "timestamps outside a known track duration are omitted");
    const auto unknown = ParseLrc("[00:01]last\n", 0);
    Check(unknown.lines.size() == 1 && unknown.lines[0].endTime == 6,
        "unknown duration uses a bounded five second last line");
    Check(LoadLocalLyrics(TrackFor(FixturePath(".mp3")), [] { return true; }).lines.empty(),
        "cancelled read returns no lyrics");
}

void TestParserExpansionAndProtocolBudgets() {
    using namespace lume::music;
    Check(kLyricsMaxLines == 2000, "lyrics budget remains at the original 2000 lines");
    std::string physical;
    for (int index = 0; index < 2000; ++index) physical += "[00:01]x\n";
    Check(ParseLrc(physical).lines.size() == 1, "exactly 2000 physical lines are accepted");
    Check(ParseLrc(physical + "[00:02]x\n").lines.empty(), "2001 physical lines are rejected");
    lume::ui::MusicLyricsMessage overBudget{1, "local-test", {}};
    for (int index = 0; index < 2001; ++index)
        overBudget.lines.push_back({double(index), double(index + 1), "x", {}, {}});
    Check(lume::ui::EncodeMusicLyrics(overBudget).find(L"\"lines\":[]") != std::wstring::npos,
        "wire encoder enforces 2000 line budget independently");
    Check(!lume::ui::ParseMusicPageJson(L"{\"version\":1,\"scope\":\"music\",\"action\":\"loadLyrics\",\"id\":\"local-test\"}"),
        "lyric requests require an explicit session");
    Check(!lume::ui::ParseMusicPageJson(L"{\"version\":1,\"scope\":\"music\",\"action\":\"loadLyrics\",\"session\":9007199254740992,\"id\":\"local-test\"}"),
        "lyric session must be a JS safe integer");
    const std::string text(2000, 'x');
    std::string expanded;
    for (int index = 0; index < 200; ++index) expanded += "[00:01]";
    expanded += text;
    Check(ParseLrc(expanded).lines.empty(), "multi-timestamp expansion cannot exceed total text budget");
    std::string tags;
    for (std::size_t index = 0; index < kLyricsMaxLines; ++index) tags += "[00:01][00:02]x\n";
    Check(ParseLrc(tags).lines.empty(), "expanded timestamp count is bounded independently of physical lines");
    const auto duplicate = ParseLrc("[00:01]first\n[00:01]duplicate\n[00:02]\n", 3);
    Check(duplicate.lines.size() == 2 && duplicate.lines[0].fullText == "first" && duplicate.lines[1].fullText.empty(),
        "duplicate times keep first line and empty timed lines clear the display");
    const auto late = ParseLrc("[offset:1000]\n[1439:59.500]outside\n", 0);
    Check(late.lines.empty(), "offset cannot push time beyond the one day ceiling");

    const auto frame = lume::ui::EncodeMusicLyrics({7, "local-test", {{1.25, 3.5, "quote: \"", {}, {}}}});
    const auto json = nlohmann::json::parse(frame);
    Check(json.size() == 5 && json["type"] == "music-lyrics" && json["session"] == 7 &&
        json["trackId"] == "local-test" && json["lines"][0]["startTime"] == 1.25 &&
        json["lines"][0]["words"].empty() && json["lines"][0]["translation"] == "",
        "native encoder emits the exact bridge contract");
    const auto invalid = lume::ui::EncodeMusicLyrics({7, "local-test", {{2, 1, "bad", {}, {}}}});
    Check(invalid.find(L"\"lines\":[]") != std::wstring::npos, "encoder fails closed for an invalid timeline");
    Check(!lume::ui::ParseMusicPageJson(L"{\"version\":1,\"scope\":\"music\",\"action\":\"loadLyrics\",\"id\":\"local-test\",\"path\":\"C:/secret.lrc\"}"),
        "page protocol never accepts a lyrics filesystem path");
}

void TestSidecarDoesNotFollowSymlinks() {
    const auto audio = FixturePath("-link.mp3");
    const auto link = std::filesystem::path(audio).replace_extension(".lrc");
    const auto outside = FixturePath("-outside.txt");
    Write(audio, "audio fixture");
    Write(outside, "[00:01]outside lyric\n");
    const bool created = CreateSymbolicLinkW(link.c_str(), outside.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
    if (created) Check(lume::music::LoadLocalLyrics(TrackFor(audio)).lines.empty(), "reader rejects reparse-point sidecars");
    else std::cout << "symlink fixture unavailable on this Windows account\n";
    auto remote = TrackFor(audio);
    remote.source = "https://example.invalid/track.mp3";
    Check(lume::music::LoadLocalLyrics(remote).lines.empty(), "remote source cannot trigger file reads");
}

void TestSourceIdentityIsPinnedUntilSidecarReadCompletes() {
    const auto audio = FixturePath("-pinned.mp3");
    const auto moved = FixturePath("-pinned-moved.mp3");
    Write(audio, "audio fixture");
    Write(std::filesystem::path(audio).replace_extension(".lrc"), "[00:01]pinned source\n");
    int cancellationChecks = 0;
    bool renamedDuringRead = false;
    const auto result = lume::music::LoadLocalLyrics(TrackFor(audio), [&] {
        if (++cancellationChecks == 2) renamedDuringRead = MoveFileW(audio.c_str(), moved.c_str()) != FALSE;
        return false;
    });
    Check(cancellationChecks >= 2, "fixture attempts source replacement during sidecar read");
    Check(!renamedDuringRead, "source cannot be replaced after path validation while sidecar is being read");
    Check(result.lines.size() == 1, "source pin allows ordinary sidecar reading");
    if (renamedDuringRead) MoveFileW(moved.c_str(), audio.c_str());
    Check(MoveFileW(audio.c_str(), moved.c_str()) != FALSE, "reader releases its source handle after completing");
    MoveFileW(moved.c_str(), audio.c_str());
}

bool Junction(const std::filesystem::path& link, const std::filesystem::path& target) {
    std::filesystem::create_directory(link);
    const auto handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    struct MountPoint {
        DWORD tag;
        WORD dataLength, reserved;
        WORD substituteOffset, substituteLength, printOffset, printLength;
        wchar_t path[2048];
    } data{};
    const auto substitute = std::wstring(L"\\??\\") + target.wstring();
    const auto print = target.wstring();
    data.tag = IO_REPARSE_TAG_MOUNT_POINT;
    data.substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data.printOffset = data.substituteLength + sizeof(wchar_t);
    data.printLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
    data.dataLength = static_cast<WORD>(8 + data.printOffset + data.printLength + sizeof(wchar_t));
    std::memcpy(data.path, substitute.c_str(), data.substituteLength + sizeof(wchar_t));
    std::memcpy(reinterpret_cast<char*>(data.path) + data.printOffset, print.c_str(), data.printLength + sizeof(wchar_t));
    DWORD returned = 0;
    const bool created = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &data,
        8 + data.dataLength, nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(handle);
    return created;
}

void TestReplacedSourceDirectoryCannotRedirectLyrics() {
    const auto root = FixturePath(".mp3").parent_path();
    const auto original = root / "original", moved = root / "moved", outside = root / "outside";
    std::filesystem::create_directory(original);
    std::filesystem::create_directory(outside);
    Write(original / "song.mp3", "authorized source");
    Write(outside / "song.mp3", "other source");
    Write(outside / "song.lrc", "[00:01]outside lyrics\n");
    const auto imported = TrackFor(original / "song.mp3");
    std::filesystem::rename(original, moved);
    const bool created = Junction(original, outside);
    Check(created, "directory junction fixture can be created without symlink privileges");
    if (created) {
        Check(lume::music::LoadLocalLyrics(imported).lines.empty(),
            "source directory changed to a junction cannot redirect the authorized lyrics read");
        Check(RemoveDirectoryW(original.c_str()) != FALSE, "junction fixture is removed without touching its target");
    } else {
        std::filesystem::remove(original);
    }
    std::filesystem::rename(moved, original);
}

}  // namespace

void TestDecodesCommonSidecarEncodings() {
    const auto audio = FixturePath("-encoding.mp3");
    Write(audio, "audio fixture");
    const auto lyric = std::filesystem::path(audio).replace_extension(".lrc");
    // Two lines (U+7B2C U+4E00 U+53E5, U+7B2C U+4E8C U+53E5) in GBK, one using [mm:ss:xx].
    Write(lyric, "[00:01.00]\xb5\xda\xd2\xbb\xbe\xe4\n[00:02:50]\xb5\xda\xb6\xfe\xbe\xe4\n");
    auto result = lume::music::LoadLocalLyrics(TrackFor(audio));
    Check(result.lines.size() == 2 && result.lines[0].fullText == "\xe7\xac\xac\xe4\xb8\x80\xe5\x8f\xa5" &&
        result.lines[1].startTime == 2.5, "GBK sidecar and [mm:ss:xx] timestamps are read");
    Write(lyric, std::string("\xFF\xFE[\0" "0\0" "0\0" ":\0" "0\0" "1\0" "]\0" "a\0", 18));
    result = lume::music::LoadLocalLyrics(TrackFor(audio));
    Check(result.lines.size() == 1 && result.lines[0].fullText == "a", "UTF-16LE sidecar is read");
    Write(lyric, std::string("\xFE\xFF\0[" "\0" "0" "\0" "0" "\0:" "\0" "0" "\0" "1" "\0]" "\0" "b", 18));
    result = lume::music::LoadLocalLyrics(TrackFor(audio));
    Check(result.lines.size() == 1 && result.lines[0].fullText == "b", "UTF-16BE sidecar is read");
}

int main() {
    TestParsesTimelineAndBoundsLastLine();
    TestDecodesCommonSidecarEncodings();
    TestRejectsFilesOutsideReadLimits();
    TestOnlyCurrentImportedLocalTrackCanResolveSidecar();
    TestParsingRejectsMalformedAndUnboundedInput();
    TestParserExpansionAndProtocolBudgets();
    TestSidecarDoesNotFollowSymlinks();
    TestSourceIdentityIsPinnedUntilSidecarReadCompletes();
    TestReplacedSourceDirectoryCannotRedirectLyrics();
    std::error_code error;
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "lume-music-lyrics-test", error);
    if (!passed) return 1;
    std::cout << "music lyrics parser behavior passed\n";
    return 0;
}
