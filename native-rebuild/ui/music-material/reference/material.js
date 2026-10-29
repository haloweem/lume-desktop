// material:liquidGlassShaders.ts
var liquidGlassVertexShader = `#version 300 es
layout(location = 0) in vec2 a_position;
out vec2 v_uv;

void main() {
    v_uv = (a_position + 1.0) * 0.5;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
`;
var liquidGlassBackgroundShader = `#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_background;
uniform sampler2D u_scene;
uniform int u_hasBackground;
uniform int u_hasScene;
uniform vec4 u_sceneBounds;
uniform float u_mediaAspect;
uniform vec2 u_viewportSize;
uniform vec2 u_surfaceOffset;
uniform vec2 u_surfaceSize;
uniform vec3 u_voidColor;
uniform vec2 u_capsuleSize;
uniform float u_surfacePadding;
uniform vec2 u_capsuleOffset;
uniform float u_shapeRadius;
uniform float u_shapeRoundness;
uniform float u_shadowExpand;
uniform float u_shadowFactor;
uniform vec2 u_shadowPosition;

float backgroundSuperellipseCornerSDF(vec2 point, float radius, float roundness) {
    point = abs(point);
    return pow(pow(point.x, roundness) + pow(point.y, roundness), 1.0 / roundness) - radius;
}

float backgroundCapsuleSDF(vec2 pixel) {
    vec2 center = u_capsuleOffset + u_capsuleSize * 0.5 + u_shadowPosition;
    vec2 distanceToEdge = abs(pixel - center) - u_capsuleSize * 0.5;
    float radius = min(u_capsuleSize.y * 0.5, u_capsuleSize.y * 0.5 * u_shapeRadius);
    if (distanceToEdge.x > -radius && distanceToEdge.y > -radius) {
        vec2 cornerCenter = sign(pixel - center) * (u_capsuleSize * 0.5 - vec2(radius));
        return backgroundSuperellipseCornerSDF(pixel - center - cornerCenter, radius, u_shapeRoundness);
    }
    return min(max(distanceToEdge.x, distanceToEdge.y), 0.0) + length(max(distanceToEdge, 0.0));
}

vec2 coverUv(vec2 uv, float viewportAspect, float mediaAspect) {
    if (viewportAspect > mediaAspect) {
        float scale = mediaAspect / viewportAspect;
        uv.y = uv.y * scale + 0.5 - 0.5 * scale;
    } else {
        float scale = viewportAspect / mediaAspect;
        uv.x = uv.x * scale + 0.5 - 0.5 * scale;
    }
    return uv;
}

void main() {
    vec2 surfacePixel = vec2(
        v_uv.x * u_surfaceSize.x,
        (1.0 - v_uv.y) * u_surfaceSize.y
    );
    float shadowDistanceCss = backgroundCapsuleSDF(surfacePixel);
    float shadow = exp(-abs(shadowDistanceCss) / max(u_shadowExpand, 0.001))
        * 0.6 * u_shadowFactor;
    vec3 backgroundColor = u_voidColor;
    vec2 viewportPixel = vec2(
        u_surfaceOffset.x + v_uv.x * u_surfaceSize.x,
        u_surfaceOffset.y + (1.0 - v_uv.y) * u_surfaceSize.y
    );
    vec2 viewportUv = vec2(
        viewportPixel.x / max(u_viewportSize.x, 1.0),
        1.0 - viewportPixel.y / max(u_viewportSize.y, 1.0)
    );
    if (u_hasBackground != 0) {
        vec2 mediaUv = coverUv(
            viewportUv,
            u_viewportSize.x / max(u_viewportSize.y, 1.0),
            max(u_mediaAspect, 0.0001)
        );
        backgroundColor = texture(u_background, clamp(mediaUv, 0.0, 1.0)).rgb;
    }
    if (u_hasScene != 0) {
        vec2 sceneUv = (viewportPixel - u_sceneBounds.xy) / u_sceneBounds.zw;
        float inside = step(0.0, sceneUv.x) * step(sceneUv.x, 1.0) * step(0.0, sceneUv.y) * step(sceneUv.y, 1.0);
        vec4 scene = texture(u_scene, vec2(clamp(sceneUv.x, 0.0, 1.0), 1.0 - clamp(sceneUv.y, 0.0, 1.0))) * inside;
        backgroundColor = mix(backgroundColor, scene.rgb, scene.a);
    }
    fragColor = vec4(max(backgroundColor - vec3(shadow), vec3(0.0)), 1.0);
}
`;
var liquidGlassBlurShader = `#version 300 es
precision highp float;

#define MAX_BLUR_RADIUS 200

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_source;
uniform vec2 u_blurDirection;
uniform int u_blurRadius;
uniform float u_blurWeights[MAX_BLUR_RADIUS + 1];

void main() {
    vec4 color = texture(u_source, v_uv) * u_blurWeights[0];
    for (int i = 1; i <= MAX_BLUR_RADIUS; ++i) {
        if (i > u_blurRadius) break;
        float weight = u_blurWeights[i];
        vec2 offset = u_blurDirection * float(i);
        color += texture(u_source, v_uv + offset) * weight;
        color += texture(u_source, v_uv - offset) * weight;
    }
    fragColor = color;
}
`;
var liquidGlassMainShader = `#version 300 es
precision highp float;

#define PI 3.14159265359
const float N_R = 1.0 - 0.02;
const float N_G = 1.0;
const float N_B = 1.0 + 0.02;

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_background;
uniform sampler2D u_blurredBackground;
uniform vec2 u_resolution;
uniform float u_dpr;
uniform float u_refThickness;
uniform float u_refFactor;
uniform float u_refDispersion;
uniform float u_refFresnelRange;
uniform float u_refFresnelHardness;
uniform float u_refFresnelFactor;
uniform float u_glareRange;
uniform float u_glareHardness;
uniform float u_glareFactor;
uniform float u_glareConvergence;
uniform float u_glareOppositeFactor;
uniform float u_glareAngle;
uniform int u_blurEdge;
uniform vec4 u_tint;
uniform float u_shapeRadius;
uniform float u_shapeRoundness;
uniform vec2 u_backdropScale;
uniform vec2 u_refractionUvScale;
uniform vec2 u_opticalViewportSize;

vec2 backdropUv(vec2 canvasUv) {
    return vec2(0.5) + (canvasUv - vec2(0.5)) * u_backdropScale;
}

float superellipseCornerSDF(vec2 point, float radius, float roundness) {
    point = abs(point);
    float value = pow(pow(point.x, roundness) + pow(point.y, roundness), 1.0 / roundness);
    return value - radius;
}

float roundedRectSDF(
    vec2 point,
    vec2 center,
    float width,
    float height,
    float cornerRadius,
    float roundness
) {
    point -= center;
    float radius = cornerRadius * u_dpr;
    vec2 size = vec2(width * u_dpr, height * u_dpr);
    vec2 distanceToEdge = abs(point) - size * 0.5;
    if (distanceToEdge.x > -radius && distanceToEdge.y > -radius) {
        vec2 cornerCenter = sign(point) * (size * 0.5 - vec2(radius));
        return superellipseCornerSDF(point - cornerCenter, radius, roundness);
    }
    return min(max(distanceToEdge.x, distanceToEdge.y), 0.0) + length(max(distanceToEdge, 0.0));
}

float mainSDF(vec2 pixel) {
    vec2 resolutionCss = u_resolution / max(u_dpr, 1.0);
    float width = max(1.0, resolutionCss.x);
    float height = max(1.0, resolutionCss.y);
    float radius = min(height * 0.5, height * 0.5 * u_shapeRadius);
    return roundedRectSDF(pixel, u_resolution * 0.5, width, height, radius, u_shapeRoundness)
        / max(u_resolution.y, 1.0);
}

// Softens the repeated optical band on the long flat sides while preserving
// the capsule silhouette and full-strength curved end caps.
float longEdgeOpticalWeight(vec2 pixelCss, vec2 sizeCss) {
    vec2 halfSize = max(sizeCss * 0.5, vec2(1.0));
    float radius = min(halfSize.y, halfSize.y * clamp(u_shapeRadius, 0.0, 1.0));
    float straightHalfSpan = max(halfSize.x - radius, 1.0);
    float longitudinal = clamp(abs(pixelCss.x - halfSize.x) / straightHalfSpan, 0.0, 1.0);
    float endWeight = mix(
        0.32,
        1.0,
        pow(smoothstep(0.0, 1.0, longitudinal), 1.35)
    );
    float horizontalEdge = smoothstep(
        0.45,
        0.92,
        abs(pixelCss.y - halfSize.y) / halfSize.y
    );
    return mix(1.0, endWeight, horizontalEdge);
}

vec2 getNormal(vec2 pixel) {
    vec2 h = vec2(max(abs(dFdx(pixel.x)), 0.0001), max(abs(dFdy(pixel.y)), 0.0001));
    vec2 grad = vec2(
        mainSDF(pixel + vec2(h.x, 0.0)) - mainSDF(pixel - vec2(h.x, 0.0)),
        mainSDF(pixel + vec2(0.0, h.y)) - mainSDF(pixel - vec2(0.0, h.y))
    ) / (2.0 * h);
    float viewportScale = u_resolution.y / max(u_opticalViewportSize.y, 1.0);
    return grad * 1.414213562 * 1000.0 * viewportScale;
}

float safeAsin(float value) {
    return asin(clamp(value, -1.0, 1.0));
}

vec4 getTextureDispersion(vec2 offset, float blurMixRate, float dispersionFactor) {
    vec4 pixel = vec4(1.0);
    vec2 sourceUv = backdropUv(v_uv);
    vec2 scaledOffset = offset * u_refractionUvScale;
    vec2 redOffset = scaledOffset * (1.0 - (N_R - 1.0) * dispersionFactor);
    vec2 greenOffset = scaledOffset * (1.0 - (N_G - 1.0) * dispersionFactor);
    vec2 blueOffset = scaledOffset * (1.0 - (N_B - 1.0) * dispersionFactor);

    vec4 sharp = vec4(
        texture(u_background, sourceUv + redOffset).r,
        texture(u_background, sourceUv + greenOffset).g,
        texture(u_background, sourceUv + blueOffset).b,
        1.0
    );
    vec4 blurred = vec4(
        texture(u_blurredBackground, sourceUv + redOffset).r,
        texture(u_blurredBackground, sourceUv + greenOffset).g,
        texture(u_blurredBackground, sourceUv + blueOffset).b,
        1.0
    );
    pixel.rgb = mix(sharp.rgb, blurred.rgb, blurMixRate);
    return pixel;
}

float angleOf(vec2 vector) {
    float angle = atan(vector.y, vector.x);
    return angle < 0.0 ? angle + 2.0 * PI : angle;
}

const vec3 D65_WHITE = vec3(0.95045592705, 1.0, 1.08905775076);
const vec3 D50_WHITE = vec3(0.96429567643, 1.0, 0.82510460251);
vec3 WHITE = D65_WHITE;
const mat3 RGB_TO_XYZ_M = mat3(
    0.4124, 0.3576, 0.1805,
    0.2126, 0.7152, 0.0722,
    0.0193, 0.1192, 0.9505
);
const mat3 XYZ_TO_XYZ50_M = mat3(
    1.0479298208405488, 0.022946793341019088, -0.05019222954313557,
    0.029627815688159344, 0.990434484573249, -0.01707382502938514,
    -0.009243058152591178, 0.015055144896577895, 0.7518742899580008
);
const mat3 XYZ_TO_RGB_M = mat3(
    3.2406255, -1.537208, -0.4986286,
    -0.9689307, 1.8757561, 0.0415175,
    0.0557101, -0.2040211, 1.0569959
);
const mat3 XYZ50_TO_XYZ_M = mat3(
    0.9554734527042182, -0.023098536874261423, 0.0632593086610217,
    -0.028369706963208136, 1.0099954580058226, 0.021041398966943008,
    0.012314001688319899, -0.020507696433477912, 1.3303659366080753
);

float UNCOMPAND_SRGB(float value) {
    return value > 0.04045 ? pow((value + 0.055) / 1.055, 2.4) : value / 12.92;
}

float COMPAND_RGB(float value) {
    return value <= 0.0031308 ? 12.92 * value : 1.055 * pow(value, 0.41666666666) - 0.055;
}

vec3 RGB_TO_XYZ(vec3 rgb) {
    return WHITE == D65_WHITE ? rgb * RGB_TO_XYZ_M : rgb * RGB_TO_XYZ_M * XYZ_TO_XYZ50_M;
}

vec3 SRGB_TO_RGB(vec3 srgb) {
    return vec3(UNCOMPAND_SRGB(srgb.x), UNCOMPAND_SRGB(srgb.y), UNCOMPAND_SRGB(srgb.z));
}

vec3 RGB_TO_SRGB(vec3 rgb) {
    return vec3(COMPAND_RGB(rgb.x), COMPAND_RGB(rgb.y), COMPAND_RGB(rgb.z));
}

vec3 SRGB_TO_XYZ(vec3 srgb) {
    return RGB_TO_XYZ(SRGB_TO_RGB(srgb));
}

float XYZ_TO_LAB_F(float value) {
    return value > 0.00885645167
        ? pow(value, 0.333333333)
        : 7.78703703704 * value + 0.13793103448;
}

vec3 XYZ_TO_LAB(vec3 xyz) {
    vec3 scaled = xyz / WHITE;
    scaled = vec3(
        XYZ_TO_LAB_F(scaled.x),
        XYZ_TO_LAB_F(scaled.y),
        XYZ_TO_LAB_F(scaled.z)
    );
    return vec3(
        116.0 * scaled.y - 16.0,
        500.0 * (scaled.x - scaled.y),
        200.0 * (scaled.y - scaled.z)
    );
}

vec3 SRGB_TO_LAB(vec3 srgb) {
    return XYZ_TO_LAB(SRGB_TO_XYZ(srgb));
}

vec3 LAB_TO_LCH(vec3 lab) {
    return vec3(lab.x, sqrt(dot(lab.yz, lab.yz)), atan(lab.z, lab.y) * 57.2957795131);
}

vec3 SRGB_TO_LCH(vec3 srgb) {
    return LAB_TO_LCH(SRGB_TO_LAB(srgb));
}

vec3 XYZ_TO_RGB(vec3 xyz) {
    return WHITE == D65_WHITE ? xyz * XYZ_TO_RGB_M : xyz * XYZ50_TO_XYZ_M * XYZ_TO_RGB_M;
}

vec3 XYZ_TO_SRGB(vec3 xyz) {
    return RGB_TO_SRGB(XYZ_TO_RGB(xyz));
}

float LAB_TO_XYZ_F(float value) {
    return value > 0.206897
        ? value * value * value
        : 0.12841854934 * (value - 0.137931034);
}

vec3 LAB_TO_XYZ(vec3 lab) {
    float whitePoint = (lab.x + 16.0) / 116.0;
    return WHITE * vec3(
        LAB_TO_XYZ_F(whitePoint + lab.y / 500.0),
        LAB_TO_XYZ_F(whitePoint),
        LAB_TO_XYZ_F(whitePoint - lab.z / 200.0)
    );
}

vec3 LAB_TO_SRGB(vec3 lab) {
    return XYZ_TO_SRGB(LAB_TO_XYZ(lab));
}

vec3 LCH_TO_LAB(vec3 lch) {
    return vec3(
        lch.x,
        lch.y * cos(lch.z * 0.01745329251),
        lch.y * sin(lch.z * 0.01745329251)
    );
}

vec3 LCH_TO_SRGB(vec3 lch) {
    return LAB_TO_SRGB(LCH_TO_LAB(lch));
}

void main() {
    vec2 resolution1x = u_resolution / max(u_dpr, 1.0);
    float merged = mainSDF(gl_FragCoord.xy);
    float mergedCss = merged * resolution1x.y;
    float edgeAaCss = max(fwidth(mergedCss), 0.75);
    float shapeAlpha = 1.0 - smoothstep(-edgeAaCss, edgeAaCss, mergedCss);
    vec4 outColor;

    if (mergedCss < 2.0) {
        float depthCss = -mergedCss;
        float xRatio = 1.0 - depthCss / max(u_refThickness, 0.001);
        float thetaI = safeAsin(pow(xRatio, 2.0));
        float thetaT = safeAsin(sin(thetaI) / max(u_refFactor, 1.0));
        float edgeFactor = -tan(thetaT - thetaI);
        if (depthCss >= u_refThickness) edgeFactor = 0.0;
        float opticalWeight = longEdgeOpticalWeight(
            gl_FragCoord.xy / max(u_dpr, 1.0),
            resolution1x
        );
        edgeFactor *= opticalWeight;

         if (edgeFactor <= 0.0) {
             outColor = texture(u_blurredBackground, backdropUv(v_uv));
             outColor = mix(outColor, u_tint, u_tint.a * 0.8);
         } else {
            vec2 normal = getNormal(gl_FragCoord.xy);
            float edgeH = depthCss / u_refThickness;
            vec4 blurredPixel = getTextureDispersion(
                -normal * edgeFactor * 0.05 * u_dpr * vec2(
                    u_resolution.y / max(u_resolution.x, 1.0),
                    1.0
                ),
                u_blurEdge > 0 ? 1.0 : edgeH,
                u_refDispersion
            );
            outColor = mix(blurredPixel, u_tint, u_tint.a * 0.8);
            float fresnelFactor = clamp(pow(max(
                1.0 - depthCss / 1500.0 * pow(500.0 / max(u_refFresnelRange, 0.001), 2.0)
                    + u_refFresnelHardness,
                0.0), 5.0), 0.0, 1.0);
            fresnelFactor *= opticalWeight;
            vec3 fresnelTintLCH = SRGB_TO_LCH(mix(vec3(1.0), u_tint.rgb, u_tint.a * 0.5));
            fresnelTintLCH.x = clamp(fresnelTintLCH.x + 20.0 * fresnelFactor * u_refFresnelFactor, 0.0, 100.0);
            outColor = mix(
                outColor,
                vec4(LCH_TO_SRGB(fresnelTintLCH), 1.0),
                clamp(fresnelFactor * u_refFresnelFactor * 0.7 * length(normal), 0.0, 1.0)
            );

            float glareGeoFactor = clamp(pow(max(
                1.0 - depthCss / 1500.0 * pow(500.0 / max(u_glareRange, 0.001), 2.0)
                    + u_glareHardness,
                0.0), 5.0), 0.0, 1.0);
            glareGeoFactor *= opticalWeight;
            float glareAngle = (angleOf(normalize(normal)) - PI / 4.0 + u_glareAngle) * 2.0;
            int glareFarside = 0;
            if (glareAngle > PI * 1.5 && glareAngle < PI * 3.5 || glareAngle < -PI * 0.5) {
                glareFarside = 1;
            }
            float glareAngleFactor = (0.5 + sin(glareAngle) * 0.5)
                * (glareFarside == 1 ? 1.2 * u_glareOppositeFactor : 1.2) * u_glareFactor;
            glareAngleFactor = clamp(pow(max(glareAngleFactor, 0.0), 0.1 + u_glareConvergence * 2.0), 0.0, 1.0);

            vec3 glareTintLCH = SRGB_TO_LCH(mix(blurredPixel.rgb, u_tint.rgb, u_tint.a * 0.5));
            glareTintLCH.x = clamp(glareTintLCH.x + 150.0 * glareAngleFactor * glareGeoFactor, 0.0, 120.0);
            glareTintLCH.y = clamp(glareTintLCH.y + 30.0 * glareAngleFactor * glareGeoFactor, 0.0, 150.0);
            outColor = mix(
                outColor,
                vec4(LCH_TO_SRGB(glareTintLCH), 1.0),
                clamp(glareAngleFactor * glareGeoFactor * length(normal), 0.0, 1.0)
            );
        }
    } else {
        outColor = vec4(0.0);
    }

    fragColor = vec4(outColor.rgb, outColor.a * shapeAlpha);
}
`;

