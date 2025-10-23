/*
===========================================================================
Portal System for Quake3e-HD
Portal-style gameplay implementation
===========================================================================
*/

#ifndef G_PORTAL_H
#define G_PORTAL_H

#include "../../../engine/common/q_shared.h"
#include "../../api/g_public.h"

// Portal system constants
#define MAX_PORTAL_PAIRS 8
#define PORTAL_RADIUS 32.0f
#define PORTAL_ACTIVATION_TIME 500
#define PORTAL_CLOSE_TIME 300
#define FALL_DAMAGE_IMMUNITY_TIME 5000

typedef enum {
    PORTAL_ORANGE = 0,
    PORTAL_BLUE = 1,
    PORTAL_MAX_TYPES
} portalType_t;

typedef enum {
    PORTAL_STATE_INACTIVE = 0,
    PORTAL_STATE_OPENING,
    PORTAL_STATE_ACTIVE,
    PORTAL_STATE_CLOSING,
    PORTAL_STATE_CLOSED
} portalState_t;

typedef struct portalInfo_s {
    qboolean        inUse;
    portalType_t    type;
    portalState_t   state;
    
    int             entityNum;
    int             linkedPortalNum;
    int             ownerNum;
    
    vec3_t          origin;
    vec3_t          surfaceNormal;
    vec3_t          portalForward;
    vec3_t          portalRight;
    vec3_t          portalUp;
    
    float           radius;
    int             creationTime;
    int             stateChangeTime;
    
    matrix3_t       rotationMatrix;
    vec3_t          viewOffset;
} portalInfo_t;

typedef struct playerPortalState_s {
    int             lastPortalExitTime;
    int             fallDamageImmunityEndTime;
    vec3_t          lastPortalExitVelocity;
    int             activeOrangePortal;
    int             activeBluePortal;
} playerPortalState_t;

extern portalInfo_t g_portals[MAX_PORTAL_PAIRS * 2];
extern playerPortalState_t g_playerPortalStates[MAX_CLIENTS];

// Main portal functions
void G_InitPortalSystem(void);
void G_ShutdownPortalSystem(void);
void G_UpdatePortalSystem(void);
void G_FirePortal(gentity_t *player, portalType_t type);
void G_ClosePlayerPortals(gentity_t *player);
void G_CreatePortal(vec3_t origin, vec3_t normal, gentity_t *owner, portalType_t type);
void G_RemovePortal(int portalNum);
void G_UpdatePortal(gentity_t *portal);
void G_PortalThink(gentity_t *portal);
qboolean G_CheckPortalTeleport(gentity_t *ent, gentity_t *portal);
void G_TeleportThroughPortal(gentity_t *ent, gentity_t *enterPortal, gentity_t *exitPortal);
void G_TransformVelocityThroughPortal(vec3_t velocity, vec3_t enterNormal, vec3_t exitNormal);
qboolean G_TracePortalSurface(vec3_t start, vec3_t end, vec3_t outOrigin, vec3_t outNormal);
qboolean G_IsValidPortalSurface(trace_t *trace);
void G_PortalTouch(gentity_t *portal, gentity_t *other, trace_t *trace);
void G_PortalProjectileImpact(gentity_t *projectile, trace_t *trace, portalType_t type);
qboolean G_TraceThroughPortals(vec3_t start, vec3_t end, trace_t *trace, int passEntityNum);
void G_AddPortalCommands(void);

// Fixed portal functions (improved implementation)
void G_FirePortalFixed(gentity_t *player, portalType_t type);
void G_CreatePortalFixed(vec3_t origin, vec3_t normal, gentity_t *owner, portalType_t type);
void G_RemovePortalFixed(int portalNum);
void G_TeleportThroughPortalFixed(gentity_t *ent, gentity_t *enterPortal, gentity_t *exitPortal);

// Utility functions
float Distance(const vec3_t p1, const vec3_t p2);
void vectoangles(const vec3_t value1, vec3_t angles);
void SetClientViewAngle(gentity_t *ent, vec3_t angle);
qboolean G_ValidatePortalPlacement(vec3_t origin, vec3_t normal, float radius);
qboolean G_FindNearestPortalSurface(vec3_t point, vec3_t normal, vec3_t outOrigin, vec3_t outNormal);
void G_PortalViewTransform(vec3_t viewOrigin, vec3_t viewAngles, 
                          portalInfo_t *enterPortal, portalInfo_t *exitPortal,
                          vec3_t outOrigin, vec3_t outAngles);
qboolean G_CheckPortalCollision(vec3_t origin, vec3_t mins, vec3_t maxs, portalInfo_t *portal);
void G_DebugDrawPortal(portalInfo_t *portal);

// Portal rendering and debug
void G_UpdatePortalRendering(void);
void G_DrawPortalDebugInfo(void);
void G_DebugPortalSystem(void);
void G_PrintPortalStats(void);
void G_ProcessPortalCommands(gentity_t *ent, usercmd_t *ucmd);
qboolean G_CheckPortalFallDamageImmunity(gentity_t *ent);

// Entity management (provided by game module)
gentity_t *G_Spawn(void);
void G_FreeEntity(gentity_t *ent);
gentity_t *G_TempEntity(vec3_t origin, int event);
void G_SetOrigin(gentity_t *ent, vec3_t origin);

// Client management
char *ClientConnect(int clientNum, qboolean firstTime, qboolean isBot);
void ClientBegin(int clientNum);
void ClientDisconnect(int clientNum);
void ClientThink(int clientNum);
void ClientUserinfoChanged(int clientNum);
void ClientCommand(int clientNum);
qboolean ConsoleCommand(void);

// Trap functions (provided by syscalls)
extern void trap_Printf(const char *fmt);
extern void trap_Error(const char *fmt);
extern void G_Printf(const char *fmt, ...);
extern void G_Error(const char *fmt, ...);
extern void trap_Trace(trace_t *results, const vec3_t start, const vec3_t mins, 
                       const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask);
extern void trap_LinkEntity(gentity_t *ent);
extern void trap_UnlinkEntity(gentity_t *ent);
extern int trap_EntitiesInBox(const vec3_t mins, const vec3_t maxs, int *list, int maxcount);
extern int trap_PointContents(const vec3_t point, int passEntityNum);
extern void trap_SnapVector(float *v);
extern void SnapVector(float *v);
extern void trap_AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);
extern void AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);
extern int trap_Milliseconds(void);
extern int trap_RealTime(qtime_t *qtime);
extern void trap_SendServerCommand(int clientNum, const char *text);
extern void trap_GetUsercmd(int clientNum, usercmd_t *cmd);

// Button definitions for portal control
#define BUTTON_PORTAL_ORANGE    0x1000
#define BUTTON_PORTAL_BLUE      0x2000
#define BUTTON_PORTAL_CLOSE     0x4000

// Math utilities
void MatrixMultiply(float in1[3][3], float in2[3][3], float out[3][3]);
void PerpendicularVector(vec3_t dst, const vec3_t src);
void ProjectPointOnPlane(vec3_t dst, const vec3_t p, const vec3_t normal);
void RotatePointAroundVector(vec3_t dst, const vec3_t dir, const vec3_t point, float degrees);
float vectoyaw(const vec3_t vec);
void AxisToAngles(vec3_t axis, vec3_t angles);
int ANGLE2SHORT(float x);

// Info string utilities
char *Info_ValueForKey(const char *s, const char *key);

// Global declarations
extern gentity_t g_entities[MAX_GENTITIES];
extern level_locals_t level;
extern gclient_t g_clients[MAX_CLIENTS];

#endif