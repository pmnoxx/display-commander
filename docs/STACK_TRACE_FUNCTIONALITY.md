
# Stack Trace Functionality

This document describes the stack trace functionality added to the Display Commander addon for crash debugging.

## Overview

The stack trace functionality automatically captures and logs detailed stack traces to `debug.log` when crashes occur. This helps developers and users diagnose issues by providing detailed information about where the crash happened in the code.

## Features

- **Automatic Crash Detection**: Captures stack traces when unhandled exceptions occur
- **Detailed Symbol Information**: Includes function names, file names, line numbers, and memory addresses
- **Thread-Safe Operation**: Uses critical sections to ensure thread safety during stack capture
- **Integration with Existing Logging**: Uses the same `debug.log` system as other parts of the addon
- **Graceful Degradation**: Falls back gracefully if stack trace functionality is not available

## Implementation Details

### Core Components

1. **`stack_trace.hpp/cpp`**: Core stack trace functionality using Windows DbgHelp API
2. **Enhanced Exception Handler**: Modified `process_exit_hooks.cpp` to capture stack traces on crashes
3. **Developer Tab Integration**: Added testing interface in the developer tab

### Stack Trace Information Captured

For each stack frame, the following information is captured:
- **Module Name**: The DLL or executable containing the code
- **Function Name**: The function where the frame is located
- **File Name**: Source file name (if available)
- **Line Number**: Line number in source file (if available)
- **Memory Address**: Exact memory address of the instruction
- **Offset**: Offset within the function

### Example Output

When a crash occurs, the following information is logged to `debug.log`:

```
=== CRASH STACK TRACE ===
Exception Code: 0xc0000005
Exception Address: 0x00007ff6a1b2c000
Stack Trace:
  #00 0x00007ff6a1b2c000 display_commander.dll!SomeFunction +0x100
  #01 0x00007ff6a1b2d000 display_commander.dll!AnotherFunction (file.cpp:42)
  #02 0x00007ff6a1b2e000 game.exe!MainLoop +0x50
  #03 0x00007ff6a1b2f000 game.exe!WinMain (main.cpp:123)
=== END STACK TRACE ===
```

## Usage

### Automatic Operation

The stack trace functionality works automatically when crashes occur. No user intervention is required.

### Manual Testing

For testing purposes, the developer tab includes two buttons:

1. **Test Stack Trace**: Captures and logs the current stack trace without crashing
2. **Test Crash Handler**: Intentionally triggers a crash to test the crash handler (WARNING: Will crash the application!)

### Developer Tab Access

1. Open ReShade overlay in a game
2. Navigate to the Display Commander addon
3. Go to the "Developer" tab
4. Scroll down to the "Stack Trace Testing" section

## Technical Requirements

- **Windows DbgHelp API**: Uses `dbghelp.dll` for symbol resolution
- **Debug Symbols**: Works best with debug symbols available (PDB files)
- **Thread Safety**: Uses critical sections for thread-safe operation
- **Memory Management**: Properly initializes and cleans up DbgHelp resources

## Configuration

The stack trace functionality is automatically initialized when the addon starts and cleaned up when it shuts down. No configuration is required.

## Troubleshooting

### Stack Trace Not Available

If the stack trace functionality is not available, check:
- Ensure `dbghelp.dll` is available on the system
- Check that the addon has proper permissions to access symbol information
- Verify that the stack trace initialization succeeded (check debug.log for initialization messages)

### Incomplete Stack Traces

If stack traces are incomplete or missing information:
- Ensure debug symbols (PDB files) are available for the modules
- Check that the modules are not stripped of symbol information
- Verify that the crash occurred in code that has symbol information available

### Performance Considerations

- Stack trace capture is only performed during crashes, so it has no impact on normal operation
- Symbol loading is deferred until needed to minimize startup time
- Critical sections ensure thread safety without significant performance impact

## Integration Points

The stack trace functionality integrates with:
- **Process Exit Hooks**: Captures stack traces during unhandled exceptions
- **Exit Handler**: Uses the same logging system for consistent output
- **Developer Tab**: Provides testing interface for verification
- **CMake Build System**: Links with `dbghelp` library

## Future Enhancements

Potential future improvements:
- Support for different symbol formats
- Enhanced filtering of stack frames
- Integration with external crash reporting systems
- Support for different architectures (ARM, etc.)
