#include "apps/shell/music_font_asset_session.h"
#include "vendor/nlohmann/json.hpp"
#include "unique_test_directory.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
using lume::shell::MusicFontAssetSession;
using lume::shell::MusicFontAssetSessionOptions;
void Expect(bool ok, const char* why) {
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
void Put32(std::string& data, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) data[at + i] = static_cast<char>(value >> ((3 - i) * 8));
}
std::string Font(const std::string& ext) {
    if (ext == "ttf" || ext == "otf") {
        std::string data(32, '\0');
        Put32(data, 0, ext == "ttf" ? 0x00010000U : 0x4f54544fU);
        data[5] = 1;
        data.replace(12, 4, "name");
        Put32(data, 20, 28);
        Put32(data, 24, 4);
        return data;
    }
    std::string data(ext == "woff" ? 68 : 52, '\0');
    Put32(data, 0, ext == "woff" ? 0x774f4646U : 0x774f4632U);
    Put32(data, 4, 0x00010000U);
    Put32(data, 8, static_cast<std::uint32_t>(data.size()));
    data[13] = 1;
    Put32(data, 16, 32);
    if (ext == "woff") {
        data.replace(44, 4, "name");
        Put32(data, 48, 64); Put32(data, 52, 4); Put32(data, 56, 4);
    } else Put32(data, 20, 4);
    return data;
}
void Write(const fs::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    Expect(file.good(), "write isolated fixture");
}
std::string Read(const fs::path& path) {
    const auto size = fs::file_size(path);
    Expect(size > 0 && size <= 50ULL * 1024 * 1024, "bounded fixture read");
    std::string content(static_cast<std::size_t>(size), '\0');
    std::ifstream file(path, std::ios::binary);
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
    Expect(file.good(), "read complete fixture");
    return content;
}
std::set<fs::path> Entries(const fs::path& root) {
    std::set<fs::path> result;
    for (const auto& entry : fs::recursive_directory_iterator(root))
        result.insert(entry.path().lexically_relative(root));
    return result;
}
Json Request(MusicFontAssetSession& session, const char* action, const std::string& id = "",
    std::uint64_t request = 0) {
    static std::uint64_t nextRequest = 1000;
    if (!request) request = ++nextRequest;
    Json command = {{"version", 1}, {"scope", "music-font-assets"}, {"action", action}};
    if (std::string(action) != "snapshot") command["request"] = request;
    if (!id.empty()) command["id"] = id;
    const auto text = command.dump();
    const auto output = session.HandleMessage(std::wstring(text.begin(), text.end()));
    Expect(output.has_value(), "valid command handled");
    const auto result = Json::parse(output->begin(), output->end());
    Expect(result.size() == 6 && result["version"] == 1 && result["type"] == "music-font-assets-state",
        "exact public response shape");
    const auto error = result.at("error").get<std::string>();
    Expect(error.empty() || error == "load" || error == "invalid" || error == "save" || error == "cancelled",
        "bounded public errors");
    Expect(result["request"] == (std::string(action) == "snapshot" ? 0 : request), "request correlation");
    Expect(result["assets"].size() <= 8, "bounded asset count");
    for (const auto& asset : result["assets"]) {
        Expect(asset.size() == 5 && asset["family"] ==
            "LumeImportedFont_" + asset.at("id").get<std::string>(), "metadata-only immutable alias");
        Expect(asset.at("label").get<std::string>().find('\\') == std::string::npos &&
            !asset.contains("path") && !asset.contains("url"), "no private paths");
    }
    return result;
}
void RealFontRoundTrip(const fs::path& root, const fs::path& source) {
    wchar_t windows[MAX_PATH]{};
    const auto length = GetWindowsDirectoryW(windows, MAX_PATH);
    Expect(length > 0 && length < MAX_PATH, "locate Windows directory read-only");
    fs::path systemFont;
    for (const auto* name : {L"arial.ttf", L"segoeui.ttf", L"tahoma.ttf"}) {
        const auto path = fs::path(windows) / "Fonts" / name;
        if (fs::is_regular_file(path)) { systemFont = path; break; }
    }
    Expect(!systemFont.empty(), "real installed TrueType font required for round-trip fixture");
    const auto originalWriteTime = fs::last_write_time(systemFont);
    const auto bytes = Read(systemFont);
    const auto fixture = source / "RealSystemFont.ttf";
    // Windows fonts can be WinSxS hardlinks. Copy bytes into isolation without
    // weakening the importer's source/owned-file hardlink checks.
    Write(fixture, bytes);
    int picks = 0;
    bool cancelDuringChoose = false;
    MusicFontAssetSession* active = nullptr;
    Json asset;
    MusicFontAssetSessionOptions options;
    options.root = root / "real-font-round-trip";
    options.verification = true;
    options.chooseFont = [&](HWND) {
        ++picks;
        Expect(picks <= 3, "nested choose never recursively invokes picker");
        if (cancelDuringChoose) {
            const auto before = Request(*active, "snapshot");
            Expect(before["error"] == "" && before["assets"] == Json::array({asset}) &&
                before["candidate"].is_null(), "reentrant snapshot sees only persisted assets");
            const auto nested = Request(*active, "choose");
            Expect(nested["error"] == "cancelled" && nested["candidate"].is_null(),
                "reentrant choose rejected while modal chooser active");
            active->Cancel();
            const auto late = Request(*active, "choose");
            Expect(late["error"] == "cancelled" && late["candidate"].is_null(),
                "reentrant choose after Cancel cannot publish a candidate");
            const auto drop = Request(*active, "drop", asset.at("id").get<std::string>());
            Expect(drop["error"] == "cancelled", "reentrant drop cannot mutate persisted assets");
        }
        return fixture;
    };
    fs::path ownedFont;
    std::string manifest;
    {
        MusicFontAssetSession importing(options);
        active = &importing;
        const auto result = Request(importing, "choose");
        Expect(result["error"] == "" && result["candidate"].is_object(),
            "real installed font imports successfully");
        asset = result["candidate"];
        Expect(asset["extension"] == "ttf" && asset["label"] == "RealSystemFont" &&
            asset["size"] == bytes.size() && result["assets"] == Json::array({asset}),
            "real font candidate metadata is complete");
        ownedFont = importing.AssetDirectory() / (asset.at("id").get<std::string>() + ".ttf");
        Expect(Read(ownedFont) == bytes, "published real font bytes equal installed source");
        manifest = Read(options.root / "font-assets.json");
        Expect(Json::parse(manifest)["assets"] == result["assets"], "real font metadata persisted");
    }
    {
        MusicFontAssetSession restored(options);
        active = &restored;
        const auto filename = ownedFont.filename().wstring();
        Expect(restored.AssetSize(filename) == 0, "asset lookup never lazily reads manifest");
        const auto snapshot = Request(restored, "snapshot");
        Expect(snapshot["error"] == "" && snapshot["assets"] == Json::array({asset}) &&
            snapshot["candidate"].is_null() && picks == 1,
            "fresh session restores immutable real font without invoking chooser");
        Expect(restored.AssetSize(filename) == bytes.size(), "loaded metadata authorizes exact filename size");
        for (const auto& invalid : {L"../" + filename, filename + L"?query", filename + L".bak",
            std::wstring(L"00000000000000000000000000000000.ttf"), filename.substr(0, 32) + L".TTF",
            filename.substr(0, 32) + L".otf", std::wstring(ownedFont.wstring())})
            Expect(restored.AssetSize(invalid) == 0, "asset lookup denies non-whitelisted filenames");
        Expect(Read(ownedFont) == bytes && Read(options.root / "font-assets.json") == manifest,
            "restore preserves real font and manifest bytes");
        const auto entries = Entries(options.root);
        Expect(entries.size() == 3, "only font directory, immutable font and manifest persisted");
        cancelDuringChoose = true;
        const auto cancelled = Request(restored, "choose");
        Expect(cancelled["error"] == "cancelled" && cancelled["candidate"].is_null() &&
            cancelled["assets"] == snapshot["assets"] && picks == 2,
            "Cancel invalidates valid late chooser result during reentrancy");
        Expect(Entries(options.root) == entries && Read(ownedFont) == bytes &&
            Read(options.root / "font-assets.json") == manifest,
            "cancelled reentrant chooser creates no files and changes no persisted bytes");
        cancelDuringChoose = false;
        const auto retry = Request(restored, "choose");
        Expect(retry["error"] == "" && retry["candidate"].is_object() &&
            retry["assets"].size() == 2 && picks == 3 &&
            retry["candidate"]["id"] != asset["id"], "fresh choose works after cancelled modal returns");
        const auto locked = CreateFileW((options.root / "font-assets.json").c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, 0, nullptr);
        Expect(locked != INVALID_HANDLE_VALUE, "lock manifest to prove cached lookup performs no IO");
        Expect(restored.AssetSize(filename) == bytes.size(), "asset lookup succeeds without reopening manifest");
        CloseHandle(locked);
        restored.Shutdown();
        Expect(restored.AssetSize(filename) == 0, "shutdown revokes cached asset access");
    }
    Expect(Read(systemFont) == bytes && fs::last_write_time(systemFont) == originalWriteTime,
        "real installed font remains unchanged");
    std::cout << "real system font import, session restore and reentrant cancellation passed\n";
}
}

