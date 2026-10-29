#pragma once
#include "core/organizer/organizer_model.h"
#include "core/settings/file_store.h"
namespace lume::organizer {
enum class LoadState { Fresh,Loaded,Recovered,Failed };
struct LoadedState { State value;LoadState state=LoadState::Fresh; };
bool Encode(const State&,std::string&);
bool Decode(std::string_view,State&,const TextKey&);
class Store final {
public:
    Store(settings::IFileStore& files,TextKey pathKey):files_(files),pathKey_(std::move(pathKey)){}
    LoadedState Load(const std::string&) const;
    bool Save(const std::string&,const State&);
private:
    settings::IFileStore& files_;TextKey pathKey_;
};
}
