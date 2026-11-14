#include "platform/windows/dynamic_island/island_shape_mask.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

using lume::platform::windows::IslandShapeMask;
using lume::platform::windows::WidgetBitmap;
namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
WidgetBitmap Content(float width, float height, unsigned extraWidth = 7, unsigned extraHeight = 5,
                     unsigned seed = 7) {
    WidgetBitmap result;
    result.width = static_cast<unsigned>(std::ceil(width)) + extraWidth;
    result.height = static_cast<unsigned>(std::ceil(height)) + extraHeight;
    result.pixels.resize(static_cast<std::size_t>(result.width) * result.height);
    for (auto& pixel : result.pixels) {
        seed = seed * 1664525u + 1013904223u;
        const unsigned choices[]{0, 17, 128, 255};
        const unsigned alpha = choices[(seed >> 24) & 3];
        const unsigned blue = (seed & 255) * alpha / 255;
        const unsigned green = ((seed >> 8) & 255) * alpha / 255;
        const unsigned red = ((seed >> 16) & 255) * alpha / 255;
        pixel = blue | (green << 8) | (red << 16) | (alpha << 24);
    }
    return result;
}
// The original FinishShape loop, deliberately without the production cache or
// centre skip, remains the pixel oracle for both geometry and integer rounding.
void Original(WidgetBitmap& bitmap, float width, float height, float radius, float mix, float scale) {
    if (mix <= 0) return;
    mix = std::clamp(mix, 0.f, 1.f);
    const unsigned liveWidth = std::min(bitmap.width, static_cast<unsigned>(std::ceil(width)));
    const unsigned liveHeight = std::min(bitmap.height, static_cast<unsigned>(std::ceil(height)));
    for (unsigned y = 0; y < liveHeight; ++y) for (unsigned x = 0; x < liveWidth; ++x) {
        auto& pixel = bitmap.pixels[static_cast<std::size_t>(y) * bitmap.width + x];
        if (!pixel) continue;
        const float distance = lume::dynamic_island::MorphShapeDistance(
            static_cast<float>(x) + .5f, static_cast<float>(y) + .5f, width, height, radius, mix, scale);
        if (distance <= -.708f) continue;
        if (distance >= .708f) { pixel = 0; continue; }
        unsigned coverage = 0;
        for (int sy = 0; sy < 8; ++sy) for (int sx = 0; sx < 8; ++sx)
            coverage += lume::dynamic_island::MorphShapeDistance(
                static_cast<float>(x) + (static_cast<float>(sx) + .5f) / 8,
                static_cast<float>(y) + (static_cast<float>(sy) + .5f) / 8,
                width, height, radius, mix, scale) <= 0 ? 1u : 0u;
        std::uint32_t result = 0;
        for (const unsigned shift : {0u, 8u, 16u, 24u})
            result |= ((((pixel >> shift) & 255) * coverage + 32) / 64) << shift;
        pixel = result;
    }
}
float Radius(float height) {
    const float expansion = std::clamp((height - 34.f) / (156.f - 34.f), 0.f, 1.f);
    return std::min(height / 2, height / 2 + (30 - height / 2) * expansion);
}
void Equivalence() {
    IslandShapeMask mask;
    const float sizes[][2]{{7.125f, 3.5f}, {120, 30}, {236.375f, 34.625f}, {372, 156}, {820.25f, 174.75f}};
    unsigned cases = 0;
    std::uint64_t compared = 0;
    for (const float scale : {.75f, 1.f, 1.25f, 1.5f, 2.f, 2.75f})
        for (const auto& size : sizes) for (const float mix : {0.f, .000001f, .125f, .5f, .875f, 1.f})
            for (int radiusChoice = 0; radiusChoice < 3; ++radiusChoice) {
                const float width = size[0] * scale, height = size[1] * scale;
                const float radius = radiusChoice == 0 ? Radius(size[1]) * scale :
                    radiusChoice == 1 ? 0.f : std::min(width, height) / 2;
                const auto source = Content(width, height, 7, 5, ++cases);
                auto expected = source, actual = source;
                Original(expected, width, height, radius, mix, scale);
                Check(mask.Apply(actual, width, height, radius, mix, scale), "valid geometry applies");
                if (actual.pixels != expected.pixels) {
                    ++failures;
                    std::cerr << "FAIL: pixel equality case=" << cases << " width=" << width << " height=" << height
                        << " radius=" << radius << " mix=" << mix << " scale=" << scale << '\n';
                    return;
                }
                compared += actual.pixels.size();
            }
    std::cout << "Exact BGRA equality: " << cases << " geometries, " << compared << " pixels\n";
}
void CacheLifecycle() {
    IslandShapeMask mask;
    float width = 236.375f, height = 34.625f, radius = 17.3125f, mix = .5f, scale = 1.25f;
    auto bitmap = Content(width, height);
    Check(mask.Apply(bitmap, width, height, radius, mix, scale), "initial cache builds");
    auto generation = mask.Generation();
    Check(generation == 1 && mask.EntryCount() && mask.Capacity() >= mask.EntryCount(), "cache retains only outline entries");
    auto changed = Content(width, height, 7, 5, 991), expected = changed;
    Original(expected, width, height, radius, mix, scale);
    mask.Apply(changed, width, height, radius, mix, scale);
    Check(mask.Generation() == generation && changed.pixels == expected.pixels, "new content reuses geometry without retaining pixels");
    const auto invalidate = [&](const char* message) {
        auto frame = Content(width, height);
        mask.Apply(frame, width, height, radius, mix, scale);
        Check(mask.Generation() == ++generation, message);
    };
    width = std::nextafter(width, std::numeric_limits<float>::infinity()); invalidate("fractional width invalidates without quantization");
    height = std::nextafter(height, std::numeric_limits<float>::infinity()); invalidate("fractional height invalidates");
    radius = std::nextafter(radius, std::numeric_limits<float>::infinity()); invalidate("radius invalidates");
    mix = std::nextafter(mix, std::numeric_limits<float>::infinity()); invalidate("mix invalidates");
    scale = std::nextafter(scale, std::numeric_limits<float>::infinity()); invalidate("scale invalidates");
    auto wider = Content(width, height, 8, 5); mask.Apply(wider, width, height, radius, mix, scale);
    Check(mask.Generation() == ++generation, "bitmap stride invalidates");
    auto taller = Content(width, height, 8, 6); mask.Apply(taller, width, height, radius, mix, scale);
    Check(mask.Generation() == ++generation, "bitmap height invalidates");
    mask.Reset();
    Check(mask.EntryCount() == 0 && mask.Capacity() == 0, "reset releases all mask storage");
    mask.Apply(taller, width, height, radius, mix, scale);
    Check(mask.Generation() == ++generation && mask.EntryCount() > 0, "reset invalidates even identical geometry");
    auto invalid = taller; invalid.pixels.pop_back();
    Check(!mask.Apply(invalid, width, height, radius, mix, scale) && mask.Generation() == generation,
        "invalid bitmap does not disturb a valid cache");
}
void FractionalEdges() {
    IslandShapeMask mask;
    unsigned cases = 0;
    for (const float edge : {.0625f, .125f, .25f, .5f, .6875f, .75f, .875f, .9375f})
        for (const float delta : {-.00001f, 0.f, .00001f})
            for (const float mix : {.000001f, .25f, .5f, 1.f})
                for (const float scale : {.75f, 1.f, 1.5f, 2.75f}) {
                    // Physical-pixel fractions straddle every important sample
                    // height, including exactly the last 15/16 sample row.
                    const float width = 236.375f * scale, height = 34 + edge + delta;
                    const float radius = std::min(17.3125f * scale, height / 2);
                    const auto source = Content(width, height, 3, 2, ++cases);
                    auto expected = source, actual = source;
                    Original(expected, width, height, radius, mix, scale);
                    mask.Apply(actual, width, height, radius, mix, scale);
                    if (expected.pixels != actual.pixels) {
                        ++failures;
                        std::cerr << "FAIL: fractional edge height=" << height << " scale=" << scale
                            << " mix=" << mix << '\n';
                        return;
                    }
                }
    std::cout << "Fractional sample-edge equality: " << cases << " geometries\n";
}
void SmallFractionalEdges() {
    IslandShapeMask mask;
    unsigned cases = 0;
    for (const float edge : {.0625f, .5f, .9375f})
        for (const float delta : {-.00001f, 0.f, .00001f})
            for (const float mix : {.000001f, .5f, 1.f})
                for (const float height : {edge + delta, 1 + edge + delta}) {
                    constexpr float width = 120.375f;
                    const float radius = height / 2;
                    const auto source = Content(width, height, 2, 2, ++cases);
                    auto expected = source, actual = source;
                    Original(expected, width, height, radius, mix, 1);
                    mask.Apply(actual, width, height, radius, mix, 1);
                    Check(expected.pixels == actual.pixels, "tiny-height sample boundaries preserve exact coverage");
                }
    std::cout << "Tiny fractional-edge equality: " << cases << " geometries\n";
}
bool CompareGeometry(IslandShapeMask& mask, float width, float height, float radius,
                     float mix, float scale, unsigned seed, const char* group) {
    const auto source = Content(width, height, 3, 2, seed);
    auto expected = source, actual = source;
    Original(expected, width, height, radius, mix, scale);
    if (!mask.Apply(actual, width, height, radius, mix, scale) || actual.pixels != expected.pixels) {
        ++failures;
        std::cerr << "FAIL: " << group << " seed=" << seed << " width=" << width << " height=" << height
            << " radius=" << radius << " mix=" << mix << " scale=" << scale << '\n';
        return false;
    }
    return true;
}
void SeparatedEdgeBoundaries() {
    IslandShapeMask mask;
    unsigned cases = 0;
    for (const float scale : {.5f, 1.f, 1.25f, 2.75f, 4.f})
        for (const float height : {4.0625f, 34.625f, 176.5f}) {
            const float radius = Radius(height / scale) * scale;
            const float reach = (lume::dynamic_island::IslandShoulder(height / scale) +
                lume::dynamic_island::IslandLowerRadius(height / scale)) * scale;
            const float boundary = 2 * std::ceil(std::max(radius, reach) + 2) + 4;
            // Cross the conservative fast-path guard in both directions, as
            // well as the final right pixel whose centre can lie past width.
            for (const float width : {std::nextafter(boundary, 0.f), boundary,
                std::nextafter(boundary, std::numeric_limits<float>::infinity()),
                boundary + .0625f, boundary + 19.9375f,
                std::nextafter(boundary + 20.5f, 0.f), boundary + 20.5f,
                std::nextafter(boundary + 20.5f, std::numeric_limits<float>::infinity())})
                for (const float mix : {.125f, .5f, 1.f})
                    if (!CompareGeometry(mask, width, height, radius, mix, scale, ++cases, "edge guard")) return;
        }
    std::cout << "Separated-edge guard equality: " << cases << " geometries\n";
}
void OverlappingCornerFallback() {
    IslandShapeMask mask;
    auto actual = Content(20, 40, 2, 2);
    std::fill(actual.pixels.begin(), actual.pixels.end(), 0xffffffffu);
    auto expected = actual;
    Original(expected, 20, 40, 10, 1, 1);
    // In a narrow outline the right half is not monotone: this row goes
    // outside, inside, then toward the outside again. An all-zero result
    // cannot accidentally pass the fallback coverage check.
    Check(expected.pixels[35 * expected.width + 10] == 0 &&
        expected.pixels[35 * expected.width + 15] == 0xffffffffu &&
        expected.pixels[35 * expected.width + 19] == 0xffffffffu,
        "narrow-corner oracle actually contains both outside and inside pixels");
    Check(mask.Apply(actual, 20, 40, 10, 1, 1) && actual.pixels == expected.pixels,
        "overlapping corners retain the original nonmonotone scan");
    unsigned seed = 7000;
    for (const float scale : {.125f, .49999f, 4.00001f, 16.f})
        for (const float radius : {0.f, 6.f, 1024.f})
            if (!CompareGeometry(mask, 71.0625f, 12.9375f, radius, .75f, scale, ++seed, "fallback range")) return;
    actual = Content(96, 64, 0, 0, ++seed); expected = actual;
    Original(expected, 120.375f, 34.625f, 17.3125f, .5f, 1.25f);
    Check(mask.Apply(actual, 120.375f, 34.625f, 17.3125f, .5f, 1.25f) && actual.pixels == expected.pixels,
        "clipped bitmap falls back without changing stride or off-shape pixels");
}
void RandomGeometryEquivalence() {
    IslandShapeMask mask;
    unsigned state = 0x853c49e6u;
    const auto next = [&] { state = state * 1664525u + 1013904223u; return state; };
    constexpr float scales[]{.25f, .5f, .75f, 1.f, 1.25f, 2.75f, 4.f, 4.25f};
    constexpr float mixes[]{.000001f, .125f, .49999997f, .5f, .875f, 1.f, 1.000001f};
    constexpr unsigned cases = 320;
    for (unsigned i = 0; i < cases; ++i) {
        // Independent asymmetric content, non-DIP fractions and small widths
        // exercise interpolation and fallback as well as typical wide islands.
        const float width = .0625f + static_cast<float>(next() % 900000u) / 1000;
        const float height = .03125f + static_cast<float>(next() % 180000u) / 1000;
        const float scale = scales[(next() >> 16) % std::size(scales)];
        const float mix = mixes[(next() >> 16) % std::size(mixes)];
        const float half = std::min(width, height) / 2;
        const float fraction = static_cast<float>(next() % 1001u) / 1000;
        const float radius = i % 4 == 0 ? 0.f : i % 4 == 1 ? half : half * fraction;
        const auto seed = next();
        if (!CompareGeometry(mask, width, height, radius, mix, scale, seed, "random oracle")) return;
    }
    std::cout << "Deterministic random pixel equality: " << cases << " geometries\n";
}
volatile std::uint32_t benchmarkSink = 0;
template<class Action> double Time(unsigned count, const WidgetBitmap& source, Action action) {
    auto frame = source;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < count; ++i) {
        std::copy(source.pixels.begin(), source.pixels.end(), frame.pixels.begin());
        action(frame, i);
        benchmarkSink ^= frame.pixels[(i * 103u) % frame.pixels.size()];
    }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / count;
}
void Benchmark() {
    const float width = 1640.5f, height = 349.5f, radius = 60, mix = 1, scale = 2;
    const auto source = Content(width, height);
    const double original = Time(16, source, [&](WidgetBitmap& frame, unsigned) {
        Original(frame, width, height, radius, mix, scale);
    });
    IslandShapeMask cold, warm;
    const double rebuild = Time(16, source, [&](WidgetBitmap& frame, unsigned i) {
        cold.Apply(frame, width + (i & 1 ? .001f : 0.f), height, radius, mix, scale);
    });
    auto first = source; warm.Apply(first, width, height, radius, mix, scale);
    const double cached = Time(160, source, [&](WidgetBitmap& frame, unsigned) {
        warm.Apply(frame, width, height, radius, mix, scale);
    });
    std::cout << std::fixed << std::setprecision(4) << "Mask benchmark (ms/frame, includes same BGRA copy): original="
        << original << " geometry-change=" << rebuild << " cached-hover=" << cached
        << " entries=" << warm.EntryCount() << '/' << source.pixels.size() << '\n';
}
}
int main() {
    Equivalence(); FractionalEdges(); SmallFractionalEdges(); SeparatedEdgeBoundaries();
    OverlappingCornerFallback(); RandomGeometryEquivalence(); CacheLifecycle(); Benchmark();
    std::cout << "island shape mask failures=" << failures << '\n';
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