int main() {
    const auto root = UniqueTestDirectory(fs::temp_directory_path(), "lume-font-assets-");
    const auto source = root / "source";
    fs::create_directory(source);
    RealFontRoundTrip(root, source);
    for (const auto* extension : {"ttf", "otf", "woff", "woff2"})
        Write(source / (std::string("Fixture.") + extension), Font(extension));
    fs::path selected;
    int picks = 0;
    std::vector<std::string> references;
    MusicFontAssetSessionOptions options;
    options.root = root / "owned";
    options.verification = true;
    options.chooseFont = [&](HWND) { ++picks; return selected; };
    options.selectedFamilies = [&] { return references; };
    MusicFontAssetSession session(options);
    Expect(Request(session, "snapshot")["assets"].empty() && picks == 0 &&
        !fs::exists(options.root), "snapshot lazily reads and never creates store or picker");
    Expect(session.AssetDirectory() == options.root / "fonts", "native-only served directory");
    for (const auto* malformed : {
        L"", L"{}", LR"({"version":1,"scope":"other","action":"snapshot"})",
        LR"({"version":1,"scope":"music-font-assets","action":"snapshot","request":1})",
        LR"({"version":1,"scope":"music-font-assets","action":"choose","request":0})",
        LR"({"version":1,"scope":"music-font-assets","action":"choose","request":-1})",
        LR"({"version":1,"scope":"music-font-assets","action":"choose","request":true})",
        LR"({"version":1,"scope":"music-font-assets","action":"choose","request":1.0})",
        LR"({"version":1,"scope":"music-font-assets","action":"choose","request":9007199254740992})",
        LR"({"version":1,"scope":"music-font-assets","action":"choose","request":1,"path":"C:/private"})",
        LR"({"version":1,"scope":"music-font-assets","action":"choose","request":1,"requ\u0065st":2})",
        LR"({"version":1,"scope":"music-font-assets","action":"choose","request":{"x":1}})",
        LR"({"version":1,"scope":"music-font-assets","action":"cancel"})",
        LR"({"version":1,"scope":"music-font-assets","action":"cancel","request":0})",
        LR"({"version":1,"scope":"music-font-assets","action":"cancel","request":-1})",
        LR"({"version":1,"scope":"music-font-assets","action":"cancel","request":true})",
        LR"({"version":1,"scope":"music-font-assets","action":"cancel","request":1.0})",
        LR"({"version":1,"scope":"music-font-assets","action":"cancel","request":9007199254740992})",
        LR"({"version":1,"scope":"music-font-assets","action":"cancel","request":1,"id":"extra"})",
        LR"({"version":1,"scope":"music-font-assets","action":"drop","request":1,"id":"../outside"})",
        LR"({"version":1.0,"scope":"music-font-assets","action":"snapshot"})",
        LR"({"version":1,"scope":"music-font-assets","action":"install","request":1})"
    }) Expect(!session.HandleMessage(malformed), "malformed command rejected before picker");
    Expect(!session.HandleMessage(std::wstring(2049, 'x')) && picks == 0, "oversized command rejected");
    auto nul = std::wstring(LR"({"version":1,"scope":"music-font-assets","action":"snapshot"})");
    nul.push_back(0);
    Expect(!session.HandleMessage(nul), "embedded NUL rejected");
    Expect(Request(session, "choose")["error"] == "cancelled", "empty chooser is cancellation");
    Json imported = Json::array();
    for (const auto* extension : {"ttf", "otf", "woff", "woff2"}) {
        selected = source / (std::string("Fixture.") + extension);
        const auto result = Request(session, "choose");
        Expect(result["error"] == "" && result["candidate"].is_object(), "all historical font formats accepted");
        const auto asset = result["candidate"];
        Expect(asset["extension"] == extension && asset["label"] == "Fixture" &&
            asset["size"] == fs::file_size(selected), "extension label and file size preserved");
        imported.push_back(asset);
        Expect(fs::exists(session.AssetDirectory() /
            (asset.at("id").get<std::string>() + "." + extension)), "import file published before metadata");
    }
    const auto saved = Request(session, "snapshot");
    Expect(saved["assets"] == imported && saved["candidate"].is_null(), "snapshot does not select a candidate");
    MusicFontAssetSession restored(options);
    Expect(Request(restored, "snapshot")["assets"] == imported, "immutable assets survive restart");
    const auto id = imported[0].at("id").get<std::string>();
    Expect(Request(restored, "drop", id)["error"] == "invalid", "old-session assets never deleted");
    references = {imported[0].at("family").get<std::string>()};
    std::transform(references[0].begin(), references[0].end(), references[0].begin(),
        [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    Expect(Request(session, "drop", id)["error"] == "invalid", "persisted and backup reference protects candidate");
    references.clear();
    Expect(Request(session, "drop", id)["error"] == "" &&
        !fs::exists(session.AssetDirectory() / (id + ".ttf")), "uncommitted decode-failure cleanup");

    for (const auto& invalid : {std::string("not a font"), Font("woff")}) {
        selected = source / "Bad.ttf";
        Write(selected, invalid);
        const auto result = Request(session, "choose");
        Expect(result["error"] == "invalid" && result["candidate"].is_null(), "signature mismatch rejected");
    }
    auto bounds = Font("ttf");
    Put32(bounds, 24, 100000);
    selected = source / "Bounds.ttf"; Write(selected, bounds);
    Expect(Request(session, "choose")["error"] == "invalid", "table range validated");
    selected = source / "TooLarge.ttf"; Write(selected, Font("ttf"));
    fs::resize_file(selected, 50ULL * 1024 * 1024 + 1);
    Expect(Request(session, "choose")["error"] == "invalid", "50MiB file size checked before copy");
    selected = source / "Fixture.ttf";
    while (Request(session, "snapshot")["assets"].size() < 8)
        Expect(Request(session, "choose")["error"] == "", "fill cap without evicting existing fonts");
    const auto capped = Request(session, "snapshot")["assets"];
    Expect(Request(session, "choose")["error"] == "invalid" &&
        Request(session, "snapshot")["assets"] == capped, "ninth import rejected without eviction");

    auto quotaOptions = options;
    quotaOptions.root = root / "total-quota";
    fs::create_directories(quotaOptions.root / "fonts");
    for (int i = 0; i < 4; ++i) {
        const auto file = quotaOptions.root / "fonts" / (std::to_string(i) + ".ttf");
        Write(file, Font("ttf"));
        fs::resize_file(file, 50ULL * 1024 * 1024);
    }
    MusicFontAssetSession quota(quotaOptions);
    Expect(Request(quota, "choose")["error"] == "invalid" &&
        !fs::exists(quotaOptions.root / "font-assets.json"),
        "200MiB physical quota counts crash-orphaned files and never evicts");

    auto saveOptions = options;
    saveOptions.root = root / "save-failure";
    MusicFontAssetSession failingSave(saveOptions);
    Expect(Request(failingSave, "choose")["error"] == "", "seed publication-failure fixture");
    const auto prior = Request(failingSave, "snapshot")["assets"];
    const auto manifest = saveOptions.root / "font-assets.json";
    const auto locked = CreateFileW(manifest.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, 0, nullptr);
    Expect(locked != INVALID_HANDLE_VALUE, "lock isolated manifest against replacement");
    Expect(Request(failingSave, "choose")["error"] == "save", "atomic manifest replace failure reported");
    CloseHandle(locked);
    Expect(Request(failingSave, "snapshot")["assets"] == prior &&
        std::distance(fs::directory_iterator(saveOptions.root / "fonts"), fs::directory_iterator{}) == 1,
        "failed publication keeps prior metadata and removes unpublished copy");
    const auto lockDrop = CreateFileW(manifest.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, 0, nullptr);
    Expect(lockDrop != INVALID_HANDLE_VALUE, "lock isolated manifest for drop");
    Expect(Request(failingSave, "drop", prior[0].at("id").get<std::string>())["error"] == "save",
        "drop cannot delete font before successful metadata publication");
    CloseHandle(lockDrop);
    Expect(Request(failingSave, "snapshot")["assets"] == prior, "failed drop preserves existing font");
    const auto dropId = prior[0].at("id").get<std::string>();
    const auto dropFont = failingSave.AssetDirectory() / (dropId + ".ttf");
    const auto priorManifest = Read(manifest);
    const auto priorEntries = Entries(saveOptions.root);
    const auto attributes = GetFileAttributesW(dropFont.c_str());
    Expect(attributes != INVALID_FILE_ATTRIBUTES &&
        SetFileAttributesW(dropFont.c_str(), attributes | FILE_ATTRIBUTE_READONLY),
        "make isolated candidate read-only to force disposition failure");
    const auto failedDrop = Request(failingSave, "drop", dropId);
    Expect(failedDrop["error"] == "save" && failedDrop["assets"] == prior &&
        failedDrop["candidate"].is_null() && Read(manifest) == priorManifest &&
        Entries(saveOptions.root) == priorEntries && Read(dropFont) == Font("ttf"),
        "failed deletion preserves metadata, file bytes and retry bookkeeping");
    {
        MusicFontAssetSession afterFailure(saveOptions);
        Expect(Request(afterFailure, "snapshot")["assets"] == prior,
            "failed deletion remains recoverable by fresh session");
    }
    Expect(SetFileAttributesW(dropFont.c_str(), attributes), "restore isolated candidate attributes");
    const auto retriedDrop = Request(failingSave, "drop", dropId);
    Expect(retriedDrop["error"] == "" && retriedDrop["assets"].empty() &&
        !fs::exists(dropFont) && Json::parse(Read(manifest))["assets"].empty() &&
        failingSave.AssetSize(dropFont.filename().wstring()) == 0,
        "same session retries deletion successfully after read-only failure");

    auto cancelOptions = options;
    cancelOptions.root = root / "cancelled";
    MusicFontAssetSession* cancelling = nullptr;
    cancelOptions.chooseFont = [&](HWND) {
        const auto snapshot = Request(*cancelling, "snapshot");
        Expect(snapshot["error"] == "" && snapshot["assets"].empty() &&
            snapshot["candidate"].is_null(), "empty-store reentrant snapshot creates nothing");
        cancelling->Cancel();
        const auto nested = Request(*cancelling, "choose");
        Expect(nested["error"] == "cancelled" && nested["candidate"].is_null(),
            "empty-store reentrant choose after Cancel is rejected");
        return source / "Fixture.ttf";
    };
    MusicFontAssetSession cancelSession(cancelOptions);
    cancelling = &cancelSession;
    const auto cancelled = Request(cancelSession, "choose");
    Expect(cancelled["error"] == "cancelled" && cancelled["candidate"].is_null() &&
        cancelled["assets"].empty() &&
        !fs::exists(cancelOptions.root), "modal cancellation invalidates returned path before publication");
    cancelSession.Shutdown();
    Expect(Request(cancelSession, "snapshot")["error"] == "cancelled", "shutdown rejects new work");
    auto protocolOptions = options;
    protocolOptions.root = root / "protocol-cancel";
    MusicFontAssetSession* modal = nullptr;
    int modalPicks = 0;
    protocolOptions.chooseFont = [&](HWND) {
        ++modalPicks;
        const auto target = modalPicks == 3 ? 103ULL : 101ULL;
        const auto reply = Request(*modal, "cancel", "", target);
        Expect(reply["error"] == "cancelled" && reply["candidate"].is_null(),
            "cancel acknowledgement is correlated and never returns a candidate");
        return source / "Fixture.ttf";
    };
    MusicFontAssetSession protocolCancel(protocolOptions);
    modal = &protocolCancel;
    Expect(Request(protocolCancel, "cancel", "", 99)["error"] == "cancelled" &&
        modalPicks == 0 && !fs::exists(protocolOptions.root), "idle cancel performs no IO or chooser work");
    Expect(Request(protocolCancel, "choose", "", 101)["error"] == "cancelled" &&
        modalPicks == 1 && !fs::exists(protocolOptions.root),
        "matching protocol cancel prevents late chooser result from creating store");
    const auto newer = Request(protocolCancel, "choose", "", 102);
    Expect(newer["error"] == "" && newer["candidate"].is_object() && modalPicks == 2,
        "old request cancellation cannot invalidate newer modal choose");
    const auto newerManifest = Read(protocolOptions.root / "font-assets.json");
    const auto newerEntries = Entries(protocolOptions.root);
    const auto cancelNewer = Request(protocolCancel, "choose", "", 103);
    Expect(cancelNewer["error"] == "cancelled" && cancelNewer["candidate"].is_null() &&
        cancelNewer["assets"] == newer["assets"] && modalPicks == 3 &&
        Read(protocolOptions.root / "font-assets.json") == newerManifest &&
        Entries(protocolOptions.root) == newerEntries,
        "matching protocol cancel preserves existing persisted store without late candidate");
    Expect(Request(protocolCancel, "cancel", "", 102)["error"] == "cancelled" &&
        Request(protocolCancel, "snapshot")["assets"].empty() &&
        fs::is_empty(protocolCancel.AssetDirectory()),
        "late UI abort after successful import removes undelivered candidate and metadata");
    const auto confirmed = Request(protocolCancel, "choose", "", 104);
    Expect(confirmed["error"] == "", "seed confirmed candidate");
    references = {confirmed["candidate"].at("family").get<std::string>()};
    Expect(Request(protocolCancel, "cancel", "", 104)["error"] == "invalid" &&
        Request(protocolCancel, "snapshot")["assets"] == confirmed["assets"],
        "late cancel never deletes a candidate already referenced by preferences");
    references.clear();
    Expect(Request(protocolCancel, "cancel", "", 104)["error"] == "cancelled" &&
        Request(protocolCancel, "snapshot")["assets"].empty(),
        "guarded late cleanup remains retryable once candidate has no references");
    auto lateOptions = options;
    lateOptions.root = root / "late-cancel-newer-modal";
    MusicFontAssetSession* lateSession = nullptr;
    int latePicks = 0;
    lateOptions.chooseFont = [&](HWND) {
        if (++latePicks == 2) {
            Expect(Request(*lateSession, "cancel", "", 201)["error"] == "cancelled",
                "late cancel cleans older unpublished-to-UI candidate during newer chooser");
        }
        return source / "Fixture.ttf";
    };
    MusicFontAssetSession lateCancel(lateOptions);
    lateSession = &lateCancel;
    Expect(Request(lateCancel, "choose", "", 201)["error"] == "", "seed undelivered old candidate");
    const auto survived = Request(lateCancel, "choose", "", 202);
    Expect(survived["error"] == "" && survived["assets"].size() == 1 &&
        survived["assets"][0] == survived["candidate"] && latePicks == 2,
        "late cancellation of completed older request does not invalidate newer chooser");
    auto verifyOptions = options;
    verifyOptions.root = root / "no-dialog";
    verifyOptions.chooseFont = {};
    MusicFontAssetSession verification(verifyOptions);
    Expect(Request(verification, "choose")["error"] == "cancelled", "verification never invokes real picker");

    auto maximumOptions = options;
    maximumOptions.root = root / "maximum-file";
    maximumOptions.selectedFamilies = {};
    MusicFontAssetSession maximum(maximumOptions);
    selected = source / "Maximum.ttf";
    Write(selected, Font("ttf"));
    fs::resize_file(selected, 50ULL * 1024 * 1024);
    const auto maxResult = Request(maximum, "choose");
    Expect(maxResult["error"] == "" && maxResult["candidate"]["size"] == 50ULL * 1024 * 1024,
        "historical inclusive 50MiB limit accepted with bounded streaming copy");
    Expect(Request(maximum, "drop", maxResult["candidate"].at("id").get<std::string>())["error"] == "invalid",
        "missing reference hook denies cleanup");
    const auto maxFont = maximum.AssetDirectory() /
        (maxResult["candidate"].at("id").get<std::string>() + ".ttf");
    Expect(fs::file_size(maxFont) == 50ULL * 1024 * 1024, "complete maximum-size copy published");

    auto unsafeOptions = options;
    unsafeOptions.root = root / "hardlink";
    selected = source / "Linked.ttf";
    Expect(CreateHardLinkW(selected.c_str(), (source / "Fixture.ttf").c_str(), nullptr), "create isolated hardlink");
    MusicFontAssetSession unsafe(unsafeOptions);
    Expect(Request(unsafe, "choose")["error"] != "" && !fs::exists(unsafeOptions.root),
        "hardlinked source rejected");
    fs::remove(selected);
    selected = source / "Fixture.ttf";
    const auto own = Request(unsafe, "choose")["candidate"];
    const auto ownedFile = unsafe.AssetDirectory() /
        (own.at("id").get<std::string>() + ".ttf");
    Expect(CreateHardLinkW((root / "owned-link").c_str(), ownedFile.c_str(), nullptr), "create owned hardlink");
    Expect(Request(unsafe, "snapshot")["error"] == "load", "hardlink in owned root rejected");
    fs::remove(root / "owned-link");
    const auto link = root / "reparse";
    if (CreateSymbolicLinkW(link.c_str(), unsafeOptions.root.c_str(),
        SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        auto linkedOptions = options; linkedOptions.root = link;
        MusicFontAssetSession linked(linkedOptions);
        Expect(Request(linked, "snapshot")["error"] == "load", "reparse root rejected");
        fs::remove(link);
    } else std::cout << "reparse fixture skipped: symlink creation unavailable\n";
    Write(unsafeOptions.root / "font-assets.json", R"({"version":1,"version":1,"assets":[]})");
    Expect(Request(unsafe, "snapshot")["error"] == "load", "duplicate manifest keys rejected");
    Write(unsafeOptions.root / "font-assets.json", R"({"version":1,"assets":[{"id":"../escape"}]})");
    Expect(Request(unsafe, "snapshot")["error"] == "load", "corrupt metadata never authorizes arbitrary path");
    Expect(Request(unsafe, "choose")["error"] == "load", "corrupt metadata never overwritten by choose");
    const auto cleanup = fs::weakly_canonical(root);
    Expect(cleanup.parent_path() == fs::weakly_canonical(fs::temp_directory_path()) &&
        cleanup.filename().wstring().find(L"lume-font-assets-") == 0, "cleanup remains in isolated test directory");
    fs::remove_all(cleanup);
    std::cout << "music imported font asset fixtures passed in isolated directory\n";
}
