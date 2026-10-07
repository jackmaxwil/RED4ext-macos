#include "AssertionFailed.hpp"
#include "Addresses.hpp"
#include "App.hpp"
#include "Detail/AddressHashes.hpp"
#include "Hook.hpp"
#include "stdafx.hpp"

namespace
{

bool isAttached = false;

void _AssertionFailed(const char*, int, const char*, const char*, ...);

Hook<decltype(&_AssertionFailed)> AssertionFailed_fnc(Hashes::AssertionFailed, &_AssertionFailed);

void _AssertionFailed(const char* aFile, int aLineNum, const char* aCondition, const char* aMessage, ...)
{
    Log::error("Crash report");
    Log::error("------------");
    Log::error("File: {}", aFile);
    Log::error("Line: {}", aLineNum);

    // Size limit defined by the game.
    char msg[0x400] = "<not supplied>";

    if (aCondition)
    {
        Log::error("Condition: {}", aCondition);
    }
    if (aMessage)
    {
        va_list args;
        va_start(args, aMessage);

#ifdef RED4EXT_PLATFORM_MACOS
        vsnprintf(msg, sizeof(msg), aMessage, args);
#else
        vsnprintf_s(msg, sizeof(msg), aMessage, args);
#endif
        Log::error("Message: {}", msg);

        va_end(args);
    }

    Log::error("------------");
    spdlog::details::registry::instance().flush_all();

    AssertionFailed_fnc(aFile, aLineNum, aCondition, msg);
}
} // namespace

bool Hooks::AssertionFailed::Attach()
{
    Log::trace("Trying to attach the hook for the assertion failed function at {:#x}...",
               AssertionFailed_fnc.GetAddress());

    auto result = AssertionFailed_fnc.Attach();
    if (result != NO_ERROR)
    {
        Log::error("Could not attach the hook for the assertion failed function. Detour error code: {}", result);
    }
    else
    {
        Log::trace("The hook for the assertion failed function was attached");
    }

    isAttached = result == NO_ERROR;
    return isAttached;
}

bool Hooks::AssertionFailed::Detach()
{
    if (!isAttached)
    {
        return false;
    }

    Log::trace("Trying to detach the hook for the assertion failed function at {:#x}...",
               AssertionFailed_fnc.GetAddress());

    auto result = AssertionFailed_fnc.Detach();
    if (result != NO_ERROR)
    {
        Log::error("Could not detach the hook for the assertion failed function. Detour error code: {}", result);
    }
    else
    {
        Log::trace("The hook for the assertion failed function was detached");
    }

    isAttached = result != NO_ERROR;
    return !isAttached;
}
