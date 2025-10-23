/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Game Interface Implementation for AI System
Bridges AI system with game entities and functions
===========================================================================
*/

#include "game_interface.h"
#include "ai_main.h"
#include "../../engine/common/q_shared.h"

// External functions from engine
extern void *Z_Malloc(int size);
extern void Z_Free(void *ptr);
extern void Com_Printf(const char *fmt, ...);
extern void Com_DPrintf(const char *fmt, ...);
extern void Com_Error(int code, const char *fmt, ...);
extern cvar_t *Cvar_Get(const char *name, const char *value, int flags);
extern int Sys_Milliseconds(void);
extern int FS_FOpenFileWrite(const char *filename, fileHandle_t *f);
extern void FS_Write(const void *buffer, int len, fileHandle_t f);
extern void FS_FCloseFile(fileHandle_t f);

// Math macros
#ifndef CLAMP
#define CLAMP(x, min, max) ((x) < (min) ? (min) : (x) > (max) ? (max) : (x))
#endif

#ifndef Min
#define Min(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifndef Max
#define Max(x, y) ((x) > (y) ? (x) : (y))
#endif

#ifndef Distance
#define Distance(a, b) VectorDistance((a), (b))
#endif
#include "../server/portal/g_portal.h"
#include "../server/portal/g_portal_integration.h"

// Import game globals from portal integration
extern gentity_t *g_entities;
extern level_locals_t level;

// Game locals wrapper
game_locals_t game;

// Navigation mesh structure
typedef struct nav_mesh_s {
    qboolean loaded;
    char mapname[MAX_QPATH];
    int numNodes;
    int numAreas;
    vec3_t *nodes;
    int *areas;
    float *costs;
} nav_mesh_t;

static nav_mesh_t *current_navmesh = NULL;

/*
==================
G_InitGameInterface

Initialize game interface connections
==================
*/
void G_InitGameInterface(void) {
    // Connect to game globals
    game.gentities = g_entities;
    game.time = level.time;
    game.previousTime = level.previousTime;
    game.framenum = level.framenum;
    game.startTime = level.startTime;
    game.maxclients = level.maxclients;
    game.clients = level.clients;
    
    Com_Printf("Game interface initialized\n");
}

/*
==================
G_ShutdownGameInterface

Cleanup game interface
==================
*/
void G_ShutdownGameInterface(void) {
    if (current_navmesh) {
        Nav_FreeMesh(current_navmesh);
        current_navmesh = NULL;
    }
}

/*
==================
BotAISetupClient

Setup bot AI for a client
==================
*/
int BotAISetupClient(int clientNum, const char *botname, int skill) {
    bot_controller_t *bot;
    gentity_t *ent;
    
    if (clientNum < 0 || clientNum >= MAX_CLIENTS) {
        return qfalse;
    }
    
    ent = &g_entities[clientNum];
    if (!ent || !ent->client) {
        return qfalse;
    }
    
    // Create bot controller using new AI system
    bot = AI_CreateBot(clientNum, botname, BOT_PERSONALITY_NORMAL);
    if (!bot) {
        return qfalse;
    }
    
    // Set skill level
    bot->skill_level = skill;
    
    // Initialize bot state
    VectorCopy(ent->client->ps.origin, bot->current_state.position);
    VectorCopy(ent->client->ps.viewangles, bot->current_state.view_angles);
    
    return qtrue;
}

/*
==================
BotAIShutdownClient

Shutdown bot AI for a client
==================
*/
void BotAIShutdownClient(int clientNum) {
    AI_RemoveBot(clientNum);
}

/*
==================
BotAIStartFrame

Run bot AI frame
==================
*/
void BotAIStartFrame(int time) {
    game.time = time;
    AI_Frame((float)time * 0.001f);
}

/*
==================
G_IsVisible

Check if two entities can see each other
==================
*/
qboolean G_IsVisible(gentity_t *ent1, gentity_t *ent2) {
    trace_t trace;
    vec3_t start, end;
    
    if (!ent1 || !ent2) {
        return qfalse;
    }
    
    // Use eye position for visibility checks
    VectorCopy(ent1->s.pos.trBase, start);
    start[2] += 24; // Eye height
    
    VectorCopy(ent2->s.pos.trBase, end);
    end[2] += 24;
    
    trap_Trace(&trace, start, NULL, NULL, end, ent1->s.number, MASK_SHOT);
    
    return (trace.fraction >= 1.0f || trace.entityNum == ent2->s.number);
}

