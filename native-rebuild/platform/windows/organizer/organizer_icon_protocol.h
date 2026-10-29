#pragma once
#include <cstdint>
#include <cstddef>

namespace lume::platform::windows::organizer_icon_ipc {
constexpr std::uint32_t Magic=0x4c554943;
constexpr std::uint32_t Version=2;
constexpr std::size_t MaxPath=32768,MaxSide=256,MaxPixels=MaxSide*MaxSide;
enum class Result:std::uint32_t { Pending,Missing,Ready };
struct Message {
    std::uint32_t magic=Magic,version=Version;
    std::uint32_t pathLength=0,width=0,height=0,requestedSide=64;
    Result result=Result::Pending;
    wchar_t path[MaxPath]{};
    std::uint32_t pixels[MaxPixels]{};
};
}
