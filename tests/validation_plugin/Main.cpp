// Common.hpp first: the API headers need its macros.
#include <RED4ext/Common.hpp>

#include <RED4ext/Api/ApiVersion.hpp>
#include <RED4ext/Api/v1/EMainReason.hpp>
#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <RED4ext/Api/v1/PluginInfo.hpp>
#include <RED4ext/Api/v1/Runtime.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>
#include <RED4ext/Api/v1/Version.hpp>
#include <RED4ext/Detail/AddressHashes.hpp>
#include <RED4ext/Relocation.hpp>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <unistd.h>

#include "AddressHashList.hpp"

namespace
{
constexpr std::array<std::string_view, 12> kDataHashes = {
    "CGameEngine",
    "CStack_vtbl",
    "CBaseFunction_Handlers",
    "g_DeviceData",
    "ISerializable_Counter",
    "OpcodeHandlers",
    "ResourceDepot",
    "ResourceLoader",
    "JobDispatcher",
    "Memory_Vault",
    "CRTTIRegistrator_RTTIAsyncId",
    "LaunchParameters",
};

constexpr std::array<std::string_view, 2> kStubHashes = {
    "CBaseRTTIType_sub_98",
    "CBaseRTTIType_sub_A0",
};

constexpr std::array<std::string_view, 23> kZeroAllowedHashes = {
    "Allocator_CreateResource",
    "g_DeviceData",
    "IRenderProxy_sub_00",
    "IRenderProxy_sub_08",
    "IRenderProxy_sub_18",
    "IRenderProxy_sub_58",
    "IRenderProxy_sub_60",
    "IRenderProxy_sub_78",
    "IRenderProxy_sub_80",
    "IRenderProxy_sub_88",
    "IRenderProxy_sub_90",
    "IRenderProxy_sub_98",
    "CClass_sub_80",
    "CClass_sub_88",
    "CClass_sub_90",
    "CClass_sub_98",
    "CClass_sub_A0",
    "CClass_sub_B0",
    "CClass_sub_C0",
    "CClass_GetMaxAlignment",
    "CClass_sub_D0",
    "CClass_InitializeProperties",
    "CClass_AssignDefaultValuesToProperties",
};

bool IsInList(std::string_view aName, const auto& aList)
{
    for (auto name : aList)
    {
        if (name == aName)
        {
            return true;
        }
    }
    return false;
}

std::filesystem::path GetLogPath()
{
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0')
    {
        return "/tmp/red4ext_address_validator.log";
    }

    return std::filesystem::path(home) / "Library" / "Application Support" / "Steam" / "steamapps" / "common" /
           "Cyberpunk 2077" / "red4ext" / "plugins" / "address_validator" / "validation_results.log";
}

std::shared_ptr<spdlog::logger> GetLogger()
{
    static std::shared_ptr<spdlog::logger> s_logger;
    if (s_logger)
    {
        return s_logger;
    }

    try
    {
        auto logPath = GetLogPath();
        std::filesystem::create_directories(logPath.parent_path());
        s_logger = spdlog::basic_logger_mt("address_validator", logPath.string(), true);
        s_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        s_logger->set_level(spdlog::level::info);
        s_logger->flush_on(spdlog::level::info);
    }
    catch (const std::exception&)
    {
        return nullptr;
    }

    return s_logger;
}

bool ReadU32(uintptr_t aAddress, uint32_t& aOut)
{
    vm_size_t readSize = 0;
    const auto result = vm_read_overwrite(mach_task_self(), static_cast<vm_address_t>(aAddress), sizeof(uint32_t),
                                          reinterpret_cast<vm_address_t>(&aOut), &readSize);
    return result == KERN_SUCCESS && readSize == sizeof(uint32_t);
}

bool ReadU64(uintptr_t aAddress, uint64_t& aOut)
{
    vm_size_t readSize = 0;
    const auto result = vm_read_overwrite(mach_task_self(), static_cast<vm_address_t>(aAddress), sizeof(uint64_t),
                                          reinterpret_cast<vm_address_t>(&aOut), &readSize);
    return result == KERN_SUCCESS && readSize == sizeof(uint64_t);
}

bool IsPacibsp(uint32_t aInstr)
{
    return aInstr == 0xD503237F;
}

bool IsStpFpLr(uint32_t aInstr)
{
    return (aInstr & 0xFF80FFFFu) == 0xA9007BFDu;
}

bool IsStpX28X27(uint32_t aInstr)
{
    return (aInstr & 0xFF80FFFFu) == 0xA9006FFCu;
}

bool IsSubSp(uint32_t aInstr)
{
    return (aInstr & 0xFF0003FFu) == 0xD10003FFu;
}

bool IsAddImm(uint32_t aInstr)
{
    if ((aInstr & 0xFF000000u) != 0x91000000u)
    {
        return false;
    }

    const uint32_t rn = (aInstr >> 5) & 0x1Fu;
    const uint32_t rd = aInstr & 0x1Fu;
    return rn == 31u || rn == rd;
}

bool IsLdrImm64(uint32_t aInstr)
{
    if ((aInstr & 0xFFC00000u) != 0xF9400000u)
    {
        return false;
    }

    const uint32_t rn = (aInstr >> 5) & 0x1Fu;
    const uint32_t rd = aInstr & 0x1Fu;
    return rn == rd;
}

bool IsGenericStp(uint32_t aInstr)
{
    return (aInstr & 0xFF000000u) == 0xA9000000u;
}

bool IsValidPrologue(uint32_t aInstr)
{
    return IsPacibsp(aInstr) || IsStpFpLr(aInstr) || IsStpX28X27(aInstr) || IsSubSp(aInstr) || IsAddImm(aInstr) ||
           IsLdrImm64(aInstr) || IsGenericStp(aInstr);
}

struct SegmentInfo
{
    std::uintptr_t vmaddr{0};
    std::uintptr_t vmsize{0};
};

uint32_t GetMainExecutableImageIndex()
{
    uint32_t exeSize = 0;
    _NSGetExecutablePath(nullptr, &exeSize);
    if (exeSize == 0 || exeSize > 4096)
    {
        return 0;
    }

    std::string exeBuf;
    exeBuf.resize(exeSize);
    if (_NSGetExecutablePath(exeBuf.data(), &exeSize) != 0)
    {
        return 0;
    }

    const auto exeFile = std::filesystem::path(exeBuf.c_str()).filename();
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i)
    {
        const char* name = _dyld_get_image_name(i);
        if (!name || !*name)
            continue;
        if (std::filesystem::path(name).filename() == exeFile)
            return i;
    }

