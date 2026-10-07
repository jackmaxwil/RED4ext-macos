#pragma once

#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace TestFramework
{
// Test result
enum class TestResult
{
    Pass,
    Fail,
    Skip
};

// Test case
class TestCase
{
public:
    TestCase(const std::string& aName, std::function<void()> aTestFunc)
        : m_name(aName)
        , m_testFunc(aTestFunc)
        , m_result(TestResult::Skip)
    {
    }

    void Run()
    {
        try
        {
            m_startTime = std::chrono::steady_clock::now();
            m_testFunc();
            m_result = TestResult::Pass;
        }
        catch (const std::exception& e)
        {
            m_errorMessage = e.what();
            m_result = TestResult::Fail;
        }
        catch (...)
        {
            m_errorMessage = "Unknown exception";
            m_result = TestResult::Fail;
        }
        m_endTime = std::chrono::steady_clock::now();
    }

    const std::string& GetName() const
    {
        return m_name;
    }
    TestResult GetResult() const
    {
        return m_result;
    }
    const std::string& GetErrorMessage() const
    {
        return m_errorMessage;
    }
    std::chrono::milliseconds GetDuration() const
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(m_endTime - m_startTime);
    }

private:
    std::string m_name;
    std::function<void()> m_testFunc;
    TestResult m_result;
    std::string m_errorMessage;
    std::chrono::steady_clock::time_point m_startTime;
    std::chrono::steady_clock::time_point m_endTime;
};

// Test suite
class TestSuite
{
public:
    TestSuite(const std::string& aName)
        : m_name(aName)
    {
    }

    void AddTest(const std::string& aName, std::function<void()> aTestFunc)
    {
        m_tests.emplace_back(aName, aTestFunc);
    }

    void Run()
    {
        std::cout << "Running test suite: " << m_name << std::endl;
        for (auto& test : m_tests)
        {
            test.Run();
            PrintTestResult(test);
        }
    }

    size_t GetPassCount() const
    {
        size_t count = 0;
        for (const auto& test : m_tests)
        {
            if (test.GetResult() == TestResult::Pass)
            {
                count++;
            }
        }
        return count;
    }

    size_t GetFailCount() const
    {
        size_t count = 0;
        for (const auto& test : m_tests)
        {
            if (test.GetResult() == TestResult::Fail)
            {
                count++;
            }
        }
        return count;
    }

    const std::string& GetName() const
    {
        return m_name;
    }
    const std::vector<TestCase>& GetTests() const
    {
        return m_tests;
    }

private:
    void PrintTestResult(const TestCase& test)
    {
        const char* status = test.GetResult() == TestResult::Pass ? "PASS" : "FAIL";
        std::cout << "  [" << status << "] " << test.GetName();
        if (test.GetResult() == TestResult::Fail)
        {
            std::cout << " - " << test.GetErrorMessage();
        }
        std::cout << " (" << test.GetDuration().count() << "ms)" << std::endl;
    }

    std::string m_name;
    std::vector<TestCase> m_tests;
};

// Test runner
class TestRunner
{
public:
    static TestRunner& GetInstance()
    {
        static TestRunner instance;
        return instance;
    }

    void RegisterSuite(const std::string& aName, std::function<void(TestSuite&)> aSuiteFunc)
    {
        auto suite = std::make_unique<TestSuite>(aName);
        aSuiteFunc(*suite);
        m_suites.push_back(std::move(suite));
    }

    int RunAll()
    {
        std::cout << "Running all test suites..." << std::endl;
        std::cout << "================================" << std::endl;

        size_t totalPass = 0;
        size_t totalFail = 0;

        for (auto& suite : m_suites)
        {
            suite->Run();
            totalPass += suite->GetPassCount();
            totalFail += suite->GetFailCount();
            std::cout << std::endl;
        }

        std::cout << "================================" << std::endl;
        std::cout << "Total: " << (totalPass + totalFail) << " tests" << std::endl;
        std::cout << "Passed: " << totalPass << std::endl;
        std::cout << "Failed: " << totalFail << std::endl;

        return totalFail > 0 ? 1 : 0;
    }

private:
    std::vector<std::unique_ptr<TestSuite>> m_suites;
};

// Assertions
class AssertionFailure : public std::exception
{
public:
    AssertionFailure(const std::string& aMessage)
        : m_message(aMessage)
    {
    }
    const char* what() const noexcept override
    {
        return m_message.c_str();
    }

private:
    std::string m_message;
};

inline void Assert(bool aCondition, const std::string& aMessage)
{
    if (!aCondition)
    {
        throw AssertionFailure(aMessage);
    }
}

inline void AssertEqual(auto aExpected, auto aActual, const std::string& aMessage = "")
{
    if (aExpected != aActual)
    {
        std::ostringstream oss;
        oss << "Expected: " << aExpected << ", Actual: " << aActual;
        if (!aMessage.empty())
        {
            oss << " - " << aMessage;
        }
        throw AssertionFailure(oss.str());
    }
}

inline void AssertNotEqual(auto aExpected, auto aActual, const std::string& aMessage = "")
{
    if (aExpected == aActual)
    {
        std::ostringstream oss;
        oss << "Expected not equal, but both are: " << aExpected;
        if (!aMessage.empty())
        {
            oss << " - " << aMessage;
        }
        throw AssertionFailure(oss.str());
    }
}

inline void AssertNull(void* aPtr, const std::string& aMessage = "")
{
    if (aPtr != nullptr)
    {
        std::ostringstream oss;
        oss << "Expected null pointer, got: " << aPtr;
        if (!aMessage.empty())
        {
            oss << " - " << aMessage;
        }
        throw AssertionFailure(oss.str());
    }
}

inline void AssertNotNull(void* aPtr, const std::string& aMessage = "")
{
    if (aPtr == nullptr)
    {
        std::ostringstream oss;
        oss << "Expected non-null pointer";
        if (!aMessage.empty())
        {
            oss << " - " << aMessage;
        }
        throw AssertionFailure(oss.str());
    }
}

// Test macros
#define TEST_SUITE(name)                                                                                               \
    void RegisterSuite_##name(TestFramework::TestSuite& suite);                                                        \
    void RegisterSuite_##name(TestFramework::TestSuite& suite)

#define TEST(name)                                                                                                     \
        suite.AddTest(#name, []() {
#define END_TEST                                                                                                       \
    });

#define ASSERT(condition) TestFramework::Assert(condition, "Assertion failed: " #condition)

#define ASSERT_EQ(expected, actual) TestFramework::AssertEqual(expected, actual, #expected " == " #actual)

#define ASSERT_NE(expected, actual) TestFramework::AssertNotEqual(expected, actual, #expected " != " #actual)

#define ASSERT_NULL(ptr) TestFramework::AssertNull(ptr, #ptr " is null")

#define ASSERT_NOT_NULL(ptr) TestFramework::AssertNotNull(ptr, #ptr " is not null")

#define ASSERT_TRUE(condition) TestFramework::Assert(condition, #condition " is true")

#define ASSERT_FALSE(condition) TestFramework::Assert(!(condition), #condition " is false")
} // namespace TestFramework
