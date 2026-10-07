#include "stdafx.hpp"
#include "Hooking.hpp"
#include "Platform.hpp"
#include "Platform/NativeHook.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#ifdef RED4EXT_USE_FRIDA_GUM
#include <frida-gum.h>
#endif

// Hooking backends on macOS:
//  - Frida Gadget: registration-only; actual interception is done externally.
//  - Native inline: patch target __TEXT and create an in-process trampoline.

namespace
{

enum class HookBackend : int32_t
{
    FridaGadget = 0,
    NativeInline = 1,
    FridaGum = 2,
};

std::atomic<int32_t> g_backend{
#ifdef RED4EXT_USE_FRIDA_GADGET
    static_cast<int32_t>(HookBackend::FridaGadget)
#else
    static_cast<int32_t>(HookBackend::NativeInline)
#endif
};

bool g_inTransaction = false;
int g_hookCount = 0;

#ifdef RED4EXT_USE_FRIDA_GUM
std::once_flag g_gumInitOnce;
GumInterceptor* g_gumInterceptor = nullptr;
bool g_gumInTransaction = false;

struct GumHookRecord
{
    void* target = nullptr;
    void* detour = nullptr;
};

std::unordered_map<void*, GumHookRecord> g_gumHooksByOriginal;

bool EnsureGum()
{
    std::call_once(g_gumInitOnce, []() {
        gum_init_embedded();
        g_gumInterceptor = gum_interceptor_obtain();
    });

    return g_gumInterceptor != nullptr;
}
#endif

}

