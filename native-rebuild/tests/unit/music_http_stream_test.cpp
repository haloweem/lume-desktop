#include <winsock2.h>
#include <ws2tcpip.h>
#include "platform/windows/music_http_stream.h"
#include "platform/windows/music_http_flac_decoder.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

// Loopback only. No provider credentials or external network are used.
class RangeServer {
public:
    explicit RangeServer(std::string behavior = {}, std::vector<char> payload = {})
        : mode(std::move(behavior)), data(std::move(payload)) {
        if (data.empty()) {
            data.resize(512 * 1024);
            for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i % 251);
            std::memcpy(data.data(), mode == "not-flac" ? "ID3x" : "fLaC", 4);
        }
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Check(listener != INVALID_SOCKET, "loopback socket created");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Check(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
            listen(listener, SOMAXCONN) == 0, "loopback server bound");
        int size = sizeof(address);
        Check(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0, "loopback port available");
        port = ntohs(address.sin_port);
        worker = std::thread([this] { Serve(); });
    }
    ~RangeServer() {
        stopping = true;
        shutdown(listener, SD_BOTH);
        closesocket(listener);
        worker.join();
    }
    std::wstring Url() const { return L"http://127.0.0.1:" + std::to_wstring(port) + L"/audio.flac?token=test"; }
    std::atomic<int> requests{0};
    std::atomic<std::size_t> bytes{0};
    std::atomic<bool> validatorSeen{false};
private:
    static bool Send(SOCKET client, const char* value, std::size_t count) {
        while (count) {
            const int sent = send(client, value, static_cast<int>(std::min<std::size_t>(count, 16384)), 0);
            if (sent <= 0) return false;
            value += sent;
            count -= static_cast<std::size_t>(sent);
        }
        return true;
    }
    void Serve() {
        while (!stopping) {
            const auto client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) break;
            DWORD timeout = 2000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            std::string request;
            char buffer[2048];
            while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
                const int got = recv(client, buffer, sizeof(buffer), 0);
                if (got <= 0) break;
                request.append(buffer, static_cast<std::size_t>(got));
            }
            ++requests;
            const auto at = request.find("Range: bytes=");
            std::size_t start = 0, last = data.size() - 1;
            if (at != std::string::npos) {
                const auto dash = request.find('-', at + 13);
                start = std::strtoull(request.c_str() + at + 13, nullptr, 10);
                if (dash != std::string::npos) last = std::min(last,
                    static_cast<std::size_t>(std::strtoull(request.c_str() + dash + 1, nullptr, 10)));
            }
            validatorSeen = validatorSeen || request.find("If-Range: \"fixture-v1\"") != std::string::npos;
            const bool reject = mode == "no-range" || (mode == "changed" && requests > 1);
            if (start > last || start >= data.size()) { closesocket(client); continue; }
            const auto advertised = mode == "bad-second" && requests > 1 ? start + 1 :
                mode == "bad-range" ? start + 1 : start;
            auto response = std::string(reject ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 206 Partial Content\r\n") +
                "Connection: close\r\nContent-Type: audio/flac\r\nETag: \"fixture-v1\"\r\nContent-Length: " +
                std::to_string(last - start + 1) + "\r\nContent-Range: bytes " + std::to_string(advertised) + "-" +
                std::to_string(last) + "/" + std::to_string(data.size()) + "\r\n\r\n";
            if (Send(client, response.data(), response.size())) {
                const auto count = mode == "truncated" ? 16 : last - start + 1;
                if (Send(client, data.data() + start, count)) bytes += count;
            }
            shutdown(client, SD_BOTH);
            closesocket(client);
        }
    }
    std::string mode;
    std::vector<char> data;
    SOCKET listener = INVALID_SOCKET;
    unsigned short port = 0;
    std::atomic<bool> stopping{false};
    std::thread worker;
};

