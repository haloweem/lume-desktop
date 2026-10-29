#pragma once
#include <algorithm>
#include <cmath>

namespace lume_glass {
// Analytic critically damped response: re-targeting preserves position and
// velocity, and elapsed time is independent of display refresh rate.
struct InteractionSpring {
  float value = 0, velocity = 0;
  bool Step(float target, float seconds, float frequency = 24.f) {
    const float before = value;
    const float offset = value - target;
    const float c = velocity + frequency * offset;
    const float decay = std::exp(-frequency * (std::max)(seconds, 0.f));
    value = target + (offset + c * seconds) * decay;
    velocity = (velocity - frequency * c * seconds) * decay;
    if (std::abs(value - target) < .0001f && std::abs(velocity) < .002f) {
      value = target; velocity = 0;
    }
    // Zero elapsed time is possible at high refresh rates. A changing target
    // or retained velocity still needs a future frame, even without new pixels.
    return value != before || value != target || velocity != 0;
  }
};
struct InteractionInput {
  bool pressed = false, reducedMotion = false;
  float x = 0, y = 0;
};
struct GlassInteraction {
  InteractionSpring pressure, x, y, motionX, motionY;
  // The rim highlight trails the pointer on its own soft spring, separate
  // from the responsive contact light. It starts at the top-left rest key.
  InteractionSpring lightX{-.62f, 0}, lightY{-.85f, 0};
  static constexpr float HighlightFrequency = 5.5f;
  float light = 0;
  bool Step(const InteractionInput& input, float seconds, float dx, float dy, float dpi) {
    bool changed = false;
    const float target = input.pressed ? 1.f : 0.f;
    const float previousLight = light;
    if (input.reducedMotion) {
      changed = pressure.value != 0 || motionX.value != 0 || motionY.value != 0 ||
          x.value != input.x || y.value != input.y;
      changed |= lightX.value != input.x || lightY.value != input.y;
      pressure = {}; motionX = {}; motionY = {};
      x = {input.x, 0}; y = {input.y, 0}; light = target;
      lightX = {input.x, 0}; lightY = {input.y, 0};
    } else {
      // A new press starts at the pointer rather than sweeping the contact
      // light across the pane. Hover keeps the springs, so the key highlight
      // glides after the pointer and back to its rest position on leave.
      if (input.pressed && pressure.value == 0 && pressure.velocity == 0) {
        x = {input.x, 0}; y = {input.y, 0};
      }
      changed |= pressure.Step(target, seconds);
      changed |= x.Step(input.x, seconds);
      changed |= y.Step(input.y, seconds);
      changed |= lightX.Step(input.x, seconds, HighlightFrequency);
      changed |= lightY.Step(input.y, seconds, HighlightFrequency);
      const float divisor = (std::max)(seconds, .001f) * (std::max)(dpi, .5f) * 1000.f;
      changed |= motionX.Step(input.pressed ? std::tanh(dx / divisor) : 0.f, seconds);
      changed |= motionY.Step(input.pressed ? std::tanh(dy / divisor) : 0.f, seconds);
      light = pressure.value;
    }
    return changed || previousLight != light;
  }
};
}  // namespace lume_glass
