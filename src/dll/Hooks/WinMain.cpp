#include "WinMain.hpp"
#include "Addresses.hpp"
#include "App.hpp"
#include "Detail/AddressHashes.hpp"
#include "Hook.hpp"

#include <cstdlib>

namespace
{
bool isAttached = false;

#ifdef RED4EXT_PLATFORM_MACOS
// RED4ext is loaded through DYLD_INSERT_LIBRARIES, so its constructor runs before the game's own static
// initializers: the memory pools and singletons (RTTI system, name pool) do not exist yet. Plugins must not be
// loaded until the game's main() has been entered, exactly as on Windows.
// On macOS, Hashes::WinMain is the engine initializer that the game's main() calls first with argc/argv, before
// the body that creates CGameApplication. main() itself sits too close to __PAGEZERO for a near-branch patch.
void WinMain_fnc(int aArgc, char** aArgv);
Hook<decltype(&WinMain_fnc)> _WinMain(Hashes::WinMain, &WinMain_fnc);

void WinMain_fnc(int aArgc, char** aArgv)
{
    try
    {
        App::Get()->Startup();

        // Shut down before C++ static destructors run (the dylib destructor fires after them, when the systems'
        // mutexes are already gone). atexit handlers registered after static init run first, in reverse order.
        std::atexit(
            []()
            {
                if (auto app = App::Get())
                {
                    app->Shutdown();
                    App::Destruct();
                }
            });
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

    // Shutdown runs from the atexit handler above.
    _WinMain(aArgc, aArgv);
}
#else
int WINAPI WinMain_fnc(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow);
Hook<decltype(&WinMain_fnc)> _WinMain(Hashes::WinMain, &WinMain_fnc);

int WINAPI WinMain_fnc(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
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

    return _WinMain(hInstance, hPrevInstance, pCmdLine, nCmdShow);
}
#endif
} // namespace

bool Hooks::WinMain::Attach()
{
    Log::trace("Trying to attach the hook for the WinMain function at {:#x}...", _WinMain.GetAddress());

    auto result = _WinMain.Attach();
    if (result != NO_ERROR)
    {
        Log::error("Could not attach the hook for the WinMain function. Detour error code: {}", result);
    }
    else
    {
        Log::trace("The hook for the WinMain function was attached");
    }

    isAttached = result == NO_ERROR;
    return isAttached;
}

bool Hooks::WinMain::Detach()
{
    if (!isAttached)
    {
        return false;
    }

    Log::trace("Trying to detach the hook for the WinMain function at {:#x}...", _WinMain.GetAddress());

    auto result = _WinMain.Detach();
    if (result != NO_ERROR)
    {
        Log::error("Could not detach the hook for the WinMain function. Detour error code: {}", result);
    }
    else
    {
        Log::trace("The hook for the WinMain function was detached");
    }

    isAttached = result != NO_ERROR;
    return !isAttached;
}
