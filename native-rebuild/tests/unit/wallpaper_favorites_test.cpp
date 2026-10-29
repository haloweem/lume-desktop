#include "core/wallpaper/favorites.h"
#include "vendor/nlohmann/json.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace lume;
using Json = nlohmann::json;

void Expect(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}

struct Files final : settings::IFileStore {
    std::map<std::string, std::string> entries;
    std::string failWrite, failRename, throwWrite;
    mutable unsigned unboundedReads = 0, boundedReads = 0;
    mutable std::size_t largestReadLimit = 0;

    bool Read(const std::string& path, std::string& out) const override {
        ++unboundedReads;
        const auto found = entries.find(path);
        if (found == entries.end()) return false;
        out = found->second;
        return true;
    }
    bool ReadBounded(const std::string& path, std::size_t limit, std::string& out) const override {
        ++boundedReads;
        if (limit > largestReadLimit) largestReadLimit = limit;
        const auto found = entries.find(path);
        if (found == entries.end() || found->second.size() > limit) return false;
        out = found->second;
        return true;
    }
    bool Exists(const std::string& path) const override { return entries.count(path) != 0; }
    bool Write(const std::string& path, const std::string& value) override {
        if (path == throwWrite) {
            entries[path] = "interrupted";
            throw std::runtime_error("fixture write interruption");
        }
        if (path == failWrite) { entries[path] = "partial"; return false; }
        entries[path] = value;
        return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        if (to == failRename || !Exists(from)) return false;
        entries[to] = entries.at(from);
        entries.erase(from);
        return true;
    }
    bool Remove(const std::string& path) override { entries.erase(path); return true; }
};

constexpr const char* Path = "isolated/favorites.json";

std::string Document(const std::vector<std::string>& ids) {
    return Json{{"version", 1}, {"ids", ids}}.dump();
}

void ExpectNoTemporaryFiles(const Files& files) {
    Expect(!files.Exists(std::string(Path) + ".tmp") && !files.Exists(std::string(Path) + ".bak.tmp"),
        "temporary writes are cleaned after failure");
}

void FreshAndRoundTrip() {
    Files files;
    wallpaper::FavoritesStore store(files);
    auto loaded = store.Load(Path);
    Expect(loaded.status == "fresh" && loaded.writable && loaded.ids.empty(),
        "missing favorites start as a writable local empty list");
    const std::vector<std::string> first{"forest-2", "Lake_3"};
    Expect(store.Save(Path, first), "save a local favorite ID list");
    const auto original = files.entries.at(Path);
    loaded = store.Load(Path);
    Expect(loaded.status == "loaded" && loaded.writable && loaded.ids == first,
        "favorite IDs round-trip in caller order without changing case");
    const auto json = Json::parse(original);
    Expect(json.is_object() && json.size() == 2 && json["version"] == 1 && json["ids"] == first,
        "favorites persist only their own version and IDs without counts or playback settings");
    const std::vector<std::string> second{"Lake_3"};
    Expect(store.Save(Path, second) && files.entries.at(std::string(Path) + ".bak") == original,
        "replacement preserves the previous valid favorite list");
    Expect(store.Save(Path, {}) && store.Load(Path).ids.empty(),
        "an explicitly empty favorite list is persisted successfully");
    Expect(files.unboundedReads == 0 && files.boundedReads > 0 &&
        files.largestReadLimit <= 2u * 1024u * 1024u, "all reads enforce the metadata allocation bound");
    Expect(!store.Load("").writable && !store.Save("", first), "empty paths cannot access implicit storage");
    const std::string nulPath("favorites.json\0other", 20);
    Expect(!store.Load(nulPath).writable && !store.Save(nulPath, first),
        "embedded-NUL paths cannot alias a different disk file");
}

void StrictSchemaAndIds() {
    Files files;
    wallpaper::FavoritesStore store(files);
    const std::vector<std::string> valid{"keep"};
    Expect(store.Save(Path, valid), "seed a valid list for validation failures");
    const auto original = files.entries.at(Path);
    std::vector<std::vector<std::string>> invalid{
        {"repeat", "repeat"}, {"forest", "FOREST"}, {""}, {"../outside"}, {"folder/name"},
        {"folder\\name"}, {"CON"}, {"nul.video"}, {"COM1"}, {"LPT9"},
        {"trailing."}, {std::string(97, 'a')}, {std::string("a\0b", 3)}, {u8"壁纸"}
    };
    for (const auto& ids : invalid) {
        const auto before = ids;
        Expect(!store.Save(Path, ids) && ids == before && files.entries.at(Path) == original,
            "invalid or case-duplicate IDs cannot mutate caller data or valid storage");
        ExpectNoTemporaryFiles(files);
    }
    for (const auto* text : {
             "{", "[]", "{\"version\":1}", "{\"version\":1,\"ids\":null}",
             "{\"version\":1,\"ids\":[1]}", "{\"version\":1,\"ids\":[\"a\",\"A\"]}",
             "{\"version\":1,\"ids\":[\"CON\"]}", "{\"version\":1,\"ids\":[],\"count\":9}",
             "{\"version\":1,\"version\":1,\"ids\":[]}",
             "{\"version\":1,\"ids\":[],\"ids\":[\"other\"]}"
         }) {
        files.entries[Path] = text;
        const auto loaded = store.Load(Path);
        Expect(loaded.status == "failed" && !loaded.writable && loaded.ids.empty(),
            "malformed or unsupported favorite records never look like an empty success");
    }
    files.entries[Path] = std::string(30, '[') + "0" + std::string(30, ']');
    Expect(!store.Load(Path).writable, "deep JSON is rejected before unbounded parsing");
}

