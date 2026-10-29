#pragma once

#include "liquid-glass.h"

namespace lume_materials {

// Frosted controls and panels share one optical material and desktop scene.
constexpr char kFrostedGlassPixelShader[] = R"(
Texture2D desktopTexture : register(t0);
SamplerState linearClamp : register(s0);
cbuffer GlassConstants : register(b0) {
  float2 surfaceOrigin;
  float2 surfaceSize;
  float2 outputOrigin;
  float2 outputSize;
  float2 lensOrigin;
  float2 lensSize;
  float4 effect;
  float2 sampleBoundsMin;
  float2 sampleBoundsMax;
  float4 interaction;
  float4 motion;
  float4 readability;
  float4 optical;
};

float2 clampSampleUv(float2 uv) {
  float2 minimum = (sampleBoundsMin - outputOrigin) / outputSize;
  float2 maximum = (sampleBoundsMax - outputOrigin) / outputSize;
  return clamp(uv, minimum, maximum);
}

float2 sampleUv(float2 samplePoint) {
  return clampSampleUv((samplePoint - outputOrigin) / outputSize);
}

float3 knownColor(float2 samplePoint, float3 fallback) {
  float4 tap = desktopTexture.SampleLevel(linearClamp, sampleUv(samplePoint), 0);
  return lerp(fallback, tap.rgb / max(tap.a, 0.0001), saturate(tap.a));
}

float roundedDistance(float2 p, float2 halfSize, float radius) {
  float2 q = abs(p) - halfSize + radius;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}
float roundedPixelCoverage(float2 p, float2 halfSize, float radius, float distance) {
  // Actual coverage of one physical pixel, matching the shared liquid pane.
  // Work is limited to the boundary; no supersized offscreen surface.
  [branch] if (abs(distance) >= 0.708) return distance < 0.0 ? 1.0 : 0.0;
  float coverage = 0.0;
  [unroll] for (int y = 0; y < 8; ++y) {
    [unroll] for (int x = 0; x < 8; ++x) {
      float2 offset = (float2(x, y) + 0.5) / 8.0 - 0.5;
      coverage += roundedDistance(p + offset, halfSize, radius) <= 0.0 ? 1.0 : 0.0;
    }
  }
  return coverage / 64.0;
}

float annularPixelCoverage(float2 p, float innerRadius, float outerRadius, float distance) {
  [branch] if (abs(distance) >= 0.708) return distance < 0.0 ? 1.0 : 0.0;
  float coverage = 0.0;
  [unroll] for (int y = 0; y < 8; ++y) {
    [unroll] for (int x = 0; x < 8; ++x) {
      float2 offset = (float2(x, y) + 0.5) / 8.0 - 0.5;
      float radial = length(p + offset);
      coverage += radial >= innerRadius && radial <= outerRadius ? 1.0 : 0.0;
    }
  }
  return coverage / 64.0;
}
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
  float2 screenPoint = surfaceOrigin + uv * surfaceSize;
  float2 p = screenPoint - lensOrigin - lensSize * 0.5;
  float dpi = max(effect.w, 0.5);
  bool pane = effect.x < 0.0;
  // Panel dimensions must not stretch the control's contiguous blur kernel.
  float scale = effect.z < 0.0 ? dpi
      : dpi * clamp(min(lensSize.x, lensSize.y) / (36.0 * dpi), 0.75, 1.5);
  float radius = min(lensSize.x, lensSize.y) * 0.5;
  float radial = length(p);
  float inside = radius - radial;
  float shadowDistance = length(p - float2(0.5, 1.4) * scale) - radius;
  float2 direction = p / max(radial, 0.001);
  // Negative effect.z carries a panel corner radius; circles retain their exact path.
  if (effect.z < 0.0) {
    float2 halfSize = lensSize * 0.5;
    float cornerRadius = min(-effect.z, radius);
    inside = -roundedDistance(p, halfSize, cornerRadius);
    shadowDistance = roundedDistance(p - float2(0.5, 1.4) * scale, halfSize, cornerRadius);
    float2 q = abs(p) - halfSize + cornerRadius;
    float2 corner = max(q, 0.0);
    float cornerLength = length(corner);
    direction = cornerLength > 0.001 ? sign(p) * corner / cornerLength
        : q.x > q.y ? float2(sign(p.x), 0.0) : float2(0.0, sign(p.y));
  }
  float mask = roundedPixelCoverage(p, effect.z < 0.0 ? lensSize * 0.5 : float2(radius, radius),
      effect.z < 0.0 ? min(-effect.z, radius) : radius, -inside);
  if (optical.x > 0.0) {
    float outerRadius = radius - 8.0 * dpi;
    inside = min(outerRadius - radial, radial - optical.x);
    if (radial < optical.x - 0.708) return float4(0, 0, 0, 0);
    mask = annularPixelCoverage(p, optical.x, outerRadius, -inside);
    direction = p / max(radial, 0.001) * (radial < (outerRadius + optical.x) * 0.5 ? -1.0 : 1.0);
  }
  float shadow = exp(-max(shadowDistance, 0.0) / (1.9 * scale)) * 0.17
      * smoothstep(-0.65, 0.8, p.y / radius) * (1.0 - mask);
  if (optical.x > 0.0) shadow = 0.0;
  if (mask <= 0.0) return float4(0, 0, 0, shadow);
  float edge = 1.0 - smoothstep(0.0, 6.5 * scale, inside);
  float2 samplePoint = pane ? screenPoint : screenPoint + direction * edge * 2.8 * scale;
  // Contiguous bilinear Gaussian taps prevent fine desktop detail from
  // aliasing into separate bands while the small lens moves.
  float4 sum = 0.0;
  // The Dock and launchpad (profile 3) blur much more broadly, like a macOS
  // dock: only soft colour fields of the wallpaper remain.
  bool dockPane = pane && optical.y >= 2.5;
  float spread = dockPane ? 3.0 : 1.0;
  static const float weights[9] = {0.0474670, 0.0894924, 0.1388139, 0.1771513, 0.0941508,
      0.1771513, 0.1388139, 0.0894924, 0.0474670};
  static const float offsets[9] = {-7.408452, -5.432513, -3.456897, -1.481490, 0.0,
      1.481490, 3.456897, 5.432513, 7.408452};
  // At spread 1 the taps lie about 2 px apart. Spread wider, read a mip level
  // that far down, so each tap already averages its gap instead of picking
  // single pixels of fine detail (which showed as grain).
  float level = dockPane ? log2(max(spread * scale * 1.98, 1.0)) : 0.0;
  [unroll] for (int y = 0; y < 9; ++y) {
    [unroll] for (int x = 0; x < 9; ++x) {
      float2 tap = samplePoint + float2(offsets[x], offsets[y]) * scale * spread;
      sum += desktopTexture.SampleLevel(linearClamp, sampleUv(tap), level) * weights[x] * weights[y];
    }
  }
  if (sum.a < 0.0001) return float4(0, 0, 0, 0);
  float3 soft = sum.rgb / sum.a;
  if (pane) {
    // Two frost families:
    //  - profiles 0-2 (desktop widgets, calendar editors, their inner panels and
    //    the launchpad) scale the blurred backdrop, so the pane keeps the
    //    wallpaper's hue instead of turning neutral grey.
    //  - profile 3 is the Dock: wider kernel, slight lift, brighter rim.
    // Profile 4 is reserved (see glass-scene.h SetBackdropScale).
    float paneBorder = 1.0 - smoothstep(0.0, 1.25 * dpi, inside);
    float3 color;
    [branch] if (optical.y >= 2.5) {
      color = soft * 0.486 + 0.045;
      float sheen = (1.0 - smoothstep(0.0, 14.0 * dpi, p.y + lensSize.y * 0.5)) * 0.05;
      color += sheen;
      color = lerp(color, float3(1.0, 1.0, 1.0), paneBorder * 0.28);
      float dockAlpha = mask * saturate(sum.a);
      return float4(saturate(color) * dockAlpha, dockAlpha + shadow * (1.0 - dockAlpha));
    } else {
      // Scale the backdrop, never add to it: an additive lift compresses the
      // channel differences and turns a blue wallpaper into neutral grey.
      color = soft * 0.58;
    }
    color = lerp(color, float3(1.0, 1.0, 1.0), paneBorder * 0.10);
    float alpha = mask * saturate(sum.a);
    return float4(saturate(color) * alpha, alpha + shadow * (1.0 - alpha));
  }
  // Profile 3 (Dock, launchpad): no colour fringe or lower shade band at the rim.
  float softEdge = step(2.5, optical.y);
  float2 dispersion = direction * (0.65 * scale * edge) * (1.0 - 0.85 * softEdge);
  float3 refracted = float3(knownColor(samplePoint + dispersion, soft).r,
      knownColor(samplePoint, soft).g, knownColor(samplePoint - dispersion, soft).b);
  float3 color = lerp(soft, refracted, edge * 0.32 * (1.0 - 0.6 * softEdge));
  // Overlay panels (widget inner cards, calendar editors, the launchpad's
  // search field) sit one step lighter than their pane. A small white mix keeps
  // the hue; a flat grey base would desaturate them the same way.
  color = lerp(color, float3(1.0, 1.0, 1.0), 0.08);
  float key = pow(saturate(dot(direction, normalize(float2(-0.55, -0.83)))), 2.0);
  float fill = pow(saturate(dot(direction, normalize(float2(0.65, 0.76)))), 5.0);
  float inset = max(inside, 0.0) / scale;
  float rim = exp(-pow((inset - 0.75) / 0.55, 2.0));
  float halo = exp(-pow(inset / 2.8, 2.0));
  float lower = saturate(dot(direction, normalize(float2(0.45, 0.9))));
  color *= 1.0 - lower * exp(-pow((inset - 2.4) / 1.5, 2.0)) * 0.15 * (1.0 - 0.7 * softEdge);
  float environmentLight = dot(soft, float3(0.299, 0.587, 0.114));
  color += (rim * (0.025 + key * 0.52 + fill * 0.25) + halo * key * 0.08)
      * (0.75 + environmentLight * 0.5);
  float alpha = mask * saturate(sum.a);
  return float4(saturate(color) * alpha, alpha + shadow * (1.0 - alpha));
}
)";

inline constexpr MaterialDefinition kFrostedGlassMaterial = {
  "frosted-glass",
  kLiquidGlassVertexShader,
  kFrostedGlassPixelShader,
};

}  // namespace lume_materials