extern "C" {

void DetourSetBackend(int32_t aBackend)
{
    if (g_inTransaction)
    {
        return;
    }

    if (aBackend != static_cast<int32_t>(HookBackend::FridaGadget) &&
        aBackend != static_cast<int32_t>(HookBackend::NativeInline)
#ifdef RED4EXT_USE_FRIDA_GUM
        && aBackend != static_cast<int32_t>(HookBackend::FridaGum)
#endif
    )
    {
        return;
    }

    g_backend.store(aBackend);
}

int32_t DetourGetBackend()
{
    return g_backend.load();
}

int32_t DetourTransactionBegin()
{
    if (g_inTransaction)
    {
        return -1;
    }

    const auto backend = static_cast<HookBackend>(g_backend.load());
#ifdef RED4EXT_USE_FRIDA_GUM
    if (backend == HookBackend::FridaGum)
    {
        if (!EnsureGum())
        {
            return -1;
        }

        gum_interceptor_begin_transaction(g_gumInterceptor);
        g_gumInTransaction = true;
    }
#endif

    if (backend == HookBackend::NativeInline)
    {
        NativeHook::Begin();
    }

    g_inTransaction = true;
    return NO_ERROR;
}

int32_t DetourTransactionCommit()
{
    if (!g_inTransaction)
    {
        return -1;
    }

    const auto backend = static_cast<HookBackend>(g_backend.load());

#ifdef RED4EXT_USE_FRIDA_GUM
    if (backend == HookBackend::FridaGum && g_gumInTransaction)
    {
        gum_interceptor_end_transaction(g_gumInterceptor);
        g_gumInTransaction = false;
    }
#endif

    if (backend == HookBackend::NativeInline)
    {
        const int32_t rc = NativeHook::Commit();
        if (rc != NO_ERROR)
        {
            return rc;
        }

        g_inTransaction = false;
        return NO_ERROR;
    }

    g_inTransaction = false;

    if (backend == HookBackend::FridaGadget)
    {
        spdlog::info("[Hooking] Transaction commit: {} hooks registered (Frida Gadget backend)", g_hookCount);
    }
    else if (backend == HookBackend::FridaGum)
    {
        spdlog::info("[Hooking] Transaction commit: {} hooks registered (Frida Gum backend)", g_hookCount);
    }

    return NO_ERROR;
}

int32_t DetourTransactionAbort()
{
    if (!g_inTransaction)
    {
        return -1;
    }

#ifdef RED4EXT_USE_FRIDA_GUM
    const auto backend = static_cast<HookBackend>(g_backend.load());
    if (backend == HookBackend::FridaGum && g_gumInTransaction)
    {
        gum_interceptor_end_transaction(g_gumInterceptor);
        g_gumInTransaction = false;
    }
#endif

    if (static_cast<HookBackend>(g_backend.load()) == HookBackend::NativeInline)
    {
        NativeHook::Abort();
    }

    g_inTransaction = false;
    g_hookCount = 0;
    return NO_ERROR;
}

int32_t DetourUpdateThread([[maybe_unused]] void* hThread)
{
    return NO_ERROR;
}

int32_t DetourAttach(void** ppPointer, void* pDetour)
{
    if (!g_inTransaction)
    {
        spdlog::error("[Hooking] DetourAttach failed: not in transaction");
        return -1;
    }

    const auto backend = static_cast<HookBackend>(g_backend.load());

    if (backend == HookBackend::FridaGadget)
    {
        void* pTarget = *ppPointer;
        if (!pTarget || !pDetour)
        {
            spdlog::error("[Hooking] DetourAttach failed: null pointer");
            return -1;
        }

        g_hookCount++;
        spdlog::info("[Hooking] Hook #{} registered at {} -> {} (Frida Gadget backend)", g_hookCount, pTarget,
                     pDetour);
        return NO_ERROR;
    }

    if (backend == HookBackend::FridaGum)
    {
#ifdef RED4EXT_USE_FRIDA_GUM
        if (!EnsureGum())
        {
            spdlog::error("[Hooking] Frida Gum init failed");
            return -1;
        }

        if (!g_gumInTransaction)
        {
            spdlog::error("[Hooking] Frida Gum DetourAttach called outside transaction");
            return -1;
        }

        void* pTarget = *ppPointer;
        gpointer original = nullptr;
        const auto ret = gum_interceptor_replace(g_gumInterceptor, pTarget, pDetour, nullptr, &original);
        if (ret != GUM_REPLACE_OK || original == nullptr)
        {
            spdlog::error("[Hooking] gum_interceptor_replace failed at {} -> {} (ret={})", pTarget, pDetour,
                          static_cast<int>(ret));
            return -1;
        }

        g_gumHooksByOriginal[original] = GumHookRecord{pTarget, pDetour};

        g_hookCount++;
        spdlog::info("[Hooking] Hook #{} installed at {} -> {} (Frida Gum backend)", g_hookCount, pTarget, pDetour);

        *ppPointer = original;
        return NO_ERROR;
#else
        spdlog::error("[Hooking] Frida Gum backend requested but RED4EXT_USE_FRIDA_GUM is not enabled");
        return -1;
#endif
    }

    return NativeHook::Attach(ppPointer, pDetour);
}

int32_t DetourDetach(void** ppPointer, void* pDetour)
{
    if (!g_inTransaction)
    {
        return -1;
    }

    const auto backend = static_cast<HookBackend>(g_backend.load());

    if (backend == HookBackend::FridaGadget)
    {
        return NO_ERROR;
    }

    if (backend == HookBackend::FridaGum)
    {
#ifdef RED4EXT_USE_FRIDA_GUM
        if (!EnsureGum())
        {
            return -1;
        }

        if (!g_gumInTransaction)
        {
            return -1;
        }

        void* original = *ppPointer;
        auto it = g_gumHooksByOriginal.find(original);
        if (it == g_gumHooksByOriginal.end())
        {
            return -1;
        }

        const auto rec = it->second;
        if (rec.detour != pDetour)
        {
            return -1;
        }

        gum_interceptor_revert(g_gumInterceptor, rec.target);
        g_gumHooksByOriginal.erase(it);
        *ppPointer = rec.target;
        return NO_ERROR;
#else
        return -1;
#endif
    }

    return NativeHook::Detach(ppPointer, pDetour);
}

}

#endif