    return 0;
}

bool GetSegmentInfo(const char* segmentName, SegmentInfo& out)
{
    const auto imgIdx = GetMainExecutableImageIndex();
    const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(imgIdx));
    if (!header || header->magic != MH_MAGIC_64)
    {
        return false;
    }

    const auto slide = static_cast<std::intptr_t>(_dyld_get_image_vmaddr_slide(imgIdx));
    const auto* cmd = reinterpret_cast<const std::uint8_t*>(header) + sizeof(mach_header_64);
    for (std::uint32_t i = 0; i < header->ncmds; ++i)
    {
        const auto* lc = reinterpret_cast<const load_command*>(cmd);
        if (!lc || lc->cmdsize == 0)
            break;
        if (lc->cmd == LC_SEGMENT_64 && lc->cmdsize >= sizeof(segment_command_64))
        {
            const auto* seg = reinterpret_cast<const segment_command_64*>(cmd);
            std::size_t nameLen = 0;
            while (nameLen < sizeof(seg->segname) && seg->segname[nameLen] != '\0')
                ++nameLen;
            const std::string_view segName(seg->segname, nameLen);
            if (segName == segmentName)
            {
                out.vmaddr = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(seg->vmaddr) + slide);
                out.vmsize = static_cast<std::uintptr_t>(seg->vmsize);
                return true;
            }
        }
        cmd += lc->cmdsize;
    }

    return false;
}

