#include "platform/windows/taskbar/taskbar_payload.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <string>
#include <vector>

namespace lume::platform::windows {
namespace {
struct Entry { const wchar_t* file; const char* x64; const char* arm64; };
constexpr Entry Files[] = {
    {L"Assets/SplashScreen.jpeg", "2E18F60892B528DE45B01A79DD07C45CBF5760539663F5FEA74115DEDB137CC4", "2E18F60892B528DE45B01A79DD07C45CBF5760539663F5FEA74115DEDB137CC4"},
    {L"Assets/SplashScreen.scale-400.jpeg", "4C9634AC99EF09BA7A503E269C6FE388323D9C20627E04C5DB339BBF5CA3B738", "4C9634AC99EF09BA7A503E269C6FE388323D9C20627E04C5DB339BBF5CA3B738"},
    {L"ExplorerHooks.dll", "392964D71B809EBF45B49A33D6372586846CDA004F9F561BEC74ED3CA4078BF2", "2B97943C32FD9729E2D5021DBD4A1E9AE0156E4180F33893DB85F23DA1EA5CD1"},
    {L"ExplorerTAP.dll", "86F65A4B341E767B364BF5172E297395672CB859BDCE0422B3D20823A3CD8696", "6D2156A1F1A80C862D414A6D27DAD5D78236704EB692BBECD46ECFD7D11E9191"},
    {L"LumeTaskbarCore.exe", "096AFD5603F4CB8DF088768AB31C6552A73452E6C51935A60C9A873421D56841", "14C5C17BC20897155C821CA43D563DD2410880C4CF2DD0C1C0A392030586900E"},
    {L"ProgramLog.dll", "B352DC8EEA5042A1A0FA46FDE9185BF3EDC1DA5A45B458F3126EE8749529EF16", "4586AD8EB3D3EB31AB87CA2AAD73DD48B96E73FDA48AF75728F4FF51858A0EAE"},
    {L"resources.pri", "18E547BCD38F54C8380BAF8A1781347262DDCAC480C6D27D1BFC7489C23257AC", "18E547BCD38F54C8380BAF8A1781347262DDCAC480C6D27D1BFC7489C23257AC"},
    {L"Xaml.dll", "3AAECBF8347ADC2D217F38448DD73E46B076F0A1414BFA114331F96681144EAC", "0C7B777B3996003585DAA197714B604AE0C034BA795158BE7A6057FEFC90070E"},
    {L"msvcp140.dll", "0F885B509A685D2BBFA652FED26B5FB31D88FBDAB0A978C641D1C7B8AA460AA9", "045FAEAB0B5710816AE160EA20DAE8495FFC11BB51FECA15D22AAF7FF3752CB3"},
    {L"msvcp140_atomic_wait.dll", "640B2AEFCED484D0368EEA5BDD06ADDD0658A3A70A49256E560D6923B404A479", "4384E35592B7BE1F0B80718EBA2F779060D73C3E232EB1BFADF8A4CB9B14601F"},
    {L"vcruntime140.dll", "D5E4D9A3E835FA679450145D6A7D94E36573A509317111904D9B3712C30D9066", "D8A8513921544569837E400D37CC71302819967AE6DEFA932266A7ECB41DBAF9"},
    {L"vcruntime140_1.dll", "1F2D41C4AA5DB0BC33EBF7B66D72943A817D7CE6CBE880502A9403823633093F", nullptr}
};
constexpr auto ManifestHash = "81A583536398F0468C96AB37619741401808F214C41755A5F482F829DCA567B1";
constexpr auto SettingsHash = "A315CA5250A566015ECCC0FE6D7E21C2B4EF571E70B878667C85F3E6D4054AAA";
bool NoReparse(const std::filesystem::path& path) {
    if (!path.is_absolute()) return false;
    auto current = path.root_path();
    for (const auto& component : path.relative_path()) {
        if (component == L".." || component == L".") return false;
        current /= component;
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    return true;
}
bool HashMatches(const std::filesystem::path& path, const char* expected) {
    if (!NoReparse(path)) return false;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const auto length = input.tellg();
    if (length < 0 || length > 32 * 1024 * 1024) return false;
    input.seekg(0);
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    DWORD size = 0, copied = 0;
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                               sizeof(size), &copied, 0) >= 0;
    std::vector<unsigned char> object(ok ? size : 0);
    if (ok) ok = BCryptCreateHash(algorithm, &hash, object.data(), size, nullptr, 0, 0) >= 0;
    std::array<char, 65536> bytes{};
    while (ok && input) {
        input.read(bytes.data(), bytes.size());
        if (input.gcount() > 0)
            ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(bytes.data()),
                                static_cast<ULONG>(input.gcount()), 0) >= 0;
    }
    if (!input.eof()) ok = false;
    std::array<unsigned char, 32> digest{};
    if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return false;
    std::string actual; actual.reserve(64);
    for (const auto value : digest) {
        actual.push_back("0123456789ABCDEF"[value >> 4]);
        actual.push_back("0123456789ABCDEF"[value & 15]);
    }
    return actual == expected;
}
bool StageFile(const std::filesystem::path& source, const std::filesystem::path& target, const char* hash) {
    if (!NoReparse(source) || !NoReparse(target)) return false;
    if (HashMatches(target, hash)) return true;
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    if (error || !NoReparse(target.parent_path())) return false;
    const auto temporary = std::filesystem::path(target.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    if (!NoReparse(temporary) || !CopyFileW(source.c_str(), temporary.c_str(), FALSE)) return false;
    const bool ok = HashMatches(temporary, hash) &&
        MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}
}  // namespace
taskbar::Error VerifyTaskbarPayload(const std::filesystem::path& root, bool arm64) {
    if (root.empty() || !NoReparse(root)) return taskbar::Error::MissingPayload;
    if (!HashMatches(root / L"SHA256SUMS.txt", ManifestHash) ||
        !HashMatches(root / L"settings.json", SettingsHash)) return taskbar::Error::PayloadIntegrity;
    for (const auto& file : Files) {
        const auto hash = arm64 ? file.arm64 : file.x64;
        if (hash && !HashMatches(root / (arm64 ? L"arm64" : L"x64") / file.file, hash))
            return taskbar::Error::PayloadIntegrity;
    }
    return taskbar::Error::None;
}
taskbar::Error StageTaskbarPayload(const TaskbarPaths& paths, bool arm64,
                                  const std::atomic_bool& cancel,
                                  std::filesystem::path& executable) {
    if (cancel) return taskbar::Error::Cancelled;
    const auto verified = VerifyTaskbarPayload(paths.payloadRoot, arm64);
    if (verified != taskbar::Error::None) return verified;
    if (paths.cacheRoot.empty() || !NoReparse(paths.cacheRoot)) return taskbar::Error::MissingPayload;
    const auto architecture = arm64 ? L"arm64" : L"x64";
    const auto destination = paths.cacheRoot / L"2026.1" / architecture;
    for (const auto& file : Files) {
        if (cancel) return taskbar::Error::Cancelled;
        const auto hash = arm64 ? file.arm64 : file.x64;
        if (!hash) continue;
        if (!StageFile(paths.payloadRoot / architecture / file.file, destination / file.file,
                       hash)) return taskbar::Error::PayloadIntegrity;
    }
    if (!StageFile(paths.payloadRoot / L"settings.json", destination / L"settings.json", SettingsHash))
        return taskbar::Error::PayloadIntegrity;
    executable = destination / L"LumeTaskbarCore.exe";
    return taskbar::Error::None;
}
}  // namespace lume::platform::windows
