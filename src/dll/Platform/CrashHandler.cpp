#include "CrashHandler.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include "App.hpp"
#include "Paths.hpp"
#include "Utils.hpp"
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <RED4ext/Detail/Memory.hpp>
#include "Utils.hpp"

// Helper function for pointer formatting (matches fmt::ptr usage in Addresses.cpp)
namespace fmt
{
template<typename T>
std::string ptr(T* p)
{
    if (!p)
    {
        return "nullptr";
    }
    return fmt::format("{:#018x}", reinterpret_cast<uintptr_t>(p));
}
}

namespace Platform
{
namespace CrashHandler
{
namespace
{
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_inCrashHandler{false};
constexpr size_t kMaxStackTraceDepth = 32;

void SignalHandler(int sig, siginfo_t* info, void* ucontext)
{
    if (g_inCrashHandler.exchange(true))
    {
        // Already handling a crash, abort immediately
        _exit(1);
    }

    const char* sigName = "UNKNOWN";
    switch (sig)
    {
    case SIGSEGV:
        sigName = "SIGSEGV";
        break;
    case SIGBUS:
        sigName = "SIGBUS";
        break;
    case SIGABRT:
        sigName = "SIGABRT";
        break;
    case SIGILL:
        sigName = "SIGILL";
        break;
    case SIGFPE:
        sigName = "SIGFPE";
        break;
    }

    std::cerr << "\n"
              << "========================================\n"
              << "RED4ext CRASH DETECTED\n"
              << "========================================\n"
              << "Signal: " << sigName << " (" << sig << ")\n"
              << "Fault Address: " << fmt::ptr(info->si_addr) << "\n"
              << "Signal Code: " << info->si_code << "\n";

    if (ucontext)
    {
        ucontext_t* ctx = static_cast<ucontext_t*>(ucontext);
        std::cerr << "PC (Program Counter): " << fmt::ptr(reinterpret_cast<void*>(ctx->uc_mcontext->__ss.__pc)) << "\n"
                  << "LR (Link Register): " << fmt::ptr(reinterpret_cast<void*>(ctx->uc_mcontext->__ss.__lr)) << "\n"
                  << "SP (Stack Pointer): " << fmt::ptr(reinterpret_cast<void*>(ctx->uc_mcontext->__ss.__sp)) << "\n"
                  << "FP (Frame Pointer): " << fmt::ptr(reinterpret_cast<void*>(ctx->uc_mcontext->__ss.__fp)) << "\n";
        
        // Log register state
        std::cerr << "\nRegister State:\n";
        for (int i = 0; i < 30; ++i)
        {
            std::cerr << "  x" << i << ": " << fmt::ptr(reinterpret_cast<void*>(ctx->uc_mcontext->__ss.__x[i])) << "\n";
        }
    }

    std::cerr << "\nStack Trace:\n";
    std::cerr << CaptureStackTrace(kMaxStackTraceDepth);

    std::cerr << "\nLoaded Images:\n";
    LogLoadedImages();

    // Try to write crash report to file
    try
    {
        GenerateCrashReport(sigName, info->si_addr);
    }
    catch (...)
    {
        // Ignore errors during crash reporting
    }

    std::cerr << "\n========================================\n";

    // Restore default handler and re-raise
    signal(sig, SIG_DFL);
    raise(sig);
}

void AbortHandler()
{
    if (g_inCrashHandler.exchange(true))
    {
        _exit(1);
    }

    std::cerr << "\n"
              << "========================================\n"
              << "RED4ext ABORT DETECTED\n"
              << "========================================\n";

    std::cerr << "\nStack Trace:\n";
    std::cerr << CaptureStackTrace(kMaxStackTraceDepth);

    std::cerr << "\nLoaded Images:\n";
    LogLoadedImages();

    try
    {
        GenerateCrashReport("ABORT", nullptr);
    }
    catch (...)
    {
    }

    std::cerr << "\n========================================\n";
}
}

void Initialize()
{
    if (g_initialized.exchange(true))
    {
        return;
    }

    // Install signal handlers
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = SignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);

