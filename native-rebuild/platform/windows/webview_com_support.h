#pragma once
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <functional>
#include <utility>

namespace lume::platform::windows::webview_detail {
template<class T> class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* value) : value_(value) {}
    ~ComPtr() { Reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) { Reset(); value_ = std::exchange(other.value_, nullptr); }
        return *this;
    }
    void Reset() { if (value_) std::exchange(value_, nullptr)->Release(); }
    T** Put() { Reset(); return &value_; }
    T* Get() const { return value_; }
    T* operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    void CopyFrom(T* value) { if (value) value->AddRef(); Reset(); value_ = value; }
private:
    T* value_ = nullptr;
};

inline GUID InterfaceId(const wchar_t* id) {
    GUID result{};
    CLSIDFromString(id, &result);
    return result;
}

// SDK-generated MIDL __uuidof metadata isn't supported by every MinGW toolchain.
// IDs supplied below are the SDK interface IDs, not private COM interfaces.
template<class Interface, class... Args> class Callback final : public Interface {
public:
    Callback(const wchar_t* id, std::function<HRESULT(Args...)> fn)
        : id_(InterfaceId(id)), fn_(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, id_)) {
            *out = static_cast<Interface*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto refs = --refs_;
        if (!refs) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override {
        try { return fn_(args...); } catch (...) { return E_FAIL; }
    }
private:
    std::atomic<ULONG> refs_{1};
    GUID id_;
    std::function<HRESULT(Args...)> fn_;
};
template<class Interface, class... Args, class F>
ComPtr<Interface> Handler(const wchar_t* id, F fn) {
    return ComPtr<Interface>(new Callback<Interface, Args...>(id, std::move(fn)));
}
}
