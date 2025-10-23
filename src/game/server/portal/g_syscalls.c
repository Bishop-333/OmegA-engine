/*
===========================================================================
Portal System - Game Syscalls Implementation
Provides the actual trap function implementations for the portal system
===========================================================================
*/

#include "g_portal.h"
#include "../../../engine/common/q_shared.h"
#include "../../api/g_public.h"

static intptr_t (QDECL *syscall)( intptr_t arg, ... ) = (intptr_t (QDECL *)( intptr_t, ...))-1;

void dllEntry( intptr_t (QDECL *syscallptr)( intptr_t arg,... ) ) {
    syscall = syscallptr;
}

// Error handling
void G_Error( const char *fmt, ... ) {
    va_list argptr;
    char text[1024];
    
    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);
    
    syscall( G_ERROR, text );
}

// Print functions
void G_Printf( const char *fmt, ... ) {
    va_list argptr;
    char text[1024];
    
    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);
    
    syscall( G_PRINT, text );
}

void trap_Printf( const char *fmt ) {
    syscall( G_PRINT, fmt );
}

// Time functions
int trap_Milliseconds( void ) {
    return syscall( G_MILLISECONDS );
}

int trap_RealTime( qtime_t *qtime ) {
    return syscall( G_REAL_TIME, qtime );
}

// Entity management
void trap_LinkEntity( gentity_t *ent ) {
    syscall( G_LINKENTITY, ent );
}

void trap_UnlinkEntity( gentity_t *ent ) {
    syscall( G_UNLINKENTITY, ent );
}

// Collision detection
void trap_Trace( trace_t *results, const vec3_t start, const vec3_t mins, 
                const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask ) {
    syscall( G_TRACE, results, start, mins, maxs, end, passEntityNum, contentmask );
}

void trap_TraceCapsule( trace_t *results, const vec3_t start, const vec3_t mins,
                       const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask ) {
    syscall( G_TRACECAPSULE, results, start, mins, maxs, end, passEntityNum, contentmask );
}

int trap_PointContents( const vec3_t point, int passEntityNum ) {
    return syscall( G_POINT_CONTENTS, point, passEntityNum );
}

qboolean trap_InPVS( const vec3_t p1, const vec3_t p2 ) {
    return syscall( G_IN_PVS, p1, p2 );
}

qboolean trap_InPVSIgnorePortals( const vec3_t p1, const vec3_t p2 ) {
    return syscall( G_IN_PVS_IGNORE_PORTALS, p1, p2 );
}

void trap_AdjustAreaPortalState( gentity_t *ent, qboolean open ) {
    syscall( G_ADJUST_AREA_PORTAL_STATE, ent, open );
}

qboolean trap_AreasConnected( int area1, int area2 ) {
    return syscall( G_AREAS_CONNECTED, area1, area2 );
}

// Entity queries
int trap_EntitiesInBox( const vec3_t mins, const vec3_t maxs, int *list, int maxcount ) {
    return syscall( G_ENTITIES_IN_BOX, mins, maxs, list, maxcount );
}

qboolean trap_EntityContact( const vec3_t mins, const vec3_t maxs, const gentity_t *ent ) {
    return syscall( G_ENTITY_CONTACT, mins, maxs, ent );
}

qboolean trap_EntityContactCapsule( const vec3_t mins, const vec3_t maxs, const gentity_t *ent ) {
    return syscall( G_ENTITY_CONTACTCAPSULE, mins, maxs, ent );
}

// Math functions
void trap_SnapVector( float *v ) {
    syscall( G_SNAPVECTOR, v );
}

void SnapVector( float *v ) {
    trap_SnapVector(v);
}

void trap_AngleVectors( const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up ) {
    syscall( G_ANGLEVECTORS, angles, forward, right, up );
}

void AngleVectors( const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up ) {
    trap_AngleVectors(angles, forward, right, up);
}

void trap_MatrixMultiply( float in1[3][3], float in2[3][3], float out[3][3] ) {
    syscall( G_MATRIXMULTIPLY, in1, in2, out );
}

void trap_PerpendicularVector( vec3_t dst, const vec3_t src ) {
    syscall( G_PERPENDICULARVECTOR, dst, src );
}

// File system
int trap_FS_FOpenFile( const char *qpath, fileHandle_t *f, fsMode_t mode ) {
    return syscall( G_FS_FOPEN_FILE, qpath, f, mode );
}

