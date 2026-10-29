#include <iostream>
#include "platform/windows/account/credentials.h"
#include "unique_test_directory.h"
#include <fstream>
#include <cstdlib>
using namespace lume::account;
using namespace lume::platform::windows::account;
namespace {
void Expect(bool ok, const char* why) { if (!ok) { std::cerr << why << '\n'; std::exit(1); } }
}
int main() {
    const auto root = UniqueTestDirectory(std::filesystem::path(LUME_ACCOUNT_TEST_ARTIFACTS), "lume-account-test-");
    DpapiCredentials first((root / "one").u8string()), second((root / "two").u8string());
    Expect(!first.Load() && !second.Load(), "empty isolated stores");
    Credentials saved{"fixture-refresh-secret", "fixture-subject", "fixture-nonce"};
    auto device = first.DeviceId();
    Expect(device.size() == 36 && device[14] == '4' && std::string("89ab").find(device[19]) != std::string::npos &&
        device.find_first_not_of("0123456789abcdef-") == std::string::npos, "legacy-compatible CNG UUID-v4 device id");
    Expect(!device.empty() && first.DeviceId() == device && second.DeviceId() != device, "stable isolated device id");
    first.Save(saved);
    auto loaded = first.Load();
    Expect(loaded && loaded->refreshToken == saved.refreshToken && loaded->subject == saved.subject &&
        loaded->nonce == saved.nonce && !second.Load(), "DPAPI roundtrip isolation");
    auto path = root / "one" / "account.credentials";
    std::ifstream file(path, std::ios::binary);
    std::string cipher((std::istreambuf_iterator<char>(file)), {});
    file.close();
    Expect(cipher.find(saved.refreshToken) == std::string::npos, "no plaintext refresh token on disk");
    std::filesystem::copy_file(path, root / "two" / "account.credentials");
    bool rejected = false;
    try { second.Load(); } catch (const Failure& e) { rejected = e.code == Error::CredentialCorrupt; }
    Expect(rejected, "cipher bound to storage location");
    {
        auto corrupt = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        Expect(corrupt != INVALID_HANDLE_VALUE, "open encrypted fixture for corruption");
        const char text[] = "corrupt fixture"; DWORD written = 0;
        Expect(WriteFile(corrupt, text, sizeof(text), &written, nullptr) && SetEndOfFile(corrupt),
            "corrupt fixture bytes actually written");
        CloseHandle(corrupt);
    }
    rejected = false;
    try { first.Load(); } catch (const Failure& e) { rejected = e.code == Error::CredentialCorrupt; }
    Expect(rejected, "corruption rejected, no silent successful restore");
    first.Clear(); Expect(!first.Load() && first.DeviceId() == device, "clear credentials keeps device identity");
    const auto exhausted = [&](DpapiCredentials& target, std::int64_t day, std::string_view subject, unsigned limit) {
        try { target.ReserveWallpaperDownload(day, subject, limit); }
        catch (const Failure& e) { return e.code == Error::WallpaperQuota; }
        return false;
    };
    // A guest has ten downloads in total on this device, kept across restarts
    // and days; signed-in use does not spend them.
    DpapiCredentials restarted((root / "one").u8string());
    for (unsigned i = 0; i < GuestWallpaperTotalLimit - 1; ++i)
        (i % 2 ? first : restarted).ReserveWallpaperDownload(20000 + i / 3, {}, GuestWallpaperTotalLimit);
    for (unsigned i = 0; i < 12; ++i)
        first.ReserveWallpaperDownload(20004, "member", SignedInWallpaperReservationLimit);
    restarted.ReserveWallpaperDownload(20004, {}, GuestWallpaperTotalLimit);
    Expect(exhausted(first, 20004, {}, GuestWallpaperTotalLimit), "eleventh guest download blocked across restart and days");
    Expect(exhausted(restarted, 20100, {}, GuestWallpaperTotalLimit), "a later day does not restore the guest total");
    Expect(exhausted(first, 19000, {}, GuestWallpaperTotalLimit), "rolling the clock back does not restore it either");
    first.ReleaseWallpaperDownload(20200, {});
    restarted.ReserveWallpaperDownload(20200, {}, GuestWallpaperTotalLimit);
    Expect(exhausted(first, 20200, {}, GuestWallpaperTotalLimit), "a failed admission refunds exactly one guest download");
    first.Clear();
    Expect(exhausted(restarted, 20200, {}, GuestWallpaperTotalLimit), "clearing credentials keeps the guest total");
    first.ReserveWallpaperDownload(20200, "member", SignedInWallpaperReservationLimit);
    // Signed-in reservations defer the final tier limit to the server.
    for (unsigned i = 0; i < 12; ++i)
        first.ReserveWallpaperDownload(20002, "member-" + std::to_string(i), SignedInWallpaperReservationLimit);
    for (unsigned i = 0; i < 12; ++i)
        restarted.ReleaseWallpaperDownload(20002, "member-" + std::to_string(i));
    bool invalidLimit = false;
    try { first.ReserveWallpaperDownload(20203, {}, SignedInWallpaperReservationLimit); }
    catch (const Failure& e) { invalidLimit = e.code == Error::InvalidCommand; }
    Expect(invalidLimit, "signed-in reservation limit cannot enlarge guest allowance");
    std::filesystem::create_directory(path);
    bool saveFailed = false;
    try { first.Save(saved); } catch (const Failure& e) { saveFailed = e.code == Error::Storage; }
    Expect(saveFailed, "atomic replace failure reported");
    // Exact directory was uniquely allocated above; no user data enters this test.
    std::filesystem::remove_all(root);
    std::cout << "account DPAPI roundtrip, corruption, isolation and save failure passed\n";
}