void LogCClassVtableFromMemory()
{
    auto logger = GetLogger();
    if (!logger)
    {
        return;
    }

    SegmentInfo dataConst{};
    if (!GetSegmentInfo("__DATA_CONST", dataConst) || dataConst.vmaddr == 0 || dataConst.vmsize == 0)
    {
        logger->error("[address_validator] CClass vtable probe: __DATA_CONST segment not found");
        return;
    }

    const auto unserialize = RED4ext::UniversalRelocBase::Resolve(RED4ext::Detail::AddressHashes::CClass_Unserialize);
    const auto toString = RED4ext::UniversalRelocBase::Resolve(RED4ext::Detail::AddressHashes::CClass_ToString);
    if (unserialize == 0 || toString == 0)
    {
        logger->error("[address_validator] CClass vtable probe: missing CClass_Unserialize/ToString");
        return;
    }

    constexpr std::size_t kChunkSize = 1 << 20;
    const auto scanStart = dataConst.vmaddr;
    const auto scanEnd = dataConst.vmaddr + dataConst.vmsize;

    logger->info("[address_validator] CClass vtable scan: __DATA_CONST=0x{:016X} size=0x{:X}",
                 static_cast<std::uint64_t>(scanStart), static_cast<std::uint64_t>(dataConst.vmsize));

    std::vector<std::uint8_t> buffer;
    buffer.resize(kChunkSize);

    bool found = false;
    std::uintptr_t vtblBase = 0;

    for (std::uintptr_t addr = scanStart; addr < scanEnd; addr += kChunkSize)
    {
        const auto remaining = scanEnd - addr;
        const auto toRead = static_cast<vm_size_t>(remaining < kChunkSize ? remaining : kChunkSize);
        vm_size_t readSize = 0;
        const auto result = vm_read_overwrite(mach_task_self(), static_cast<vm_address_t>(addr), toRead,
                                              reinterpret_cast<vm_address_t>(buffer.data()), &readSize);
        if (result != KERN_SUCCESS || readSize == 0)
        {
            continue;
        }

        for (std::size_t i = 0; i + 16 <= readSize; i += 8)
        {
            auto* ptr = reinterpret_cast<const std::uint64_t*>(buffer.data() + i);
            if (ptr[0] == unserialize && ptr[1] == toString)
            {
                const auto unserializeAddr = addr + i;
                if (unserializeAddr < 0x60)
                    continue;
                vtblBase = unserializeAddr - 0x60; // CClass::Unserialize is at vtable offset 0x60
                found = true;
                break;
            }
        }

        if (found)
            break;
    }

    if (!found || vtblBase == 0)
    {
        logger->error("[address_validator] CClass vtable scan: pattern not found");
        return;
    }

    logger->info("[address_validator] CClass vtable scan: vtbl=0x{:016X}", static_cast<std::uint64_t>(vtblBase));

    auto logSlot = [&](const char* label, std::size_t offset)
    {
        uint64_t ptr = 0;
        if (!ReadU64(vtblBase + offset, ptr))
        {
            logger->error("[address_validator] CClass_vtbl_mem {} offset=0x{:X} READ_FAIL", label, offset);
            return;
        }
        logger->info("[address_validator] CClass_vtbl_mem {} offset=0x{:X} ptr=0x{:016X}", label, offset, ptr);
    };

    logSlot("sub_80", 0x80);
    logSlot("sub_88", 0x88);
    logSlot("sub_90", 0x90);
    logSlot("sub_98", 0x98);
    logSlot("sub_A0", 0xA0);
    logSlot("sub_B0", 0xB0);
    logSlot("sub_C0", 0xC0);
    logSlot("GetMaxAlignment", 0xC8);
    logSlot("sub_D0", 0xD0);
    logSlot("InitializeProperties", 0xD8);
    logSlot("AssignDefaultValuesToProperties", 0xE0);
}

void LogIRenderProxyVtableFromMemory()
{
    auto logger = GetLogger();
    if (!logger)
    {
        return;
    }

    const auto subA8 = RED4ext::UniversalRelocBase::Resolve(RED4ext::Detail::AddressHashes::IRenderProxy_sub_A8);
    const auto subB0 = RED4ext::UniversalRelocBase::Resolve(RED4ext::Detail::AddressHashes::IRenderProxy_sub_B0);
    if (subA8 == 0 || subB0 == 0)
    {
        logger->error("[address_validator] IRenderProxy vtable scan: missing sub_A8/sub_B0");
        return;
    }

    auto scanSegment = [&](const char* segName) -> std::uintptr_t
    {
        SegmentInfo seg{};
        if (!GetSegmentInfo(segName, seg) || seg.vmaddr == 0 || seg.vmsize == 0)
        {
            return 0;
        }

        std::vector<std::uint8_t> buffer;
        buffer.resize(static_cast<std::size_t>(seg.vmsize));

        vm_size_t readSize = 0;
        const auto result = vm_read_overwrite(mach_task_self(), static_cast<vm_address_t>(seg.vmaddr),
                                              static_cast<vm_size_t>(seg.vmsize),
                                              reinterpret_cast<vm_address_t>(buffer.data()), &readSize);
        if (result != KERN_SUCCESS || readSize == 0)
        {
            return 0;
        }

        const std::size_t maxOffset = (readSize >= 0xB0 + sizeof(uint64_t)) ? readSize - (0xB0 + sizeof(uint64_t)) : 0;
        for (std::size_t i = 0; i <= maxOffset; i += sizeof(uint64_t))
        {
            const auto* ptr = reinterpret_cast<const std::uint64_t*>(buffer.data() + i);
            if (ptr[0xA8 / 8] == subA8 && ptr[0xB0 / 8] == subB0)
            {
                return seg.vmaddr + i;
            }
        }

        return 0;
    };

    std::uintptr_t vtblBase = scanSegment("__DATA_CONST");
    if (vtblBase == 0)
    {
        vtblBase = scanSegment("__DATA");
    }

    if (vtblBase == 0)
    {
        logger->error("[address_validator] IRenderProxy vtable scan: pattern not found");
        return;
    }

    logger->info("[address_validator] IRenderProxy vtable scan: vtbl=0x{:016X}", static_cast<std::uint64_t>(vtblBase));

    auto logSlot = [&](const char* label, std::size_t offset)
    {
        uint64_t ptr = 0;
        if (!ReadU64(vtblBase + offset, ptr))
        {
            logger->error("[address_validator] IRenderProxy_vtbl_mem {} offset=0x{:X} READ_FAIL", label, offset);
            return;
        }
        logger->info("[address_validator] IRenderProxy_vtbl_mem {} offset=0x{:X} ptr=0x{:016X}", label, offset, ptr);
    };

    logSlot("sub_00", 0x00);
    logSlot("sub_08", 0x08);
    logSlot("sub_18", 0x18);
    logSlot("sub_58", 0x58);
    logSlot("sub_60", 0x60);
    logSlot("sub_78", 0x78);
    logSlot("sub_80", 0x80);
    logSlot("sub_88", 0x88);
    logSlot("sub_90", 0x90);
    logSlot("sub_98", 0x98);
}

