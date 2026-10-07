#include "DetourTransaction.hpp"
#include "Platform.hpp"
#include "Platform/Hooking.hpp"
#include "Utils.hpp"

#include <cstdint>
#include <source_location>
#include <utility>

#ifdef RED4EXT_PLATFORM_MACOS
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/vm_map.h>
#else
#include <Windows.h>
#include <winternl.h>

#include <detours.h>
#include <spdlog/spdlog.h>
#include <wil/resource.h>

extern "C" NTSYSCALLAPI NTSTATUS NTAPI NtGetNextThread(_In_ HANDLE ProcessHandle, _In_opt_ HANDLE ThreadHandle,
                                                       _In_ ACCESS_MASK DesiredAccess, _In_ ULONG HandleAttributes,
                                                       _In_opt_ _Reserved_ ULONG Flags, _Out_ PHANDLE NewThreadHandle);
#endif

DetourTransaction::DetourTransaction(const std::source_location aSource)
    : m_source(aSource)
    , m_state(State::Invalid)
#ifdef RED4EXT_PLATFORM_MACOS
    , m_threadArray(nullptr)
    , m_threadCount(0)
#else
    , m_hasHeapLock(false)
#endif
{

    Log::trace("Trying to start a detour transaction in '{}' ({}:{})", m_source.function_name(), m_source.file_name(),
               m_source.line());

#ifndef RED4EXT_PLATFORM_MACOS
    auto hasLock = HeapLock(GetProcessHeap());
    if (!hasLock)
    {
        Log::error("Could not lock the process heap in '{}' ({}:{}). Last error: {}", m_source.function_name(),
                   m_source.file_name(), m_source.line(), GetLastError());
        return;
    }

    m_hasHeapLock = true;
#endif

    auto result = DetourTransactionBegin();
    if (result == NO_ERROR)
    {
        Log::trace("Transaction was started successfully", m_source.function_name(), m_source.file_name(),
                   m_source.line());

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

#ifndef RED4EXT_PLATFORM_MACOS
    if (m_hasHeapLock)
    {
        HeapUnlock(GetProcessHeap());
    }
#endif
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

    if (!QueueThreadsForUpdate())
    {
        Log::error("Cannot continue with committing the transaction due to failure in queuing threads for update");
        Abort();

        return false;
    }

    auto result = DetourTransactionCommit();
    if (result != NO_ERROR)
    {
#ifndef RED4EXT_PLATFORM_MACOS
        // Detours already aborts the transaction if commit fails. The native engine on macOS does not: the transaction
        // stays started and the destructor aborts it.
        SetState(State::Aborted);
#endif
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

bool DetourTransaction::QueueThreadsForUpdate()
{
    Log::trace("Queueing threads for detour update...");

#ifdef RED4EXT_PLATFORM_MACOS
    // Threads are suspended inside the native commit, after islands and
    // trampolines are allocated. Suspending here would run malloc and logging
    // while other threads are frozen.
    m_threadArray = nullptr;
    m_threadCount = 0;
    return true;
#else
    const HANDLE currentProcess = GetCurrentProcess();
    const DWORD currentThreadId = GetCurrentThreadId();

    static constexpr ACCESS_MASK threadAccess =
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION | THREAD_SUSPEND_RESUME;

    HANDLE thread = nullptr;
    bool closePrevThread = false;

    while (true)
    {
        HANDLE nextThread = nullptr;

        NTSTATUS status = NtGetNextThread(currentProcess, thread, threadAccess, 0, 0, &nextThread);
        if (closePrevThread)
        {
            CloseHandle(thread);
        }

        if (!NT_SUCCESS(status))
        {
            break;
        }

        thread = nextThread;
        closePrevThread = true;

        const DWORD threadId = GetThreadId(thread);
        if (threadId == 0)
        {
            auto lastError = GetLastError();
            spdlog::warn("Could not get thread ID. handle: {}, lastError: {}", thread, lastError);

            continue;
        }

        if (threadId == currentThreadId)
        {
            continue;
        }

        // https://ntdoc.m417z.com/threadinfoclass
        static constexpr THREADINFOCLASS ThreadIsTerminated = (THREADINFOCLASS)0x14;

        BOOL isTerminated = FALSE;
        status = NtQueryInformationThread(thread, ThreadIsTerminated, &isTerminated, sizeof(isTerminated), nullptr);
        if (!NT_SUCCESS(status))
        {
            spdlog::warn("Could not query thread information. threadId: {}, handle: {}, status: {}", threadId, thread,
                         status);
            continue;
        }

        if (isTerminated)
        {
            continue;
        }

        const LONG result = DetourUpdateThread(thread);
        if (result == NO_ERROR)
        {
            m_handles.emplace_back(thread);
            closePrevThread = false;
        }
        else
        {
            spdlog::warn("Could not queue the thread for update. threadId: {}, handle: {}, error code: {}", threadId,
                         thread, result);
            return false;
        }
    }

    spdlog::trace("{} thread(s) queued for detour update (excl. current thread)", m_handles.size());
    return true;
#endif
}

void DetourTransaction::SetState(const State aState)
{
    m_state = aState;
}
