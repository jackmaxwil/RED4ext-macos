#include "NativeHook.hpp"

#ifdef RED4EXT_PLATFORM_MACOS

#include "Detail/AddressHashes.hpp"

#include <libkern/OSCacheControl.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/arm/thread_status.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/thread_act.h>
#include <mach/vm_region.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

namespace NativeHook
{
namespace
{

constexpr uint32_t kMaxThreads = 2048;
constexpr uint32_t kMaxSites = 64;
constexpr int kPcRetries = 8;

struct Node
{
    void* detour = nullptr;
    void** pp = nullptr;
    uint8_t* stub = nullptr;
    uint32_t stubBytes[12]{};
    std::atomic<uint64_t> hits{0};
    Node* older = nullptr;
    bool committed = false;
    bool detach = false;
    char name[160]{};
    char owner[128]{};
};

struct Site
{
    uintptr_t target = 0;
    uint32_t original[3]{};
    uint32_t patchBytes = 4;
    bool far = false;
    uint8_t* page = nullptr;
    uint32_t tramp[40]{};
    uint32_t trampCount = 0;
    Node* head = nullptr;
    bool installed = false;
    int installKr = 0;
    uint32_t nextStub = 256;
    Site* next = nullptr;
};

struct Ident
{
    uintptr_t target = 0;
    char name[160]{};
    char owner[128]{};
    Ident* next = nullptr;
};

struct Failed
{
    bool used = false;
    uintptr_t target = 0;
    int installKr = -1;
    char name[160]{};
    char owner[128]{};
    char patch[8]{};
};

std::mutex g_mu;
Site* g_sites = nullptr;
Ident* g_idents = nullptr;
Failed g_failed[64]{};
bool g_open = false;
std::atomic<bool> g_writesAllowed{true};
#ifdef RED4EXT_NATIVE_HOOK_TEST
std::atomic<bool> g_forceFar{false};
#endif

ProtectOp g_protLog[64]{};
uint32_t g_protCount = 0;

std::string g_statsPath;
std::string g_statsUuid;
std::atomic<bool> g_statsStop{false};
std::atomic<bool> g_statsRunning{false};
std::thread g_statsThread;

uintptr_t PageSize()
{
    static const uintptr_t page = vm_page_size != 0 ? static_cast<uintptr_t>(vm_page_size) : 16384;
    return page;
}

uintptr_t PageOf(uintptr_t addr)
{
    return addr & ~(PageSize() - 1);
}

void CopyStr(char* dst, size_t cap, const char* src)
{
    if (cap == 0)
    {
        return;
    }

    if (src == nullptr)
    {
        src = "";
    }

    std::snprintf(dst, cap, "%s", src);
}

int64_t SignExtend(uint32_t value, int bits)
{
    const uint32_t mask = (1u << bits) - 1u;
    uint32_t v = value & mask;
    if ((v & (1u << (bits - 1))) != 0)
    {
        v |= ~mask;
    }

    return static_cast<int32_t>(v);
}

bool IsPcRelative(uint32_t instr)
{
    if ((instr & 0x1F000000u) == 0x10000000u)
    {
        return true;
    }

    if ((instr & 0x7C000000u) == 0x14000000u)
    {
        return true;
    }

    if ((instr & 0xFF000010u) == 0x54000000u)
    {
        return true;
    }

    if ((instr & 0x7E000000u) == 0x34000000u)
    {
        return true;
    }

    if ((instr & 0x7E000000u) == 0x36000000u)
    {
        return true;
    }

    return (instr & 0x3B000000u) == 0x18000000u;
}

void Push(RelocOut& out, uint32_t word)
{
    if (out.count < 24)
    {
        out.words[out.count++] = word;
    }
}

void EmitMovAbs(RelocOut& out, uint32_t rd, uint64_t value)
{
    const auto movz = [](uint32_t reg, uint16_t imm, uint32_t shift)
    {
        const uint32_t hw = (shift / 16) & 3u;
        return 0xD2800000u | (hw << 21) | (static_cast<uint32_t>(imm) << 5) | (reg & 31u);
    };
    const auto movk = [](uint32_t reg, uint16_t imm, uint32_t shift)
    {
        const uint32_t hw = (shift / 16) & 3u;
        return 0xF2800000u | (hw << 21) | (static_cast<uint32_t>(imm) << 5) | (reg & 31u);
    };

    Push(out, movz(rd, static_cast<uint16_t>(value & 0xFFFFu), 0));
    Push(out, movk(rd, static_cast<uint16_t>((value >> 16) & 0xFFFFu), 16));
    Push(out, movk(rd, static_cast<uint16_t>((value >> 32) & 0xFFFFu), 32));
    Push(out, movk(rd, static_cast<uint16_t>((value >> 48) & 0xFFFFu), 48));
}

void EmitAbsJump(RelocOut& out, uint64_t dest)
{
    Push(out, 0x58000050u);
    Push(out, 0xD61F0200u);
    Push(out, static_cast<uint32_t>(dest));
    Push(out, static_cast<uint32_t>(dest >> 32));
}

void EmitAbsCall(RelocOut& out, uint64_t dest)
{
    Push(out, 0x58000070u);
    Push(out, 0xD63F0200u);
    Push(out, 0x14000003u);
    Push(out, static_cast<uint32_t>(dest));
    Push(out, static_cast<uint32_t>(dest >> 32));
}

bool TryEncodeB(uint64_t at, uint64_t dest, uint32_t* out)
{
    const int64_t diff = static_cast<int64_t>(dest) - static_cast<int64_t>(at);
    if ((diff & 3) != 0)
    {
        return false;
    }

    const int64_t imm = diff >> 2;
    if (imm < -(1 << 25) || imm >= (1 << 25))
    {
        return false;
    }

    *out = 0x14000000u | (static_cast<uint32_t>(imm) & 0x03FFFFFFu);
    return true;
}

bool TryEncodeBCond(uint64_t at, uint64_t dest, uint32_t cond, uint32_t* out)
{
    const int64_t diff = static_cast<int64_t>(dest) - static_cast<int64_t>(at);
    if ((diff & 3) != 0)
    {
        return false;
    }

    const int64_t imm = diff >> 2;
    if (imm < -(1 << 18) || imm >= (1 << 18))
    {
        return false;
    }

    *out = 0x54000000u | ((static_cast<uint32_t>(imm) & 0x7FFFFu) << 5) | (cond & 0xFu);
    return true;
}

bool TryEncodeCbz(uint64_t at, uint64_t dest, uint32_t sf, uint32_t op, uint32_t rt, uint32_t* out)
{
    const int64_t diff = static_cast<int64_t>(dest) - static_cast<int64_t>(at);
    if ((diff & 3) != 0)
    {
        return false;
    }

    const int64_t imm = diff >> 2;
    if (imm < -(1 << 18) || imm >= (1 << 18))
    {
        return false;
    }

    *out = (sf << 31) | 0x34000000u | (op << 24) | ((static_cast<uint32_t>(imm) & 0x7FFFFu) << 5) | (rt & 31u);
    return true;
}

bool TryEncodeTbz(uint64_t at, uint64_t dest, uint32_t op, uint32_t bit, uint32_t rt, uint32_t* out)
{
    const int64_t diff = static_cast<int64_t>(dest) - static_cast<int64_t>(at);
    if ((diff & 3) != 0)
    {
        return false;
    }

    const int64_t imm = diff >> 2;
    if (imm < -(1 << 13) || imm >= (1 << 13))
    {
        return false;
    }

    const uint32_t b5 = (bit >> 5) & 1u;
    const uint32_t b40 = bit & 31u;
    *out = (b5 << 31) | 0x36000000u | (op << 24) | (b40 << 19) | ((static_cast<uint32_t>(imm) & 0x3FFFu) << 5) |
           (rt & 31u);
    return true;
}

bool InBranchRange(uintptr_t from, uintptr_t to)
{
    const int64_t diff = static_cast<int64_t>(to) - static_cast<int64_t>(from);
    const int64_t limit = (static_cast<int64_t>(1) << 25) * 4;
    return diff > -limit && diff < limit - 4;
}

bool InAdrpRange(uintptr_t pc, uintptr_t dest)
{
    const int64_t pages = (static_cast<int64_t>(dest) >> 12) - (static_cast<int64_t>(pc) >> 12);
    return pages >= -(static_cast<int64_t>(1) << 20) && pages < (static_cast<int64_t>(1) << 20);
}

bool WantFar()
{
#ifdef RED4EXT_NATIVE_HOOK_TEST
    return g_forceFar.load(std::memory_order_relaxed);
#else
    return false;
#endif
}

bool NeedsCopy(uintptr_t page)
{
    vm_region_extended_info_data_t info{};
    mach_msg_type_number_t count = VM_REGION_EXTENDED_INFO_COUNT;
    mach_port_t object = MACH_PORT_NULL;
    mach_vm_address_t addr = page;
    mach_vm_size_t size = 0;
    const kern_return_t kr = mach_vm_region(mach_task_self(), &addr, &size, VM_REGION_EXTENDED_INFO,
                                            reinterpret_cast<vm_region_info_t>(&info), &count, &object);
    if (kr != KERN_SUCCESS || addr > page)
    {
        return true;
    }

    const unsigned mode = info.share_mode;
    return mode != SM_PRIVATE && mode != SM_PRIVATE_ALIASED && mode != SM_EMPTY;
}

void RecordProt(uint32_t prot, bool copy, bool restore)
{
    if (g_protCount < 64)
    {
        g_protLog[g_protCount++] = ProtectOp{prot, copy, restore};
    }
}

kern_return_t ProtectWritable(uintptr_t page)
{
    const bool copy = NeedsCopy(page);
    vm_prot_t prot = VM_PROT_READ | VM_PROT_WRITE;
    RecordProt(VM_PROT_READ | VM_PROT_WRITE, copy, false);
    if (copy)
    {
        prot |= VM_PROT_COPY;
    }

    return mach_vm_protect(mach_task_self(), page, PageSize(), FALSE, prot);
}

kern_return_t ProtectExecutable(uintptr_t page)
{
    const vm_prot_t prot = VM_PROT_READ | VM_PROT_EXECUTE;
    RecordProt(prot, false, true);
    return mach_vm_protect(mach_task_self(), page, PageSize(), FALSE, prot);
}

bool AllocateWindow(uintptr_t target, uint8_t** out, uintptr_t limit, int maxSteps, bool adrp)
{
    const uintptr_t page = PageSize();
    uintptr_t cursor = target > limit ? (target - limit) & ~(page - 1) : page;
    const uintptr_t hi = target + limit;

    for (int n = 0; n < maxSteps && cursor < hi; ++n)
    {
        mach_vm_address_t region = cursor;
        mach_vm_size_t regionSize = 0;
        vm_region_basic_info_data_64_t info{};
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        const kern_return_t kr = mach_vm_region(mach_task_self(), &region, &regionSize, VM_REGION_BASIC_INFO_64,
                                                reinterpret_cast<vm_region_info_t>(&info), &count, &object);
        const mach_vm_address_t gapEnd = kr == KERN_SUCCESS ? region : hi;
        const mach_vm_address_t capped = gapEnd > hi ? hi : gapEnd;
        if (capped > cursor && capped - cursor >= page)
        {
            mach_vm_address_t hint = cursor;
            if (mach_vm_allocate(mach_task_self(), &hint, page, VM_FLAGS_FIXED) == KERN_SUCCESS)
            {
                const auto got = static_cast<uintptr_t>(hint);
                if (adrp ? InAdrpRange(target, got) : InBranchRange(target, got))
                {
                    *out = reinterpret_cast<uint8_t*>(hint);
                    return true;
                }

                mach_vm_deallocate(mach_task_self(), hint, page);
            }
        }

        if (kr != KERN_SUCCESS)
        {
            break;
        }

        const mach_vm_address_t next = region + regionSize;
        if (next <= cursor)
        {
            break;
        }

        cursor = static_cast<uintptr_t>(next);
    }

    return false;
}

bool AllocateNear(uintptr_t target, uint8_t** out)
{
    const uintptr_t limit = (static_cast<uintptr_t>(1) << 25) * 4 - 4;
    return AllocateWindow(target, out, limit, 8192, false);
}

bool AllocateFar(uintptr_t target, uint8_t** out)
{
    const uintptr_t page = PageSize();
    mach_vm_address_t hint = 0;
    if (mach_vm_allocate(mach_task_self(), &hint, page, VM_FLAGS_ANYWHERE) == KERN_SUCCESS)
    {
        if (InAdrpRange(target, static_cast<uintptr_t>(hint)))
        {
            *out = reinterpret_cast<uint8_t*>(hint);
            return true;
        }

        mach_vm_deallocate(mach_task_self(), hint, page);
    }

    // ponytail: ANYWHERE missed the ±4GiB ADRP window; walk gaps inside it.
    const uintptr_t limit = (static_cast<uintptr_t>(1) << 32) - page;
    return AllocateWindow(target, out, limit, 16384, true);
}

bool TryEncodeAdrp(uint64_t pc, uint64_t page, uint32_t rd, uint32_t* out)
{
    const int64_t imm = (static_cast<int64_t>(page) >> 12) - (static_cast<int64_t>(pc) >> 12);
    if (imm < -(static_cast<int64_t>(1) << 20) || imm >= (static_cast<int64_t>(1) << 20))
    {
        return false;
    }

    const uint32_t bits = static_cast<uint32_t>(imm);
    const uint32_t immlo = bits & 3u;
    const uint32_t immhi = (bits >> 2) & 0x7FFFFu;
    *out = 0x90000000u | (immlo << 29) | (immhi << 5) | (rd & 31u);
    return true;
}

bool EncodeFar(uintptr_t pc, uintptr_t island, uint32_t out[3])
{
    const uintptr_t page = island & ~static_cast<uintptr_t>(0xFFF);
    const uint32_t off = static_cast<uint32_t>(island & 0xFFF);
    if (!TryEncodeAdrp(pc, page, 17, &out[0]))
    {
        return false;
    }

    out[1] = 0x91000000u | (off << 10) | (17u << 5) | 17u;
    out[2] = 0xD61F0220u;
    return true;
}

// x16/x17 only. x0-x8 are left alone so a struct return pointer survives.
void EncodeHitStub(uint32_t* words, uint64_t counter, uint64_t detour)
{
    words[0] = 0x580000D0u;
    words[1] = 0xD2800031u;
    words[2] = 0xF8310211u;
    words[3] = 0x580000B0u;
    words[4] = 0xD61F0200u;
    words[5] = 0xD503201Fu;
    std::memcpy(words + 6, &counter, sizeof(counter));
    std::memcpy(words + 8, &detour, sizeof(detour));
}

void EncodeIsland(uint32_t* words, uint64_t dest)
{
    words[0] = 0x58000050u;
    words[1] = 0xD61F0200u;
    std::memcpy(words + 2, &dest, sizeof(dest));
}

Site* FindSite(uintptr_t target)
{
    for (Site* site = g_sites; site != nullptr; site = site->next)
    {
        if (site->target == target)
        {
            return site;
        }
    }

    return nullptr;
}

void RememberFailure(uintptr_t target, int kr, const char* name, const char* owner, const char* patch)
{
    for (Failed& failed : g_failed)
    {
        if (!failed.used)
        {
            failed.used = true;
            failed.target = target;
            failed.installKr = kr;
            CopyStr(failed.name, sizeof(failed.name), name != nullptr ? name : "hook");
            CopyStr(failed.owner, sizeof(failed.owner), owner != nullptr ? owner : "RED4ext");
            CopyStr(failed.patch, sizeof(failed.patch), patch != nullptr ? patch : "near");
            return;
        }
    }
}

Ident* TakeIdent(uintptr_t target)
{
    Ident** link = &g_idents;
    while (*link != nullptr)
    {
        if ((*link)->target == target)
        {
            Ident* found = *link;
            *link = found->next;
            return found;
        }

        link = &(*link)->next;
    }

    return nullptr;
}

int32_t FailAttach(uintptr_t target, int32_t kr, const char* patch)
{
    Ident* ident = TakeIdent(target);
    RememberFailure(target, kr, ident != nullptr ? ident->name : "hook", ident != nullptr ? ident->owner : "RED4ext",
                    patch);
    delete ident;
    return kr;
}

void ConsumeIdent(uintptr_t target, Node* node)
{
    Ident* ident = TakeIdent(target);
    if (ident != nullptr)
    {
        CopyStr(node->name, sizeof(node->name), ident->name);
        CopyStr(node->owner, sizeof(node->owner), ident->owner);
        delete ident;
        return;
    }

    CopyStr(node->name, sizeof(node->name), "hook");
    CopyStr(node->owner, sizeof(node->owner), "RED4ext");
}

Node* LiveHead(Site* site)
{
    for (Node* node = site->head; node != nullptr; node = node->older)
    {
        if (!node->detach)
        {
            return node;
        }
    }

    return nullptr;
}

void RestorePp(Node* node, Site* site)
{
    if (node->pp != nullptr)
    {
        *node->pp = reinterpret_cast<void*>(site->target);
    }
}

void Unlink(Site* site, Node* node)
{
    Node* newer = nullptr;
    Node** link = &site->head;
    while (*link != nullptr && *link != node)
    {
        newer = *link;
        link = &(*link)->older;
    }

    if (*link == nullptr)
    {
        return;
    }

    *link = node->older;
    if (newer != nullptr && newer->pp != nullptr)
    {
        void* previous =
            node->older != nullptr && !node->older->detach ? node->older->detour : static_cast<void*>(site->page + 16);
        *newer->pp = previous;
    }

    RestorePp(node, site);
}

void FreePage(uint8_t* page)
{
    if (page != nullptr)
    {
        mach_vm_deallocate(mach_task_self(), reinterpret_cast<mach_vm_address_t>(page), PageSize());
    }
}

void DestroySite(Site* site)
{
    Site** link = &g_sites;
    while (*link != nullptr)
    {
        if (*link == site)
        {
            *link = site->next;
            break;
        }

        link = &(*link)->next;
    }

    Node* node = site->head;
    while (node != nullptr)
    {
        Node* next = node->older;
        delete node;
        node = next;
    }

    FreePage(site->page);
    delete site;
}

int32_t FailSite(Site* site, int32_t kr)
{
    const uintptr_t target = site->target;
    const char* patch = site->far ? "far" : "near";
    DestroySite(site);
    return FailAttach(target, kr, patch);
}

bool SiteHasLive(Site* site)
{
    return LiveHead(site) != nullptr;
}

bool NeedsWork()
{
    for (Site* site = g_sites; site != nullptr; site = site->next)
    {
        for (Node* node = site->head; node != nullptr; node = node->older)
        {
            if (!node->committed || node->detach)
            {
                return true;
            }
        }
    }

    return false;
}

uint64_t ThreadId(thread_t thread)
{
    thread_identifier_info_data_t info{};
    mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
    if (thread_info(thread, THREAD_IDENTIFIER_INFO, reinterpret_cast<thread_info_t>(&info), &count) != KERN_SUCCESS)
    {
        return 0;
    }

    return info.thread_id;
}

uint64_t CurrentThreadId()
{
    const thread_t self = mach_thread_self();
    const uint64_t id = ThreadId(self);
    mach_port_deallocate(mach_task_self(), self);
    return id;
}

void FinishThreads(thread_act_array_t threads, mach_msg_type_number_t count, uint64_t selfId, bool resume)
{
    if (threads == nullptr)
    {
        return;
    }

    if (resume)
    {
        for (mach_msg_type_number_t i = 0; i < count; ++i)
        {
            if (ThreadId(threads[i]) != selfId)
            {
                thread_resume(threads[i]);
            }
        }
    }

    for (mach_msg_type_number_t i = 0; i < count; ++i)
    {
        mach_port_deallocate(mach_task_self(), threads[i]);
    }

    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_t));
}

