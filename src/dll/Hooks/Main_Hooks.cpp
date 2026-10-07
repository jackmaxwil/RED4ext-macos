#include "Main_Hooks.hpp"
#include "Addresses.hpp"
#include "App.hpp"
#include "Detail/AddressHashes.hpp"
#include "Hook.hpp"
#include "stdafx.hpp"


namespace
{
bool isAttached = false;

#ifdef RED4EXT_PLATFORM_MACOS
// RED4ext is loaded through DYLD_INSERT_LIBRARIES, so its constructor runs before the game's own static
// initializers: the memory pools and singletons (RTTI system, name pool) do not exist yet. Plugins must not be
// loaded until the game's main() has been entered, exactly as on Windows.
// On macOS, Hashes::Main is the engine initializer that the game's main() calls first with argc/argv, before
// the body that creates CGameApplication. main() itself sits too close to __PAGEZERO for a near-branch patch.
void _Main(int aArgc, char** aArgv);
Hook<decltype(&_Main)> Main_fnc(Hashes::Main, &_Main);

void _Main(int aArgc, char** aArgv)
{
    try
    {
        App::Get()->Startup();
    }
    catch (const std::exception& e)
    {
        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE("An exception occurred while RED4ext was starting up.\n\n{}",
                                            Utils::Widen(e.what()));
    }
    catch (...)
    {
        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE("An unknown exception occurred while RED4ext was starting up.");
    }

    // Shutdown runs from the dylib destructor.
    Main_fnc(aArgc, aArgv);
}
#else

int WINAPI _Main(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow);
Hook<decltype(&_Main)> Main_fnc(Hashes::Main, &_Main);

int WINAPI _Main(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    try
    {
        auto app = App::Get();
        app->Startup();
    }
    catch (const std::exception& e)
    {
        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE("An exception occurred while RED4ext was starting up.\n\n{}",
                                            Utils::Widen(e.what()));
    }
    catch (...)
    {
        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE("An unknown exception occurred while RED4ext was starting up.");
    }

    auto result = Main_fnc(hInstance, hPrevInstance, pCmdLine, nCmdShow);

    try
    {
        auto app = App::Get();
        app->Shutdown();
    }
    catch (const std::exception& e)
    {
        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE("An exception occurred while RED4ext was shutting down.\n\n{}",
                                            Utils::Widen(e.what()));
    }
    catch (...)
    {
        SHOW_MESSAGE_BOX_AND_EXIT_FILE_LINE("An unknown exception occurred while RED4ext was shutting down.");
    }

    return result;
}
#endif
} // namespace

bool Hooks::Main::Attach()
{
    Log::trace("Trying to attach the hook for the main function at {:#x}...", Main_fnc.GetAddress());

    auto result = Main_fnc.Attach();
    if (result != NO_ERROR)
    {
        Log::error("Could not attach the hook for the main function. Detour error code: {}", result);
    }
    else
    {
        Log::trace("The hook for the main function was attached");
    }

    isAttached = result == NO_ERROR;
    return isAttached;
}

bool Hooks::Main::Detach()
{
    if (!isAttached)
    {
        return false;
    }

    Log::trace("Trying to detach the hook for the main function at {:#x}...", Main_fnc.GetAddress());

    auto result = Main_fnc.Detach();
    if (result != NO_ERROR)
    {
        Log::error("Could not detach the hook for the main function. Detour error code: {}", result);
    }
    else
    {
        Log::trace("The hook for the main function was detached");
    }

    isAttached = result != NO_ERROR;
    return !isAttached;
}