void TestRanges() {
    RangeServer server;
    Microsoft::WRL::ComPtr<IMFByteStream> stream;
    Check(SUCCEEDED(lume::music::CreateMusicHttpFlacStream(server.Url(), &stream)), "range FLAC stream opens");
    QWORD length = 0;
    DWORD capabilities = 0;
    Check(SUCCEEDED(stream->GetLength(&length)) && length == 512 * 1024 &&
        SUCCEEDED(stream->GetCapabilities(&capabilities)) && (capabilities & MFBYTESTREAM_IS_SEEKABLE),
        "range stream advertises exact size and random access");
    char buffer[32]{};
    ULONG got = 0;
    Check(SUCCEEDED(stream->SetCurrentPosition(300000)) && SUCCEEDED(stream->Read(
        reinterpret_cast<BYTE*>(buffer), 32, &got)) && got == 32, "distant read returns a bounded range");
    for (std::size_t i = 0; i < 32; ++i)
        Check(static_cast<unsigned char>(buffer[i]) == (300000 + i) % 251, "distant bytes match file offsets");
    Check(server.requests == 2 && server.bytes <= 256 * 1024 && server.validatorSeen,
        "seek skips intervening data, bounds cache and sends the strong validator");
    Check(SUCCEEDED(stream->SetCurrentPosition(length - 4)) && SUCCEEDED(stream->Read(
        reinterpret_cast<BYTE*>(buffer), 32, &got)) && got == 4, "last range trims at EOF");
    Check(SUCCEEDED(stream->Read(reinterpret_cast<BYTE*>(buffer), 32, &got)) && got == 0, "EOF remains stable");
    stream.Reset();
}

void TestRejection() {
    for (const auto* mode : {"no-range", "bad-range", "not-flac", "truncated"}) {
        RangeServer server(mode);
        Microsoft::WRL::ComPtr<IMFByteStream> stream;
        Check(FAILED(lume::music::CreateMusicHttpFlacStream(server.Url(), &stream)) && !stream,
            "unsupported and malformed responses leave the native fallback available");
    }
    for (const auto* mode : {"changed", "bad-second"}) {
        RangeServer server(mode);
        Microsoft::WRL::ComPtr<IMFByteStream> stream;
        Check(SUCCEEDED(lume::music::CreateMusicHttpFlacStream(server.Url(), &stream)), "first range is accepted");
        ULONG got = 0;
        BYTE byte = 0;
        Check(SUCCEEDED(stream->SetCurrentPosition(300000)) && FAILED(stream->Read(&byte, 1, &got)) && got == 0,
            "changed resources and wrong range offsets never return mismatched audio bytes");
        stream.Reset();
    }
    IMFByteStream* stream = nullptr;
    for (const auto* url : {L"file:///test.flac", L"http://user:secret@127.0.0.1/a.flac", L"https://example.invalid/a.mp3"})
        Check(FAILED(lume::music::CreateMusicHttpFlacStream(url, &stream)) && !stream,
            "local sources, credentials and known other formats skip the range path");
}

// A valid, deterministic 184-second native FLAC with independent verbatim
// subframes, CRCs and no seek table. No encoder or downloaded fixture is needed.
// Its PCM deliberately contains unchecked sync lookalikes with another bit
// depth, which previously made random seeking fall back to scanning from zero.
std::uint16_t FixtureSample(std::uint64_t frame, unsigned channel) {
    switch (frame % 16) {
    case 0: return 0xfff8;
    case 1: return 0xc402;
    case 2: return 0;
    default: return static_cast<std::uint16_t>(frame * 109 + channel * 7919);
    }
}

std::uint16_t Crc(const std::vector<char>& data, std::size_t start, unsigned bits, unsigned polynomial) {
    unsigned crc = 0;
    const unsigned top = 1u << (bits - 1);
    const unsigned mask = (1u << bits) - 1;
    for (auto i = start; i < data.size(); ++i) {
        crc ^= static_cast<unsigned char>(data[i]) << (bits - 8);
        for (int bit = 0; bit < 8; ++bit) crc = ((crc << 1) ^ ((crc & top) ? polynomial : 0)) & mask;
    }
    return static_cast<std::uint16_t>(crc);
}

