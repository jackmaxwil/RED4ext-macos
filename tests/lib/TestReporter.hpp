#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include "TestFramework.hpp"

namespace TestFramework
{
    // Test report formats
    enum class ReportFormat
    {
        JSON,
        HTML,
        JUnit,
        Console
    };

    // Test report generator
    class TestReporter
    {
    public:
        struct TestSuiteResult
        {
            std::string name;
            size_t totalTests;
            size_t passedTests;
            size_t failedTests;
            size_t skippedTests;
            std::chrono::milliseconds duration;
            std::vector<std::pair<std::string, std::string>> failures; // test name, error message
        };

        struct TestReport
        {
            std::string timestamp;
            std::vector<TestSuiteResult> suites;
            size_t totalTests;
            size_t totalPassed;
            size_t totalFailed;
            size_t totalSkipped;
            std::chrono::milliseconds totalDuration;
        };

        static void GenerateReport(const std::vector<TestSuite>& aSuites, 
                                   ReportFormat aFormat, 
                                   const std::string& aOutputPath = "")
        {
            TestReport report;
            report.timestamp = GetCurrentTimestamp();
            report.totalTests = 0;
            report.totalPassed = 0;
            report.totalFailed = 0;
            report.totalSkipped = 0;

            for (const auto& suite : aSuites)
            {
                TestSuiteResult suiteResult;
                suiteResult.name = suite.GetName();
                suiteResult.totalTests = suite.GetTests().size();
                suiteResult.passedTests = suite.GetPassCount();
                suiteResult.failedTests = suite.GetFailCount();
                suiteResult.skippedTests = suiteResult.totalTests - 
                                          suiteResult.passedTests - 
                                          suiteResult.failedTests;

                for (const auto& test : suite.GetTests())
                {
                    if (test.GetResult() == TestResult::Fail)
                    {
                        suiteResult.failures.push_back({test.GetName(), test.GetErrorMessage()});
                    }
                }

                report.suites.push_back(suiteResult);
                report.totalTests += suiteResult.totalTests;
                report.totalPassed += suiteResult.passedTests;
                report.totalFailed += suiteResult.failedTests;
                report.totalSkipped += suiteResult.skippedTests;
            }

            switch (aFormat)
            {
            case ReportFormat::JSON:
                GenerateJSONReport(report, aOutputPath);
                break;
            case ReportFormat::HTML:
                GenerateHTMLReport(report, aOutputPath);
                break;
            case ReportFormat::JUnit:
                GenerateJUnitReport(report, aOutputPath);
                break;
            case ReportFormat::Console:
                GenerateConsoleReport(report);
                break;
            }
        }

    private:
        static std::string GetCurrentTimestamp()
        {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            char buffer[64];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
            return buffer;
        }

        static void GenerateJSONReport(const TestReport& report, const std::string& path)
        {
            std::ofstream file(path.empty() ? "test_report.json" : path);
            file << "{\n";
            file << "  \"timestamp\": \"" << report.timestamp << "\",\n";
            file << "  \"summary\": {\n";
            file << "    \"total\": " << report.totalTests << ",\n";
            file << "    \"passed\": " << report.totalPassed << ",\n";
            file << "    \"failed\": " << report.totalFailed << ",\n";
            file << "    \"skipped\": " << report.totalSkipped << "\n";
            file << "  },\n";
            file << "  \"suites\": [\n";
            
            for (size_t i = 0; i < report.suites.size(); ++i)
            {
                const auto& suite = report.suites[i];
                if (i > 0) file << ",\n";
                file << "    {\n";
                file << "      \"name\": \"" << suite.name << "\",\n";
                file << "      \"total\": " << suite.totalTests << ",\n";
                file << "      \"passed\": " << suite.passedTests << ",\n";
                file << "      \"failed\": " << suite.failedTests << ",\n";
                file << "      \"skipped\": " << suite.skippedTests << ",\n";
                file << "      \"failures\": [\n";
                
                for (size_t j = 0; j < suite.failures.size(); ++j)
                {
                    if (j > 0) file << ",\n";
                    file << "        {\n";
                    file << "          \"test\": \"" << suite.failures[j].first << "\",\n";
                    file << "          \"error\": \"" << suite.failures[j].second << "\"\n";
                    file << "        }";
                }
                
                file << "\n      ]\n";
                file << "    }";
            }
            
            file << "\n  ]\n";
            file << "}\n";
        }

        static void GenerateHTMLReport(const TestReport& report, const std::string& path)
        {
            std::ofstream file(path.empty() ? "test_report.html" : path);
            file << "<!DOCTYPE html>\n<html><head><title>Test Report</title></head><body>\n";
            file << "<h1>Test Report</h1>\n";
            file << "<p>Generated: " << report.timestamp << "</p>\n";
            file << "<h2>Summary</h2>\n";
            file << "<ul>\n";
            file << "<li>Total: " << report.totalTests << "</li>\n";
            file << "<li>Passed: " << report.totalPassed << "</li>\n";
            file << "<li>Failed: " << report.totalFailed << "</li>\n";
            file << "<li>Skipped: " << report.totalSkipped << "</li>\n";
            file << "</ul>\n";
            
            for (const auto& suite : report.suites)
            {
                file << "<h2>" << suite.name << "</h2>\n";
                file << "<p>Passed: " << suite.passedTests << ", Failed: " << suite.failedTests << "</p>\n";
                
                if (!suite.failures.empty())
                {
                    file << "<h3>Failures</h3>\n<ul>\n";
                    for (const auto& failure : suite.failures)
                    {
                        file << "<li><strong>" << failure.first << ":</strong> " << failure.second << "</li>\n";
                    }
                    file << "</ul>\n";
                }
            }
            
            file << "</body></html>\n";
        }

        static void GenerateJUnitReport(const TestReport& report, const std::string& path)
        {
            std::ofstream file(path.empty() ? "test_report.xml" : path);
            file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            file << "<testsuites>\n";
            
            for (const auto& suite : report.suites)
            {
                file << "  <testsuite name=\"" << suite.name << "\" tests=\"" << suite.totalTests 
                     << "\" failures=\"" << suite.failedTests << "\" skipped=\"" << suite.skippedTests << "\">\n";
                
                for (const auto& failure : suite.failures)
                {
                    file << "    <testcase name=\"" << failure.first << "\">\n";
                    file << "      <failure message=\"" << failure.second << "\"/>\n";
                    file << "    </testcase>\n";
                }
                
                file << "  </testsuite>\n";
            }
            
            file << "</testsuites>\n";
        }

        static void GenerateConsoleReport(const TestReport& report)
        {
            std::cout << "\nTest Report\n";
            std::cout << "===========\n\n";
            std::cout << "Timestamp: " << report.timestamp << "\n\n";
            std::cout << "Summary:\n";
            std::cout << "  Total: " << report.totalTests << "\n";
            std::cout << "  Passed: " << report.totalPassed << "\n";
            std::cout << "  Failed: " << report.totalFailed << "\n";
            std::cout << "  Skipped: " << report.totalSkipped << "\n\n";
            
            for (const auto& suite : report.suites)
            {
                std::cout << suite.name << ":\n";
                std::cout << "  Passed: " << suite.passedTests << "\n";
                std::cout << "  Failed: " << suite.failedTests << "\n";
                
                if (!suite.failures.empty())
                {
                    std::cout << "  Failures:\n";
                    for (const auto& failure : suite.failures)
                    {
                        std::cout << "    - " << failure.first << ": " << failure.second << "\n";
                    }
                }
                std::cout << "\n";
            }
        }
    };
}
#endif
