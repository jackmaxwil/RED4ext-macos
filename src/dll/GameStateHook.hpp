#pragma once

#include "MemoryProtection.hpp"
#include "Platform.hpp"

#include "Utils.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include <libkern/OSCacheControl.h>
#endif

template<typename T>
class GameStateHook
{
    // Index of IGameState::OnEnter in the vtable; OnUpdate and OnExit follow it.
    // MSVC emits one destructor slot. The macOS game is built with clang (Itanium ABI), which emits two
    // (complete + deleting), so every later virtual moves down one slot.
#ifdef RED4EXT_PLATFORM_MACOS
    static constexpr size_t OnEnterSlot = 4;
#else
    static constexpr size_t OnEnterSlot = 3;
#endif

public:
    using Func_t = bool (*)(T*, RED4ext::CGameApplication*);

    static_assert(std::is_base_of_v<RED4ext::IGameState, T>, "T should inherit IGameState");

    GameStateHook(Func_t aOnEnter, Func_t aOnUpdate, Func_t aOnExit)
        : m_isAttached(false)
        , m_onEnter(aOnEnter)
        , m_onUpdate(aOnUpdate)
        , m_onExit(aOnExit)
    {
    }

    bool AttachAt(T* aState)
    {
        auto name = aState->GetName();
        auto vtbl = *reinterpret_cast<Func_t**>(aState);

        Log::trace("Changing virtual functions for '{}' state at {}...", name, fmt::ptr(vtbl));

        m_onEnter.orig = reinterpret_cast<Func_t>(vtbl[OnEnterSlot]);
        m_onUpdate.orig = reinterpret_cast<Func_t>(vtbl[OnEnterSlot + 1]);
        m_onExit.orig = reinterpret_cast<Func_t>(vtbl[OnEnterSlot + 2]);

        if (SwapVFuncs(aState, m_onEnter.detour, m_onUpdate.detour, m_onExit.detour))
        {
            Log::trace("Virtual functions for '{}' state were changed successfully", name);
            return true;
        }

        return false;
    }

    bool DetachAt(T* aState)
    {
        auto name = aState->GetName();
        auto vtbl = *reinterpret_cast<void**>(aState);

        Log::trace("Restoring virtual functions for '{}' state at {}...", name, vtbl);

        if (SwapVFuncs(aState, m_onEnter.orig, m_onUpdate.orig, m_onExit.orig))
        {
            Log::trace("Virtual functions for '{}' state were restored successfully", name);
            return true;
        }

        return false;
    }

    bool OnEnter(T* aState, RED4ext::CGameApplication* aApp)
    {
        if (m_onEnter.shouldExecute && m_onEnter.orig)
        {
            auto result = m_onEnter.orig(aState, aApp);
            m_onEnter.shouldExecute = !result;

            return result;
        }

        return true;
    }

    bool OnUpdate(T* aState, RED4ext::CGameApplication* aApp)
    {
        if (m_onUpdate.shouldExecute && m_onUpdate.orig)
        {
            auto result = m_onUpdate.orig(aState, aApp);
            m_onUpdate.shouldExecute = !result;

            return result;
        }

        return true;
    }

    bool OnExit(T* aState, RED4ext::CGameApplication* aApp)
    {
        if (m_onExit.shouldExecute && m_onExit.orig)
        {
            auto result = m_onExit.orig(aState, aApp);
            m_onExit.shouldExecute = !result;

            return result;
        }

        return true;
    }

private:
    struct FuncHook
    {
        FuncHook(Func_t aDetour)
            : shouldExecute(true)
            , detour(aDetour)
            , orig(nullptr)
        {
        }

        bool shouldExecute;
        Func_t detour;
        Func_t orig;
    };

    bool SwapVFuncs(T* aState, Func_t aOnEnter, Func_t aOnUpdate, Func_t aOnExit)
    {
        auto name = aState->GetName();
        auto vtbl = *reinterpret_cast<Func_t**>(aState);

        try
        {
            const auto addr = &vtbl[OnEnterSlot];

            constexpr auto VirtualFuncs = 3;
            constexpr auto size = VirtualFuncs * sizeof(addr);

            MemoryProtection _(addr, size, Platform::Memory_ReadWrite);

            auto onEnter = &vtbl[OnEnterSlot];
            Log::trace("Changing 'OnEnter' function at {} from {} to {}...", fmt::ptr(onEnter),
                       fmt::ptr(vtbl[OnEnterSlot]), fmt::ptr(aOnEnter));

            *onEnter = aOnEnter;
            Log::trace("'OnEnter' function was changed successfully");

            auto onUpdate = &vtbl[OnEnterSlot + 1];
            Log::trace("Changing 'OnUpdate' function at {} from {} to {}...", fmt::ptr(onUpdate),
                       fmt::ptr(vtbl[OnEnterSlot + 1]), fmt::ptr(aOnUpdate));

            *onUpdate = aOnUpdate;
            Log::trace("'OnUpdate' function was changed successfully");

            auto onExit = &vtbl[OnEnterSlot + 2];
            Log::trace("Changing 'OnExit' function at {} from {} to {}...", fmt::ptr(onExit),
                       fmt::ptr(vtbl[OnEnterSlot + 2]), fmt::ptr(aOnExit));

            *onExit = aOnExit;
            Log::trace("'OnExit' function was changed successfully");

#ifdef RED4EXT_PLATFORM_MACOS
            // Invalidate instruction cache for ARM64 after vtable modification
            sys_icache_invalidate(addr, size);
#endif
        }
        catch (const MemoryProtection::Exception&)
        {
            Log::warn("Could not change / restoring the protection for '{}' state, the execution will continue but "
                      "unexpected behavior might happen",
                      name);
            return false;
        }
        catch (const std::exception& e)
        {
            Log::warn("An exception occured while changing / restoring the virtual functions for '{}' state, the "
                      "execution will continue but unexpected behavior might happen",
                      name);
            Log::warn(e.what());

            return false;
        }
        catch (...)
        {
            Log::warn(
                "An unknown exception occured while changing / restoring the virtual functions for '{}' state, the "
                "execution will continue but unexpected behavior might happen",
                name);
            return false;
        }

        return true;
    }

    bool m_isAttached;
    FuncHook m_onEnter;
    FuncHook m_onUpdate;
    FuncHook m_onExit;
};
