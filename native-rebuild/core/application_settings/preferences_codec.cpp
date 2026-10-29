#include "core/application_settings/application_settings.h"
#include "core/application_settings/json_codec.h"
#include <algorithm>
#include <cstring>

namespace lume::application_settings {
DecodedPreferences DecodePreferences(std::string_view bytes) noexcept {
    try {
        const auto json = detail::ParseObject(bytes);
        if (!json.contains("version") || !json["version"].is_number_integer())
            return {{}, "preferences-invalid"};
        // Version 2 adds wallpaperDirectory; version 1 files still load with it empty.
        if (json["version"] != 1 && json["version"] != 2) return {{}, "preferences-version"};
        const bool hasDirectory = json.contains("wallpaperDirectory");
        if (json.size() != (hasDirectory ? 5u : 4u) || !json.at("language").is_string() ||
            !json.at("balancedMode").is_boolean() || !json.at("autostart").is_boolean())
            return {{}, "preferences-invalid"};
        Preferences preferences{json["language"].get<std::string>(),
                                json["balancedMode"].get<bool>(), json["autostart"].get<bool>()};
        if (hasDirectory) {
            const auto& directory = json["wallpaperDirectory"];
            if (!directory.is_string()) return {{}, "preferences-invalid"};
            preferences.wallpaperDirectory = directory.get<std::string>();
            if (preferences.wallpaperDirectory.size() > 260) return {{}, "preferences-invalid"};
            for (const auto c : preferences.wallpaperDirectory)
                if (static_cast<unsigned char>(c) < 32) return {{}, "preferences-invalid"};
        }
        if (preferences.language != "zh-CN" && preferences.language != "en")
            return {{}, "preferences-invalid"};
        return {preferences, {}};
    } catch (...) { return {{}, "preferences-invalid"}; }
}
std::string EncodePreferences(const Preferences& preferences) {
    return nlohmann::json{{"version", 2}, {"language", preferences.language},
        {"balancedMode", preferences.balancedMode}, {"autostart", preferences.autostart},
        {"wallpaperDirectory", preferences.wallpaperDirectory}}.dump();
}
std::wstring SerializeSnapshot(const Snapshot& state) {
    const auto json = nlohmann::json{{"version", 1}, {"type", "app-settings-state"},
        {"revision", state.revision}, {"language", state.preferences.language},
        {"balancedMode", state.preferences.balancedMode}, {"autostart", state.preferences.autostart},
        {"dataDirectory", state.dataDirectory}, {"wallpaperDirectory", state.wallpaperDirectory},
        {"busy", state.busy}, {"error", state.error}, {"update", {
            {"status", state.update.status}, {"version", state.update.version},
            {"progress", std::clamp(state.update.progress, 0, 100)}}}}.dump(-1, ' ', true);
    return {json.begin(), json.end()};
}
std::optional<std::wstring> StartupCommand(std::wstring_view executable) noexcept {
    try {
        if (executable.size() < 4 || executable.size() > 32760) return {};
        const bool drive = ((executable[0] >= L'A' && executable[0] <= L'Z') ||
                            (executable[0] >= L'a' && executable[0] <= L'z')) &&
                           executable[1] == L':' && (executable[2] == L'\\' || executable[2] == L'/');
        const bool unc = executable.size() > 4 && executable.substr(0, 2) == L"\\\\";
        if (!drive && !unc) return {};
        for (const auto c : executable) if (c < 32 || c == L'"') return {};
        auto command = L"\"" + std::wstring(executable) + L"\" --tray-only";
        if (command.size() > 260) return {};
        return command;
    } catch (...) { return {}; }
}
StartupEntry EnabledStartupEntry(std::wstring_view command) {
    StartupEntry entry{true, 1, std::vector<std::uint8_t>((command.size() + 1) * sizeof(wchar_t), 0)};
    std::memcpy(entry.bytes.data(), command.data(), command.size() * sizeof(wchar_t));
    return entry;
}
bool SameStartupEntry(const StartupEntry& left, const StartupEntry& right) noexcept {
    return left.exists == right.exists && (!left.exists || (left.type == right.type && left.bytes == right.bytes));
}
namespace {
bool SamePath(std::wstring_view left, std::wstring_view right) noexcept {
    const auto normalize = [](wchar_t c) {
        if (c == L'/') return L'\\';
        return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
    };
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
        [&](wchar_t a, wchar_t b) { return normalize(a) == normalize(b); });
}
}
StartupEntryKind ClassifyStartupEntry(const StartupEntry& entry, std::wstring_view executable) noexcept {
    if (!entry.exists) return StartupEntryKind::Missing;
    try {
        if (entry.type != 1 || entry.bytes.empty() || entry.bytes.size() % sizeof(wchar_t) != 0)
            return StartupEntryKind::Foreign;
        std::wstring command(entry.bytes.size() / sizeof(wchar_t), L'\0');
        std::memcpy(command.data(), entry.bytes.data(), entry.bytes.size());
        if (command.back() != L'\0') return StartupEntryKind::Foreign;
        command.pop_back();
        if (command.empty() || command.front() != L'"') return StartupEntryKind::Foreign;
        const auto closing = command.find(L'"', 1);
        if (closing == std::wstring::npos) return StartupEntryKind::Foreign;
        const std::wstring_view path(command.data() + 1, closing - 1);
        const std::wstring_view arguments(command.data() + closing + 1, command.size() - closing - 1);
        if (!StartupCommand(path)) return StartupEntryKind::Foreign;
        if (arguments == L" --tray-only" && SamePath(path, executable)) return StartupEntryKind::Current;
        const auto directoryEnd = path.find_last_of(L"\\/");
        const auto name = path.substr(directoryEnd + 1);
        // 只接受已核实的旧版文件名及单短横线参数，不猜测其他历史命令。
        if (SamePath(name, L"lume.exe") && arguments == L" -tray-only") {
            const auto executableEnd = executable.find_last_of(L"\\/");
            if (executableEnd != std::wstring_view::npos &&
                SamePath(path.substr(0, directoryEnd + 1), executable.substr(0, executableEnd + 1)))
                return StartupEntryKind::Legacy;
            return StartupEntryKind::OtherInstallation;
        }
        if (SamePath(name, L"LumeShell.exe") && arguments == L" --tray-only")
            return StartupEntryKind::OtherInstallation;
    } catch (...) {}
    return StartupEntryKind::Foreign;
}
}
