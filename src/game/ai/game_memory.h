/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Memory management functions for AI System
===========================================================================
*/

#ifndef GAME_MEMORY_H
#define GAME_MEMORY_H

#include "../../engine/common/q_shared.h"

// Memory allocation functions are provided by the engine via qcommon.h
// Z_Malloc and Z_Free are available through engine includes

// Forward declarations - avoid redefining existing structures
typedef struct cvar_s cvar_t;
cvar_t *Cvar_Get(const char *var_name, const char *var_value, int flags);

// Debug output
void Com_DPrintf(const char *fmt, ...);
void Com_Printf(const char *fmt, ...);

// File system functions
typedef int fileHandle_t;
fileHandle_t FS_FOpenFileWrite(const char *qpath);
int FS_Write(const void *buffer, int len, fileHandle_t f);
void FS_FCloseFile(fileHandle_t f);
int FS_FOpenFileRead(const char *qpath, fileHandle_t *f, qboolean uniqueFILE);
int FS_Read(void *buffer, int len, fileHandle_t f);

// Utility functions
int Sys_Milliseconds(void);

// Math utilities
#ifndef CLAMP
#define CLAMP(x, min, max) ((x) < (min) ? (min) : (x) > (max) ? (max) : (x))
#endif

#endif // GAME_MEMORY_H