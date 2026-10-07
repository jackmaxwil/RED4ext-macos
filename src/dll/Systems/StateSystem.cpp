#include "stdafx.hpp"
#include "StateSystem.hpp"
#include "Utils.hpp"
#include "App.hpp"
#include "Platform/NativeHook.hpp"

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

// Registered from the RTTI post-register callback at startup; its presence at Running proves the native function
// registration path that plugins (ModMenu, test bridges) depend on.
constexpr auto SelfCheckFunctionName = "RED4extSelfCheck_Ping";

void SelfCheckPing(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, int32_t* aOut, int64_t)
{
    aFrame->code++;
    if (aOut)
    {
        *aOut = 42;
    }
}

// RED4ext_TestReport(line: String): appends one line to red4ext/logs/autotest.log. The unattended test driver
// (tools/autotest) reports its results through it.
void TestReport(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, void*, int64_t)
{
    RED4ext::CString line;
    RED4ext::GetParameter(aFrame, &line);
    aFrame->code++;

    if (const auto app = App::Get(); app && app->GetPaths())
    {
        std::ofstream file(app->GetPaths()->GetLogsDir() / "autotest.log", std::ios::app);
        file << line.c_str() << std::endl;
    }
}

void RegisterSelfCheckFunctions()
{
    auto func = RED4ext::CGlobalFunction::Create(SelfCheckFunctionName, SelfCheckFunctionName, &SelfCheckPing);
    func->flags = {.isNative = true, .isStatic = true};
    func->SetReturnType("Int32");
    RED4ext::CRTTISystem::Get()->RegisterFunction(func);

    // AddParam grows a game DynArray, which needs a verified DynArray_Realloc. Check the capability before using it.
    static RED4ext::UniversalRelocFunc<void (*)()> dynArrayRealloc(RED4ext::Detail::AddressHashes::DynArray_Realloc);
    if (!dynArrayRealloc.IsValid())
    {
        Log::warn("DynArray_Realloc is not verified; RED4ext_TestReport is not registered");
        return;
    }

    auto report = RED4ext::CGlobalFunction::Create("RED4ext_TestReport", "RED4ext_TestReport", &TestReport);
    report->flags = {.isNative = true, .isStatic = true};
    report->AddParam("String", "line");
    RED4ext::CRTTISystem::Get()->RegisterFunction(report);
}

// SDK smoke checks against the live game, run once when the Running state is entered. Each writes
// "CHECK <name> pass|fail" to milestones.log for the test runner.
void RunSdkSelfChecks()
{
    const auto app = App::Get();
    std::ofstream file(app->GetPaths()->GetLogsDir() / "milestones.log", std::ios::app);

    auto allocator = RED4ext::Memory::DefaultAllocator::Get();
    auto block = allocator->Alloc(64);
    bool memOk = block.memory != nullptr && block.size >= 64;
    if (memOk)
    {
        std::memset(block.memory, 0xAB, 64);
        auto grown = allocator->Realloc(block, 4096);
        memOk = grown.memory != nullptr && static_cast<uint8_t*>(grown.memory)[63] == 0xAB;
        if (grown.memory)
        {
            allocator->Free(grown);
        }
    }
    file << "CHECK memory_pool_default " << (memOk ? "pass" : "fail") << std::endl;

    // A wrong CRTTISystem_Get address returns an object without a vtable; do not call through it.
    auto rtti = RED4ext::CRTTISystem::Get();
    auto hasVtable = rtti && *reinterpret_cast<void**>(rtti) != nullptr;
    auto boolType = hasVtable ? rtti->GetType("Bool") : nullptr;
    file << "CHECK rtti_get_type " << (boolType && boolType->GetSize() == 1 ? "pass" : "fail") << std::endl;

    auto name = RED4ext::CNamePool::Add("RED4extSelfCheckName");
    auto nameText = RED4ext::CNamePool::Get(name);
    bool nameOk = name.hash == RED4ext::CName("RED4extSelfCheckName").hash && std::strcmp(nameText, "RED4extSelfCheckName") == 0;
    file << "CHECK cname_pool " << (nameOk ? "pass" : "fail") << std::endl;

    bool strOk = false;
    {
        RED4ext::CString shortStr("hello");
        RED4ext::CString longStr("a string longer than the inline buffer of twenty bytes");
        RED4ext::CString copy(longStr);
        strOk = shortStr.Length() == 5 && std::strcmp(shortStr.c_str(), "hello") == 0 && copy.Length() == longStr.Length() &&
                std::strcmp(copy.c_str(), longStr.c_str()) == 0;
    }
    file << "CHECK cstring " << (strOk ? "pass" : "fail") << std::endl;

    auto registered = hasVtable ? rtti->GetFunction(SelfCheckFunctionName) : nullptr;
    file << "CHECK native_function_registration " << (registered ? "pass" : "fail") << std::endl;
}

