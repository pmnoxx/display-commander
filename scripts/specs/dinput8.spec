# Wine dinput8.spec - https://github.com/wine-mirror/wine/blob/master/dlls/dinput8/dinput8.spec
@ stdcall DirectInput8Create(long long ptr ptr ptr)
@ stdcall -private DllCanUnloadNow()
@ stdcall -private DllGetClassObject(ptr ptr ptr)
@ stdcall -private DllRegisterServer()
@ stdcall -private DllUnregisterServer()
