#!/bin/bash
# Test runner script for RED4ext tests

set -e

BUILD_DIR="${1:-build-macos}"
TEST_TYPE="${2:-all}"

echo "Running RED4ext tests..."
echo "Build directory: $BUILD_DIR"
echo "Test type: $TEST_TYPE"

cd "$BUILD_DIR"

case "$TEST_TYPE" in
    unit)
        echo "Running unit tests..."
        ./tests/UnitTests
        ;;
    integration)
        echo "Running integration tests..."
        ./tests/IntegrationTests
        ;;
    performance)
        echo "Running performance benchmarks..."
        ./tests/PerformanceBenchmarks
        ;;
    all)
        echo "Running all tests..."
        ctest --output-on-failure -V
        ;;
    *)
        echo "Unknown test type: $TEST_TYPE"
        echo "Usage: $0 [build_dir] [unit|integration|performance|all]"
        exit 1
        ;;
esac

echo "Tests completed!"
