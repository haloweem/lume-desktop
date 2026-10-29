#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
namespace lume::platform::windows::widget_snap {
// Cross-widget edge highlighting only. No calendar/clock model dependency.
inline constexpr UINT Message=WM_APP+0x6a4;
inline constexpr UINT CancelMessage=Message+1;
inline constexpr wchar_t Capability[]=L"LumeWidgetSnapProtocol";
inline constexpr wchar_t ActiveEdges[]=L"LumeWidgetSnapEdges";
inline constexpr unsigned Left=1,Right=2,Top=4,Bottom=8,Linger=16;
inline bool Compatible(HWND window) {return GetPropW(window,Capability)!=nullptr;}
inline void BeginInteraction(HWND owner) {
    // Guide ownership belongs to one current drag/resize. Cancel the previous
    // relationship before the new source publishes any edges, including when
    // it was the previous target during the 260ms linger.
    std::vector<HWND> windows;
    EnumWindows([](HWND window,LPARAM data)->BOOL {
        DWORD process=0;GetWindowThreadProcessId(window,&process);
        if(process==GetCurrentProcessId()&&Compatible(window))
            reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
        return TRUE;
    },reinterpret_cast<LPARAM>(&windows));
    for(const auto window:windows) {
        if(GetWindowThreadProcessId(window,nullptr)==GetCurrentThreadId()) {
            // Old posted edges on this UI thread must not resurrect the
            // relationship after its synchronous cancellation.
            MSG pending{};
            while(PeekMessageW(&pending,window,Message,Message,PM_REMOVE)){}
            SendMessageW(window,CancelMessage,0,reinterpret_cast<LPARAM>(owner));
        } else {
            // Never block another UI thread. Its queued cancellation precedes
            // the new source's subsequent edge notifications.
            PostMessageW(window,CancelMessage,0,reinterpret_cast<LPARAM>(owner));
        }
    }
}
inline void Notify(HWND target,HWND source,unsigned edges,bool linger=false) {
    if(target&&IsWindow(target)&&Compatible(target))PostMessageW(target,Message,edges|(linger?Linger:0),reinterpret_cast<LPARAM>(source));
}
inline void SetEdges(HWND window,unsigned edges) {
    if(edges)SetPropW(window,ActiveEdges,reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(edges)));
    else RemovePropW(window,ActiveEdges);
}
inline void DrawBitmapEdges(std::vector<std::uint32_t>& pixels,unsigned width,unsigned height,unsigned edges,unsigned thickness) {
    if(pixels.size()!=static_cast<std::size_t>(width)*height)return;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
        if(((edges&Left)&&x<thickness)||((edges&Right)&&x+thickness>=width)||((edges&Top)&&y<thickness)||((edges&Bottom)&&y+thickness>=height))
            pixels[static_cast<std::size_t>(y)*width+x]=0xe60977e6u; // Premultiplied blue guide.
}
}
