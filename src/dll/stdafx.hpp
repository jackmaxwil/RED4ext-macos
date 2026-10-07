#pragma once

#include <cstdarg>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <ranges>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>

#ifndef RED4EXT_PLATFORM_MACOS
#include <Windows.h>
#include <detours.h>
#include <tlhelp32.h>

#include <wil/resource.h>
#include <wil/stl.h>
#include <wil/win32_helpers.h>
#else
// TEXT macro for wide strings - on macOS, we use narrow strings internally
// since wchar_t is 32-bit on macOS vs 16-bit on Windows
#define TEXT(x) x
#define __TEXT(x) L##x
#define MB_OK 0x00000000L
#define MB_ICONWARNING 0x00000030L
#define MB_ICONERROR 0x00000010L
#define WINAPI
#include <dlfcn.h>
#include <memory>

// Windows type compatibility for macOS
using HINSTANCE = void*;
using PWSTR = wchar_t*;
using LPWSTR = wchar_t*;
using LPCWSTR = const wchar_t*;

// macOS-compatible unique_hmodule (replaces wil::unique_hmodule)
namespace wil
{
struct unique_hmodule
{
    unique_hmodule()
        : handle_(nullptr)
    {
    }
    explicit unique_hmodule(void* handle)
        : handle_(handle)
    {
    }
    unique_hmodule(unique_hmodule&& other) noexcept
        : handle_(other.release())
    {
    }
    ~unique_hmodule()
    {
        reset();
    }

    unique_hmodule& operator=(unique_hmodule&& other) noexcept
    {
        reset(other.release());
        return *this;
    }

    // Plugins are never dlclose'd on macOS: a plugin's hooks can outlive its unload (they are not detached at exit,
    // and a refused plugin may have attached some), so unmapping its code would leave live jumps into nothing.
    void reset(void* handle = nullptr)
    {
        handle_ = handle;
    }

    void* release()
    {
        void* h = handle_;
        handle_ = nullptr;
        return h;
    }

    void* get() const
    {
        return handle_;
    }
    explicit operator bool() const
    {
        return handle_ != nullptr;
    }

private:
    void* handle_;

    unique_hmodule(const unique_hmodule&) = delete;
    unique_hmodule& operator=(const unique_hmodule&) = delete;
};
} // namespace wil
#endif

#ifdef RED4EXT_PLATFORM_MACOS
#include "Platform/Hooking.hpp"
#endif

// Include simdjson BEFORE fmt to avoid name conflicts with 'formatter' template parameter
#include <simdjson.h>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/xchar.h>

#ifndef RED4EXT_PLATFORM_MACOS
// simdjson already included above
#else
#define RED4EXT_UNUSED_PARAMETER(x) (void)x
#endif

#include "Log.hpp"
#include <spdlog/spdlog.h>
#include <toml.hpp>
#include <tsl/ordered_map.h>

#include <RED4ext/RED4ext.hpp>