// material:liquidGlassPreset.ts
var LIQUID_GLASS_STUDIO_PRESET = {
  refThickness: 20,
  refFactor: 1.4,
  refDispersion: 14,
  refFresnelRange: 30,
  refFresnelHardness: 0.2,
  refFresnelFactor: 0.2,
  glareRange: 30,
  glareHardness: 0.2,
  glareFactor: 0.9,
  glareConvergence: 0.5,
  glareOppositeFactor: 0.8,
  glareAngle: -45,
  blurRadius: 1,
  blurEdge: true,
  tint: [1, 1, 1, 0],
  shadowExpand: 25,
  shadowFactor: 0.15,
  shadowPosition: [0, -10],
  shapeRadius: 1,
  shapeRoundness: 2,
  surface: {
    width: 600,
    height: 90,
    radiusPercent: 100
  }
};

// material:liquidGlassModel.ts
var resolveLiquidGlassRenderScale = (devicePixelRatio) => {
  const normalizedRatio = Number.isFinite(devicePixelRatio) && devicePixelRatio > 0 ? devicePixelRatio : 1;
  return Math.min(1.5, Math.max(1, normalizedRatio));
};
var resolveLiquidGlassTargetScale = (targetScale) => {
  if (!Number.isFinite(targetScale)) return 1;
  return Math.min(1, Math.max(0.1, targetScale ?? 1));
};
var resolveLiquidGlassShapeRadiusFactor = (surfaceHeight, cornerRadiusPx, fallbackFactor = 1) => {
  if (!Number.isFinite(cornerRadiusPx)) {
    return Math.min(1, Math.max(0, fallbackFactor));
  }
  const height = Number.isFinite(surfaceHeight) && surfaceHeight > 0 ? surfaceHeight : 1;
  const maxCornerRadius = height / 2;
  return Math.min(1, Math.max(0, (cornerRadiusPx ?? 0) / maxCornerRadius));
};
var resolveLiquidGlassOpticalCanvasSize = (viewportWidth, viewportHeight) => {
  const width = Number.isFinite(viewportWidth) && viewportWidth > 0 ? viewportWidth : 0;
  const height = Number.isFinite(viewportHeight) && viewportHeight > 0 ? viewportHeight : 0;
  return Math.max(Math.min(width, height) - 150, 600);
};
var resolveLiquidGlassTextureRegion = (reference, surfaceWidth, surfaceHeight, dpr, targetScale = 1, padding = 96) => {
  const full = resolveLiquidGlassReferenceTargetSize(
    reference.opticalSizeCss,
    dpr,
    padding,
    targetScale,
    reference.targetSizeCss
  );
  const cssPerTexel = reference.targetSizeCss / full.width;
  const safePadding = Number.isFinite(padding) ? Math.max(0, padding) : 96;
  const crop = (size) => {
    const extent = (Number.isFinite(size) ? Math.max(1, size) : 1) + 2 * safePadding;
    const start = Math.max(0, Math.floor((reference.targetSizeCss - extent) / (2 * cssPerTexel)));
    return { start, pixels: Math.max(1, full.width - 2 * start) };
  };
  const x = crop(surfaceWidth);
  const y = crop(surfaceHeight);
  return {
    width: x.pixels,
    height: y.pixels,
    sizeCss: { width: x.pixels * cssPerTexel, height: y.pixels * cssPerTexel },
    offsetCss: {
      x: reference.surfaceOffsetCss.x + x.start * cssPerTexel,
      y: reference.surfaceOffsetCss.y + y.start * cssPerTexel
    }
  };
};
var resolveLiquidGlassReferenceTargetSize = (opticalSizeCss, devicePixelRatio, overscanCssPx, targetScale, targetSizeCss) => {
  const dpr = resolveLiquidGlassRenderScale(devicePixelRatio);
  const opticalSize = Number.isFinite(opticalSizeCss) && opticalSizeCss > 0 ? opticalSizeCss : 600;
  const overscan = Number.isFinite(overscanCssPx) && overscanCssPx >= 0 ? overscanCssPx : 0;
  const referenceSize = Number.isFinite(targetSizeCss) && (targetSizeCss ?? 0) > 0 ? targetSizeCss : opticalSize + overscan * 2;
  const fullResolutionSize = Math.round(referenceSize * dpr);
  const physicalSize = Math.round(fullResolutionSize * resolveLiquidGlassTargetScale(targetScale));
  return { width: Math.max(1, physicalSize), height: Math.max(1, physicalSize) };
};
var resolveLiquidGlassReferenceFrame = ({
  viewportWidth,
  viewportHeight,
  surfaceX,
  surfaceY,
  surfaceWidth,
  surfaceHeight,
  devicePixelRatio,
  overscanCssPx = 96
}) => {
  const opticalSizeCss = resolveLiquidGlassOpticalCanvasSize(viewportWidth, viewportHeight);
  const overscan = Math.max(0, overscanCssPx);
  const normalizedSurfaceWidth = Number.isFinite(surfaceWidth) && surfaceWidth > 0 ? surfaceWidth : 0;
  const normalizedSurfaceHeight = Number.isFinite(surfaceHeight) && surfaceHeight > 0 ? surfaceHeight : 0;
  const targetSizeCss = Math.max(opticalSizeCss, normalizedSurfaceWidth, normalizedSurfaceHeight) + overscan * 2;
  const surfaceCenterCss = {
    x: surfaceX + surfaceWidth / 2,
    y: surfaceY + surfaceHeight / 2
  };
  const surfaceOffsetCss = {
    x: surfaceCenterCss.x - targetSizeCss / 2,
    y: surfaceCenterCss.y - targetSizeCss / 2
  };
  resolveLiquidGlassRenderScale(devicePixelRatio);
  return { opticalSizeCss, targetSizeCss, surfaceCenterCss, surfaceOffsetCss };
};
var resolveLiquidGlassBackdrop = (mode, media) => mode === "wallpaper" && media ? { kind: media.kind, url: media.url } : { kind: "void", url: null };
var shouldContinuouslyRenderLiquidGlass = ({
  visible,
  mediaKind,
  mediaReady
}) => visible && mediaKind === "video" && mediaReady;

