#pragma once
#include "core/settings/file_store.h"
#include "core/taskbar/taskbar_state.h"
#include <string_view>

namespace lume::taskbar {
constexpr std::size_t MaxRecordBytes = 16384;
constexpr std::size_t MaxTaskbars = 32;
bool Encode(const Record&, std::string&);
bool Decode(std::string_view, Record&);
enum class LoadState { Fresh, Loaded, Failed };
struct LoadedRecord { Record record; LoadState state = LoadState::Fresh; };
class RecordStore final {
public:
    explicit RecordStore(settings::IFileStore& files) : files_(files) {}
    LoadedRecord Load(const std::string&) const;
    bool Save(const std::string&, const Record&);
private:
    settings::IFileStore& files_;
};
}  // namespace lume::taskbar
