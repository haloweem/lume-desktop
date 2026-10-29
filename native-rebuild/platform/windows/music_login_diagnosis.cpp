#include "platform/windows/music_login_diagnosis.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <memory>

namespace lume::platform::windows {
namespace {
bool EnvSet(const wchar_t* name) {
    wchar_t value[8]{};
    return GetEnvironmentVariableW(name, value, static_cast<DWORD>(std::size(value))) > 0;
}
// Sign-in goes through WinHTTP with the system proxy settings, so those are what
// matter; the environment variables Folia checks are honoured by some VPN tools too.
bool HasProxy() {
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config{};
    bool system = false;
    if (WinHttpGetIEProxyConfigForCurrentUser(&config)) {
        system = config.lpszProxy || config.lpszAutoConfigUrl;
        for (auto* text : {config.lpszProxy, config.lpszProxyBypass, config.lpszAutoConfigUrl})
            if (text) GlobalFree(text);
    }
    return system || EnvSet(L"HTTPS_PROXY") || EnvSet(L"https_proxy") || EnvSet(L"HTTP_PROXY") ||
        EnvSet(L"http_proxy") || EnvSet(L"ALL_PROXY") || EnvSet(L"all_proxy");
}
using Addresses = std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>;
Addresses Resolve(const char* host, int family) {
    addrinfo hints{};
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host, "443", &hints, &result) != 0) result = nullptr;
    return {result, &freeaddrinfo};
}
// Clash/Surge-style TUN modes answer DNS with 198.18.0.0/15 fake IPs.
bool FakeIp(const addrinfo* address) {
    if (!address || address->ai_family != AF_INET) return false;
    const auto ip = ntohl(reinterpret_cast<const sockaddr_in*>(address->ai_addr)->sin_addr.s_addr);
    return (ip & 0xFFFE0000u) == 0xC6120000u;
}
// Non-blocking TCP connect with a 4s budget; the answer we need is "can this
// family reach the host at all", not a full request.
bool Connects(const addrinfo* address, bool& timedOut) {
    timedOut = false;
    SOCKET socket = ::socket(address->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) return false;
    u_long nonBlocking = 1;
    ioctlsocket(socket, FIONBIO, &nonBlocking);
    const int result = connect(socket, address->ai_addr, static_cast<int>(address->ai_addrlen));
    bool ok = false;
    if (result == 0) {
        ok = true;
    } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set writable{}, failed{};
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(socket, &writable);
        FD_SET(socket, &failed);
        timeval budget{4, 0};
        const int ready = select(0, nullptr, &writable, &failed, &budget);
        if (ready > 0 && FD_ISSET(socket, &writable)) ok = true;
        else if (ready == 0) timedOut = true;
    }
    closesocket(socket);
    return ok;
}
LoginVerdict Diagnose(const std::string& host) {
    const auto ipv4 = Resolve(host.c_str(), AF_INET);
    const auto ipv6 = Resolve(host.c_str(), AF_INET6);
    if (!ipv4 && !ipv6) return LoginVerdict::DnsFailed;
    const bool proxy = HasProxy() || FakeIp(ipv4.get());
    bool timedOut = false;
    const bool v4 = ipv4 && Connects(ipv4.get(), timedOut);
    bool v6 = false;
    if (ipv6) {
        bool v6TimedOut = false;
        v6 = Connects(ipv6.get(), v6TimedOut);
    }
    // A direct path that works while sign-in failed points at the proxy in between;
    // a broken direct path behind a proxy is most likely the proxy as well.
    if (proxy) return LoginVerdict::Proxy;
    if (!v4 && !v6 && timedOut) return LoginVerdict::Timeout;
    // Folia's rule: IPv6 that cannot connect while IPv4 can is its own verdict.
    if (v4 && ipv6 && !v6) return LoginVerdict::Ipv6Failed;
    if (!v4 && !v6) return LoginVerdict::Ipv4Failed;
    return LoginVerdict::Ok;
}
}  // namespace

LoginVerdict DiagnoseLoginHost(const std::string& host) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return LoginVerdict::Ok;
    const auto verdict = Diagnose(host);
    WSACleanup();
    return verdict;
}

std::string LoginVerdictToken(LoginVerdict verdict) {
    switch (verdict) {
    case LoginVerdict::DnsFailed: return "dns-failed";
    case LoginVerdict::Ipv6Failed: return "ipv6-failed";
    case LoginVerdict::Ipv4Failed: return "ipv4-failed";
    case LoginVerdict::Timeout: return "timeout";
    case LoginVerdict::Proxy: return "proxy";
    default: return {};
    }
}

}  // namespace lume::platform::windows
