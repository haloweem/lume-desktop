#include "apps/shell/todo_session.h"
#include "core/todo/todo_time.h"
#include "unique_test_directory.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace lume;
void Check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(){try{
    const auto root=UniqueTestDirectory(std::filesystem::u8path(TODO_TEST_ARTIFACT_ROOT));
    const auto path=(root/"calendar-preferences.json").u8string();
    shell::TodoSession unavailable("");Check(!unavailable.Read().writable,"empty path never creates relative storage");
    const auto blockedRoot=root/"blocked";std::filesystem::create_directories(blockedRoot);
    {std::ofstream blocker(blockedRoot/"todo-data");blocker<<"isolated directory creation failure";}
    shell::TodoSession blocked((blockedRoot/"calendar.json").u8string());
    Check(!blocked.Read().writable&&!blocked.BaseDirectory().empty(),"failed directory creation retains recovery path");
    std::filesystem::remove(blockedRoot/"todo-data");blocked.RefreshTime();
    Check(blocked.Read().writable,"temporary storage failure recovers without process restart");
    shell::TodoSession session(path);auto snapshot=session.Read();Check(snapshot.writable&&snapshot.scope.accountKey=="local","local session ready");
    const auto now=platform::windows::WindowsLocalTime::NowMillis();auto day=session.Time().ToLocal(now);
    todo::Command add;add.scope=snapshot.scope;add.patch.title=u8"中文日程😀";add.patch.notes=u8"第一行\n第二行";
    add.patch.schedule=todo::DefaultSchedule(day->date,now,session.Time());
    auto saved=session.Dispatch(add);Check(saved.ok&&saved.committed&&saved.snapshot.tasks.size()==1,"session commits real local task");
    Check(saved.snapshot.outboxCount==0,"local-only session does not enqueue unused cloud operations");
    shell::TodoSession reload(path);Check(reload.Read().tasks[0].title==u8"中文日程😀","session restart reads own directory");
    auto port=reload.Port();auto scope=port.read().scope;port.refreshTime();
    Check(port.read().scope.generation==scope.generation,"unchanged timezone keeps draft scope");
    DYNAMIC_TIME_ZONE_INFORMATION zone{};zone.DynamicDaylightTimeDisabled=TRUE;
    shell::TodoSession shifting((root/"shift"/"calendar.json").u8string(),[&](DYNAMIC_TIME_ZONE_INFORMATION* out){*out=zone;return TIME_ZONE_ID_UNKNOWN;});
    auto shiftingPort=shifting.Port();const auto initial=shifting.Read().scope;
    shiftingPort.refreshTime();Check(shifting.Read().scope.generation==initial.generation,"injected unchanged timezone keeps scope");
    zone.Bias=-480;shiftingPort.refreshTime();Check(shifting.Read().scope.generation>initial.generation,"changed timezone invalidates scope");
    add.scope=initial;Check(!shifting.Dispatch(add).ok,"old timezone draft rejected");
    Check(std::filesystem::exists(root/"todo-data"/"local"/"todo-data.json")&&!std::filesystem::exists(root/"calendar-preferences.json"),"todo session never writes preferences");
    std::cout<<"TODO_SESSION_PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
