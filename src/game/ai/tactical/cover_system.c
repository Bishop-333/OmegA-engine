/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

This file is part of Quake3e-HD source code.

Quake3e-HD source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake3e-HD source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
===========================================================================
*/

#include "cover_system.h"
#include "../../../engine/core/qcommon.h"
#include "../game_interface.h"
#include "../ai_system.h"
#include "../ai_constants.h"
#include "../game_entities.h"
#include "../../server/server.h"
#include <math.h>
#include <string.h>

static struct {
    qboolean initialized;
    cover_manager_t *global_manager;
    cvar_t *cover_debug;
    cvar_t *cover_analysis_detail;
    cvar_t *cover_dynamic_update;
} cover_global;

// Forward declarations
static void Cover_AnalyzeCoverProperties(cover_manager_t *manager, cover_point_t *cover);
static void Cover_ConnectCoverPoints(cover_manager_t *manager);

/*
==================
Cover_InitSystem
==================
*/
void Cover_InitSystem(void) {
    if (cover_global.initialized) {
        return;
    }
    
    memset(&cover_global, 0, sizeof(cover_global));
    
    cover_global.cover_debug = Cvar_Get("ai_cover_debug", "0", 0);
    cover_global.cover_analysis_detail = Cvar_Get("ai_cover_detail", "1", CVAR_ARCHIVE);
    cover_global.cover_dynamic_update = Cvar_Get("ai_cover_dynamic", "1", CVAR_ARCHIVE);
    
    cover_global.global_manager = Cover_CreateManager();
    cover_global.initialized = qtrue;
    
    Com_Printf("Cover System Initialized\n");
}

/*
==================
Cover_ShutdownSystem
==================
*/
void Cover_ShutdownSystem(void) {
    if (!cover_global.initialized) {
        return;
    }
    
    if (cover_global.global_manager) {
        Cover_DestroyManager(cover_global.global_manager);
    }
    
    cover_global.initialized = qfalse;
    Com_Printf("Cover System Shutdown\n");
}

/*
==================
Cover_CreateManager
==================
*/
cover_manager_t *Cover_CreateManager(void) {
    cover_manager_t *manager;
    
    manager = (cover_manager_t *)Z_Malloc(sizeof(cover_manager_t));
    memset(manager, 0, sizeof(cover_manager_t));
    
    manager->needs_update = qtrue;
    
    return manager;
}

/*
==================
Cover_DestroyManager
==================
*/
void Cover_DestroyManager(cover_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    Z_Free(manager);
}

/*
==================
Cover_AnalyzeMap
==================
*/
void Cover_AnalyzeMap(cover_manager_t *manager) {
    vec3_t origin, test_pos, trace_end;
    trace_t trace;
    int x, y, z;
    float spacing = COVER_POINT_SPACING;
    vec3_t mins, maxs;
    
    if (!manager) {
        return;
    }
    
    Com_Printf("Analyzing map for cover points...\n");
    
    manager->num_cover_points = 0;
    memset(manager->cover_grid, -1, sizeof(manager->cover_grid));
    
    // Get world bounds (simplified - would need actual world bounds)
    VectorSet(mins, -4096, -4096, -512);
    VectorSet(maxs, 4096, 4096, 2048);
    
    // Scan the map in a grid pattern
    for (x = mins[0]; x < maxs[0] && manager->num_cover_points < MAX_COVER_POINTS; x += spacing) {
        for (y = mins[1]; y < maxs[1] && manager->num_cover_points < MAX_COVER_POINTS; y += spacing) {
            for (z = mins[2]; z < maxs[2] && manager->num_cover_points < MAX_COVER_POINTS; z += spacing) {
                VectorSet(origin, x, y, z);
                
                // Check if this position is valid ground
                VectorCopy(origin, trace_end);
                trace_end[2] -= 64;
                trap_Trace(&trace, origin, NULL, NULL, trace_end, ENTITYNUM_NONE, MASK_SOLID);
                
                if (trace.fraction < 1.0f && trace.plane.normal[2] > 0.7f) {
                    // Found ground, check for cover
                    cover_type_t type;
                    float quality;
                    
                    if (Cover_ValidateCoverPoint(trace.endpos, &type, &quality)) {
                        cover_point_t *cover = &manager->cover_points[manager->num_cover_points];
                        
                        VectorCopy(trace.endpos, cover->position);
                        cover->position[2] += 2; // Slight offset from ground
                        cover->type = type;
                        cover->quality = (cover_quality_t)(quality * 3); // Convert to quality enum
                        
                        // Analyze cover properties
                        Cover_AnalyzeCoverProperties(manager, cover);
                        
                        // Add to spatial grid
                        Cover_AddToGrid(manager, manager->num_cover_points);
                        
                        manager->num_cover_points++;
                    }
                }
            }
        }
    }
    
    // Connect nearby cover points
    Cover_ConnectCoverPoints(manager);
    
    manager->last_analysis_time = level.time * 0.001f * 1000 * 0.001f;
    manager->needs_update = qfalse;
    
    Com_Printf("Found %d cover points\n", manager->num_cover_points);
}

