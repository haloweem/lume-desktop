#include "process_memory_snapshot.h"

#include <windows.h>
#include <psapi.h>

#include <cstdint>
#include <fstream>
#include <limits>
#include <locale>

namespace {
struct ProcessMemory {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    bool complete = false;
    DWORD error = ERROR_SUCCESS;
};

struct HeapMemory {
    const char* status = "not_started";
    bool complete = false;
    bool countComplete = false;
    bool compatibilityKnown = false;
    bool lockSucceeded = false;
    bool unlockSucceeded = false;
    DWORD processHeapCount = 0;
    DWORD countError = ERROR_SUCCESS;
    DWORD error = ERROR_SUCCESS;
    DWORD walkEndError = ERROR_SUCCESS;
    DWORD unlockError = ERROR_SUCCESS;
    ULONG compatibility = 0;
    std::uint64_t busyBytes = 0;
    std::uint64_t busyBlocks = 0;
    std::uint64_t freeBytes = 0;
    std::uint64_t freeBlocks = 0;
    std::uint64_t busyOverheadBytes = 0;
    std::uint64_t freeOverheadBytes = 0;
    std::uint64_t regionCount = 0;
    std::uint64_t uncommittedRangeBytes = 0;
};

struct VirtualMemory {
    bool complete = false;
    const char* status = "not_started";
    DWORD error = ERROR_SUCCESS;
    std::uint64_t queriedRegions = 0;
    std::uint64_t privateCommittedBytes = 0;
    std::uint64_t mappedCommittedBytes = 0;
    std::uint64_t imageCommittedBytes = 0;
    std::uint64_t otherCommittedBytes = 0;
    std::uint64_t privateReservedBytes = 0;
    std::uint64_t privateAllocationCount = 0;
    std::uint64_t privateCommittedAllocationCount = 0;
    std::uint64_t scannedThroughAddress = 0;
};

struct Resources {
    bool complete = false;
    bool handleCountComplete = false;
    bool gdiComplete = false;
    bool userComplete = false;
    DWORD handleCount = 0;
    DWORD gdiObjects = 0;
    DWORD userObjects = 0;
    DWORD handleError = ERROR_SUCCESS;
    DWORD gdiError = ERROR_SUCCESS;
    DWORD userError = ERROR_SUCCESS;
};

ProcessMemory ReadProcessMemory(HANDLE process) {
    ProcessMemory result;
    result.counters.cb = sizeof(result.counters);
    result.complete = GetProcessMemoryInfo(process,
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&result.counters),
        static_cast<DWORD>(sizeof(result.counters))) != FALSE;
    if (!result.complete) result.error = GetLastError();
    return result;
}

HeapMemory ReadDefaultHeap() {
    // All storage and handles exist before locking. Never retain handles to
    // third-party heaps: GetProcessHeaps does not keep those heaps alive.
    // https://learn.microsoft.com/windows/win32/api/heapapi/nf-heapapi-getprocessheaps
    HeapMemory result;
    result.processHeapCount = GetProcessHeaps(0, nullptr);
    result.countComplete = result.processHeapCount != 0;
    if (!result.countComplete) result.countError = GetLastError();
    const HANDLE heap = GetProcessHeap();
    if (!heap) {
        result.error = GetLastError();
        result.status = "default_heap_unavailable";
        return result;
    }
    if (!HeapQueryInformation(heap, HeapCompatibilityInformation,
            &result.compatibility, sizeof(result.compatibility), nullptr)) {
        result.error = GetLastError();
        result.status = "compatibility_query_failed";
        return result;
    }
    result.compatibilityKnown = true;
    // Documented compatibility values: standard (0), look-aside (1), LFH (2).
    // Do not claim support for a segment/custom allocator reported otherwise.
    if (result.compatibility > 2) {
        result.error = ERROR_NOT_SUPPORTED;
        result.status = "unsupported_allocator";
        return result;
    }
    PROCESS_HEAP_ENTRY entry{};
    if (!HeapLock(heap)) {
        result.error = GetLastError();
        result.status = "lock_failed";
        return result;
    }
    result.lockSucceeded = true;
    // LOCKED SECTION: only HeapWalk/GetLastError and scalar arithmetic. No
    // allocation, I/O, logging, callbacks, containers, or GUI operations here.
    // https://learn.microsoft.com/windows/win32/api/heapapi/nf-heapapi-heapwalk
    while (HeapWalk(heap, &entry)) {
        if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0) {
            result.busyBytes += entry.cbData;
            result.busyOverheadBytes += entry.cbOverhead;
            ++result.busyBlocks;
        } else if ((entry.wFlags & PROCESS_HEAP_REGION) != 0) {
            ++result.regionCount;
        } else if ((entry.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE) != 0) {
            result.uncommittedRangeBytes += entry.cbData;
        } else {
            result.freeBytes += entry.cbData;
            result.freeOverheadBytes += entry.cbOverhead;
            ++result.freeBlocks;
        }
    }
    // Capture immediately, before HeapUnlock can overwrite last error.
    result.walkEndError = GetLastError();
    result.unlockSucceeded = HeapUnlock(heap) != FALSE;
    if (!result.unlockSucceeded) result.unlockError = GetLastError();
    // End locked section. Partial counters remain available but are marked.
    if (!result.unlockSucceeded) {
        result.error = result.unlockError;
        result.status = "unlock_failed";
    } else if (result.walkEndError != ERROR_NO_MORE_ITEMS) {
        result.error = result.walkEndError;
        result.status = "walk_failed";
    } else {
        result.complete = true;
        result.status = "complete_within_default_heap";
    }
    return result;
}