bool PcOnPages(uint64_t pc, const uintptr_t* pages, uint32_t pageCount)
{
    const uintptr_t page = PageOf(static_cast<uintptr_t>(pc));
    for (uint32_t i = 0; i < pageCount; ++i)
    {
        if (pages[i] == page)
        {
            return true;
        }
    }

    return false;
}

bool PatchSites(Site** sites, uint32_t siteCount, const uintptr_t* hotPages, uint32_t hotCount)
{
    struct Snap
    {
        void* addr;
        uint8_t bytes[16];
        uint32_t size;
        uintptr_t page;
    };

    Snap snaps[kMaxSites * 2]{};
    uint32_t snapCount = 0;
    auto remember = [&](void* addr, uint32_t size, uintptr_t page)
    {
        if (snapCount >= kMaxSites * 2)
        {
            return;
        }

        snaps[snapCount].addr = addr;
        snaps[snapCount].size = size;
        snaps[snapCount].page = page;
        std::memcpy(snaps[snapCount].bytes, addr, size);
        ++snapCount;
    };

    for (uint32_t i = 0; i < siteCount; ++i)
    {
        Site* site = sites[i];
        const bool writeIsland = true;
        const bool live = SiteHasLive(site);
        if (writeIsland)
        {
            remember(site->page, 16, PageOf(reinterpret_cast<uintptr_t>(site->page)));
        }

        if (!site->installed && live)
        {
            remember(reinterpret_cast<void*>(site->target), site->patchBytes, PageOf(site->target));
        }
        else if (site->installed && !live)
        {
            remember(reinterpret_cast<void*>(site->target), site->patchBytes, PageOf(site->target));
        }
    }

    auto rollback = [&]()
    {
        uintptr_t rolled[kMaxSites * 2]{};
        uint32_t rolledCount = 0;
        for (uint32_t i = 0; i < snapCount; ++i)
        {
            bool seen = false;
            for (uint32_t j = 0; j < rolledCount; ++j)
            {
                if (rolled[j] == snaps[i].page)
                {
                    seen = true;
                    break;
                }
            }

            if (!seen && rolledCount < kMaxSites * 2)
            {
                ProtectWritable(snaps[i].page);
                rolled[rolledCount++] = snaps[i].page;
            }

            std::memcpy(snaps[i].addr, snaps[i].bytes, snaps[i].size);
        }

        for (uint32_t j = 0; j < rolledCount; ++j)
        {
            sys_icache_invalidate(reinterpret_cast<void*>(rolled[j]), PageSize());
            ProtectExecutable(rolled[j]);
        }
    };

    (void)hotPages;
    (void)hotCount;

    for (uint32_t i = 0; i < siteCount; ++i)
    {
        Site* site = sites[i];
        const kern_return_t krw = ProtectWritable(reinterpret_cast<uintptr_t>(site->page));
        if (krw != KERN_SUCCESS)
        {
            site->installKr = static_cast<int>(krw);
            rollback();
            return false;
        }

        if (!site->installed)
        {
            std::memcpy(site->page + 16, site->tramp, site->trampCount * sizeof(uint32_t));
        }

        for (Node* node = site->head; node != nullptr; node = node->older)
        {
            if (!node->detach)
            {
                std::memcpy(node->stub, node->stubBytes, 40);
            }
        }

        Node* live = LiveHead(site);
        if (live != nullptr)
        {
            uint32_t island[4]{};
            EncodeIsland(island, reinterpret_cast<uint64_t>(live->stub));
            std::memcpy(site->page, island, sizeof(island));
        }

        sys_icache_invalidate(site->page, PageSize());
        const kern_return_t krx = ProtectExecutable(reinterpret_cast<uintptr_t>(site->page));
        if (krx != KERN_SUCCESS)
        {
            site->installKr = static_cast<int>(krx);
            rollback();
            return false;
        }
    }

    uintptr_t targetPages[kMaxSites]{};
    uint32_t targetPageCount = 0;
    for (uint32_t i = 0; i < siteCount; ++i)
    {
        Site* site = sites[i];
        const bool live = SiteHasLive(site);
        const bool patch = !site->installed && live;
        const bool restore = site->installed && !live;
        if (!patch && !restore)
        {
            continue;
        }

        const uintptr_t page = PageOf(site->target);
        bool seen = false;
        for (uint32_t j = 0; j < targetPageCount; ++j)
        {
            if (targetPages[j] == page)
            {
                seen = true;
                break;
            }
        }

        if (!seen)
        {
            targetPages[targetPageCount++] = page;
        }
    }

    uintptr_t finished[kMaxSites]{};
    uint32_t finishedCount = 0;
    for (uint32_t p = 0; p < targetPageCount; ++p)
    {
        const uintptr_t page = targetPages[p];
        const kern_return_t krw = ProtectWritable(page);
        if (krw != KERN_SUCCESS)
        {
            for (uint32_t i = 0; i < siteCount; ++i)
            {
                if (PageOf(sites[i]->target) == page)
                {
                    sites[i]->installKr = static_cast<int>(krw);
                }
            }

            rollback();
            return false;
        }

        for (uint32_t i = 0; i < siteCount; ++i)
        {
            Site* site = sites[i];
            if (PageOf(site->target) != page)
            {
                continue;
            }

            const bool live = SiteHasLive(site);
            if (!site->installed && live)
            {
                if (site->far)
                {
                    uint32_t words[3]{};
                    if (!EncodeFar(site->target, reinterpret_cast<uintptr_t>(site->page), words))
                    {
                        site->installKr = kErrUnreachable;
                        std::memcpy(reinterpret_cast<void*>(site->target), site->original, site->patchBytes);
                        sys_icache_invalidate(reinterpret_cast<void*>(page), PageSize());
                        ProtectExecutable(page);
                        rollback();
                        return false;
                    }

                    std::memcpy(reinterpret_cast<void*>(site->target), words, site->patchBytes);
                }
                else
                {
                    uint32_t branch = 0;
                    if (!TryEncodeB(site->target, reinterpret_cast<uint64_t>(site->page), &branch))
                    {
                        site->installKr = kErrUnreachable;
                        std::memcpy(reinterpret_cast<void*>(site->target), site->original, site->patchBytes);
                        sys_icache_invalidate(reinterpret_cast<void*>(page), PageSize());
                        ProtectExecutable(page);
                        rollback();
                        return false;
                    }

                    std::memcpy(reinterpret_cast<void*>(site->target), &branch, 4);
                }
            }
            else if (site->installed && !live)
            {
                std::memcpy(reinterpret_cast<void*>(site->target), site->original, site->patchBytes);
            }
        }

        sys_icache_invalidate(reinterpret_cast<void*>(page), PageSize());
        const kern_return_t krx = ProtectExecutable(page);
        if (krx != KERN_SUCCESS)
        {
            for (uint32_t i = 0; i < siteCount; ++i)
            {
                Site* site = sites[i];
                if (PageOf(site->target) == page)
                {
                    std::memcpy(reinterpret_cast<void*>(site->target), site->original, site->patchBytes);
                    site->installKr = static_cast<int>(krx);
                }
            }

            sys_icache_invalidate(reinterpret_cast<void*>(page), PageSize());
            ProtectExecutable(page);
            for (uint32_t f = 0; f < finishedCount; ++f)
            {
                ProtectWritable(finished[f]);
                for (uint32_t i = 0; i < siteCount; ++i)
                {
                    if (PageOf(sites[i]->target) == finished[f])
                    {
                        std::memcpy(reinterpret_cast<void*>(sites[i]->target), sites[i]->original,
                                    sites[i]->patchBytes);
                    }
                }

                sys_icache_invalidate(reinterpret_cast<void*>(finished[f]), PageSize());
                ProtectExecutable(finished[f]);
            }

            rollback();
            return false;
        }

        finished[finishedCount++] = page;
    }

    for (uint32_t i = 0; i < siteCount; ++i)
    {
        Site* site = sites[i];
        const bool live = SiteHasLive(site);
        if (!live)
        {
            if (site->installed || site->page != nullptr)
            {
                FreePage(site->page);
                site->page = nullptr;
            }

            site->installed = false;
        }
        else
        {
            site->installed = true;
            site->installKr = 0;
        }
    }

    return true;
}

