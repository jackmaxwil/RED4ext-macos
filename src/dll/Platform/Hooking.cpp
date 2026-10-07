#include "stdafx.hpp"
#include "Hooking.hpp"
#include "Platform.hpp"
#include "Platform/NativeHook.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
// Detours-compatible entry points on macOS, backed by the native hook engine (Platform/NativeHook).

namespace
{
bool g_inTransaction = false;
}

extern "C" {

int32_t DetourTransactionBegin()
{
    if (g_inTransaction)
    {
        return -1;
    }

    NativeHook::Begin();
    g_inTransaction = true;
    return NO_ERROR;
}

int32_t DetourTransactionCommit()
{
    if (!g_inTransaction)
    {
        return -1;
    }

    const int32_t rc = NativeHook::Commit();
    if (rc != NO_ERROR)
    {
        return rc;
    }

    g_inTransaction = false;
    return NO_ERROR;
}

int32_t DetourTransactionAbort()
{
    if (!g_inTransaction)
    {
        return -1;
    }

    NativeHook::Abort();
    g_inTransaction = false;
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

    return NativeHook::Attach(ppPointer, pDetour);
}

int32_t DetourDetach(void** ppPointer, void* pDetour)
{
    if (!g_inTransaction)
    {
        return -1;
    }

    return NativeHook::Detach(ppPointer, pDetour);
}

}

#endif