VirtualMemory ReadVirtualMemory() {
    VirtualMemory result;
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto maximum = reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress);
    std::uintptr_t address = 0;
    const void* lastPrivateAllocation = nullptr;
    const void* lastCommittedPrivateAllocation = nullptr;
    while (address <= maximum) {
        MEMORY_BASIC_INFORMATION region{};
        const SIZE_T received = VirtualQuery(reinterpret_cast<const void*>(address),
            &region, sizeof(region));
        if (received == 0) {
            result.error = GetLastError();
            result.status = "query_failed";
            return result;
        }
        if (received != sizeof(region)) {
            result.error = ERROR_BAD_LENGTH;
            result.status = "unexpected_query_size";
            return result;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (region.RegionSize == 0 || region.RegionSize >
                std::numeric_limits<std::uintptr_t>::max() - base ||
                base > address || base + region.RegionSize <= address) {
            result.error = ERROR_INVALID_DATA;
            result.status = "invalid_region_bounds";
            return result;
        }
        ++result.queriedRegions;
        if (region.Type == MEM_PRIVATE && region.State != MEM_FREE) {
            if (region.AllocationBase != lastPrivateAllocation) {
                ++result.privateAllocationCount;
                lastPrivateAllocation = region.AllocationBase;
            }
            if (region.State == MEM_RESERVE) result.privateReservedBytes += region.RegionSize;
        }
        if (region.State == MEM_COMMIT) {
            if (region.Type == MEM_PRIVATE) {
                result.privateCommittedBytes += region.RegionSize;
                if (region.AllocationBase != lastCommittedPrivateAllocation) {
                    ++result.privateCommittedAllocationCount;
                    lastCommittedPrivateAllocation = region.AllocationBase;
                }
            } else if (region.Type == MEM_MAPPED) {
                result.mappedCommittedBytes += region.RegionSize;
            } else if (region.Type == MEM_IMAGE) {
                result.imageCommittedBytes += region.RegionSize;
            } else {
                result.otherCommittedBytes += region.RegionSize;
            }
        }
        address = base + region.RegionSize;
        result.scannedThroughAddress = static_cast<std::uint64_t>(address - 1);
    }
    result.complete = true;
    result.status = "complete_non_atomic_walk";
    return result;
}

Resources ReadResources(HANDLE process) {
    Resources result;
    result.handleCountComplete = GetProcessHandleCount(process, &result.handleCount) != FALSE;
    if (!result.handleCountComplete) result.handleError = GetLastError();
    // Zero is valid for a console process; clear stale errors before each call.
    SetLastError(ERROR_SUCCESS);
    result.gdiObjects = GetGuiResources(process, GR_GDIOBJECTS);
    result.gdiError = result.gdiObjects == 0 ? GetLastError() : ERROR_SUCCESS;
    result.gdiComplete = result.gdiError == ERROR_SUCCESS;
    SetLastError(ERROR_SUCCESS);
    result.userObjects = GetGuiResources(process, GR_USEROBJECTS);
    result.userError = result.userObjects == 0 ? GetLastError() : ERROR_SUCCESS;
    result.userComplete = result.userError == ERROR_SUCCESS;
    result.complete = result.handleCountComplete && result.gdiComplete && result.userComplete;
    return result;
}

void WriteJsonString(std::ostream& stream, const char* value) {
    constexpr char hex[] = "0123456789abcdef";
    stream.put('"');
    for (const char* next = value; *next; ++next) {
        const auto ch = static_cast<unsigned char>(*next);
        if (ch == '"' || ch == '\\') {
            stream.put('\\');
            stream.put(static_cast<char>(ch));
        } else if (ch < 0x20) {
            stream << "\\u00" << hex[ch >> 4] << hex[ch & 0xf];
        } else {
            stream.put(static_cast<char>(ch));
        }
    }
    stream.put('"');
}
}

