/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

AI System Common Declarations
Provides external function declarations and utilities
===========================================================================
*/

#ifndef AI_SYSTEM_H
#define AI_SYSTEM_H

#include "../../engine/common/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

// Memory allocation functions are provided by engine (qcommon.h)
// Z_Malloc and Z_Free are included via q_shared.h/qcommon.h

// Console output functions
extern void Com_Printf(const char *fmt, ...);
extern void Com_DPrintf(const char *fmt, ...);
extern void Com_Error(int code, const char *fmt, ...);

// Console variable functions
typedef struct cvar_s cvar_t;
extern cvar_t *Cvar_Get(const char *var_name, const char *var_value, int flags);
extern void Cvar_Set(const char *var_name, const char *value);
extern float Cvar_VariableValue(const char *var_name);
extern int Cvar_VariableIntegerValue(const char *var_name);
extern void Cvar_VariableStringBuffer(const char *var_name, char *buffer, int bufsize);

// File system functions
typedef int fileHandle_t;
extern int FS_FOpenFileWrite(const char *qpath, fileHandle_t *f);
extern int FS_FOpenFileRead(const char *qpath, fileHandle_t *f, qboolean uniqueFILE);
extern int FS_Write(const void *buffer, int len, fileHandle_t f);
extern int FS_Read(void *buffer, int len, fileHandle_t f);
extern void FS_FCloseFile(fileHandle_t f);
extern int FS_Seek(fileHandle_t f, long offset, int origin);

// File seek origins
#define FS_SEEK_CUR 1
#define FS_SEEK_SET 0
#define FS_SEEK_END 2

// System time
extern int Sys_Milliseconds(void);

// Info string functions
const char *Info_ValueForKey(const char *s, const char *key);

// Math utilities
#ifndef CLAMP
#define CLAMP(x, min, max) ((x) < (min) ? (min) : (x) > (max) ? (max) : (x))
#endif

#ifndef Min
#define Min(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifndef Max
#define Max(x, y) ((x) > (y) ? (x) : (y))
#endif

// String functions
#ifndef Q_strncpyz
void Q_strncpyz(char *dest, const char *src, int destsize);
#endif

#ifndef Q_stricmp
int Q_stricmp(const char *s1, const char *s2);
#endif

// Random number generation
#ifndef random
float random(void);
#endif

// Vector operations
#ifndef Distance
float Distance(const vec3_t p1, const vec3_t p2);
#endif

#ifndef VectorCopy
#define VectorCopy(a,b) ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2])
#endif

#ifndef VectorLength
float VectorLength(const vec3_t v);
#endif

#ifndef VectorNormalize
float VectorNormalize(vec3_t v);
#endif

#ifndef DotProduct
#define DotProduct(x,y) ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#endif

#ifndef CrossProduct
#define CrossProduct(v1,v2,cross) \
    ((cross)[0]=(v1)[1]*(v2)[2]-(v1)[2]*(v2)[1], \
     (cross)[1]=(v1)[2]*(v2)[0]-(v1)[0]*(v2)[2], \
     (cross)[2]=(v1)[0]*(v2)[1]-(v1)[1]*(v2)[0])
#endif

// Angle conversion
#ifndef ANGLE2SHORT
#define ANGLE2SHORT(x) ((int)((x)*65536/360) & 65535)
#endif

#ifndef SHORT2ANGLE
#define SHORT2ANGLE(x) ((x)*(360.0/65536))
#endif

// Angle vectors
void AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);

// Constants
#ifndef MAX_NAME_LENGTH
#define MAX_NAME_LENGTH 36
#endif

#ifndef MAX_QPATH
#define MAX_QPATH 64
#endif

#ifndef MAX_STRING_CHARS
#define MAX_STRING_CHARS 1024
#endif

// Game constants
#ifndef MAX_CLIENTS
#define MAX_CLIENTS 64
#endif

#ifndef MAX_ITEMS
#define MAX_ITEMS 256
#endif

#ifndef MAX_WEAPONS
#define MAX_WEAPONS 16
#endif

// Button definitions
#ifndef BUTTON_ATTACK
#define BUTTON_ATTACK       1
#endif

#ifndef BUTTON_USE
#define BUTTON_USE          32
#endif

#ifndef BUTTON_GESTURE
#define BUTTON_GESTURE      8
#endif

// Portal buttons
#ifndef BUTTON_PORTAL_ORANGE
#define BUTTON_PORTAL_ORANGE 64
#endif

#ifndef BUTTON_PORTAL_BLUE
#define BUTTON_PORTAL_BLUE   128
#endif

// Powerup indices are already defined in bg_public.h

// Weapon indices are already defined in bg_public.h

// Player stats are already defined in bg_public.h

// Config string indices
#ifndef CS_PLAYERS
#define CS_PLAYERS 544
#endif

#ifndef CS_ITEMS
#define CS_ITEMS 27
#endif

#ifndef CS_SCORES1
#define CS_SCORES1 6
#endif

#ifndef CS_SCORES2
#define CS_SCORES2 7
#endif

// Team definitions are already defined in bg_public.h

#ifdef __cplusplus
}
#endif

#endif // AI_SYSTEM_H