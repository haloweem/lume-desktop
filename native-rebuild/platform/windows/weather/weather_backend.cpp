#include "platform/windows/weather/weather_backend.h"
#include "platform/windows/weather/weather_http.h"
#include "platform/windows/weather/weather_timezone.h"
#include "core/weather/weather_location.h"
#include "core/weather/weather_places.h"
#include <locationapi.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.devices.geolocation.h>
#include <windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>
#include <vector>
#include <fstream>
namespace lume::platform::windows {
namespace {
using namespace weather;
template<class T> T LoadProc(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(address), "Windows function pointer ABI");
    std::memcpy(&result, &address, sizeof(result));
    return result;
}
// Preserve the existing COM fallback despite its deprecation in newer Windows SDKs.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4995)
#endif
std::optional<Location> LegacyLocation() {
    using Initialize = HRESULT(WINAPI*)(LPVOID, DWORD);
    using Uninitialize = void(WINAPI*)();
    using Create = HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
    const auto ole32 = LoadLibraryW(L"ole32.dll");
    if (!ole32) return {};
    const auto initialize = LoadProc<Initialize>(ole32, "CoInitializeEx");
    const auto uninitialize = LoadProc<Uninitialize>(ole32, "CoUninitialize");
    const auto create = LoadProc<Create>(ole32, "CoCreateInstance");
    if (!initialize || !uninitialize || !create) {
        FreeLibrary(ole32);
        return {};
    }
    const auto initialized = initialize(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        FreeLibrary(ole32);
        return {};
    }
    struct Apartment {
        HRESULT result;
        Uninitialize uninitialize;
        HMODULE module;
        ~Apartment() {
            if (SUCCEEDED(result)) uninitialize();
            FreeLibrary(module);
        }
    } apartment{initialized, uninitialize, ole32};
    const GUID locationClass{
        0xe5b8e079, 0xee6d, 0x4e33, {0xa4, 0x38, 0xc8, 0x7f, 0x2e, 0x95, 0x92, 0x54}};
    const GUID locationInterface{
        0xab2ece69, 0x56d9, 0x4f28, {0xb5, 0x25, 0xde, 0x1b, 0x0e, 0xe4, 0x42, 0x37}};
    const GUID latLongInterface{
        0x7fed806d, 0x0ef8, 0x4f07, {0x80, 0xac, 0x36, 0xa0, 0xbe, 0xae, 0x31, 0x34}};
    Microsoft::WRL::ComPtr<ILocation> location;
    if (FAILED(create(locationClass, nullptr, CLSCTX_INPROC_SERVER, locationInterface,
        reinterpret_cast<void**>(location.GetAddressOf())))) return {};
    ILocationReport* base = nullptr;
    if (FAILED(location->GetReport(latLongInterface, &base)) || !base) return {};
    Microsoft::WRL::ComPtr<ILocationReport> report;
    report.Attach(base);
    Microsoft::WRL::ComPtr<ILatLongReport> latLong;
    if (FAILED(report->QueryInterface(latLongInterface, reinterpret_cast<void**>(latLong.GetAddressOf()))))
        return {};
    double latitude = 0, longitude = 0;
    if (FAILED(latLong->GetLatitude(&latitude)) || FAILED(latLong->GetLongitude(&longitude)))
        return {};
    return Location{latitude, longitude, {}, {}};
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
// Windows.Devices.Geolocation positions from nearby Wi-Fi (or GPS), so a VPN
// or proxy cannot move it. Requires the system location service and "let
// desktop apps access your location". combase is loaded on demand, as ole32
// is above. Waits at most about 12 seconds on the weather worker.
std::optional<Location> GeolocatorLocation() {
    namespace geo = ABI::Windows::Devices::Geolocation;
    namespace foundation = ABI::Windows::Foundation;
    using Initialize = HRESULT(WINAPI*)(RO_INIT_TYPE);
    using Uninitialize = void(WINAPI*)();
    using Activate = HRESULT(WINAPI*)(HSTRING, IInspectable**);
    using CreateReference = HRESULT(WINAPI*)(PCWSTR, UINT32, HSTRING_HEADER*, HSTRING*);
    const auto combase = LoadLibraryW(L"combase.dll");
    if (!combase) return {};
    struct Module { HMODULE value; ~Module() { FreeLibrary(value); } } module{combase};
    const auto initialize = LoadProc<Initialize>(combase, "RoInitialize");
    const auto uninitialize = LoadProc<Uninitialize>(combase, "RoUninitialize");
    const auto activate = LoadProc<Activate>(combase, "RoActivateInstance");
    const auto reference = LoadProc<CreateReference>(combase, "WindowsCreateStringReference");
    if (!initialize || !uninitialize || !activate || !reference) return {};
    const auto initialized = initialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return {};
    struct Apartment {
        HRESULT result;
        Uninitialize uninitialize;
        ~Apartment() { if (SUCCEEDED(result)) uninitialize(); }
    } apartment{initialized, uninitialize};
    constexpr wchar_t className[] = L"Windows.Devices.Geolocation.Geolocator";
    HSTRING_HEADER header{};
    HSTRING name = nullptr;
    if (FAILED(reference(className, static_cast<UINT32>(std::size(className) - 1), &header, &name))) return {};
    Microsoft::WRL::ComPtr<IInspectable> instance;
    if (FAILED(activate(name, instance.GetAddressOf()))) return {};
    Microsoft::WRL::ComPtr<geo::IGeolocator> geolocator;
    if (FAILED(instance.As(&geolocator))) return {};
    geo::PositionStatus status{};
    if (SUCCEEDED(geolocator->get_LocationStatus(&status)) &&
        (status == geo::PositionStatus_Disabled || status == geo::PositionStatus_NotAvailable)) return {};
    foundation::TimeSpan maximumAge{}, timeout{};
    maximumAge.Duration = 30LL * 60 * 10000000;  // 100 ns units
    timeout.Duration = 10LL * 10000000;
    Microsoft::WRL::ComPtr<foundation::IAsyncOperation<geo::Geoposition*>> operation;
    if (FAILED(geolocator->GetGeopositionAsyncWithAgeAndTimeout(maximumAge, timeout, operation.GetAddressOf())))
        return {};
    Microsoft::WRL::ComPtr<foundation::IAsyncInfo> info;
    if (FAILED(operation.As(&info))) return {};
    auto state = foundation::AsyncStatus::Started;
    for (const auto deadline = GetTickCount64() + 12000; GetTickCount64() < deadline;) {
        if (FAILED(info->get_Status(&state)) || state != foundation::AsyncStatus::Started) break;
        Sleep(50);
    }
    if (state != foundation::AsyncStatus::Completed) { info->Cancel(); return {}; }
    Microsoft::WRL::ComPtr<geo::IGeoposition> position;
    Microsoft::WRL::ComPtr<geo::IGeocoordinate> coordinate;
    Microsoft::WRL::ComPtr<geo::IGeocoordinateWithPoint> withPoint;
    Microsoft::WRL::ComPtr<geo::IGeopoint> point;
    if (FAILED(operation->GetResults(position.GetAddressOf())) || !position ||
        FAILED(position->get_Coordinate(coordinate.GetAddressOf())) || !coordinate ||
        FAILED(coordinate.As(&withPoint)) || FAILED(withPoint->get_Point(point.GetAddressOf())) || !point)
        return {};
    geo::BasicGeoposition basic{};
    if (FAILED(point->get_Position(&basic))) return {};
    return Location{basic.Latitude, basic.Longitude, {}, {}};
}
std::optional<Location> SystemLocation() {
    if (auto location = GeolocatorLocation()) return location;
    return LegacyLocation();
}
// The computer's own countries: Windows region and the user's locale.
std::vector<std::string> SystemCountries() {
    std::vector<std::string> countries;
    const auto add = [&](std::wstring_view code) {
        if (code.size() != 2) return;
        std::string text;
        for (const auto c : code) {
            if (c < L'A' || c > L'Z') return;
            text.push_back(static_cast<char>(c));
        }
        if (std::find(countries.begin(), countries.end(), text) == countries.end()) countries.push_back(text);
    };
    wchar_t region[16]{};
    if (GetUserDefaultGeoName(region, static_cast<int>(std::size(region))) > 0) add(region);
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0) {
        const std::wstring_view name(locale);
        const auto dash = name.rfind(L'-');
        if (dash != std::wstring_view::npos) add(name.substr(dash + 1));
    }
    return countries;
}
// System wall time at a UTC instant, as YYYY-MM-DDTHH:mm.
std::optional<std::string> SystemLocalTime(Millis utc) {
    ULARGE_INTEGER ticks{};
    ticks.QuadPart = static_cast<ULONGLONG>(utc + 11644473600000LL) * 10000ULL;
    FILETIME file{ticks.LowPart, ticks.HighPart};
    SYSTEMTIME universal{}, local{};
    if (!FileTimeToSystemTime(&file, &universal) || !SystemTimeToTzSpecificLocalTime(nullptr, &universal, &local))
        return {};
    char text[32]{};
    std::snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u", local.wYear, local.wMonth, local.wDay,
                  local.wHour, local.wMinute);
    return std::string(text);
}
constexpr std::size_t RawCacheMax = 8 * ProviderMaxBytes + 16384;
std::size_t Limit(WeatherCacheKind kind) {
    if (kind == WeatherCacheKind::Location) return 4096;
    return kind == WeatherCacheKind::Display ? SnapshotMaxBytes : RawCacheMax;
}
const wchar_t* Filename(WeatherCacheKind kind) {
    if (kind == WeatherCacheKind::Location) return L"weather-location.json";
    return kind == WeatherCacheKind::Display ? L"weather-display.json" : L"weather-providers.json";
}
// Only an in-process unchanged-write optimization, not an integrity check.
struct Fingerprint {
    std::size_t size;
    std::uint64_t hash;
    bool operator==(const Fingerprint& other) const { return size == other.size && hash == other.hash; }
};
Fingerprint Signature(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : text) { hash ^= c; hash *= 1099511628211ULL; }
    return {text.size(), hash};
}
void Simplify(Snapshot& snapshot) {
    snapshot.cityName = SimplifyWeatherPlaceName(snapshot.cityName);
    snapshot.location.name = SimplifyWeatherPlaceName(snapshot.location.name);
    snapshot.condition = SimplifyWeatherPlaceName(snapshot.condition);
    for (auto& hour : snapshot.hourly) hour.condition = SimplifyWeatherPlaceName(hour.condition);
    for (auto& day : snapshot.forecast) day.condition = SimplifyWeatherPlaceName(day.condition);
}
class NativeBackend final : public IWeatherBackend {
public:
    explicit NativeBackend(WeatherBackendOptions options)
        : cache_(std::move(options.cache)),
          clock_(options.clock ? std::move(options.clock) : Clock{WeatherNow}),
          local_(options.local ? std::move(options.local) : LocalTime{[this](Millis time, std::string_view zone) {
              if (!timezone_) timezone_ = std::make_unique<WeatherTimezone>();
              return timezone_->Local(time, zone);
          }}),
          transport_(options.transport ? std::move(options.transport) :
              HttpTransport{[this](const auto& request, const auto& cancel) { return http_.Get(request, cancel); }}),
          location_(transport_, clock_, local_, SimplifyWeatherPlaceName,
              options.systemLocation ? DeviceLocation{[](Millis) { return SystemLocation(); }} : DeviceLocation{},
              options.environment ? *options.environment : LocationEnvironment{SystemCountries(), SystemLocalTime},
              LocationStore{[this] { return Read(WeatherCacheKind::Location); },
                            [this](std::string_view text) { return Write(WeatherCacheKind::Location, text); }}),
          provider_(transport_, clock_, local_) {
        if (const auto raw = Read(WeatherCacheKind::Provider)) provider_.RestoreCache(*raw);
    }
    std::optional<Snapshot> Load(std::string_view city) override {
        if (!ValidCity(city)) return {};
        const auto text = Read(WeatherCacheKind::Display);
        if (!text) return {};
        auto snapshot = DecodeCache(*text, city, clock_());
        if (snapshot) {
            Simplify(*snapshot);
            if (!ValidSnapshot(*snapshot)) return {};
        }
        return snapshot;
    }
    WeatherFetch Fetch(std::string_view city, const Cancellation& cancel) override {
        if (cancel.Cancelled()) return {{{}, ProviderError::Cancelled, 0}, false};
        const auto position = location_.Resolve(city, cancel);
        if (!position.location) return {{{}, position.error, 0}, false};
        auto result = provider_.Fetch(city, *position.location, cancel);
        if (cancel.Cancelled()) return {{{}, ProviderError::Cancelled, 0}, false};
        if (result.snapshot) {
            Simplify(*result.snapshot);
            if (!ValidSnapshot(*result.snapshot)) {
                result.snapshot.reset();
                result.error = ProviderError::Unavailable;
            }
        }
        bool saved = true;
        if (result.snapshot && cache_.write) {
            const auto display = EncodeCache(*result.snapshot);
            saved = display && Write(WeatherCacheKind::Display, *display);
        }
        if (!cancel.Cancelled() && cache_.write) {
            const auto raw = provider_.SaveCache();
            if (!raw || !Write(WeatherCacheKind::Provider, *raw)) saved = false;
        }
        return {std::move(result), !saved};
    }
    void InvalidateLocation() override { location_.Invalidate(); }
private:
    std::optional<std::string> Read(WeatherCacheKind kind) noexcept {
        try {
            if (!cache_.read) return {};
            auto text = cache_.read(kind, Limit(kind));
            if (!text || text->size() > Limit(kind)) return {};
            signatures_[static_cast<std::size_t>(kind)] = Signature(*text);
            return text;
        } catch (...) { return {}; }
    }
    bool Write(WeatherCacheKind kind, std::string_view text) noexcept {
        try {
            if (text.size() > Limit(kind)) return false;
            const auto signature = Signature(text);
            auto& previous = signatures_[static_cast<std::size_t>(kind)];
            if (previous && *previous == signature) return true;
            if (!cache_.write(kind, text)) return false;
            previous = signature;
            return true;
        } catch (...) { return false; }
    }
    WeatherCachePort cache_;
    Clock clock_;
    std::unique_ptr<WeatherTimezone> timezone_;
    WeatherHttp http_;
    LocalTime local_;
    HttpTransport transport_;
    WeatherLocation location_;
    WeatherProvider provider_;
    std::array<std::optional<Fingerprint>, 3> signatures_{};
};
}
WeatherCachePort LocalWeatherCache(std::filesystem::path directory) {
    return {
        [directory](WeatherCacheKind kind, std::size_t bound) -> std::optional<std::string> {
            if (directory.empty()) return {};
            bound = std::min(bound, Limit(kind));
            std::ifstream input(directory / Filename(kind), std::ios::binary);
            if (!input) return {};
            std::array<char, 16384> buffer{};
            std::string text;
            while (input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto read = static_cast<std::size_t>(input.gcount());
                if (read > bound - text.size()) return {};
                text.append(buffer.data(), read);
            }
            if (!input.eof()) return {};
            return text;
        },
        [directory](WeatherCacheKind kind, std::string_view text) {
            if (directory.empty() || text.size() > Limit(kind)) return false;
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error) return false;
            const auto target = directory / Filename(kind);
            static std::atomic<unsigned> sequence{0};
            const auto temporary = target.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId()) +
                L"-" + std::to_wstring(++sequence);
            const auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE) return false;
            DWORD written = 0;
            bool success = WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                written == text.size() && FlushFileBuffers(handle);
            if (!CloseHandle(handle)) success = false;
            if (success) success = MoveFileExW(temporary.c_str(), target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
            if (!success) DeleteFileW(temporary.c_str());
            return success;
        }
    };
}
weather::Millis WeatherNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
std::unique_ptr<IWeatherBackend> CreateWeatherBackend(WeatherBackendOptions options) {
    return std::make_unique<NativeBackend>(std::move(options));
}
}
