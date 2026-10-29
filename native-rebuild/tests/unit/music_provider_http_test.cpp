#include "platform/windows/music_provider_http.h"
#include <chrono>
#include <cstdlib>
#include <iostream>

using namespace lume::music::auth;
namespace {
void Expect(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
template<class F> void Rejected(F action, const char* message) {
    try { action(); } catch (const std::runtime_error&) { return; }
    Expect(false, message);
}
std::string Hex(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output;
    for (unsigned char c : value) { output += digits[c >> 4]; output += digits[c & 15]; }
    return output;
}
void Policy() {
    struct Allowed { Provider provider; const char* method; const char* url; };
    const Allowed allowed[] = {
        {Provider::Netease, "POST", "https://music.163.com/weapi/login/qrcode/unikey"},
        {Provider::Netease, "POST", "https://music.163.com/weapi/login/qrcode/client/login"},
        {Provider::Netease, "POST", "https://music.163.com/weapi/nuser/account/get"},
        {Provider::Netease, "POST", "https://music.163.com/weapi/w/nuser/account/get"},
        {Provider::Netease, "POST", "https://music.163.com/api/login/qrcode/unikey"},
        {Provider::Netease, "POST", "https://music.163.com/api/login/qrcode/client/login"},
        {Provider::Netease, "POST", "https://music.163.com/api/nuser/account/get"},
        {Provider::Netease, "POST", "https://music.163.com/api/w/nuser/account/get"},
        {Provider::Netease, "POST", "https://interfacepc.music.163.com/eapi/login/qrcode/unikey"},
        {Provider::Netease, "POST", "https://interfacepc.music.163.com/eapi/login/qrcode/client/login"},
        {Provider::QQ, "GET", "https://ssl.ptlogin2.qq.com/ptqrshow?appid=716027609"},
        {Provider::QQ, "GET", "https://ssl.ptlogin2.qq.com/ptqrlogin"},
        {Provider::QQ, "GET", "https://ssl.ptlogin2.graph.qq.com/check_sig"},
        {Provider::QQ, "POST", "https://graph.qq.com/oauth2.0/authorize"},
        {Provider::QQ, "POST", "https://api.tencentmusic.com/tme/trpc/proxy"},
        {Provider::QQ, "POST", "https://u.y.qq.com/cgi-bin/musicu.fcg"},
        {Provider::Kugou, "GET", "https://login-user.kugou.com/v2/qrcode"},
        {Provider::Kugou, "GET", "https://login-user.kugou.com/v2/get_userinfo_qrcode"},
        {Provider::Kugou, "POST", "https://userservice.kugou.com/risk/v2/r_register_dev"},
        {Provider::Kugou, "POST", "https://gateway.kugou.com/v3/get_my_info"},
        {Provider::Netease, "GET", "https://p1.music.126.net/album.jpg?param=256y256"},
        {Provider::QQ, "GET", "https://y.gtimg.cn/music/photo_new/T002R300x300M000album.jpg"},
        {Provider::Kugou, "GET", "https://imgessl.kugou.com/stdmusic/20261007/cover.jpg"},
        {Provider::Netease, "GET", "https://p2.music.126.net/album.jpg"},
        {Provider::Netease, "GET", "https://p3.music.126.net/album.jpg"},
        {Provider::Netease, "GET", "https://p4.music.126.net/album.jpg"},
        {Provider::Kugou, "GET", "https://imge.kugou.com/stdmusic/cover.jpg"},
    };
    for (const auto& route : allowed) {
        HttpRequest request{route.provider, route.method, route.url, {}, {}, 256 * 1024};
        Expect(ValidHttpRequest(request), "historical route allowed");
        request.method = request.method == "POST" ? "GET" : "POST";
        Expect(!ValidHttpRequest(request), "route method is exact");
        request.method = route.method;
        request.provider = route.provider == Provider::QQ ? Provider::Netease : Provider::QQ;
        Expect(!ValidHttpRequest(request), "route cannot cross provider boundary");
    }
    for (const auto& route : {allowed[20], allowed[21], allowed[22]}) {
        HttpRequest image{route.provider, route.method, route.url, {}, {}, 256 * 1024};
        for (const auto* header : {"Cookie", "cookie", "Authorization", "Host", "Proxy-Authorization",
                                  "dfid", "mid", "Referer", "X-Router"}) {
            image.headers = {{header, "account-secret"}};
            Expect(!ValidHttpRequest(image), "public covers must never carry account credentials");
        }
        image.headers.clear();
        image.url += "#fragment";
        Expect(!ValidHttpRequest(image), "ambiguous cover URL rejected");
    }
    for (const auto* url : {
            "http://y.gtimg.cn/cover.jpg", "https://y.gtimg.cn.evil.test/cover.jpg",
            "https://y.gtimg.cn@evil.test/cover.jpg", "https://evil.test@y.gtimg.cn/cover.jpg",
            "https://y.gtimg.cn:443/cover.jpg", "https://y.gtimg.cn./cover.jpg",
            "https://y.gtimg.cn\\@evil.test/cover.jpg", "https://y.gtimg.cn/a b.jpg",
            "https://y.gtimg.cn/cover.jpg\r\nHost: evil", "https://127.0.0.1/cover.jpg",
            "https://y.gtimg.cn?redirect=evil.test/cover.jpg"}) {
        Expect(!ValidHttpRequest({Provider::QQ, "GET", url, {}, {}, 1024}),
            "public cover URL confusion rejected");
    }
    auto imageUrl = std::string("https://y.gtimg.cn/cover.jpg");
    imageUrl.push_back('\0');
    Expect(!ValidHttpRequest({Provider::QQ, "GET", imageUrl, {}, {}, 1024}),
        "public cover embedded NUL rejected");
    HttpRequest request{Provider::QQ, "GET", "https://ssl.ptlogin2.qq.com/ptqrlogin", {}, {}, 256 * 1024};
    const auto original = request;
    for (const auto* url : {
            "http://ssl.ptlogin2.qq.com/ptqrlogin", "https://ssl.ptlogin2.qq.com.evil.test/ptqrlogin",
            "https://ssl.ptlogin2.qq.com@evil.test/ptqrlogin", "https://evil.test@ssl.ptlogin2.qq.com/ptqrlogin",
            "https://ssl.ptlogin2.qq.com:443/ptqrlogin", "https://ssl.ptlogin2.qq.com./ptqrlogin",
            "https://ssl.ptlogin2.qq.com/ptqrlogin/extra", "https://ssl.ptlogin2.qq.com/ptqrlogin#fragment",
            "https://ssl.ptlogin2.qq.com/./ptqrlogin", "https://ssl.ptlogin2.qq.com/%70tqrlogin",
            "https://ssl.ptlogin2.qq.com\\@evil.test/ptqrlogin", "https://ssl.ptlogin2.qq.com/ptqrlogin\r\nHost: evil",
            "https://127.0.0.1/ptqrlogin", "https://[::1]/ptqrlogin", "https://music.163.com/weapi/logout"}) {
        request.url = url;
        Expect(!ValidHttpRequest(request), "URL confusion and unrelated routes rejected");
    }
    request = original;
    request.url.append("\0evil", 5);
    Expect(!ValidHttpRequest(request), "embedded URL NUL rejected");
    request = original;
    for (const auto* header : {"Host", "Authorization", "Proxy-Authorization", "Connection", "Transfer-Encoding",
                              "Content-Length", "Set-Cookie", "X-Forwarded-Host", "x-router", "Accept-Encoding"}) {
        request.headers = {{header, "evil"}};
        Expect(!ValidHttpRequest(request), "untrusted routing and auth headers rejected");
    }
    for (const auto& value : {std::string("qrsig=x\r\nHost: evil"), std::string("qrsig=x\0evil", 12),
                             std::string("qrsig=x; Domain=evil.test"), std::string("qrsig=x; Path=/"),
                             std::string(16385, 'x')}) {
        request.headers = {{"Cookie", value}};
        Expect(!ValidHttpRequest(request), "cookie injection rejected");
    }
    request.headers = {{"Cookie", "qrsig=fixture"}, {"Referer", "https://xui.ptlogin2.qq.com/"}};
    Expect(ValidHttpRequest(request), "explicit provider cookie allowed");
    request.headers["cookie"] = "second=fixture";
    Expect(!ValidHttpRequest(request), "case insensitive duplicate headers rejected");
    request = original;
    request.maxResponse = 0;
    Expect(!ValidHttpRequest(request), "zero response budget rejected");
    request.maxResponse = 4 * 1024 * 1024;
    Expect(ValidHttpRequest(request), "large catalog response stays within the supported bound");
    request.maxResponse = 4 * 1024 * 1024 + 1;
    Expect(!ValidHttpRequest(request), "unbounded response rejected");
    request = original;
    request.body = "get body";
    Expect(!ValidHttpRequest(request), "GET body rejected");
    request = {Provider::Kugou, "POST", "https://gateway.kugou.com/v3/get_my_info",
        {{"x-router", "usercenter.kugou.com"}, {"dfid", "-"}, {"mid", "fixture"}, {"clienttime", "123"},
         {"kg-rc", "1"}, {"kg-rec", "1"}, {"kg-thash", "5d816a0"},
         {"kg-rf", "B9EDA08A64250DEFFBCADDEE00F8F25F"}}, "{}", 1024};
    Expect(ValidHttpRequest(request), "Kugou fixed routing and bounded device headers allowed");
    request.headers["x-router"] = "evil.test";
    Expect(!ValidHttpRequest(request), "Kugou cannot redirect cookies through router override");
    request = {Provider::QQ, "POST", "https://api.tencentmusic.com/tme/trpc/proxy",
        {{"Host", "api.tencentmusic.com"}, {"method", "GetQimei"}, {"appid", "qimei_qq_android"},
         {"service", "trpc.tme_datasvr.qimeiproxy.QimeiProxy"}, {"sign", std::string(32, 'a')},
         {"timestamp", "123"}}, "{}", 1024};
    Expect(ValidHttpRequest(request), "QIMEI fixed RPC headers allowed");
    request.headers["Host"] = "evil.test";
    Expect(!ValidHttpRequest(request), "QIMEI Host cannot reroute request");
    Rejected([&] { SendHttp(request, {}); }, "invalid requests fail before network");
    const auto start = std::chrono::steady_clock::now();
    try {
        SendHttp(original, [] { return true; });
        Expect(false, "pre-cancelled request rejected");
    } catch (const std::runtime_error& error) {
        Expect(std::string(error.what()) == "cancelled", "cancellation preserves generic contract");
    }
    Expect(std::chrono::steady_clock::now() - start < std::chrono::seconds(1), "pre-cancellation is prompt");
}
void Crypto() {
    Expect(UrlEncode(std::string("a b/+\0", 6)) == "a%20b%2F%2B%00", "URL encoding binary and delimiters");
    Expect(UrlEncode("AZaz09-_.~") == "AZaz09-_.~", "unreserved URL characters");
    Expect(Base64("") == "" && Base64("f") == "Zg==" && Base64("foo") == "Zm9v" &&
        Base64(std::string("\0\xff", 2)) == "AP8=", "base64 vectors");
    Expect(HashHex("abc") == "900150983cd24fb0d6963f7d28e17f72", "MD5 vector");
    Expect(HashHex("abc", "SHA256") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA256 vector");
    const auto random = RandomHex(32);
    Expect(random.size() == 64 && random.find_first_not_of("0123456789abcdef") == random.npos &&
        random != RandomHex(32) && RandomHex(0).empty(), "CNG random bounded hex");
    Rejected([] { RandomHex(4097); }, "random bound");
    Rejected([] { HashHex("x", "SHA1"); }, "unknown digest rejected");
    const auto cbc = Aes("fixture plaintext", "0123456789abcdef", "0102030405060708");
    const auto ecb = Aes("fixture plaintext", "0123456789abcdef", "");
    Expect(Hex(cbc) == "7b3df78215a4d20f6bacdd938b40f61136949a4f5c7428501a85c41ed07fdb19", "AES CBC PKCS7 vector");
    Expect(Hex(ecb) == "71aaaf5f75affd02ff7a681dc186404acd363d350c68d0ac4b51d0832b07d459", "AES ECB PKCS7 vector");
    Expect(Aes(cbc, "0123456789abcdef", "0102030405060708", false) == "fixture plaintext" &&
        Aes(ecb, "0123456789abcdef", "", false) == "fixture plaintext", "AES decryption");
    for (const auto size : {16U, 24U, 32U}) {
        const std::string key(size, 'k');
        Expect(Aes(Aes("", key, ""), key, "", false).empty(), "AES empty plaintext padding roundtrip");
    }
    Rejected([] { Aes("x", "short", ""); }, "invalid AES key");
    Rejected([] { Aes("x", std::string(16, 'k'), "short"); }, "invalid AES IV");
    Rejected([] { Aes("invalid", std::string(16, 'k'), "", false); }, "invalid AES cipher");
    constexpr auto spki = "-----BEGIN PUBLIC KEY-----\n"
        "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDgtQn2JZ34ZC28NWYpAUd98iZ37BUrX/aKzmFbt7clFSs6sXqHauqKWqdtLkF2"
        "KexO40H1YTX8z2lSgBBOAxLsvaklV8k4cBFK9snQXE9/DDaFt6Rr7iVZMldczhC0JNgTz+SHXT6CBHuX3e9SdB1Ua44oncaTWz7OBGLbCiK45wIDAQAB\n"
        "-----END PUBLIC KEY-----";
    constexpr auto pkcs1 = "-----BEGIN RSA PUBLIC KEY-----\n"
        "MIGJAoGBAOC1CfYlnfhkLbw1ZikBR33yJnfsFStf9orOYVu3tyUVKzqxeodq6opa\n"
        "p20uQXYp7E7jQfVhNfzPaVKAEE4DEuy9qSVXyThwEUr2ydBcT38MNoW3pGvuJVky\n"
        "V1zOELQk2BPP5IddPoIEe5fd71J0HVRrjiidxpNbPs4EYtsKIrjnAgMBAAE=\n-----END RSA PUBLIC KEY-----";
    const auto raw = RsaEncrypt("0123456789abcdef", spki, true);
    Expect(Hex(raw) ==
        "ac744caab466f1fd5a75228365d15df7add288e8982a2c8f80da5f8f22d54834b5e1eab04dde9dac7341851a98de37463d8ba"
        "01d9e9c4d2c546c0f948e3163c667785a1b9c4f160305b0fb1ec3b698de0c704719b2fbc469582654b9c2595317dc40c3ecee32baa6d9753970b66667c01bde9ad34048fa8b2b0f628e56a5bf49",
        "raw RSA big-endian vector");
    Expect(raw == RsaEncrypt("0123456789abcdef", pkcs1, true), "PKCS1 and SPKI PEM agree");
    const auto padded = RsaEncrypt("fixture", spki);
    Expect(padded.size() == 128 && padded != RsaEncrypt("fixture", spki), "RSA PKCS1 padding is randomized");
    Rejected([&] { RsaEncrypt(std::string(118, 'x'), spki); }, "RSA padding size checked");
    Rejected([&] { RsaEncrypt(std::string(129, 'x'), spki, true); }, "raw RSA size checked");
    Rejected([] { RsaEncrypt("x", "not a public key"); }, "invalid PEM rejected");
}
}  // namespace
int main() {
    Policy();
    Crypto();
    std::cout << "music HTTP policy, pre-cancellation and native crypto vectors passed without network\n";
}
