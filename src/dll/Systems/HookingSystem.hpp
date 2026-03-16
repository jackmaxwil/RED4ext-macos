#pragma once

#include "Hook.hpp"
#include "ISystem.hpp"
#include "PluginBase.hpp"

class HookingSystem : public ISystem
{
public:
    ESystemType GetType() final;

    void Startup() final;
    void Shutdown() final;

    bool Attach(std::shared_ptr<PluginBase> aPlugin, void* aTarget, void* aDetour, void** aOriginal);
    bool Attach(std::shared_ptr<PluginBase> aPlugin, const char* aSymbol, void* aDetour, void** aOriginal);
    bool Detach(std::shared_ptr<PluginBase> aPlugin, void* aTarget);

private:
    struct Item
    {
        Item(void* aTarget, void* aDetour, void** aOriginal)
            : target(aTarget)
            , symbol(nullptr)
            , original(aOriginal)
            , hook(aTarget, aDetour)
        {
        }

        Item(const char* aSymbol, void* aDetour, void** aOriginal)
            : target(nullptr)
            , symbol(aSymbol)
            , original(aOriginal)
            , hook(static_cast<uint32_t>(0), aDetour) // Dummy hash
        {
        }

        void* target;
        const char* symbol;
        void** original;
        Hook<void*> hook;
    };

    using Map_t = std::unordered_multimap<std::shared_ptr<PluginBase>, Item>;
    using MapIter_t = Map_t::iterator;

    bool QueueForDetach(std::shared_ptr<PluginBase> aPlugin, Item& aItem);

    std::mutex m_mutex;
    Map_t m_hooks;
};