/*
==================
Cover_ValidateCoverPoint
==================
*/
qboolean Cover_ValidateCoverPoint(const vec3_t position, cover_type_t *type, float *quality) {
    trace_t trace;
    vec3_t test_pos, trace_end;
    int num_blocked = 0;
    int num_directions = 8;
    float angle_step = 360.0f / num_directions;
    float max_height = 0;
    qboolean has_overhead = qfalse;
    
    *type = COVER_TYPE_NONE;
    *quality = 0;
    
    // Test multiple directions for obstacles
    for (int i = 0; i < num_directions; i++) {
        float angle = i * angle_step;
        float rad = DEG2RAD(angle);
        
        // Test at multiple heights
        for (int h = 0; h < 3; h++) {
            VectorCopy(position, test_pos);
            test_pos[2] += h * 32; // Test at different heights
            
            VectorCopy(test_pos, trace_end);
            trace_end[0] += cos(rad) * 64;
            trace_end[1] += sin(rad) * 64;
            
            trap_Trace(&trace, test_pos, NULL, NULL, trace_end, ENTITYNUM_NONE, MASK_SOLID);
            
            if (trace.fraction < 1.0f) {
                num_blocked++;
                
                float obstacle_height = trace.endpos[2] - position[2];
                if (obstacle_height > max_height) {
                    max_height = obstacle_height;
                }
            }
        }
    }
    
    // Check overhead cover
    VectorCopy(position, test_pos);
    test_pos[2] += 96;
    VectorCopy(test_pos, trace_end);
    trace_end[2] += 32;
    trap_Trace(&trace, test_pos, NULL, NULL, trace_end, ENTITYNUM_NONE, MASK_SOLID);
    if (trace.fraction < 1.0f) {
        has_overhead = qtrue;
    }
    
    // Determine cover type based on analysis
    if (num_blocked == 0) {
        return qfalse; // No cover here
    }
    
    float block_ratio = (float)num_blocked / (num_directions * 3);
    
    if (block_ratio > 0.75f) {
        *type = COVER_TYPE_PILLAR; // Surrounded
        *quality = 0.9f;
    } else if (max_height > COVER_HEIGHT_THRESHOLD) {
        *type = COVER_TYPE_HIGH;
        *quality = 0.7f + (block_ratio * 0.3f);
    } else if (max_height > 24) {
        *type = COVER_TYPE_LOW;
        *quality = 0.5f + (block_ratio * 0.3f);
    } else if (block_ratio > 0.3f && block_ratio < 0.5f) {
        *type = COVER_TYPE_CORNER;
        *quality = 0.6f + (block_ratio * 0.2f);
    } else {
        *type = COVER_TYPE_EDGE;
        *quality = 0.3f + (block_ratio * 0.2f);
    }
    
    // Bonus for overhead protection
    if (has_overhead) {
        *quality += 0.1f;
    }
    
    *quality = CLAMP(*quality, 0, 1);
    
    return qtrue;
}

// Functions are defined later in the file

