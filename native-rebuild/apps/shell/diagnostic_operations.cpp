#include "apps/shell/diagnostic_operations.h"

namespace lume::shell {

std::optional<std::string> DiagnosticModule(ui::PageCommand command) {
    if(command==ui::PageCommand::CalendarTextColor||command==ui::PageCommand::CalendarHighlightColor||
       command==ui::PageCommand::WeatherLocationSettings||command==ui::PageCommand::RadialCaptureBegin||
       command==ui::PageCommand::RadialCaptureEnd)return {};
    const auto value=static_cast<int>(command);
    if(value>=static_cast<int>(ui::PageCommand::ClockEnable)&&value<=static_cast<int>(ui::PageCommand::ClockReset))return "clock";
    if(value>=static_cast<int>(ui::PageCommand::CalendarEnable)&&value<=static_cast<int>(ui::PageCommand::CalendarReset))return "calendar";
    if(value>=static_cast<int>(ui::PageCommand::PerformanceEnable)&&value<=static_cast<int>(ui::PageCommand::PerformanceReset))return "performance";
    if(value>=static_cast<int>(ui::PageCommand::AlmanacEnable)&&value<=static_cast<int>(ui::PageCommand::AlmanacReset))return "almanac";
    if(value>=static_cast<int>(ui::PageCommand::WeatherEnable)&&value<=static_cast<int>(ui::PageCommand::WeatherCityNanjing))return "weather";
    if(value>=static_cast<int>(ui::PageCommand::OrganizerEnable)&&value<=static_cast<int>(ui::PageCommand::OrganizerIconScaleLast))return "organizer";
    if(value>=static_cast<int>(ui::PageCommand::IconsEnable)&&value<=static_cast<int>(ui::PageCommand::IconsDelayLast))return "icons";
    if(value>=static_cast<int>(ui::PageCommand::TaskbarEnable)&&value<=static_cast<int>(ui::PageCommand::TaskbarReset))return "taskbar";
    if(value>=static_cast<int>(ui::PageCommand::MinimizeEnable)&&value<=static_cast<int>(ui::PageCommand::MinimizeUnhideDurationLast))return "minimize";
    if(value>=static_cast<int>(ui::PageCommand::RadialEnable)&&value<=static_cast<int>(ui::PageCommand::RadialShortcutLast))return "radial";
    if(value>=static_cast<int>(ui::PageCommand::DockEnable)&&value<=static_cast<int>(ui::PageCommand::DockLaunchpadIconScaleLast))return "dock";
    return {};
}
std::string DiagnosticModuleForMessage(const nlohmann::json& message) {
    auto type=message.value("type",std::string{});
    if(type=="settings")return "clock";
    if(type=="calendar-settings")return "calendar";
    if(type.size()>6&&type.compare(type.size()-6,6,"-state")==0)type.resize(type.size()-6);
    return type;
}
std::string LatestEventIdentity(const nlohmann::json& state) {
    if(!state.contains("events")||!state.at("events").is_array()||state.at("events").empty())return {};
    const auto& event=state.at("events").back();
    return event.value("module",std::string{})+"|"+event.value("kind",std::string{})+"|"+
        std::to_string(event.value("firstAt",0ULL))+"|"+event.value("error",std::string{})+"|"+
        std::to_string(event.value("repeatCount",1u));
}

void DiagnosticOperations::Begin(ui::PageCommand command, const std::function<std::uint64_t()>& taskbarRevision) {
    const auto module=diagnostics::FeatureEnabled?DiagnosticModule(command):std::nullopt;
    if(!module)return;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto old=pending_.find(*module);
    if(old!=pending_.end())
        collector_.CompleteOperation(old->second,"cancelled",*module,"configure");
    pending_[*module]=collector_.StartOperation(*module,"configure");
    if(*module=="taskbar")pendingTaskbarRevision_=taskbarRevision();
}
void DiagnosticOperations::Record(std::string_view bytes) {
    if(!diagnostics::FeatureEnabled)return;
    auto message=nlohmann::json::parse(bytes,nullptr,false);
    if(!message.is_object())return;
    const auto module=DiagnosticModuleForMessage(message);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto pending=pending_.find(module);
    if(pending==pending_.end()) {collector_.RecordMessage(message);return;}
    // Taskbar runtime revisions reject a stale completion from an earlier request.
    if(module=="taskbar"&&message.value("revision",0ULL)<=pendingTaskbarRevision_) {
        collector_.RecordMessage(message);return;
    }
    message["operationId"]=pending->second;
    message["action"]="configure";
    const auto error=message.value("error",std::string{});
    const auto result=message.value("result",std::string{});
    const bool completed=!error.empty()||result=="failed"||result=="cancelled"||message.value("saved",false)||
        (module=="taskbar"&&!message.value("applying",false)&&!message.value("pendingRecovery",false));
    collector_.RecordMessage(message);
    if(completed) {
        collector_.CompleteOperation(pending->second,"completed",module,"configure");
        pending_.erase(pending);
    }
}
void DiagnosticOperations::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    collector_.Clear();pending_.clear();
}

}  // namespace lume::shell
