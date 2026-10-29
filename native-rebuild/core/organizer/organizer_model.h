#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace lume::organizer {
inline constexpr std::size_t MaxItems=120,MaxCollections=64,MaxStateBytes=1024*1024,MaxTitleLength=80,MaxDismissed=240;
inline constexpr const char* DefaultCollectionId="default";
enum class ItemKind { File,Folder,Shortcut };
enum class Mode { Expanded,Collapsed };
enum class Layout { Icons,List };
enum class Change { Invalid,Unchanged,Changed,Limit };
enum class DesktopRole { None,Applications,Files };
struct Bounds { int x=0,y=0,width=420,height=392; };
struct Item {
    std::string id,path,name;
    ItemKind kind=ItemKind::File;
    bool desktop=false; // An automatically maintained reference to an actual desktop entry.
};
struct Collection {
    std::string id,title;
    std::optional<Bounds> bounds;
    // The default collection's enabled flag is also the module switch, even
    // when it is a child tab. Group topology must never overwrite that flag.
    bool enabled=true,locked=false,frosted=true;
    Mode mode=Mode::Expanded;
    Layout layout=Layout::Icons;
    int iconScale=70; // New collections default to 70%; saved sizes remain unchanged.
    DesktopRole desktopRole=DesktopRole::None;
    std::vector<Item> items;
    // Desktop collections only: normalized paths of desktop entries the user
    // took out (deleted from the collection, dragged back to the desktop).
    // Desktop sync does not add them again while they stay on the desktop.
    std::vector<std::string> dismissed;
    // Tab grouping. A host (empty parent) owns geometry, icon size and material;
    // a tab names its host and only contributes title and items.
    // Tabs are ordered by their position in State::collections. A host's
    // active is empty or the id of the visible tab (itself or a child).
    std::string parent,active;
};
struct State {
    std::vector<Collection> collections;
    // Automatic desktop tabs the user deleted. They are not created again and
    // desktop sync leaves their entries out, until Reset restores them.
    bool applicationsRetired=false,filesRetired=false;
};
// Platform supplies Unicode case normalization; the domain owns no filesystem.
using TextKey=std::function<std::string(std::string_view)>;
State Defaults();
bool ValidId(std::string_view);
bool ValidBounds(const Bounds&);
std::optional<std::string> NormalizeLocalPath(std::string_view);
std::optional<std::string> SanitizeTitle(std::string_view);
std::string DisplayName(std::string_view normalizedPath,ItemKind);
bool Valid(const State&,const TextKey& pathKey);
Collection* Find(State&,std::string_view);
const Collection* Find(const State&,std::string_view);
Change AddItems(Collection&,const std::vector<Item>&,const TextKey& pathKey);
// Removes a reference, never a file. Taking an automatically maintained
// desktop entry out of a desktop collection also dismisses its path there.
Change RemoveItem(Collection&,std::string_view);
// As above for any collection: an automatically maintained desktop entry taken
// out of a custom collection is dismissed in the desktop collections too, so
// desktop sync does not bring it back.
Change RemoveItem(State&,std::string_view collectionId,std::string_view itemId);
// Records that the user took this desktop entry out of every collection.
void DismissDesktopPath(State&,const std::string& path);
Change Reorder(Collection&,const std::vector<std::string>& ids);
Change Rename(Collection&,std::string_view title);
Change CreateCollection(State&,std::string id,std::string_view title="新建收纳",std::optional<Bounds> bounds={});
Change RemoveCollection(State&,std::string_view);
Change Reset(Collection&);
// Tab groups. Tabs() lists the host first, then its children in state order.
// Host() of a tab id is its parent; of a host id, itself.
const Collection* Host(const State&,std::string_view id);
std::vector<const Collection*> Tabs(const State&,std::string_view hostId);
// The visible tab of a host: active if still a member, else the host itself.
const Collection* ActiveTab(const State&,std::string_view hostId);
// Moves every tab of source's group into target's group (appended after the
// target's last tab). The source host's window state is discarded and the
// first merged tab becomes active. Same-group merges are refused.
Change MergeGroup(State&,std::string_view sourceHostId,std::string_view targetHostId);
// Detaches one tab into its own host window at bounds. Detaching a group's
// host promotes its next tab to host, inheriting the window state.
Change DetachTab(State&,std::string_view tabId,Bounds bounds);
Change ActivateTab(State&,std::string_view hostId,std::string_view tabId);
// Moves one tab to position `to` (0-based, clamped) within its group. A tab
// moved to the front becomes the host and inherits the window state; the
// visible tab stays the same.
Change MoveTab(State&,std::string_view tabId,std::size_t to);
// Copies each host's mode/layout/material/lock onto its tabs and clears tab
// window fields. Call after any host edit.
void SyncGroups(State&);
// Removes non-default collections that were closed while empty. Earlier
// builds offered no way to reopen them, so they only consumed slots.
bool PruneClosedEmpty(State&);
// Removes a collection and its items. A removed host promotes its next tab.
// The default collection cannot be removed.
Change DeleteTab(State&,std::string_view tabId);
// Moves items between any two collections; duplicates in the target are
// dropped from the source rather than copied twice.
Change MoveItems(State&,std::string_view fromId,std::string_view toId,const std::vector<std::string>& itemIds,const TextKey& pathKey);
// Search keys must implement NFKC + lowercase, matching original organizer.js.
bool HasSearchTerms(std::string_view,const TextKey& searchKey);
std::vector<std::size_t> Search(const Collection&,std::string_view,const TextKey& searchKey);
}
