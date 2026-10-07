#include "stdafx.hpp"
#include "StateSystem.hpp"
#include "Utils.hpp"
#include "App.hpp"

#include <chrono>
#include <fstream>

namespace
{
// One line per state transition in red4ext/logs/milestones.log, read by the unattended test runner.
void WriteMilestone(const char* aEvent, RED4ext::EGameStateType aStateType)
{
    const auto app = App::Get();
    if (!app || !app->GetPaths())
    {
        return;
    }

    std::ofstream file(app->GetPaths()->GetLogsDir() / "milestones.log", std::ios::app);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    file << ms << ' ' << aEvent << ' ' << Utils::Narrow(Utils::GetStateName(aStateType)) << '\n';
}
} // namespace

ESystemType StateSystem::GetType()
{
    return ESystemType::State;
}

void StateSystem::Startup()
{
}

void StateSystem::Shutdown()
{
    Log::trace("Removing all game states...");

    m_baseInitialization.onEnter.clear();
    m_baseInitialization.onUpdate.clear();
    m_baseInitialization.onExit.clear();

    m_initialization.onEnter.clear();
    m_initialization.onUpdate.clear();
    m_initialization.onExit.clear();

    m_running.onEnter.clear();
    m_running.onUpdate.clear();
    m_running.onExit.clear();

    m_shutdown.onEnter.clear();
    m_shutdown.onUpdate.clear();
    m_shutdown.onExit.clear();

    Log::trace("All game states were removed successfully");
}

bool StateSystem::Add(std::shared_ptr<PluginBase> aPlugin, RED4ext::EGameStateType aStateType, Func_t aOnEnter,
                      Func_t aOnUpdate, Func_t aOnExit)
{
    State* state = GetStateByType(aStateType);
    if (state)
    {
        if (aOnEnter)
        {
            state->onEnter.emplace_back(aPlugin, aOnEnter);
        }

        if (aOnUpdate)
        {
            state->onUpdate.emplace_back(aPlugin, aOnUpdate);
        }

        if (aOnExit)
        {
            state->onExit.emplace_back(aPlugin, aOnExit);
        }
    }

    return state;
}

bool StateSystem::OnEnter(RED4ext::EGameStateType aStateType, RED4ext::CGameApplication* aApp)
{
    State* state = GetStateByType(aStateType);
    if (state)
    {
        WriteMilestone("ENTER", aStateType);
        auto action = fmt::format(L"{}::OnEnter", Utils::GetStateName(aStateType));
        return Run(action, state->onEnter, aApp);
    }

    return true;
}

bool StateSystem::OnUpdate(RED4ext::EGameStateType aStateType, RED4ext::CGameApplication* aApp)
{
    State* state = GetStateByType(aStateType);
    if (state)
    {
        auto action = fmt::format(L"{}::OnUpdate", Utils::GetStateName(aStateType));
        return Run(action, state->onUpdate, aApp);
    }

    return true;
}

bool StateSystem::OnExit(RED4ext::EGameStateType aStateType, RED4ext::CGameApplication* aApp)
{
    State* state = GetStateByType(aStateType);
    if (state)
    {
        WriteMilestone("EXIT", aStateType);
        auto action = fmt::format(L"{}::OnExit", Utils::GetStateName(aStateType));
        return Run(action, state->onExit, aApp);
    }

    return true;
}

StateSystem::State* StateSystem::GetStateByType(RED4ext::EGameStateType aStateType)
{
    using enum RED4ext::EGameStateType;
    switch (aStateType)
    {
    case BaseInitialization:
    {
        return &m_baseInitialization;
    }
    case Initialization:
    {
        return &m_initialization;
    }
    case Running:
    {
        return &m_running;
    }
    case Shutdown:
    {
        return &m_shutdown;
    }
    default:
    {
        Log::warn("State with type {} is not handled", static_cast<int32_t>(aStateType));
        break;
    }
    }

    return nullptr;
}

bool StateSystem::Run(std::wstring_view aAction, std::list<StateItem>& aList, RED4ext::CGameApplication* aApp)
{
    bool result = true;
    for (auto it = aList.begin(); it != aList.end();)
    {
        auto pluginName = it->plugin->GetName();

        try
        {
            if (it->func(aApp))
            {
                it = aList.erase(it);
            }
            else
            {
                ++it;
                result = false;
            }
        }
        catch (const std::exception& e)
        {
            Log::warn(L"An exception occured while executing '{}' registered by '{}'", aAction, pluginName);
            Log::warn(e.what());
        }
        catch (...)
        {
            Log::warn(L"An unknown exception occured while executing '{}' registered by '{}'", aAction, pluginName);
        }
    }

    return result;
}
