#pragma once

#include "core/dynamic_island/island_shape.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lume::platform::windows {

// One geometry-only mask for the island's current outline. Content and opacity
// are never retained; reusing it for a hover repaint preserves every BGRA bit.
class IslandShapeMask final {
public:
    bool Apply(WidgetBitmap& bitmap, float width, float height, float radius,
               float mix, float scale) {
        const std::size_t count = static_cast<std::size_t>(bitmap.width) * bitmap.height;
        if (!bitmap.width || !bitmap.height || count > MaxPixels || bitmap.pixels.size() != count ||
            !std::isfinite(width) || !std::isfinite(height) || !std::isfinite(radius) ||
            !std::isfinite(mix) || !std::isfinite(scale) || width <= 0 || height <= 0 ||
            width > 8192 || height > 8192 || radius < 0 || scale <= 0) return false;
        const Key next{bitmap.width, bitmap.height, width, height, radius, mix, scale};
        if (!valid_ || !(key_ == next)) Build(next);
        for (const auto& entry : entries_) {
            auto& pixel = bitmap.pixels[entry.index];
            if (!pixel) continue;
            if (!entry.coverage) { pixel = 0; continue; }
            std::uint32_t result = 0;
            for (const unsigned shift : {0u, 8u, 16u, 24u})
                result |= ((((pixel >> shift) & 255) * entry.coverage + 32) / 64) << shift;
            pixel = result;
        }
        return true;
    }

    void Reset() {
        std::vector<Entry>().swap(entries_);
        valid_ = false;
    }
    std::uint64_t Generation() const { return generation_; }
    std::size_t EntryCount() const { return entries_.size(); }
    std::size_t Capacity() const { return entries_.capacity(); }

private:
    static constexpr std::size_t MaxPixels = 16777216;
    struct Entry { std::uint32_t index; std::uint8_t coverage; };
    struct Key {
        unsigned bitmapWidth, bitmapHeight;
        float width, height, radius, mix, scale;
        bool operator==(const Key& other) const {
            return bitmapWidth == other.bitmapWidth && bitmapHeight == other.bitmapHeight &&
                width == other.width && height == other.height && radius == other.radius &&
                mix == other.mix && scale == other.scale;
        }
    };
    Key key_{};
    bool valid_ = false;
    std::uint64_t generation_ = 0;
    std::vector<Entry> entries_;

