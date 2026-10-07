#include "Platform/MainLookup.hpp"
#include "Platform/NativeHook.hpp"

#include <dlfcn.h>

#include <libkern/OSCacheControl.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" __attribute__((visibility("default"))) int Main(void)
{
    return 1;
}

static int g_failed = 0;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                 \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

static void OnAlarm(int)
{
    std::fprintf(stderr, "FAIL watchdog: hook test deadlocked\n");
    _exit(98);
}

static void OnFault(int sig)
{
    std::fprintf(stderr, "FAIL signal %d during hook test\n", sig);
    _exit(99);
}

struct Tx
{
    Tx()
    {
        NativeHook::Begin();
    }

    bool Commit()
    {
        return NativeHook::Commit() == 0;
    }

    ~Tx()
    {
        NativeHook::Abort();
    }
};

__attribute__((noinline, optnone, aligned(16384), section("__TEXT,__nhook"))) static int TextTarget(int x)
{
    return x + 1;
}

static int TextDetour(int)
{
    return 42;
}

__attribute__((noinline, optnone, aligned(16384), section("__TEXT,__nhook2"))) static int ChainTarget(int x)
{
    return x + 1;
}

static void* g_chainSlotA = nullptr;
static void* g_chainSlotB = nullptr;

static int ChainA(int x)
{
    return reinterpret_cast<int (*)(int)>(g_chainSlotA)(x) + 10;
}

static int ChainB(int x)
{
    return reinterpret_cast<int (*)(int)>(g_chainSlotB)(x) + 100;
}

static void TestRelocator()
{
    const uint64_t src = 0x10000;
    const uint64_t dst = 0x20000;
    const uint32_t opcodes[] = {
        0x10000000u, 0x90000000u, 0x14000003u, 0x94000003u, 0x54000000u, 0x34000060u, 0x35000060u,
        0x36000000u, 0x37000000u, 0x18000040u, 0x58000040u, 0x98000040u, 0x1C000040u, 0x5C000040u,
        0x9C000040u, 0xD8000040u,
    };

    for (uint32_t instr : opcodes)
    {
        const auto out = NativeHook::Relocate(instr, src, dst);
        CHECK(out.kind == NativeHook::RelocOut::Kind::Relocated);
        CHECK(out.count > 0);
        CHECK(!(out.count == 1 && out.words[0] == instr));
    }

    const auto copied = NativeHook::Relocate(0x91000400u, src, dst);
    CHECK(copied.kind == NativeHook::RelocOut::Kind::Copied);
    CHECK(copied.count == 1 && copied.words[0] == 0x91000400u);

    const auto refused = NativeHook::Relocate(0xDC0003E0u, src, dst);
    CHECK(refused.kind == NativeHook::RelocOut::Kind::Refused);
    CHECK(refused.count == 0);

    std::printf("PASS relocator\n");
}

static bool Hook(void** slot, void* detour)
{
    Tx tx;
    if (NativeHook::Attach(slot, detour) != 0)
    {
        return false;
    }

    return tx.Commit();
}

static void TestProtectionAndIsland()
{
    CHECK(TextTarget(1) == 2);
    void* original = reinterpret_cast<void*>(TextTarget);
    const uint32_t before = *reinterpret_cast<uint32_t*>(TextTarget);
    CHECK(Hook(&original, reinterpret_cast<void*>(TextDetour)));
    CHECK(TextTarget(1) == 42);

    const uint32_t patched = *reinterpret_cast<uint32_t*>(TextTarget);
    const int32_t imm = static_cast<int32_t>(patched & 0x03FFFFFFu);
    const int32_t simm = (imm & 0x02000000) != 0 ? imm | ~0x03FFFFFF : imm;
    const auto island = reinterpret_cast<uintptr_t>(TextTarget) + static_cast<intptr_t>(simm) * 4;
    const auto distance = island > reinterpret_cast<uintptr_t>(TextTarget)
                              ? island - reinterpret_cast<uintptr_t>(TextTarget)
                              : reinterpret_cast<uintptr_t>(TextTarget) - island;
    CHECK((patched & 0xFC000000u) == 0x14000000u);
    CHECK(distance < (128u * 1024u * 1024u));

    NativeHook::ProtectOp ops[32];
    const uint32_t logged = NativeHook::CopyProtectLog(ops, 32);
    bool sawRestore = false;
    bool restoreCopied = false;
    for (uint32_t i = 0; i < logged && i < 32; ++i)
    {
        if (!ops[i].restore)
        {
            continue;
        }

        sawRestore = true;
        restoreCopied = restoreCopied || ops[i].copy;
        CHECK(ops[i].prot == (VM_PROT_READ | VM_PROT_EXECUTE));
    }

    CHECK(sawRestore);
    CHECK(!restoreCopied);

    Tx tx;
    CHECK(NativeHook::Detach(&original, reinterpret_cast<void*>(TextDetour)) == 0);
    CHECK(tx.Commit());
    CHECK(TextTarget(1) == 2);
    CHECK(*reinterpret_cast<uint32_t*>(TextTarget) == before);
    std::printf("PASS protection and island\n");
}

