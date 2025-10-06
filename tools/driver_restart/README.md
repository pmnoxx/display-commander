# Driver Restart Tool

A C++ utility for restarting graphics drivers on Windows systems. This tool is a modernized version of the CRU restart utility, rewritten in C++ with improved error handling and a cleaner architecture.

## Features

- **Graphics Driver Restart**: Safely disable and re-enable graphics drivers using Windows SetupAPI
- **Process Management**: Automatically stops and restarts graphics control panel processes
- **Window Position Preservation**: Saves and restores window positions during driver restart
- **Command Line Interface**: Supports quiet, verbose, and interactive modes
- **WOW64 Support**: Automatically launches 64-bit version when running on 32-bit Windows
- **Error Handling**: Comprehensive error checking and user-friendly error messages

## Usage

```cmd
driver_restart.exe [options]
```

### Options

- `/q`, `-q` - Quiet mode (no prompts or dialogs)
- `/v`, `-v` - Verbose mode (detailed output)
- `/h`, `-h` - Show help message

### Examples

```cmd
# Interactive mode (shows confirmation dialog)
driver_restart.exe

# Quiet mode (no prompts)
driver_restart.exe /q

# Verbose mode (detailed output)
driver_restart.exe /v
```

## Requirements

- Windows 7 or later
- Administrator privileges
- Visual C++ Redistributable (for the compiled executable)

## Building

The tool is built using CMake as part of the main project:

```cmd
mkdir build
cd build
cmake ..
cmake --build . --target driver_restart
```

## Architecture

The tool is organized into several classes:

- **ProcessManager**: Handles process termination and execution
- **WindowManager**: Manages window position saving and restoration
- **DriverManager**: Controls graphics driver state using SetupAPI
- **Utils**: Utility functions for system detection and user interaction

## Safety Features

- Desktop readiness check before proceeding
- Window position preservation to maintain user experience
- Graceful error handling with informative messages
- Process cleanup to prevent conflicts

## Limitations

- Requires administrator privileges
- May cause temporary screen flicker or black screen
- Only works with Windows graphics drivers
- Does not modify registry settings (Intel/AMD specific features removed)

## License

This tool is part of the ReShade Display Commander project and follows the same licensing terms.
