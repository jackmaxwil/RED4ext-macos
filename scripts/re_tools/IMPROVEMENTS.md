# Dynamic Analysis Script Improvements

## Summary

Enhanced `dynamic_analysis.py` with:
- ✅ **Error Handling**: Comprehensive try-catch blocks, validation, graceful degradation
- ✅ **Logging**: Structured logging with levels (DEBUG/INFO/WARNING/ERROR), file logging support
- ✅ **Observability**: Progress metrics, real-time statistics, hook statistics
- ✅ **Progress Tracking**: Elapsed time, call rates, function counts, error tracking
- ✅ **Save File Format**: Improved JSON structure with metadata, metrics, statistics
- ✅ **Frida JS Best Practices**: Robust module discovery, error handling, validation

## Key Improvements

### 1. Error Handling

**Before**: Basic error messages, no validation
```python
pid = find_process(process_name)
if not pid:
    print(f"[ERROR] Process not found")
    return {}, {}
```

**After**: Comprehensive validation and error handling
```python
def find_process(process_name: str, logger: logging.Logger) -> Optional[int]:
    """Find process ID with flexible matching and error handling."""
    try:
        # Multiple matching strategies
        # Detailed error logging
        # Exception handling with context
    except Exception as e:
        logger.error(f"Failed to enumerate processes: {e}", exc_info=True)
        return None
```

**Improvements**:
- ✅ Input validation (offset ranges, process names)
- ✅ Address validation (readability checks before hooking)
- ✅ Graceful degradation (continue on non-critical errors)
- ✅ Error context (stack traces, detailed messages)
- ✅ Error metrics tracking

### 2. Logging

**Before**: Simple print statements
```python
print(f"[+] Attached to process {pid}")
print(f"[ERROR] Process not found")
```

**After**: Structured logging with levels
```python
logger = setup_logging("INFO", log_file=Path("analysis.log"))
logger.info(f"Attached to process {pid}")
logger.error(f"Process not found: {process_name}", exc_info=True)
logger.debug(f"Hook statistics: {hook_stats}")
```

**Features**:
- ✅ Log levels: DEBUG, INFO, WARNING, ERROR, CRITICAL
- ✅ File logging (optional, separate from console)
- ✅ Timestamps and context (filename, line number in file logs)
- ✅ Structured format (timestamp, level, message)
- ✅ Configurable via `--log-level` and `--log-file`

**JavaScript Logging**:
```javascript
function log(level, message, data) {
    const timestamp = new Date().toISOString();
    const logEntry = {
        timestamp: timestamp,
        level: level,
        message: message,
        data: data || {}
    };
    // Console + RPC to Python
}
```

### 3. Observability

**Before**: Minimal progress feedback
```python
print(f"  [{elapsed}s] Functions: {stats['functionCount']}, Calls: {stats['totalCalls']}")
```

**After**: Comprehensive metrics tracking
```python
@dataclass
class ProgressMetrics:
    start_time: float
    elapsed_time: float = 0.0
    functions_hooked: int = 0
    functions_called: int = 0
    total_calls: int = 0
    call_graph_edges: int = 0
    errors: int = 0
    warnings: int = 0
    calls_per_second: float = 0.0
```

**Features**:
- ✅ Real-time progress updates
- ✅ Performance metrics (calls per second)
- ✅ Error/warning counters
- ✅ Hook statistics (successful/failed hooks)
- ✅ Export metrics to JSON output

### 4. Progress Tracking

**Before**: Basic time-based updates
```python
if elapsed % 10 == 0:
    print(f"  [{elapsed}s] Functions: {stats['functionCount']}")
```

**After**: Comprehensive progress tracking
```python
# Real-time metrics updates
metrics.update(
    functions_hooked=hooked_count,
    functions_called=stats['functionCount'],
    total_calls=stats['totalCalls'],
    call_graph_edges=stats['callGraphSize'],
    elapsed_time=elapsed
)

# Detailed progress logging
logger.info(f"[{elapsed}s] Functions: {stats['functionCount']}, "
            f"Calls: {stats['totalCalls']}, Edges: {stats['callGraphSize']}, "
            f"Rate: {metrics.calls_per_second:.1f} calls/s")
```

**Features**:
- ✅ Per-second metrics updates
- ✅ Burst mode progress (burst number, phase)
- ✅ Hook progress (hooked/total, success/failure)
- ✅ Call rate calculation
- ✅ Final summary with all metrics

### 5. Save File Format

**Before**: Basic structure
```json
{
  "version": "1.0",
  "functions": {...},
  "call_graph": {...},
  "stats": {...}
}
```

