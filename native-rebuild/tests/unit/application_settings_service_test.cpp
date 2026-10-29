#include "core/application_settings/application_settings.h"
#include "vendor/nlohmann/json.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace lume::application_settings;
namespace {
void Check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
struct File final : IPreferencesFile {
    FileRead value;
    bool fail = false;
    int writes = 0;
    std::function<void()> onRead;
    FileRead Read() override { if (onRead) onRead(); return value; }
    bool WriteAtomically(const std::string& bytes) override {
        ++writes;
        if (fail) return false;
        value = {ReadStatus::Loaded, bytes};
        return true;
    }
};
struct Startup final : IStartupRegistration {
    StartupEntry value;
    int writes = 0;
    int failWrite = 0;
    int ignoreWrite = 0;
    bool failRead = false;
    std::function<void()> onRead;
    std::function<void()> onWrite;
    bool Read(StartupEntry& entry) override { if (onRead) onRead(); entry = value; return !failRead; }
    bool Write(const StartupEntry& entry) override {
        ++writes;
        if (writes != ignoreWrite) value = entry;
        if (onWrite) onWrite();
        return writes != failWrite;
    }
};
struct Fixture {
    File file;
    Startup startup;
    bool runtime = true;
    int calls = 0;
    int failCall = 0;
    Service service{file, startup, L"C:\\Program Files\\Lume\\LumeShell.exe", [&](bool enabled) {
        ++calls;
        runtime = enabled;
        return calls != failCall;
    }};
};
struct RuntimeFixture {
    File file;
    Startup startup;
    std::optional<bool> runtime{true};
    int calls = 0;
    int failCall = 0;
    bool failAll = false;
    bool unknownOnFailure = false;
    Service service{file, startup, L"C:\\Lume\\LumeShell.exe", [&](bool enabled) {
        ++calls;
        if (failAll || calls == failCall) {
            if (unknownOnFailure) runtime.reset();
            return false;
        }
        runtime = enabled;
        return true;
    }};
    void FailRollback() {
        file.value = {ReadStatus::Loaded, EncodePreferences({})};
        const auto before = file.value.bytes;
        Check(service.Load(), "load known balanced preference before runtime mismatch");
        file.fail = true;
        failCall = calls + 2;
        Check(!service.SetBalancedMode(false) && runtime == false &&
              service.State().preferences.balancedMode && service.State().error == "balanced-rollback" &&
              file.value.bytes == before, "failed rollback has no effect: runtime false and persisted preference true");
        file.fail = false;
    }
};
void VerifyFirstRunAutostart() {
    Fixture fresh;
    const auto enabled = EnabledStartupEntry(*StartupCommand(L"C:\\Program Files\\Lume\\LumeShell.exe"));
    Check(fresh.service.Load() && fresh.startup.writes == 0 && fresh.file.writes == 0,
          "首次加载保持只读");
    Check(fresh.service.Restore() && SameStartupEntry(fresh.startup.value, enabled) &&
          fresh.service.State().preferences.autostart && fresh.file.writes == 1 &&
          DecodePreferences(fresh.file.value.bytes).preferences.autostart,
          "首次恢复必须真实注册并持久化默认开启");
    const auto writes = fresh.startup.writes;
    Check(fresh.service.Restore() && fresh.startup.writes == writes && fresh.file.writes == 1,
          "重复恢复不重复注册或保存");
    Check(fresh.service.SetAutostart(false) && !fresh.startup.value.exists &&
          !DecodePreferences(fresh.file.value.bytes).preferences.autostart,
          "关闭同时删除本实例启动项并持久化");
    Service reopened(fresh.file, fresh.startup, L"C:\\Program Files\\Lume\\LumeShell.exe", [](bool) { return true; });
    const auto disabledWrites = fresh.startup.writes;
    Check(reopened.Restore() && !reopened.State().preferences.autostart &&
          fresh.startup.writes == disabledWrites, "已保存关闭在下一次启动仍保持关闭");

    Fixture preferences;
    Check(Preferences{}.autostart && preferences.service.LoadPreferences() &&
          preferences.service.State().preferences.autostart && preferences.startup.writes == 0,
          "新配置默认开启意图不在只读加载阶段丢失");
    for (const bool intent : {false, true}) {
        Fixture existing;
        existing.file.value = {ReadStatus::Loaded, EncodePreferences({"en", false, intent})};
        const auto before = existing.file.value.bytes;
        Check(existing.service.LoadPreferences() && existing.service.State().preferences.autostart == intent,
              "只读配置加载保留已保存的自启意图");
        Check(existing.service.Restore() && !existing.service.State().preferences.autostart &&
              existing.startup.writes == 0 && existing.file.writes == 0 && existing.file.value.bytes == before,
              "已有配置以系统读回为准且不触发新用户默认注册");
    }
    Fixture disabledBeforeRestore;
    Check(disabledBeforeRestore.service.LoadPreferences() && disabledBeforeRestore.service.SetAutostart(false) &&
          disabledBeforeRestore.service.Restore() && disabledBeforeRestore.startup.writes == 0 &&
          !disabledBeforeRestore.service.State().preferences.autostart,
          "首次恢复前的显式关闭优先于默认开启");

    Fixture appeared;
    Check(appeared.service.LoadPreferences(), "读取尚不存在的配置");
    appeared.file.value = {ReadStatus::Loaded, EncodePreferences({"en", false, false})};
    const auto before = appeared.file.value.bytes;
    Check(appeared.service.Restore() && appeared.startup.writes == 0 && appeared.file.writes == 0 &&
          appeared.file.value.bytes == before && !appeared.service.State().preferences.autostart &&
          appeared.service.State().preferences.language == "en" && !appeared.runtime,
          "异步恢复前出现的已有关闭配置不能被默认值覆盖");
}
void VerifyAutostartInitializationFailures() {
    Fixture writeFailure;
    writeFailure.startup.failWrite = 1;
    Check(!writeFailure.service.Restore() && !writeFailure.startup.value.exists &&
          !writeFailure.service.State().preferences.autostart && writeFailure.file.writes == 0 &&
          writeFailure.service.State().error == "autostart-write",
          "首次部分写入失败回滚且不保存开启");
    Fixture saveFailure;
    saveFailure.file.fail = true;
    Check(!saveFailure.service.Restore() && !saveFailure.startup.value.exists &&
          !saveFailure.service.State().preferences.autostart && saveFailure.file.value.status == ReadStatus::Missing &&
          saveFailure.service.State().error == "preferences-save",
          "首次保存失败恢复原系统状态并保留错误");
    Fixture ignoredWrite;
    ignoredWrite.startup.ignoreWrite = 1;
    Check(!ignoredWrite.service.Restore() && !ignoredWrite.service.State().preferences.autostart &&
          ignoredWrite.file.writes == 0 && ignoredWrite.service.State().error == "autostart-write",
          "写入返回成功但读回不符不能报告已启用");
    Fixture readFailure;
    readFailure.startup.failRead = true;
    Check(!readFailure.service.Restore() && readFailure.startup.writes == 0 &&
          readFailure.file.writes == 0 && readFailure.service.State().error == "autostart-read",
          "首次系统读取失败不能尝试注册");
    Fixture rollbackFailure;
    rollbackFailure.file.fail = true;
    rollbackFailure.startup.ignoreWrite = 2;
    Check(!rollbackFailure.service.Restore() && rollbackFailure.service.State().preferences.autostart &&
          rollbackFailure.service.State().error == "autostart-rollback",
          "首次回滚失败必须返回系统实际残留的开启状态");
    Fixture appearedDuringWrite;
    const auto disabled = EncodePreferences({"en", false, false});
    appearedDuringWrite.startup.onWrite = [&] {
        appearedDuringWrite.file.value = {ReadStatus::Loaded, disabled};
    };
    Check(!appearedDuringWrite.service.Restore() && !appearedDuringWrite.startup.value.exists &&
          appearedDuringWrite.file.writes == 0 && appearedDuringWrite.file.value.bytes == disabled &&
          !appearedDuringWrite.service.State().error.empty(),
          "注册期间出现的已保存关闭必须保留并回滚默认注册");
    for (const auto status : {ReadStatus::Failed, ReadStatus::Loaded}) {
        Fixture unsafe;
        unsafe.file.value = {status, "{"};
        Check(!unsafe.service.Restore() && unsafe.startup.writes == 0 && unsafe.file.writes == 0,
              "读取失败或损坏配置不是新用户");
    }
}
void VerifyLegacyStartupMigration() {
    const auto legacy = EnabledStartupEntry(L"\"C:\\Program Files\\Lume\\lume.exe\" -tray-only");
    const auto native = EnabledStartupEntry(*StartupCommand(L"C:\\Program Files\\Lume\\LumeShell.exe"));
    Fixture fresh;
    fresh.startup.value = legacy;
    Check(fresh.service.Load() && fresh.service.State().preferences.autostart &&
          fresh.startup.writes == 0, "同安装目录旧版命令仍会自启，不能被只读快照误报为关闭");
    Check(fresh.service.Restore() && SameStartupEntry(fresh.startup.value, native) &&
          DecodePreferences(fresh.file.value.bytes).preferences.autostart,
          "首次默认开启将同目录旧版入口迁移为当前原生命令");
    for (const bool intent : {false, true}) {
        Fixture existing;
        existing.file.value = {ReadStatus::Loaded, EncodePreferences({"en", false, intent})};
        existing.startup.value = legacy;
        Check(existing.service.Load() && existing.service.State().preferences.autostart,
              "只读加载报告旧版入口仍然真实启用");
        Check(existing.service.Restore() && existing.service.State().preferences.autostart == intent &&
              SameStartupEntry(existing.startup.value, intent ? native : StartupEntry{}) &&
              DecodePreferences(existing.file.value.bytes).preferences.autostart == intent,
              "恢复保留已保存意图：关闭删除旧入口，开启迁移到原生命令");
    }
    Fixture nativeDisabled;
    nativeDisabled.file.value = {ReadStatus::Loaded, EncodePreferences({"zh-CN", true, false})};
    nativeDisabled.startup.value = native;
    Check(nativeDisabled.service.Load() && nativeDisabled.service.Restore() &&
          !nativeDisabled.startup.value.exists && !nativeDisabled.service.State().preferences.autostart,
          "已保存关闭也清理本实例原生命令，不能被实际读取覆盖关闭意图");
    Fixture preserveIntent;
    preserveIntent.file.value = {ReadStatus::Loaded, EncodePreferences({"zh-CN", true, false})};
    preserveIntent.startup.value = legacy;
    Check(preserveIntent.service.Load() && preserveIntent.service.SetLanguage("en") &&
          !DecodePreferences(preserveIntent.file.value.bytes).preferences.autostart &&
          preserveIntent.service.State().preferences.autostart && preserveIntent.service.Restore() &&
          !preserveIntent.startup.value.exists,
          "无关设置保存不能把系统实际开启状态覆盖到用户已保存关闭意图");
    Fixture explicitOff;
    explicitOff.startup.value = legacy;
    Check(explicitOff.service.Load() && explicitOff.service.SetAutostart(false) &&
          !explicitOff.startup.value.exists && !DecodePreferences(explicitOff.file.value.bytes).preferences.autostart,
          "显式关闭会真正删除同目录旧版入口");
    Fixture rollback;
    rollback.startup.value = legacy;
    rollback.file.fail = true;
    Check(!rollback.service.Restore() && SameStartupEntry(rollback.startup.value, legacy) &&
          rollback.service.State().preferences.autostart && rollback.service.State().error == "preferences-save",
          "迁移保存失败恢复旧版命令原始字节并报告仍开启");
    Fixture caseInsensitive;
    caseInsensitive.startup.value = EnabledStartupEntry(L"\"c:\\program files\\LUME\\LUME.EXE\" -tray-only");
    Check(caseInsensitive.service.Load() && caseInsensitive.service.State().preferences.autostart &&
          caseInsensitive.service.SetAutostart(false) && !caseInsensitive.startup.value.exists,
          "同目录旧命令识别兼容 Windows 路径大小写");
    for (const auto* command : {L"\"D:\\lume\\lume.exe\" -tray-only",
                                L"\"C:\\Old\\LumeShell.exe\" --tray-only"}) {
        Fixture otherInstallation;
        otherInstallation.file.value = {ReadStatus::Loaded, EncodePreferences({"zh-CN", true, false})};
        otherInstallation.startup.value = EnabledStartupEntry(command);
        Check(otherInstallation.service.Load() && otherInstallation.service.State().preferences.autostart &&
              otherInstallation.service.State().error == "autostart-conflict" &&
              !otherInstallation.service.Restore() && otherInstallation.startup.writes == 0 &&
              !otherInstallation.service.SetAutostart(false) && otherInstallation.startup.writes == 0 &&
              otherInstallation.service.State().preferences.autostart &&
              otherInstallation.service.State().error == "autostart-conflict",
              "其他安装目录明确仍自启时显示开启与归属冲突，不能假关闭也不能擅自删除");
        Check(otherInstallation.service.SetLanguage("en") &&
              otherInstallation.service.State().error == "autostart-conflict" &&
              !DecodePreferences(otherInstallation.file.value.bytes).preferences.autostart &&
              otherInstallation.service.BeginCheck(), "无关设置保存保留关闭意图与启动项冲突");
        otherInstallation.service.FinishCheck({{"current", CurrentVersion, 0}, {}});
        Check(otherInstallation.service.State().error == "autostart-conflict",
              "只读更新完成不能掩盖已确认的旧目录启动项冲突");
    }
}
void VerifyConcurrentAutostartIntent() {
    const auto legacy = EnabledStartupEntry(L"\"C:\\Program Files\\Lume\\lume.exe\" -tray-only");
    const auto enabledPreferences = EncodePreferences({"zh-CN", true, true});
    const auto disabledPreferences = EncodePreferences({"en", false, false});
    Fixture beforeRestore;
    beforeRestore.file.value = {ReadStatus::Loaded, enabledPreferences};
    beforeRestore.startup.value = legacy;
    Check(beforeRestore.service.LoadPreferences(), "加载原有开启意图");
    beforeRestore.file.value = {ReadStatus::Loaded, disabledPreferences};
    Check(beforeRestore.service.Restore() && !beforeRestore.startup.value.exists &&
          !DecodePreferences(beforeRestore.file.value.bytes).preferences.autostart &&
          !beforeRestore.service.State().preferences.autostart,
          "恢复必须尊重其他实例刚保存的关闭，不能按构造阶段旧意图重新开启");
    Fixture duringMigration;
    duringMigration.file.value = {ReadStatus::Loaded, enabledPreferences};
    duringMigration.startup.value = legacy;
    duringMigration.startup.onWrite = [&] {
        duringMigration.file.value = {ReadStatus::Loaded, disabledPreferences};
    };
    Check(!duringMigration.service.Restore() && SameStartupEntry(duringMigration.startup.value, legacy) &&
          duringMigration.file.value.bytes == disabledPreferences && duringMigration.file.writes == 0 &&
          duringMigration.service.State().preferences.autostart &&
          duringMigration.service.State().error == "preferences-save",
          "自动迁移期间出现的关闭配置必须保留并回滚旧命令");
    Fixture partialException;
    partialException.startup.onWrite = [&] {
        if (partialException.startup.writes == 1) throw std::runtime_error("模拟部分写入异常");
    };
    Check(!partialException.service.Restore() && !partialException.startup.value.exists &&
          !partialException.service.State().preferences.autostart &&
          partialException.service.State().error == "autostart-write",
          "部分写入抛异常仍需系统读回并回滚");
}
void VerifyConcurrentStartupRemoval() {
    Fixture removed;
    removed.file.value = {ReadStatus::Loaded, EncodePreferences({"zh-CN", true, true})};
    removed.startup.value = EnabledStartupEntry(L"\"C:\\Program Files\\Lume\\lume.exe\" -tray-only");
    const auto disabled = EncodePreferences({"en", false, false});
    removed.startup.onWrite = [&] {
        if (removed.startup.writes == 1) {
            removed.startup.value = {};
            removed.file.value = {ReadStatus::Loaded, disabled};
        }
    };
    Check(!removed.service.Restore() && !removed.startup.value.exists && removed.startup.writes == 1 &&
          removed.file.writes == 0 && removed.file.value.bytes == disabled &&
          !removed.service.State().preferences.autostart,
          "迁移期间外部已经删除的启动项不能被回滚重新开启");
}
void VerifyConcurrentUnrelatedPreferences() {
    Fixture changed;
    changed.file.value = {ReadStatus::Loaded, EncodePreferences({"zh-CN", true, true})};
    const auto legacy = EnabledStartupEntry(L"\"C:\\Program Files\\Lume\\lume.exe\" -tray-only");
    changed.startup.value = legacy;
    const auto updated = EncodePreferences({"en", false, true});
    changed.startup.onWrite = [&] { changed.file.value = {ReadStatus::Loaded, updated}; };
    Check(!changed.service.Restore() && changed.file.value.bytes == updated && changed.file.writes == 0 &&
          SameStartupEntry(changed.startup.value, legacy) && changed.service.State().error == "preferences-save",
          "自动迁移不能覆盖同期修改的语言和均衡模式，即使自启意图未变");
}
void VerifyForeignStartupProtection() {
    for (const auto& entry : {StartupEntry{true, 7, {1, 2, 0, 255}},
                             EnabledStartupEntry(L"\"C:\\Other\\Other.exe\" --tray-only"),
                             EnabledStartupEntry(L"\"C:\\Program Files\\Lume\\Other.exe\" -tray-only"),
                             EnabledStartupEntry(L"\"C:\\Program Files\\Lume\\lume.exe\" --tray-only"),
                             EnabledStartupEntry(L"\"C:\\Program Files\\Lume\\lume.exe\" -tray-only --other")}) {
        Fixture foreign;
        foreign.startup.value = entry;
        Check(foreign.service.Load() && !foreign.service.State().preferences.autostart,
              "无法确认归属的启动项不视为当前实例开启");
        for (const bool enabled : {false, true}) {
            Check(!foreign.service.SetAutostart(enabled) && foreign.startup.writes == 0 &&
                  foreign.file.writes == 0 && SameStartupEntry(entry, foreign.startup.value) &&
                  !foreign.service.State().error.empty(),
                  "显式开关不能覆盖或删除无关程序与未确认旧实例");
        }
        Check(!foreign.service.Restore() && foreign.startup.writes == 0 &&
              SameStartupEntry(entry, foreign.startup.value), "首次默认注册同样保护既有启动项");
    }
    Fixture replaced;
    const auto foreign = EnabledStartupEntry(L"\"C:\\Other\\Other.exe\"");
    replaced.startup.onWrite = [&] { replaced.startup.value = foreign; };
    Check(replaced.service.Load() && !replaced.service.SetAutostart(true) &&
          replaced.startup.writes == 1 && SameStartupEntry(foreign, replaced.startup.value) &&
          !replaced.service.State().preferences.autostart &&
          replaced.service.State().error == "autostart-rollback",
          "回滚前出现的无关程序值也不能覆盖或删除");
}
void VerifyPersistentRuntimeMismatch() {
    RuntimeFixture mismatch;
    mismatch.FailRollback();
    const auto calls = mismatch.calls;
    Check(mismatch.service.SetLanguage("en") && mismatch.runtime == false &&
          mismatch.service.State().preferences.language == "en" && mismatch.service.State().preferences.balancedMode &&
          mismatch.service.State().error == "balanced-rollback",
          "successful language save cannot hide an unresolved runtime mismatch");
    Check(mismatch.service.BeginCheck() && mismatch.service.State().error == "balanced-rollback",
          "starting an update check preserves the independent runtime error");
    mismatch.service.FinishCheck({{"current", CurrentVersion, 0}, {}});
    Check(mismatch.service.State().update.status == "current" && mismatch.service.State().error == "balanced-rollback",
          "successful update check cannot hide an unresolved runtime mismatch");
    Check(mismatch.service.BeginCheck(), "start failed update check with an existing runtime mismatch");
    mismatch.service.FinishCheck({{"error", {}, 0}, "update-offline"});
    Check(mismatch.service.State().error == "balanced-rollback", "unrelated failure cannot replace the runtime diagnosis");
    Check(mismatch.service.SetAutostart(true) && mismatch.service.State().error == "balanced-rollback",
          "successful startup transaction cannot clear a runtime mismatch");
    Check(mismatch.service.Load() && mismatch.service.State().error == "balanced-rollback" && mismatch.calls == calls,
          "reloading preferences is not confirmation of the runtime policy");
    mismatch.failAll = true;
    Check(!mismatch.service.Restore() && mismatch.service.State().error == "balanced-rollback",
          "unsuccessful restore keeps an existing runtime mismatch");
    mismatch.failAll = false;
    Check(mismatch.service.Restore() && mismatch.runtime == true && mismatch.service.State().error.empty(),
          "only a confirmed successful restore clears the runtime mismatch");

    RuntimeFixture reapply;
    reapply.FailRollback();
    const auto beforeReapply = reapply.calls;
    Check(reapply.service.SetBalancedMode(true) && reapply.calls == beforeReapply + 1 &&
          reapply.runtime == true && reapply.service.State().error.empty(),
          "reapplying the persisted value must reach the port and clear the mismatch only on success");

    RuntimeFixture unknown;
    Check(unknown.service.Load(), "load unknown runtime fixture");
    unknown.failAll = true;
    unknown.unknownOnFailure = true;
    Check(!unknown.service.Restore() && !unknown.runtime && unknown.service.State().error == "balanced-runtime",
          "failed restore leaves runtime unknown rather than implicitly confirmed");
    Check(unknown.service.SetLanguage("en") && unknown.service.State().error == "balanced-runtime" &&
          !unknown.runtime, "unrelated save cannot clear unknown runtime state");
    Check(unknown.service.BeginCheck() && unknown.service.State().error == "balanced-runtime",
          "update checking preserves unknown runtime state");
    unknown.service.FinishCheck({{"current", CurrentVersion, 0}, {}});
    Check(unknown.service.State().error == "balanced-runtime", "current version cannot confirm unknown runtime state");
    unknown.failAll = false;
    Check(unknown.service.SetBalancedMode(false) && unknown.runtime == false &&
          !unknown.service.State().preferences.balancedMode && unknown.service.State().error.empty(),
          "successful explicit apply and persistence resolve unknown runtime state");

    RuntimeFixture restoredByRollback;
    restoredByRollback.FailRollback();
    restoredByRollback.file.fail = true;
    Check(!restoredByRollback.service.SetBalancedMode(false) && restoredByRollback.runtime == true &&
          restoredByRollback.service.State().error == "preferences-save",
          "confirmed successful rollback resolves the mismatch but preserves the independent save failure");
    restoredByRollback.file.fail = false;
    Check(restoredByRollback.service.SetLanguage("en") && restoredByRollback.service.State().error.empty(),
          "unrelated success can clear an ordinary save error only after runtime was really restored");
}
}
int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "";
    if (selected == "--autostart-defaults") { VerifyFirstRunAutostart(); return 0; }
    if (selected == "--autostart-failures") { VerifyAutostartInitializationFailures(); return 0; }
    if (selected == "--autostart-ownership") { VerifyForeignStartupProtection(); return 0; }
    if (selected == "--autostart-legacy") { VerifyLegacyStartupMigration(); return 0; }
    if (selected == "--autostart-concurrency") { VerifyConcurrentAutostartIntent(); return 0; }
    if (selected == "--autostart-removal-race") { VerifyConcurrentStartupRemoval(); return 0; }
    if (selected == "--autostart-preferences-race") { VerifyConcurrentUnrelatedPreferences(); return 0; }
    VerifyFirstRunAutostart();
    VerifyAutostartInitializationFailures();
    VerifyForeignStartupProtection();
    VerifyLegacyStartupMigration();
    VerifyConcurrentAutostartIntent();
    VerifyConcurrentStartupRemoval();
    VerifyConcurrentUnrelatedPreferences();
    VerifyPersistentRuntimeMismatch();
    const auto command = StartupCommand(L"C:\\Program Files\\Lume\\LumeShell.exe");
    Check(command && *command == L"\"C:\\Program Files\\Lume\\LumeShell.exe\" --tray-only", "quoted native tray command");
    Check(!StartupCommand(L"") && !StartupCommand(L"C:\\bad\".exe") && !StartupCommand(L"relative.exe"), "reject unsafe executable paths");
    Check(!StartupCommand(L"C:\\" + std::wstring(260, L'a') + L"\\LumeShell.exe"),
          "Run command lines longer than 260 characters cannot claim working autostart");
    Fixture f;
    Check(f.service.Load(), "small-file load succeeds before restoring runtime");
    Check(f.calls == 0 && f.startup.writes == 0 && f.file.writes == 0, "load is strictly read-only");
    Check(f.service.State().preferences.language == "zh-CN" && f.service.State().preferences.balancedMode,
          "Chinese and balanced mode defaults");
    Check(f.service.Restore() && f.calls == 1 && f.startup.writes == 1 && f.file.writes == 1,
          "首次恢复应用均衡模式并完成默认自启事务");
    Check(f.service.SetLanguage("en"), "persist language");
    Check(f.service.SetBalancedMode(false) && !f.runtime, "persist balanced mode and apply runtime");
    Service reloaded(f.file, f.startup, L"C:\\Program Files\\Lume\\LumeShell.exe", {});
    Check(reloaded.Load() && reloaded.State().preferences.language == "en" &&
          !reloaded.State().preferences.balancedMode, "preferences round trip");
    Check(!f.service.SetLanguage("fr") && f.service.State().error == "language-invalid", "backend validates language enum");
    const auto saved = f.file.value.bytes;
    f.file.fail = true;
    Check(!f.service.SetLanguage("zh-CN") && f.service.State().preferences.language == "en" &&
          f.file.value.bytes == saved, "language save failure preserves file and snapshot");
    Check(!f.service.SetBalancedMode(true) && !f.runtime &&
          !f.service.State().preferences.balancedMode && f.service.State().error == "preferences-save",
          "balanced persistence failure rolls runtime back");
    f.file.fail = false;
    f.failCall = f.calls + 1;
    Check(!f.service.SetBalancedMode(true) && !f.runtime &&
          f.service.State().error == "balanced-runtime" && f.file.value.bytes == saved,
          "partial runtime failure rolls back without persisting");
    f.failCall = 0;
    f.file.fail = true;
    f.failCall = f.calls + 2;
    Check(!f.service.SetBalancedMode(true) && f.service.State().error == "balanced-rollback", "rollback failure is explicit");

    for (const auto& body : {
        std::string("{"), std::string(R"({"version":99,"language":"en","balancedMode":false,"autostart":true})"),
        std::string(R"({"version":1,"language":"en","balancedMode":"false","autostart":true})"),
        std::string(R"({"version":1,"version":1,"language":"en","balancedMode":false,"autostart":true})"),
        std::string(R"({"version":1,"language":"en","balancedMode":false,"autostart":true,"extra":0})")
    }) {
        Fixture broken;
        broken.file.value = {ReadStatus::Loaded, body};
        Check(!broken.service.Load(), "reject corrupt or unknown preferences");
        const auto error = broken.service.State().error;
        Check(!error.empty() && !broken.service.SetLanguage("en") && !broken.service.SetBalancedMode(false) &&
              !broken.service.SetAutostart(true) && broken.file.writes == 0 && broken.startup.writes == 0 &&
              broken.calls == 0 && broken.file.value.bytes == body, "unsafe storage is never overwritten or applied");
    }
    Fixture external;
    Check(external.service.Load(), "load new preferences");
    external.file.value = {ReadStatus::Loaded, R"({"version":3})"};
    Check(!external.service.SetLanguage("en") && external.file.writes == 0 &&
          external.service.State().error == "preferences-version", "revalidate externally upgraded file before every save");

    Fixture startup;
    startup.file.value = {ReadStatus::Loaded, R"({"version":1,"language":"en","balancedMode":true,"autostart":false})"};
    startup.startup.value = EnabledStartupEntry(*command);
    Check(startup.service.Load() && startup.service.State().preferences.autostart &&
          startup.startup.writes == 0, "actual startup registration wins over cached preference");
    const auto previous = startup.startup.value;
    startup.file.fail = true;
    Check(!startup.service.SetAutostart(false) && SameStartupEntry(startup.startup.value, previous) &&
          startup.service.State().preferences.autostart && startup.service.State().error == "preferences-save",
          "startup file failure restores original registration");
    startup.file.fail = false;
    startup.startup.failWrite = startup.startup.writes + 1;
    Check(!startup.service.SetAutostart(false) && SameStartupEntry(startup.startup.value, previous) &&
          startup.service.State().error == "autostart-write", "partial registration failure restores original value");
    startup.startup.failWrite = 0;
    Check(startup.service.SetAutostart(false) && !startup.startup.value.exists, "explicit user request removes Run value");
    Check(startup.service.SetAutostart(true) && SameStartupEntry(startup.startup.value, previous), "explicit enable writes quoted command");
    Fixture foreign;
    foreign.startup.value = {true, 7, {1, 2, 0, 255}};
    const auto raw = foreign.startup.value;
    Check(foreign.service.Load() && !foreign.service.State().preferences.autostart, "foreign registry type is not our enabled command");
    foreign.file.fail = true;
    Check(!foreign.service.SetAutostart(true) && foreign.startup.writes == 0 &&
          SameStartupEntry(raw, foreign.startup.value), "未知类型启动项在写入前即被保护");
    foreign.startup.failRead = true;
    Check(!foreign.service.SetAutostart(false) && foreign.service.State().error == "autostart-read", "read failure cannot pretend disabled success");

    Fixture rollback;
    Check(rollback.service.Load(), "load startup rollback fixture");
    rollback.file.fail = true;
    rollback.startup.ignoreWrite = 2;
    Check(!rollback.service.SetAutostart(true) && rollback.service.State().error == "autostart-rollback" &&
          rollback.service.State().preferences.autostart,
          "failed registry rollback reports the actual remaining registration, not the desired old value");
    Fixture protectedStorage;
    protectedStorage.file.value = {ReadStatus::Loaded, R"({"version":7})"};
    Check(!protectedStorage.service.Load() && protectedStorage.service.BeginCheck(), "corrupt preferences do not prevent read-only update checks");
    protectedStorage.service.FinishCheck({{"current", CurrentVersion, 0}, {}});
    Check(!protectedStorage.service.SetLanguage("en") && protectedStorage.file.writes == 0 &&
          protectedStorage.service.State().error == "preferences-version", "update state cannot erase storage protection");

    File cancelledFile;
    Startup cancelledStartup;
    Cancellation cancellation;
    bool cancelledRuntime = true;
    int cancelledCalls = 0;
    Service cancelledService(cancelledFile, cancelledStartup, L"C:\\Lume\\LumeShell.exe", [&](bool enabled) {
        cancelledRuntime = enabled;
        ++cancelledCalls;
        return true;
    }, &cancellation);
    Check(cancelledService.Load(), "load cancellation fixture");
    cancelledFile.onRead = [&] { cancellation.cancelled = true; };
    Check(!cancelledService.SetBalancedMode(false) && cancelledRuntime && cancelledCalls == 2 &&
          cancelledFile.writes == 0 && cancelledService.State().error == "cancelled",
          "cancellation during file preflight prevents save and rolls runtime back");
    cancellation.cancelled = false;
    cancelledFile.onRead = {};
    cancelledStartup.onRead = [&] { cancellation.cancelled = true; };
    Check(!cancelledService.SetAutostart(true) && cancelledStartup.writes == 0 &&
          cancelledService.State().error == "cancelled", "cancelled registry preflight cannot write Run");

    Fixture disabledUpdates;
    Check(disabledUpdates.service.Load() && disabledUpdates.service.BeginCheck(), "load compatible update fixture");
    disabledUpdates.service.FinishCheck({{"available", "1.0.8", 0}, {}});
    Check(disabledUpdates.service.State().update.status == "available",
          "compatible release remains available for the native downloader");
    disabledUpdates.service.UnsupportedUpdate();
    Check(disabledUpdates.service.State().error == "update-disabled", "compatible downloads are still deliberately disabled");
    disabledUpdates.service.UnsupportedUpdate();
    Check(disabledUpdates.service.State().error == "update-disabled", "repeated disabled operations do not mislabel a compatible source");
    const auto jsonText = SerializeSnapshot(startup.service.State());
    const auto json = nlohmann::json::parse(jsonText);
    Check(json.size() == 11 && json.at("version") == 1 && json.at("type") == "app-settings-state" &&
          json.at("revision").is_number_unsigned() && json.at("update").size() == 3 &&
          json.at("dataDirectory").is_string() && json.at("wallpaperDirectory").is_string(),
          "fixed public snapshot contract");
    std::cout << "application settings service passed\n";
}
