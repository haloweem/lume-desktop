#pragma once
// Probe helper: records every frame the desktop shows in one screen rectangle
// (DXGI desktop duplication), as 8-bit luminance with the present time, so the
// actual on-screen motion can be checked frame by frame.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

struct ScreenFrame {
    double ms = 0;  // QPC present time, milliseconds
    std::vector<std::uint8_t> luma;
};

class ScreenCapture {
public:
    bool Start(RECT area) {
        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return false;
        const POINT centre{(area.left + area.right) / 2, (area.top + area.bottom) / 2};
        IDXGIAdapter1* adapter = nullptr;
        for (UINT a = 0; !output && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
            IDXGIOutput* o = nullptr;
            for (UINT i = 0; adapter->EnumOutputs(i, &o) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_OUTPUT_DESC d{};
                o->GetDesc(&d);
                if (PtInRect(&d.DesktopCoordinates, centre)) {
                    o->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output));
                    desc = d;
                    o->Release();
                    break;
                }
                o->Release();
            }
            if (output) {
                D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
            }
            adapter->Release();
        }
        factory->Release();
        if (!output || !device || FAILED(output->DuplicateOutput(device, &duplication))) return false;
        rect = area;
        OffsetRect(&rect, -desc.DesktopCoordinates.left, -desc.DesktopCoordinates.top);
        running = true;
        worker = std::thread([this] { Run(); });
        return true;
    }
    std::vector<ScreenFrame> Stop() {
        running = false;
        if (worker.joinable()) worker.join();
        if (duplication) duplication->Release();
        if (staging) staging->Release();
        if (context) context->Release();
        if (device) device->Release();
        if (output) output->Release();
        duplication = nullptr; staging = nullptr; context = nullptr; device = nullptr; output = nullptr;
        return std::move(frames);
    }
    int Width() const { return rect.right - rect.left; }
    int Height() const { return rect.bottom - rect.top; }

private:
    void Run() {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        while (running) {
            DXGI_OUTDUPL_FRAME_INFO info{};
            IDXGIResource* resource = nullptr;
            const HRESULT hr = duplication->AcquireNextFrame(50, &info, &resource);
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
            if (FAILED(hr)) break;
            if (info.LastPresentTime.QuadPart && info.AccumulatedFrames) {
                ID3D11Texture2D* texture = nullptr;
                resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
                if (texture && Copy(texture)) frames.back().ms = info.LastPresentTime.QuadPart * 1000.0 / frequency.QuadPart;
                if (texture) texture->Release();
            }
            resource->Release();
            duplication->ReleaseFrame();
        }
    }
    bool Copy(ID3D11Texture2D* texture) {
        if (!staging) {
            D3D11_TEXTURE2D_DESC d{};
            texture->GetDesc(&d);
            d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
            if (FAILED(device->CreateTexture2D(&d, nullptr, &staging))) return false;
        }
        context->CopyResource(staging, texture);
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &map))) return false;
        ScreenFrame frame;
        frame.luma.resize(static_cast<std::size_t>(Width()) * Height());
        for (int y = 0; y < Height(); ++y) {
            const auto* row = static_cast<const std::uint8_t*>(map.pData) + static_cast<std::size_t>(rect.top + y) * map.RowPitch + rect.left * 4;
            for (int x = 0; x < Width(); ++x)
                frame.luma[static_cast<std::size_t>(y) * Width() + x] =
                    static_cast<std::uint8_t>((row[x * 4] * 29 + row[x * 4 + 1] * 150 + row[x * 4 + 2] * 77) >> 8);
        }
        context->Unmap(staging, 0);
        frames.push_back(std::move(frame));
        return true;
    }
    IDXGIOutput1* output = nullptr;
    DXGI_OUTPUT_DESC desc{};
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGIOutputDuplication* duplication = nullptr;
    ID3D11Texture2D* staging = nullptr;
    RECT rect{};
    std::atomic<bool> running{false};
    std::thread worker;
    std::vector<ScreenFrame> frames;
};
