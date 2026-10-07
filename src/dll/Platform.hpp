#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <dlfcn.h>
#include <sys/mman.h>
#else
#include <Windows.h>
#endif

namespace Platform
{
#ifdef RED4EXT_PLATFORM_MACOS
    using Handle = void*;
    
    constexpr uint32_t Memory_NoAccess = PROT_NONE;
    constexpr uint32_t Memory_Read = PROT_READ;
    constexpr uint32_t Memory_ReadWrite = PROT_READ | PROT_WRITE;
    constexpr uint32_t Memory_Execute = PROT_EXEC;
    constexpr uint32_t Memory_ExecuteRead = PROT_READ | PROT_EXEC;
#else
    using Handle = HMODULE;
    
    constexpr uint32_t Memory_NoAccess = PAGE_NOACCESS;
    constexpr uint32_t Memory_Read = PAGE_READONLY;
    constexpr uint32_t Memory_ReadWrite = PAGE_READWRITE;
    constexpr uint32_t Memory_Execute = PAGE_EXECUTE;
    constexpr uint32_t Memory_ExecuteRead = PAGE_EXECUTE_READ;
    constexpr uint32_t Memory_ExecuteReadWrite = PAGE_EXECUTE_READWRITE;
#endif

    Handle GetModuleHandle(const wchar_t* aName = nullptr);
    void* GetProcAddress(Handle aHandle, const char* aName);
    
    bool ProtectMemory(void* aAddress, size_t aSize, uint32_t aNewProtection, uint32_t* aOldProtection);
    
    std::filesystem::path GetModuleFileName(Handle aHandle = nullptr);
    
    void ShowMessageBox(const wchar_t* aCaption, const wchar_t* aText, uint32_t aType);
    
    uint32_t GetLastError();

    bool IsDebuggerPresent();

    [[noreturn]] void TerminateProcess();
}
