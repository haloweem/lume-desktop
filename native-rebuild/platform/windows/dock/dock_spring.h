#pragma once
#include <windows.h>
#include <cmath>

namespace lume::platform::windows {
// Under-damped spring (closed form, as the organizer motion uses). Changing the
// target keeps the velocity, so drags and hovers can retarget on any frame.
struct DockSpring {
    float value = 0, velocity = 0;
    // Returns true while still moving.
    // zeta must stay below 1 (the closed form is the under-damped solution).
    bool Step(float target, float dt, float omega, float zeta, float epsilon, bool reducedMotion) {
        if (reducedMotion || dt <= 0) {
            const bool moved = value != target;
            if (reducedMotion) { value = target; velocity = 0; }
            return moved && !reducedMotion;
        }
        const float decay = zeta * omega, frequency = omega * std::sqrt(1 - zeta * zeta);
        const float displacement = value - target;
        const float attenuation = std::exp(-decay * dt), cosine = std::cos(frequency * dt), sine = std::sin(frequency * dt);
        const float v = velocity;
        value = target + attenuation * (displacement * cosine + (v + decay * displacement) * sine / frequency);
        velocity = attenuation * (v * cosine - (decay * v + omega * omega * displacement) * sine / frequency);
        if (std::abs(value - target) <= epsilon && std::abs(velocity) <= epsilon * omega) {
            value = target;
            velocity = 0;
            return false;
        }
        return true;
    }
};
// Animation time in seconds from the high-resolution counter. GetTickCount64
// advances in ~15.6 ms steps, which made fast displays alternate between
// frozen and jumping frames.
inline double AnimationSeconds() {
    static const double period = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return 1.0 / static_cast<double>(f.QuadPart); }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) * period;
}
// Icon magnification: bouncy, like Nexus. Positions: firmer, like the organizer.
inline constexpr float MagnifyOmega = 24, MagnifyZeta = .68f, SlideOmega = 26, SlideZeta = .78f;
}  // namespace lume::platform::windows
