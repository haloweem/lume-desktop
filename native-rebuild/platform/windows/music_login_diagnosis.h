#pragma once
#include <string>

namespace lume::platform::windows {

// Login self-check modelled on Folia's triage verdicts: probe the same things a
// failing sign-in would touch and name the one that broke, so the page can show
// "IPv6 failed, IPv4 worked" instead of a generic network error.
enum class LoginVerdict {
    Ok,
    DnsFailed,     // no A or AAAA record
    Ipv6Failed,    // AAAA exists and IPv6 cannot connect, IPv4 can
    Ipv4Failed,    // no usable IPv4 connection
    Timeout,       // TCP 443 timed out
    Proxy,         // a system/environment proxy or a TUN fake IP sits on the path
};

// `host` is the provider's login host (e.g. ssl.ptlogin2.qq.com).
LoginVerdict DiagnoseLoginHost(const std::string& host);
// Stable token used in the page error message, "" for Ok.
std::string LoginVerdictToken(LoginVerdict verdict);

}  // namespace lume::platform::windows
