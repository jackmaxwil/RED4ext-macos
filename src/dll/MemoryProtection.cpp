#include "MemoryProtection.hpp"
#include "Platform.hpp"
#include "Utils.hpp"

MemoryProtection::MemoryProtection(void* aAddress, size_t aSize, uint32_t aProtection)
    : m_address(aAddress)
    , m_size(aSize)
    , m_shouldRestore(false)
{
    Log::trace("Trying to change the protection at {} ({} byte(s)) to {:#x}...", m_address, m_size, aProtection);

    if (!Platform::ProtectMemory(m_address, m_size, aProtection, &m_oldProtection))
    {
        auto msg = Utils::FormatLastError();
        Log::warn(L"Could not change protection at {} ({} byte(s)) to {:#x}. Error code: {}, msg: '{}'", m_address,
                  m_size, aProtection, Platform::GetLastError(), msg);

        throw Exception();
    }

    Log::trace("The protection at {} was successfully changed from {:#x} to {:#x}", aAddress, m_oldProtection,
               aProtection);

    m_shouldRestore = true;
}

MemoryProtection::~MemoryProtection()
{
    if (!m_shouldRestore)
    {
        return;
    }

    Log::trace("Trying to restore the protection at {} ({} byte(s)) to {:#x}...", m_address, m_size, m_oldProtection);

    decltype(m_oldProtection) oldProtection;
    if (!Platform::ProtectMemory(m_address, m_size, m_oldProtection, &oldProtection))
    {
        auto msg = Utils::FormatLastError();
        Log::warn(L"Could not restore protection at {} ({} byte(s)) to {:#x}. Error code: {}, msg: '{}'", m_address,
                  m_size, m_oldProtection, Platform::GetLastError(), msg);

        return;
    }

    Log::trace("The protection at {} was successfully restored from {:#x} to {:#x}", m_address, oldProtection,
               m_oldProtection);
}

MemoryProtection::Exception::Exception()
    : m_lastError(Platform::GetLastError())
{
}

uint32_t MemoryProtection::Exception::GetLastError() const
{
    return m_lastError;
}