std::vector<char> FlacFixture() {
    constexpr unsigned block = 4096, blocks = 360, rate = 8000;
    constexpr std::uint64_t frames = static_cast<std::uint64_t>(block) * blocks;
    std::vector<char> data{'f', 'L', 'a', 'C', static_cast<char>(0x80), 0, 0, 34};
    const auto word = [&](unsigned value) {
        data.push_back(static_cast<char>(value >> 8));
        data.push_back(static_cast<char>(value));
    };
    word(block); word(block);
    data.insert(data.end(), 6, 0); // Unknown min/max encoded frame sizes.
    const std::uint64_t info = (static_cast<std::uint64_t>(rate) << 44) |
        (1ull << 41) | (15ull << 36) | frames;
    for (int byte = 7; byte >= 0; --byte) data.push_back(static_cast<char>(info >> (byte * 8)));
    data.insert(data.end(), 16, 0); // Optional MD5 omitted.
    for (unsigned number = 0; number < blocks; ++number) {
        const auto start = data.size();
        data.insert(data.end(), {static_cast<char>(0xff), static_cast<char>(0xf8),
            static_cast<char>(0xc4), static_cast<char>(0x18)});
        if (number < 128) data.push_back(static_cast<char>(number));
        else {
            data.push_back(static_cast<char>(0xc0 | (number >> 6)));
            data.push_back(static_cast<char>(0x80 | (number & 63)));
        }
        data.push_back(static_cast<char>(Crc(data, start, 8, 0x07)));
        for (unsigned channel = 0; channel < 2; ++channel) {
            data.push_back(2); // Verbatim subframe, no wasted bits.
            for (unsigned i = 0; i < block; ++i)
                word(FixtureSample(static_cast<std::uint64_t>(number) * block + i, channel));
        }
        word(Crc(data, start, 16, 0x8005));
    }
    return data;
}

void TestDecodedSeeking() {
    RangeServer server({}, FlacFixture());
    lume::music::HttpFlacDecoder decoder;
    Check(decoder.Open(server.Url()) && decoder.Rate() == 8000 && decoder.Channels() == 2 &&
        decoder.Frames() == 4096ull * 360, "real FLAC opens with exact stream metadata");
    for (const double seconds : {120.0, 175.0, 15.0, 120.25}) {
        const auto requests = server.requests.load();
        const auto bytes = server.bytes.load();
        Check(SUCCEEDED(decoder.Seek(seconds)), "distant and backward FLAC seeks succeed");
        Microsoft::WRL::ComPtr<IMFSample> sample;
        DWORD flags = 0;
        LONGLONG timestamp = -1;
        Check(SUCCEEDED(decoder.Read(&sample, &flags, &timestamp)) && sample &&
            timestamp == static_cast<LONGLONG>(seconds * 10000000), "decoded timestamp matches exact seek");
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        BYTE* pcm = nullptr;
        DWORD length = 0;
        Check(SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) &&
            SUCCEEDED(buffer->Lock(&pcm, nullptr, &length)) && length == 400 * 4,
            "decoder returns a complete PCM chunk");
        for (unsigned i = 0; i < 400; ++i) for (unsigned channel = 0; channel < 2; ++channel) {
            const auto at = (i * 2 + channel) * 2;
            const auto actual = static_cast<std::uint16_t>(pcm[at] | (pcm[at + 1] << 8));
            Check(actual == FixtureSample(static_cast<std::uint64_t>(seconds * 8000) + i, channel),
                "actual PCM, including false sync bytes, matches the requested frame");
        }
        buffer->Unlock();
        Check(server.requests - requests <= 16 && server.bytes - bytes <= 2 * 1024 * 1024,
            "seek fetches bounded distant ranges instead of scanning from the start");
    }
    Check(SUCCEEDED(decoder.Seek(184.32)), "seeking exactly to EOF succeeds");
    Microsoft::WRL::ComPtr<IMFSample> sample;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    Check(SUCCEEDED(decoder.Read(&sample, &flags, &timestamp)) && !sample &&
        (flags & MF_SOURCE_READERF_ENDOFSTREAM), "decoder reports stable EOF");
}
}

int main() {
    WSADATA data{};
    Check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Winsock starts");
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Check(SUCCEEDED(com) && SUCCEEDED(MFStartup(MF_VERSION)), "Media Foundation starts");
    TestRanges();
    TestRejection();
    TestDecodedSeeking();
    MFShutdown();
    CoUninitialize();
    WSACleanup();
    std::cout << "HTTP FLAC range tests passed\n";
}
