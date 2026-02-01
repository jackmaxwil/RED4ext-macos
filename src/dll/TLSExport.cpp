#include "stdafx.hpp"

#ifdef RED4EXT_PLATFORM_MACOS

#include <atomic>
#include <cstddef>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>

namespace RED4ext::TlsExportDetail
{
std::atomic<int> g_tlsPthreadKey{-1};

struct VmRegion
{
    mach_vm_address_t start = 0;
    mach_vm_address_t end = 0;
    vm_prot_t protection = 0;
};

bool QueryRegion(const void* aPtr, VmRegion& aOut)
{
    if (!aPtr)
        return false;

    const auto addr = static_cast<mach_vm_address_t>(reinterpret_cast<uintptr_t>(aPtr));
    if (addr == 0)
        return false;

    mach_vm_address_t query = addr;
    mach_vm_size_t regionSize = 0;
    vm_region_basic_info_data_64_t info{};
    mach_msg_type_number_t infoCount = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t objectName = MACH_PORT_NULL;

    const auto kr = mach_vm_region(mach_task_self(), &query, &regionSize, VM_REGION_BASIC_INFO_64,
                                   reinterpret_cast<vm_region_info_t>(&info), &infoCount, &objectName);
    if (kr != KERN_SUCCESS)
        return false;

    aOut.start = query;
    aOut.end = query + regionSize;
    aOut.protection = info.protection;
    return true;
}

bool IsReadableRange(const void* aPtr, std::size_t aSize)
{
    if (!aPtr || aSize == 0)
        return false;

    VmRegion region{};
    if (!QueryRegion(aPtr, region))
        return false;

    if ((region.protection & VM_PROT_READ) == 0)
        return false;

    const auto start = static_cast<mach_vm_address_t>(reinterpret_cast<uintptr_t>(aPtr));
    const auto end = start + static_cast<mach_vm_address_t>(aSize);
    if (end < start)
        return false;

    return start >= region.start && end <= region.end;
}

bool IsExecutableAddress(const void* aPtr)
{
    VmRegion region{};
    if (!QueryRegion(aPtr, region))
        return false;

    return (region.protection & VM_PROT_EXECUTE) != 0;
}

bool LooksLikeGameTls(RED4ext::TLS* aPtr)
{
    if (!aPtr)
        return false;

    VmRegion region{};
    if (!QueryRegion(aPtr, region))
        return false;

    if ((region.protection & (VM_PROT_READ | VM_PROT_WRITE)) != (VM_PROT_READ | VM_PROT_WRITE))
        return false;

    if ((region.protection & VM_PROT_EXECUTE) != 0)
        return false;

    if (!IsReadableRange(aPtr, sizeof(RED4ext::TLS)))
        return false;

    // Heuristic: if the first word looks like a vtable pointer, require it to point to executable memory.
    const auto firstWord = *reinterpret_cast<const uintptr_t*>(aPtr);
    if (firstWord != 0 && IsExecutableAddress(reinterpret_cast<const void*>(firstWord)))
        return true;

    // Fallback heuristic: density of pointer-like values in the first cache line.
    const auto* words = reinterpret_cast<const uintptr_t*>(aPtr);
    constexpr std::size_t kSampleWords = 16;
    std::size_t pointerLike = 0;

    for (std::size_t i = 0; i < kSampleWords; ++i)
    {
        const uintptr_t v = words[i];
        if (v == 0)
            continue;

        if ((v & 0x7) != 0)
            continue;

        if (v < 0x10000)
            continue;

        if (IsReadableRange(reinterpret_cast<const void*>(v), sizeof(uintptr_t)))
            ++pointerLike;
    }

    return pointerLike >= 3;
}

RED4ext::TLS* GetFromPthreadKey(int aKey)
{
    if (aKey < 0)
        return nullptr;

    return static_cast<RED4ext::TLS*>(pthread_getspecific(static_cast<pthread_key_t>(aKey)));
}

RED4ext::TLS* DiscoverTlsViaPthreadTsd()
{
    for (int key = 0; key < PTHREAD_KEYS_MAX; ++key)
    {
        auto* ptr = GetFromPthreadKey(key);
        if (!ptr)
            continue;

        if (!LooksLikeGameTls(ptr))
            continue;

        int expected = -1;
        if (g_tlsPthreadKey.compare_exchange_strong(expected, key))
            spdlog::debug("[TLS] Discovered TLS pthread key: {}", key);

        return ptr;
    }

    return nullptr;
}
} // namespace RED4ext::TlsExportDetail

extern "C" __attribute__((visibility("default"))) RED4ext::TLS* RED4EXT_CALL RED4ext_GetTLS()
{
    if (const int key = RED4ext::TlsExportDetail::g_tlsPthreadKey.load(std::memory_order_relaxed); key >= 0)
    {
        auto* ptr = RED4ext::TlsExportDetail::GetFromPthreadKey(key);
        if (!ptr)
            return nullptr;

        return RED4ext::TlsExportDetail::LooksLikeGameTls(ptr) ? ptr : nullptr;
    }

    return RED4ext::TlsExportDetail::DiscoverTlsViaPthreadTsd();
}

extern "C" __attribute__((visibility("default"))) bool RED4EXT_CALL RED4ext_IsTLSInitialized()
{
    return RED4ext_GetTLS() != nullptr;
}

#endif
