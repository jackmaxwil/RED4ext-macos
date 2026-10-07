#include "DetourTransaction.hpp"
#include "Utils.hpp"
#include "Platform.hpp"
#include "Platform/Hooking.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/vm_map.h>
#endif

DetourTransaction::DetourTransaction(const std::source_location aSource)
    : m_source(aSource)
    , m_state(State::Invalid)
{
#ifdef RED4EXT_PLATFORM_MACOS
    m_threadArray = nullptr;
    m_threadCount = 0;
#endif

    Log::trace("Trying to start a detour transaction in '{}' ({}:{})", m_source.function_name(),
                  m_source.file_name(), m_source.line());

    auto result = DetourTransactionBegin();
    if (result == NO_ERROR)
    {
        Log::trace("Transaction was started successfully", m_source.function_name(), m_source.file_name(),
                      m_source.line());

        QueueThreadsForUpdate();
        SetState(State::Started);
    }
    else
    {
        Log::error("Could not start the detour transaction in '{}' ({}:{}). Detour error code: {}",
                      m_source.function_name(), m_source.file_name(), m_source.line(), result);
    }
}

DetourTransaction::~DetourTransaction()
{
    // Abort if the transaction is dangling.
    if (m_state == State::Started)
    {
        Abort();
    }
}

const bool DetourTransaction::IsValid() const
{
    return m_state != State::Invalid;
}

bool DetourTransaction::Commit()
{
    Log::trace("Committing the transaction...");

    if (m_state != State::Started && m_state != State::Failed)
    {
        switch (m_state)
        {
        case State::Invalid:
        {
            Log::warn("The transaction is in an invalid state");
            break;
        }
        case State::Committed:
        {
            Log::warn("The transaction is already committed");
            break;
        }
        case State::Aborted:
        {
            Log::warn("The transaction is aborted, can not commit it");
            break;
        }
        default:
        {
            Log::warn("Unknown transaction state. State: {}", static_cast<int32_t>(m_state));
            break;
        }
        }

        return false;
    }

    auto result = DetourTransactionCommit();
    if (result != NO_ERROR)
    {
        Log::error("Could not commit the transaction. Detours error code: {}", result);
        return false;
    }

#ifdef RED4EXT_PLATFORM_MACOS
    // Resume suspended threads
    for (auto thread : m_threads)
    {
        thread_resume(thread);
        mach_port_deallocate(mach_task_self(), thread);
    }
    
    // Deallocate thread array and any threads we didn't suspend
    if (m_threadArray)
    {
        thread_t selfThread = mach_thread_self();
        for (mach_msg_type_number_t i = 0; i < m_threadCount; i++)
        {
            // Check if this thread was suspended (and already deallocated)
            bool wasSuspended = false;
            for (auto suspendedThread : m_threads)
            {
                if (m_threadArray[i] == suspendedThread)
                {
                    wasSuspended = true;
                    break;
                }
            }
            // Don't deallocate self thread or suspended threads (already done)
            if (!wasSuspended && m_threadArray[i] != selfThread)
            {
                mach_port_deallocate(mach_task_self(), m_threadArray[i]);
            }
        }
        mach_port_deallocate(mach_task_self(), selfThread);
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(m_threadArray), 
                     m_threadCount * sizeof(thread_t));
        m_threadArray = nullptr;
        m_threadCount = 0;
    }
    
    m_threads.clear();
#endif

    SetState(State::Committed);
    Log::trace("The transaction was committed successfully");

    return true;
}

