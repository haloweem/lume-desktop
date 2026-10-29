#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lume::shell::resources {

enum class ResourceRole {
    Shell,
    WebViewBrowser,
    WebViewRenderer,
    WebViewGpu,
    WebViewUtility,
    WebViewCrashpad,
    MusicHost,
    Wallpaper,
    Other,
};

enum class Coverage {
    Unavailable,
    Partial,
    Complete,
};

struct ProcessSample {
    DWORD pid = 0;
    DWORD parentPid = 0;
    std::wstring name;
    std::wstring commandLine;
    ResourceRole role = ResourceRole::Other;
    std::optional<std::uint64_t> workingSetBytes;
    std::optional<std::uint64_t> privateBytes;
    std::optional<std::uint64_t> gpuDedicatedBytes;
    std::optional<std::uint64_t> gpuSharedBytes;
};

struct ResourceGroup {
    std::optional<std::uint64_t> workingSetBytes;
    std::optional<std::uint64_t> privateBytes;
    std::optional<std::uint64_t> gpuDedicatedBytes;
    std::optional<std::uint64_t> gpuSharedBytes;
    std::size_t processCount = 0;
    Coverage coverage = Coverage::Unavailable;
    std::vector<ProcessSample> processes;
};

struct ResourceSnapshot {
    std::uint32_t schemaVersion = 1;
    FILETIME sampledAt{};
    ResourceGroup shell;
    ResourceGroup webview;
    ResourceGroup music;
    ResourceGroup gpu;
    ResourceGroup wallpaper;
    ResourceGroup nonWallpaper;
    std::vector<ProcessSample> processes;
};

ResourceRole ClassifyProcess(std::wstring_view name, std::wstring_view commandLine);
ResourceSnapshot Aggregate(const std::vector<ProcessSample>& samples);
ResourceSnapshot SampleProcessTree(DWORD rootPid = GetCurrentProcessId());
std::wstring EncodeResourceSnapshot(const ResourceSnapshot& snapshot);
const wchar_t* ResourceRoleName(ResourceRole role) noexcept;
const wchar_t* CoverageName(Coverage coverage) noexcept;

}  // namespace lume::shell::resources
