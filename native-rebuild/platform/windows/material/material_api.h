#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
inline constexpr UINT LumeMaterialStatusChangedMessage=WM_APP+0x5a0;

// Versioned C ABI: no CRT objects, allocation ownership, or C++ types cross DLLs.
// Calls on a handle must be serialized by its window's UI thread.
struct LumeMaterialPanel { float x,y,width,height,radius; };
// One sampled state of a V7 timeline; fields as in LumeMaterialParameters (DIP).
struct LumeMaterialKeyframe {
    float lensX,lensY,lensWidth,lensHeight;
    float contentX,contentY,contentWidth,contentHeight;
    float opacity,cornerRadius;
    uint32_t panelCount;
    LumeMaterialPanel panels[8];
    // Opacity of the content layer on its own (net, not on top of `opacity`),
    // so content can fade in after the glass and out before it.
    float contentOpacity;
};
struct LumeMaterialParameters {
    uint32_t size;
    HWND owner;
    // 0: liquid widget, 1: frosted widget, 2: original frosted panel overlay.
    uint32_t frosted,opticalProfile,lightForeground;
    uint32_t pressed,reducedMotion;
    float contactX,contactY;
    uint32_t panelCount;
    LumeMaterialPanel panels[8];
    float cornerRadius; // DIP; zero retains the original material radius.
    HWND sceneAnchor; // Optional source-stack anchor; null samples below owner.
    uint32_t bottomInset; // DIP below the glass body; foreground HWND is unchanged.
    // Double alignment keeps this extension after the complete V4 ABI, including padding.
    double innerRadius; // DIP; zero preserves rectangular widgets, positive renders an annulus.
    uint32_t opacityEnabled; // Opt in; zero retains the fully opaque material used by existing widgets.
    float opacity; // 0..1, following the owner's foreground animation progress.
    // V7 (optional): an explicit lens rectangle (DIP from the owner's client
    // origin; it may extend past the window) instead of the client area, and a
    // premultiplied BGRA content layer composed over the glass in the same
    // frame (DIP from the lens origin). An owner animating both glass and
    // content hands them to one presenter so they cannot reach the screen on
    // different frames. Pixels are copied during the call when the generation
    // changes; the pointer need not outlive it.
    // lensOverride 2 (with a timeline): the timeline is relative to the owner's
    // client origin at the call that hands it over, and stays put while the owner
    // window moves or resizes during the motion.
    uint32_t lensOverride;
    float lensX,lensY,lensWidth,lensHeight;
    uint32_t contentEnabled;
    float contentX,contentY,contentWidth,contentHeight;
    const uint32_t* contentPixels;
    uint32_t contentPixelWidth,contentPixelHeight,contentGeneration;
    // V7 timeline (optional, with lensOverride and contentEnabled): the owner's
    // whole motion sampled every timelineStepMs from timelineStart (a
    // QueryPerformanceCounter value). The glass evaluates it for the moment
    // each frame reaches the screen, so the motion follows its curve exactly
    // however late the owner's thread runs. Frames are copied during the call
    // when timelineStart changes; the last frame holds after the end.
    const LumeMaterialKeyframe* timeline;
    uint32_t timelineCount;
    float timelineStepMs;
    int64_t timelineStart;
};
inline constexpr uint32_t LumeMaterialParametersV1Size=offsetof(LumeMaterialParameters,cornerRadius);
inline constexpr uint32_t LumeMaterialParametersV2Size=offsetof(LumeMaterialParameters,sceneAnchor);
inline constexpr uint32_t LumeMaterialParametersV3Size=offsetof(LumeMaterialParameters,bottomInset);
inline constexpr uint32_t LumeMaterialParametersV4Size=offsetof(LumeMaterialParameters,innerRadius);
inline constexpr uint32_t LumeMaterialParametersV5Size=offsetof(LumeMaterialParameters,opacityEnabled);
inline constexpr uint32_t LumeMaterialParametersV6Size=offsetof(LumeMaterialParameters,lensOverride);
struct LumeMaterialStatus {
    uint32_t size,initialized,frameReady,failed,frames;
    int32_t lastError;
    uint32_t sources,readySources,scenePixels,surfacePixels;
};
struct LumeMaterialStatisticsData { uint32_t size,users,devices,captures; };
using LumeMaterialHandle=uint64_t;
using LumeMaterialAttachFn=LumeMaterialHandle (__cdecl*)(const LumeMaterialParameters*);
using LumeMaterialUpdateFn=int (__cdecl*)(LumeMaterialHandle,const LumeMaterialParameters*);
using LumeMaterialDetachFn=void (__cdecl*)(LumeMaterialHandle);
using LumeMaterialInspectFn=int (__cdecl*)(LumeMaterialHandle,LumeMaterialStatus*);
using LumeMaterialStatisticsFn=int (__cdecl*)(LumeMaterialStatisticsData*);