// material:LiquidGlassRenderer.ts
var computeGaussianKernelByRadius = (radius) => {
  const clampedRadius = Math.max(0, Math.min(200, Math.round(radius)));
  if (clampedRadius === 0) return new Float32Array([1]);
  const sigma = clampedRadius / 3;
  const weights = new Float32Array(clampedRadius + 1);
  let sum = 0;
  for (let index = 0; index <= clampedRadius; index += 1) {
    const weight = Math.exp(-0.5 * (index * index) / (sigma * sigma));
    weights[index] = weight;
    sum += index === 0 ? weight : weight * 2;
  }
  for (let index = 0; index < weights.length; index += 1) {
    weights[index] /= sum;
  }
  return weights;
};
var BACKDROP_OVERSCAN_CSS_PX = 96;
var createShader = (gl, type, source) => {
  const shader = gl.createShader(type);
  if (!shader) {
    throw new Error("Unable to allocate liquid-glass shader");
  }
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    const message = gl.getShaderInfoLog(shader) || "Unknown shader compile error";
    gl.deleteShader(shader);
    throw new Error(message);
  }
  return shader;
};
var createProgram = (gl, fragmentSource) => {
  const vertexShader = createShader(gl, gl.VERTEX_SHADER, liquidGlassVertexShader);
  const fragmentShader = createShader(gl, gl.FRAGMENT_SHADER, fragmentSource);
  const program = gl.createProgram();
  if (!program) {
    gl.deleteShader(vertexShader);
    gl.deleteShader(fragmentShader);
    throw new Error("Unable to allocate liquid-glass program");
  }
  gl.attachShader(program, vertexShader);
  gl.attachShader(program, fragmentShader);
  gl.linkProgram(program);
  gl.deleteShader(vertexShader);
  gl.deleteShader(fragmentShader);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    const message = gl.getProgramInfoLog(program) || "Unknown program link error";
    gl.deleteProgram(program);
    throw new Error(message);
  }
  return program;
};
var createRenderTarget = (gl, width, height, highPrecision) => {
  const framebuffer = gl.createFramebuffer();
  const texture = gl.createTexture();
  if (!framebuffer || !texture) {
    if (framebuffer) gl.deleteFramebuffer(framebuffer);
    if (texture) gl.deleteTexture(texture);
    throw new Error("Unable to allocate liquid-glass render target");
  }
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  const internalFormat = highPrecision ? gl.RGBA16F : gl.RGBA8;
  const pixelType = highPrecision ? gl.FLOAT : gl.UNSIGNED_BYTE;
  gl.texImage2D(gl.TEXTURE_2D, 0, internalFormat, width, height, 0, gl.RGBA, pixelType, null);
  gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
  gl.framebufferTexture2D(
    gl.FRAMEBUFFER,
    gl.COLOR_ATTACHMENT0,
    gl.TEXTURE_2D,
    texture,
    0
  );
  if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
    gl.deleteFramebuffer(framebuffer);
    gl.deleteTexture(texture);
    throw new Error("Liquid-glass framebuffer is incomplete");
  }
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return { framebuffer, texture, width, height, internalFormat, pixelType };
};
var resizeRenderTarget = (gl, target, width, height) => {
  if (target.width === width && target.height === height) return;
  target.width = width;
  target.height = height;
  gl.bindTexture(gl.TEXTURE_2D, target.texture);
  gl.texImage2D(
    gl.TEXTURE_2D,
    0,
    target.internalFormat,
    width,
    height,
    0,
    gl.RGBA,
    target.pixelType,
    null
  );
  gl.bindTexture(gl.TEXTURE_2D, null);
};
var uniform = (gl, program, name) => gl.getUniformLocation(program, name);
var LiquidGlassRenderer = class _LiquidGlassRenderer {
  gl;
  backgroundProgram;
  blurProgram;
  mainProgram;
  vao;
  vertexBuffer;
  mediaTexture;
  sceneTexture;
  targets;
  blurWeights = computeGaussianKernelByRadius(LIQUID_GLASS_STUDIO_PRESET.blurRadius);
  lastUploadedMedia = null;
  lastUploadedCanvasVersion;
  disposed = false;
  static create(canvas) {
    const gl = canvas.getContext("webgl2", {
      alpha: true,
      antialias: true,
      depth: false,
      premultipliedAlpha: false,
      powerPreference: "high-performance",
      preserveDrawingBuffer: true
    });
    if (!gl) return null;
    try {
      return new _LiquidGlassRenderer(gl);
    } catch (error) {
      if (false) {
        console.warn("[LumenicLiquidGlass] renderer initialization failed", error);
      }
      return null;
    }
  }
  constructor(gl) {
    this.gl = gl;
    this.backgroundProgram = createProgram(gl, liquidGlassBackgroundShader);
    this.blurProgram = createProgram(gl, liquidGlassBlurShader);
    this.mainProgram = createProgram(gl, liquidGlassMainShader);
    const vao = gl.createVertexArray();
    const vertexBuffer = gl.createBuffer();
    const mediaTexture = gl.createTexture();
    const sceneTexture = gl.createTexture();
    if (!vao || !vertexBuffer || !mediaTexture || !sceneTexture) {
      throw new Error("Unable to allocate liquid-glass geometry");
    }
    this.vao = vao;
    this.vertexBuffer = vertexBuffer;
    this.mediaTexture = mediaTexture;
    this.sceneTexture = sceneTexture;
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);
    gl.bufferData(
      gl.ARRAY_BUFFER,
      new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]),
      gl.STATIC_DRAW
    );
    const location = gl.getAttribLocation(this.backgroundProgram, "a_position");
    gl.enableVertexAttribArray(location);
    gl.vertexAttribPointer(location, 2, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    gl.bindTexture(gl.TEXTURE_2D, mediaTexture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      1,
      1,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      new Uint8Array([0, 0, 0, 255])
    );
    gl.bindTexture(gl.TEXTURE_2D, sceneTexture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      1,
      1,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      new Uint8Array([0, 0, 0, 0])
    );
    gl.bindTexture(gl.TEXTURE_2D, null);
    const highPrecisionTargets = Boolean(gl.getExtension("EXT_color_buffer_float"));
    this.targets = [
      createRenderTarget(gl, 1, 1, highPrecisionTargets),
      createRenderTarget(gl, 1, 1, highPrecisionTargets),
      createRenderTarget(gl, 1, 1, highPrecisionTargets)
    ];
  }
  resizeForFrame(frame, targetSize) {
    if (this.disposed) return;
    const dpr = frame.dpr;
    const pixelWidth = Math.max(1, Math.round(frame.width * dpr));
    const pixelHeight = Math.max(1, Math.round(frame.height * dpr));
    const canvas = this.gl.canvas;
    if (canvas.width !== pixelWidth) canvas.width = pixelWidth;
    if (canvas.height !== pixelHeight) canvas.height = pixelHeight;
    for (const target of this.targets) {
      resizeRenderTarget(this.gl, target, targetSize.width, targetSize.height);
    }
  }
  uploadMedia(media) {
    if (!media) return false;
    const isVideo = typeof HTMLVideoElement !== "undefined" && media instanceof HTMLVideoElement;
    const hasRetainedFrame = media === this.lastUploadedMedia;
    const isCanvas = typeof HTMLCanvasElement !== "undefined" && media instanceof HTMLCanvasElement;
    if (!isVideo && !isCanvas && hasRetainedFrame) return true;
    const canvasVersion = isCanvas ? media.dataset.glassFrameVersion : void 0;
    if (isCanvas && hasRetainedFrame && canvasVersion !== void 0 && canvasVersion === this.lastUploadedCanvasVersion) return true;
    if (isVideo && media.readyState < media.HAVE_CURRENT_DATA) return hasRetainedFrame;
    const gl = this.gl;
    try {
      gl.bindTexture(gl.TEXTURE_2D, this.mediaTexture);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, media);
      gl.bindTexture(gl.TEXTURE_2D, null);
      this.lastUploadedMedia = media;
      this.lastUploadedCanvasVersion = canvasVersion;
      return true;
    } catch {
      gl.bindTexture(gl.TEXTURE_2D, null);
      return hasRetainedFrame;
    }
  }
  uploadScene(scene) {
    if (!scene || scene.width < 1 || scene.height < 1) return false;
    const gl = this.gl;
    try {
      gl.bindTexture(gl.TEXTURE_2D, this.sceneTexture);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, scene);
      gl.bindTexture(gl.TEXTURE_2D, null);
      return true;
    } catch {
      gl.bindTexture(gl.TEXTURE_2D, null);
      return false;
    }
  }
  bindTarget(target) {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, target?.framebuffer ?? null);
    const width = target?.width ?? gl.drawingBufferWidth;
    const height = target?.height ?? gl.drawingBufferHeight;
    gl.viewport(0, 0, width, height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
  }
  bindTexture(program, name, texture, unit) {
    const gl = this.gl;
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.uniform1i(uniform(gl, program, name), unit);
  }
  draw(program) {
    const gl = this.gl;
    gl.useProgram(program);
    gl.bindVertexArray(this.vao);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    gl.bindVertexArray(null);
  }
  render(frame) {
    if (this.disposed) return;
    const gl = this.gl;
    const referenceFrame = resolveLiquidGlassReferenceFrame({
      viewportWidth: frame.viewportWidth,
      viewportHeight: frame.viewportHeight,
      surfaceX: frame.viewportX,
      surfaceY: frame.viewportY,
      surfaceWidth: frame.width,
      surfaceHeight: frame.height,
      devicePixelRatio: frame.dpr,
      overscanCssPx: BACKDROP_OVERSCAN_CSS_PX
    });
    const targetScale = resolveLiquidGlassTargetScale(frame.renderTargetScale);
    const region = resolveLiquidGlassTextureRegion(
      referenceFrame,
      frame.width,
      frame.height,
      frame.dpr,
      targetScale,
      BACKDROP_OVERSCAN_CSS_PX
    );
    this.resizeForFrame(frame, region);
    const uploaded = this.uploadMedia(frame.media);
    const uploadedScene = this.uploadScene(frame.scene);
    const shapeRadiusFactor = resolveLiquidGlassShapeRadiusFactor(
      frame.height,
      frame.cornerRadiusPx,
      LIQUID_GLASS_STUDIO_PRESET.shapeRadius
    );
    this.bindTarget(this.targets[0]);
    gl.useProgram(this.backgroundProgram);
    this.bindTexture(this.backgroundProgram, "u_background", this.mediaTexture, 0);
    this.bindTexture(this.backgroundProgram, "u_scene", this.sceneTexture, 1);
    gl.uniform1i(uniform(gl, this.backgroundProgram, "u_hasBackground"), uploaded ? 1 : 0);
    gl.uniform1i(uniform(gl, this.backgroundProgram, "u_hasScene"), uploadedScene ? 1 : 0);
    const sceneBounds = frame.sceneBounds ?? { left: 0, top: 0, right: frame.viewportWidth, bottom: frame.viewportHeight };
    gl.uniform4f(
      uniform(gl, this.backgroundProgram, "u_sceneBounds"),
      sceneBounds.left,
      sceneBounds.top,
      Math.max(1, sceneBounds.right - sceneBounds.left),
      Math.max(1, sceneBounds.bottom - sceneBounds.top)
    );
    gl.uniform1f(uniform(gl, this.backgroundProgram, "u_mediaAspect"), frame.mediaRatio || 1);
    gl.uniform2f(
      uniform(gl, this.backgroundProgram, "u_viewportSize"),
      frame.viewportWidth,
      frame.viewportHeight
    );
    gl.uniform2f(
      uniform(gl, this.backgroundProgram, "u_surfaceOffset"),
      region.offsetCss.x,
      region.offsetCss.y
    );
    gl.uniform2f(
      uniform(gl, this.backgroundProgram, "u_surfaceSize"),
      region.sizeCss.width,
      region.sizeCss.height
    );
    gl.uniform3f(uniform(gl, this.backgroundProgram, "u_voidColor"), 5e-3, 5e-3, 6e-3);
    gl.uniform2f(
      uniform(gl, this.backgroundProgram, "u_capsuleSize"),
      frame.width,
      frame.height
    );
    gl.uniform1f(
      uniform(gl, this.backgroundProgram, "u_surfacePadding"),
      BACKDROP_OVERSCAN_CSS_PX
    );
    gl.uniform2f(
      uniform(gl, this.backgroundProgram, "u_capsuleOffset"),
      referenceFrame.surfaceCenterCss.x - region.offsetCss.x - frame.width / 2,
      referenceFrame.surfaceCenterCss.y - region.offsetCss.y - frame.height / 2
    );
    gl.uniform1f(
      uniform(gl, this.backgroundProgram, "u_shapeRadius"),
      shapeRadiusFactor
    );
    gl.uniform1f(
      uniform(gl, this.backgroundProgram, "u_shapeRoundness"),
      LIQUID_GLASS_STUDIO_PRESET.shapeRoundness
    );
    gl.uniform1f(
      uniform(gl, this.backgroundProgram, "u_shadowExpand"),
      LIQUID_GLASS_STUDIO_PRESET.shadowExpand
    );
    gl.uniform1f(
      uniform(gl, this.backgroundProgram, "u_shadowFactor"),
      LIQUID_GLASS_STUDIO_PRESET.shadowFactor
    );
    gl.uniform2f(
      uniform(gl, this.backgroundProgram, "u_shadowPosition"),
      -LIQUID_GLASS_STUDIO_PRESET.shadowPosition[0],
      -LIQUID_GLASS_STUDIO_PRESET.shadowPosition[1]
    );
    this.draw(this.backgroundProgram);
    this.bindTarget(this.targets[1]);
    gl.useProgram(this.blurProgram);
    this.bindTexture(this.blurProgram, "u_source", this.targets[0].texture, 0);
    gl.uniform2f(
      uniform(gl, this.blurProgram, "u_blurDirection"),
      1 / this.targets[0].width,
      0
    );
    gl.uniform1i(
      uniform(gl, this.blurProgram, "u_blurRadius"),
      LIQUID_GLASS_STUDIO_PRESET.blurRadius
    );
    gl.uniform1fv(uniform(gl, this.blurProgram, "u_blurWeights[0]"), this.blurWeights);
    this.draw(this.blurProgram);
    this.bindTarget(this.targets[2]);
    gl.useProgram(this.blurProgram);
    this.bindTexture(this.blurProgram, "u_source", this.targets[1].texture, 0);
    gl.uniform2f(
      uniform(gl, this.blurProgram, "u_blurDirection"),
      0,
      1 / this.targets[1].height
    );
    gl.uniform1i(
      uniform(gl, this.blurProgram, "u_blurRadius"),
      LIQUID_GLASS_STUDIO_PRESET.blurRadius
    );
    gl.uniform1fv(uniform(gl, this.blurProgram, "u_blurWeights[0]"), this.blurWeights);
    this.draw(this.blurProgram);
    this.bindTarget(null);
    gl.disable(gl.BLEND);
    gl.useProgram(this.mainProgram);
    this.bindTexture(this.mainProgram, "u_background", this.targets[0].texture, 0);
    this.bindTexture(this.mainProgram, "u_blurredBackground", this.targets[2].texture, 1);
    gl.uniform2f(
      uniform(gl, this.mainProgram, "u_resolution"),
      gl.drawingBufferWidth,
      gl.drawingBufferHeight
    );
    const preset = LIQUID_GLASS_STUDIO_PRESET;
    gl.uniform1f(uniform(gl, this.mainProgram, "u_dpr"), frame.dpr);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_refThickness"), preset.refThickness);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_refFactor"), preset.refFactor);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_refDispersion"), preset.refDispersion);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_refFresnelRange"), preset.refFresnelRange);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_refFresnelHardness"), preset.refFresnelHardness);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_refFresnelFactor"), preset.refFresnelFactor);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_glareRange"), preset.glareRange);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_glareHardness"), preset.glareHardness);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_glareFactor"), preset.glareFactor);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_glareConvergence"), preset.glareConvergence);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_glareOppositeFactor"), preset.glareOppositeFactor);
    gl.uniform1f(
      uniform(gl, this.mainProgram, "u_glareAngle"),
      preset.glareAngle * Math.PI / 180
    );
    gl.uniform1i(uniform(gl, this.mainProgram, "u_blurEdge"), preset.blurEdge ? 1 : 0);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_shapeRadius"), shapeRadiusFactor);
    gl.uniform1f(uniform(gl, this.mainProgram, "u_shapeRoundness"), preset.shapeRoundness);
    const opticalCanvasSize = referenceFrame.opticalSizeCss * frame.dpr;
    gl.uniform2f(
      uniform(gl, this.mainProgram, "u_opticalViewportSize"),
      opticalCanvasSize,
      opticalCanvasSize
    );
    gl.uniform2f(
      uniform(gl, this.mainProgram, "u_backdropScale"),
      gl.drawingBufferWidth * targetScale / this.targets[0].width,
      gl.drawingBufferHeight * targetScale / this.targets[0].height
    );
    gl.uniform2f(
      uniform(gl, this.mainProgram, "u_refractionUvScale"),
      opticalCanvasSize * targetScale / this.targets[0].width,
      opticalCanvasSize * targetScale / this.targets[0].height
    );
    gl.uniform4f(
      uniform(gl, this.mainProgram, "u_tint"),
      preset.tint[0],
      preset.tint[1],
      preset.tint[2],
      preset.tint[3]
    );
    this.draw(this.mainProgram);
  }
  dispose() {
    if (this.disposed) return;
    this.disposed = true;
    const gl = this.gl;
    for (const target of this.targets) {
      gl.deleteFramebuffer(target.framebuffer);
      gl.deleteTexture(target.texture);
    }
    gl.deleteTexture(this.mediaTexture);
    gl.deleteTexture(this.sceneTexture);
    gl.deleteBuffer(this.vertexBuffer);
    gl.deleteVertexArray(this.vao);
    gl.deleteProgram(this.backgroundProgram);
    gl.deleteProgram(this.blurProgram);
    gl.deleteProgram(this.mainProgram);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.bindTexture(gl.TEXTURE_2D, null);
  }
};

