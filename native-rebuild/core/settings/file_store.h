#pragma once
#include <string>
#include <utility>

namespace lume::settings {
class IFileStore {
public:
    virtual ~IFileStore() = default;
    virtual bool Read(const std::string& path, std::string& out) const = 0;
    // In-memory adapters may use this default. Disk adapters should reject the
    // size before allocating/reading the payload.
    virtual bool ReadBounded(const std::string& path,std::size_t limit,std::string& out) const {
        std::string value;
        if(!Read(path,value)||value.size()>limit)return false;
        out=std::move(value);return true;
    }
    virtual bool Exists(const std::string& path) const = 0;
    virtual bool Write(const std::string& path, const std::string& value) = 0;
    // Success replaces the target. Failure leaves both source and target unchanged.
    virtual bool Rename(const std::string& from, const std::string& to) = 0;
    virtual bool Remove(const std::string& path) = 0;
};
}  // namespace lume::settings