bool StopAndPatch(Site** sites, uint32_t siteCount)
{
    uintptr_t hotPages[kMaxSites * 2]{};
    uint32_t hotCount = 0;
    auto addHot = [&](uintptr_t page)
    {
        for (uint32_t i = 0; i < hotCount; ++i)
        {
            if (hotPages[i] == page)
            {
                return;
            }
        }

        if (hotCount < kMaxSites * 2)
        {
            hotPages[hotCount++] = page;
        }
    };

    for (uint32_t i = 0; i < siteCount; ++i)
    {
        Site* site = sites[i];
        addHot(PageOf(reinterpret_cast<uintptr_t>(site->page)));
        const bool live = SiteHasLive(site);
        if ((!site->installed && live) || (site->installed && !live))
        {
            // Near patches refuse the whole target page. Far patches only
            // care about the 12-byte window, checked below.
            if (!site->far)
            {
                addHot(PageOf(site->target));
            }
        }
    }

    const uint64_t selfId = CurrentThreadId();
    for (int attempt = 0; attempt < kPcRetries; ++attempt)
    {
        thread_act_array_t threads = nullptr;
        mach_msg_type_number_t count = 0;
        if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS)
        {
            return false;
        }

        if (count > kMaxThreads)
        {
            FinishThreads(threads, count, selfId, false);
            return false;
        }

        uint64_t ids1[kMaxThreads]{};
        bool suspended = true;
        for (mach_msg_type_number_t i = 0; i < count; ++i)
        {
            ids1[i] = ThreadId(threads[i]);
            if (ids1[i] != selfId)
            {
                if (thread_suspend(threads[i]) != KERN_SUCCESS)
                {
                    suspended = false;
                }
            }
        }

        if (!suspended)
        {
            FinishThreads(threads, count, selfId, true);
            return false;
        }

        thread_act_array_t threads2 = nullptr;
        mach_msg_type_number_t count2 = 0;
        bool same = task_threads(mach_task_self(), &threads2, &count2) == KERN_SUCCESS && count2 == count &&
                    count2 <= kMaxThreads;
        uint64_t ids2[kMaxThreads]{};
        if (same)
        {
            for (mach_msg_type_number_t i = 0; i < count2; ++i)
            {
                ids2[i] = ThreadId(threads2[i]);
            }

            std::sort(ids1, ids1 + count);
            std::sort(ids2, ids2 + count2);
            same = std::memcmp(ids1, ids2, count * sizeof(uint64_t)) == 0;
        }

        if (threads2 != nullptr)
        {
            for (mach_msg_type_number_t i = 0; i < count2; ++i)
            {
                mach_port_deallocate(mach_task_self(), threads2[i]);
            }

            vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads2), count2 * sizeof(thread_t));
        }

        if (!same)
        {
            FinishThreads(threads, count, selfId, true);
            return false;
        }

        bool hot = false;
        for (mach_msg_type_number_t i = 0; i < count; ++i)
        {
            if (ThreadId(threads[i]) == selfId)
            {
                continue;
            }

            arm_thread_state64_t state{};
            mach_msg_type_number_t stateCount = ARM_THREAD_STATE64_COUNT;
            if (thread_get_state(threads[i], ARM_THREAD_STATE64, reinterpret_cast<thread_state_t>(&state),
                                 &stateCount) != KERN_SUCCESS)
            {
                hot = true;
                break;
            }

            const uint64_t pc = arm_thread_state64_get_pc(state);
            // Far patches overwrite 12 bytes. A stopped thread with PC in
            // (target, target+12) would resume mid-patch. PC is not rewritten:
            // Commit retries, then fails closed, same as a hot near page.
            bool farInterior = false;
            for (uint32_t s = 0; s < siteCount; ++s)
            {
                Site* site = sites[s];
                if (!site->far)
                {
                    continue;
                }

                const bool live = SiteHasLive(site);
                const bool changing = (!site->installed && live) || (site->installed && !live);
                if (changing && pc > site->target && pc < site->target + site->patchBytes)
                {
                    farInterior = true;
                    break;
                }
            }

            if (farInterior || PcOnPages(pc, hotPages, hotCount))
            {
                hot = true;
                break;
            }
        }

        if (hot)
        {
            FinishThreads(threads, count, selfId, true);
            if (attempt + 1 == kPcRetries)
            {
                return false;
            }

            usleep(static_cast<useconds_t>(1000u * static_cast<useconds_t>(attempt + 1)));
            continue;
        }

        const bool patched = PatchSites(sites, siteCount, hotPages, hotCount);
        FinishThreads(threads, count, selfId, true);
        return patched;
    }

    return false;
}

