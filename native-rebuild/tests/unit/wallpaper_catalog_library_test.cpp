#include "core/wallpaper/catalog.h"
#include "core/wallpaper/library.h"
#include "vendor/nlohmann/json.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
using namespace lume::wallpaper;
using Json = nlohmann::json;
void Expect(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
Json Item() {
    return {{"id", "forest-2"}, {"title", "Forest"}, {"type", "video"},
        {"fileName", "forest.mp4"}, {"sizeBytes", 3},
        {"sha256", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"width", 1920}, {"height", 1080},
        {"thumbnailUrl", "https://static.lumedesktop.com/lume/forest.jpg"},
        {"downloadUrl", "https://static.lumedesktop.com/lume/forest.mp4"}};
}
void CatalogValidation() {
    auto json = Json{{"wallpapers", Json::array({Item()})}};
    auto result = ParseCatalog(json.dump());
    Expect(result.error.empty() && result.items.size() == 1, "parse original manifest schema");
    const auto& item = result.items.front();
    Expect(item.id == "forest-2" && item.kind == MediaKind::Video &&
        item.width == 1920 && item.height == 1080 && item.sizeBytes == 3,
        "preserve catalog media metadata");
    Expect(item.previewUrl.empty(), "manifests without previewUrl keep previewing the full file");
    {
        auto preview = json;
        preview["wallpapers"][0]["previewUrl"] = "https://static.lumedesktop.com/lume/previews/forest.mp4";
        const auto parsed = ParseCatalog(preview.dump());
        Expect(parsed.error.empty() && parsed.items[0].previewUrl == "https://static.lumedesktop.com/lume/previews/forest.mp4",
            "previewUrl parsed from the official CDN");
        preview["wallpapers"][0]["previewUrl"] = "https://example.invalid/forest.mp4";
        Expect(!ParseCatalog(preview.dump()).error.empty(), "previewUrl outside the official origins is rejected");
    }
    json["wallpapers"][0]["downloadUrl"] = "http://static.lumedesktop.com/lume/forest.mp4";
    result = ParseCatalog(json.dump());
    Expect(result.error.empty() && result.items[0].mediaUrl.find("https://") == 0, "upgrade original CDN HTTP URLs");
    json["wallpapers"][0]["downloadUrl"] = "https://static.lumedesktop.com/lume/forest.mkv";
    result = ParseCatalog(json.dump());
    Expect(result.error.empty() && result.items[0].kind == MediaKind::Video, "MKV uses native video playback");
    for (const auto* url : {"https://static.lumedesktop.com.evil.invalid/x.mp4",
             "https://user:pass@static.lumedesktop.com/lume/x.mp4",
             "file:///C:/secret.mp4", "http://127.0.0.1/x.mp4",
             "https://static.lumedesktop.com:444/lume/x.mp4",
             "https://static.lumedesktop.com/lume/%2e%2e/secret.mp4",
             "https://static.lumedesktop.com/lume/../secret.mp4",
             "https://static.lumedesktop.com/lume/%2fsecret.mp4",
             "https://static.lumedesktop.com/lume/%252e%252e/secret.mp4"}) {
        json["wallpapers"][0]["downloadUrl"] = url;
        Expect(!ParseCatalog(json.dump()).error.empty(), "reject untrusted media URL");
    }
    json["wallpapers"][0] = Item();
    json["wallpapers"][0]["sha256"] = "123";
    Expect(!ParseCatalog(json.dump()).error.empty(), "reject malformed SHA256");
    json["wallpapers"][0] = Item();
    json["wallpapers"].push_back(Item());
    Expect(!ParseCatalog(json.dump()).error.empty(), "reject duplicate catalog IDs");
    json["wallpapers"][1]["id"] = "FOREST-2";
    Expect(!ParseCatalog(json.dump()).error.empty(), "reject IDs that alias the same Windows media filename");
    json["wallpapers"][0]["id"] = "../escape";
    Expect(!ParseCatalog(json.dump()).error.empty(), "reject path-like IDs");
    json["wallpapers"] = Json::array({Item()});
    json["wallpapers"][0]["id"] = std::string(97, 'a');
    Expect(!ParseCatalog(json.dump()).error.empty(), "catalog IDs match 96-byte page protocol");
    for (const auto* reserved : {"CON", "nul.video", "COM1", "lpt9"}) {
        json["wallpapers"][0]["id"] = reserved;
        Expect(!ParseCatalog(json.dump()).error.empty(), "reject Windows device names before path construction");
    }
    Expect(!ParseCatalog("{").error.empty(), "malformed JSON is a result error");
    Expect(!ParseCatalog(std::string(40, '[') + "0" + std::string(40, ']')).error.empty(),
        "nested catalog fails before unbounded parser recursion");
}
void CatalogPresentationValidation() {
    auto record = Item();
    record["heroThumbnailUrl"] = "https://static.lumedesktop.com/lume/heroes/forest.jpg";
    auto manifest = Json{{"wallpapers", Json::array({record})},
        {"carouselCoverOrder", Json::array({"forest-2"})}};
    Expect(ParseCatalog(manifest.dump()).error.empty(), "accept trusted hero cover and original carousel order");
    for (const auto* url : {"https://evil.invalid/lume/hero.jpg",
             "https://user@static.lumedesktop.com/lume/hero.jpg",
             "file:///C:/private.jpg", "http://127.0.0.1/hero.jpg",
             "https://static.lumedesktop.com/lume/../hero.jpg",
             "https://static.lumedesktop.com/lume/%2e%2e/hero.jpg"}) {
        manifest["wallpapers"][0]["heroThumbnailUrl"] = url;
        Expect(!ParseCatalog(manifest.dump()).error.empty(), "reject an untrusted hero even when a valid card thumbnail exists");
    }
    manifest["wallpapers"][0] = record;
    for (const auto& order : {Json("forest-2"), Json(nullptr), Json::array({1}),
             Json::array({"../forest"}), Json::array({"forest-2", "FOREST-2"})}) {
        manifest["carouselCoverOrder"] = order;
        Expect(!ParseCatalog(manifest.dump()).error.empty(), "reject malformed carousel order instead of coercing it");
    }
    manifest.erase("carouselCoverOrder");
    for (const auto& rank : {Json(-1), Json(0.5), Json("1"), Json(nullptr), Json(kMaxLibraryItems)}) {
        manifest["wallpapers"][0]["homeOrder"] = rank;
        Expect(!ParseCatalog(manifest.dump()).error.empty(), "cached carousel rank must be a bounded integer");
    }
}
void LibraryLifecycle(const std::filesystem::path& base) {
    const auto root = base / "library";
    const auto source = base / std::filesystem::u8path(u8"我的视频.mp4");
    { std::ofstream out(source, std::ios::binary); out << "abc"; }
    Library library(root.u8string());
    Expect(library.Load().ok && library.Items().empty(), "empty library is valid");
    std::atomic<bool> cancelled{true};
    const auto cancelledImport = library.ImportLocal(source.u8string(), &cancelled);
    Expect(!cancelledImport.ok && cancelledImport.error == "WALLPAPER_CANCELLED" &&
        library.Items().empty(), "cancelled local import cannot copy or register");
    const auto imported = library.ImportLocal(source.u8string());
    Expect(imported.ok && !imported.id.empty(), "import UTF8 path");
    Expect(std::filesystem::exists(source), "import preserves source");
    Expect(library.Items().size() == 1 && library.Find(imported.id)->imported,
        "import registers a local entry");
    auto playable = library.ResolvePlayablePath(imported.id);
    Expect(playable.ok && playable.path == imported.path, "local import resolves owned file");
    Library restored(root.u8string());
    Expect(restored.Load().ok && restored.Items().size() == 1, "library reload restores media");
    Expect(restored.ResolvePlayablePath(imported.id).ok, "restored local entry resolves");
    const auto catalog = ParseCatalog(Json{{"wallpapers", Json::array({Item()})}}.dump()).items.front();
    Expect(!restored.RegisterDownload(catalog, source.u8string()).ok,
        "official registration rejects external paths");
    const auto unsafe = root / "media" / "cleartext.mp4";
    { std::ofstream out(unsafe, std::ios::binary); out << "abc"; }
    Expect(!restored.RegisterDownload(catalog, unsafe.u8string()).ok,
        "official registration never silently accepts cleartext");
    const auto manifest = root / "library.json";
    const auto original = [&] { std::ifstream in(manifest); return std::string(
        std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); }();
    { std::ofstream out(manifest); out << "{\"version\":999,\"items\":[]}"; }
    Expect(!restored.Load().ok && restored.Items().size() == 1,
        "unknown version leaves current snapshot intact");
    Expect(!restored.Save().ok, "saving refuses to replace an unknown-version manifest");
    { std::ofstream out(manifest); out << "{\"version\":1,\"items\":[{\"malformed\":true}]}"; }
    Expect(!restored.Save().ok, "saving refuses semantically corrupt existing manifest");
    auto invalid = Json::parse(original);
    invalid["version"] = 1.5;
    { std::ofstream out(manifest); out << invalid.dump(); }
    Expect(!restored.Load().ok && restored.Items().size() == 1,
        "fractional versions cannot be coerced into the supported library schema");
    invalid = Json::parse(original);
    auto alias = invalid["items"][0];
    alias["item"]["id"] = "different-id";
    invalid["items"].push_back(alias);
    { std::ofstream out(manifest); out << invalid.dump(); }
    Expect(!restored.Load().ok && restored.Items().size() == 1,
        "two library entries cannot own the same removable media file");
    invalid["items"][1]["file"] = "different-file.mp4";
    auto collidingId = imported.id;
    std::transform(collidingId.begin(), collidingId.end(), collidingId.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    invalid["items"][1]["item"]["id"] = collidingId;
    { std::ofstream out(manifest); out << invalid.dump(); }
    Expect(!restored.Load().ok && restored.Items().size() == 1,
        "restored library IDs also reject Windows case aliases");
    { std::ofstream out(manifest); out << original; }
    std::filesystem::remove(std::filesystem::u8path(imported.path));
    Expect(restored.Load().ok && restored.Items().size() == 1 &&
        !restored.ResolvePlayablePath(imported.id).ok,
        "a missing media file does not discard unrelated library metadata");
    Expect(restored.Remove(imported.id).ok && restored.Items().empty(), "remove owned entry");
    Expect(std::filesystem::exists(source), "remove never deletes original import source");
    Expect(!restored.ResolvePlayablePath(imported.id).ok, "removed entry is not playable");
}
void LibraryRegistrationOwnership(const std::filesystem::path& base) {
    const auto root = base / "ownership";
    std::filesystem::create_directories(root / "media");
    const auto path = root / "media" / "forest-2.apk";
    { std::ofstream out(path, std::ios::binary); out << "LUMEWP02fixture"; }
    Library library(root.u8string());
    const auto item = ParseCatalog(Json{{"wallpapers", Json::array({Item()})}}.dump()).items.front();
    Expect(library.RegisterDownload(item, path.u8string()).ok, "register an owned official encrypted file");
    auto other = item;
    other.id = "another-id";
    Expect(!library.RegisterDownload(other, path.u8string()).ok && library.Items().size() == 1,
        "registration cannot grant two IDs deletion ownership over one file");
    const auto otherPath = root / "media" / "other.apk";
    std::filesystem::copy_file(path, otherPath);
    other.id = "FOREST-2";
    Expect(!library.RegisterDownload(other, otherPath.u8string()).ok && library.Items().size() == 1,
        "registration rejects case-aliased IDs before persistence");
}
void LibraryRegistrationFailureCleansDownloadedFile(const std::filesystem::path& base) {
    const auto root = base / "registration-failure-cleanup";
    std::filesystem::create_directories(root / "media");
    Library library(root.u8string());
    Expect(library.Load().ok, "load an empty library before a failed registration");
    const auto path = root / "media" / "forest-2.apk";
    { std::ofstream out(path, std::ios::binary); out << "LUMEWP02download"; }
    { std::ofstream out(root / "library.json", std::ios::binary); out << "{\"version\":999,\"items\":[]}"; }
    const auto item = ParseCatalog(Json{{"wallpapers", Json::array({Item()})}}.dump()).items.front();
    Expect(!library.RegisterDownload(item, path.u8string()).ok,
        "registration reports an existing unsupported manifest");
    Expect(!std::filesystem::exists(path),
        "failed registration removes the newly downloaded unowned file");
}
void LibraryReplacementCleansOldFile(const std::filesystem::path& base) {
    const auto root = base / "replacement-cleanup";
    std::filesystem::create_directories(root / "media");
    Library library(root.u8string());
    Expect(library.Load().ok, "load an empty library before replacing a media entry");
    const auto oldPath = root / "media" / "forest-2.apk";
    const auto newPath = root / "media" / "forest-2-replacement.apk";
    { std::ofstream out(oldPath, std::ios::binary); out << "LUMEWP02old"; }
    { std::ofstream out(newPath, std::ios::binary); out << "LUMEWP02new"; }
    const auto item = ParseCatalog(Json{{"wallpapers", Json::array({Item()})}}.dump()).items.front();
    Expect(library.RegisterDownload(item, oldPath.u8string()).ok,
        "register the original media entry before replacement");
    Expect(library.RegisterDownload(item, newPath.u8string()).ok,
        "register the replacement media entry");
    Expect(!std::filesystem::exists(oldPath) && std::filesystem::exists(newPath),
        "successful same-ID replacement removes the old owned media file");
}
std::string FileText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void WriteText(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value;
    Expect(bool(output), "write recovery fixture");
}
void LibraryBackupRecovery(const std::filesystem::path& base) {
    const auto root = base / "recovery";
    const auto source = base / "recovery-source.mp4";
    WriteText(source, "abc");
    Library initial(root.u8string());
    const auto imported = initial.ImportLocal(source.u8string());
    Expect(imported.ok && initial.Save().ok, "create valid primary and backup fixture");
    const auto primary = root / "library.json";
    const auto backup = root / "library.json.bak";
    const auto valid = FileText(primary);
    Expect(FileText(backup) == valid, "backup begins as a validated library snapshot");
    for (const auto& corrupt : {std::string("{"),
             std::string("{\"version\":1,\"items\":[{\"malformed\":true}]}")}) {
        WriteText(primary, corrupt);
        Library restored(root.u8string());
        Expect(restored.Load().ok && restored.Recovered() && restored.Items().size() == 1 &&
            restored.Find(imported.id).has_value(), "damaged primary recovers the valid backup");
        Expect(FileText(primary) == corrupt && FileText(backup) == valid,
            "recovery load is read-only and preserves both manifest files");
        Expect(restored.Save().ok && !restored.Recovered(),
            "recovered library can atomically restore a healthy primary");
        Expect(FileText(backup) == valid, "repair never replaces the valid backup with corrupt primary bytes");
        Library verified(root.u8string());
        Expect(verified.Load().ok && !verified.Recovered() && verified.Find(imported.id).has_value(),
            "repaired primary validates on a fresh load");
    }
    std::filesystem::remove(primary);
    Library missing(root.u8string());
    Expect(missing.Load().ok && missing.Recovered() && missing.Find(imported.id).has_value(),
        "missing primary recovers metadata from a valid backup");
    Expect(missing.Save().ok && !missing.Recovered() && FileText(backup) == valid,
        "missing-primary repair preserves the good backup");
    const auto unsupported = "{\"version\":999,\"items\":[]}";
    WriteText(primary, "{");
    Library blocked(root.u8string());
    Expect(blocked.Load().ok && blocked.Recovered(), "prepare recovered state before an external schema change");
    WriteText(primary, unsupported);
    Expect(!blocked.Load().ok && blocked.Items().size() == 1 &&
        !blocked.Save().ok && FileText(primary) == unsupported && FileText(backup) == valid,
        "explicit unknown primary schema never downgrades even after prior recovery");
    WriteText(primary, "{");
    WriteText(backup, "{");
    Library broken(root.u8string());
    Expect(!broken.Load().ok && !broken.Recovered() && broken.Items().empty(),
        "two damaged manifests cannot be reported as successful recovery");
    WriteText(backup, valid);
    Expect(blocked.Load().ok && blocked.Recovered(), "restore valid backup for save failure checks");
    WriteText(backup, "{");
    Expect(!blocked.Save().ok && blocked.Recovered() && FileText(primary) == "{",
        "a backup that becomes corrupt after recovery blocks repair without claiming success");
    WriteText(backup, valid);
#ifdef _WIN32
    const auto locked = CreateFileW(primary.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(locked != INVALID_HANDLE_VALUE, "lock primary against replacement");
    const auto refused = blocked.Save();
    CloseHandle(locked);
    Expect(!refused.ok && blocked.Recovered() && FileText(primary) == "{" &&
        FileText(backup) == valid, "failed atomic repair leaves the backup and recovered state intact");
    for (const auto& entry : std::filesystem::directory_iterator(root))
        Expect(entry.path().extension() != ".tmp", "failed repair cleans its owned temporary files");
#endif
    Expect(blocked.Save().ok && !blocked.Recovered(), "repair succeeds after the replacement obstruction is removed");
}
void CatalogCacheLifecycle(const std::filesystem::path& base) {
    const auto root = base / "catalog-cache";
    CatalogCache cache(root.u8string());
    auto loaded = cache.Load();
    Expect(loaded.ok && !loaded.found && cache.Items().empty(), "missing catalog cache has an explicit empty status");
    auto video = ParseCatalog(Json{{"wallpapers", Json::array({Item()})}}.dump()).items.front();
    video.id = "cache-video";
    auto imageJson = Item();
    imageJson["id"] = "lake";
    imageJson["downloadUrl"] = "https://static.lumedesktop.com/lume/lake.jpg";
    auto image = ParseCatalog(Json{{"wallpapers", Json::array({imageJson})}}.dump()).items.front();
    CatalogResult full{{video, image}, {}, true, true};
    Expect(cache.Save(full).ok && cache.Items().size() == 2, "successful catalogs are persisted together");
    CatalogCache restored(root.u8string());
    loaded = restored.Load();
    Expect(loaded.ok && loaded.found && restored.Items().size() == 2 &&
        restored.HasImageCatalog() && restored.HasVideoCatalog(), "offline catalog restores both validated collections");
    video.title = "New video title";
    CatalogResult videoOnly{{video}, "image:WALLPAPER_HTTP_RESPONSE", false, true};
    Expect(restored.Save(videoOnly).ok && restored.Items().size() == 2,
        "partial refresh preserves the failed collection");
    const auto unchangedImage = std::find_if(restored.Items().begin(), restored.Items().end(),
        [](const auto& item) { return item.id == "lake"; });
    Expect(unchangedImage != restored.Items().end() && unchangedImage->mediaUrl == image.mediaUrl,
        "partial refresh retains the previous image metadata");
    CatalogResult failed{{}, "WALLPAPER_HTTP_RESPONSE"};
    Expect(!restored.Save(failed).ok && restored.Items().size() == 2,
        "failed refresh cannot clear cached entries");
    CatalogResult emptyImages{{}, {}, true, false};
    Expect(restored.Save(emptyImages).ok && restored.Items().size() == 1 &&
        restored.Items().front().title == "New video title" && restored.HasImageCatalog(),
        "successful empty collection clears only its previous entries");
    auto bad = videoOnly;
    bad.items.front().mediaUrl = "https://evil.invalid/test.mp4";
    Expect(!restored.Save(bad).ok && restored.Items().front().title == "New video title",
        "cache independently validates every incoming URL");
    const auto path = root / "catalog-cache.json";
    const auto original = [&] { std::ifstream in(path); return std::string(
        std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); }();
    for (const auto* invalid : {"{", "{\"version\":99,\"wallpapers\":[]}",
             "{\"version\":1,\"imageCatalogLoaded\":false,\"videoCatalogLoaded\":false,\"wallpapers\":[]}"}) {
        { std::ofstream out(path); out << invalid; }
        Expect(!restored.Load().ok && restored.Items().size() == 1 &&
            restored.HasVideoCatalog(), "invalid cache load preserves the last valid in-memory snapshot");
    }
    { std::ofstream out(path); out << std::string(kMaxCatalogBytes + 1, ' '); }
    Expect(!restored.Load().ok && restored.Items().size() == 1,
        "oversized cache is rejected before parsing");
    { std::ofstream out(path); out << original; }
    CatalogCache finalLoad(root.u8string());
    Expect(finalLoad.Load().ok && finalLoad.Items().size() == 1 &&
        finalLoad.HasImageCatalog() && finalLoad.HasVideoCatalog(), "empty successful collection survives restart");
}
void CatalogPresentationRoundTrip(const std::filesystem::path& base) {
    const auto root = base / "catalog-presentation";
    auto first = Item(), second = Item();
    first["heroThumbnailUrl"] = "http://static.lumedesktop.com/lume/heroes/forest.jpg";
    first["previewThumbnailUrl"] = "https://static.lumedesktop.com/lume/thumbs/forest.jpg";
    second["id"] = "lake";
    second["downloadUrl"] = "https://static.lumedesktop.com/lume/lake.jpg";
    second["heroThumbnailUrl"] = "https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com/lume/heroes/lake.jpg";
    const auto manifest = Json{{"wallpapers", Json::array({first, second})},
        {"carouselCoverOrder", Json::array({"lake", "unavailable-safe-id", "forest-2"})}};
    auto parsed = ParseCatalog(manifest.dump());
    Expect(parsed.error.empty(), "parse ordered covers without requiring every cover to be present in a partial collection");
    parsed.imageCatalogLoaded = true;
    parsed.videoCatalogLoaded = true;
    CatalogCache cache(root.u8string());
    Expect(cache.Save(parsed).ok, "save ordered hero metadata to the official cache");
    const auto serialized = Json::parse(FileText(root / "catalog-cache.json"));
    const auto& records = serialized.at("wallpapers");
    Expect(records[0].value("thumbnailUrl", "") == "https://static.lumedesktop.com/lume/thumbs/forest.jpg",
        "lightweight card image stays separate from the homepage cover");
    Expect(records[0].value("heroThumbnailUrl", "") == "https://static.lumedesktop.com/lume/heroes/forest.jpg",
        "cache preserves and normalizes the hero cover");
    Expect(records[1].value("heroThumbnailUrl", "") == "https://static.lumedesktop.com/lume/heroes/lake.jpg",
        "COS bucket URLs are rewritten to the CDN host");
    Expect(std::string(kOfficialCatalogUrl).rfind("https://static.lumedesktop.com/", 0) == 0 &&
        std::string(kStaticCatalogUrl).rfind("https://static.lumedesktop.com/", 0) == 0,
        "both manifests are read through the CDN");
    Expect(records[0].value("homeOrder", -1) == 2 && records[1].value("homeOrder", -1) == 0,
        "root carousel order survives as per-item homepage ranks");
    CatalogCache restored(root.u8string());
    Expect(restored.Load().ok, "reload presentation metadata");
    Expect(restored.Items()[0].heroThumbnailUrl == "https://static.lumedesktop.com/lume/heroes/forest.jpg" &&
        restored.Items()[0].homeOrder == 2u && restored.Items()[1].homeOrder == 0u,
        "cached covers restore independently of record order");
    CatalogResult reloaded{restored.Items(), {}, true, true};
    Expect(restored.Save(reloaded).ok &&
        Json::parse(FileText(root / "catalog-cache.json")).at("wallpapers") == records,
        "hero images and carousel ranks survive a cache restart");
    auto legacy = serialized;
    for (auto& item : legacy["wallpapers"]) {
        item.erase("heroThumbnailUrl");
        item.erase("homeOrder");
    }
    WriteText(root / "catalog-cache.json", legacy.dump());
    CatalogCache old(root.u8string());
    Expect(old.Load().ok && old.Items().size() == 2, "existing version-one caches remain readable without presentation fields");
    Expect(old.Items()[0].heroThumbnailUrl.empty() && !old.Items()[0].homeOrder,
        "legacy cache does not invent a homepage order or substitute a cover");
    const auto libraryRoot = base / "catalog-presentation-library";
    std::filesystem::create_directories(libraryRoot / "media");
    const auto encrypted = libraryRoot / "media" / "forest-2.apk";
    WriteText(encrypted, "LUMEWP02fixture");
    Library library(libraryRoot.u8string());
    Expect(library.RegisterDownload(parsed.items[0], encrypted.u8string()).ok,
        "store an official download with the current cover metadata");
    Library reopened(libraryRoot.u8string());
    Expect(reopened.Load().ok && reopened.Items().front().item.heroThumbnailUrl == parsed.items[0].heroThumbnailUrl &&
        reopened.Items().front().item.homeOrder == parsed.items[0].homeOrder,
        "official local library retains hero metadata across restart");
}
Json OfficialPolicyManifest() {
    Json records = Json::array();
    for (const auto* id : {"forest-2", "eb-sunflower-girl", "media-player", "wlop-2-2",
             "hatsune-miku-night-beach", "3d-snowflakes", "wlop-2",
             "hatsune-miku-nostalgic-night", "unrelated-forest"}) {
        auto record = Item();
        record["id"] = id;
        record["title"] = id;
        records.push_back(record);
    }
    records[0]["title"] = "Forest 2";
    records[1]["displayName"] = u8"EB\u5341 Sunflower Girl";
    records[8]["title"] = "Forest 2";
    return {{"wallpapers", records},
        {"carouselCoverOrder", Json::array({"eb-sunflower-girl", "forest-2"})}};
}
void ExpectOfficialPolicy(const std::vector<CatalogItem>& items) {
    Expect(items.size() == 5, "official catalog hides exactly the four requested IDs");
    Expect(items[0].id == "forest-2" && items[0].title == "Forest" &&
        items[1].id == "eb-sunflower-girl" && items[1].title == "Sunflower Girl",
        "carousel and official pages share the two corrected catalog titles");
    Expect(items[2].id == "wlop-2" && items[3].id == "hatsune-miku-nostalgic-night" &&
        items[4].id == "unrelated-forest" && items[4].title == "Forest 2",
        "similar IDs, identical hashes and unrelated titles are not removed or renamed");
    Expect(items[0].homeOrder == 1u && items[1].homeOrder == 0u,
        "catalog curation does not change carousel order");
}
void OfficialCatalogPolicyPreservesMedia() {
    const auto original = ParseCatalog(OfficialPolicyManifest().dump());
    Expect(original.error.empty(), "parse original catalog metadata before curation");
    auto items = original.items;
    ApplyOfficialCatalogPolicy(items);
    ExpectOfficialPolicy(items);
    for (const auto& item : items) {
        const auto before = std::find_if(original.items.begin(), original.items.end(),
            [&](const auto& value) { return value.id == item.id; });
        Expect(before != original.items.end() && item.kind == before->kind &&
            item.mediaUrl == before->mediaUrl && item.thumbnailUrl == before->thumbnailUrl &&
            item.heroThumbnailUrl == before->heroThumbnailUrl && item.sha256 == before->sha256 &&
            item.sizeBytes == before->sizeBytes && item.width == before->width &&
            item.height == before->height && item.homeOrder == before->homeOrder,
            "renaming and hiding never change retained media, hashes, dimensions or ordering");
    }
    ApplyOfficialCatalogPolicy(items);
    ExpectOfficialPolicy(items);
}
void OfficialCatalogCachePolicy(const std::filesystem::path& base) {
    const auto root = base / "official-catalog-policy";
    std::filesystem::create_directories(root);
    auto legacy = OfficialPolicyManifest();
    legacy["version"] = 1;
    legacy["imageCatalogLoaded"] = false;
    legacy["videoCatalogLoaded"] = true;
    const auto path = root / "catalog-cache.json";
    WriteText(path, legacy.dump());
    CatalogCache cache(root.u8string());
    Expect(cache.Load().ok, "load old official metadata without requiring a network refresh");
    ExpectOfficialPolicy(cache.Items());
    auto refreshed = ParseCatalog(OfficialPolicyManifest().dump());
    Expect(refreshed.error.empty() && refreshed.items.size() == 9,
        "shared library parser retains all validated records");
    refreshed.videoCatalogLoaded = true;
    Expect(cache.Save(refreshed).ok, "refresh official cache from legacy manifest titles");
    ExpectOfficialPolicy(cache.Items());
    const auto persisted = ParseCatalog(FileText(path));
    Expect(persisted.error.empty(), "saved curated cache is valid");
    ExpectOfficialPolicy(persisted.items);
    CatalogCache reopened(root.u8string());
    Expect(reopened.Load().ok, "curated cache survives a restart");
    ExpectOfficialPolicy(reopened.Items());
    auto invalid = refreshed;
    invalid.items[2].mediaUrl = "https://evil.invalid/hidden.mp4";
    Expect(!cache.Save(invalid).ok, "hidden records must still pass full security validation");
    ExpectOfficialPolicy(cache.Items());
    legacy["wallpapers"][2]["downloadUrl"] = "https://static.lumedesktop.com/lume/hidden.jpg";
    WriteText(path, legacy.dump());
    Expect(!cache.Load().ok, "hidden records cannot bypass cache collection validation");
    ExpectOfficialPolicy(cache.Items());
}
void HiddenOfficialDownloadsRemainAvailable(const std::filesystem::path& base) {
    const auto root = base / "hidden-official-downloads";
    std::filesystem::create_directories(root / "media");
    const auto parsed = ParseCatalog(OfficialPolicyManifest().dump());
    Expect(parsed.error.empty() && parsed.items.size() == 9,
        "library parser does not apply official listing policy");
    Library library(root.u8string());
    for (std::size_t index = 2; index < 6; ++index) {
        const auto& item = parsed.items[index];
        const auto path = root / "media" / (item.id + ".apk");
        WriteText(path, "LUMEWP02fixture");
        Expect(library.RegisterDownload(item, path.u8string()).ok,
            "previously downloaded hidden items remain valid library records");
    }
    CatalogCache cache(root.u8string());
    auto refreshed = parsed;
    refreshed.videoCatalogLoaded = true;
    Expect(cache.Save(refreshed).ok, "refresh official listing alongside existing downloads");
    Library restored(root.u8string());
    Expect(restored.Load().ok && restored.Items().size() == 4,
        "all four hidden official downloads survive library reload");
    for (std::size_t index = 2; index < 6; ++index) {
        const auto& item = parsed.items[index];
        Expect(restored.Find(item.id).has_value() &&
            FileText(root / "media" / (item.id + ".apk")) == "LUMEWP02fixture",
            "official listing changes never remove or modify downloaded files");
    }
}
}
int main() {
    CatalogValidation();
    CatalogPresentationValidation();
    OfficialCatalogPolicyPreservesMedia();
    const auto fixtureRoot = std::filesystem::absolute(std::filesystem::current_path() / "build-artifacts" / "fixtures");
    const auto base = fixtureRoot /
        ("lume-wallpaper-library-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(base);
    LibraryLifecycle(base);
    LibraryRegistrationOwnership(base);
    LibraryRegistrationFailureCleansDownloadedFile(base);
    LibraryReplacementCleansOldFile(base);
    LibraryBackupRecovery(base);
    CatalogCacheLifecycle(base);
    CatalogPresentationRoundTrip(base);
    OfficialCatalogCachePolicy(base);
    HiddenOfficialDownloadsRemainAvailable(base);
    // Only this test's uniquely created temporary directory is removed.
    Expect(std::filesystem::weakly_canonical(base).parent_path() ==
        std::filesystem::weakly_canonical(fixtureRoot), "fixture cleanup remains in explicitly owned directory");
    std::filesystem::remove_all(base);
    std::cout << "wallpaper catalog/library tests passed\n";
}