    void Build(const Key& key) {
        entries_.clear();
        key_ = key;
        valid_ = true;
        ++generation_;
        // FinishShape bypasses masking entirely for the capsule endpoint.
        if (key.mix <= 0) return;
        const float mix = std::clamp(key.mix, 0.f, 1.f);
        const unsigned liveWidth = std::min(key.bitmapWidth, static_cast<unsigned>(std::ceil(key.width)));
        const unsigned liveHeight = std::min(key.bitmapHeight, static_cast<unsigned>(std::ceil(key.height)));
        const float attachedReach =
            (dynamic_island::IslandShoulder(key.height / key.scale) +
             dynamic_island::IslandLowerRadius(key.height / key.scale)) * key.scale;
        // Beyond both shapes' corner reaches the centre has no curved boundary.
        // All 8x8 sample points have nonpositive distance for both shapes when
        // their final y (y+15/16) is within the bottom. This includes the top
        // row: its first sample is +1/16, so its exact coverage is still 64.
        // Two extra pixels keep every x sample conservatively past the corners.
        const unsigned band = static_cast<unsigned>(std::min(static_cast<float>(liveWidth),
            std::ceil(std::max(key.radius, attachedReach) + 2.f)));
        const unsigned right = static_cast<unsigned>(std::clamp(
            std::floor(key.width) - static_cast<float>(band),
            static_cast<float>(band), static_cast<float>(liveWidth)));
        const auto distanceAt = [&](unsigned x, unsigned y) {
            return dynamic_island::MorphShapeDistance(
                static_cast<float>(x) + .5f, static_cast<float>(y) + .5f,
                key.width, key.height, key.radius, mix, key.scale);
        };
        const auto coverageAt = [&](unsigned x, unsigned y) {
            const float distance = distanceAt(x, y);
            if (distance <= -.708f) return 64u;
            if (distance >= .708f) return 0u;
            unsigned coverage = 0;
            for (int sy = 0; sy < 8; ++sy) for (int sx = 0; sx < 8; ++sx)
                coverage += dynamic_island::MorphShapeDistance(
                    static_cast<float>(x) + (static_cast<float>(sx) + .5f) / 8,
                    static_cast<float>(y) + (static_cast<float>(sy) + .5f) / 8,
                    key.width, key.height, key.radius, mix, key.scale) <= 0 ? 1u : 0u;
            return coverage;
        };
        const auto record = [&](unsigned x, unsigned y, unsigned coverage) {
            if (coverage < 64) entries_.push_back({
                    static_cast<std::uint32_t>(static_cast<std::size_t>(y) * key.bitmapWidth + x),
                    static_cast<std::uint8_t>(coverage)});
        };
        // On separate left/right half-planes both source distances decrease
        // toward the centre. The attached outline needs enough room for its
        // shoulder plus lower corner: narrow overlapping corners are not
        // monotone. Leave those and unusual parameter ranges on the original
        // scan, including any clipped input bitmap.
        const bool separateEdges = key.width >= static_cast<float>(2 * band + 4) &&
            key.height >= 4.f && key.radius <= std::min(key.width, key.height) * .5f &&
            key.scale >= .5f && key.scale <= 4.f &&
            key.bitmapWidth >= static_cast<unsigned>(std::ceil(key.width)) &&
            key.bitmapHeight >= static_cast<unsigned>(std::ceil(key.height));
        const auto scan = [&](unsigned y, unsigned first, unsigned end, bool fromRight) {
            const auto originalScan = [&] {
                for (unsigned x = first; x < end; ++x) record(x, y, coverageAt(x, y));
            };
            if (!separateEdges) { originalScan(); return; }
            // A fractional rightmost pixel can have its centre past width.
            // The concave shoulder's distance is not monotone there; evaluate
            // that pixel independently instead of extending the proof outside.
            if (fromRight && end > first && static_cast<float>(end - 1) + .5f > key.width) {
                record(end - 1, y, coverageAt(end - 1, y));
                --end;
            }
            const unsigned count = end - first;
            if (count < 12) { originalScan(); return; }
            const auto xAt = [&](unsigned offset) { return fromRight ? end - 1 - offset : first + offset; };
            const float outer = distanceAt(xAt(0), y), inner = distanceAt(xAt(count - 1), y);
            if (!std::isfinite(outer) || !std::isfinite(inner) || outer < inner) { originalScan(); return; }
            const auto boundary = [&](float threshold) {
                unsigned low = 0, high = count;
                while (low < high) {
                    const unsigned middle = low + (high - low) / 2;
                    if (distanceAt(xAt(middle), y) > threshold) low = middle + 1;
                    else high = middle;
                }
                return low;
            };
            // Search well away from the original +/-0.708 classifier, rather
            // than assume its floating-point result stays monotone within an
            // ulp at the threshold. In the bounded fast-path geometry all SDF
            // terms are below 2^15; roundoff is far below this >1 pixel margin.
            // One extra pixel on either side still uses the exact old path.
            unsigned exteriorEnd = boundary(2.f), interiorBegin = boundary(-2.f);
            if (exteriorEnd > interiorBegin) { originalScan(); return; }
            if ((exteriorEnd && distanceAt(xAt(exteriorEnd - 1), y) < 2.f) ||
                (interiorBegin < count && distanceAt(xAt(interiorBegin), y) > -2.f)) {
                originalScan(); return;
            }
            if (exteriorEnd) --exteriorEnd;
            if (interiorBegin < count) ++interiorBegin;
            for (unsigned offset = 0; offset < exteriorEnd; ++offset) record(xAt(offset), y, 0);
            for (unsigned offset = exteriorEnd; offset < interiorBegin; ++offset)
                record(xAt(offset), y, coverageAt(xAt(offset), y));
        };
        for (unsigned y = 0; y < liveHeight; ++y) {
            scan(y, 0, band, false);
            if (band < right && static_cast<float>(y) + .9375f > key.height) {
                // In this last partial row, every centre-strip x lies more
                // than two pixels past both corner reaches. At the centre and
                // all subpixels, y-height dominates the horizontal terms for
                // both shapes. Evaluating the unchanged formula once therefore
                // gives the identical coverage for every x in this strip.
                const unsigned coverage = coverageAt(band, y);
                if (coverage < 64) for (unsigned x = band; x < right; ++x) record(x, y, coverage);
            }
            scan(y, right, liveWidth, true);
        }
    }
};

}  // namespace lume::platform::windows
