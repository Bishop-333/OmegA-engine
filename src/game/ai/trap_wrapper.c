/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Trap wrapper functions for AI system
===========================================================================
*/

#include "../../engine/common/q_shared.h"
// server.h is not accessible from game module
#include "game_entities.h"

// The actual engine trap function pointer (would be set by engine)
static void (*trap_Trace_Engine)(trace_t *results, const vec3_t start, const vec3_t mins, 
                                  const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask) = NULL;

// Forward declarations - these are implemented below
void trap_Trace(trace_t *results, const vec3_t start, const vec3_t mins, 
                const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask);
void trap_LinkEntity(gentity_t *ent);
int trap_PointContents(const vec3_t point, int passEntityNum);

/*
==================
trap_Trace

Wrapper for engine trace function used by AI systems
==================
*/
void trap_Trace(trace_t *results, const vec3_t start, const vec3_t mins, 
                const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask) {
    // If we have access to the engine's trap function, use it
    if (trap_Trace_Engine) {
        trap_Trace_Engine(results, start, mins, maxs, end, passEntityNum, contentmask);
        return;
    }
    
    // Fallback implementation for testing/development
    // This performs a simple line trace without actual collision detection
    vec3_t delta;
    float fraction = 1.0f;
    
    // Initialize trace result
    memset(results, 0, sizeof(trace_t));
    results->fraction = 1.0f;
    results->entityNum = ENTITYNUM_NONE;
    VectorCopy(end, results->endpos);
    
    // Calculate ray direction
    VectorSubtract(end, start, delta);
    
    // Simple distance-based check (would be replaced with actual collision in engine)
    float distance = VectorLength(delta);
    
    // For AI testing, we simulate some basic collision scenarios
    // This helps the AI system function even without full engine integration
    
    // Ground check (downward traces)
    if (end[2] < start[2] && (contentmask & CONTENTS_SOLID)) {
        // Simulate ground at z=0 for basic movement
        if (end[2] <= 0 && start[2] > 0) {
            fraction = -start[2] / (end[2] - start[2]);
            results->fraction = fraction;
            VectorMA(start, fraction, delta, results->endpos);
            results->endpos[2] = 0; // Ground level
            
            // Set surface normal for ground
            VectorSet(results->plane.normal, 0, 0, 1);
            results->surfaceFlags = SURF_NODAMAGE;
            results->contents = CONTENTS_SOLID;
        }
    }
    
    // Wall check (horizontal traces)
    if (contentmask & CONTENTS_SOLID) {
        // Simulate walls at map boundaries for testing
        const float MAP_SIZE = 4096.0f;
        
        // Check X boundaries
        if (fabsf(end[0]) > MAP_SIZE) {
            float boundary = (end[0] > 0) ? MAP_SIZE : -MAP_SIZE;
            fraction = (boundary - start[0]) / (end[0] - start[0]);
            if (fraction < results->fraction && fraction > 0) {
                results->fraction = fraction;
                VectorMA(start, fraction, delta, results->endpos);
                results->endpos[0] = boundary;
                VectorSet(results->plane.normal, (end[0] > 0) ? -1 : 1, 0, 0);
                results->contents = CONTENTS_SOLID;
            }
        }
        
        // Check Y boundaries
        if (fabsf(end[1]) > MAP_SIZE) {
            float boundary = (end[1] > 0) ? MAP_SIZE : -MAP_SIZE;
            fraction = (boundary - start[1]) / (end[1] - start[1]);
            if (fraction < results->fraction && fraction > 0) {
                results->fraction = fraction;
                VectorMA(start, fraction, delta, results->endpos);
                results->endpos[1] = boundary;
                VectorSet(results->plane.normal, 0, (end[1] > 0) ? -1 : 1, 0);
                results->contents = CONTENTS_SOLID;
            }
        }
    }
    
    // Shot traces (for line of sight checks)
    if (contentmask & MASK_SHOT) {
        // For now, assume clear line of sight within reasonable range
        if (distance < 2000.0f) {
            // 90% chance of clear sight for testing
            if (random() < 0.9f) {
                results->fraction = 1.0f;
                VectorCopy(end, results->endpos);
            } else {
                // Simulate obstruction
                results->fraction = 0.5f + random() * 0.4f;
                VectorMA(start, results->fraction, delta, results->endpos);
                results->contents = CONTENTS_SOLID;
            }
        }
    }
    
    // Calculate surface properties if we hit something
    if (results->fraction < 1.0f) {
        results->startsolid = qfalse;
        results->allsolid = qfalse;
        
        // Ensure normal is normalized
        VectorNormalize(results->plane.normal);
        
        // Calculate plane distance
        results->plane.dist = DotProduct(results->endpos, results->plane.normal);
        results->plane.type = 0; // Axial plane
        
        // Set basic surface flags
        if (results->plane.normal[2] > 0.7f) {
            results->surfaceFlags |= SURF_NODAMAGE; // Floor
        } else if (results->plane.normal[2] < -0.7f) {
            results->surfaceFlags |= SURF_SKY; // Ceiling
        }
    }
}

/*
==================
trap_LinkEntity

Links an entity back into the world for collision detection
This is a stub for AI compilation - actual implementation is in the engine
==================
*/
void trap_LinkEntity(gentity_t *ent) {
    // This is a stub function for compilation
    // The actual linking is handled by the engine's SV_LinkEntity
    // When integrated with the full game, this would make a syscall:
    // syscall(G_LINKENTITY, ent);
    
    // For now, just validate the entity
    if (ent && ent->inuse) {
        // Entity is valid and would be linked
        // Actual linking happens in the engine
    }
}

/*
==================
trap_PointContents

Returns the contents at a given point
==================
*/
int trap_PointContents(const vec3_t point, int passEntityNum) {
    // Basic implementation for AI testing
    // Check if point is below ground level
    if (point[2] < 0) {
        return CONTENTS_SOLID;
    }
    
    // Check if point is in water (arbitrary water level for testing)
    if (point[2] < 64 && point[2] > 0) {
        // Could return CONTENTS_WATER in certain map areas
    }
    
    // Default to empty space
    return 0;
}