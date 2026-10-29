#include "platform/windows/music_provider_credentials.h"
#include "unique_test_directory.h"
#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace lume::music::auth;
namespace {
void Expect(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
template<class F> void Rejected(F action, const char* message) {
    try { action(); } catch (const std::runtime_error& error) {
        Expect(std::string(error.what()) == "music credential storage error", "storage error must not expose secrets or paths");
        return;
    }
    Expect(false, message);
}
std::filesystem::path File(const std::filesystem::path& root, Provider provider) {
    return root / (std::string("music-") + Name(provider) + ".credentials");
}
std::string Read(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void Write(const std::filesystem::path& file, std::string_view value) {
    const auto output = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    Expect(output != INVALID_HANDLE_VALUE, "open isolated fixture");
    DWORD written = 0;
    const bool ok = WriteFile(output, value.data(), static_cast<DWORD>(value.size()), &written, nullptr) &&
        written == value.size() && SetEndOfFile(output);
    CloseHandle(output);
    Expect(ok, "write isolated fixture");
}
}  // namespace
int main() {
    const auto root = UniqueTestDirectory(std::filesystem::temp_directory_path(), "lume-music-credentials-test-");
    const auto one = root / "one", two = root / "two";
    const Json fixture{{"token", "fixture-only-secret"}, {"uid", "fixture-user"}, {"device", {{"id", "fixture-device"}}}};
    Expect(LoadCredentials(one, Provider::Netease).empty(), "missing store is signed out");
    ClearCredentials(one, Provider::Netease);
    Expect(!std::filesystem::exists(one), "missing load and clear do not create directories");
    for (const auto provider : {Provider::Netease, Provider::QQ, Provider::Kugou}) {
        SaveCredentials(one, provider, fixture);
        Expect(LoadCredentials(one, provider) == fixture, "provider DPAPI roundtrip");
        const auto cipher = Read(File(one, provider));
        Expect(!cipher.empty() && cipher.size() <= 64 * 1024 &&
            cipher.find("fixture-only-secret") == cipher.npos, "bounded ciphertext, no plaintext");
    }
    auto updated = fixture;
    updated["token"] = "fixture-updated";
    SaveCredentials(one, Provider::Netease, updated);
    Expect(LoadCredentials(one, Provider::Netease) == updated, "atomic replace updates credentials");
    Expect(LoadCredentials(one, Provider::QQ) == fixture, "provider update isolated");
    std::filesystem::create_directory(two);
    std::filesystem::copy_file(File(one, Provider::Netease), File(two, Provider::Netease));
    Rejected([&] { LoadCredentials(two, Provider::Netease); }, "DPAPI is path-bound");
    std::filesystem::copy_file(File(one, Provider::Netease), File(two, Provider::QQ));
    Rejected([&] { LoadCredentials(two, Provider::QQ); }, "DPAPI is provider-bound");
    ClearCredentials(one, Provider::QQ);
    std::filesystem::copy_file(File(one, Provider::Netease), File(one, Provider::QQ));
    Rejected([&] { LoadCredentials(one, Provider::QQ); }, "provider rename rejected in same directory");
    SaveCredentials(one, Provider::QQ, fixture);
    Rejected([&] { SaveCredentials(one, Provider::Netease, Json{{"token", std::string(65536, 'x')}}); },
        "oversized secret rejected");
    Rejected([&] { SaveCredentials(one, Provider::Netease, Json::array()); }, "nonobject secrets rejected");
    Expect(LoadCredentials(one, Provider::Netease) == updated, "failed saves preserve existing credentials");
    Write(File(two, Provider::Netease), std::string(65537, 'x'));
    Rejected([&] { LoadCredentials(two, Provider::Netease); }, "oversized cipher rejected before DPAPI");
    Write(File(two, Provider::Netease), "corrupt fixture");
    Rejected([&] { LoadCredentials(two, Provider::Netease); }, "corrupt cipher rejected");
    Write(File(two, Provider::Netease), "");
    Rejected([&] { LoadCredentials(two, Provider::Netease); }, "empty file is corruption, not signed out");
    Rejected([&] { LoadCredentials(one, static_cast<Provider>(99)); }, "invalid provider cannot choose file");
    Rejected([&] { SaveCredentials({}, Provider::QQ, fixture); }, "empty storage root rejected");
    Rejected([&] { LoadCredentials(root / "one:stream", Provider::QQ); }, "alternate data stream root rejected");
    Rejected([&] { LoadCredentials(std::filesystem::path(L"\\\\?\\") += one.wstring(), Provider::QQ); }, "device namespace rejected");
    ClearCredentials(two, Provider::Netease);
    ClearCredentials(two, Provider::QQ);
    const auto blocked = File(two, Provider::Kugou);
    std::filesystem::create_directory(blocked);
    Rejected([&] { SaveCredentials(two, Provider::Kugou, fixture); }, "directory destination cannot be replaced");
    Rejected([&] { LoadCredentials(two, Provider::Kugou); }, "directory cannot be read as credentials");
    Rejected([&] { ClearCredentials(two, Provider::Kugou); }, "directory cannot be cleared as credentials");
    std::filesystem::remove(blocked);
    const auto linked = root / "linked";
    const auto symlinkFlags = SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2;
    if (CreateSymbolicLinkW(linked.c_str(), one.c_str(), symlinkFlags)) {
        Rejected([&] { LoadCredentials(linked, Provider::Netease); }, "reparse root cannot load");
        Rejected([&] { SaveCredentials(linked, Provider::Netease, fixture); }, "reparse root cannot save");
        Rejected([&] { ClearCredentials(linked, Provider::Netease); }, "reparse root cannot clear");
        Rejected([&] { SaveCredentials(linked / "nested", Provider::Netease, fixture); }, "reparse ancestor cannot save");
        Expect(!std::filesystem::exists(one / "nested"), "reparse rejected before directory creation");
        std::filesystem::remove(linked);
    } else {
        std::cout << "directory symlink fixture unavailable; no symlink privilege\n";
    }
    if (CreateSymbolicLinkW(File(two, Provider::Netease).c_str(), File(one, Provider::Netease).c_str(), 0x2)) {
        Rejected([&] { LoadCredentials(two, Provider::Netease); }, "reparse file cannot load");
        Rejected([&] { SaveCredentials(two, Provider::Netease, fixture); }, "reparse file cannot save");
        Rejected([&] { ClearCredentials(two, Provider::Netease); }, "reparse file cannot clear");
        std::filesystem::remove(File(two, Provider::Netease));
    } else {
        std::cout << "file symlink fixture unavailable; no symlink privilege\n";
    }
    Expect(CreateHardLinkW(File(two, Provider::Netease).c_str(), File(one, Provider::Netease).c_str(), nullptr),
        "create isolated hardlink fixture");
    Rejected([&] { LoadCredentials(two, Provider::Netease); }, "hardlink cannot load");
    Rejected([&] { SaveCredentials(two, Provider::Netease, fixture); }, "hardlink cannot save");
    Rejected([&] { ClearCredentials(two, Provider::Netease); }, "hardlink cannot clear");
    std::filesystem::remove(File(two, Provider::Netease));
    for (const auto& entry : std::filesystem::directory_iterator(one))
        Expect(entry.path().extension() != ".tmp", "failed operations leave no temporary files");
    ClearCredentials(one, Provider::Netease);
    ClearCredentials(one, Provider::Netease);
    Expect(LoadCredentials(one, Provider::Netease).empty() && LoadCredentials(one, Provider::QQ) == fixture,
        "idempotent clear preserves other providers");
    // Only this test's uniquely reserved fixture directory is removed.
    std::filesystem::remove_all(root);
    std::cout << "music DPAPI isolation, corruption, bounds, atomic replace and reparse policy passed\n";
}
