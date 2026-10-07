#pragma once

#include "Addresses.hpp"
#ifdef RED4EXT_PLATFORM_MACOS
#include "Platform/NativeHook.hpp"
#include <cstdio>
#endif

template<typename T>
class Hook
{
public:
    Hook(T aAddress, T aDetour)
        : m_isAttached(false)
        , m_address(aAddress)
        , m_detour(aDetour)
        , m_hash(0)
    {
    }

    Hook(std::uint32_t aHash, T aDetour)
        : Hook(reinterpret_cast<T>(0), aDetour)
    {
        m_hash = aHash;
    }

    operator T() const
    {
        return m_address;
    }

    uintptr_t GetAddress() const
    {
        if (m_address == 0)
        {
            const auto address = Addresses::Instance();
            m_address = reinterpret_cast<T>(address->Resolve(m_hash));
        }

        return reinterpret_cast<uintptr_t>(m_address);
    }

    int32_t Attach()
    {
        if (m_isAttached)
        {
            return 0;
        }

        if (m_address == 0)
        {
            m_address = reinterpret_cast<T>(GetAddress());
        }

#ifdef RED4EXT_PLATFORM_MACOS
        if (m_hash != 0)
        {
            const char* friendly = NativeHook::CoreHookName(m_hash);
            std::string symbol;
            if (Addresses::Instance() != nullptr)
            {
                symbol = Addresses::Instance()->SymbolForHash(m_hash);
            }

            char hashName[32];
            const char* name = friendly;
            if (name == nullptr && !symbol.empty())
            {
                name = symbol.c_str();
            }

            if (name == nullptr)
            {
                std::snprintf(hashName, sizeof(hashName), "hash:%08x", m_hash);
                name = hashName;
            }

            NativeHook::SetIdentity(reinterpret_cast<void*>(m_address), name, "RED4ext");
        }

        auto result = DetourAttach(reinterpret_cast<void**>(&m_address), reinterpret_cast<void*>(m_detour));
#else
        auto result = DetourAttach(&m_address, m_detour);
#endif
        m_isAttached = result == NO_ERROR;

        return result;
    }

    int32_t Detach()
    {
        if (!m_isAttached)
        {
            return 0;
        }

#ifdef RED4EXT_PLATFORM_MACOS
        auto result = DetourDetach(reinterpret_cast<void**>(&m_address), reinterpret_cast<void*>(m_detour));
#else
        auto result = DetourDetach(&m_address, m_detour);
#endif
        m_isAttached = result == NO_ERROR;

        return result;
    }

private:
    bool m_isAttached;
    mutable T m_address;
    T m_detour;

    uint32_t m_hash;
};
