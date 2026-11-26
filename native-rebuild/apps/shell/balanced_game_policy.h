#pragma once
#include "platform/windows/widget_ui/window_layer_policy.h"
#include <filesystem>
#include <string>

namespace lume::shell {
// UI-thread transient state. Only query the executable when the foreground
// identity changes. There is no scan of installed or running applications.
class BalancedGamePolicy {
public:
    void Configure(bool enabled,const std::vector<std::string>& paths) {
        balancedEnabled=enabled;
        gamePrograms.clear();
        for(const auto& path:paths) gamePrograms.push_back(std::filesystem::u8path(path).wstring());
        gameForeground=nullptr;
    }
    bool Observe(const platform::windows::window_layer::Context& state,ULONGLONG now) {
        inFront=balancedEnabled && state.foreignForeground &&
            (state.exclusive || !state.fullscreen.empty() || SelectedGame(state.foreground));
        if(inFront) resumeDesktopAt=now+2000;
        idle=balancedEnabled && (inFront || (idle && now<resumeDesktopAt));
        return idle;
    }
    bool InFront() const {return inFront;}
private:
    bool SelectedGame(HWND foreground) {
        if(gamePrograms.empty() || !foreground)return false;
        DWORD pid=0;GetWindowThreadProcessId(foreground,&pid);
        if(foreground==gameForeground && pid==gamePid)return selectedGame;
        gameForeground=foreground;gamePid=pid;selectedGame=false;
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(process) {
            wchar_t path[32768];DWORD size=32768;
            if(QueryFullProcessImageNameW(process,0,path,&size))
                selectedGame=std::any_of(gamePrograms.begin(),gamePrograms.end(),[&](const auto& game){
                    return CompareStringOrdinal(game.c_str(),-1,path,static_cast<int>(size),TRUE)==CSTR_EQUAL;
                });
            CloseHandle(process);
        }
        return selectedGame;
    }
    bool balancedEnabled=true,idle=false,inFront=false,selectedGame=false;
    HWND gameForeground=nullptr;
    DWORD gamePid=0;
    ULONGLONG resumeDesktopAt=0;
    std::vector<std::wstring> gamePrograms;
};
}
