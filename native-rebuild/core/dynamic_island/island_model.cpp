#include "core/dynamic_island/island_model.h"
#include <algorithm>
#include <cmath>
#include <utility>
namespace lume::dynamic_island {
void Model::Update(Media media) {
    media.title.resize(std::min<std::size_t>(media.title.size(), 512));
    media.artist.resize(std::min<std::size_t>(media.artist.size(), 512));
    if (!media.available || media.id.empty() || media.id.size()>1024 || !media.session) media = {};
    if (!std::isfinite(media.duration) || media.duration < 0) media.duration = 0;
    media.duration = std::min(media.duration,604800.0);
    if (!std::isfinite(media.position)) media.position = 0;
    media.position = std::clamp(media.position, 0.0, media.duration);
    media.energy = std::isfinite(media.energy) ? std::clamp(media.energy, 0.0f, 1.0f) : 0.0f;
    for (auto& value : media.spectrum)
        value = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
    media_ = std::move(media);
}
void Model::StartFocus(std::uint64_t now) {
    deadline_ = now + 25ULL * 60 * 1000;
    remaining_ = 25 * 60;
    finished_ = false;
}
void Model::CancelFocus() { deadline_ = 0; remaining_ = 0; finished_ = false; }
bool Model::Tick(std::uint64_t now) {
    if (!deadline_) return false;
    const auto before = remaining_;
    if (now >= deadline_) { deadline_ = 0; remaining_ = 0; finished_ = true; }
    else remaining_ = static_cast<unsigned>((deadline_ - now + 999) / 1000);
    return before != remaining_;
}
}