static void TestChain()
{
    g_chainSlotA = reinterpret_cast<void*>(ChainTarget);
    g_chainSlotB = reinterpret_cast<void*>(ChainTarget);
    CHECK(Hook(&g_chainSlotA, reinterpret_cast<void*>(ChainA)));
    CHECK(Hook(&g_chainSlotB, reinterpret_cast<void*>(ChainB)));
    CHECK(ChainTarget(1) == 112);

    {
        Tx tx;
        CHECK(NativeHook::Detach(&g_chainSlotB, reinterpret_cast<void*>(ChainB)) == 0);
        CHECK(tx.Commit());
    }
    CHECK(ChainTarget(1) == 12);

    {
        Tx tx;
        CHECK(NativeHook::Detach(&g_chainSlotA, reinterpret_cast<void*>(ChainA)) == 0);
        CHECK(tx.Commit());
    }
    CHECK(ChainTarget(1) == 2);

    g_chainSlotA = reinterpret_cast<void*>(ChainTarget);
    g_chainSlotB = reinterpret_cast<void*>(ChainTarget);
    CHECK(Hook(&g_chainSlotA, reinterpret_cast<void*>(ChainA)));
    CHECK(Hook(&g_chainSlotB, reinterpret_cast<void*>(ChainB)));
    {
        Tx tx;
        CHECK(NativeHook::Detach(&g_chainSlotA, reinterpret_cast<void*>(ChainA)) == 0);
        CHECK(tx.Commit());
    }
    CHECK(ChainTarget(1) == 102);
    {
        Tx tx;
        CHECK(NativeHook::Detach(&g_chainSlotB, reinterpret_cast<void*>(ChainB)) == 0);
        CHECK(tx.Commit());
    }
    CHECK(ChainTarget(1) == 2);

    g_chainSlotA = reinterpret_cast<void*>(ChainTarget);
    CHECK(Hook(&g_chainSlotA, reinterpret_cast<void*>(ChainA)));
    CHECK(ChainTarget(1) == 12);
    {
        Tx tx;
        CHECK(NativeHook::Detach(&g_chainSlotA, reinterpret_cast<void*>(ChainA)) == 0);
        CHECK(tx.Commit());
    }
    CHECK(ChainTarget(1) == 2);
    std::printf("PASS chain detach reattach\n");
}

static void TestOutOfRange()
{
    constexpr mach_vm_size_t span = 256ull << 20;
    mach_vm_address_t block = 0;
    CHECK(mach_vm_allocate(mach_task_self(), &block, span, VM_FLAGS_ANYWHERE) == KERN_SUCCESS);
    auto* target = reinterpret_cast<uint32_t*>(block + (128ull << 20));
    target[0] = 0x91000400u;
    target[1] = 0xD65F03C0u;
    const uintptr_t page = reinterpret_cast<uintptr_t>(target) & ~(static_cast<uintptr_t>(vm_page_size) - 1);
    CHECK(mprotect(reinterpret_cast<void*>(page), static_cast<size_t>(vm_page_size), PROT_READ | PROT_EXEC) == 0);
    const uint32_t before = target[0];
    void* slot = target;
    const bool hooked = Hook(&slot, reinterpret_cast<void*>(TextDetour));
    CHECK(!hooked);
    CHECK(target[0] == before);
    mach_vm_deallocate(mach_task_self(), block, span);
    std::printf("PASS out of range\n");
}

