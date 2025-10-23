/*
===========================================================================
Portal System Trap Functions
Provides the interface between the portal game code and the engine
===========================================================================
*/

#include "g_portal.h"
#include "../../../engine/common/q_shared.h"
#include "../../../engine/renderer/core/tr_public.h"

// External engine functions we need to call
extern void CM_BoxTrace(trace_t *results, const vec3_t start, const vec3_t end,
                       const vec3_t mins, const vec3_t maxs, clipHandle_t model,
                       int brushmask, int capsule);
extern void SV_LinkEntity(sharedEntity_t *ent);
extern void SV_UnlinkEntity(sharedEntity_t *ent);
extern int Com_Printf(const char *fmt, ...);

// Implementation of trap functions for portal system

/*
================
trap_Trace

Performs a collision trace in the world
================
*/
void trap_Trace(trace_t *results, const vec3_t start, const vec3_t mins, 
                const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask) {
    if (!results) {
        return;
    }
    
    // Initialize trace results
    memset(results, 0, sizeof(trace_t));
    results->fraction = 1.0f;
    results->entityNum = ENTITYNUM_NONE;
    
    // For now, do a simple trace that checks basic collision
    // In a real implementation, this would call into the engine's collision system
    vec3_t dir;
    VectorSubtract(end, start, dir);
    float length = VectorLength(dir);
    
    if (length > 0) {
        VectorNormalize(dir);
        
        // Simple wall check - this is a placeholder
        // Real implementation would use CM_BoxTrace or similar
        if (contentmask & MASK_SOLID) {
            // Check if we hit a wall (simplified check)
            if (fabs(end[0]) > 1000 || fabs(end[1]) > 1000) {
                results->fraction = 0.5f;  // Hit something halfway
                VectorMA(start, results->fraction * length, dir, results->endpos);
                VectorSet(results->plane.normal, -dir[0], -dir[1], -dir[2]);
                results->surfaceFlags = SURF_NODRAW;
                results->contents = CONTENTS_SOLID;
            } else {
                results->fraction = 1.0f;  // Didn't hit anything
                VectorCopy(end, results->endpos);
            }
        } else {
            results->fraction = 1.0f;
            VectorCopy(end, results->endpos);
        }
    }
}

/*
================
trap_LinkEntity

Links an entity into the world for collision detection
================
*/
void trap_LinkEntity(gentity_t *ent) {
    if (!ent || !ent->inuse) {
        return;
    }
    
    // Update entity shared state
    ent->r.linked = qtrue;
    ent->r.linkcount++;
    
    // In a real implementation, this would call SV_LinkEntity
    // to add the entity to the world's collision detection system
    
    // For now, just mark it as linked
    ent->r.svFlags |= SVF_BROADCAST;
}

/*
================
trap_UnlinkEntity

Unlinks an entity from the world
================
*/
void trap_UnlinkEntity(gentity_t *ent) {
    if (!ent || !ent->r.linked) {
        return;
    }
    
    ent->r.linked = qfalse;
    
    // In a real implementation, this would call SV_UnlinkEntity
}

/*
================
trap_EntitiesInBox

Find all entities within a bounding box
================
*/
int trap_EntitiesInBox(const vec3_t mins, const vec3_t maxs, int *entityList, int maxcount) {
    int i, count = 0;
    gentity_t *ent;
    vec3_t entMins, entMaxs;
    
    if (!entityList || maxcount <= 0) {
        return 0;
    }
    
    // Check all entities
    for (i = 0; i < MAX_GENTITIES && count < maxcount; i++) {
        ent = &g_entities[i];
        
        if (!ent->inuse) {
            continue;
        }
        
        // Get entity bounds
        VectorAdd(ent->s.origin, ent->r.mins, entMins);
        VectorAdd(ent->s.origin, ent->r.maxs, entMaxs);
        
        // Check for intersection
        if (mins[0] <= entMaxs[0] && maxs[0] >= entMins[0] &&
            mins[1] <= entMaxs[1] && maxs[1] >= entMins[1] &&
            mins[2] <= entMaxs[2] && maxs[2] >= entMins[2]) {
            entityList[count++] = i;
        }
    }
    
    return count;
}

/*
================
trap_PointContents

Returns the contents flags at a point
================
*/
int trap_PointContents(const vec3_t point, int passEntityNum) {
    // Simplified implementation
    // In reality, this would check the BSP and entity contents
    
    // Check for water/lava/slime (simplified)
    if (point[2] < -100) {
        return CONTENTS_WATER;
    }
    
    // Check for solid (simplified - walls at boundaries)
    if (fabs(point[0]) > 1000 || fabs(point[1]) > 1000 || fabs(point[2]) > 1000) {
        return CONTENTS_SOLID;
    }
    
    return 0;  // Empty space
}

/*
================
G_Printf

Print function wrapper for portal system
================
*/
void G_Printf(const char *fmt, ...) {
    va_list argptr;
    char text[1024];
    
    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);
    
    ri.Printf(PRINT_ALL, "%s", text);
}

/*
================
G_Error

Error function for portal system
================
*/
void G_Error(const char *fmt, ...) {
    va_list argptr;
    char text[1024];
    
    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);
    
    ri.Error(ERR_DROP, "%s", text);
}

/*
================
trap_Milliseconds

Get current time in milliseconds
================
*/
int trap_Milliseconds(void) {
    return ri.Milliseconds();
}

/*
================
trap_RealTime

Get real time
================
*/
int trap_RealTime(qtime_t *qtime) {
    return ri.Com_RealTime(qtime);
}

/*
================
trap_SnapVector

Snap vector components to integers
================
*/
void trap_SnapVector(float *v) {
    v[0] = (int)v[0];
    v[1] = (int)v[1];
    v[2] = (int)v[2];
}

/*
================
SnapVector

Compatibility wrapper
================
*/
void SnapVector(float *v) {
    trap_SnapVector(v);
}

/*
================
AngleVectors

Calculate forward, right, and up vectors from angles
================
*/
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

/*
================
ANGLE2SHORT

Convert angle to short for network transmission
================
*/
int ANGLE2SHORT(float x) {
    return ((int)((x) * 65536.0f / 360.0f) & 65535);
}