bool DetourTransaction::Abort()
{
    Log::trace("Aborting the transaction...");

    if (m_state != State::Started && m_state != State::Failed)
    {
        switch (m_state)
        {
        case State::Invalid:
        {
            Log::warn("The transaction is in an invalid state");
            break;
        }
        case State::Committed:
        {
            Log::warn("The transaction is committed, can not abort it");
            break;
        }
        case State::Aborted:
        {
            Log::warn("The transaction is already aborted");
            break;
        }
        default:
        {
            Log::warn("Unknown transaction state. State: {}", static_cast<int32_t>(m_state));
            break;
        }
        }

        return false;
    }

    auto result = DetourTransactionAbort();
    if (result != NO_ERROR)
    {
        // If this happen, we can't abort it.
        SetState(State::Failed);
        Log::error("Could not abort the transaction. Detours error code: {}", result);

        return false;
    }

#ifdef RED4EXT_PLATFORM_MACOS
    // Resume suspended threads
    for (auto thread : m_threads)
    {
        thread_resume(thread);
        mach_port_deallocate(mach_task_self(), thread);
    }
    
    // Deallocate thread array and any threads we didn't suspend
    if (m_threadArray)
    {
        thread_t selfThread = mach_thread_self();
        for (mach_msg_type_number_t i = 0; i < m_threadCount; i++)
        {
            // Check if this thread was suspended (and already deallocated)
            bool wasSuspended = false;
            for (auto suspendedThread : m_threads)
            {
                if (m_threadArray[i] == suspendedThread)
                {
                    wasSuspended = true;
                    break;
                }
            }
            // Don't deallocate self thread or suspended threads (already done)
            if (!wasSuspended && m_threadArray[i] != selfThread)
            {
                mach_port_deallocate(mach_task_self(), m_threadArray[i]);
            }
        }
        mach_port_deallocate(mach_task_self(), selfThread);
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(m_threadArray), 
                     m_threadCount * sizeof(thread_t));
        m_threadArray = nullptr;
        m_threadCount = 0;
    }
    
    m_threads.clear();
#endif

    SetState(State::Aborted);
    Log::trace("The transaction was aborted successfully");

    return true;
}

void DetourTransaction::QueueThreadsForUpdate()
{
    Log::trace("Queueing threads for detour update...");

#ifdef RED4EXT_PLATFORM_MACOS
    // Threads are suspended inside the native commit, after islands and
    // trampolines are allocated. Suspending here would run malloc and logging
    // while other threads are frozen.
    m_threadArray = nullptr;
    m_threadCount = 0;
    return;
#else
    wil::unique_tool_help_snapshot snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    if (!snapshot)
    {
        auto msg = Utils::FormatLastError();
        Log::warn(L"Could not create a snapshot of the threads. The transaction will continue but unexpected "
                     L"behavior might happen. Error code: {}, msg: '{}'",
                     Platform::GetLastError(), msg);
        return;
    }

    THREADENTRY32 entry;
    entry.dwSize = sizeof(THREADENTRY32);

    if (!Thread32First(snapshot.get(), &entry))
    {
        auto msg = Utils::FormatLastError();
        Log::warn(L"Could not get the first thread entry from the snapshot. The transaction will continue but "
                     L"unexpected behavior might happen. Error code: {}, msg: '{}'",
                     Platform::GetLastError(), msg);
        return;
    }

    auto processId = GetCurrentProcessId();
    auto threadId = GetCurrentThreadId();

    bool shouldContinue = true;
    do
    {
        if (entry.th32OwnerProcessID == processId && entry.th32ThreadID != threadId)
        {
            wil::unique_handle handle(
                OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, false, entry.th32ThreadID));
            if (handle)
            {
                auto result = DetourUpdateThread(handle.get());
                if (result == NO_ERROR)
                {
                    m_handles.emplace_back(std::move(handle));
                }
                else
                {
                    Log::warn(L"Could not queue the thread for update. The transaction will continue but unexpected "
                                 L"behavior might happen. Thread ID: {}, handle: {}, detour error code: {}",
                                 entry.th32ThreadID, handle.get(), result);
                }
            }
            else
            {
                auto msg = Utils::FormatLastError();
                Log::warn(L"Could not open a thread. The transaction will continue but unexpected behavior might "
                             L"happen. Thread ID: {}, error code: {}, msg: '{}'",
                             entry.th32ThreadID, Platform::GetLastError(), msg);
            }
        }

        shouldContinue = Thread32Next(snapshot.get(), &entry);
        if (!shouldContinue && Platform::GetLastError() != ERROR_NO_MORE_FILES)
        {
            auto msg = Utils::FormatLastError();
            Log::warn(L"Could not get the next thread entry from the snapshot. The transaction will continue but "
                         L"unexpected behavior might happen. Error code: {}, msg: '{}'",
                         Platform::GetLastError(), msg);
        }
    } while (shouldContinue);

    Log::trace("{} thread(s) queued for detour update (excl. current thread)", m_handles.size());
#endif
}

void DetourTransaction::SetState(const State aState)
{
    m_state = aState;
}