struct ParkArgs
{
    std::atomic<int>* ready;
    std::atomic<int>* stop;
    uint8_t* fn;
};

static void* ParkThread(void* raw)
{
    auto* args = static_cast<ParkArgs*>(raw);
    using Fn = void (*)(std::atomic<int>*, std::atomic<int>*);
    reinterpret_cast<Fn>(args->fn)(args->ready, args->stop);
    return nullptr;
}

static uint8_t* AllocPage()
{
    mach_vm_address_t addr = 0;
    if (mach_vm_allocate(mach_task_self(), &addr, static_cast<mach_vm_size_t>(vm_page_size), VM_FLAGS_ANYWHERE) !=
        KERN_SUCCESS)
    {
        return nullptr;
    }

    return reinterpret_cast<uint8_t*>(addr);
}

static void Seal(uint8_t* page)
{
    sys_icache_invalidate(page, static_cast<size_t>(vm_page_size));
    mprotect(page, static_cast<size_t>(vm_page_size), PROT_READ | PROT_EXEC);
}

static int ParkForward(int x)
{
    return x;
}

static void TestParked(bool hookSpinner)
{
    uint8_t* page = AllocPage();
    CHECK(page != nullptr);
    auto* words = reinterpret_cast<uint32_t*>(page);
    words[0] = 0x91000400u;
    words[1] = 0xD65F03C0u;
    // spinner(ready, stop): STR WZR, [X0] is wrong. ready is X0, stop is X1.
    // MOV W2, #1; STR W2, [X0]; loop: LDR W2, [X1]; CBZ W2, loop; RET
    uint32_t* spin = words + 8;
    spin[0] = 0x52800022u;
    spin[1] = 0xB9000002u;
    spin[2] = 0xB9400022u;
    spin[3] = 0x34FFFFE2u;
    spin[4] = 0xD65F03C0u;
    Seal(page);

    std::atomic<int> ready{0};
    std::atomic<int> stop{0};
    ParkArgs args{&ready, &stop, reinterpret_cast<uint8_t*>(spin)};
    pthread_t thread{};
    CHECK(pthread_create(&thread, nullptr, ParkThread, &args) == 0);
    while (ready.load() == 0)
    {
        usleep(1000);
    }

    void* slot = hookSpinner ? reinterpret_cast<void*>(spin) : page;
    const uint32_t before = *reinterpret_cast<uint32_t*>(slot);
    Tx tx;
    const int attached = NativeHook::Attach(&slot, reinterpret_cast<void*>(ParkForward));
    const bool committed = attached == 0 && tx.Commit();
    if (!committed)
    {
        CHECK(*reinterpret_cast<uint32_t*>(hookSpinner ? reinterpret_cast<void*>(spin) : page) == before);
    }

    stop.store(1);
    CHECK(pthread_join(thread, nullptr) == 0);
    if (!hookSpinner && !committed)
    {
        CHECK(reinterpret_cast<int (*)(int)>(page)(1) == 2);
    }

    mach_vm_deallocate(mach_task_self(), reinterpret_cast<mach_vm_address_t>(page),
                       static_cast<mach_vm_size_t>(vm_page_size));
    std::printf("PASS park %s\n", hookSpinner ? "inside" : "same-page");
}

static void TestHookStats()
{
    CHECK(TextTarget(1) == 2);
    NativeHook::SetIdentity(reinterpret_cast<void*>(TextTarget), "CGameApplication_AddState", "RED4ext");
    void* slot = reinterpret_cast<void*>(TextTarget);
    CHECK(Hook(&slot, reinterpret_cast<void*>(TextDetour)));
    CHECK(TextTarget(1) == 42);
    CHECK(TextTarget(1) == 42);

    const auto path = std::filesystem::temp_directory_path() / "red4ext-hookstats.json";
    char uuid[80];
    NativeHook::MainImageUuid(uuid, sizeof(uuid));
    CHECK(NativeHook::WriteHookStats(path.string().c_str(), getpid(), uuid));
    std::ifstream file(path);
    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("\"schema\":1") != std::string::npos);
    CHECK(json.find("\"pid\":") != std::string::npos);
    CHECK(json.find(std::string("\"uuid\":\"") + uuid + "\"") != std::string::npos);
    CHECK(json.find("\"written_at\":\"") != std::string::npos);
    CHECK(json.find("\"name\":\"CGameApplication_AddState\"") != std::string::npos);
    CHECK(json.find("\"owner\":\"RED4ext\"") != std::string::npos);
    CHECK(json.find("\"install_kr\":0") != std::string::npos);
    CHECK(json.find("\"installed\":true") != std::string::npos);
    CHECK(json.find("\"patch\":\"near\"") != std::string::npos);
    CHECK(json.find("\"hits\":2") != std::string::npos);
    CHECK(json.find("\"target\":\"0x") != std::string::npos);
    CHECK(json.find("\"image_offset\":\"0x") != std::string::npos);

    Tx tx;
    CHECK(NativeHook::Detach(&slot, reinterpret_cast<void*>(TextDetour)) == 0);
    CHECK(tx.Commit());
    std::printf("PASS hookstats\n");
}

