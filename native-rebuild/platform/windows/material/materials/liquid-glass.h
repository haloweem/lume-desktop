#pragma once

#include "material-definition.h"

namespace lume_materials {

// The production liquid-glass material. Keep scene capture and composition
// outside this file so the material can be reused by any native widget.
constexpr char kLiquidGlassVertexShader[] = R"(
struct VertexOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VertexOutput main(uint id : SV_VertexID) {
  VertexOutput output;
  float2 p = float2((id << 1) & 2, id & 2);
  output.uv = p;
  output.position = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  return output;
}
)";

constexpr char kLiquidGlassPixelShader[] = R"(Texture2D desktopTexture : register(t0);
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
  float4 interaction; // elastic pressure, contact XY, light response
  float4 motion;      // bounded drag velocity XY; highlight position ZW
  float4 readability; // enabled, light-foreground flag, reserved ZW
  float4 optical;      // annulus inner radius, optical profile, thickness profile, corner radius
};
float2 clampSampleUv(float2 uv) {
  float2 minimum = (sampleBoundsMin - outputOrigin) / outputSize;
  float2 maximum = (sampleBoundsMax - outputOrigin) / outputSize;
  return clamp(uv, minimum, maximum);
}
float2 sampleUv(float2 samplePoint) {
  return clampSampleUv((samplePoint - outputOrigin) / outputSize);
}
float rrSdf(float2 p, float2 halfSize, float radius) {
  float2 q = abs(p) - halfSize + radius;
  return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}
