#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace Platform
{
namespace ThreadSafety
{
// Thread-local storage for tracking current operation
struct ThreadContext
{
    std::thread::id threadId;
    const char* operation = nullptr;
    void* hookTarget = nullptr;
    uint64_t timestamp = 0;
};

// Check if current thread is the main game thread
bool IsMainThread();

// Get main thread ID (set during initialization)
std::thread::id GetMainThreadId();

// Set main thread ID (call during initialization)
void SetMainThreadId(std::thread::id aThreadId);

// Validate hook operations are thread-safe
bool ValidateHookThreadSafety(void* aTarget, const char* aOperation);

// Record hook operation for debugging
void RecordHookOperation(void* aTarget, const char* aOperation, bool aSuccess);

// Check for potential race conditions
struct RaceConditionCheck
{
    bool hasRaceCondition = false;
    std::string reason;
    std::vector<std::string> conflictingOperations;
};
RaceConditionCheck CheckForRaceConditions();

// Guard for hook operations (RAII)
class HookOperationGuard
{
public:
    HookOperationGuard(void* aTarget, const char* aOperation);
    ~HookOperationGuard();

    void MarkSuccess();
    void MarkFailure(const std::string& aReason);

private:
    void* m_target;
    const char* m_operation;
    bool m_completed;
};
} // namespace ThreadSafety
} // namespace Platform
#endif