void CollectTrash(Node** trash, uint32_t* trashCount, Site** deadSites, uint32_t* deadCount)
{
    Site** link = &g_sites;
    while (*link != nullptr)
    {
        Site* site = *link;
        Node** nodeLink = &site->head;
        while (*nodeLink != nullptr)
        {
            Node* node = *nodeLink;
            if (node->detach || (!site->installed && !node->committed))
            {
                Unlink(site, node);
                if (*trashCount < kMaxSites * 4)
                {
                    trash[(*trashCount)++] = node;
                }
                else
                {
                    delete node;
                }

                continue;
            }

            if (site->installed)
            {
                node->committed = true;
            }

            nodeLink = &node->older;
        }

        if (site->head == nullptr && !site->installed)
        {
            *link = site->next;
            if (*deadCount < kMaxSites)
            {
                deadSites[(*deadCount)++] = site;
            }
            else
            {
                FreePage(site->page);
                delete site;
            }

            continue;
        }

        link = &site->next;
    }
}

void RollbackUnlocked()
{
    Node* trash[kMaxSites * 4]{};
    Site* dead[kMaxSites]{};
    uint32_t trashCount = 0;
    uint32_t deadCount = 0;

    for (Site* site = g_sites; site != nullptr; site = site->next)
    {
        for (Node* node = site->head; node != nullptr; node = node->older)
        {
            if (!node->committed)
            {
                node->detach = true;
            }
            else
            {
                node->detach = false;
            }
        }
    }

    CollectTrash(trash, &trashCount, dead, &deadCount);
    for (uint32_t i = 0; i < trashCount; ++i)
    {
        delete trash[i];
    }

    for (uint32_t i = 0; i < deadCount; ++i)
    {
        if (dead[i]->page != nullptr)
        {
            FreePage(dead[i]->page);
            dead[i]->page = nullptr;
        }

        delete dead[i];
    }

    g_open = false;
}

