// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// DLL_PROCESS_ATTACH watchdog: surfaces a message box if DllMain attach does not return within the timeout.

void StartDllProcessAttachWatchdog();
void CancelDllProcessAttachWatchdog();
