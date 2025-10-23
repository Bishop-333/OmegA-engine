/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

This file provides stub implementations for game-specific functions
needed by the AI modules when building with the main engine.
===========================================================================
*/

#include "../../engine/common/q_shared.h"
#include "../../engine/common/trap_common.h"
#include "../../engine/core/qcommon.h"
#include "ai_main.h"
#include "ai_system.h"
#include "game_interface.h"
#include "game_entities.h"
// Include only existing headers
#include "tactical/tactical_combat.h"
#include "team/team_coordination.h"
#include "perception/ai_perception.h"
#include "learning/skill_adaptation.h"
#include "neural/nn_core.h"
#include <stdlib.h>
#include <string.h>

// Memory allocation functions - wrapper implementations for game module
// The game module can't directly call engine Z_Malloc, so we provide compatible wrappers
// Use different names to avoid macro conflicts
#ifdef Z_Malloc
#undef Z_Malloc
#endif
#ifdef Z_Free
#undef Z_Free
#endif

void *Z_Malloc(int size) {
    void *ptr = calloc(1, size);
    if (!ptr) {
        Com_Error(ERR_FATAL, "Z_Malloc: failed on allocation of %i bytes", size);
    }
    return ptr;
}

void Z_Free(void *ptr) {
    if (ptr) {
        free(ptr);
    }
}

// Cvar functions
cvar_t *Cvar_Get(const char *var_name, const char *var_value, int flags) {
    static cvar_t dummy_cvar;
    dummy_cvar.string = (char *)var_value;
    dummy_cvar.value = atof(var_value);
    dummy_cvar.integer = atoi(var_value);
    return &dummy_cvar;
}

// File system functions
fileHandle_t FS_FOpenFileWrite(const char *qpath) {
    return 0; // Return invalid handle for now
}

int FS_Write(const void *buffer, int len, fileHandle_t f) {
    return 0;
}

void FS_FCloseFile(fileHandle_t f) {
    // No-op
}

// Trace function for collision detection
void trap_Trace(trace_t *results, const vec3_t start, const vec3_t mins,
                const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask) {
    if (!results) return;
    
    // Simple stub - assume no collision
    memset(results, 0, sizeof(trace_t));
    results->fraction = 1.0f;
    VectorCopy(end, results->endpos);
    results->entityNum = ENTITYNUM_NONE;
    results->allsolid = qfalse;
    results->startsolid = qfalse;
}

// Bot AI functions
void BotAIStartFrame(int time) {
    // Stub implementation
}

// Navigation functions
nav_mesh_t *Nav_LoadMesh(const char *mapname) {
    // Stub - return NULL for now
    return NULL;
}

// Game interface functions are defined in ai_implementation.c
// Removed duplicates from here

// AI Entity update is defined in ai_implementation.c
// Removed duplicate from here

// Skill functions
void Skill_SaveProfile(skill_profile_t *profile, const char *filename) {
    // Stub - don't actually save
}

// Combat functions
void Combat_ExecuteState(tactical_combat_t *combat) {
    // Execute combat behaviors
}

// Team functions
void Team_RemoveMember(team_coordinator_t *coordinator, int member_id) {
    if (!coordinator) return;
    
    // Find and remove member
    for (int i = 0; i < coordinator->num_members; i++) {
        if (coordinator->members[i].client_id == member_id) {
            // Shift remaining members
            for (int j = i; j < coordinator->num_members - 1; j++) {
                coordinator->members[j] = coordinator->members[j + 1];
            }
            coordinator->num_members--;
            break;
        }
    }
}

// AI Decision functions are defined in ai_implementation.c
// Removed duplicates from here

// Combat AI functions are defined in ai_implementation.c
// Removed duplicates from here

// Navigation AI functions are defined in ai_implementation.c
// Removed duplicates from here

// Team AI functions are defined in ai_implementation.c
// Removed duplicate from here

// Learning functions are defined in ai_implementation.c
// Removed duplicates from here

// Debug functions stub - actual implementation is in ai_implementation.c
void AI_DebugDrawStub(bot_controller_t *bot) {
    // Draw debug information
}

// Additional helper functions
// Distance is already defined in q_shared.h, no need to redefine

// VectorDistance is defined in ai_implementation.c
// Removed duplicate from here

// Min and Max are already defined as macros in q_shared.h

// Print functions
void Com_Printf(const char *fmt, ...) {
    // Stub - could forward to actual Com_Printf if available
}

void Com_DPrintf(const char *fmt, ...) {
    // Debug print stub
}

void Com_Error(int code, const char *fmt, ...) {
    // Error handling stub
    exit(1);
}

// Math utilities
// CLAMP is already defined as a macro

// Random functions - random and crandom are already defined as macros in q_shared.h
// No need to redefine them

// Angle utilities
void AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up) {
    float angle;
    float sr, sp, sy, cr, cp, cy;
    
    angle = angles[YAW] * (M_PI * 2 / 360);
    sy = sin(angle);
    cy = cos(angle);
    angle = angles[PITCH] * (M_PI * 2 / 360);
    sp = sin(angle);
    cp = cos(angle);
    angle = angles[ROLL] * (M_PI * 2 / 360);
    sr = sin(angle);
    cr = cos(angle);
    
    if (forward) {
        forward[0] = cp * cy;
        forward[1] = cp * sy;
        forward[2] = -sp;
    }
    if (right) {
        right[0] = (-1 * sr * sp * cy + -1 * cr * -sy);
        right[1] = (-1 * sr * sp * sy + -1 * cr * cy);
        right[2] = -1 * sr * cp;
    }
    if (up) {
        up[0] = (cr * sp * cy + -sr * -sy);
        up[1] = (cr * sp * sy + -sr * cy);
        up[2] = cr * cp;
    }
}

// Entity access - g_entities is already declared in game_entities.h
// Only define level here
level_locals_t level = {0};  // level_locals_t is defined in game_entities.h

// Trap functions needed by AI
void trap_LinkEntity(gentity_t *ent) {
    // Stub - entity linking handled by engine
}

int trap_PointContents(const vec3_t point, int passEntityNum) {
    // Stub - return empty space
    return 0;
}