#pragma once
#include "core/organizer/organizer_model.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lume::dock {
inline constexpr std::size_t MaxPinned = 32, MaxAdded = 200, MaxOrder = 1000,
                                 MaxPreferencesBytes = 512 * 1024, MaxCollections = 32;
inline constexpr int Columns = 7, Rows = 3, PageSize = Columns * Rows;

// Paths are normalized UTF-8 local paths (organizer::NormalizeLocalPath).
// Appearance values are integer DIPs/percentages so the settings page can
// adjust them one unit at a time.
inline constexpr int MinIconScale = 50, MaxIconScale = 120, DefaultIconScale = 100;
inline constexpr int MinPosition = 0, MaxPosition = 100, DefaultPosition = 50;
inline constexpr int MinBottomOffset = 0, MaxBottomOffset = 160, DefaultBottomOffset = 0;
inline constexpr int MinIconSpacing = 4, MaxIconSpacing = 24, DefaultIconSpacing = 10;
inline constexpr int MinLaunchpadScale=60,MaxLaunchpadScale=140,DefaultLaunchpadScale=100;
inline constexpr int MinLaunchpadIconScale=50,MaxLaunchpadIconScale=120,DefaultLaunchpadIconScale=100;
bool ValidPosition(int);
bool ValidBottomOffset(int);
bool ValidIconSpacing(int);
bool ValidLumePosition(int, std::size_t pinnedCount);
struct Collection {
    std::string id, title;
    std::vector<std::string> paths;  // normalized local paths; hidden from the flat launchpad
};
struct Preferences {
    bool enabled = false;
    bool frosted = false;
    bool transparent = false;  // no material: a fully transparent bar ("无")
    bool reflection = false;   // mirrored icons below the row
    int iconScale = DefaultIconScale;
    int position = DefaultPosition;  // horizontal percentage of available desktop space
    int bottomOffset = DefaultBottomOffset;  // extra distance above the default bottom margin, in DIPs
    int iconSpacing = DefaultIconSpacing;  // DIPs before iconScale is applied
    int launchpadScale=DefaultLaunchpadScale;
    int launchpadIconScale=DefaultLaunchpadIconScale;
    bool lumeVisible = true;              // whether the Lume manager icon is shown
    int lumePosition = 0;                 // insertion slot among pinned items (0 = first)
    std::vector<std::string> pinned;  // Dock order around the optional Lume item.
    std::vector<std::string> added;   // Launchpad items besides the desktop.
    std::vector<std::string> order;   // Launchpad arrangement; unlisted items follow by name.
    std::vector<Collection> collections;  // launchpad folders; their paths are hidden from the flat grid
    std::vector<std::string> dockCollections;  // collection ids, immediately after the launchpad
};
using TextKey = organizer::TextKey;
bool Valid(const Preferences&, const TextKey& pathKey);
bool Encode(const Preferences&, std::string& output);
bool Decode(const std::string& input, Preferences& output, const TextKey& pathKey);

struct Entry {
    std::string path, name;
    bool added = false;
    bool collection = false;
    std::string collectionId;
    std::vector<std::string> previewPaths;  // Display only: first four members in saved order.
};
std::string EntryName(std::string_view normalizedPath);
std::string CollectionKey(std::string_view id);
bool IsCollectionKey(std::string_view value);
const Collection* FindCollection(const Preferences&, std::string_view id);
// Desktop and user-added paths merged, de-duplicated by path key and ordered
// by display name; an added path that is also on the desktop stays "added".
// Paths contained in a collection are replaced by one collection entry.
std::vector<Entry> LaunchpadItems(const std::vector<std::string>& desktop, const std::vector<std::string>& added,
                                  const std::vector<std::string>& order, const TextKey& pathKey, const TextKey& searchKey,
                                  const std::vector<Collection>& collections = {});
// Current contents of one collection, in stored order.
std::vector<Entry> CollectionItems(const Collection&, const TextKey& pathKey);
// The arrangement after moving items[from] to index `to` (paths, at most MaxOrder).
std::vector<std::string> Moved(const std::vector<Entry>& items, std::size_t from, std::size_t to);
// Every whitespace-separated term must occur in the name (NFKC lowercase keys).
std::vector<Entry> Filter(const std::vector<Entry>&, std::string_view query, const TextKey& searchKey);
int PageCount(std::size_t items);
std::vector<Entry> PageItems(const std::vector<Entry>&, int page);