void Limits() {
    Files files;
    wallpaper::FavoritesStore store(files);
    std::vector<std::string> ids;
    ids.reserve(20001);
    for (unsigned index = 0; index < 20000; ++index)
        ids.push_back(std::to_string(index) + std::string(91, 'a'));
    Expect(store.Save(Path, ids) && store.Load(Path).ids == ids,
        "20,000 safe IDs of up to 96 bytes remain within the storage bound");
    const auto original = files.entries.at(Path);
    ids.push_back("one-too-many");
    Expect(!store.Save(Path, ids) && files.entries.at(Path) == original,
        "more than 20,000 IDs are rejected before replacing the document");
    files.entries[Path] = Document(ids);
    Expect(!store.Load(Path).writable, "oversized ID arrays are rejected during load");
    files.entries[Path] = std::string(2u * 1024u * 1024u + 1u, ' ');
    Expect(!store.Load(Path).writable && files.unboundedReads == 0,
        "oversized byte payloads never enter an unbounded file read");
}

void BackupAndUnknownVersions() {
    Files files;
    wallpaper::FavoritesStore store(files);
    const std::vector<std::string> first{"forest"}, second{"lake"};
    Expect(store.Save(Path, first) && store.Save(Path, second), "seed favorite backup");
    const auto backupPath = std::string(Path) + ".bak";
    const auto validBackup = files.entries.at(backupPath);
    files.entries[Path] = "{";
    auto loaded = store.Load(Path);
    Expect(loaded.status == "recovered" && loaded.writable && loaded.ids == first,
        "corrupt favorites recover from a fully validated backup");
    Expect(store.Save(Path, second) && files.entries.at(backupPath) == validBackup &&
        store.Load(Path).ids == second, "recovery writes a healthy primary without corrupting its backup");
    files.entries.erase(Path);
    loaded = store.Load(Path);
    Expect(loaded.status == "recovered" && loaded.writable && loaded.ids == first,
        "missing primary also restores the existing backup");
    Expect(store.Save(Path, second) && files.entries.at(backupPath) == validBackup,
        "missing-primary repair keeps the valid backup");
    for (const auto* version : {"9", "1.0", "\"1\"", "null", "true"}) {
        const auto future = std::string("{\"version\":") + version + ",\"ids\":[]}";
        files.entries[Path] = future;
        loaded = store.Load(Path);
        Expect(loaded.status == "failed" && !loaded.writable && loaded.ids.empty() &&
            !store.Save(Path, first) && files.entries.at(Path) == future &&
            files.entries.at(backupPath) == validBackup,
            "explicit unknown schemas cannot fall back or be overwritten by version 1");
    }
    files.entries[Path] = "{";
    files.entries[backupPath] = "{\"version\":1,\"ids\":[\"bad\",\"BAD\"]}";
    Expect(store.Load(Path).status == "failed" && !store.Load(Path).writable &&
        !store.Save(Path, first), "invalid backup content cannot authorize a destructive reset");
}

void WriteFailures() {
    for (const auto* stage : {"primary-write", "backup-write", "backup-rename", "primary-rename", "throw"}) {
        Files files;
        wallpaper::FavoritesStore store(files);
        const std::vector<std::string> before{"old"}, requested{"new"};
        Expect(store.Save(Path, before), "seed primary for I/O failure");
        const auto original = files.entries.at(Path);
        const std::string stageName = stage;
        if (stageName == "primary-write") files.failWrite = std::string(Path) + ".tmp";
        else if (stageName == "backup-write") files.failWrite = std::string(Path) + ".bak.tmp";
        else if (stageName == "backup-rename") files.failRename = std::string(Path) + ".bak";
        else if (stageName == "primary-rename") files.failRename = Path;
        else files.throwWrite = std::string(Path) + ".tmp";
        Expect(!store.Save(Path, requested) && requested == std::vector<std::string>{"new"} &&
            files.entries.at(Path) == original, "I/O failure preserves caller input and the completed primary");
        ExpectNoTemporaryFiles(files);
        Expect(store.Load(Path).status == "loaded" && store.Load(Path).ids == before,
            "failed persistence cannot be reported as a successful new favorite state");
    }
}
}

int main() {
    FreshAndRoundTrip();
    StrictSchemaAndIds();
    Limits();
    BackupAndUnknownVersions();
    WriteFailures();
    std::cout << "wallpaper favorites tests passed\n";
}