// material:sceneOverflow.ts
var getSceneOverflowBounds = (element) => {
  const document2 = element.ownerDocument;
  const window = document2.defaultView;
  const root = document2.documentElement;
  const rootStyle = root && window.getComputedStyle(root);
  if (element === root || element === document2.body && rootStyle?.overflowX === "visible" && rootStyle.overflowY === "visible") {
    return { left: 0, top: 0, right: window.innerWidth, bottom: window.innerHeight };
  }
  return element.getBoundingClientRect();
};
var establishesFixedContainer = (style) => ["transform", "perspective", "filter", "backdropFilter"].some((key) => {
  const value = style[key];
  return value && value !== "none";
}) || /transform|perspective|filter/.test(style.willChange) || /paint|layout|strict|content/.test(style.contain);
var getSceneLeafClip = (element, bounds) => {
  const document2 = element.ownerDocument;
  const window = document2.defaultView;
  const clip = { left: bounds.left, top: bounds.top, right: bounds.right, bottom: bounds.bottom };
  let position = window.getComputedStyle(element).position;
  let seekingContainer = position === "fixed" || position === "absolute";
  for (let node = element; node; node = node.parentElement) {
    const style = window.getComputedStyle(node);
    if (node !== element && seekingContainer) {
      const viewport = node === document2.documentElement || node === document2.body;
      if (!viewport && !establishesFixedContainer(style) && !(position === "absolute" && style.position && style.position !== "static")) continue;
      seekingContainer = false;
    }
    const clipsX = /^(hidden|clip|scroll|auto)$/.test(style.overflowX);
    const clipsY = /^(hidden|clip|scroll|auto)$/.test(style.overflowY);
    if (clipsX || clipsY) {
      const rect = getSceneOverflowBounds(node);
      if (clipsX) {
        clip.left = Math.max(clip.left, rect.left);
        clip.right = Math.min(clip.right, rect.right);
      }
      if (clipsY) {
        clip.top = Math.max(clip.top, rect.top);
        clip.bottom = Math.min(clip.bottom, rect.bottom);
      }
    }
    if (node !== element && (style.position === "fixed" || style.position === "absolute")) {
      position = style.position;
      seekingContainer = true;
    }
  }
  return clip;
};