const mach_header_64* MainHeader()
{
    char buffer[4096];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0)
    {
        return nullptr;
    }

    const auto exeName = std::filesystem::path(buffer).filename();
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i)
    {
        const char* name = _dyld_get_image_name(i);
        if (name == nullptr)
        {
            continue;
        }

        if (std::filesystem::path(name).filename() == exeName)
        {
            return reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
        }
    }

    return nullptr;
}

void JsonEscape(std::string& out, const char* text)
{
    if (text == nullptr)
    {
        return;
    }

    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text); *cursor != 0; ++cursor)
    {
        if (*cursor == '\\' || *cursor == '"')
        {
            out.push_back('\\');
            out.push_back(static_cast<char>(*cursor));
        }
        else if (*cursor < 0x20)
        {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "\\u%04x", *cursor);
            out += hex;
        }
        else
        {
            out.push_back(static_cast<char>(*cursor));
        }
    }
}

bool ParseVer(const char* text, int* major, int* minor, int* patch)
{
    *major = 0;
    *minor = 0;
    *patch = 0;
    if (text == nullptr || text[0] == '\0')
    {
        return false;
    }

    return std::sscanf(text, "%d.%d.%d", major, minor, patch) >= 1;
}

void NormUuid(const char* text, char* out, size_t cap)
{
    size_t n = 0;
    for (const char* cursor = text != nullptr ? text : ""; *cursor != '\0' && n + 1 < cap; ++cursor)
    {
        if (*cursor == '-')
        {
            continue;
        }

        char c = *cursor;
        if (c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - 'a' + 'A');
        }

        out[n++] = c;
    }

    out[n] = '\0';
}

struct FnSpan
{
    bool start = false;
    uintptr_t next = 0;
    uintptr_t end = 0;
};

const uint8_t* MappedFile(const mach_header_64* header, intptr_t slide, uint32_t dataoff, uint32_t datasize)
{
    if (datasize == 0)
    {
        return nullptr;
    }

    const auto* cmd = reinterpret_cast<const load_command*>(header + 1);
    for (uint32_t i = 0; i < header->ncmds; ++i)
    {
        if (cmd->cmdsize < 8)
        {
            return nullptr;
        }

        if (cmd->cmd == LC_SEGMENT_64)
        {
            const auto* seg = reinterpret_cast<const segment_command_64*>(cmd);
            const uint64_t begin = seg->fileoff;
            const uint64_t end = begin + seg->filesize;
            const uint64_t off = dataoff;
            if (off >= begin && off + datasize <= end)
            {
                return reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(seg->vmaddr) +
                                                        static_cast<uintptr_t>(slide) +
                                                        static_cast<uintptr_t>(off - begin));
            }
        }

        cmd = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(cmd) + cmd->cmdsize);
    }

    return nullptr;
}

bool DescribeFunction(uintptr_t target, FnSpan* out)
{
    const uint32_t images = _dyld_image_count();
    for (uint32_t image = 0; image < images; ++image)
    {
        const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(image));
        if (header == nullptr || header->magic != MH_MAGIC_64)
        {
            continue;
        }

        const intptr_t slide = _dyld_get_image_vmaddr_slide(image);
        const auto* cmd = reinterpret_cast<const load_command*>(header + 1);
        bool contains = false;
        uintptr_t sectionEnd = 0;
        uint64_t textVm = 0;
        bool haveText = false;
        const linkedit_data_command* starts = nullptr;
        for (uint32_t c = 0; c < header->ncmds; ++c)
        {
            if (cmd->cmdsize < 8)
            {
                break;
            }

            if (cmd->cmd == LC_SEGMENT_64)
            {
                const auto* seg = reinterpret_cast<const segment_command_64*>(cmd);
                const uintptr_t segStart = static_cast<uintptr_t>(seg->vmaddr + static_cast<uint64_t>(slide));
                const uintptr_t segEnd = segStart + static_cast<uintptr_t>(seg->vmsize);
                if (std::strncmp(seg->segname, "__TEXT", sizeof(seg->segname)) == 0)
                {
                    textVm = seg->vmaddr;
                    haveText = true;
                }

                if (target >= segStart && target < segEnd)
                {
                    contains = true;
                    const auto* sect = reinterpret_cast<const section_64*>(seg + 1);
                    for (uint32_t s = 0; s < seg->nsects; ++s, ++sect)
                    {
                        const uintptr_t start = static_cast<uintptr_t>(sect->addr + static_cast<uint64_t>(slide));
                        const uintptr_t end = start + static_cast<uintptr_t>(sect->size);
                        if (target >= start && target < end)
                        {
                            sectionEnd = end;
                        }
                    }
                }
            }
            else if (cmd->cmd == LC_FUNCTION_STARTS)
            {
                starts = reinterpret_cast<const linkedit_data_command*>(cmd);
            }

            cmd = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(cmd) + cmd->cmdsize);
        }

        if (!contains)
        {
            continue;
        }

        out->end = sectionEnd;
        if (!haveText || starts == nullptr || sectionEnd == 0)
        {
            return sectionEnd != 0;
        }

        const uint8_t* cursor = MappedFile(header, slide, starts->dataoff, starts->datasize);
        if (cursor == nullptr)
        {
            return true;
        }

        const uint8_t* blobEnd = cursor + starts->datasize;
        uint64_t addr = textVm + static_cast<uint64_t>(slide);
        while (cursor < blobEnd)
        {
            uint64_t delta = 0;
            int shift = 0;
            bool ok = false;
            while (cursor < blobEnd && shift <= 63)
            {
                const uint8_t byte = *cursor++;
                delta |= static_cast<uint64_t>(byte & 0x7Fu) << shift;
                if ((byte & 0x80u) == 0)
                {
                    ok = true;
                    break;
                }

                shift += 7;
            }

            if (!ok || delta == 0)
            {
                break;
            }

            addr += delta;
            if (addr == target)
            {
                out->start = true;
            }
            else if (addr > target)
            {
                out->next = static_cast<uintptr_t>(addr);
                break;
            }
        }

        return true;
    }

    return false;
}