/*
==================
Cover_FindBestCover
==================
*/
cover_point_t *Cover_FindBestCover(cover_manager_t *manager, const cover_search_params_t *params) {
    cover_point_t *best_cover = NULL;
    float best_score = -1;
    
    if (!manager || !params) {
        return NULL;
    }
    
    // Search within radius
    for (int i = 0; i < manager->num_cover_points; i++) {
        cover_point_t *cover = &manager->cover_points[i];
        float dist = Distance(params->search_origin, cover->position);
        
        if (dist > params->search_radius) {
            continue;
        }
        
        // Evaluate this cover point
        cover_evaluation_t eval = Cover_EvaluatePoint(cover, params);
        
        if (eval.is_valid && eval.total_score > best_score) {
            best_score = eval.total_score;
            best_cover = cover;
        }
    }
    
    return best_cover;
}

/*
==================
Cover_EvaluatePoint
==================
*/
cover_evaluation_t Cover_EvaluatePoint(const cover_point_t *cover, const cover_search_params_t *params) {
    cover_evaluation_t eval;
    memset(&eval, 0, sizeof(eval));
    
    if (!cover || !params) {
        return eval;
    }
    
    eval.distance_to_cover = Distance(params->search_origin, cover->position);
    eval.distance_to_threat = Distance(params->threat_position, cover->position);
    
    // Check basic validity
    if (eval.distance_to_threat < params->min_distance_from_threat ||
        eval.distance_to_threat > params->max_distance_from_threat) {
        eval.is_valid = qfalse;
        return eval;
    }
    
    // Protection score
    eval.protection_score = Cover_CalculateProtection(cover, params->threat_position);
    
    // Position score (prefer optimal distance from threat)
    float optimal_dist = 400;
    float dist_factor = 1.0f - fabsf(eval.distance_to_threat - optimal_dist) / optimal_dist;
    eval.position_score = CLAMP(dist_factor, 0, 1) * 0.5f;
    
    // Add bonus for preferred direction
    if (VectorLength(params->preferred_direction) > 0) {
        vec3_t to_cover;
        VectorSubtract(cover->position, params->search_origin, to_cover);
        VectorNormalize(to_cover);
        float dot = DotProduct(to_cover, params->preferred_direction);
        eval.position_score += (dot + 1.0f) * 0.25f; // 0 to 0.5 bonus
    }
    
    // Tactical score
    eval.tactical_score = 0.5f; // Base score
    
    if (cover->is_corner) {
        eval.tactical_score += 0.2f;
    }
    
    if (cover->allows_peek_left || cover->allows_peek_right) {
        eval.tactical_score += 0.15f;
    }
    
    if (cover->type == params->preferred_type) {
        eval.tactical_score += 0.15f;
    }
    
    // Accessibility score
    eval.exposure_time = Cover_CalculateExposure(params->search_origin, cover->position, params->threat_position);
    eval.accessibility_score = 1.0f - (eval.exposure_time / eval.distance_to_cover * 1000);
    eval.accessibility_score = CLAMP(eval.accessibility_score, 0, 1);
    
    // Urgency modifier
    if (params->time_pressure > 0) {
        // Prefer closer cover when under pressure
        float urgency_bonus = (1.0f - eval.distance_to_cover / params->search_radius) * params->time_pressure;
        eval.accessibility_score += urgency_bonus * 0.3f;
    }
    
    // Calculate total score
    eval.total_score = eval.protection_score * 0.4f +
                      eval.position_score * 0.2f +
                      eval.tactical_score * 0.2f +
                      eval.accessibility_score * 0.2f;
    
    // Quality modifier
    float quality_mult = 0.7f + (cover->quality * 0.3f);
    eval.total_score *= quality_mult;
    
    // Penalize recently used cover
    if (cover->last_used_time > 0 && level.time * 0.001f * 1000 * 0.001f - cover->last_used_time < 10.0f) {
        eval.total_score *= 0.7f;
    }
    
    eval.is_valid = qtrue;
    
    return eval;
}

