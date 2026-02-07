# AI Agent Optimized Logging

## Overview

RED4ext now includes structured logging optimized for AI agent debugging and analysis. This system provides machine-readable logs with rich context, making it easier for AI agents to understand system state, trace operations, and diagnose issues.

## Features

### 1. Structured Log Format

All logs include:
- **Sequence Numbers**: Unique ID for each log entry (easy reference)
- **Timestamps**: Relative time from system start (milliseconds)
- **Log Levels**: TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL
- **Categories**: Component tags like `[Hooking]`, `[Addresses]`, `[RuntimeValidation]`
- **Context**: Operation, component, plugin, target address, hash
- **Thread IDs**: Track which thread generated the log

**Example Format**:
```
[#1234] [+1250ms] [INFO] [Hooking] {op:HookAttach,comp:Hooking,target:0x104e5c000,hash:0x0e54032b} [tid:0x123456789] Hook attached successfully
```

### 2. Context Tracking

Operations are tracked with context that includes:
- **Operation Name**: e.g., "HookAttach", "ResolveAddress", "PluginLoad"
- **Component**: e.g., "Hooking", "Addresses", "PluginSystem"
- **Plugin Name**: If operation is plugin-specific
- **Target Address**: Pointer being operated on
- **Hash**: Address hash being resolved

Context is automatically pushed/popped for nested operations.

### 3. JSON Export

Enable JSON export for machine parsing:
```bash
export RED4EXT_JSON_LOG="/path/to/red4ext_logs.json"
```

JSON format:
```json
{
  "sequence": 1234,
  "timestamp": "1250",
  "level": "INFO",
  "category": "[Hooking]",
  "message": "Hook attached successfully",
  "thread_id": "0x123456789",
  "context": {
    "operation": "HookAttach",
    "component": "Hooking",
    "plugin": "",
    "target": "0x104e5c000",
    "hash": "0x0e54032b",
    "thread_id": "0x123456789",
    "timestamp": "1250"
  }
}
```

### 4. Log Markers and Sections

Use markers to identify important events:
- `LogMarker("SYSTEM_START")`: System initialization
- `LogSectionStart("HookAttachment")`: Beginning of hook attachment phase
- `LogSectionEnd("HookAttachment")`: End of hook attachment phase

These create easily parseable boundaries in logs.

### 5. Log Summary Generation

Generate summaries for quick analysis:
```cpp
auto summary = Platform::StructuredLogging::GenerateSummary();
```

Summary includes:
- Total log entries
- Error/warning counts
- Unique errors/warnings with counts
- All operations performed
- All components involved
- Time range

### 6. Export Functions

- **ExportToJSON()**: Export all logs to JSON array
- **ExportSummaryToJSON()**: Export summary statistics to JSON

## Usage for AI Agents

### Parsing Logs

1. **Find Errors**: Search for `[ERROR]` or `[CRITICAL]`
2. **Trace Operations**: Follow sequence numbers and context
3. **Identify Patterns**: Group by category, operation, or component
4. **Timeline Analysis**: Use timestamps to understand sequence

### Example Analysis Queries

**Find all hook failures**:
```python
# In JSON logs
errors = [e for e in logs if e["level"] == "ERROR" and "Hook" in e["category"]]
```

**Trace address resolution**:
```python
# Find all ResolveAddress operations
resolutions = [e for e in logs if e["context"]["operation"] == "ResolveAddress"]
```

**Find plugin issues**:
```python
# Group by plugin
plugin_errors = {}
for e in logs:
    if e["level"] in ["ERROR", "WARN"] and e["context"]["plugin"]:
        plugin = e["context"]["plugin"]
        if plugin not in plugin_errors:
            plugin_errors[plugin] = []
        plugin_errors[plugin].append(e)
```

### Log Summary Analysis

The summary JSON provides:
- **Error Frequency**: Which errors occur most often
- **Operation Coverage**: What operations were performed
- **Component Activity**: Which components logged most
- **Time Distribution**: When errors occurred

## Integration Points

### Hook Attachment
- Context: `{op:"HookAttach", comp:"Hooking", target:ptr, hash:hash}`
- Logs: Validation results, success/failure, trampoline addresses

### Address Resolution
- Context: `{op:"ResolveAddress", comp:"Addresses", hash:hash}`
- Logs: Database lookup, symbol resolution, validation results

### Plugin Loading
- Context: `{op:"PluginLoad", comp:"PluginSystem", plugin:name}`
- Logs: Load attempts, errors, initialization results

### Health Checks
- Context: `{op:"HealthCheck", comp:"RuntimeValidation"}`
- Logs: Validation results, warnings, errors

## Best Practices

### For AI Agents

1. **Use Sequence Numbers**: Reference logs by sequence number for precise tracking
2. **Follow Context**: Use context to understand operation flow
3. **Check Summaries**: Start with summary to identify patterns
4. **Filter by Category**: Focus on relevant components
5. **Timeline Analysis**: Use timestamps to understand causality

### For Developers

1. **Push Context**: Always push context before operations
2. **Pop Context**: Always pop context after operations
3. **Use Categories**: Use consistent category names
4. **Include Details**: Log relevant addresses, hashes, plugin names
5. **Mark Sections**: Use markers for major phases

## Environment Variables

- `RED4EXT_JSON_LOG`: Path to JSON log file (enables JSON export)
- `RED4EXT_DEBUG=1`: Enables debug-level logging
- `RED4EXT_VERBOSE=1`: Enables verbose logging

## File Locations

- **Structured Logs**: Same location as regular logs (`red4ext/logs/`)
- **JSON Export**: Path specified in `RED4EXT_JSON_LOG`
- **Summary**: `red4ext/logs/red4ext_summary.json` (generated on shutdown)

## Performance Impact

- **Overhead**: ~1-2 microseconds per log entry
- **Memory**: ~100 bytes per log entry (50k entry buffer = ~5MB)
- **JSON Export**: Minimal overhead (streaming write)

## Future Enhancements

1. **Log Filtering**: Filter logs by category/level at runtime
2. **Remote Logging**: Stream logs to remote server
3. **Log Compression**: Compress old logs automatically
4. **Query Interface**: SQL-like query interface for logs
5. **Visualization**: Generate graphs/charts from logs
