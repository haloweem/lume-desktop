#include "platform/windows/minimize_animation/minimize_animation_ipc.h"
#include <filesystem>
using namespace lume::platform::windows::minimize_detail;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,ObjectName(NativeBits,L"Control").c_str());
    if (!mapping) return 1;
    auto* state=static_cast<SharedControl*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControl)));
    if (!state || state->version!=ProtocolVersion || state->size!=sizeof(SharedControl)) {
        if (state) UnmapViewOfFile(state);
        CloseHandle(mapping);
        return 2;
    }
    HANDLE stop=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,ObjectName(NativeBits,L"Stop").c_str());
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,ObjectName(NativeBits,L"Ready").c_str());
    HANDLE owner=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,state->ownerPid);
    HMODULE hookModule=nullptr; HHOOK cbt=nullptr,shown=nullptr;
    FILETIME created{},exit{},kernel{},user{};
    const bool ownerValid=owner && GetProcessTimes(owner,&created,&exit,&kernel,&user) &&
        static_cast<LONG>(created.dwLowDateTime)==state->ownerTimeLow &&
        static_cast<LONG>(created.dwHighDateTime)==state->ownerTimeHigh;
    DWORD error=ERROR_INVALID_HANDLE;
    InterlockedExchange(&state->hostPid,static_cast<LONG>(GetCurrentProcessId()));
    if (stop && ready && ownerValid) {
        std::wstring path(32768,L'\0');
        const auto size=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        path.resize(size);
        path=(std::filesystem::path(path).parent_path()/
             (L"LumeMinimizeHook"+std::to_wstring(NativeBits)+L".dll")).wstring();
        hookModule=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (hookModule) {
            const auto cbtProc=Procedure<HOOKPROC>(hookModule,"LumeMinimizeCbt");
            const auto shownProc=Procedure<HOOKPROC>(hookModule,"LumeMinimizeShown");
            if (cbtProc && shownProc) {
                cbt=SetWindowsHookExW(WH_CBT,cbtProc,hookModule,state->targetThread);
                if (cbt) shown=SetWindowsHookExW(WH_CALLWNDPROCRET,shownProc,hookModule,state->targetThread);
                error=cbt && shown ? ERROR_SUCCESS : GetLastError();
            } else error=ERROR_PROC_NOT_FOUND;
        } else error=GetLastError();
    }
    InterlockedExchange(&state->installedHooks,(cbt?1:0)+(shown?1:0));
    InterlockedExchange(&state->startupError,static_cast<LONG>(error));
    if (ready) SetEvent(ready);
    if (!error) {
        HANDLE waits[]={stop,owner}; bool quit=false;
        while (!quit) {
            const DWORD result=MsgWaitForMultipleObjects(2,waits,FALSE,INFINITE,QS_ALLINPUT);
            if (result!=WAIT_OBJECT_0+2) break;
            MSG msg{};
            while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
                if (msg.message==WM_QUIT) { quit=true; break; }
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
        }
    }
    InterlockedExchange(&state->enabled,FALSE);
    if (stop) SetEvent(stop);
    if (shown) UnhookWindowsHookEx(shown);
    if (cbt) UnhookWindowsHookEx(cbt);
    InterlockedExchange(&state->installedHooks,0);
    // Target workers pin their own DLL and hold the stop/owner/Host handles.
    // This process can leave after a bounded drain without unloading code in a
    // target or pretending its DWM restoration has completed. The runtime keeps
    // those leases and IPC alive until the target actually releases them.
    const auto drainDeadline=GetTickCount64()+HostDrainTimeoutMs;
    while (ActiveWorkers(*state) || ActiveEntries(*state)) {
        ReapExitedTargets(*state);
        if (GetTickCount64()>=drainDeadline) break;
        MSG msg{};
        while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        Sleep(10);
    }
    const bool outstanding=ActiveWorkers(*state) || ActiveEntries(*state);
    if (hookModule) FreeLibrary(hookModule);
    for (HANDLE h:{stop,ready,owner}) if (h) CloseHandle(h);
    UnmapViewOfFile(state); CloseHandle(mapping);
    return error ? 3 : outstanding ? 4 : 0;
}