/*
==================
Cover_CalculateProtection
==================
*/
float Cover_CalculateProtection(const cover_point_t *cover, const vec3_t threat_pos) {
    vec3_t to_threat;
    float protection = 0;
    
    if (!cover) {
        return 0;
    }
    
    VectorSubtract(threat_pos, cover->position, to_threat);
    to_threat[2] = 0; // Horizontal only
    VectorNormalize(to_threat);
    
    // Check if cover normal faces the threat
    if (VectorLength(cover->normal) > 0) {
        float dot = DotProduct(cover->normal, to_threat);
        if (dot < 0) {
            protection = -dot; // 0 to 1, where 1 is directly facing
        }
    }
    
    // Add protection based on cover type
    switch (cover->type) {
        case COVER_TYPE_HIGH:
            protection = (protection + 1.0f) * 0.5f * 0.9f;
            break;
        case COVER_TYPE_LOW:
            protection = (protection + 1.0f) * 0.5f * 0.6f;
            break;
        case COVER_TYPE_PILLAR:
            protection = 0.95f; // Great protection from all sides
            break;
        case COVER_TYPE_CORNER:
            protection = (protection + 1.0f) * 0.5f * 0.75f;
            break;
        default:
            protection = (protection + 1.0f) * 0.5f * 0.4f;
            break;
    }
    
    return CLAMP(protection, 0, 1);
}

/*
==================
Cover_CalculateExposure
==================
*/
float Cover_CalculateExposure(const vec3_t from, const vec3_t to, const vec3_t threat_pos) {
    vec3_t segment;
    vec3_t point;
    trace_t trace;
    float exposure = 0;
    int samples = 10;
    
    VectorSubtract(to, from, segment);
    float total_dist = VectorLength(segment);
    VectorNormalize(segment);
    
    // Sample points along the path
    for (int i = 0; i <= samples; i++) {
        float fraction = (float)i / samples;
        VectorMA(from, fraction * total_dist, segment, point);
        
        // Check visibility from threat to this point
        trap_Trace(&trace, threat_pos, NULL, NULL, point, ENTITYNUM_NONE, MASK_SHOT);
        
        if (trace.fraction >= 1.0f) {
            exposure += 1.0f / samples;
        }
    }
    
    return exposure;
}

/*
==================
Cover_EnterCover
==================
*/
void Cover_EnterCover(cover_state_t *state, cover_point_t *cover) {
    if (!state || !cover) {
        return;
    }
    
    state->current_cover = cover;
    state->current_cover_index = -1; // Would need to find index
    state->time_in_cover = 0;
    state->last_peek_time = 0;
    state->peek_count = 0;
    state->is_suppressed = qfalse;
    state->needs_new_cover = qfalse;
    VectorClear(state->peek_position);
    VectorClear(state->lean_angles);
    state->exposure_percentage = 0;
    
    // Mark cover as used
    cover->last_used_time = level.time * 0.001f * 1000 * 0.001f;
    cover->times_used++;
}

/*
==================
Cover_UpdateInCover
==================
*/
void Cover_UpdateInCover(cover_state_t *state, float delta_time) {
    if (!state || !state->current_cover) {
        return;
    }
    
    state->time_in_cover += delta_time;
    
    // Check if we should peek
    if (Cover_ShouldPeek(state)) {
        state->last_peek_time = level.time * 0.001f * 1000 * 0.001f;
        state->peek_count++;
        
        // Alternate peek directions
        if (state->current_cover->allows_peek_left && (state->peek_count % 3 == 0)) {
            Cover_CalculateLeanAngles(state->current_cover, qtrue, state->lean_angles);
        } else if (state->current_cover->allows_peek_right && (state->peek_count % 3 == 1)) {
            Cover_CalculateLeanAngles(state->current_cover, qfalse, state->lean_angles);
        } else if (state->current_cover->allows_peek_over) {
            // Stand up to peek over
            state->peek_position[2] = 20;
        }
    }
    
    // Check suppression
    if (state->current_cover->danger_level > 0.7f) {
        state->is_suppressed = qtrue;
    }
}

/*
==================
Cover_ShouldPeek
==================
*/
qboolean Cover_ShouldPeek(cover_state_t *state) {
    if (!state || !state->current_cover) {
        return qfalse;
    }
    
    // Don't peek if suppressed
    if (state->is_suppressed) {
        return qfalse;
    }
    
    // Minimum time between peeks
    float time_since_peek = level.time * 0.001f * 1000 * 0.001f - state->last_peek_time;
    if (time_since_peek < 1.0f) {
        return qfalse;
    }
    
    // Vary peek timing
    float peek_chance = 0.3f + (time_since_peek * 0.1f);
    
    return (random() < peek_chance);
}

