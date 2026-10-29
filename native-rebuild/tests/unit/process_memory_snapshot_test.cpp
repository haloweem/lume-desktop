#include "process_memory_snapshot.h"

#include <windows.h>

#include <filesystem>
#include <iostream>

namespace {
constexpr SIZE_T heapBytes = 1024 * 1024;
constexpr SIZE_T virtualBytes = 4 * 1024 * 1024;

class HeapAllocation {
public:
    explicit HeapAllocation(HANDLE heap) : heap_(heap) {}
    ~HeapAllocation() { if (memory_) HeapFree(heap_, 0, memory_); }
    HeapAllocation(const HeapAllocation&) = delete;
    HeapAllocation& operator=(const HeapAllocation&) = delete;
    bool Allocate() {
        memory_ = HeapAlloc(heap_, 0, heapBytes);
        return memory_ != nullptr;
    }
    void Touch() const {
        auto* bytes = static_cast<volatile unsigned char*>(memory_);
        for (SIZE_T index = 0; index < heapBytes; ++index) bytes[index] = 0xa5;
    }
    bool Release() {
        if (!memory_) return true;
        if (!HeapFree(heap_, 0, memory_)) return false;
        memory_ = nullptr;
        return true;
    }
private:
    HANDLE heap_ = nullptr;
    void* memory_ = nullptr;
};

class VirtualAllocation {
public:
    ~VirtualAllocation() { if (memory_) VirtualFree(memory_, 0, MEM_RELEASE); }
    VirtualAllocation() = default;
    VirtualAllocation(const VirtualAllocation&) = delete;
    VirtualAllocation& operator=(const VirtualAllocation&) = delete;
    bool Allocate() {
        memory_ = VirtualAlloc(nullptr, virtualBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        return memory_ != nullptr;
    }
    void Touch() const {
        auto* bytes = static_cast<volatile unsigned char*>(memory_);
        for (SIZE_T index = 0; index < virtualBytes; ++index) bytes[index] = 0x5a;
    }
    bool Release() {
        if (!memory_) return true;
        if (!VirtualFree(memory_, 0, MEM_RELEASE)) return false;
        memory_ = nullptr;
        return true;
    }
private:
    void* memory_ = nullptr;
};

bool Check(bool passed, const char* operation) {
    if (!passed) std::cerr << "FAIL: " << operation << '\n';
    return passed;
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: lume-process-memory-snapshot-test <new-output.jsonl>\n";
        return 2;
    }
    try {
        const auto output = std::filesystem::u8path(argv[1]);
        if (std::filesystem::exists(output)) {
            std::cerr << "FAIL: calibration output must not already exist\n";
            return 2;
        }
        const HANDLE heap = GetProcessHeap();
        if (!Check(heap != nullptr, "GetProcessHeap")) return 1;
        HeapAllocation allocation(heap);
        VirtualAllocation virtualAllocation;
        // Prime stream/locale initialization so first-write allocations do not
        // masquerade as calibration growth. Keep the warmup record as evidence.
        if (!Check(WriteProcessMemorySnapshot(output, "warmup", 0), "warmup snapshot") ||
            !Check(WriteProcessMemorySnapshot(output, "baseline", 0), "baseline snapshot")) return 1;
        if (!Check(allocation.Allocate(), "HeapAlloc 1 MiB")) return 1;
        allocation.Touch();
        if (!Check(WriteProcessMemorySnapshot(output, "heap_allocated", 0), "heap allocation snapshot")) return 1;
        if (!Check(allocation.Release(), "HeapFree")) return 1;
        if (!Check(WriteProcessMemorySnapshot(output, "heap_released", 0), "heap release snapshot")) return 1;
        if (!Check(virtualAllocation.Allocate(), "VirtualAlloc 4 MiB")) return 1;
        virtualAllocation.Touch();
        if (!Check(WriteProcessMemorySnapshot(output, "virtual_allocated", 0), "virtual allocation snapshot")) return 1;
        if (!Check(virtualAllocation.Release(), "VirtualFree")) return 1;
        if (!Check(WriteProcessMemorySnapshot(output, "virtual_released", 0), "virtual release snapshot")) return 1;
        // Rapid blank control checks retention caused by repeated diagnostics.
        // It does not match GUI-phase dwell times or third-party allocators.
        // Analyze the first/last ten means externally; no universal threshold.
        for (int round = 1; round <= 100; ++round) {
            if (!Check(WriteProcessMemorySnapshot(output, "diagnostic_baseline", round),
                    "diagnostic-only baseline snapshot")) return 1;
        }
        std::cout << "Calibration records written; validate declared coverage and byte deltas in JSONL.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
