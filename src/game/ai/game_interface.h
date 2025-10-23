/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Game Interface for AI System
Provides proper connections between AI and game entities
===========================================================================
*/

#ifndef GAME_INTERFACE_H
#define GAME_INTERFACE_H

#include "../../engine/common/q_shared.h"
#include "../shared/bg_public.h"
// g_public.h included via bg_public.h

// Entity system from portal integration
typedef struct gentity_s gentity_t;
typedef struct gclient_s gclient_t;

// Global game state
typedef struct {
    int time;
    int previousTime;
    int framenum;
    int startTime;
    int maxclients;
    gentity_t *gentities;
    gclient_t *clients;
} game_locals_t;

// External globals from game module
extern game_locals_t game;
extern gentity_t *g_entities;

// Entity management
gentity_t *G_Spawn(void);
void G_FreeEntity(gentity_t *ent);
gentity_t *G_Find(gentity_t *from, int fieldofs, const char *match);
gentity_t *G_PickTarget(char *targetname);
void G_UseTargets(gentity_t *ent, gentity_t *activator);
void G_SetMovedir(vec3_t angles, vec3_t movedir);

// Client management
void ClientSpawn(gentity_t *ent);
void ClientBegin(int clientNum);
void ClientDisconnect(int clientNum);
void ClientThink(int clientNum);
void ClientCommand(int clientNum);
char *ClientConnect(int clientNum, qboolean firstTime, qboolean isBot);
void ClientUserinfoChanged(int clientNum);

// Bot specific
int BotAISetupClient(int clientNum, const char *botname, int skill);
void BotAIShutdownClient(int clientNum);
void BotAIStartFrame(int time);

// Physics and movement
void G_Physics(gentity_t *ent, int msec);
void G_RunThink(gentity_t *ent);
void G_SetClientViewAngle(gentity_t *ent, vec3_t angle);
void G_SetOrigin(gentity_t *ent, vec3_t origin);

// Weapons and combat
void G_Damage(gentity_t *target, gentity_t *inflictor, gentity_t *attacker,
              vec3_t dir, vec3_t point, int damage, int dflags, int mod);
gentity_t *fire_plasma(gentity_t *self, vec3_t start, vec3_t dir);
gentity_t *fire_rocket(gentity_t *self, vec3_t start, vec3_t dir);
gentity_t *fire_bfg(gentity_t *self, vec3_t start, vec3_t dir);
gentity_t *fire_grenade(gentity_t *self, vec3_t start, vec3_t dir);

// Items and pickups
gitem_t *BG_FindItem(const char *pickupName);
gitem_t *BG_FindItemForWeapon(weapon_t weapon);
gitem_t *BG_FindItemForPowerup(powerup_t pw);
gitem_t *BG_FindItemForHoldable(holdable_t pw);
void G_SpawnItem(gentity_t *ent, gitem_t *item);
void Touch_Item(gentity_t *ent, gentity_t *other, trace_t *trace);

// Teams
team_t TeamCount(int ignoreClientNum, team_t team);
team_t PickTeam(int ignoreClientNum);
void SetTeam(gentity_t *ent, const char *s);
void Team_CheckDroppedItem(gentity_t *dropped);
void Team_DroppedFlagThink(gentity_t *ent);

// Utility functions
void TeleportPlayer(gentity_t *player, vec3_t origin, vec3_t angles);
qboolean G_IsVisible(gentity_t *ent1, gentity_t *ent2);
float G_Distance(gentity_t *ent1, gentity_t *ent2);
void G_Sound(gentity_t *ent, int channel, int soundIndex);
void G_AddEvent(gentity_t *ent, int event, int eventParm);

// Trace functions
void trap_Trace(trace_t *results, const vec3_t start, const vec3_t mins,
                const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask);
int trap_PointContents(const vec3_t point, int passEntityNum);
qboolean trap_InPVS(const vec3_t p1, const vec3_t p2);
qboolean trap_InPVSIgnorePortals(const vec3_t p1, const vec3_t p2);

// Configuration
void trap_GetConfigstring(int num, char *buffer, int bufferSize);
void trap_SetConfigstring(int num, const char *string);
void trap_GetUserinfo(int num, char *buffer, int bufferSize);
void trap_SetUserinfo(int num, const char *info);

// Server commands
void trap_SendServerCommand(int clientNum, const char *text);
void trap_SendConsoleCommand(int exec_when, const char *text);

// File system
int trap_FS_FOpenFile(const char *qpath, fileHandle_t *f, fsMode_t mode);
void trap_FS_Read(void *buffer, int len, fileHandle_t f);
void trap_FS_Write(const void *buffer, int len, fileHandle_t f);
void trap_FS_FCloseFile(fileHandle_t f);
int trap_FS_GetFileList(const char *path, const char *extension, char *listbuf, int bufsize);

// Cvars
void trap_Cvar_Register(vmCvar_t *vmCvar, const char *varName, const char *defaultValue, int flags);
void trap_Cvar_Update(vmCvar_t *vmCvar);
void trap_Cvar_Set(const char *var_name, const char *value);
int trap_Cvar_VariableIntegerValue(const char *var_name);
float trap_Cvar_VariableValue(const char *var_name);
void trap_Cvar_VariableStringBuffer(const char *var_name, char *buffer, int bufsize);

// Memory
void *G_Alloc(int size);
void G_InitMemory(void);
void G_Free(void *ptr);

// Time
int trap_Milliseconds(void);

// Navigation interface (replaces AAS)
typedef struct nav_mesh_s nav_mesh_t;

nav_mesh_t *Nav_LoadMesh(const char *mapname);
void Nav_FreeMesh(nav_mesh_t *mesh);
int Nav_PointAreaNum(nav_mesh_t *mesh, vec3_t point);
qboolean Nav_RouteToGoal(nav_mesh_t *mesh, vec3_t start, vec3_t goal, vec3_t *waypoints, int *numWaypoints, int maxWaypoints);
float Nav_AreaTravelTime(nav_mesh_t *mesh, int startArea, int goalArea);
qboolean Nav_Swimming(nav_mesh_t *mesh, vec3_t point);
qboolean Nav_GroundTrace(nav_mesh_t *mesh, vec3_t start, vec3_t end, vec3_t groundPoint);

#endif // GAME_INTERFACE_H