/*
==================
Cover_ShouldRelocate
==================
*/
qboolean Cover_ShouldRelocate(cover_state_t *state, float danger_level) {
    if (!state || !state->current_cover) {
        return qfalse;
    }
    
    // Relocate if position compromised
    if (danger_level > 0.8f) {
        return qtrue;
    }
    
    // Relocate if in cover too long
    if (state->time_in_cover > 10.0f) {
        return qtrue;
    }
    
    // Relocate if peeked too many times
    if (state->peek_count > 5) {
        return qtrue;
    }
    
    return state->needs_new_cover;
}

/*
==================
Cover_CalculateLeanAngles
==================
*/
void Cover_CalculateLeanAngles(const cover_point_t *cover, qboolean left, vec3_t angles) {
    VectorClear(angles);
    
    if (!cover) {
        return;
    }
    
    // Calculate lean angle based on cover type
    float lean_amount = 15.0f; // degrees
    
    if (cover->type == COVER_TYPE_CORNER) {
        lean_amount = 20.0f;
    } else if (cover->type == COVER_TYPE_PILLAR) {
        lean_amount = 25.0f;
    }
    
    angles[ROLL] = left ? -lean_amount : lean_amount;
}

/*
==================
Cover_IsPathExposed
==================
*/
qboolean Cover_IsPathExposed(const vec3_t from, const vec3_t to, const vec3_t threat) {
    trace_t trace;
    vec3_t midpoint;
    
    // Check midpoint visibility
    VectorAdd(from, to, midpoint);
    VectorScale(midpoint, 0.5f, midpoint);
    
    if (VectorLength(threat) > 0) {
        trap_Trace(&trace, threat, NULL, NULL, midpoint, ENTITYNUM_NONE, MASK_SHOT);
        return (trace.fraction >= 1.0f);
    }
    
    return qfalse;
}

/*
==================
Cover_AddToGrid
==================
*/
void Cover_AddToGrid(cover_manager_t *manager, int cover_index) {
    int x, y, z;
    
    if (!manager || cover_index < 0 || cover_index >= manager->num_cover_points) {
        return;
    }
    
    if (Cover_GetGridIndex(manager->cover_points[cover_index].position, &x, &y, &z)) {
        if (x >= 0 && x < 64 && y >= 0 && y < 64 && z >= 0 && z < 16) {
            manager->cover_grid[x][y][z] = cover_index;
        }
    }
}

/*
==================
Cover_GetGridIndex
==================
*/
int Cover_GetGridIndex(const vec3_t position, int *x, int *y, int *z) {
    // Convert world position to grid index
    *x = (int)((position[0] + 4096) / 128);
    *y = (int)((position[1] + 4096) / 128);
    *z = (int)((position[2] + 512) / 256);
    
    return (*x >= 0 && *x < 64 && *y >= 0 && *y < 64 && *z >= 0 && *z < 16);
}