/*
==================
G_Distance

Calculate distance between two entities
==================
*/
float G_Distance(gentity_t *ent1, gentity_t *ent2) {
    vec3_t diff;
    
    if (!ent1 || !ent2) {
        return 999999.0f;
    }
    
    VectorSubtract(ent2->s.pos.trBase, ent1->s.pos.trBase, diff);
    return VectorLength(diff);
}

/*
==================
Nav_LoadMesh

Load navigation mesh for map
==================
*/
nav_mesh_t *Nav_LoadMesh(const char *mapname) {
    nav_mesh_t *mesh;
    vec3_t mins, maxs;
    int gridSize = 32;
    int x, y, z;
    int nodeIndex = 0;
    
    if (current_navmesh && !Q_stricmp(current_navmesh->mapname, mapname)) {
        return current_navmesh;
    }
    
    // Free old mesh
    if (current_navmesh) {
        Nav_FreeMesh(current_navmesh);
    }
    
    // Allocate new mesh
    mesh = (nav_mesh_t *)Z_Malloc(sizeof(nav_mesh_t));
    Q_strncpyz(mesh->mapname, mapname, sizeof(mesh->mapname));
    
    // Generate simple grid-based navigation mesh
    // This is a simplified implementation - a real system would analyze map geometry
    VectorSet(mins, -4096, -4096, -512);
    VectorSet(maxs, 4096, 4096, 512);
    
    // Calculate number of nodes
    mesh->numNodes = ((maxs[0] - mins[0]) / gridSize) * 
                     ((maxs[1] - mins[1]) / gridSize) * 
                     ((maxs[2] - mins[2]) / gridSize);
    
    mesh->nodes = (vec3_t *)Z_Malloc(mesh->numNodes * sizeof(vec3_t));
    mesh->areas = (int *)Z_Malloc(mesh->numNodes * sizeof(int));
    mesh->costs = (float *)Z_Malloc(mesh->numNodes * mesh->numNodes * sizeof(float));
    
    // Generate grid nodes
    for (x = mins[0]; x < maxs[0]; x += gridSize) {
        for (y = mins[1]; y < maxs[1]; y += gridSize) {
            for (z = mins[2]; z < maxs[2]; z += gridSize) {
                if (nodeIndex < mesh->numNodes) {
                    VectorSet(mesh->nodes[nodeIndex], x, y, z);
                    mesh->areas[nodeIndex] = nodeIndex; // Simple area assignment
                    nodeIndex++;
                }
            }
        }
    }
    
    mesh->numAreas = nodeIndex;
    mesh->loaded = qtrue;
    
    current_navmesh = mesh;
    
    Com_Printf("Navigation mesh loaded for %s (%d nodes)\n", mapname, mesh->numNodes);
    
    return mesh;
}

/*
==================
Nav_FreeMesh

Free navigation mesh
==================
*/
void Nav_FreeMesh(nav_mesh_t *mesh) {
    if (!mesh) {
        return;
    }
    
    if (mesh->nodes) {
        Z_Free(mesh->nodes);
    }
    if (mesh->areas) {
        Z_Free(mesh->areas);
    }
    if (mesh->costs) {
        Z_Free(mesh->costs);
    }
    
    Z_Free(mesh);
}

/*
==================
Nav_PointAreaNum

Get area number for a point
==================
*/
int Nav_PointAreaNum(nav_mesh_t *mesh, vec3_t point) {
    int i;
    float bestDist = 999999;
    int bestArea = -1;
    
    if (!mesh || !mesh->loaded) {
        return -1;
    }
    
    // Find closest node
    for (i = 0; i < mesh->numNodes; i++) {
        vec3_t diff;
        float dist;
        
        VectorSubtract(point, mesh->nodes[i], diff);
        dist = VectorLength(diff);
        
        if (dist < bestDist) {
            bestDist = dist;
            bestArea = mesh->areas[i];
        }
    }
    
    return bestArea;
}