static void TestRefuseWrites()
{
    char line[640];
    const bool allowed = NativeHook::AllowWrites("1.0.0", "", "9.9.9", "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE", true,
                                                  line, sizeof(line));
    CHECK(!allowed);
    const char* expected = "Refusing native code writes: address DB mismatch (db_version='1.0.0' image_version='9.9.9' "
                           "db_uuid='' image_uuid='AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE')";
    CHECK(std::strcmp(line, expected) == 0);

    uint8_t* page = AllocPage();
    CHECK(page != nullptr);
    auto* words = reinterpret_cast<uint32_t*>(page);
    words[0] = 0x91000400u;
    words[1] = 0xD65F03C0u;
    Seal(page);
    const uint32_t before = words[0];
    void* slot = page;
    CHECK(!Hook(&slot, reinterpret_cast<void*>(TextDetour)));
    CHECK(words[0] == before);

    char okLine[64];
    CHECK(NativeHook::AllowWrites("2.3.1", "abcdef", "2.3.1", "ABCDEF", true, okLine, sizeof(okLine)));
    mach_vm_deallocate(mach_task_self(), reinterpret_cast<mach_vm_address_t>(page),
                       static_cast<mach_vm_size_t>(vm_page_size));
    std::printf("PASS version gate\n");
}

static void TestMainOnly()
{
    char exe[4096];
    uint32_t size = sizeof(exe);
    CHECK(_NSGetExecutablePath(exe, &size) == 0);
    const auto stub = std::filesystem::path(exe).parent_path() / "libnative_hook_plugin_stub.dylib";
    void* plugin = dlopen(stub.string().c_str(), RTLD_NOW | RTLD_GLOBAL);
    CHECK(plugin != nullptr);
    void* pluginMain = dlsym(plugin, "Main");
    CHECK(pluginMain != nullptr);
    CHECK(pluginMain != reinterpret_cast<void*>(Main));
    void* mainHandle = dlopen(nullptr, RTLD_LAZY);
    CHECK(Red4extLookupSymbol(mainHandle, "Main") == reinterpret_cast<void*>(Main));
    CHECK(Red4extLookupSymbol(RTLD_DEFAULT, "Main") == reinterpret_cast<void*>(Main));
    CHECK(Red4extLookupSymbol(RTLD_MAIN_ONLY, "Main") == reinterpret_cast<void*>(Main));
    std::printf("PASS main-only lookup\n");
}

struct NhBig
{
    uint64_t v[8];
};

