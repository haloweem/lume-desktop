#include "core/dock/dock_model.h"
#include "core/ui/page_bridge.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>

using namespace lume::dock;
namespace {
int failures = 0;
void Check(bool ok, const char* text) { if (!ok) { std::cerr << "FAIL: " << text << '\n'; ++failures; } }
std::string Lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
const TextKey key = [](std::string_view text) { return Lower(text); };
}  // namespace
int main() {
    // Launchpad merge, de-duplication, ordering and "added" marking.
    const auto items = LaunchpadItems({"C:\\Users\\a\\Desktop\\Zed.lnk", "C:\\Users\\a\\Desktop\\notes.txt",
                                       "C:\\Users\\a\\Desktop\\Apps"},
                                      {"D:\\Tools\\calc.exe", "c:\\users\\a\\desktop\\NOTES.TXT"}, {}, key, key);
    Check(items.size() == 4, "duplicate added/desktop path appears once");
    Check(items[0].name == "Apps" && items[1].name == "calc.exe" && items[2].name == "notes.txt" && items[3].name == "Zed",
          "items are sorted by case-insensitive name; shortcuts drop .lnk");
    Check(items[2].added && items[1].added && !items[0].added, "user-added items are marked even when also on the desktop");

    // A saved arrangement comes first; unlisted items follow by name.
    const auto arranged = LaunchpadItems({"C:\\D\\b.txt", "C:\\D\\a.txt", "C:\\D\\c.txt"}, {},
                                         {"c:\\d\\C.TXT", "C:\\D\\gone.txt", "C:\\D\\b.txt"}, key, key);
    Check(arranged.size() == 3 && arranged[0].name == "c.txt" && arranged[1].name == "b.txt" && arranged[2].name == "a.txt",
          "arrangement order, stale entries ignored");
    const auto moved = Moved(arranged, 0, 2);
    Check(moved.size() == 3 && moved[0] == "C:\\D\\b.txt" && moved[2] == "C:\\D\\c.txt", "moved arrangement");
    // Search terms all must match.
    Check(Filter(items, "  ", key).size() == 4, "blank query keeps everything");
    Check(Filter(items, "ZE", key).size() == 1 && Filter(items, "ZE", key)[0].name == "Zed", "case-insensitive term");
    Check(Filter(items, "no txt", key).size() == 1 && Filter(items, "no exe", key).empty(), "every term must occur");

    // Launchpad collections hide their members from the flat grid, expose one
    // tile, and return the members when the collection is removed.
    Preferences folders;
    Check(CreateCollection(folders, "c1", "项目") == Change::Changed, "create launchpad collection");
    Check(AddToCollection(folders, "c1", {"C:\\D\\a.txt", "C:\\D\\b.txt"}, key) == Change::Changed,
          "add paths to launchpad collection");
    const auto collected = LaunchpadItems({"C:\\D\\a.txt", "C:\\D\\c.txt"}, folders.added, {}, key, key,
                                          folders.collections);
    Check(collected.size() == 2, "collection members are hidden from the flat grid");
    const auto tile = std::find_if(collected.begin(), collected.end(), [](const auto& item) { return item.collection; });
    Check(tile != collected.end() && tile->collectionId == "c1" && tile->name == "项目",
          "collection tile is exposed");
    Check(tile != collected.end() && tile->previewPaths == folders.collections[0].paths,
          "collection preview contains its members in saved order");
    Check(CollectionItems(folders.collections[0], key).size() == 2, "collection contents are readable");
    Check(RenameCollection(folders, "c1", "归档") == Change::Changed &&
          LaunchpadItems({}, {}, {}, key, key, folders.collections)[0].name == "归档", "rename collection");
    Check(ArrangeCollection(folders, "c1", {"C:\\D\\b.txt", "C:\\D\\a.txt"}, key) == Change::Changed &&
          CollectionItems(folders.collections[0], key)[0].path == "C:\\D\\b.txt", "collection arrangement");
    Check(LaunchpadItems({}, {}, {}, key, key, folders.collections)[0].previewPaths == folders.collections[0].paths,
          "collection preview follows member reorder");
    Check(RemoveFromCollection(folders, "c1", "C:\\D\\b.txt", key) == Change::Changed, "remove collection member");
    const auto ordered = LaunchpadItems({}, {}, {CollectionKey("c1")}, key, key, folders.collections);
    Check(!ordered.empty() && ordered.front().collection, "collection order key places the tile first");
    Preferences restoredPrefs;
    std::string folderText;
    Check(Encode(folders, folderText) && Decode(folderText, restoredPrefs, key) &&
          restoredPrefs.collections.size() == 1 && restoredPrefs.collections[0].title == "归档",
          "collection preferences round-trip");
    Check(LaunchpadItems({}, {}, {}, key, key, restoredPrefs.collections)[0].previewPaths == folders.collections[0].paths,
          "preview rebuilds from persisted members after removal and restart");
    const Collection preview{"preview", "Preview", {"C:\\D\\1.exe", "C:\\D\\2.exe", "C:\\D\\3.exe",
                                                   "C:\\D\\4.exe", "C:\\D\\5.exe"}};
    Check(LaunchpadItems({}, {}, {}, key, key, {preview})[0].previewPaths ==
          std::vector<std::string>(preview.paths.begin(), preview.paths.begin() + 4), "preview bounded to first four members");
    Check(LaunchpadItems({}, {}, {}, key, key, {{"empty", "Empty", {}}})[0].previewPaths.empty(),
          "empty collection has no member previews");
    Check(DeleteCollection(folders, "c1") == Change::Changed && folders.collections.empty(),
          "delete collection");
    const auto restored = LaunchpadItems({}, folders.added, {}, key, key, folders.collections);
    Check(restored.size() == 2, "deleted collection returns its members to the launchpad");

    // Paging: 7 x 3 per page, clamped.
    std::vector<Entry> many(45);
    for (std::size_t i = 0; i < many.size(); ++i) many[i].name = std::to_string(i);
    Check(PageCount(0) == 1 && PageCount(21) == 1 && PageCount(22) == 2 && PageCount(45) == 3, "page count");
    Check(PageItems(many, 0).size() == 21 && PageItems(many, 2).size() == 3 && PageItems(many, 2)[0].name == "42", "page slice");
    Check(PageItems(many, 9).size() == 3 && PageItems(many, -4).size() == 21, "out-of-range pages clamp");

    // Dock: launchpad, Lume, pinned (running dot via resolved target), separator, other running programs.
    const std::vector<std::string> pinned{"C:\\Users\\a\\Desktop\\Chrome.lnk", "C:\\Apps\\music.exe"};
    const std::vector<std::string> targets{"C:\\Program Files\\Chrome\\chrome.exe", "C:\\Apps\\music.exe"};
    const auto dockItems = DockItems(pinned, targets,
        {{"c:\\program files\\chrome\\CHROME.EXE", "Chrome"}, {"C:\\Apps\\editor.exe", "Editor"}, {"C:\\Apps\\editor.exe", "Editor"}}, key);
    Check(dockItems.size() == 6, "launchpad, Lume, 2 pinned, separator, 1 running");
    Check(dockItems[0].kind == Kind::Launchpad && dockItems[1].kind == Kind::Lume, "fixed first two items");
    Check(dockItems[2].kind == Kind::Pinned && dockItems[2].running && dockItems[2].name == "Chrome", "shortcut target matches running program");
    Check(dockItems[3].kind == Kind::Pinned && !dockItems[3].running, "idle pinned program has no dot");
    Check(dockItems[4].kind == Kind::Separator && dockItems[5].kind == Kind::Running && dockItems[5].name == "Editor" &&
          dockItems[5].running, "unpinned running program follows a separator once");
    Check(DockItems({}, {}, {}, key).size() == 2, "no separator without running extras");
    {   // A launcher whose UI runs from a subfolder under another name keeps one icon.
        const std::vector<std::string> launcher{"C:\\Users\\a\\Desktop\\WeGame.lnk"};
        const std::vector<std::string> launcherTarget{"C:\\Program Files (x86)\\WeGame\\wegame.exe"};
        const auto claimed = DockItems(launcher, launcherTarget,
            {{"C:\\Program Files (x86)\\WeGame\\x64\\wegameui.exe", "WeGame UI"}}, key);
        Check(claimed.size() == 3 && claimed[2].running &&
              claimed[2].runningProgram == "C:\\Program Files (x86)\\WeGame\\x64\\wegameui.exe",
              "pinned launcher claims its UI program from a subfolder");
        const auto sibling = DockItems(launcher, launcherTarget,
            {{"C:\\Program Files (x86)\\WeGame\\other.exe", "Other"}}, key);
        Check(sibling.size() == 5 && !sibling[2].running, "a program beside the target is a separate program");
        const auto shared = DockItems({"C:\\Apps\\music.exe"}, {"C:\\Apps\\music.exe"},
            {{"C:\\Apps\\tools\\editor.exe", "Editor"}}, key);
        Check(shared.size() == 5 && !shared[2].running, "a shallow shared folder does not claim programs");
        const auto system = DockItems({"C:\\Windows\\System32\\cmd.exe"}, {"C:\\Windows\\System32\\cmd.exe"},
            {{"C:\\Windows\\System32\\wbem\\wmic.exe", "WMIC"}}, key);
        Check(system.size() == 5 && !system[2].running, "Windows folders never claim programs");
    }
    {   // A program whose windows are all minimized appears only as its window thumbnails,
        // but a pinned one still shows its running dot.
        RunningApp hidden{"C:\\Apps\\music.exe", "Music"}, other{"C:\\Apps\\editor.exe", "Editor"};
        hidden.asWindows = other.asWindows = true;
        const auto quiet = DockItems(pinned, targets, {hidden, other}, key);
        Check(quiet.size() == 4 && quiet[3].running, "minimized-only programs add no running icon; pinned keeps its dot");
    }
    const auto movedLume = DockItems(pinned, targets, {}, key, true, 1);
    Check(movedLume.size() == 4 && movedLume[0].kind == Kind::Launchpad &&
          movedLume[1].kind == Kind::Pinned && movedLume[2].kind == Kind::Lume &&
          movedLume[1].orderIndex == 0 && movedLume[2].orderIndex == 1,
          "Lume follows its saved insertion slot");
    Check(DockItems(pinned, targets, {}, key, false, 0).size() == 3 &&
          DockItems(pinned, targets, {}, key, false, 0)[1].kind == Kind::Pinned,
          "Lume can be hidden without removing pinned items");

    // Pin, unpin, add, remove and validation.
    Preferences prefs;
    Check(Pin(prefs, "C:\\Apps\\a.exe", key) == Change::Changed && Pin(prefs, "c:\\apps\\A.EXE", key) == Change::Unchanged,
          "pin once per path key");
    Check(Pin(prefs, "relative\\a.exe", key) == Change::Invalid, "relative paths rejected");
    Check(IsPinned(prefs, "C:\\APPS\\a.exe", key) && Unpin(prefs, "c:\\apps\\a.exe", key) == Change::Changed &&
          prefs.pinned.empty(), "unpin by path key");
    for (std::size_t i = 0; i < MaxPinned; ++i) Pin(prefs, "C:\\Apps\\" + std::to_string(i) + ".exe", key);
    Check(Pin(prefs, "C:\\Apps\\extra.exe", key) == Change::Limit, "pin limit");
    Check(Add(prefs, {"C:\\Docs", "C:\\Docs\\a.txt", "c:\\docs"}, key) == Change::Changed && prefs.added.size() == 2,
          "add de-duplicates");
    Check(Add(prefs, {"C:\\Docs"}, key) == Change::Unchanged, "re-adding is unchanged");
    Check(Add(prefs, {"C:\\Docs\\b.txt", "bad"}, key) == Change::Invalid && prefs.added.size() == 2, "invalid batch is atomic");
    Check(RemoveAdded(prefs, "c:\\docs\\A.TXT", key) == Change::Changed && prefs.added.size() == 1, "remove added");

    // Pinned reordering and arrangement.
    Preferences order;
    for (const auto* path : {"C:\\A\\1.exe", "C:\\A\\2.exe", "C:\\A\\3.exe"}) Pin(order, path, key);
    Check(MovePinned(order, 0, 2) == Change::Changed && order.pinned[2] == "C:\\A\\1.exe" && order.pinned[0] == "C:\\A\\2.exe",
          "move pinned item");
    Check(MovePinned(order, 1, 1) == Change::Unchanged && MovePinned(order, 3, 0) == Change::Invalid, "move bounds");
    Check(Arrange(order, {"C:\\X\\b", "C:\\X\\a"}, key) == Change::Changed && order.order.size() == 2, "arrange");
    Check(Arrange(order, {"C:\\X\\a", "c:\\x\\A"}, key) == Change::Invalid, "duplicate arrangement rejected");
    Check(PinAt(order, "C:\\A\\4.exe", 1, key) == Change::Changed && order.pinned.size() == 4 && order.pinned[1] == "C:\\A\\4.exe",
          "pin at a position");
    Check(PinAt(order, "c:\\a\\4.EXE", 9, key) == Change::Changed && order.pinned[3] == "C:\\A\\4.exe" && order.pinned.size() == 4,
          "pinning an already pinned path moves it");
    order.lumePosition = 2;
    Check(Unpin(order, "C:\\A\\2.exe", key) == Change::Changed && order.lumePosition == 1,
          "removing a pinned item before Lume shifts its slot");
    Check(PinAt(order, "C:\\A\\5.exe", 1, key) == Change::Changed && order.lumePosition == 2,
          "inserting before Lume shifts its slot");
    // Encoding round trip and strict decoding.
    prefs.enabled = true;
    prefs.frosted = true;
    prefs.order = {"C:\\X\\b", "C:\\X\\a"};
    std::string text;
    Preferences decoded;
    Check(Encode(prefs, text) && Decode(text, decoded, key) && decoded.enabled && decoded.frosted &&
          decoded.pinned == prefs.pinned && decoded.added == prefs.added && decoded.order == prefs.order, "round trip");
    Check(Decode("{\"version\":1,\"enabled\":true,\"frosted\":false,\"pinned\":[],\"added\":[]}", decoded, key) &&
          decoded.order.empty(), "files saved before the arrangement still load");
    Check(!Decode("{\"version\":1,\"enabled\":true,\"frosted\":false,\"pinned\":[\"C:\\\\a\",\"c:\\\\A\"],\"added\":[]}", decoded, key),
          "duplicate pinned paths rejected");
    Check(!Decode("{\"version\":2,\"enabled\":true,\"frosted\":false,\"pinned\":[],\"added\":[]}", decoded, key), "unknown version");
    Check(!Decode("{\"version\":1,\"enabled\":true,\"frosted\":false,\"pinned\":[]}", decoded, key), "missing field");
    Check(!Decode(std::string("{\"version\":1}\0", 14), decoded, key) && !Decode("not json", decoded, key), "malformed input");

    Preferences appearance;
    appearance.position = 0; appearance.bottomOffset = 160; appearance.iconSpacing = 24;
    Check(Encode(appearance, text) && Decode(text, decoded, key) && decoded.position == 0 &&
          decoded.bottomOffset == 160 && decoded.iconSpacing == 24, "Dock position and spacing survive save/reopen");
    appearance.launchpadScale=83;appearance.launchpadIconScale=50;
    Check(Encode(appearance,text)&&Decode(text,decoded,key)&&decoded.launchpadScale==83&&decoded.launchpadIconScale==50,"launchpad size and icons persist independently");
    for(const auto* invalid:{"\"launchpadScale\":59","\"launchpadScale\":141","\"launchpadIconScale\":49","\"launchpadIconScale\":121","\"launchpadScale\":4294967396"})
        Check(!Decode(std::string("{\"version\":1,\"enabled\":true,\"frosted\":false,\"pinned\":[],\"added\":[],")+invalid+"}",decoded,key),"launchpad scale rejects out of bounds and integer overflow");
    Check(Decode("{\"version\":1,\"enabled\":true,\"frosted\":false,\"pinned\":[],\"added\":[],\"iconScale\":140}",decoded,key)&&decoded.iconScale==120,"old Dock icon scale migrates without losing preferences");
    appearance.pinned = {"C:\\A\\1.exe", "C:\\A\\2.exe"};
    appearance.lumeVisible = false;
    appearance.lumePosition = 2;
    Check(Encode(appearance, text) && Decode(text, decoded, key) && !decoded.lumeVisible &&
          decoded.lumePosition == 2, "Lume visibility and position survive save/reopen");
    Check(Decode("{\"version\":1,\"enabled\":true,\"frosted\":false,\"pinned\":[],\"added\":[]}", decoded, key) &&
          decoded.position == 50 && decoded.bottomOffset == 0 && decoded.iconSpacing == 10,
          "older preferences preserve the original Dock layout");
    for (const auto* invalid : {"\"position\":101", "\"position\":-1", "\"bottomOffset\":-20",
                               "\"bottomOffset\":180", "\"iconSpacing\":3", "\"iconSpacing\":25",
                               "\"iconSpacing\":\"10\"",
                               "\"lumePosition\":33", "\"lumeVisible\":\"false\""})
        Check(!Decode(std::string("{\"version\":1,\"enabled\":true,\"frosted\":false,\"pinned\":[],\"added\":[],") +
                      invalid + "}", decoded, key), "invalid layout preference is rejected");
    using lume::ui::PageCommand;
    Check(lume::ui::ParsePageCommand(L"v1|dock|position|75") ==
              static_cast<PageCommand>(static_cast<int>(PageCommand::DockPositionFirst) + 75) &&
          lume::ui::ParsePageCommand(L"v1|dock|bottomOffset|160") ==
              static_cast<PageCommand>(static_cast<int>(PageCommand::DockBottomOffsetFirst) + 160) &&
          lume::ui::ParsePageCommand(L"v1|dock|iconSpacing|24") ==
              static_cast<PageCommand>(static_cast<int>(PageCommand::DockIconSpacingFirst) + 20) &&
          lume::ui::ParsePageCommand(L"v1|dock|iconScale|51") ==
              static_cast<PageCommand>(static_cast<int>(PageCommand::DockIconScaleFirst) + 1) &&
          lume::ui::ParsePageCommand(L"v1|dock|position|075") == PageCommand::Invalid &&
          lume::ui::ParsePageCommand(L"v1|dock|iconSpacing|25") == PageCommand::Invalid &&
          lume::ui::ParsePageCommand(L"v1|dock|bottomOffset|161") == PageCommand::Invalid,
          "native layout commands accept every integer in range");
    Check(lume::ui::ParsePageCommand(L"v1|dock|launchpadScale|60")==PageCommand::DockLaunchpadScaleFirst&&
        lume::ui::ParsePageCommand(L"v1|dock|launchpadIconScale|120")==PageCommand::DockLaunchpadIconScaleLast&&
        lume::ui::ParsePageCommand(L"v1|dock|launchpadIconScale|121")==PageCommand::Invalid,"launchpad numeric commands enforce separate bounds");

    {
        Preferences grouped;
        const std::string a = "C:\\CollectionTest\\a.exe", b = "C:\\CollectionTest\\b.exe";
        grouped.pinned = {a, b};
        Check(CreateCollection(grouped, "dock1", "Dock group") == Change::Changed &&
              AddToCollection(grouped, "dock1", {a}, key) == Change::Changed &&
              PinCollection(grouped, "dock1", key) == Change::Changed && grouped.pinned == std::vector<std::string>{b},
              "pinning a launchpad collection moves its pinned members atomically");
        auto visible = DockItemsFor(grouped, grouped.pinned, {}, key);
        Check(visible[0].kind == Kind::Launchpad && visible[1].kind == Kind::Collection &&
              visible[1].path == CollectionKey("dock1") && visible[1].previewPaths == std::vector<std::string>{a},
              "new collection follows launchpad and previews saved members");
        Check(MoveToDockCollection(grouped, "dock1", {b}, key) == Change::Changed && grouped.pinned.empty() &&
              FindCollection(grouped, "dock1")->paths == std::vector<std::string>{a, b}, "Dock icon moves into collection");
        Check(MoveFromDockCollection(grouped, "dock1", a, 0, key) == Change::Changed &&
              grouped.pinned == std::vector<std::string>{a} && FindCollection(grouped, "dock1")->paths == std::vector<std::string>{b},
              "member moves back to Dock without losing its original path");
        Check(RenameCollection(grouped, "dock1", "Renamed") == Change::Changed &&
              DockItemsFor(grouped, grouped.pinned, {}, key)[1].name == "Renamed", "Dock label follows shared collection title");
        std::string saved;
        Preferences reopened;
        Check(Encode(grouped, saved) && Decode(saved, reopened, key) && reopened.dockCollections == grouped.dockCollections &&
              FindCollection(reopened, "dock1")->paths == std::vector<std::string>{b}, "Dock collection persists across restart");
        const auto unchanged = saved;
        Check(MoveToDockCollection(grouped, "dock1", {a, "https://invalid"}, key) == Change::Invalid &&
              Encode(grouped, saved) && saved == unchanged, "failed mixed drop changes neither pinned icons nor members");
        grouped.dockCollections.push_back("dock1");
        Check(!Valid(grouped, key), "duplicate Dock binding rejected");
        grouped.dockCollections = {"missing"};
        Check(!Valid(grouped, key), "orphaned Dock binding rejected");
        grouped = reopened;
        for (std::size_t i = grouped.pinned.size(); i < MaxPinned; ++i)
            grouped.pinned.push_back("C:\\CollectionTest\\full" + std::to_string(i) + ".exe");
        Check(Encode(grouped, saved), "encode full Dock fixture");
        const auto full = saved;
        Check(MoveFromDockCollection(grouped, "dock1", b, 0, key) == Change::Limit &&
              Encode(grouped, saved) && saved == full, "full Dock rejects drag-out without removing member");
        grouped = reopened;
        Check(DeleteCollection(grouped, "dock1") == Change::Changed && grouped.dockCollections.empty() &&
              std::find(grouped.added.begin(), grouped.added.end(), b) != grouped.added.end(),
              "deleting collection removes binding while keeping its files in launchpad");
        Preferences capacity;
        CreateCollection(capacity, "full", "Full");
        PinCollection(capacity, "full", key);
        capacity.pinned = {a};
        for (std::size_t i = 0; i < MaxAdded; ++i) capacity.added.push_back("C:\\CollectionTest\\member" + std::to_string(i));
        Check(MoveToDockCollection(capacity, "full", {a}, key) == Change::Limit && capacity.pinned == std::vector<std::string>{a} &&
              FindCollection(capacity, "full")->paths.empty(), "full collection preserves source Dock icon");
    }
    {
        Item lume{Kind::Lume}, pinnedItem{Kind::Pinned}, runningItem{Kind::Running}, separator{Kind::Separator},
            launchpad{Kind::Launchpad}, collection{Kind::Collection}, windowItem{Kind::Window};
        Check(Movable(lume) && Movable(pinnedItem) && Movable(runningItem) && !Movable(separator) && !Movable(launchpad) &&
              !Movable(collection) && !Movable(windowItem), "movable kinds");
        Check(Removable(lume) && Removable(pinnedItem) && !Removable(runningItem) && !Removable(separator) &&
              !Removable(launchpad) && !Removable(collection) && !Removable(windowItem), "removable kinds");
        pinnedItem.path = "C:\\A.exe"; windowItem.path = "C:\\A.exe"; windowItem.window = 42;
        Item other = windowItem; other.window = 43;
        Check(MotionKey(pinnedItem) == "2|C:\\A.exe" && MotionKey(windowItem) == "6|C:\\A.exe#42" &&
              MotionKey(windowItem) != MotionKey(other), "motion keys");
    }
    if (failures) return 1;
    std::cout << "dock model passed\n";
    return 0;
}
