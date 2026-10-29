#pragma once

#include "core/wallpaper/catalog.h"

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lume::wallpaper {

struct LibraryEntry {
    CatalogItem item;
    std::string localPath; // Absolute UTF-8; always underneath this library root.
    bool imported = false;
    bool protectedMedia = true;
};

struct LibraryResult {
    bool ok = false;
    std::string id;
    std::string path;
    std::string error;
};

// All calls are serialized by the owning wallpaper actor.
class Library final {
public:
    // Official media remain encrypted at rest. The resolver returns the owning
    // native protected-media service's capability URL, never a cleartext file.
    using ProtectedResolver = std::function<LibraryResult(const LibraryEntry&)>;
    explicit Library(std::string root);
    LibraryResult Load() noexcept;
    LibraryResult Save() noexcept;
    LibraryResult ImportLocal(const std::string& path, const std::atomic<bool>* cancel = nullptr) noexcept;
    LibraryResult Remove(const std::string& id) noexcept;
    std::optional<LibraryEntry> Find(const std::string& id) const noexcept;
    LibraryResult ResolvePlayablePath(const std::string& id) const noexcept;
    const std::vector<LibraryEntry>& Items() const noexcept { return items_; }
    LibraryResult RegisterDownload(const CatalogItem& item, const std::string& path) noexcept;
    void SetProtectedResolver(ProtectedResolver resolver) noexcept;
    const std::string& Root() const noexcept { return root_; }
    // True after loading a backup, until a primary load or save succeeds.
    bool Recovered() const noexcept { return recovered_; }

private:
    std::string root_;
    std::vector<LibraryEntry> items_;
    ProtectedResolver protectedResolver_;
    bool recovered_ = false;
};

} // namespace lume::wallpaper
