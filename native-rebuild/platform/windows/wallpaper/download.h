#pragma once

#include "core/wallpaper/catalog.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace lume::platform::windows::wallpaper {

struct DownloadProgress {
    std::uint64_t receivedBytes = 0;
    std::uint64_t totalBytes = 0;
    unsigned attempt = 0;
};

struct DownloadResult {
    bool ok = false;
    bool cancelled = false;
    std::string path;
    std::string error;
    std::uint64_t receivedBytes = 0;
};

// Native encryption owns its key and authenticated file format. Download feeds
// at most 64 KiB at a time; plaintext is never written to disk by this module.
class IProtectedDownloadWriter {
public:
    virtual ~IProtectedDownloadWriter() = default;
    virtual bool Append(const std::uint8_t* bytes, std::size_t size, std::string& error) noexcept = 0;
    virtual bool Finish(std::string& error) noexcept = 0;
};
using ProtectedWriterFactory = std::function<std::unique_ptr<IProtectedDownloadWriter>(
    const std::string& stagingPath, std::string& error)>;
using ProgressCallback = std::function<void(const DownloadProgress&)>;

struct DownloadOptions {
    ProtectedWriterFactory protectedWriter;
    // Explicit fixture-only escape hatch. Production callers leave this false.
    bool allowLoopbackForTests = false;
};

struct CatalogFetchOptions {
    // Production callers use the fixed official endpoints and HTTPS policy.
    std::string videoUrl = lume::wallpaper::kOfficialCatalogUrl;
    std::string imageUrl = lume::wallpaper::kStaticCatalogUrl;
    bool allowLoopbackForTests = false;
};

// Native authenticated cache v2. A per-library 32-byte key is protected using
// DPAPI CurrentUser in root/cache-key.bin. No legacy cache migration is attempted.
class EncryptedMediaReader final {
public:
    ~EncryptedMediaReader();
    EncryptedMediaReader(const EncryptedMediaReader&) = delete;
    EncryptedMediaReader& operator=(const EncryptedMediaReader&) = delete;
    static std::shared_ptr<EncryptedMediaReader> Open(const std::string& root,
        const std::string& path, std::string& error) noexcept;
    std::uint64_t Size() const noexcept;
    bool Read(std::uint64_t offset, char* buffer, std::size_t capacity,
        std::size_t& read) noexcept;
private:
    struct Impl;
    explicit EncryptedMediaReader(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
ProtectedWriterFactory MakeProtectedWriterFactory(const std::string& libraryRoot) noexcept;

// Blocking worker operations: never call on the UI thread. Every network
// operation polls cancellation and closes its request handle to abort promptly.
lume::wallpaper::CatalogResult FetchCatalog(const std::atomic<bool>& cancel,
    const CatalogFetchOptions& options = {}) noexcept;
DownloadResult Download(const lume::wallpaper::CatalogItem& item, const std::string& destination,
    const std::atomic<bool>& cancel, const ProgressCallback& progress = {},
    const DownloadOptions& options = {}) noexcept;

} // namespace lume::platform::windows::wallpaper
