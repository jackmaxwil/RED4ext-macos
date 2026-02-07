#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <chrono>
#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>

namespace TestFramework
{
    // Performance benchmark result
    struct BenchmarkResult
    {
        std::string name;
        std::chrono::nanoseconds minTime;
        std::chrono::nanoseconds maxTime;
        std::chrono::nanoseconds avgTime;
        std::chrono::nanoseconds medianTime;
        std::vector<std::chrono::nanoseconds> samples;
        size_t iterations;
    };

    // Performance benchmark runner
    class BenchmarkRunner
    {
    public:
        static BenchmarkRunner& GetInstance()
        {
            static BenchmarkRunner instance;
            return instance;
        }

        void RegisterBenchmark(const std::string& aName, std::function<void()> aBenchmarkFunc, size_t aIterations = 1000)
        {
            m_benchmarks.push_back({aName, aBenchmarkFunc, aIterations});
        }

        BenchmarkResult RunBenchmark(const std::string& aName)
        {
            for (const auto& bench : m_benchmarks)
            {
                if (bench.name == aName)
                {
                    return RunBenchmark(bench);
                }
            }
            return BenchmarkResult{};
        }

        std::vector<BenchmarkResult> RunAll()
        {
            std::vector<BenchmarkResult> results;
            for (const auto& bench : m_benchmarks)
            {
                results.push_back(RunBenchmark(bench));
            }
            return results;
        }

        void PrintResults(const std::vector<BenchmarkResult>& results)
        {
            std::cout << "\nBenchmark Results\n";
            std::cout << "=================\n\n";
            
            for (const auto& result : results)
            {
                std::cout << result.name << ":\n";
                std::cout << "  Iterations: " << result.iterations << "\n";
                std::cout << "  Min: " << FormatDuration(result.minTime) << "\n";
                std::cout << "  Max: " << FormatDuration(result.maxTime) << "\n";
                std::cout << "  Avg: " << FormatDuration(result.avgTime) << "\n";
                std::cout << "  Median: " << FormatDuration(result.medianTime) << "\n";
                std::cout << "\n";
            }
        }

    private:
        struct Benchmark
        {
            std::string name;
            std::function<void()> func;
            size_t iterations;
        };

        BenchmarkResult RunBenchmark(const Benchmark& bench)
        {
            BenchmarkResult result;
            result.name = bench.name;
            result.iterations = bench.iterations;
            result.samples.reserve(bench.iterations);

            // Warmup
            for (size_t i = 0; i < 10; ++i)
            {
                bench.func();
            }

            // Run benchmark
            for (size_t i = 0; i < bench.iterations; ++i)
            {
                auto start = std::chrono::high_resolution_clock::now();
                bench.func();
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
                result.samples.push_back(duration);
            }

            // Calculate statistics
            std::sort(result.samples.begin(), result.samples.end());
            result.minTime = result.samples.front();
            result.maxTime = result.samples.back();
            result.medianTime = result.samples[result.samples.size() / 2];
            
            auto sum = std::accumulate(result.samples.begin(), result.samples.end(), 
                                     std::chrono::nanoseconds(0));
            result.avgTime = sum / result.samples.size();

            return result;
        }

        std::string FormatDuration(std::chrono::nanoseconds duration)
        {
            if (duration.count() < 1000)
            {
                return std::to_string(duration.count()) + " ns";
            }
            else if (duration.count() < 1000000)
            {
                return std::to_string(duration.count() / 1000.0) + " us";
            }
            else if (duration.count() < 1000000000)
            {
                return std::to_string(duration.count() / 1000000.0) + " ms";
            }
            else
            {
                return std::to_string(duration.count() / 1000000000.0) + " s";
            }
        }

        std::vector<Benchmark> m_benchmarks;
    };

    // Benchmark macros
    #define BENCHMARK(name, iter_count) \
        void RegisterBenchmark_##name() { \
            TestFramework::BenchmarkRunner::GetInstance().RegisterBenchmark(#name, []() {

    #define END_BENCHMARK(iter_count) \
        }, iter_count); \
    }
}
#endif
