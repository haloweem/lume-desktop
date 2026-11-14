#pragma once
#include <array>
#include <string_view>

namespace lume::platform::windows {
struct OffWorkVoiceCue {
    unsigned startMs;
    std::wstring_view chinese, english;
};

// Aligned to the unchanged user recording, SHA-256 F388DE714B023581CE957B60AB5BF13E160E8F6DFBCDD834BA31230E4D884AB1.
// Numbers use measured PCM onsets, not one-second wall-clock intervals.
// The later phrase boundaries use local ASR; the user confirmed the transcript
// and corrected the final phrase to “油污”. Retiming is required if the WAV changes.
inline constexpr std::array<OffWorkVoiceCue, 13> OffWorkVoiceCues{{
    {0, L"5", L"5"},
    {480, L"4", L"4"},
    {1367, L"3", L"3"},
    {2220, L"2", L"2"},
    {2824, L"1", L"1"},
    {3720, L"我下班了，蟹老板", L"I'm off work, Mr. Krabs."},
    {5966, L"我要说的是", L"What I mean is,"},
    {7010, L"如果有一天", L"if one day"},
    {7880, L"我真的实现了", L"I really achieve"},
    {9250, L"我生命中的梦想", L"the dream of my life,"},
    {11000, L"我永远也不会让", L"I will never let"},
    {12530, L"我的双脚", L"my own two feet"},
    {13440, L"站在这油污的地板上", L"stand on this greasy floor."}
}};

inline std::wstring_view OffWorkVoiceCaption(unsigned positionMs, bool english = false) noexcept {
    const OffWorkVoiceCue* cue = &OffWorkVoiceCues.front();
    for (const auto& next : OffWorkVoiceCues) {
        if (next.startMs > positionMs) break;
        cue = &next;
    }
    return english ? cue->english : cue->chinese;
}
}