/*
==================
Nav_RouteToGoal

Find route from start to goal
==================
*/
qboolean Nav_RouteToGoal(nav_mesh_t *mesh, vec3_t start, vec3_t goal, 
                         vec3_t *waypoints, int *numWaypoints, int maxWaypoints) {
    int startArea, goalArea;
    vec3_t direction;
    float distance;
    int waypointCount = 0;
    
    if (!mesh || !mesh->loaded || !waypoints || !numWaypoints) {
        return qfalse;
    }
    
    startArea = Nav_PointAreaNum(mesh, start);
    goalArea = Nav_PointAreaNum(mesh, goal);
    
    if (startArea < 0 || goalArea < 0) {
        return qfalse;
    }
    
    // Simple direct path for now
    // A real implementation would use A* or similar pathfinding
    VectorSubtract(goal, start, direction);
    distance = VectorNormalize(direction);
    
    // Generate waypoints along direct path
    while (distance > 64 && waypointCount < maxWaypoints) {
        vec3_t waypoint;
        
        VectorMA(start, Min(distance, 128), direction, waypoint);
        VectorCopy(waypoint, waypoints[waypointCount]);
        waypointCount++;
        
        VectorCopy(waypoint, start);
        VectorSubtract(goal, start, direction);
        distance = VectorNormalize(direction);
    }
    
    // Add final waypoint at goal
    if (waypointCount < maxWaypoints) {
        VectorCopy(goal, waypoints[waypointCount]);
        waypointCount++;
    }
    
    *numWaypoints = waypointCount;
    
    return (waypointCount > 0);
}

/*
==================
Nav_AreaTravelTime

Calculate travel time between areas
==================
*/
float Nav_AreaTravelTime(nav_mesh_t *mesh, int startArea, int goalArea) {
    vec3_t diff;
    float distance;
    
    if (!mesh || !mesh->loaded) {
        return 999999;
    }
    
    if (startArea < 0 || startArea >= mesh->numAreas ||
        goalArea < 0 || goalArea >= mesh->numAreas) {
        return 999999;
    }
    
    VectorSubtract(mesh->nodes[goalArea], mesh->nodes[startArea], diff);
    distance = VectorLength(diff);
    
    // Assume movement speed of 320 units/second
    return distance / 320.0f;
}

/*
==================
Nav_Swimming

Check if point is in water
==================
*/
qboolean Nav_Swimming(nav_mesh_t *mesh, vec3_t point) {
    int contents;
    
    contents = trap_PointContents(point, -1);
    
    return (contents & (CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA)) != 0;
}

/*
==================
Nav_GroundTrace

Trace to ground from point
==================
*/
qboolean Nav_GroundTrace(nav_mesh_t *mesh, vec3_t start, vec3_t end, vec3_t groundPoint) {
    trace_t trace;
    vec3_t down;
    
    VectorCopy(start, down);
    down[2] = end[2] - 256; // Trace down
    
    trap_Trace(&trace, start, NULL, NULL, down, -1, MASK_SOLID);
    
    if (trace.fraction < 1.0f) {
        VectorCopy(trace.endpos, groundPoint);
        return qtrue;
    }
    
    return qfalse;
}

/*
==================
Stub implementations for missing functions
These need to be properly connected to game module
==================
*/

void G_SetClientViewAngle(gentity_t *ent, vec3_t angle) {
    if (ent && ent->client) {
        SetClientViewAngle(ent, angle);
    }
}

void G_SetOrigin(gentity_t *ent, vec3_t origin) {
    if (ent) {
        VectorCopy(origin, ent->s.pos.trBase);
        VectorCopy(origin, ent->r.currentOrigin);
        if (ent->client) {
            VectorCopy(origin, ent->client->ps.origin);
        }
    }
}

gentity_t *fire_rocket(gentity_t *self, vec3_t start, vec3_t dir) {
    // Stub - would fire actual rocket projectile
    return NULL;
}

gentity_t *fire_bfg(gentity_t *self, vec3_t start, vec3_t dir) {
    // Stub - would fire BFG projectile
    return NULL;
}

gentity_t *fire_grenade(gentity_t *self, vec3_t start, vec3_t dir) {
    // Stub - would fire grenade projectile
    return NULL;
}
/*
==================
AI_UpdateEntity

Update AI knowledge of an entity
==================
*/
void AI_UpdateEntity(int ent, bot_entitystate_t *state) {
    // Update perception system with entity state
    if (state && ent >= 0 && ent < MAX_GENTITIES) {
        // This would update the AI's perception of the entity
        // For now, just track basic info
    }
}