bool WriteProcessMemorySnapshot(const std::filesystem::path& output,
    const char* phase, int round) {
    if (!phase || output.empty()) return false;
    // Collect into stack-owned POD before creating the output stream. These
    // observations are sequential; neither process-wide nor cross-API atomic.
    const ULONGLONG startedAt = GetTickCount64();
    const HANDLE process = GetCurrentProcess();
    const auto memory = ReadProcessMemory(process);
    const auto heap = ReadDefaultHeap();
    const auto virtualMemory = ReadVirtualMemory();
    const auto resources = ReadResources(process);
    const ULONGLONG endedAt = GetTickCount64();
    const bool complete = memory.complete && heap.complete && heap.countComplete &&
        virtualMemory.complete && resources.complete;
    try {
        std::ofstream stream(output, std::ios::out | std::ios::app | std::ios::binary);
        if (!stream) return false;
        stream.imbue(std::locale::classic());
        stream << std::boolalpha << "{\"schemaVersion\":1,\"phase\":";
        WriteJsonString(stream, phase);
        stream << ",\"round\":" << round << ",\"pid\":" << GetCurrentProcessId()
               << ",\"tickMs\":" << startedAt << ",\"collectionEndTickMs\":" << endedAt
               << ",\"collectionDurationMs\":" << endedAt - startedAt
               << ",\"diagnosticsCompleteWithinDeclaredCoverage\":" << complete
               << ",\"atomic\":false,\"processMemory\":{\"complete\":" << memory.complete
               << ",\"error\":" << memory.error
               << ",\"privateCommitBytes\":" << memory.counters.PrivateUsage
               << ",\"workingSetBytes\":" << memory.counters.WorkingSetSize
               << ",\"peakWorkingSetBytes\":" << memory.counters.PeakWorkingSetSize
               << ",\"pageFaultCount\":" << memory.counters.PageFaultCount
               << "},\"heap\":{\"coverage\":\"default_process_heap_only\""
               << ",\"allProcessHeapsWalked\":false,\"allocatorCoverageComplete\":false"
               << ",\"complete\":" << heap.complete << ",\"status\":";
        WriteJsonString(stream, heap.status);
        stream << ",\"error\":" << heap.error << ",\"processHeapCount\":" << heap.processHeapCount
               << ",\"countComplete\":" << heap.countComplete << ",\"countError\":" << heap.countError
               << ",\"compatibilityKnown\":" << heap.compatibilityKnown
               << ",\"compatibility\":" << heap.compatibility
               << ",\"lockSucceeded\":" << heap.lockSucceeded
               << ",\"unlockSucceeded\":" << heap.unlockSucceeded
               << ",\"walkEndError\":" << heap.walkEndError << ",\"unlockError\":" << heap.unlockError
               << ",\"busyBytes\":" << heap.busyBytes << ",\"busyBlocks\":" << heap.busyBlocks
               << ",\"freeBytes\":" << heap.freeBytes << ",\"freeBlocks\":" << heap.freeBlocks
               << ",\"busyOverheadBytes\":" << heap.busyOverheadBytes
               << ",\"freeOverheadBytes\":" << heap.freeOverheadBytes
               << ",\"regionCount\":" << heap.regionCount
               << ",\"uncommittedRangeBytes\":" << heap.uncommittedRangeBytes
               << "},\"virtualMemory\":{\"complete\":" << virtualMemory.complete << ",\"status\":";
        WriteJsonString(stream, virtualMemory.status);
        stream << ",\"error\":" << virtualMemory.error
               << ",\"queriedRegions\":" << virtualMemory.queriedRegions
               << ",\"privateCommittedBytes\":" << virtualMemory.privateCommittedBytes
               << ",\"mappedCommittedBytes\":" << virtualMemory.mappedCommittedBytes
               << ",\"imageCommittedBytes\":" << virtualMemory.imageCommittedBytes
               << ",\"otherCommittedBytes\":" << virtualMemory.otherCommittedBytes
               << ",\"privateReservedBytes\":" << virtualMemory.privateReservedBytes
               << ",\"privateAllocationCount\":" << virtualMemory.privateAllocationCount
               << ",\"privateCommittedAllocationCount\":" << virtualMemory.privateCommittedAllocationCount
               << ",\"scannedThroughAddress\":" << virtualMemory.scannedThroughAddress
               << "},\"resources\":{\"complete\":" << resources.complete
               << ",\"handleCountComplete\":" << resources.handleCountComplete
               << ",\"handleCount\":" << resources.handleCount << ",\"handleError\":" << resources.handleError
               << ",\"gdiComplete\":" << resources.gdiComplete << ",\"gdiObjects\":" << resources.gdiObjects
               << ",\"gdiError\":" << resources.gdiError << ",\"userComplete\":" << resources.userComplete
               << ",\"userObjects\":" << resources.userObjects << ",\"userError\":" << resources.userError
               << "},\"limitations\":[\"default_heap_blocks_are_not_all_process_allocations\","
                  "\"heap_bytes_exclude_unreported_allocator_storage\","
                  "\"virtual_types_include_copy_on_write_pages_in_original_mapped_or_image_type\","
                  "\"private_allocation_counts_include_reserved_allocations\","
                  "\"working_set_is_total_not_private_working_set\","
                  "\"sequential_observations_include_probe_overhead\","
                  "\"no_allocation_stacks_or_object_ownership\"]}\n";
        stream.close();
        return !stream.fail();
    } catch (...) {
        return false;
    }
}