bool BranchesIntoPatch(uintptr_t target, uintptr_t end)
{
    if (end < target)
    {
        return false;
    }

    const auto* cursor = reinterpret_cast<const uint32_t*>(target);
    const auto* last = reinterpret_cast<const uint32_t*>(end);
    for (const uint32_t* instr = cursor; instr < last; ++instr)
    {
        const uint32_t word = *instr;
        const uint64_t pc = reinterpret_cast<uint64_t>(instr);
        uint64_t dest = 0;
        bool branch = false;
        if ((word & 0xFC000000u) == 0x14000000u)
        {
            dest = pc + static_cast<uint64_t>(SignExtend(word & 0x03FFFFFFu, 26) << 2);
            branch = true;
        }
        else if ((word & 0xFF000010u) == 0x54000000u)
        {
            dest = pc + static_cast<uint64_t>(SignExtend((word >> 5) & 0x7FFFFu, 19) << 2);
            branch = true;
        }
        else if ((word & 0x7E000000u) == 0x34000000u)
        {
            dest = pc + static_cast<uint64_t>(SignExtend((word >> 5) & 0x7FFFFu, 19) << 2);
            branch = true;
        }
        else if ((word & 0x7E000000u) == 0x36000000u)
        {
            dest = pc + static_cast<uint64_t>(SignExtend((word >> 5) & 0x3FFFu, 14) << 2);
            branch = true;
        }

        if (branch && (dest == target + 4 || dest == target + 8))
        {
            return true;
        }
    }

    return false;
}

int32_t FarRefusal(uintptr_t target)
{
    FnSpan span;
    if (!DescribeFunction(target, &span) || !span.start)
    {
        return kErrNotFunctionStart;
    }

    uintptr_t limit = span.end;
    if (span.next != 0 && span.next < limit)
    {
        limit = span.next;
    }

    if (limit < target + 12)
    {
        return kErrFunctionTooShort;
    }

    if (PageOf(target) != PageOf(target + 11))
    {
        return kErrSpansPage;
    }

    if (BranchesIntoPatch(target, limit))
    {
        return kErrInteriorBranch;
    }

    return 0;
}

int32_t BuildTrampoline(Site* site)
{
    const uint32_t countInsns = site->far ? 3u : 1u;
    uint32_t count = 0;
    uint64_t dst = reinterpret_cast<uint64_t>(site->page + 16);
    const auto* src = reinterpret_cast<const uint32_t*>(site->target);
    for (uint32_t i = 0; i < countInsns; ++i)
    {
        const uint64_t srcPc = site->target + static_cast<uint64_t>(i) * 4u;
        const RelocOut relocated = Relocate(src[i], srcPc, dst);
        if (relocated.kind == RelocOut::Kind::Refused || relocated.count == 0 || count + relocated.count + 4 > 40)
        {
            return kErrRelocate;
        }

        std::memcpy(site->tramp + count, relocated.words, relocated.count * sizeof(uint32_t));
        count += relocated.count;
        dst += static_cast<uint64_t>(relocated.count) * 4u;
    }

    const uint64_t resume = site->target + static_cast<uint64_t>(countInsns) * 4u;
    uint32_t branch = 0;
    if (TryEncodeB(dst, resume, &branch))
    {
        site->tramp[count++] = branch;
    }
    else if (site->far && count + 4 <= 40)
    {
        // x16 only. x0-x8 stay intact across the jump back into the function.
        site->tramp[count++] = 0x58000050u;
        site->tramp[count++] = 0xD61F0200u;
        site->tramp[count++] = static_cast<uint32_t>(resume);
        site->tramp[count++] = static_cast<uint32_t>(resume >> 32);
    }
    else
    {
        return kErrUnreachable;
    }

    site->trampCount = count;
    return 0;
}

} // namespace

const char* CoreHookName(uint32_t hash)
{
    switch (hash)
    {
    case Hashes::Main:
        return "Main";
    case Hashes::CGameApplication_AddState:
        return "CGameApplication_AddState";
    case Hashes::Global_ExecuteProcess:
        return "Global_ExecuteProcess";
    case Hashes::CBaseEngine_InitScripts:
        return "CBaseEngine_InitScripts";
    case Hashes::CBaseEngine_LoadScripts:
        return "CBaseEngine_LoadScripts";
    case Hashes::ScriptValidator_Validate:
        return "ScriptValidator_Validate";
    case Hashes::AssertionFailed:
        return "AssertionFailed";
    case Hashes::GameInstance_CollectSaveableSystems:
        return "GameInstance_CollectSaveableSystems";
    case Hashes::GsmState_SessionActive_ReportErrorCode:
        return "GsmState_SessionActive_ReportErrorCode";
    default:
        return nullptr;
    }
}

RelocOut Relocate(uint32_t instr, uint64_t srcPc, uint64_t dstPc)
{
    RelocOut out{};
    const bool relative = IsPcRelative(instr);
    const uint64_t dst = dstPc;

    if ((instr & 0x9F000000u) == 0x10000000u || (instr & 0x9F000000u) == 0x90000000u)
    {
        const bool page = (instr & 0x9F000000u) == 0x90000000u;
        const uint32_t rd = instr & 0x1Fu;
        const int64_t imm = SignExtend(((instr >> 5) & 0x7FFFFu) << 2 | ((instr >> 29) & 0x3u), 21);
        const uint64_t base = page ? (srcPc & ~0xFFFull) : srcPc;
        const uint64_t target = base + (page ? (imm << 12) : imm);
        EmitMovAbs(out, rd, target);
        out.kind = RelocOut::Kind::Relocated;
        return out;
    }

    if ((instr & 0xFC000000u) == 0x14000000u || (instr & 0xFC000000u) == 0x94000000u)
    {
        const bool link = (instr & 0xFC000000u) == 0x94000000u;
        const int64_t imm = SignExtend(instr & 0x03FFFFFFu, 26);
        const uint64_t target = srcPc + (imm << 2);
        uint32_t encoded = 0;
        if (!link && TryEncodeB(dst, target, &encoded))
        {
            Push(out, encoded);
        }
        else if (link)
        {
            uint32_t direct = 0;
            if (TryEncodeB(dst, target, &direct))
            {
                Push(out, 0x94000000u | (direct & 0x03FFFFFFu));
            }
            else
            {
                EmitAbsCall(out, target);
            }
        }
        else
        {
            EmitAbsJump(out, target);
        }

        out.kind = RelocOut::Kind::Relocated;
        return out;
    }

    if ((instr & 0xFF000010u) == 0x54000000u)
    {
        const uint32_t cond = instr & 0xFu;
        const int64_t imm = SignExtend((instr >> 5) & 0x7FFFFu, 19);
        const uint64_t target = srcPc + (imm << 2);
        uint32_t encoded = 0;
        if (TryEncodeBCond(dst, target, cond, &encoded))
        {
            Push(out, encoded);
        }
        else
        {
            const uint32_t index = out.count;
            Push(out, 0);
            EmitAbsJump(out, target);
            out.words[index] = 0x540000A0u | ((cond ^ 1u) & 0xFu);
        }

        out.kind = RelocOut::Kind::Relocated;
        return out;
    }

    if ((instr & 0x7E000000u) == 0x34000000u)
    {
        const uint32_t sf = instr >> 31;
        const uint32_t op = (instr >> 24) & 1u;
        const uint32_t rt = instr & 31u;
        const int64_t imm = SignExtend((instr >> 5) & 0x7FFFFu, 19);
        const uint64_t target = srcPc + (imm << 2);
        uint32_t encoded = 0;
        if (TryEncodeCbz(dst, target, sf, op, rt, &encoded))
        {
            Push(out, encoded);
        }
        else
        {
            const uint32_t index = out.count;
            Push(out, 0);
            EmitAbsJump(out, target);
            out.words[index] = (sf << 31) | 0x34000000u | ((op ^ 1u) << 24) | (5u << 5) | rt;
        }

        out.kind = RelocOut::Kind::Relocated;
        return out;
    }

    if ((instr & 0x7E000000u) == 0x36000000u)
    {
        const uint32_t op = (instr >> 24) & 1u;
        const uint32_t bit = ((instr >> 31) << 5) | ((instr >> 19) & 31u);
        const uint32_t rt = instr & 31u;
        const int64_t imm = SignExtend((instr >> 5) & 0x3FFFu, 14);
        const uint64_t target = srcPc + (imm << 2);
        uint32_t encoded = 0;
        if (TryEncodeTbz(dst, target, op, bit, rt, &encoded))
        {
            Push(out, encoded);
        }
        else
        {
            const uint32_t index = out.count;
            Push(out, 0);
            EmitAbsJump(out, target);
            const uint32_t b5 = (bit >> 5) & 1u;
            const uint32_t b40 = bit & 31u;
            out.words[index] = (b5 << 31) | 0x36000000u | ((op ^ 1u) << 24) | (b40 << 19) | (5u << 5) | rt;
        }

        out.kind = RelocOut::Kind::Relocated;
        return out;
    }

    if ((instr & 0x3B000000u) == 0x18000000u)
    {
        const uint32_t top = instr & 0xFF000000u;
        const uint32_t rt = instr & 31u;
        const int64_t imm = SignExtend((instr >> 5) & 0x7FFFFu, 19);
        const uint64_t literal = srcPc + static_cast<uint64_t>(imm << 2);
        const bool simd = top == 0x1C000000u || top == 0x5C000000u || top == 0x9C000000u;
        const bool prfm = top == 0xD8000000u;
        if (top != 0x18000000u && top != 0x58000000u && top != 0x98000000u && !simd && !prfm)
        {
            out.kind = RelocOut::Kind::Refused;
            out.count = 0;
            return out;
        }

        const uint32_t scratch = (!simd && !prfm && rt == 16u) ? 17u : 16u;
        EmitMovAbs(out, scratch, literal);
        uint32_t load = 0;
        if (top == 0x18000000u)
        {
            load = 0xB9400000u | (scratch << 5) | rt;
        }
        else if (top == 0x58000000u)
        {
            load = 0xF9400000u | (scratch << 5) | rt;
        }
        else if (top == 0x98000000u)
        {
            load = 0xB9800000u | (scratch << 5) | rt;
        }
        else if (top == 0x1C000000u)
        {
            load = 0xBD400000u | (scratch << 5) | rt;
        }
        else if (top == 0x5C000000u)
        {
            load = 0xFD400000u | (scratch << 5) | rt;
        }
        else if (top == 0x9C000000u)
        {
            load = 0x3DC00000u | (scratch << 5) | rt;
        }
        else
        {
            load = 0xF9800000u | (scratch << 5) | rt;
        }

        Push(out, load);
        out.kind = RelocOut::Kind::Relocated;
        return out;
    }

    if (relative)
    {
        out.kind = RelocOut::Kind::Refused;
        out.count = 0;
        return out;
    }

    Push(out, instr);
    out.kind = RelocOut::Kind::Copied;
    return out;
}

