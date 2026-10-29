// Adapted from iyinchao/liquid-glass-studio at
// d13c3e53813ebc1d8b52d878071b63212a550ebc (MIT).

export const liquidGlassVertexShader = `#version 300 es
layout(location = 0) in vec2 a_position;
out vec2 v_uv;

void main() {
    v_uv = (a_position + 1.0) * 0.5;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
`;

export const liquidGlassBackgroundShader = `#version 300 es
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

export const liquidGlassBlurShader = `#version 300 es
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

export const liquidGlassMainShader = `#version 300 es
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
