#include "apps/shell/todo_session.h"
#include <objbase.h>
#include <filesystem>
namespace lume::shell {
namespace {
std::string NewId(){
    GUID id{};if(FAILED(CoCreateGuid(&id)))return {};
    wchar_t text[40]{};if(!StringFromGUID2(id,text,40))return {};
    std::string value;for(int i=1;text[i]&&text[i]!=L'}';++i)value.push_back(static_cast<char>(text[i]));return value;
}
}
TodoSession::TodoSession(const std::string& path,platform::windows::WindowsLocalTime::ZoneReader reader):time_(std::move(reader)){
    if(path.empty())return;
    const auto prefs=std::filesystem::u8path(path);
    // Relative or parentless paths cannot accidentally target the process cwd.
    if(!prefs.is_absolute()||!prefs.has_parent_path())return;
    const auto directory=prefs.parent_path()/"todo-data";std::error_code error;
    base_=directory.u8string();
    std::filesystem::create_directories(directory/"local",error);if(error)return;
    repository_=std::make_unique<todo::TodoRepository>(files_,time_,base_,NewId,false);
    repository_->SwitchAccount("local");
}
todo::RepositorySnapshot TodoSession::Read()const{return repository_?repository_->Read():todo::RepositorySnapshot{};}
todo::CommandResult TodoSession::Dispatch(const todo::Command& command){
    if(repository_){
        auto result=repository_->Dispatch(command,platform::windows::WindowsLocalTime::NowMillis());
        const auto changed=changed_;
        if(result.committed&&changed)changed();
        return result;
    }
    todo::CommandResult result;result.error=todo::Error::ReadOnly;return result;
}
void TodoSession::RefreshTime(){
    const auto previous=time_.Revision();time_.Refresh();
    if(!repository_&&!base_.empty()){
        std::error_code error;std::filesystem::create_directories(std::filesystem::u8path(base_)/"local",error);
        if(!error)repository_=std::make_unique<todo::TodoRepository>(files_,time_,base_,NewId,false);
    }
    if(repository_&&(previous!=time_.Revision()||!repository_->Read().writable)){
        repository_->SwitchAccount("local");
        const auto changed=changed_;
        if(changed&&(previous!=time_.Revision()||repository_->Read().writable))changed();
    }
}
platform::windows::CalendarTodoPort TodoSession::Port(){
    return {&time_,[this]{return Read();},[this](const todo::Command& c){return Dispatch(c);},
        platform::windows::WindowsLocalTime::NowMillis,[this]{RefreshTime();}};
}
}
