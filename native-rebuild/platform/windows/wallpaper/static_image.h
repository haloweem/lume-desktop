#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lume::platform::windows::wallpaper {

struct StaticImageSource {
    std::uint64_t bytes = 0;
    std::function<bool(std::uint64_t, char*, std::size_t, std::size_t&)> read;
};

enum class StaticImageLoad { Loaded, Unsupported, Failed };

struct StaticImage {
    // The fitted bitmap may be smaller than the canvas when using contain.
    unsigned width = 0, height = 0;
    int canvasWidth = 0, canvasHeight = 0;
    // Top-down opaque BGRA pixels, with source alpha composited over black.
    std::vector<std::uint32_t> pixels;
};

// With a callback source, localPath is a format hint only; it is never opened.
// Unrecognizable data with a built-in raster extension fails as corrupt. An
// anonymous source without recognizable bytes has no format identity, so WIC's
// missing-codec result is Unsupported; WebP/AVIF hints retain that fallback.
StaticImageLoad LoadStaticImage(const std::wstring& localPath,
    const StaticImageSource* source, int width, int height, bool cover,
    const std::atomic<bool>* cancellation, StaticImage& output, std::string& error);
void PaintStaticImage(HDC dc, const RECT& bounds, const StaticImage& image);
// Writes the fitted bitmap as a JPEG (the still shown at sign-in before Lume starts).
bool SaveStaticImageJpeg(const StaticImage& image, const std::wstring& path);

}  // namespace lume::platform::windows::wallpaper
