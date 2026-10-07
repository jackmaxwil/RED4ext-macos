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
