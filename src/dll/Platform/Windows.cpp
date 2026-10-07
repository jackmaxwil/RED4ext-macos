#include "Platform.hpp"

#ifndef RED4EXT_PLATFORM_MACOS

namespace Platform
{
Handle GetModuleHandle(const wchar_t* aName)
{
    return ::GetModuleHandle(aName);
}

void* GetProcAddress(Handle aHandle, const char* aName)
{
    return reinterpret_cast<void*>(::GetProcAddress(aHandle, aName));
}

bool ProtectMemory(void* aAddress, size_t aSize, uint32_t aNewProtection, uint32_t* aOldProtection)
{
    return ::VirtualProtect(aAddress, aSize, aNewProtection, reinterpret_cast<PDWORD>(aOldProtection));
}

std::filesystem::path GetModuleFileName(Handle aHandle)
{
    wchar_t buffer[MAX_PATH];
    if (::GetModuleFileName(aHandle, buffer, MAX_PATH))
    {
        return std::filesystem::path(buffer);
    }
    return {};
}

void ShowMessageBox(const wchar_t* aCaption, const wchar_t* aText, uint32_t aType)
{
    ::MessageBox(nullptr, aText, aCaption, aType);
}

uint32_t GetLastError()
{
    return ::GetLastError();
}

bool IsDebuggerPresent()
{
    return ::IsDebuggerPresent();
}

void TerminateProcess()
{
    ::TerminateProcess(::GetCurrentProcess(), 1);
}
} // namespace Platform

#endif
