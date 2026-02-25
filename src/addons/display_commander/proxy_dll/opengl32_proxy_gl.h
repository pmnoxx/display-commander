#pragma once

/*
 * Minimal OpenGL / WGL types for opengl32 proxy only.
 * Do not include system GL/gl.h here (would pull in opengl32.lib).
 * Types must match Windows opengl32.dll ABI.
 */

#include <Windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef double GLdouble;
typedef float GLclampf;
typedef double GLclampd;
typedef char GLchar;

typedef ptrdiff_t GLintptr;
typedef size_t GLsizeiptr;

#ifdef __cplusplus
}
#endif