    // Install abort handler
    std::atexit(AbortHandler);
}

std::string CaptureStackTrace(uint32_t maxDepth)
{
    void* buffer[kMaxStackTraceDepth];
    int count = backtrace(buffer, std::min(maxDepth, static_cast<uint32_t>(kMaxStackTraceDepth)));

    std::ostringstream oss;
    char** symbols = backtrace_symbols(buffer, count);

    if (symbols)
    {
        for (int i = 0; i < count; ++i)
        {
            oss << "  [" << std::setw(2) << i << "] " << symbols[i] << "\n";
        }
        std::free(symbols);
    }
    else
    {
        for (int i = 0; i < count; ++i)
        {
            oss << "  [" << std::setw(2) << i << "] " << fmt::ptr(buffer[i]) << "\n";
        }
    }

    return oss.str();
}

bool IsValidAddress(void* aAddress, size_t aSize)
{
    if (!aAddress)
    {
        return false;
    }

    mach_port_t task = mach_task_self();
    vm_address_t address = reinterpret_cast<vm_address_t>(aAddress);
    vm_size_t size = static_cast<vm_size_t>(aSize);

    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t infoCount = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t objectName;
    vm_address_t regionAddress = address;
    vm_size_t regionSize = size;

    kern_return_t kr = vm_region_64(task, &regionAddress, &regionSize, VM_REGION_BASIC_INFO_64,
                                     reinterpret_cast<vm_region_info_t>(&info), &infoCount, &objectName);

    if (kr != KERN_SUCCESS)
    {
        return false;
    }

    // Check if the address is within the region
    return (address >= regionAddress && (address + size) <= (regionAddress + regionSize));
}

bool GetMemoryRegionInfo(void* aAddress, MemoryRegionInfo& outInfo)
{
    if (!aAddress)
    {
        return false;
    }

    mach_port_t task = mach_task_self();
    vm_address_t address = reinterpret_cast<vm_address_t>(aAddress);

    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t infoCount = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t objectName;
    vm_address_t regionAddress = address;
    vm_size_t regionSize = 0;

    kern_return_t kr = vm_region_64(task, &regionAddress, &regionSize, VM_REGION_BASIC_INFO_64,
                                     reinterpret_cast<vm_region_info_t>(&info), &infoCount, &objectName);

    if (kr != KERN_SUCCESS)
    {
        return false;
    }

    outInfo.start = reinterpret_cast<void*>(regionAddress);
    outInfo.size = static_cast<size_t>(regionSize);
    outInfo.protection = info.protection;

    // Try to get segment name from dyld
    const uint32_t imageCount = _dyld_image_count();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const struct mach_header_64* header =
            reinterpret_cast<const struct mach_header_64*>(_dyld_get_image_header(i));
        if (!header)
        {
            continue;
        }

        const char* imageName = _dyld_get_image_name(i);
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        uintptr_t imageBase = reinterpret_cast<uintptr_t>(header) - slide;

        if (address >= imageBase && address < imageBase + 0x10000000) // Rough check
        {
            outInfo.name = imageName ? imageName : "<unknown>";
            break;
        }
    }

    return true;
}

void LogLoadedImages()
{
    const uint32_t imageCount = _dyld_image_count();
    std::cerr << "Total loaded images: " << imageCount << "\n";

    for (uint32_t i = 0; i < std::min(imageCount, 20u); ++i) // Limit to first 20
    {
        const char* imageName = _dyld_get_image_name(i);
        const struct mach_header_64* header =
            reinterpret_cast<const struct mach_header_64*>(_dyld_get_image_header(i));
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);

        if (header)
        {
            std::cerr << "  [" << std::setw(2) << i << "] " << fmt::ptr(reinterpret_cast<const void*>(header))
                      << " slide=" << std::hex << slide << std::dec << " " << (imageName ? imageName : "<null>")
                      << "\n";
        }
    }

    if (imageCount > 20)
    {
        std::cerr << "  ... (" << (imageCount - 20) << " more images)\n";
    }
}

void GenerateCrashReport(const char* aReason, void* aFaultAddress)
{
    try
    {
        auto* app = App::Get();
        if (!app)
        {
            return;
        }

        const auto* paths = app->GetPaths();
        if (!paths)
        {
            return;
        }
        const auto crashLogPath = paths->GetLogsDir() / L"crash_report.txt";

        std::ofstream file(crashLogPath, std::ios::app);
        if (!file.is_open())
        {
            return;
        }

        auto now = std::time(nullptr);
        file << "\n"
             << "========================================\n"
             << "RED4ext Crash Report\n"
             << "========================================\n"
             << "Timestamp: " << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "\n"
             << "Reason: " << (aReason ? aReason : "UNKNOWN") << "\n"
             << "Fault Address: " << fmt::ptr(aFaultAddress) << "\n"
             << "\n";

        if (aFaultAddress)
        {
            MemoryRegionInfo regionInfo;
            if (GetMemoryRegionInfo(aFaultAddress, regionInfo))
            {
                file << "Memory Region:\n"
                     << "  Start: " << fmt::ptr(regionInfo.start) << "\n"
                     << "  Size: " << regionInfo.size << " bytes\n"
                     << "  Protection: " << std::hex << regionInfo.protection << std::dec << "\n"
                     << "  Name: " << regionInfo.name << "\n"
                     << "\n";
            }
            else
            {
                file << "Memory Region: INVALID ADDRESS\n\n";
            }
        }

        file << "Stack Trace:\n" << CaptureStackTrace(kMaxStackTraceDepth) << "\n";

        file << "Loaded Images:\n";
        const uint32_t imageCount = _dyld_image_count();
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            const char* imageName = _dyld_get_image_name(i);
            const struct mach_header_64* header =
                reinterpret_cast<const struct mach_header_64*>(_dyld_get_image_header(i));
            intptr_t slide = _dyld_get_image_vmaddr_slide(i);

            if (header)
            {
                file << "  [" << std::setw(2) << i << "] " << fmt::ptr(reinterpret_cast<const void*>(header))
                     << " slide=" << std::hex << slide << std::dec << " " << (imageName ? imageName : "<null>")
                     << "\n";
            }
        }

        file << "========================================\n\n";
        file.flush();
    }
    catch (...)
    {
        // Ignore errors
    }
}
}
}
#endif
