#pragma once

#include <windows.h>
#include <gl/gl.h>
#include <atomic>

// OpenGL hook function pointer types
typedef BOOL (WINAPI *wglSwapBuffers_pfn)(HDC hdc);
typedef BOOL (WINAPI *wglMakeCurrent_pfn)(HDC hdc, HGLRC hglrc);
typedef HGLRC (WINAPI *wglCreateContext_pfn)(HDC hdc);
typedef BOOL (WINAPI *wglDeleteContext_pfn)(HGLRC hglrc);
typedef int (WINAPI *wglChoosePixelFormat_pfn)(HDC hdc, const PIXELFORMATDESCRIPTOR *ppfd);
typedef BOOL (WINAPI *wglSetPixelFormat_pfn)(HDC hdc, int iPixelFormat, const PIXELFORMATDESCRIPTOR *ppfd);
typedef int (WINAPI *wglGetPixelFormat_pfn)(HDC hdc);
typedef BOOL (WINAPI *wglDescribePixelFormat_pfn)(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd);
typedef HGLRC (WINAPI *wglCreateContextAttribsARB_pfn)(HDC hdc, HGLRC hshareContext, const int *attribList);
typedef BOOL (WINAPI *wglChoosePixelFormatARB_pfn)(HDC hdc, const int *piAttribIList, const FLOAT *pfAttribFList, UINT nMaxFormats, int *piFormats, UINT *nNumFormats);
typedef BOOL (WINAPI *wglGetPixelFormatAttribivARB_pfn)(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes, const int *piAttributes, int *piValues);
typedef BOOL (WINAPI *wglGetPixelFormatAttribfvARB_pfn)(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes, const int *piAttributes, FLOAT *pfValues);
typedef PROC (WINAPI *wglGetProcAddress_pfn)(LPCSTR lpszProc);
typedef BOOL (WINAPI *wglSwapIntervalEXT_pfn)(int interval);
typedef int (WINAPI *wglGetSwapIntervalEXT_pfn)(void);

// OpenGL hook counters (defined in globals.hpp)

// Original function pointers
extern wglSwapBuffers_pfn wglSwapBuffers_Original;
extern wglMakeCurrent_pfn wglMakeCurrent_Original;
extern wglCreateContext_pfn wglCreateContext_Original;
extern wglDeleteContext_pfn wglDeleteContext_Original;
extern wglChoosePixelFormat_pfn wglChoosePixelFormat_Original;
extern wglSetPixelFormat_pfn wglSetPixelFormat_Original;
extern wglGetPixelFormat_pfn wglGetPixelFormat_Original;
extern wglDescribePixelFormat_pfn wglDescribePixelFormat_Original;
extern wglCreateContextAttribsARB_pfn wglCreateContextAttribsARB_Original;
extern wglChoosePixelFormatARB_pfn wglChoosePixelFormatARB_Original;
extern wglGetPixelFormatAttribivARB_pfn wglGetPixelFormatAttribivARB_Original;
extern wglGetPixelFormatAttribfvARB_pfn wglGetPixelFormatAttribfvARB_Original;
extern wglGetProcAddress_pfn wglGetProcAddress_Original;
extern wglSwapIntervalEXT_pfn wglSwapIntervalEXT_Original;
extern wglGetSwapIntervalEXT_pfn wglGetSwapIntervalEXT_Original;

// Hook installation/uninstallation functions
bool InstallOpenGLHooks();
void UninstallOpenGLHooks();
bool AreOpenGLHooksInstalled();

// Hook detour functions
BOOL WINAPI wglSwapBuffers_Detour(HDC hdc);
BOOL WINAPI wglMakeCurrent_Detour(HDC hdc, HGLRC hglrc);
HGLRC WINAPI wglCreateContext_Detour(HDC hdc);
BOOL WINAPI wglDeleteContext_Detour(HGLRC hglrc);
int WINAPI wglChoosePixelFormat_Detour(HDC hdc, const PIXELFORMATDESCRIPTOR *ppfd);
BOOL WINAPI wglSetPixelFormat_Detour(HDC hdc, int iPixelFormat, const PIXELFORMATDESCRIPTOR *ppfd);
int WINAPI wglGetPixelFormat_Detour(HDC hdc);
BOOL WINAPI wglDescribePixelFormat_Detour(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd);
HGLRC WINAPI wglCreateContextAttribsARB_Detour(HDC hdc, HGLRC hshareContext, const int *attribList);
BOOL WINAPI wglChoosePixelFormatARB_Detour(HDC hdc, const int *piAttribIList, const FLOAT *pfAttribFList, UINT nMaxFormats, int *piFormats, UINT *nNumFormats);
BOOL WINAPI wglGetPixelFormatAttribivARB_Detour(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes, const int *piAttributes, int *piValues);
BOOL WINAPI wglGetPixelFormatAttribfvARB_Detour(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes, const int *piAttributes, FLOAT *pfValues);
PROC WINAPI wglGetProcAddress_Detour(LPCSTR lpszProc);
BOOL WINAPI wglSwapIntervalEXT_Detour(int interval);
int WINAPI wglGetSwapIntervalEXT_Detour(void);