// material:LiquidGlassSceneComposition.ts
var styleOf = (element) => element.ownerDocument.defaultView.getComputedStyle(element);
var opacityOf = (style) => {
  const value = Number.parseFloat(style.opacity);
  return Number.isFinite(value) ? Math.max(0, Math.min(1, value)) : 1;
};
var blendOf = (style) => style.mixBlendMode && style.mixBlendMode !== "normal" ? style.mixBlendMode : "source-over";
var hasZIndex = (element, style) => style.position && style.position !== "static" || element.parentElement && /flex|grid/.test(styleOf(element.parentElement).display);
var stackingContext = (element, style) => !element.parentElement || opacityOf(style) < 1 || blendOf(style) !== "source-over" || style.isolation === "isolate" || style.position === "fixed" || style.position === "sticky" || hasZIndex(element, style) && style.zIndex && style.zIndex !== "auto" || ["transform", "perspective", "filter", "backdropFilter", "clipPath", "maskImage"].some((key) => {
  const value = style[key];
  return value && value !== "none";
}) || /transform|opacity|filter/.test(style.willChange) || /paint|layout|strict|content/.test(style.contain);
var LiquidGlassSceneComposition = class {
  scratch = [];
  paint(context, leaves, bounds, backdrop) {
    const nodes = /* @__PURE__ */ new Map();
    const roots = [];
    const selected = new Set(leaves.map((leaf) => leaf.element));
    const ensureNode = (element) => {
      const found = nodes.get(element);
      if (found) return found;
      const node = { element, children: [], style: styleOf(element) };
      nodes.set(element, node);
      let parent = element.parentElement;
      while (parent && !selected.has(parent) && !stackingContext(parent, styleOf(parent))) parent = parent.parentElement;
      if (parent) ensureNode(parent).children.push(node);
      else roots.push(node);
      return node;
    };
    for (const leaf of leaves) ensureNode(leaf.element).paint = leaf.paint;
    if ([...nodes.values()].some((node) => blendOf(node.style) !== "source-over")) backdrop?.();
    const width = context.canvas.width;
    const height = context.canvas.height;
    const transform = context.getTransform();
    let scratchUsed = 0;
    let hasContent = false;
    const zIndex = (node) => hasZIndex(node.element, node.style) ? Number.parseInt(node.style.zIndex, 10) || 0 : 0;
    const paintNode = (node, target, inheritedClip, depth) => {
      const { style, element } = node;
      const opacity = opacityOf(style);
      if (style.display === "none" || style.visibility === "hidden" || opacity <= 1e-3) return;
      for (let parent = element.parentElement; parent; parent = parent.parentElement) {
        const parentStyle = styleOf(parent);
        if (parentStyle.display === "none" || parentStyle.visibility === "hidden") return;
      }
      const blend = blendOf(style);
      const isolated = node.children.length > 0 && (opacity < 1 || blend !== "source-over" || style.isolation === "isolate");
      let output = target;
      if (isolated) {
        const canvas = this.scratch[depth] ?? (this.scratch[depth] = document.createElement("canvas"));
        if (canvas.width !== width || canvas.height !== height) {
          canvas.width = width;
          canvas.height = height;
        }
        const scratchContext = canvas.getContext("2d");
        if (!scratchContext) return;
        scratchContext.setTransform(1, 0, 0, 1, 0, 0);
        scratchContext.clearRect(0, 0, width, height);
        scratchContext.setTransform(transform);
        output = scratchContext;
        scratchUsed = Math.max(scratchUsed, depth + 1);
      }
      output.save();
      output.globalAlpha = isolated ? 1 : opacity;
      output.globalCompositeOperation = isolated ? "source-over" : blend;
      if (node.paint) {
        const clip = getSceneLeafClip(element, inheritedClip);
        if (clip.right > clip.left && clip.bottom > clip.top) {
          output.save();
          output.beginPath();
          output.rect(clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top);
          output.clip();
          hasContent = node.paint(output) || hasContent;
          output.restore();
        }
      }
      node.children.sort((a, b) => zIndex(a) - zIndex(b) || (a.element.compareDocumentPosition(b.element) & Node.DOCUMENT_POSITION_PRECEDING ? 1 : -1));
      for (const child of node.children) paintNode(child, output, inheritedClip, depth + Number(isolated));
      output.restore();
      if (isolated) {
        target.save();
        target.setTransform(1, 0, 0, 1, 0, 0);
        target.globalAlpha = opacity;
        target.globalCompositeOperation = blend;
        target.drawImage(output.canvas, 0, 0);
        target.restore();
      }
    };
    for (const root of roots) paintNode(root, context, bounds, 0);
    this.scratch.length = scratchUsed;
    return hasContent;
  }
};