**After**: Enhanced structure with metadata
```json
{
  "metadata": {
    "version": "2.0",
    "format_version": "2.0",
    "timestamp": "2024-01-05T20:30:00",
    "analysis_date": "2024-01-05",
    "analysis_time": "20:30:00",
    "tool": "dynamic_analysis.py",
    "frida_version": "16.2.0"
  },
  "configuration": {
    "process_name": "Cyberpunk2077",
    "process_id": 12345,
    "trace_duration_seconds": 60,
    "sample_rate": 10,
    "min_call_count": 1
  },
  "metrics": {
    "elapsed_time_seconds": 60.5,
    "functions_hooked": 100,
    "functions_called": 85,
    "total_calls": 125000,
    "call_graph_edges": 250,
    "errors": 2,
    "warnings": 5,
    "calls_per_second": 2066.12
  },
  "functions": {...},
  "call_graph": {
    "edges": [...],
    "edge_count": 250,
    "node_count": 150
  },
  "signatures": {...},
  "statistics": {
    "total_functions": 85,
    "functions_with_calls": 80,
    "call_edges": 250,
    "total_calls": 125000,
    "average_calls_per_function": 1470.59,
    "max_calls": 50000,
    "functions_with_callers": 70,
    "functions_with_callees": 60
  }
}
```

**Improvements**:
- ✅ Metadata section (version, timestamps, tool info)
- ✅ Configuration section (all analysis parameters)
- ✅ Metrics section (performance and progress data)
- ✅ Enhanced statistics (averages, max, counts)
- ✅ Call graph structure (edge/node counts)
- ✅ ISO 8601 timestamps
- ✅ Version tracking for format compatibility

### 6. Frida JavaScript Best Practices

**Module Discovery**:
```javascript
function findCyberpunkBase() {
    // Method 1: Module.findBaseAddress
    // Method 2: Enumerate modules
    // Method 3: Fallback to first module
    // All with error handling and logging
}
```

**Error Handling**:
```javascript
try {
    Interceptor.attach(funcAddr, {...});
    hookStats.successfulHooks++;
} catch (e) {
    logError('Interceptor.attach failed', e);
    hookStats.failedHooks++;
    hookStats.hookErrors.push({offset, error: e.toString()});
    discoveredFunctions.delete(offsetKey);  // Cleanup
    return false;
}
```

**Validation**:
```javascript
// Validate offset
if (typeof offset !== 'number' || offset < 0 || offset >= 0x10000000) {
    logWarn('Invalid offset provided', {offset});
    return false;
}

// Validate address readability
try {
    funcAddr.readU8();  // Try to read to validate
} catch (e) {
    logWarn('Invalid function address', {offset, error: e.toString()});
    return false;
}
```

**Structured Logging**:
```javascript
function log(level, message, data) {
    const timestamp = new Date().toISOString();
    const logEntry = {
        timestamp: timestamp,
        level: level,
        message: message,
        data: data || {}
    };
    // Console + RPC to Python
}
```

**RPC Error Handling**:
```javascript
rpc.exports = {
    getFunctions: function() {
        try {
            return Array.from(discoveredFunctions.values());
        } catch (e) {
            logError('getFunctions failed', e);
            return [];
        }
    },
    // ... all exports with try-catch
};
```

## Usage Examples

### Basic Usage
```bash
python3 dynamic_analysis.py \
  --process-name "Cyberpunk" \
  --static-functions static_functions.json \
  --output results.json
```

### With Logging
```bash
python3 dynamic_analysis.py \
  --process-name "Cyberpunk" \
  --static-functions static_functions.json \
  --log-level DEBUG \
  --log-file analysis.log \
  --output results.json
```

### Burst Mode with Metrics
```bash
python3 dynamic_analysis.py \
  --process-name "Cyberpunk" \
  --static-functions static_functions.json \
  --burst-mode \
  --burst-duration 5 \
  --burst-interval 10 \
  --sample-rate 20 \
  --max-functions 50 \
  --trace-duration 120 \
  --log-level INFO \
  --output results.json
```

## Migration Guide

### From Old Script

1. **Replace print statements** with logger calls:
   ```python
   # Old
   print(f"[+] Attached to process {pid}")
   
   # New
   logger.info(f"Attached to process {pid}")
   ```

2. **Add error handling**:
   ```python
   # Old
   pid = find_process(name)
   
   # New
   pid = find_process(name, logger)
   if not pid:
       logger.error("Process not found")
       return
   ```

3. **Use metrics**:
   ```python
   # Old
   print(f"Calls: {total_calls}")
   
   # New
   metrics.update(total_calls=total_calls)
   logger.info(f"Calls: {metrics.total_calls}")
   ```

## Benefits

1. **Debugging**: Structured logs make it easier to find issues
2. **Monitoring**: Real-time metrics show analysis progress
3. **Reliability**: Error handling prevents crashes
4. **Traceability**: Metadata and timestamps in output files
5. **Performance**: Metrics show analysis efficiency
6. **Compatibility**: Version tracking for format changes

## Next Steps

- [ ] Add unit tests for error handling
- [ ] Add performance benchmarks
- [ ] Add JSON schema validation
- [ ] Add progress bar visualization
- [ ] Add export to other formats (CSV, SQLite)
