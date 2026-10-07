#include "Platform.hpp"

#ifdef RED4EXT_PLATFORM_MACOS

#include <CoreGraphics/CoreGraphics.h>
#include <fishhook.h>

#include <cstdlib>

// RED4EXT_BACKGROUND=1 (set by tools/cp-run for unattended test runs): the game must not take the user's mouse or
// focus. Its calls that capture the cursor (detach it from the mouse, warp it to the window centre) become no-ops,
// and so do NSApplication's activation requests, so the window stays behind whatever the user is working in.
// The test driver still has to press keys (the loading screens' "press to continue"), and sends them to the process
// only (CGEventPostToPid). -[GameView keyDown:] ignores keys unless -[GameWindowController m_windowFocus], and an
// inactive app does not deliver key events to its windows, so here the game is always "focused" and every key event
// goes straight to the game's view.
// The Objective-C runtime's C API, declared here: <objc/runtime.h> clashes with the precompiled header's BOOL.
extern "C"
{
    void* objc_getClass(const char* aName);
    void* sel_registerName(const char* aName);
    void* class_getInstanceMethod(void* aClass, void* aSelector);
    void* method_setImplementation(void* aMethod, void* aImp);
    void objc_msgSend();
}

namespace
{
CGError NoAssociateMouse(boolean_t)
{
    return kCGErrorSuccess;
}

CGError NoWarpCursor(CGPoint)
{
    return kCGErrorSuccess;
}

void NoActivate(void*, void*)
{
    spdlog::info("Background mode: blocked -[NSApplication activate]");
}

void NoActivateFlag(void*, void*, signed char)
{
    spdlog::info("Background mode: blocked -[NSApplication activateIgnoringOtherApps:]");
}

template<typename R, typename... Args>
R Send(void* aObject, const char* aSelector, Args... aArgs)
{
    return reinterpret_cast<R (*)(void*, void*, Args...)>(&objc_msgSend)(aObject, sel_registerName(aSelector),
                                                                         aArgs...);
}

// Returns the replaced implementation.
void* Replace(const char* aClass, const char* aSelector, void* aImp)
{
    auto cls = objc_getClass(aClass);
    auto method = cls ? class_getInstanceMethod(cls, sel_registerName(aSelector)) : nullptr;
    return method ? method_setImplementation(method, aImp) : nullptr;
}

void IgnoreNotification(void*, void*, void*)
{
}

signed char AlwaysFocused(void*, void*)
{
    return 1;
}

const char* ClassName(void* aObject)
{
    auto name = Send<void*>(Send<void*>(aObject, "className"), "UTF8String");
    return name ? static_cast<const char*>(name) : "?";
}

// The game's window is a GameWindow, its content view the GameView that handles keys.
void* GameView(void* aApp)
{
    auto windows = Send<void*>(aApp, "windows");
    auto count = Send<unsigned long>(windows, "count");
    for (unsigned long i = 0; i < count; ++i)
    {
        auto window = Send<void*>(windows, "objectAtIndex:", i);
        auto view = Send<void*>(window, "contentView");
        if (view && Send<signed char>(view, "isKindOfClass:", objc_getClass("GameView")))
        {
            return view;
        }
    }
    return nullptr;
}

constexpr unsigned long NSEventTypeKeyDown = 10;
constexpr unsigned long NSEventTypeKeyUp = 11;

using SendEventFn = void (*)(void*, void*, void*);
SendEventFn g_sendEvent = nullptr;

void SendEventToGameView(void* aApp, void* aSelector, void* aEvent)
{
    auto type = Send<unsigned long>(aEvent, "type");
    if (type == NSEventTypeKeyDown || type == NSEventTypeKeyUp)
    {
        auto view = GameView(aApp);
        if (view)
        {
            static bool focused = false;
            if (!focused)
            {
                // The engine takes gameplay input only while it has focus, which it gets from the window becoming
                // key (never, in the background): send the game's own become-key handlers once. They notify the
                // engine, its window-focus listeners and the view.
                focused = true;
                auto window = Send<void*>(view, "window");
                auto delegate = window ? Send<void*>(window, "delegate") : nullptr;
                auto controller = Send<void*>(view, "m_windowController");
                if (delegate)
                {
                    Send<void>(delegate, "windowDidBecomeKey:", static_cast<void*>(nullptr));
                }
                if (controller)
                {
                    Send<void>(controller, "windowDidBecomeKey:", static_cast<void*>(nullptr));
                }
                spdlog::info("Background mode: gave the game focus (window delegate {}, controller {})",
                             delegate ? ClassName(delegate) : "none", controller ? ClassName(controller) : "none");
            }
            Send<void>(view, type == NSEventTypeKeyDown ? "keyDown:" : "keyUp:", aEvent);
            return;
        }
    }
    g_sendEvent(aApp, aSelector, aEvent);
}
} // namespace

void EnableBackgroundModeIfRequested()
{
    const char* env = std::getenv("RED4EXT_BACKGROUND");
    if (!env || env[0] != '1')
    {
        return;
    }

    rebinding rebinds[] = {
        {"CGAssociateMouseAndMouseCursorPosition", reinterpret_cast<void*>(&NoAssociateMouse), nullptr},
        {"CGWarpMouseCursorPosition", reinterpret_cast<void*>(&NoWarpCursor), nullptr},
    };
    rebind_symbols(rebinds, 2);

    Replace("NSApplication", "activate", reinterpret_cast<void*>(&NoActivate));
    Replace("NSApplication", "activateIgnoringOtherApps:", reinterpret_cast<void*>(&NoActivateFlag));
    Replace("GameWindowController", "m_windowFocus", reinterpret_cast<void*>(&AlwaysFocused));
    Replace("NSApplication", "isActive", reinterpret_cast<void*>(&AlwaysFocused));
    // Keep that focus: the user clicking the game window and then away must not take it back.
    Replace("GameWindowController", "windowDidResignKey:", reinterpret_cast<void*>(&IgnoreNotification));
    Replace("GameWindowDelegate", "windowDidResignKey:", reinterpret_cast<void*>(&IgnoreNotification));
    // Same for the app losing focus (e.g. a click into the window and back out): the game's handler tells the engine
    // focus was lost, after which the in-game test steps stopped running.
    Replace("GameApplicationDelegate", "appDidLoseFocus:", reinterpret_cast<void*>(&IgnoreNotification));
    g_sendEvent = reinterpret_cast<SendEventFn>(
        Replace("NSApplication", "sendEvent:", reinterpret_cast<void*>(&SendEventToGameView)));
}

#endif
