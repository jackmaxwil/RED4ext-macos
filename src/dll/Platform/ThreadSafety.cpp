#include "ThreadSafety.hpp"

#ifdef RED4EXT_PLATFORM_MACOS
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <sstream>
#include <spdlog/spdlog.h>
#include <fmt/format.h>

namespace Platform
{
namespace ThreadSafety
{
namespace
{
std::atomic<std::thread::id> g_mainThreadId{std::this_thread::get_id()};
std::mutex g_operationMutex;
std::unordered_map<void*, ThreadContext> g_activeOperations;
std::atomic<uint64_t> g_operationCounter{0};
}

bool IsMainThread()
{
    return std::this_thread::get_id() == g_mainThreadId.load();
}

std::thread::id GetMainThreadId()
{
    return g_mainThreadId.load();
}

void SetMainThreadId(std::thread::id aThreadId)
{
    g_mainThreadId.store(aThreadId);
}

bool ValidateHookThreadSafety(void* aTarget, const char* aOperation)
{
    if (!aTarget)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_operationMutex);
    
    // Check if another operation is already in progress on this target
    auto it = g_activeOperations.find(aTarget);
    if (it != g_activeOperations.end())
    {
        const auto& existing = it->second;
        if (existing.threadId != std::this_thread::get_id())
        {
            std::ostringstream tid1, tid2;
            tid1 << std::this_thread::get_id();
            tid2 << existing.threadId;
            spdlog::warn("[ThreadSafety] Potential race condition: {} on {} from thread {} while {} is active from thread {}", 
                        aOperation, fmt::ptr(aTarget), tid1.str(), 
                        existing.operation ? existing.operation : "unknown", tid2.str());
            return false;
        }
    }

    return true;
}

void RecordHookOperation(void* aTarget, const char* aOperation, bool aSuccess)
{
    if (!aTarget || !aOperation)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_operationMutex);
    
    if (aSuccess)
    {
        // Remove from active operations
        g_activeOperations.erase(aTarget);
    }
    else
    {
        // Keep in active operations for debugging
        auto& ctx = g_activeOperations[aTarget];
        ctx.threadId = std::this_thread::get_id();
        ctx.operation = aOperation;
        ctx.hookTarget = aTarget;
        ctx.timestamp = g_operationCounter.fetch_add(1);
    }
}

RaceConditionCheck CheckForRaceConditions()
{
    RaceConditionCheck result;
    
    std::lock_guard<std::mutex> lock(g_operationMutex);
    
    // Check for operations from different threads on same target
    std::unordered_map<void*, std::vector<ThreadContext>> targetsByThread;
    for (const auto& [target, ctx] : g_activeOperations)
    {
        targetsByThread[target].push_back(ctx);
    }
    
    for (const auto& [target, contexts] : targetsByThread)
    {
        if (contexts.size() > 1)
        {
            std::unordered_set<std::thread::id> threadIds;
            for (const auto& ctx : contexts)
            {
                threadIds.insert(ctx.threadId);
            }
            
            if (threadIds.size() > 1)
            {
                result.hasRaceCondition = true;
                result.reason = fmt::format("Multiple threads operating on target {}", fmt::ptr(target));
                for (const auto& ctx : contexts)
                {
            std::ostringstream tid;
            tid << ctx.threadId;
            std::string opStr = ctx.operation ? ctx.operation : "unknown";
            result.conflictingOperations.push_back(
                fmt::format("{} from thread {}", opStr, tid.str()));
                }
            }
        }
    }
    
    return result;
}

HookOperationGuard::HookOperationGuard(void* aTarget, const char* aOperation)
    : m_target(aTarget)
    , m_operation(aOperation)
    , m_completed(false)
{
    if (m_target && m_operation)
    {
        std::lock_guard<std::mutex> lock(g_operationMutex);
        auto& ctx = g_activeOperations[m_target];
        ctx.threadId = std::this_thread::get_id();
        ctx.operation = m_operation;
        ctx.hookTarget = m_target;
        ctx.timestamp = g_operationCounter.fetch_add(1);
    }
}

HookOperationGuard::~HookOperationGuard()
{
    if (!m_completed && m_target)
    {
        MarkFailure("operation guard destroyed without completion");
    }
}

void HookOperationGuard::MarkSuccess()
{
    if (m_target)
    {
        RecordHookOperation(m_target, m_operation, true);
        m_completed = true;
    }
}

void HookOperationGuard::MarkFailure(const std::string& aReason)
{
    if (m_target)
    {
        RecordHookOperation(m_target, m_operation, false);
        std::ostringstream tid;
        tid << std::this_thread::get_id();
        spdlog::error("[ThreadSafety] Hook operation '{}' on {} failed: {}", 
                     m_operation, fmt::ptr(m_target), aReason);
        m_completed = true;
    }
}
}
}
#endif