void RunValidation()
{
    auto logger = GetLogger();
    if (!logger)
    {
        return;
    }

    logger->info("[address_validator] Starting address validation: total={}", AddressValidation::kAddressHashes.size());

    size_t passCount = 0;
    size_t failCount = 0;
    size_t dataCount = 0;
    size_t stubCount = 0;
    size_t zeroSkipCount = 0;
    size_t readFailCount = 0;

    for (const auto& entry : AddressValidation::kAddressHashes)
    {
        const auto addr = RED4ext::UniversalRelocBase::Resolve(entry.hash);
        if (addr == 0)
        {
            if (IsInList(entry.name, kZeroAllowedHashes))
            {
                logger->warn("[address_validator] SKIP_ZERO name={} hash=0x{:08X}", entry.name, entry.hash);
                ++zeroSkipCount;
            }
            else
            {
                logger->error("[address_validator] FAIL_NULL name={} hash=0x{:08X}", entry.name, entry.hash);
                ++failCount;
            }
            continue;
        }

        uint32_t instr = 0;
        if (!ReadU32(addr, instr))
        {
            logger->error("[address_validator] FAIL_READ name={} hash=0x{:08X} addr=0x{:016X}", entry.name, entry.hash,
                          static_cast<uint64_t>(addr));
            ++failCount;
            ++readFailCount;
            continue;
        }

        if (IsInList(entry.name, kStubHashes))
        {
            logger->info("[address_validator] STUB name={} hash=0x{:08X} addr=0x{:016X} instr=0x{:08X}", entry.name,
                         entry.hash, static_cast<uint64_t>(addr), instr);
            ++stubCount;
            continue;
        }

        if (IsInList(entry.name, kDataHashes))
        {
            logger->info("[address_validator] DATA name={} hash=0x{:08X} addr=0x{:016X} word=0x{:08X}", entry.name,
                         entry.hash, static_cast<uint64_t>(addr), instr);
            ++dataCount;
            continue;
        }

        if (IsValidPrologue(instr))
        {
            logger->info("[address_validator] PASS name={} hash=0x{:08X} addr=0x{:016X} instr=0x{:08X}", entry.name,
                         entry.hash, static_cast<uint64_t>(addr), instr);
            ++passCount;
        }
        else
        {
            logger->error("[address_validator] FAIL_PROLOGUE name={} hash=0x{:08X} addr=0x{:016X} instr=0x{:08X}",
                          entry.name, entry.hash, static_cast<uint64_t>(addr), instr);
            ++failCount;
        }
    }

    logger->info("[address_validator] Summary: total={} pass={} fail={} data={} stub={} zero_skip={} read_fail={}",
                 AddressValidation::kAddressHashes.size(), passCount, failCount, dataCount, stubCount, zeroSkipCount,
                 readFailCount);
    LogCClassVtableFromMemory();
    LogIRenderProxyVtableFromMemory();
    logger->flush();
}
} // namespace

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle, RED4ext::v1::EMainReason aReason,
                                        const RED4ext::v1::Sdk* aSdk)
{
    RED4EXT_UNUSED_PARAMETER(aHandle);
    RED4EXT_UNUSED_PARAMETER(aSdk);

    switch (aReason)
    {
    case RED4ext::v1::EMainReason::Load:
        RunValidation();
        break;
    case RED4ext::v1::EMainReason::Unload:
        if (auto logger = GetLogger())
        {
            logger->info("[address_validator] Unload");
            logger->flush();
        }
        spdlog::shutdown();
        break;
    }

    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"address_validator";
    aInfo->author = L"RED4ext";
    aInfo->version = RED4EXT_V1_SEMVER(1, 0, 0);
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_INDEPENDENT;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