void trap_FS_Read( void *buffer, int len, fileHandle_t f ) {
    syscall( G_FS_READ, buffer, len, f );
}

void trap_FS_Write( const void *buffer, int len, fileHandle_t f ) {
    syscall( G_FS_WRITE, buffer, len, f );
}

void trap_FS_FCloseFile( fileHandle_t f ) {
    syscall( G_FS_FCLOSE_FILE, f );
}

int trap_FS_GetFileList( const char *path, const char *extension, char *listbuf, int bufsize ) {
    return syscall( G_FS_GETFILELIST, path, extension, listbuf, bufsize );
}

int trap_FS_Seek( fileHandle_t f, long offset, int origin ) {
    return syscall( G_FS_SEEK, f, offset, origin );
}

// Commands and cvars
void trap_SendConsoleCommand( int exec_when, const char *text ) {
    syscall( G_SEND_CONSOLE_COMMAND, exec_when, text );
}

void trap_Cvar_Register( vmCvar_t *cvar, const char *var_name, const char *value, int flags ) {
    syscall( G_CVAR_REGISTER, cvar, var_name, value, flags );
}

void trap_Cvar_Update( vmCvar_t *cvar ) {
    syscall( G_CVAR_UPDATE, cvar );
}

void trap_Cvar_Set( const char *var_name, const char *value ) {
    syscall( G_CVAR_SET, var_name, value );
}

int trap_Cvar_VariableIntegerValue( const char *var_name ) {
    return syscall( G_CVAR_VARIABLE_INTEGER_VALUE, var_name );
}

void trap_Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize ) {
    syscall( G_CVAR_VARIABLE_STRING_BUFFER, var_name, buffer, bufsize );
}

// Server commands
void trap_SendServerCommand( int clientNum, const char *text ) {
    syscall( G_SEND_SERVER_COMMAND, clientNum, text );
}

void trap_SetConfigstring( int num, const char *string ) {
    syscall( G_SET_CONFIGSTRING, num, string );
}

void trap_GetConfigstring( int num, char *buffer, int bufferSize ) {
    syscall( G_GET_CONFIGSTRING, num, buffer, bufferSize );
}

void trap_GetUserinfo( int num, char *buffer, int bufferSize ) {
    syscall( G_GET_USERINFO, num, buffer, bufferSize );
}

void trap_SetUserinfo( int num, const char *buffer ) {
    syscall( G_SET_USERINFO, num, buffer );
}

void trap_GetServerinfo( char *buffer, int bufferSize ) {
    syscall( G_GET_SERVERINFO, buffer, bufferSize );
}

void trap_SetBrushModel( gentity_t *ent, const char *name ) {
    syscall( G_SET_BRUSH_MODEL, ent, name );
}

qboolean trap_GetEntityToken( char *buffer, int bufferSize ) {
    return syscall( G_GET_ENTITY_TOKEN, buffer, bufferSize );
}

void trap_GetUsercmd( int clientNum, usercmd_t *cmd ) {
    syscall( G_GET_USERCMD, clientNum, cmd );
}

// Client management
void trap_DropClient( int clientNum, const char *reason ) {
    syscall( G_DROP_CLIENT, clientNum, reason );
}

int trap_BotAllocateClient( void ) {
    return syscall( G_BOT_ALLOCATE_CLIENT );
}

void trap_BotFreeClient( int clientNum ) {
    syscall( G_BOT_FREE_CLIENT, clientNum );
}

// Debug functions
int trap_DebugPolygonCreate(int color, int numPoints, vec3_t *points) {
    return syscall( G_DEBUG_POLYGON_CREATE, color, numPoints, points );
}

void trap_DebugPolygonDelete(int id) {
    syscall( G_DEBUG_POLYGON_DELETE, id );
}

// Command line arguments
int trap_Argc( void ) {
    return syscall( G_ARGC );
}

void trap_Argv( int n, char *buffer, int bufferLength ) {
    syscall( G_ARGV, n, buffer, bufferLength );
}

// Locate game data - important for initialization
void trap_LocateGameData( gentity_t *gEnts, int numGEntities, int sizeofGEntity_t,
                         playerState_t *clients, int sizeofGClient_t ) {
    syscall( G_LOCATE_GAME_DATA, gEnts, numGEntities, sizeofGEntity_t, clients, sizeofGClient_t );
}