std::string JsonName(RED4ext::CName aName)
{
    std::string out;
    for (const char* c = aName.ToString(); c && *c; ++c)
    {
        if (*c == '"' || *c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(*c) >= 0x20)
            out += *c;
    }
    return out;
}

// RED4EXT_DUMP_RTTI=1: write every native/scripted class's layout as the macOS game built it to
// logs/rtti_layout_macos.json. Reads only what docs/re/reflection_layout.md confirmed from macOS code:
// IRTTISystem::GetClasses (+0x70, takes the RTTI lock), the CBaseRTTIType virtuals, CClass parent @0x10, props @0x28,
// flags @0x70, and CProperty type @0x00, name @0x08, valueOffset @0x20, flags @0x28. CClass::unk118 is a lazily built
// cache (empty for unused classes) and is not read.
void DumpRttiLayout()
{
    if (!std::getenv("RED4EXT_DUMP_RTTI"))
        return;

    auto rtti = RED4ext::CRTTISystem::Get();
    if (!rtti || !*reinterpret_cast<void**>(rtti))
        return;

    RED4ext::DynArray<RED4ext::CClass*> classes;
    rtti->GetClasses(nullptr, classes, nullptr, true);

    std::map<std::string, const RED4ext::CBaseRTTIType*> types;
    std::string body;
    for (auto cls : classes)
    {
        if (!cls)
            continue;
        types.emplace(JsonName(cls->GetName()), cls);
        body += fmt::format("{}\"{}\":{{\"parent\":\"{}\",\"size\":{},\"align\":{},\"flags\":{},\"props\":[",
                            body.empty() ? "" : ",\n", JsonName(cls->GetName()),
                            cls->parent ? JsonName(cls->parent->GetName()) : "", cls->GetSize(), cls->GetAlignment(),
                            *reinterpret_cast<const uint32_t*>(&cls->flags));
        for (uint32_t i = 0; i < cls->props.size; ++i)
        {
            auto prop = cls->props.entries[i];
            if (!prop)
                continue;
            auto typeName = prop->type ? JsonName(prop->type->GetName()) : "";
            if (prop->type)
                types.emplace(typeName, prop->type);
            body += fmt::format("{}{{\"name\":\"{}\",\"type\":\"{}\",\"offset\":{},\"flags\":{}}}", i ? "," : "",
                                JsonName(prop->name), typeName, prop->valueOffset,
                                *reinterpret_cast<const uint64_t*>(&prop->flags));
        }
        body += "]}";
    }

    std::string typeBody;
    for (const auto& [name, type] : types)
    {
        typeBody += fmt::format("{}\"{}\":{{\"size\":{},\"align\":{},\"kind\":{}}}", typeBody.empty() ? "" : ",\n", name,
                                type->GetSize(), type->GetAlignment(), static_cast<int>(type->GetType()));
    }

    char uuid[80]{};
    NativeHook::MainImageUuid(uuid, sizeof(uuid));
    const auto path = App::Get()->GetPaths()->GetLogsDir() / "rtti_layout_macos.json";
    std::ofstream file(path, std::ios::trunc);
    file << "{\"uuid\":\"" << uuid << "\",\n\"classes\":{\n" << body << "},\n\"types\":{\n" << typeBody << "}}\n";
    Log::info("Wrote the RTTI layout of {} classes and {} types to {}", classes.size, types.size(), path.string());
}
} // namespace

ESystemType StateSystem::GetType()
{
    return ESystemType::State;
}

void StateSystem::Startup()
{
    RED4ext::CRTTISystem::Get()->AddPostRegisterCallback(&RegisterSelfCheckFunctions);
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
        if (aStateType == RED4ext::EGameStateType::Running)
        {
            RunSdkSelfChecks();
            DumpRttiLayout();
        }
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