float roundedPixelCoverage(float2 p, float2 halfSize, float radius, float distance) {
  // Integrate the actual physical pixel footprint. Only the one-pixel rim
  // needs subpixel work; interiors/shadows keep the original single sample.
  // This avoids both a binary outside branch and repeated soft edge blending.
  [branch] if (abs(distance) >= 0.708) return distance < 0.0 ? 1.0 : 0.0;
  float coverage = 0.0;
  [unroll] for (int y = 0; y < 8; ++y) {
    [unroll] for (int x = 0; x < 8; ++x) {
      float2 offset = (float2(x, y) + 0.5) / 8.0 - 0.5;
      coverage += rrSdf(p + offset, halfSize, radius) <= 0.0 ? 1.0 : 0.0;
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
float3 sampleKnown(float2 uv, float3 fallback) {
  float4 tap = desktopTexture.SampleLevel(linearClamp, clampSampleUv(uv), 0);
  return lerp(fallback, tap.rgb / max(tap.a, 0.0001), saturate(tap.a));
}
float3 sampleSharp(float2 uv, float2 texel, float2 direction, float chroma, float3 fallback) {
  float2 offset = direction * texel * chroma;
  return float3(
    sampleKnown(uv + offset, fallback).r,
    sampleKnown(uv, fallback).g,
    sampleKnown(uv - offset, fallback).b
  );
}
float3 sampleSoft(float2 uv, float2 texel, float3 fallback) {
  float2 step = texel * 5.0;
  float3 color = sampleKnown(uv, fallback) * 0.34;
  color += sampleKnown(uv + float2(step.x, 0.0), fallback) * 0.165;
  color += sampleKnown(uv - float2(step.x, 0.0), fallback) * 0.165;
  color += sampleKnown(uv + float2(0.0, step.y), fallback) * 0.165;
  color += sampleKnown(uv - float2(0.0, step.y), fallback) * 0.165;
  return color;
}
float3 sampleEnvironment(float2 uv, float2 texel, float3 fallback) {
  static const float weights[9] = {0.0474670, 0.0894924, 0.1388139, 0.1771513, 0.0941508,
      0.1771513, 0.1388139, 0.0894924, 0.0474670};
  static const float offsets[9] = {-7.408452, -5.432513, -3.456897, -1.481490, 0.0,
      1.481490, 3.456897, 5.432513, 7.408452};
  float3 color = 0.0;
  [unroll] for (int y = 0; y < 9; ++y) {
    [unroll] for (int x = 0; x < 9; ++x) {
      color += sampleKnown(uv + float2(offsets[x], offsets[y]) * texel, fallback)
          * weights[x] * weights[y];
    }
  }
  return color;
}
// Adapted from Kyant0/AndroidLiquidGlass internal/Shaders.kt (Apache-2.0).
// Copyright 2025 Kyant. HLSL adaptation: uniform widget radii, bounded strength,
// existing press/drag springs. See widgets/liquid-glass.NOTICE for the license.
float2 widgetLensNormal(float2 p, float2 halfSize, float radius) {
  float gradRadius = min(radius * 1.5, min(halfSize.x, halfSize.y));
  float2 corner = abs(p) - halfSize + gradRadius;
  float2 outside = max(corner, 0.0);
  if (length(outside) > 0.0001) return sign(p) * normalize(outside);
  return sign(p) * (corner.x >= corner.y ? float2(1, 0) : float2(0, 1));
}
// normal points out of the glass through the nearest rim; height is the band.
float2 widgetLensOffset(float2 normal, float inside, float height, float strength) {
  if (inside >= height || height <= 0.001) return 0.0;
  // Strongest at the rim and fading to zero inward. The amplitude stays below
  // height / 3, so the sampled position keeps moving outward with the pixel:
  // the edge reads as one compressed lens, never as a folded, magnified ring.
  float r = 1.0 - saturate(inside / height);
  return -normal * r * r * r * height * 0.22 * strength;
}
// Rectangular widgets keep the 1.0.2.1 lens: a smooth band that ends before the
// SDF's interior medial axis (past the corner radius it creases large panes).
float2 widgetRectLensOffset(float2 normal, float inside, float height, float strength) {
  if (inside >= height || height <= 0.001) return 0.0;
  float t = saturate(1.0 - inside / height);
  float edgeProfile = t * t * (3.0 - 2.0 * t);
  return -normal * edgeProfile * height * 0.33 * strength;
}
float2 widgetInteriorOffset(float2 center, float scale, float strength, float pressure) {
  // Smooth polynomial lens field, an artistic fit rather than fluid simulation.
  // Never normalize sign(center) or the SDF's medial-axis normal: both jump
  // across axes. Opposite-side weights compress source coordinates along each
  // edge, elongating the displayed image tangentially around the whole pane.
  float2 squared = center * center;
  float2 alongEdge = -center * (1.0 - squared * 0.25) * squared.yx * squared.yx;
  float2 dome = -center * (1.0 - squared * 0.45);
  float2 bend = center.yx * (1.0 - squared) * float2(0.7, -0.5);
  return (dome * 4.0 + alongEdge * 6.0 + bend)
      * scale * strength * (1.0 + pressure * 0.06);
}
float3 sampleWidgetBody(float2 uv, float2 texel, float3 fallback) {
  // A tight footprint preserves icon/text identity while allowing the single
  // interior coordinate field to carry the curved-glass distortion.
  float3 color = sampleKnown(uv, fallback) * 0.64;
  color += sampleKnown(uv + float2(texel.x * 1.5, 0.0), fallback) * 0.09;
  color += sampleKnown(uv - float2(texel.x * 1.5, 0.0), fallback) * 0.09;
  color += sampleKnown(uv + float2(0.0, texel.y * 1.5), fallback) * 0.09;
  color += sampleKnown(uv - float2(0.0, texel.y * 1.5), fallback) * 0.09;
  return color;
}
float3 sampleReadable(float2 uv, float2 texel, float3 fallback) {
  static const float weights[5] = {0.093913, 0.304006, 0.204162, 0.304006, 0.093913};
  static const float offsets[5] = {-3.294215, -1.407333, 0.0, 1.407333, 3.294215};
  float3 color = 0.0;
  [unroll] for (int y = 0; y < 5; ++y) {
    [unroll] for (int x = 0; x < 5; ++x) {
      color += sampleKnown(uv + float2(offsets[x], offsets[y]) * texel, fallback)
          * weights[x] * weights[y];
    }
  }
  return color;
}
float3 sampleLiquid(float2 uv, float2 texel, float2 direction, float amount, float3 fallback) {
  float2 side = float2(-direction.y, direction.x);
  float2 stream = direction * amount * texel;
  float2 curl = side * amount * 0.24 * texel;
  float3 color = sampleKnown(uv, fallback) * 0.32;
  color += sampleKnown(uv + stream * 0.92 + curl, fallback) * 0.24;
  color += sampleKnown(uv - stream * 0.42 - curl * 0.45, fallback) * 0.17;
  color += sampleKnown(uv + stream * 0.16 + side * amount * 0.62 * texel, fallback) * 0.135;
  color += sampleKnown(uv - stream * 0.12 - side * amount * 0.56 * texel, fallback) * 0.135;
  return color;
}
)" R"(
float3 toLinear(float3 color) {
  return lerp(color / 12.92, pow(max((color + 0.055) / 1.055, 0.0), 2.4), step(0.04045, color));
}
float3 toDisplay(float3 color) {
  return lerp(color * 12.92, 1.055 * pow(max(color, 0.0), 1.0 / 2.4) - 0.055, step(0.0031308, color));
}
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
  float2 screenPoint = surfaceOrigin + uv * surfaceSize;
  float2 lensPoint = screenPoint - lensOrigin;
  float2 halfSize = lensSize * 0.5;
  float dpiScale = max(effect.w, 0.5);
  float materialZoom = max(1.0, min(lensSize.x / (340.0 * dpiScale), lensSize.y / (360.0 * dpiScale)));
  float scale = dpiScale * materialZoom;
  float pressure = saturate(interaction.x);
  float2 drift = clamp(motion.xy, -1.0, 1.0);
  float2 contraction = (pressure * 1.25
      + float2(abs(drift.y) * 0.40 - abs(drift.x) * 0.80,
               abs(drift.x) * 0.40 - abs(drift.y) * 0.80)) * scale;
  if (optical.x <= 0.0) {
    lensPoint -= contraction + drift * 0.55 * scale;
    halfSize -= contraction;
  }
  float radius = min(optical.w > 0.0 ? optical.w : 22.0 * scale, min(halfSize.x, halfSize.y));
  float distance = rrSdf(lensPoint - halfSize, halfSize, radius);
  float shadowDistance = rrSdf(lensPoint - halfSize - float2(0.0, 2.0 * dpiScale), halfSize, radius);
  float lower = smoothstep(0.10, 0.90, (lensPoint.y - halfSize.y) / max(halfSize.y, 1.0));
  float contact = exp(-max(shadowDistance, 0.0) / (2.0 * dpiScale)) * 0.14;
  float ambient = exp(-pow(max(shadowDistance, 0.0) / (6.0 * dpiScale), 2.0)) * 0.05;
  float shadow = (contact + ambient) * lower;
  float mask = roundedPixelCoverage(lensPoint - halfSize, halfSize, radius, distance);
  if (optical.x > 0.0) {
    float radial = length(lensPoint - halfSize);
    float outerRadius = min(halfSize.x, halfSize.y) - 8.0 * dpiScale;
    distance = max(radial - outerRadius, optical.x - radial);
    // No shadow or tint may fill the transparent center.
    if (radial < optical.x - 0.708) return float4(0, 0, 0, 0);
    mask = annularPixelCoverage(lensPoint - halfSize, optical.x, outerRadius, distance);
    shadow = 0.0;
  }
  if (mask <= 0.0) return float4(0.0, 0.0, 0.0, shadow);
  float4 direct = desktopTexture.SampleLevel(linearClamp, sampleUv(screenPoint), 0);
  if (direct.a < 0.0001) return float4(0, 0, 0, 0);
  float3 fallback = direct.rgb / direct.a;
  float opticalProfile = saturate(optical.y);
  float dynamicProfile = step(0.5, opticalProfile);
  float thicknessProfile = saturate(optical.z);
  // Profile 3 (Dock, launchpad): a soft edge whose refraction, colour and
  // shading fade continuously into the clear body instead of a distinct rim.
  float softEdge = step(2.5, optical.y);
  float inside = max(-distance, 0.0);
  float bevelRadius = max((optical.x > 0.0 ? 18.0 * scale : radius * (18.0 / 22.0)), 0.001);
  float edge = 1.0 - smoothstep(0.0, bevelRadius, inside);
  float2 signedCorner = abs(lensPoint - halfSize) - halfSize + radius;
  float2 outsideCorner = max(signedCorner, 0.0);
  float2 distanceGradient = 0.0;
  if (length(outsideCorner) > 0.0001) {
    distanceGradient = sign(lensPoint - halfSize) * outsideCorner / length(outsideCorner);
  } else if (inside < bevelRadius) {
    float2 straightNormal = signedCorner.x > signedCorner.y ? float2(1.0, 0.0) : float2(0.0, 1.0);
    distanceGradient = sign(lensPoint - halfSize) * straightNormal;
  }
  if (optical.x > 0.0) {
    float2 radialPoint = lensPoint - halfSize;
    float radial = length(radialPoint);
    float outerRadius = min(halfSize.x, halfSize.y) - 8.0 * dpiScale;
    distanceGradient = radialPoint / max(radial, 0.001)
        * (radial < (outerRadius + optical.x) * 0.5 ? -1.0 : 1.0);
  }
  float bevelSlope = 3.0 * pow(saturate(1.0 - inside / bevelRadius), 2.0);
  float2 heightGradient = -distanceGradient * bevelSlope;
  float3 normal = normalize(float3(-heightGradient, 1.0));
  float depth = smoothstep(0.0, bevelRadius, inside);
  float2 center = (lensPoint - halfSize) / max(halfSize, float2(1.0, 1.0));
  float2 texel = 1.0 / max(outputSize, float2(1.0, 1.0));
  float opticalWidth = min(72.0 * scale * (1.0 + pressure * 0.035), min(halfSize.x, halfSize.y) * 0.92);
  float2 sideDistance = halfSize - abs(lensPoint - halfSize);
  float2 t = saturate(sideDistance / opticalWidth);
  float2 remaining = 1.0 - t;
  float2 meniscus = opticalWidth * remaining * remaining * (0.08 + 0.96 * t);
  float2 cornerFlow = 1.0 - 0.65 * pow(remaining.yx, 3.0);
  float2 shapeNormal = distanceGradient;
  if (dot(shapeNormal, shapeNormal) < 0.001) {
    shapeNormal = normalize(sign(center) + float2(0.0001, 0.0001));
  }
  float thickness = saturate(depth * (0.78 + 0.28 * thicknessProfile));
  // The SDF normal bends the independent offscreen background layer inward;
  // the thickness term makes the bend grow continuously toward the rim.
  float2 normalRefraction = -shapeNormal * (2.0 + 5.0 * edge) * thickness * texel;
  float2 refractPx = sign(center) * meniscus * cornerFlow * saturate(effect.x / 0.74)
      + normalRefraction * (0.35 + 0.65 * dynamicProfile);
  if (dynamicProfile > 0.5 && optical.x > 0.0) {
    // The round menu: a thin band in DIPs at both its inner and outer edge,
    // toward each edge's own normal, so the ring grows no wide refraction band.
    float lensBand = min(18.0 * scale, (min(halfSize.x, halfSize.y) - 8.0 * dpiScale - optical.x) * 0.30);
    refractPx = widgetLensOffset(distanceGradient, inside, lensBand,
        thicknessProfile * (1.0 + pressure * 0.06));
    refractPx += widgetInteriorOffset(center, scale, thicknessProfile, pressure)
        * 0.35 * smoothstep(0.0, radius, inside);
  } else if (dynamicProfile > 0.5) {
    // Rectangular widgets refract wallpaper and icons exactly as in 1.0.2.1.
    refractPx = widgetRectLensOffset(widgetLensNormal(lensPoint - halfSize, halfSize, radius), inside,
        min(radius * 0.90, min(halfSize.x, halfSize.y) * 0.75) * (1.0 + 0.6 * softEdge),
        thicknessProfile * (1.0 + pressure * 0.06));
    refractPx += widgetInteriorOffset(center, scale, thicknessProfile, pressure);
  }
  float flowRadius = saturate(length(center));
  float2 flowCurl = float2(
      sin(center.y * 3.1 + center.x * 1.35),
      cos(center.x * 2.45 - center.y * 1.15));
  float2 flowShear = float2(center.y * center.y - center.x * 0.35,
      center.x * center.x - center.y * 0.30);
  float2 contactFlow = (center - interaction.yz - drift * 0.08) / float2(0.62, 0.62);
  float contactEnvelope = exp(-dot(contactFlow, contactFlow)) * pressure;
  // Warp a single background coordinate field instead of mixing displaced
  // copies of text/icons. Existing critically damped springs drive this field;
  // it returns exactly to rest and has no independent clock or phase.
  refractPx -= dynamicProfile * drift * contactEnvelope * (2.0 + edge) * scale;
  float2 baseUv = sampleUv(screenPoint + refractPx);
  float2 sampleMinUv = (sampleBoundsMin - outputOrigin) / outputSize;
  float2 sampleMaxUv = (sampleBoundsMax - outputOrigin) / outputSize;
  baseUv = clamp(baseUv, sampleMinUv + texel * 0.5, sampleMaxUv - texel * 0.5);
  float2 flowDirection = normalize(flowCurl + flowShear * 0.45
      + drift * contactEnvelope * 0.95 + float2(0.001, 0.0));
  float flowRipple = 0.5 + 0.5 * sin(dot(center, flowDirection) * 9.0);
  float flowStrength = (0.32 + contactEnvelope * (0.42 + length(drift) * 0.18)
      + dynamicProfile * (0.08 + 0.02 * opticalProfile) * flowRipple) * flowRadius;
  float flowAmount = (14.0 + edge * 26.0 + dynamicProfile * thickness * (7.0 + opticalProfile)) * flowStrength * scale;
  // Keep the three color taps on the same local curved sample field. Dynamic
  // widget profiles expose a stronger edge-only spectrum without separating
  // glyphs in the clear interior.
  float chromaScale = (1.0 + dynamicProfile * (2.2 + 0.35 * opticalProfile)) * (1.0 - 0.85 * softEdge);
  float2 chromaDirection = refractPx * effect.z * 0.065 * edge * chromaScale;
  if (dynamicProfile > 0.5) {
    float normalizedSeparation = length(chromaDirection) / (1.10 * scale);
    chromaDirection /= sqrt(1.0 + normalizedSeparation * normalizedSeparation);
  }
  float3 sharp = sampleSharp(baseUv, texel, chromaDirection, 1.0, fallback);
  float3 soft = dynamicProfile > 0.5
      ? sampleWidgetBody(baseUv, texel * scale, fallback)
      : sampleSoft(baseUv, texel * scale, fallback);
  float softMix = dynamicProfile > 0.5 ? 0.035 + edge * 0.15 : 0.12 + edge * 0.24;
  float3 transmission = lerp(sharp, soft, softMix);
  float3 color = transmission;
  float reflectionRadius = min(radius + 56.0 * scale, min(halfSize.x, halfSize.y));
  float reflectionWidth = reflectionRadius * 0.75;
  float reflectionFade = exp(-inside / (18.0 * scale))
      * (1.0 - smoothstep(reflectionWidth * (2.0 / 3.0), reflectionWidth, inside));
  float3 environment = color;
  [branch] if (reflectionFade > 0.0001) {
    float2 reflectionCorner = max(abs(lensPoint - halfSize) - halfSize + reflectionRadius, 0.0);
    float2 reflectionNormal = sign(lensPoint - halfSize) * reflectionCorner / max(length(reflectionCorner), 0.001);
    float2 reflectionUv = sampleUv(screenPoint + reflectionNormal * 24.0 * scale);
    environment = sampleEnvironment(reflectionUv, texel * scale, fallback);
  }
  float environmentLight = dot(environment, float3(0.299, 0.587, 0.114));
  float reflectionBand = reflectionFade * (0.18 + edge * 0.05) * (1.0 - 0.5 * softEdge);
  color = lerp(color, environment, reflectionBand);
  [branch] if (readability.x > 0.0) {
    float body = smoothstep(0.0, 26.0 * scale, inside);
    float3 readableScene = dynamicProfile > 0.5
        ? sampleWidgetBody(baseUv, texel * scale, fallback)
        : sampleReadable(baseUv, texel * scale, fallback);
    // Preserve the legacy profile's filtering; widget profiles use one local
    // Gaussian footprint centered on the same refracted coordinate as sharp.
    // Widely separated taps create visible extra copies, not liquid refraction.
    [branch] if (dynamicProfile < 0.5) {
      float2 liquidEntryUv = sampleUv(screenPoint + refractPx
          + flowDirection * flowAmount * texel * 0.72);
      float3 liquidScene = sampleLiquid(liquidEntryUv, texel * scale, flowDirection, flowAmount, fallback);
      float liquidMix = saturate(0.62 + edge * 0.24 + flowStrength * 0.16);
      readableScene = lerp(readableScene, liquidScene, liquidMix);
    }
    float scatter = saturate(readability.x) * (dynamicProfile > 0.5
        ? lerp(0.18, 0.68, body) : lerp(0.30, 0.90, body));
    color = lerp(color, readableScene, scatter);
    float3 linearColor = toLinear(saturate(color));
    float luminance = dot(linearColor, float3(0.2126, 0.7152, 0.0722));
    // White foregrounds must not turn the transmitted scene into a dark panel.
    // Keep that scene at its captured brightness; only lift near-black scenes
    // when the widget uses a dark foreground.
    float darkTextLift = saturate((0.195 - luminance) / max(1.0 - luminance, 0.0001));
    float3 darkTextScene = lerp(linearColor, 1.0, darkTextLift);
    float3 adapted = toDisplay(lerp(darkTextScene, linearColor, saturate(readability.y)));
    color = lerp(color, adapted, saturate(readability.x) * lerp(0.45, 1.0, body));
  }
  float luminance = dot(color, float3(0.299, 0.587, 0.114));
  color = (color - luminance) * 1.015 + luminance;
  color *= 0.96 + thickness * 0.035;
  // Keep the transmitted body at the captured brightness. Reflections below
  // provide edge highlights without a constant white veil over the center.
  color += 0.008 * (0.18 + thickness * 0.22);
  float3 view = float3(0.0, 0.0, 1.0);
  // The key highlight follows the pointer around the rim (widget profiles)
  // and rests top-left. Rotating the original light by the key's angle keeps
  // the rest position pixel-identical to the fixed light.
  const float2 restKey = float2(-0.62, -0.85);
  float2 keyCenter = restKey;
  if (dynamicProfile > 0.5 && dot(motion.zw, motion.zw) > 0.04) {
    float2 pointer = motion.zw;
    // Pull an interior pointer out toward the rim it is nearest.
    keyCenter = pointer / max(max(abs(pointer.x), abs(pointer.y)), 0.55) * 0.95;
  }
  float2 restDirection = normalize(restKey);
  float2 keyDirection = normalize(keyCenter);
  float2 rotation = float2(dot(keyDirection, restDirection),
      keyDirection.y * restDirection.x - keyDirection.x * restDirection.y);
  float2 baseLight = float2(-0.42, -0.78);
  float2 turnedLight = float2(baseLight.x * rotation.x - baseLight.y * rotation.y,
      baseLight.x * rotation.y + baseLight.y * rotation.x);
  float3 light = normalize(float3(turnedLight, 1.0));
  float inset = inside / scale;
  // Widget profiles keep the key short along the rim: a small, precise glint
  // rather than a long bright line down a straight edge.
  float2 keyPosition = (center - keyCenter) / lerp(float2(0.62, 0.75), float2(0.40, 0.46), dynamicProfile);
  float keyEnvelope = exp(-dot(keyPosition, keyPosition));
  float keyFacing = pow(saturate(dot(distanceGradient, normalize(light.xy))), 1.8);
  float key = keyEnvelope * keyFacing;
  float2 fillCenter = float2(0.85, 0.68) - (keyCenter - restKey) * 0.8;
  float2 fillPosition = (center - fillCenter) / float2(0.46, 0.62);
  float2 baseFill = float2(0.65, 0.76);
  float2 fillDirection = float2(baseFill.x * rotation.x - baseFill.y * rotation.y,
      baseFill.x * rotation.y + baseFill.y * rotation.x);
  float fill = exp(-dot(fillPosition, fillPosition))
      * pow(saturate(dot(distanceGradient, normalize(fillDirection))), 4.0);
  float lightResponse = 0.45 + environmentLight * 1.10;
  float3 reflectionTint = lerp(float3(0.97, 0.985, 1.0),
      saturate(environment / max(environmentLight, 0.12)), 0.25);
  // Widget profiles use a hairline crest (about one DIP) and almost no halo.
  float crestWidth = lerp(0.45 + 0.85 * (1.0 - key), 0.55 + 0.30 * (1.0 - key), dynamicProfile);
  float crest = exp(-pow((inset - lerp(1.1, 1.05, dynamicProfile)) / crestWidth, 2.0));
  float halo = exp(-pow(inset / lerp(4.2, 2.2, dynamicProfile), 2.0)) * lerp(1.0, 0.35, dynamicProfile);
  float specular = pow(saturate(dot(normal, normalize(light + view))), 54.0)
      * exp(-inset / 3.0) * keyEnvelope * (0.72 + 0.45 * thickness);
  float exitGlint = exp(-pow((inset - 0.8) / 0.65, 2.0)) * fill;
  float absorption = exp(-inset / 4.0) * (key * 0.035 + fill * 0.025);
  float thicknessShadow = edge * (0.022 + 0.035 * thickness) * (0.65 + 0.35 * keyEnvelope) * (1.0 - 0.6 * softEdge);
  color *= 1.0 - absorption - thicknessShadow;
  color += reflectionTint * ((crest * key * lerp(0.62, 0.72, dynamicProfile) + halo * key * 0.10
      + specular * 0.40 + exitGlint * 0.28) * lightResponse);
  float2 contactDelta = (center - interaction.yz - drift * 0.10) / float2(0.55, 0.55);
  float contactLight = exp(-dot(contactDelta, contactDelta)) * saturate(interaction.w);
  color += reflectionTint * contactLight * (crest * 0.15 + halo * 0.045 + reflectionFade * 0.028);
  color = saturate(color);
  float alpha = mask * direct.a;
  return float4(color * alpha, alpha + shadow * (1.0 - alpha));
}
)";

inline constexpr MaterialDefinition kLiquidGlassMaterial = {
  "liquid-glass",
  kLiquidGlassVertexShader,
  kLiquidGlassPixelShader,
};

}  // namespace lume_materials