// material:text.ts
var resolveTextAlign = (value) => {
  if (value === "center" || value === "right") return value;
  if (value === "end") return "right";
  return "left";
};
var drawText = (context, element, rect) => {
  const style = element.ownerDocument.defaultView.getComputedStyle(element);
  const text = Array.from(element.childNodes).filter((node) => node.nodeType === Node.TEXT_NODE).map((node) => node.textContent?.trim() ?? "").filter(Boolean).join(" ");
  if (!text || style.visibility === "hidden" || style.display === "none") return false;
  const visible = getSceneLeafClip(element, rect);
  if (visible.right <= visible.left || visible.bottom <= visible.top) return false;
  const fontSize = Number.parseFloat(style.fontSize);
  if (!Number.isFinite(fontSize) || fontSize <= 0) return false;
  const lineHeight = Number.parseFloat(style.lineHeight) || fontSize * 1.2;
  const textAlign = resolveTextAlign(style.textAlign);
  const textX = textAlign === "center" ? rect.left + rect.width / 2 : textAlign === "right" ? rect.right : rect.left;
  context.save();
  context.beginPath();
  context.rect(visible.left, visible.top, visible.right - visible.left, visible.bottom - visible.top);
  context.clip();
  context.fillStyle = style.color;
  context.font = `${style.fontStyle} ${style.fontWeight} ${style.fontSize}/${style.lineHeight} ${style.fontFamily}`;
  context.textAlign = textAlign;
  context.textBaseline = "middle";
  context.direction = style.direction;
  context.letterSpacing = style.letterSpacing;
  context.shadowColor = "transparent";
  context.fillText(text, textX, rect.top + Math.min(rect.height, lineHeight) / 2, rect.width);
  context.restore();
  return true;
};
export {
  LIQUID_GLASS_STUDIO_PRESET,
  LiquidGlassRenderer,
  LiquidGlassSceneComposition,
  drawText,
  getSceneLeafClip,
  resolveLiquidGlassBackdrop,
  resolveLiquidGlassOpticalCanvasSize,
  resolveLiquidGlassReferenceFrame,
  resolveLiquidGlassReferenceTargetSize,
  resolveLiquidGlassRenderScale,
  resolveLiquidGlassShapeRadiusFactor,
  resolveLiquidGlassTargetScale,
  resolveLiquidGlassTextureRegion,
  shouldContinuouslyRenderLiquidGlass
};