void Begin()
{
    std::lock_guard lock(g_mu);
    g_open = true;
}

#ifdef RED4EXT_NATIVE_HOOK_TEST
void ForceFar(bool on)
{
    g_forceFar.store(on, std::memory_order_relaxed);
}
#endif

void SetIdentity(void* target, const char* name, const char* owner)
{
    std::lock_guard lock(g_mu);
    if (target == nullptr)
    {
        return;
    }

    const uintptr_t key = reinterpret_cast<uintptr_t>(target);
    for (Ident* ident = g_idents; ident != nullptr; ident = ident->next)
    {
        if (ident->target == key)
        {
            CopyStr(ident->name, sizeof(ident->name), name);
            CopyStr(ident->owner, sizeof(ident->owner), owner);
            return;
        }
    }

    auto* ident = new Ident;
    ident->target = key;
    CopyStr(ident->name, sizeof(ident->name), name);
    CopyStr(ident->owner, sizeof(ident->owner), owner);
    ident->next = g_idents;
    g_idents = ident;
}

int32_t Attach(void** ppPointer, void* detour)
{
    std::lock_guard lock(g_mu);
    if (!g_open || ppPointer == nullptr || *ppPointer == nullptr || detour == nullptr || !g_writesAllowed.load())
    {
        return -1;
    }

    const uintptr_t target = reinterpret_cast<uintptr_t>(*ppPointer);
    Site* site = FindSite(target);
    const bool fresh = site == nullptr;
    if (fresh)
    {
        uint8_t* page = nullptr;
        bool far = false;
        if (!WantFar() && AllocateNear(target, &page))
        {
            far = false;
        }
        else
        {
            const int32_t why = FarRefusal(target);
            if (why != 0)
            {
                return FailAttach(target, why, "far");
            }

            if (!AllocateFar(target, &page))
            {
                return FailAttach(target, kErrUnreachable, "far");
            }

            far = true;
        }

        site = new Site;
        site->target = target;
        site->far = far;
        site->patchBytes = far ? 12u : 4u;
        std::memcpy(site->original, reinterpret_cast<void*>(target), site->patchBytes);
        site->page = page;
        site->next = g_sites;
        g_sites = site;

        const int32_t built = BuildTrampoline(site);
        if (built != 0)
        {
            return FailSite(site, built);
        }
    }

    for (Node* node = site->head; node != nullptr; node = node->older)
    {
        if (node->detour == detour && !node->detach)
        {
            return -1;
        }
    }

    if (site->nextStub + 64 > PageSize())
    {
        if (fresh)
        {
            DestroySite(site);
        }

        return -1;
    }

    auto* node = new Node;
    node->detour = detour;
    node->pp = ppPointer;
    node->stub = site->page + site->nextStub;
    site->nextStub += 64;
    EncodeHitStub(node->stubBytes, reinterpret_cast<uint64_t>(&node->hits), reinterpret_cast<uint64_t>(detour));
    ConsumeIdent(target, node);
    node->older = site->head;
    site->head = node;

    void* previous = reinterpret_cast<void*>(site->page + 16);
    for (Node* older = node->older; older != nullptr; older = older->older)
    {
        if (!older->detach)
        {
            previous = older->detour;
            break;
        }
    }

    *ppPointer = previous;
    return 0;
}

int32_t Detach(void** ppPointer, void* detour)
{
    std::lock_guard lock(g_mu);
    if (!g_open || ppPointer == nullptr || detour == nullptr)
    {
        return -1;
    }

    for (Site* site = g_sites; site != nullptr; site = site->next)
    {
        for (Node* node = site->head; node != nullptr; node = node->older)
        {
            if (node->detour != detour || node->pp != ppPointer)
            {
                continue;
            }

            if (!node->committed)
            {
                Unlink(site, node);
                delete node;
                if (site->head == nullptr && !site->installed)
                {
                    DestroySite(site);
                }

                return 0;
            }

            node->detach = true;
            return 0;
        }
    }

    return -1;
}

int32_t Commit()
{
    std::lock_guard lock(g_mu);
    if (!g_open)
    {
        return -1;
    }

    g_protCount = 0;
    if (!g_writesAllowed.load())
    {
        RollbackUnlocked();
        return -1;
    }

    if (!NeedsWork())
    {
        g_open = false;
        return 0;
    }

    Site* sites[kMaxSites]{};
    uint32_t siteCount = 0;
    for (Site* site = g_sites; site != nullptr && siteCount < kMaxSites; site = site->next)
    {
        bool work = false;
        for (Node* node = site->head; node != nullptr; node = node->older)
        {
            if (!node->committed || node->detach)
            {
                work = true;
                break;
            }
        }

        if (work)
        {
            sites[siteCount++] = site;
        }
    }

    if (!StopAndPatch(sites, siteCount))
    {
        for (uint32_t i = 0; i < siteCount; ++i)
        {
            if (sites[i]->installKr == 0)
            {
                sites[i]->installKr = -1;
            }
        }

        RollbackUnlocked();
        return -1;
    }

    Node* trash[kMaxSites * 4]{};
    Site* dead[kMaxSites]{};
    uint32_t trashCount = 0;
    uint32_t deadCount = 0;
    CollectTrash(trash, &trashCount, dead, &deadCount);
    for (uint32_t i = 0; i < trashCount; ++i)
    {
        delete trash[i];
    }

    for (uint32_t i = 0; i < deadCount; ++i)
    {
        delete dead[i];
    }

    g_open = false;
    return 0;
}

int32_t Abort()
{
    std::lock_guard lock(g_mu);
    if (!g_open)
    {
        return 0;
    }

    RollbackUnlocked();
    return 0;
}