extern "C" uint64_t NhAdd(uint64_t);
extern "C" uint64_t NhAddNext();
extern "C" uint64_t NhHi(uint64_t);
extern "C" uint64_t NhCbz(uint64_t);
extern "C" uint64_t NhTbz(uint64_t);
extern "C" uint64_t NhAdr();
extern "C" uint64_t NhAdrNext();
extern "C" uint64_t NhAdrp();
extern "C" uint64_t NhAdrpNext();
extern "C" uint64_t NhLdr();
extern "C" uint64_t NhLdrNext();
extern "C" uint64_t NhShort();
extern "C" uint64_t NhShortNext();
extern "C" uint64_t NhBack(uint64_t);
extern "C" uint64_t NhBackNext();
extern "C" uint64_t NhBad();
extern "C" uint64_t NhBadNext();
extern "C" NhBig NhStruct(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern "C" uint64_t NhStructNext();

__asm__(R"(
.text
.p2align 2
.globl _NhAdd
_NhAdd:
    add w0, w0, #1
    nop
    nop
    ret
.globl _NhAddNext
_NhAddNext:
    ret
.globl _NhHi
_NhHi:
    cmp w0, #0x6ac
    b.hi Lhi
    mov w0, #1
    ret
Lhi:
    mov w0, #2
    ret
.globl _NhCbz
_NhCbz:
    cbz w0, Lcbz
    mov w0, #1
    nop
    ret
Lcbz:
    mov w0, #2
    ret
.globl _NhTbz
_NhTbz:
    tbz w0, #0, Ltbz
    mov w0, #1
    nop
    ret
Ltbz:
    mov w0, #2
    ret
.globl _NhAdr
_NhAdr:
    adr x0, Ladr
    nop
    nop
    ret
    .p2align 3
Ladr:
    .quad 0
.globl _NhAdrNext
_NhAdrNext:
    ret
.globl _NhAdrp
_NhAdrp:
    adrp x0, Ladrp@PAGE
    add x0, x0, Ladrp@PAGEOFF
    nop
    ret
    .p2align 3
Ladrp:
    .quad 1
.globl _NhAdrpNext
_NhAdrpNext:
    ret
.globl _NhLdr
_NhLdr:
    ldr x0, Lldr
    nop
    nop
    ret
    .p2align 3
Lldr:
    .quad 0x1122334455667788
.globl _NhLdrNext
_NhLdrNext:
    ret
.globl _NhShort
_NhShort:
    mov w0, #5
    ret
.globl _NhShortNext
_NhShortNext:
    ret
.globl _NhBack
_NhBack:
    mov w0, #0
Lback:
    add w0, w0, #1
    nop
    cmp w0, #3
    b.lt Lback
    ret
.globl _NhBackNext
_NhBackNext:
    ret
.globl _NhBad
_NhBad:
    .inst 0xdc0003e0
    nop
    nop
    ret
.globl _NhBadNext
_NhBadNext:
    ret
.globl _NhStruct
_NhStruct:
    stp x0, x1, [x8]
    stp x2, x3, [x8, #16]
    stp x4, x5, [x8, #32]
    stp x6, x7, [x8, #48]
    mov x0, x8
    ret
.globl _NhStructNext
_NhStructNext:
    ret
)");

struct FarScope
{
    FarScope()
    {
        NativeHook::ForceFar(true);
    }

    ~FarScope()
    {
        NativeHook::ForceFar(false);
    }
};

static int FarAttach(void** slot, void* detour)
{
    FarScope scope;
    Tx tx;
    const int kr = NativeHook::Attach(slot, detour);
    if (kr != 0)
    {
        return kr;
    }

    return tx.Commit() ? 0 : -1;
}

static bool FarPatch(const void* fn)
{
    const auto* word = reinterpret_cast<const uint32_t*>(fn);
    if ((word[0] & 0x9F000000u) != 0x90000000u || (word[1] & 0xFFC00000u) != 0x91000000u || word[2] != 0xD61F0220u)
    {
        return false;
    }

    uint32_t imm = ((word[0] >> 5) & 0x7FFFFu) << 2 | ((word[0] >> 29) & 3u);
    int64_t simm = imm;
    if ((simm & (1 << 20)) != 0)
    {
        simm |= ~((static_cast<int64_t>(1) << 21) - 1);
    }

    const uint64_t page = (reinterpret_cast<uint64_t>(fn) & ~0xFFFull) + (static_cast<uint64_t>(simm) << 12);
    const uint32_t off = (word[1] >> 10) & 0xFFFu;
    const auto* island = reinterpret_cast<const uint32_t*>(page + off);
    return island[0] == 0x58000050u && island[1] == 0xD61F0200u;
}

static uint64_t (*g_addA)(uint64_t) = nullptr;
static uint64_t (*g_addB)(uint64_t) = nullptr;

static uint64_t AddA(uint64_t x)
{
    return g_addA(x) + 10;
}

static uint64_t AddB(uint64_t x)
{
    return g_addB(x) + 100;
}

static uint64_t (*g_u64)(uint64_t) = nullptr;
static uint64_t PassU64(uint64_t x)
{
    return g_u64(x);
}

static uint64_t (*g_u64v)() = nullptr;
static uint64_t PassU64v()
{
    return g_u64v();
}

static void KeepFarSymbols()
{
    void* keep[] = {reinterpret_cast<void*>(NhAddNext),  reinterpret_cast<void*>(NhAdrNext),
                    reinterpret_cast<void*>(NhAdrpNext), reinterpret_cast<void*>(NhLdrNext),
                    reinterpret_cast<void*>(NhShortNext), reinterpret_cast<void*>(NhBackNext),
                    reinterpret_cast<void*>(NhBadNext),  reinterpret_cast<void*>(NhStructNext)};
    std::atomic<void*> sink{keep[0]};
    (void)sink;
    (void)keep;
}

static void TestFarPatch()
{
    KeepFarSymbols();
    const uint32_t before[3] = {reinterpret_cast<uint32_t*>(NhAdd)[0], reinterpret_cast<uint32_t*>(NhAdd)[1],
                                reinterpret_cast<uint32_t*>(NhAdd)[2]};
    CHECK(NhAdd(1) == 2);
    void* slot = reinterpret_cast<void*>(NhAdd);
    NativeHook::SetIdentity(slot, "FarAdd", "RED4ext");
    const int kr = FarAttach(&slot, reinterpret_cast<void*>(TextDetour));
    if (kr != 0)
    {
        std::printf("FAIL far attach %d\n", kr);
    }

    CHECK(kr == 0);
    CHECK(FarPatch(reinterpret_cast<void*>(NhAdd)));
    CHECK(NhAdd(1) == 42);
    CHECK(NhAdd(1) == 42);

    NativeHook::HookInfo infos[32]{};
    const uint32_t n = NativeHook::CopyHookStats(infos, 32);
    bool saw = false;
    for (uint32_t i = 0; i < n && i < 32; ++i)
    {
        if (std::strcmp(infos[i].name, "FarAdd") != 0)
        {
            continue;
        }

        saw = true;
        CHECK(std::strcmp(infos[i].patch, "far") == 0);
        CHECK(infos[i].installed);
        CHECK(infos[i].hits == 2);
        CHECK(infos[i].installKr == 0);
    }

    CHECK(saw);
    Tx tx;
    CHECK(NativeHook::Detach(&slot, reinterpret_cast<void*>(TextDetour)) == 0);
    CHECK(tx.Commit());
    CHECK(NhAdd(1) == 2);
    CHECK(std::memcmp(reinterpret_cast<void*>(NhAdd), before, sizeof(before)) == 0);
    std::printf("PASS far patch\n");
}

static void TestFarChain()
{
    g_addA = NhAdd;
    g_addB = NhAdd;
    void* a = reinterpret_cast<void*>(g_addA);
    void* b = reinterpret_cast<void*>(g_addB);
    CHECK(FarAttach(&a, reinterpret_cast<void*>(AddA)) == 0);
    g_addA = reinterpret_cast<uint64_t (*)(uint64_t)>(a);
    CHECK(FarAttach(&b, reinterpret_cast<void*>(AddB)) == 0);
    g_addB = reinterpret_cast<uint64_t (*)(uint64_t)>(b);
    CHECK(NhAdd(1) == 112);
    {
        Tx tx;
        CHECK(NativeHook::Detach(&b, reinterpret_cast<void*>(AddB)) == 0);
        CHECK(tx.Commit());
    }
    CHECK(NhAdd(1) == 12);
    {
        Tx tx;
        CHECK(NativeHook::Detach(&a, reinterpret_cast<void*>(AddA)) == 0);
        CHECK(tx.Commit());
    }
    CHECK(NhAdd(1) == 2);
    std::printf("PASS far chain\n");
}

static void TestFarReloc()
{
    CHECK(NhHi(0x6ac) == 1);
    CHECK(NhHi(0x6ad) == 2);
    g_u64 = NhHi;
    void* slot = reinterpret_cast<void*>(g_u64);
    CHECK(FarAttach(&slot, reinterpret_cast<void*>(PassU64)) == 0);
    g_u64 = reinterpret_cast<uint64_t (*)(uint64_t)>(slot);
    CHECK(FarPatch(reinterpret_cast<void*>(NhHi)));
    CHECK(NhHi(0x6ac) == 1);
    CHECK(NhHi(0x6ad) == 2);
    {
        Tx tx;
        CHECK(NativeHook::Detach(&slot, reinterpret_cast<void*>(PassU64)) == 0);
        CHECK(tx.Commit());
    }

    CHECK(NhCbz(0) == 2);
    CHECK(NhCbz(3) == 1);
    g_u64 = NhCbz;
    slot = reinterpret_cast<void*>(g_u64);
    CHECK(FarAttach(&slot, reinterpret_cast<void*>(PassU64)) == 0);
    g_u64 = reinterpret_cast<uint64_t (*)(uint64_t)>(slot);
    CHECK(NhCbz(0) == 2);
    CHECK(NhCbz(3) == 1);
    {
        Tx tx;
        CHECK(NativeHook::Detach(&slot, reinterpret_cast<void*>(PassU64)) == 0);
        CHECK(tx.Commit());
    }

    CHECK(NhTbz(2) == 2);
    CHECK(NhTbz(3) == 1);
    g_u64 = NhTbz;
    slot = reinterpret_cast<void*>(g_u64);
    CHECK(FarAttach(&slot, reinterpret_cast<void*>(PassU64)) == 0);
    g_u64 = reinterpret_cast<uint64_t (*)(uint64_t)>(slot);
    CHECK(NhTbz(2) == 2);
    CHECK(NhTbz(3) == 1);
    {
        Tx tx;
        CHECK(NativeHook::Detach(&slot, reinterpret_cast<void*>(PassU64)) == 0);
        CHECK(tx.Commit());
    }

    const uint64_t adr = NhAdr();
    g_u64v = NhAdr;
    slot = reinterpret_cast<void*>(g_u64v);
    CHECK(FarAttach(&slot, reinterpret_cast<void*>(PassU64v)) == 0);
    g_u64v = reinterpret_cast<uint64_t (*)()>(slot);
    CHECK(NhAdr() == adr);
    {
        Tx tx;
        CHECK(NativeHook::Detach(&slot, reinterpret_cast<void*>(PassU64v)) == 0);
        CHECK(tx.Commit());
    }

    const uint64_t adrp = NhAdrp();
    g_u64v = NhAdrp;
    slot = reinterpret_cast<void*>(g_u64v);
    CHECK(FarAttach(&slot, reinterpret_cast<void*>(PassU64v)) == 0);
    g_u64v = reinterpret_cast<uint64_t (*)()>(slot);
    CHECK(NhAdrp() == adrp);
    {
        Tx tx;
        CHECK(NativeHook::Detach(&slot, reinterpret_cast<void*>(PassU64v)) == 0);
        CHECK(tx.Commit());
    }

    CHECK(NhLdr() == 0x1122334455667788ull);
    g_u64v = NhLdr;
    slot = reinterpret_cast<void*>(g_u64v);
    CHECK(FarAttach(&slot, reinterpret_cast<void*>(PassU64v)) == 0);
    g_u64v = reinterpret_cast<uint64_t (*)()>(slot);
    CHECK(NhLdr() == 0x1122334455667788ull);
    {
        Tx tx;
        CHECK(NativeHook::Detach(&slot, reinterpret_cast<void*>(PassU64v)) == 0);
        CHECK(tx.Commit());
    }

    std::printf("PASS far relocate\n");
}

static int Refuse(void* fn, void* detour)
{
    const uint32_t before = *reinterpret_cast<uint32_t*>(fn);
    void* slot = fn;
    const int kr = FarAttach(&slot, detour);
    CHECK(*reinterpret_cast<uint32_t*>(fn) == before);
    return kr;
}

static void TestFarRefuse()
{
    CHECK(NhBack(0) == 3);
    const int backKr = Refuse(reinterpret_cast<void*>(NhBack), reinterpret_cast<void*>(TextDetour));
    if (backKr != NativeHook::kErrInteriorBranch)
    {
        std::printf("FAIL interior kr=%d\n", backKr);
    }

    CHECK(backKr == NativeHook::kErrInteriorBranch);
    CHECK(NhBack(0) == 3);

    const auto shortNext = reinterpret_cast<uintptr_t>(NhShortNext);
    const auto shortFn = reinterpret_cast<uintptr_t>(NhShort);
    if (shortNext - shortFn != 8)
    {
        std::printf("FAIL short delta %llu\n", static_cast<unsigned long long>(shortNext - shortFn));
    }

    CHECK(shortNext - shortFn == 8);
    CHECK(NhShort() == 5);
    const int shortKr = Refuse(reinterpret_cast<void*>(NhShort), reinterpret_cast<void*>(TextDetour));
    if (shortKr != NativeHook::kErrFunctionTooShort)
    {
        std::printf("FAIL short kr=%d\n", shortKr);
    }

    CHECK(shortKr == NativeHook::kErrFunctionTooShort);
    CHECK(NhShort() == 5);

    const int midKr = Refuse(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(NhAdd) + 4),
                             reinterpret_cast<void*>(TextDetour));
    if (midKr != NativeHook::kErrNotFunctionStart)
    {
        std::printf("FAIL mid kr=%d\n", midKr);
    }

    CHECK(midKr == NativeHook::kErrNotFunctionStart);

    const uint32_t badBefore[3] = {reinterpret_cast<uint32_t*>(NhBad)[0], reinterpret_cast<uint32_t*>(NhBad)[1],
                                   reinterpret_cast<uint32_t*>(NhBad)[2]};
    void* bad = reinterpret_cast<void*>(NhBad);
    const int badKr = FarAttach(&bad, reinterpret_cast<void*>(TextDetour));
    if (badKr != NativeHook::kErrRelocate)
    {
        std::printf("FAIL reloc kr=%d\n", badKr);
    }

    CHECK(badKr == NativeHook::kErrRelocate);
    CHECK(std::memcmp(reinterpret_cast<void*>(NhBad), badBefore, sizeof(badBefore)) == 0);
    std::printf("PASS far refuse\n");
}

static NhBig (*g_structOrig)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) = nullptr;

static __attribute__((noinline)) NhBig StructDetour(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                                                    uint64_t f, uint64_t g, uint64_t h)
{
    NhBig out = g_structOrig(a, b, c, d, e, f, g, h);
    out.v[0] += 1;
    return out;
}

static void TestFarStruct()
{
    const NhBig plain = NhStruct(10, 20, 30, 40, 50, 60, 70, 80);
    CHECK(plain.v[0] == 10 && plain.v[1] == 20 && plain.v[2] == 30 && plain.v[3] == 40);
    CHECK(plain.v[4] == 50 && plain.v[5] == 60 && plain.v[6] == 70 && plain.v[7] == 80);
    g_structOrig = NhStruct;
    void* slot = reinterpret_cast<void*>(g_structOrig);
    CHECK(FarAttach(&slot, reinterpret_cast<void*>(StructDetour)) == 0);
    g_structOrig = reinterpret_cast<NhBig (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                              uint64_t)>(slot);
    const NhBig hooked = NhStruct(10, 20, 30, 40, 50, 60, 70, 80);
    CHECK(hooked.v[0] == 11 && hooked.v[1] == 20 && hooked.v[2] == 30 && hooked.v[3] == 40);
    CHECK(hooked.v[4] == 50 && hooked.v[5] == 60 && hooked.v[6] == 70 && hooked.v[7] == 80);
    Tx tx;
    CHECK(NativeHook::Detach(&slot, reinterpret_cast<void*>(StructDetour)) == 0);
    CHECK(tx.Commit());
    std::printf("PASS far x8\n");
}

int main()
{
    std::signal(SIGALRM, OnAlarm);
    std::signal(SIGBUS, OnFault);
    std::signal(SIGSEGV, OnFault);
    alarm(30);

    TestRelocator();
    TestProtectionAndIsland();
    TestChain();
    TestOutOfRange();
    TestParked(false);
    TestParked(true);
    TestHookStats();
    TestFarPatch();
    TestFarChain();
    TestFarReloc();
    TestFarRefuse();
    TestFarStruct();
    TestRefuseWrites();
    TestMainOnly();

    if (g_failed != 0)
    {
        std::printf("%d check(s) failed\n", g_failed);
        return 1;
    }

    std::printf("all native hook checks passed\n");
    return 0;
}