enum class Kind { Launchpad, Lume, Pinned, Running, Separator, Collection, Window };
struct RunningApp {
    std::string executable, name;
    bool asWindows = false;  // shown as its window thumbnails instead of a running icon
};
// A program window shown at the end of the Dock as a live thumbnail: every
// window of a program that is not pinned, and minimized windows of pinned ones.
struct ProgramWindow {
    std::string executable, title;
    std::uint64_t handle = 0;
    bool minimized = false;
};
struct Item {
    Kind kind = Kind::Pinned;
    std::string path, name;
    bool running = false;
    // Pinned items carry their index in Preferences::pinned. The Lume item
    // carries its insertion slot among those pinned items. This keeps
    // activation and drag commits independent of the visible item index.
    std::size_t orderIndex = static_cast<std::size_t>(-1);
    std::vector<std::string> previewPaths;
    std::uint64_t window = 0;  // Kind::Window: the window; path is its program.
    std::string target;        // the program a pinned shortcut starts; empty when path is the program
    // Pinned: a running program from the pinned program's install folder that
    // stands for it (a launcher whose UI runs as another executable).
    std::string runningProgram;
};
// Items the user can drag to a new place, and drag off the Dock to remove.
bool Movable(const Item&);
bool Removable(const Item&);
// Stable identity of an item across rebuilds, for its animation state.
// Several minimized windows can share one program.
std::string MotionKey(const Item&);
// Program windows follow everything else, after a separator, in the order given.
void AppendWindows(std::vector<Item>&, const std::vector<ProgramWindow>&);
// targets[i] is pinned[i] with a shortcut resolved to its program (or the
// pinned path itself). Running programs that match no pinned target follow a
// separator in first-seen order.
std::vector<Item> DockItems(const std::vector<std::string>& pinned, const std::vector<std::string>& targets,
                            const std::vector<RunningApp>& running, const TextKey& pathKey,
                            bool lumeVisible = true, int lumePosition = 0,
                            std::string_view lumePath = {});
std::vector<Item> DockItemsFor(const Preferences&, const std::vector<std::string>& targets,
                            const std::vector<RunningApp>& running, const TextKey& pathKey,
                            std::string_view lumePath = {});

enum class Change { Invalid, Unchanged, Changed, Limit };
Change Pin(Preferences&, std::string_view path, const TextKey& pathKey);
Change Unpin(Preferences&, std::string_view path, const TextKey& pathKey);
bool IsPinned(const Preferences&, std::string_view path, const TextKey& pathKey);
Change Add(Preferences&, const std::vector<std::string>& paths, const TextKey& pathKey);
Change RemoveAdded(Preferences&, std::string_view path, const TextKey& pathKey);
Change MovePinned(Preferences&, std::size_t from, std::size_t to);
// Pins at `index` (clamped), or moves an already pinned path there.
Change PinAt(Preferences&, std::string_view path, std::size_t index, const TextKey& pathKey);
Change Arrange(Preferences&, std::vector<std::string> order, const TextKey& pathKey);
Change CreateCollection(Preferences&, std::string id, std::string title);
Change RenameCollection(Preferences&, std::string_view id, std::string_view title);
// Removes only the collection; its paths stay alive in `added` and become
// visible in the flat launchpad again.
Change DeleteCollection(Preferences&, std::string_view id);
Change AddToCollection(Preferences&, std::string_view id, const std::vector<std::string>& paths, const TextKey& pathKey);
Change RemoveFromCollection(Preferences&, std::string_view id, std::string_view path, const TextKey& pathKey);
Change ArrangeCollection(Preferences&, std::string_view id, std::vector<std::string> paths, const TextKey& pathKey);
Change PinCollection(Preferences&, std::string_view id, const TextKey& pathKey);
// Transactional moves: failure leaves both the group and pinned list intact.
Change MoveToDockCollection(Preferences&, std::string_view id, const std::vector<std::string>& paths, const TextKey& pathKey);
Change MoveFromDockCollection(Preferences&, std::string_view id, std::string_view path,
                              std::size_t slot, const TextKey& pathKey);
}  // namespace lume::dock