bool AllowWrites(const char* dbVersion, const char* dbUuid, const char* imageVersion, const char* imageUuid,
                 bool strict, char* logLine, size_t logCap)
{
    if (!strict)
    {
        g_writesAllowed.store(true);
        if (logLine != nullptr && logCap > 0)
        {
            logLine[0] = '\0';
        }

        return true;
    }

    int dbMajor = 0;
    int dbMinor = 0;
    int dbPatch = 0;
    int imageMajor = 0;
    int imageMinor = 0;
    int imagePatch = 0;
    const bool dbOk = ParseVer(dbVersion, &dbMajor, &dbMinor, &dbPatch);
    const bool imageOk = ParseVer(imageVersion, &imageMajor, &imageMinor, &imagePatch);
    char left[80]{};
    char right[80]{};
    NormUuid(dbUuid, left, sizeof(left));
    NormUuid(imageUuid, right, sizeof(right));
    const bool uuidOk = left[0] != '\0' && right[0] != '\0' && std::strcmp(left, right) == 0;
    const bool versionOk = dbOk && imageOk && dbMajor == imageMajor && dbMinor == imageMinor && dbPatch == imagePatch;
    if (versionOk && uuidOk)
    {
        g_writesAllowed.store(true);
        if (logLine != nullptr && logCap > 0)
        {
            logLine[0] = '\0';
        }

        return true;
    }

    g_writesAllowed.store(false);
    if (logLine != nullptr && logCap > 0)
    {
        std::snprintf(logLine, logCap, kRefuseCodeWritesFormat, dbVersion != nullptr ? dbVersion : "",
                      imageVersion != nullptr ? imageVersion : "", dbUuid != nullptr ? dbUuid : "",
                      imageUuid != nullptr ? imageUuid : "");
    }

    return false;
}

bool WritesAllowed()
{
    return g_writesAllowed.load();
}

uint32_t CopyProtectLog(ProtectOp* out, uint32_t cap)
{
    std::lock_guard lock(g_mu);
    const uint32_t n = g_protCount < cap ? g_protCount : cap;
    if (out != nullptr)
    {
        std::memcpy(out, g_protLog, n * sizeof(ProtectOp));
    }

    return g_protCount;
}

uint32_t CopyHookStats(HookInfo* out, uint32_t cap)
{
    std::lock_guard lock(g_mu);
    uint32_t count = 0;
    const uint64_t base = MainImageBase();
    for (Site* site = g_sites; site != nullptr; site = site->next)
    {
        for (Node* node = site->head; node != nullptr; node = node->older)
        {
            if (count >= cap)
            {
                return count;
            }

            if (out != nullptr)
            {
                HookInfo& info = out[count];
                CopyStr(info.name, sizeof(info.name), node->name);
                CopyStr(info.owner, sizeof(info.owner), node->owner);
                info.target = site->target;
                info.imageOffset = site->target >= base ? site->target - base : 0;
                info.installKr = site->installKr;
                info.installed = site->installed && node->committed && !node->detach;
                info.hits = node->hits.load(std::memory_order_relaxed);
                CopyStr(info.patch, sizeof(info.patch), site->far ? "far" : "near");
            }

            ++count;
        }
    }

    for (const Failed& failed : g_failed)
    {
        if (!failed.used || count >= cap)
        {
            continue;
        }

        if (out != nullptr)
        {
            HookInfo& info = out[count];
            CopyStr(info.name, sizeof(info.name), failed.name);
            CopyStr(info.owner, sizeof(info.owner), failed.owner);
            info.target = failed.target;
            info.imageOffset = failed.target >= base ? failed.target - base : 0;
            info.installKr = failed.installKr;
            info.installed = false;
            info.hits = 0;
            CopyStr(info.patch, sizeof(info.patch), failed.patch);
        }

        ++count;
    }

    return count;
}

void MainImageUuid(char* out, size_t cap)
{
    if (out == nullptr || cap == 0)
    {
        return;
    }

    out[0] = '\0';
    const mach_header_64* header = MainHeader();
    if (header == nullptr)
    {
        return;
    }

    const auto* command = reinterpret_cast<const load_command*>(header + 1);
    for (uint32_t i = 0; i < header->ncmds; ++i)
    {
        if (command->cmd == LC_UUID)
        {
            const auto* uuid = reinterpret_cast<const uuid_command*>(command);
            std::snprintf(out, cap, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                          uuid->uuid[0], uuid->uuid[1], uuid->uuid[2], uuid->uuid[3], uuid->uuid[4], uuid->uuid[5],
                          uuid->uuid[6], uuid->uuid[7], uuid->uuid[8], uuid->uuid[9], uuid->uuid[10], uuid->uuid[11],
                          uuid->uuid[12], uuid->uuid[13], uuid->uuid[14], uuid->uuid[15]);
            return;
        }

        command = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(command) + command->cmdsize);
    }
}

uint64_t MainImageBase()
{
    const mach_header_64* header = MainHeader();
    return header != nullptr ? reinterpret_cast<uint64_t>(header) : 0;
}

bool WriteHookStats(const char* path, int pid, const char* uuid)
{
    if (path == nullptr || path[0] == '\0')
    {
        return false;
    }

    HookInfo infos[128]{};
    const uint32_t count = CopyHookStats(infos, 128);
    char imageUuid[80]{};
    if (uuid == nullptr || uuid[0] == '\0')
    {
        MainImageUuid(imageUuid, sizeof(imageUuid));
        uuid = imageUuid;
    }

    char stamp[40]{};
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm);

    std::string json;
    json.reserve(1024 + count * 192);
    json += "{\"schema\":1,\"pid\":";
    json += std::to_string(pid >= 0 ? pid : getpid());
    json += ",\"uuid\":\"";
    JsonEscape(json, uuid);
    json += "\",\"written_at\":\"";
    json += stamp;
    json += "\",\"hooks\":[";
    for (uint32_t i = 0; i < count; ++i)
    {
        if (i != 0)
        {
            json += ',';
        }

        char target[32];
        char offset[32];
        std::snprintf(target, sizeof(target), "0x%llx", static_cast<unsigned long long>(infos[i].target));
        std::snprintf(offset, sizeof(offset), "0x%llx", static_cast<unsigned long long>(infos[i].imageOffset));
        json += "{\"name\":\"";
        JsonEscape(json, infos[i].name);
        json += "\",\"owner\":\"";
        JsonEscape(json, infos[i].owner);
        json += "\",\"target\":\"";
        json += target;
        json += "\",\"image_offset\":\"";
        json += offset;
        json += "\",\"install_kr\":";
        json += std::to_string(infos[i].installKr);
        json += ",\"installed\":";
        json += infos[i].installed ? "true" : "false";
        json += ",\"patch\":\"";
        json += infos[i].patch;
        json += "\",\"hits\":";
        json += std::to_string(infos[i].hits);
        json += '}';
    }

    json += "]}";

    std::error_code error;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, error);
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    file << json;
    return static_cast<bool>(file);
}

void StatsThreadMain()
{
    WriteHookStats(g_statsPath.c_str(), getpid(), g_statsUuid.c_str());
    while (!g_statsStop.load(std::memory_order_relaxed))
    {
        for (int i = 0; i < 100 && !g_statsStop.load(std::memory_order_relaxed); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!g_statsStop.load(std::memory_order_relaxed))
        {
            WriteHookStats(g_statsPath.c_str(), getpid(), g_statsUuid.c_str());
        }
    }
}

void StartStatsThread(const char* path, const char* uuid)
{
    if (path == nullptr || g_statsRunning.exchange(true))
    {
        return;
    }

    g_statsPath = path;
    g_statsUuid = uuid != nullptr ? uuid : "";
    g_statsStop.store(false);
    g_statsThread = std::thread(StatsThreadMain);
}

void StopStatsThread()
{
    if (!g_statsRunning.exchange(false))
    {
        return;
    }

    g_statsStop.store(true);
    if (g_statsThread.joinable())
    {
        g_statsThread.join();
    }

    WriteHookStats(g_statsPath.c_str(), getpid(), g_statsUuid.c_str());
}

} // namespace NativeHook

#endif
