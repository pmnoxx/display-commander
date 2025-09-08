@echo off
echo Testing XInput hooks in Display Commander addon
echo.
echo This will help you check if XInput is being detected and hooked properly.
echo.
echo 1. Make sure you have a controller connected
echo 2. Load the addon in a game that supports XInput
echo 3. Check the ReShade log for XXX messages
echo.
echo Look for these messages in the log:
echo - "XXX Found XInput module: xinput1_4.dll at 0x..."
echo - "XXX Found XInputGetState in xinput1_4.dll at: 0x..."
echo - "XXX Found XInputGetStateEx (ordinal 100) in xinput1_4.dll at: 0x..."
echo - "XXX Successfully hooked XInputGetState in xinput1_4.dll"
echo - "XXX Successfully hooked XInputGetStateEx in xinput1_4.dll"
echo - "XXX XInput hooks installed successfully - hooked X XInput modules"
echo - "XXX TestXInputState: Controller 0 connected..."
echo - "XXX XInputGetState_Detour called" (when controller is used)
echo - "XXX XInputGetStateEx_Detour called" (when controller is used - more common)
echo.
echo If you don't see XInput calls, the game might be using:
echo - DirectInput instead of XInput
echo - A different input system
echo - XInput through a wrapper (like Steam Input)
echo.
echo Press any key to continue...
pause > nul
