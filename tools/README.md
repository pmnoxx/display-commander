# Enhanced ReShade Injector

An improved version of the ReShade injector that can monitor multiple executables and run continuously without termination.

## Features

- **Multi-Process Monitoring**: Monitor multiple target executables simultaneously
- **Configuration File Support**: Define target executables in a simple INI configuration file
- **Continuous Operation**: Runs indefinitely, monitoring for new processes
- **Duplicate Prevention**: Tracks injected processes to avoid duplicate injections
- **Enhanced Logging**: Detailed logging with timestamps and error reporting
- **Graceful Shutdown**: Responds to Ctrl+C for clean termination
- **Process Cleanup**: Automatically removes terminated processes from tracking

## Usage

### Basic Usage
```bash
enhanced_injector.exe
```

### Command Line Options
```bash
enhanced_injector.exe --help
```

## Configuration

The injector looks for `injector_config.ini` in the same directory as the executable. If not found, it will create a default configuration file.

### Configuration File Format

```ini
[Settings]
verbose_logging=true
reshade_dll_path=C:\Path\To\ReShade64.dll

[Targets]
Game1=game1.exe
Game2=game2.exe
Game3=game3.exe
```

### Settings Section
- `verbose_logging`: Enable/disable detailed logging (true/false)
- `reshade_dll_path`: Custom path to ReShade DLL (optional, uses default if empty)

### Targets Section
- Format: `display_name=executable_name.exe`
- `display_name`: Used in logs for identification
- `executable_name`: Actual process name to monitor

## Building

### Prerequisites
- CMake 3.16 or later
- Visual Studio 2019 or later (Windows)
- C++17 compatible compiler

### Build Steps
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## How It Works

1. **Initialization**: Loads configuration file and sets up monitoring
2. **Process Scanning**: Continuously scans running processes every second
3. **Target Matching**: Compares process names against configured targets
4. **Injection**: Injects ReShade DLL into matching processes
5. **Tracking**: Maintains list of injected PIDs to prevent duplicates
6. **Cleanup**: Removes terminated processes from tracking

## Differences from Original ReShade Injector

| Feature | Original | Enhanced |
|---------|----------|----------|
| Target Count | Single | Multiple |
| Operation Mode | One-shot | Continuous |
| Configuration | Command line | INI file |
| Process Tracking | None | Full tracking |
| Logging | Basic | Detailed with timestamps |
| Error Handling | Basic | Comprehensive |
| Shutdown | Automatic | Graceful (Ctrl+C) |

## Error Handling

The enhanced injector includes comprehensive error handling:
- Process access failures
- Architecture mismatches
- DLL loading errors
- Memory allocation failures
- Configuration file issues

All errors are logged with detailed information and error codes.

## Process Architecture Support

- Supports both 32-bit and 64-bit target processes
- Automatically detects process architecture
- Uses appropriate ReShade DLL (32-bit or 64-bit)
- Validates architecture compatibility before injection

## Logging

Log messages include:
- Timestamp
- Process discovery
- Injection attempts and results
- Error details with codes
- Process termination cleanup

Example log output:
```
[14:30:15] Enhanced ReShade Injector starting...
[14:30:15] Loaded 3 target executables from configuration
[14:30:15] Monitoring 3 target executables
[14:30:15] Press Ctrl+C to stop monitoring
[14:30:20] Found new Game1 process (PID 1234)
[14:30:20] Successfully injected ReShade into Game1 (PID 1234)
```

## Troubleshooting

### Common Issues

1. **"Failed to find ReShade DLL"**
   - Ensure ReShade32.dll and ReShade64.dll are in the same directory as the injector
   - Or specify custom path in configuration file

2. **"Process architecture does not match"**
   - Use 64-bit injector for 64-bit processes
   - Use 32-bit injector for 32-bit processes

3. **"Failed to open target application process"**
   - Run injector as administrator
   - Check if target process is running with elevated privileges

4. **Configuration not loading**
   - Check file format and encoding (should be UTF-8 or ANSI)
   - Ensure proper INI section headers
   - Verify file permissions

### Debug Mode

Enable verbose logging in configuration:
```ini
[Settings]
verbose_logging=true
```

This provides detailed information about:
- Process discovery
- Injection attempts
- Error details
- Process tracking updates

## License

Based on the original ReShade injector by Patrick Mours.
SPDX-License-Identifier: BSD-3-Clause

Enhanced version maintains the same license terms.
