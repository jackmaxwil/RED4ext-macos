#include "stdafx.hpp"
#include "HookingSystem.hpp"
#include "DetourTransaction.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include <fishhook.h>
#endif

ESystemType HookingSystem::GetType()
{
    return ESystemType::Hooking;
}

void HookingSystem::Startup()
{
}

void HookingSystem::Shutdown()
{
    std::scoped_lock _(m_mutex);

    Log::trace("Detaching {} dangling hook(s)...", m_hooks.size());

    DetourTransaction transaction;
    size_t count = 0;

    for (auto it = m_hooks.begin(); it != m_hooks.end(); ++it)
    {
        auto plugin = it->first;
        auto& item = it->second;

        if (QueueForDetach(plugin, item))
        {
            count++;
        }
    }

    if (transaction.Commit())
    {
        Log::trace("{} dangling hook(s) detached", count);
    }
    else
    {
        Log::trace("Could not detach {} dangling hook(s)", count);
    }

    m_hooks.clear();
}

bool HookingSystem::Attach(std::shared_ptr<PluginBase> aPlugin, const char* aSymbol, void* aDetour, void** aOriginal)
{
#ifdef RED4EXT_PLATFORM_MACOS
    Log::trace("Attaching a hook for '{}' at symbol '{}' with detour at {}...", aPlugin->GetName(), aSymbol, aDetour);
    std::scoped_lock _(m_mutex);

    struct rebinding rebind;
    rebind.name = aSymbol;
    rebind.replacement = aDetour;
    rebind.replaced = aOriginal;

    if (rebind_symbols(&rebind, 1) != 0)
    {
        Log::warn("The hook requested by '{}' at symbol '{}' could not be attached.", aPlugin->GetName(), aSymbol);
        return false;
    }

    Item item(aSymbol, aDetour, aOriginal);
    m_hooks.emplace(aPlugin, std::move(item));

    Log::trace("The hook requested by '{}' at symbol '{}' has been successfully attached", aPlugin->GetName(),
                  aSymbol);
    return true;
#else
    RED4EXT_UNUSED_PARAMETER(aPlugin);
    RED4EXT_UNUSED_PARAMETER(aSymbol);
    RED4EXT_UNUSED_PARAMETER(aDetour);
    RED4EXT_UNUSED_PARAMETER(aOriginal);
    return false;
#endif
}

bool HookingSystem::Attach(std::shared_ptr<PluginBase> aPlugin, void* aTarget, void* aDetour, void** aOriginal)
{
    Log::trace(L"Attaching a hook for '{}' at {} with detour at {}...", aPlugin->GetName(), aTarget, aDetour);
    std::scoped_lock _(m_mutex);

    DetourTransaction transaction;
    Item item(aTarget, aDetour, aOriginal);

    auto result = item.hook.Attach();
    if (result != NO_ERROR)
    {
        Log::warn(L"The hook requested by '{}' at {} could not be attached. Detour error code: {}",
                     aPlugin->GetName(), aTarget, result);
        return false;
    }

    if (transaction.Commit())
    {
        if (aOriginal)
        {
            *aOriginal = reinterpret_cast<void*>(item.hook.GetAddress());
        }

        m_hooks.emplace(aPlugin, std::move(item));

        Log::trace(L"The hook requested by '{}' at {} has been successfully attached", aPlugin->GetName(), aTarget);
        return true;
    }

    Log::warn(L"The hook requested by '{}' at {} was not attached", aPlugin->GetName(), aTarget);
    return false;
}

bool HookingSystem::Detach(std::shared_ptr<PluginBase> aPlugin, void* aTarget)
{
    Log::trace(L"Detaching all hooks attached by '{}' at {}...", aPlugin->GetName(), aTarget);
    std::scoped_lock _(m_mutex);

    DetourTransaction transaction;
    size_t count = 0;
    bool hasHook = false;

    auto range = m_hooks.equal_range(aPlugin);
    for (auto it = range.first; it != range.second; ++it)
    {
        auto& item = it->second;
        if (item.target == aTarget || (item.symbol && item.original && *item.original == aTarget))
        {
            hasHook = true;

            if (QueueForDetach(aPlugin, item))
            {
                count++;
            }
        }
    }

    if (!hasHook)
    {
        Log::warn(L"No hooks attached by '{}' at {} were found", aPlugin->GetName(), aTarget);
    }
    else if (count == 0)
    {
        Log::warn(L"No hooks attached by '{}' at {} were queued for detaching", aPlugin->GetName(), aTarget);
    }
    else if (transaction.Commit())
    {
        Log::trace(L"{} hook(s) attached by '{}' at {} have been successfully detached", count, aPlugin->GetName(),
                      aTarget);

        for (auto it = range.first; it != range.second;)
        {
            auto& item = it->second;
            if (item.target == aTarget || (item.symbol && item.original && *item.original == aTarget))
            {
                if (item.original)
                {
                    *item.original = nullptr;
                }
                it = m_hooks.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    return count > 0;
}

bool HookingSystem::QueueForDetach(std::shared_ptr<PluginBase> aPlugin, Item& aItem)
{
    if (aItem.symbol)
    {
#ifdef RED4EXT_PLATFORM_MACOS
        struct rebinding rebind;
        rebind.name = aItem.symbol;
        rebind.replacement = *aItem.original;
        rebind.replaced = nullptr;

        if (rebind_symbols(&rebind, 1) != 0)
        {
            Log::warn("A hook attached by '{}' at symbol '{}' could not be detached.", aPlugin->GetName(),
                         aItem.symbol);
            return false;
        }

        Log::trace("A hook attached by '{}' at symbol '{}' has been successfully queued for detaching",
                      aPlugin->GetName(), aItem.symbol);
        return true;
#else
        return false;
#endif
    }

    auto target = aItem.target;

    Log::trace(L"Queueing a hook attached by '{}' at {} for detaching...", aPlugin->GetName(), target);

    auto result = aItem.hook.Detach();
    if (result != NO_ERROR)
    {
        Log::warn(L"A hook attached by '{}' at {} could not be detached. Detour error code: {}", aPlugin->GetName(),
                     target, result);
        return false;
    }

    Log::trace(L"A hook attached by '{}' at {} has been successfully queued for detaching", aPlugin->GetName(),
                  target);
    return true;
}