/*
==================
Cover_AnalyzeCoverProperties

Analyze detailed properties of a cover point
==================
*/
static void Cover_AnalyzeCoverProperties(cover_manager_t *manager, cover_point_t *cover) {
    trace_t trace;
    vec3_t test_pos, end_pos;
    int num_directions = 16;
    float angle_step = 360.0f / num_directions;
    int protected_directions = 0;
    
    if (!manager || !cover) {
        return;
    }
    
    // Initialize properties
    cover->allows_peek_left = qfalse;
    cover->allows_peek_right = qfalse;
    cover->allows_peek_over = qfalse;
    cover->is_corner = qfalse;
    cover->danger_level = 0;
    VectorClear(cover->normal);
    
    // Test protection from multiple directions
    for (int i = 0; i < num_directions; i++) {
        float angle = i * angle_step;
        float rad = DEG2RAD(angle);
        
        VectorCopy(cover->position, test_pos);
        test_pos[2] += 50; // Test at chest height
        
        VectorCopy(test_pos, end_pos);
        end_pos[0] += cos(rad) * 100;
        end_pos[1] += sin(rad) * 100;
        
        trap_Trace(&trace, test_pos, NULL, NULL, end_pos, ENTITYNUM_NONE, MASK_SOLID);
        
        if (trace.fraction < 1.0f) {
            protected_directions++;
            
            // Accumulate normal for average direction
            VectorMA(cover->normal, 1.0f, trace.plane.normal, cover->normal);
        }
    }
    
    // Normalize the accumulated normal
    if (protected_directions > 0) {
        VectorScale(cover->normal, 1.0f / protected_directions, cover->normal);
        VectorNormalize(cover->normal);
    }
    
    // Determine if it's a corner (protected from multiple angles)
    float protection_ratio = (float)protected_directions / num_directions;
    if (protection_ratio > 0.2f && protection_ratio < 0.5f) {
        cover->is_corner = qtrue;
    }
    
    // Check peek possibilities
    // Left peek
    VectorCopy(cover->position, test_pos);
    test_pos[0] -= 30;
    test_pos[2] += 60;
    VectorCopy(test_pos, end_pos);
    end_pos[0] -= 20;
    trap_Trace(&trace, test_pos, NULL, NULL, end_pos, ENTITYNUM_NONE, MASK_SOLID);
    if (trace.fraction >= 1.0f) {
        cover->allows_peek_left = qtrue;
    }
    
    // Right peek
    VectorCopy(cover->position, test_pos);
    test_pos[0] += 30;
    test_pos[2] += 60;
    VectorCopy(test_pos, end_pos);
    end_pos[0] += 20;
    trap_Trace(&trace, test_pos, NULL, NULL, end_pos, ENTITYNUM_NONE, MASK_SOLID);
    if (trace.fraction >= 1.0f) {
        cover->allows_peek_right = qtrue;
    }
    
    // Over peek (for low cover)
    if (cover->type == COVER_TYPE_LOW) {
        VectorCopy(cover->position, test_pos);
        test_pos[2] += 80; // Stand height
        VectorCopy(test_pos, end_pos);
        end_pos[2] += 20;
        trap_Trace(&trace, test_pos, NULL, NULL, end_pos, ENTITYNUM_NONE, MASK_SOLID);
        if (trace.fraction >= 1.0f) {
            cover->allows_peek_over = qtrue;
        }
    }
    
    // Calculate initial danger level based on exposure
    cover->danger_level = 1.0f - protection_ratio;
    
    // Reset usage tracking
    cover->last_used_time = 0;
    cover->times_used = 0;
}

/*
==================
Cover_ConnectCoverPoints

Connect nearby cover points for movement planning
==================
*/
static void Cover_ConnectCoverPoints(cover_manager_t *manager) {
    trace_t trace;
    vec3_t start, end;
    float max_connection_dist = 300.0f;
    
    if (!manager || manager->num_cover_points == 0) {
        return;
    }
    
    // Connect each cover point to nearby points
    for (int i = 0; i < manager->num_cover_points; i++) {
        cover_point_t *cover1 = &manager->cover_points[i];
        int connections = 0;
        
        for (int j = i + 1; j < manager->num_cover_points && connections < MAX_COVER_CONNECTIONS; j++) {
            cover_point_t *cover2 = &manager->cover_points[j];
            
            float dist = Distance(cover1->position, cover2->position);
            
            // Skip if too far
            if (dist > max_connection_dist) {
                continue;
            }
            
            // Check if path between covers is clear
            VectorCopy(cover1->position, start);
            start[2] += 20; // Slightly above ground
            VectorCopy(cover2->position, end);
            end[2] += 20;
            
            trap_Trace(&trace, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID);
            
            // If path is mostly clear, create connection
            if (trace.fraction > 0.9f) {
                // Add connection from cover1 to cover2
                if (cover1->num_connections < 4) {
                    cover1->connected_covers[cover1->num_connections] = j;
                    // Store distance in some other way if needed
                    cover1->num_connections++;
                    connections++;
                }
                
                // Add reverse connection from cover2 to cover1
                if (cover2->num_connections < 4) {
                    cover2->connected_covers[cover2->num_connections] = i;
                    // Store distance in some other way if needed
                    cover2->num_connections++;
                }
            }
        }
    }
    
    // Calculate network properties
    for (int i = 0; i < manager->num_cover_points; i++) {
        cover_point_t *cover = &manager->cover_points[i];
        
        // Points with more connections are more valuable for movement
        if (cover->num_connections >= 3) {
            cover->quality = Min(cover->quality + 1, COVER_QUALITY_EXCELLENT);
        }
        
        // Isolated points are less valuable
        if (cover->num_connections == 0) {
            cover->quality = Max(cover->quality - 1, COVER_QUALITY_POOR);
        }
    }
}

