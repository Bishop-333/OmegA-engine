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

#include "movement_tactics.h"
#include "../../../engine/core/qcommon.h"
#include "../game_interface.h"
#include "../ai_system.h"
#include "../game_entities.h"
#include "../../server/server.h"
#include <math.h>
#include <string.h>

static struct {
    qboolean initialized;
    tactical_movement_t *movements[MAX_CLIENTS];
    int movement_count;
    cvar_t *movement_debug;
    cvar_t *movement_prediction;
    cvar_t *movement_advanced;
} movement_global;

/*
==================
Movement_Init
==================
*/
void Movement_Init(void) {
    if (movement_global.initialized) {
        return;
    }
    
    memset(&movement_global, 0, sizeof(movement_global));
    
    movement_global.movement_debug = Cvar_Get("ai_movement_debug", "0", 0);
    movement_global.movement_prediction = Cvar_Get("ai_movement_prediction", "1", CVAR_ARCHIVE);
    movement_global.movement_advanced = Cvar_Get("ai_movement_advanced", "1", CVAR_ARCHIVE);
    
    movement_global.initialized = qtrue;
    
    Com_Printf("Tactical Movement System Initialized\n");
}

/*
==================
Movement_Shutdown
==================
*/
void Movement_Shutdown(void) {
    int i;
    
    if (!movement_global.initialized) {
        return;
    }
    
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (movement_global.movements[i]) {
            Movement_Destroy(movement_global.movements[i]);
        }
    }
    
    movement_global.initialized = qfalse;
    Com_Printf("Tactical Movement System Shutdown\n");
}

/*
==================
Movement_Create
==================
*/
tactical_movement_t *Movement_Create(movement_style_t style) {
    tactical_movement_t *movement;
    int i;
    
    movement = (tactical_movement_t *)Z_Malloc(sizeof(tactical_movement_t));
    memset(movement, 0, sizeof(tactical_movement_t));
    
    movement->style = style;
    movement->state.max_speed = 320;
    movement->strafe_amplitude = 200;
    movement->strafe_frequency = 2.0f;
    
    // Style-specific initialization
    switch (style) {
        case MOVE_STYLE_AGGRESSIVE:
            movement->state.max_speed = 400;
            movement->random_strafe = qfalse;
            break;
        case MOVE_STYLE_EVASIVE:
            movement->strafe_amplitude = 300;
            movement->strafe_frequency = 3.0f;
            movement->random_strafe = qtrue;
            break;
        case MOVE_STYLE_STEALTH:
            movement->state.max_speed = 200;
            break;
        case MOVE_STYLE_PARKOUR:
            movement->parkour.momentum = 1.0f;
            break;
        default:
            break;
    }
    
    // Add to global list
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!movement_global.movements[i]) {
            movement_global.movements[i] = movement;
            movement_global.movement_count++;
            break;
        }
    }
    
    Com_DPrintf("Created tactical movement system with style %d\n", style);
    
    return movement;
}

/*
==================
Movement_Destroy
==================
*/
void Movement_Destroy(tactical_movement_t *movement) {
    int i;
    
    if (!movement) {
        return;
    }
    
    // Remove from global list
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (movement_global.movements[i] == movement) {
            movement_global.movements[i] = NULL;
            movement_global.movement_count--;
            break;
        }
    }
    
    Z_Free(movement);
}

/*
==================
Movement_UpdateState
==================
*/
void Movement_UpdateState(tactical_movement_t *movement, const vec3_t position, const vec3_t velocity) {
    float current_time;
    
    if (!movement) {
        return;
    }
    
    current_time = level.time * 0.001f * 1000 * 0.001f;
    
    // Update position and velocity
    VectorCopy(position, movement->state.position);
    VectorCopy(velocity, movement->state.velocity);
    movement->state.speed = VectorLength(velocity);
    
    // Update ground/air state
    qboolean was_on_ground = movement->state.on_ground;
    movement->state.on_ground = (position[2] < 10 && fabsf(velocity[2]) < 50);
    movement->state.in_air = !movement->state.on_ground;
    
    if (movement->state.on_ground) {
        movement->state.ground_time += 0.05f;
        movement->state.air_time = 0;
        movement->state.consecutive_jumps = 0;
    } else {
        movement->state.air_time += 0.05f;
        movement->state.ground_time = 0;
        
        if (!was_on_ground) {
            movement->state.last_jump_time = current_time;
        }
    }
    
    // Update movement statistics
    movement->distance_traveled += movement->state.speed * 0.05f;
    movement->average_speed = movement->average_speed * 0.95f + movement->state.speed * 0.05f;
    
    if (movement->state.speed > movement->peak_speed) {
        movement->peak_speed = movement->state.speed;
    }
    
    // Check for wall proximity
    trace_t trace;
    vec3_t end;
    VectorMA(position, 32, velocity, end);
    trap_Trace(&trace, position, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID);
    movement->state.against_wall = (trace.fraction < 1.0f);
    
    // Update parkour state
    if (movement_global.movement_advanced->integer) {
        Movement_UpdateParkour(movement);
    }
}

/*
==================
Movement_Execute
==================
*/
void Movement_Execute(tactical_movement_t *movement, vec3_t move_dir, float *speed) {
    vec3_t desired_move;
    float current_time;
    
    if (!movement) {
        return;
    }
    
    current_time = level.time * 0.001f * 1000 * 0.001f;
    VectorClear(desired_move);
    
    // Check if we have a path
    if (movement->path.is_valid && movement->path.current_waypoint < movement->path.num_waypoints) {
        waypoint_t *waypoint = &movement->path.waypoints[movement->path.current_waypoint];
        
        // Move towards current waypoint
        VectorSubtract(waypoint->position, movement->state.position, desired_move);
        
        // Check if reached waypoint
        if (VectorLength(desired_move) < 32) {
            Movement_NextWaypoint(movement);
        }
        
        // Apply waypoint-specific technique
        if (waypoint->technique != MOVE_TECH_NONE) {
            Movement_ApplyTechnique(movement, waypoint->technique);
        }
        
        *speed = movement->state.max_speed * waypoint->speed_modifier;
    } else {
        // No path, use direct movement
        VectorCopy(movement->state.desired_direction, desired_move);
        *speed = movement->state.max_speed;
    }
    
    // Apply movement style modifications
    switch (movement->style) {
        case MOVE_STYLE_EVASIVE:
            if (movement->dodge.in_progress) {
                Movement_ExecuteDodge(movement, desired_move);
            } else {
                Movement_GenerateStrafePattern(movement, desired_move);
            }
            break;
            
        case MOVE_STYLE_AGGRESSIVE:
            // Direct approach with slight weaving
            if (current_time - movement->strafe_pattern_time > 0.5f) {
                vec3_t strafe;
                Movement_SerpentinePattern(movement, strafe);
                VectorAdd(desired_move, strafe, desired_move);
                movement->strafe_pattern_time = current_time;
            }
            break;
            
        case MOVE_STYLE_TACTICAL:
            // Smart movement with predictions
            if (movement_global.movement_prediction->integer && movement->num_predictions > 0) {
                Movement_PredictiveStrafe(movement, movement->predictions[0].position, desired_move);
            }
            break;
            
        case MOVE_STYLE_PARKOUR:
            // Apply parkour moves
            if (movement->parkour.wall_run_available) {
                Movement_ExecuteWallRun(movement, desired_move);
            }
            break;
            
        default:
            break;
    }
    
    // Normalize and apply
    VectorNormalize(desired_move);
    VectorCopy(desired_move, move_dir);
    
    // Smooth direction changes
    Movement_SmoothDirection(movement->state.desired_direction, desired_move, 
                            Movement_CalculateTurnRate(movement->state.speed), 
                            movement->state.desired_direction);
}

/*
==================
Movement_GenerateStrafePattern
==================
*/
void Movement_GenerateStrafePattern(tactical_movement_t *movement, vec3_t strafe) {
    float time;
    vec3_t perpendicular;
    
    if (!movement) {
        VectorClear(strafe);
        return;
    }
    
    time = level.time * 0.001f * 1000 * 0.001f;
    
    // Get perpendicular to movement direction
    perpendicular[0] = -movement->state.desired_direction[1];
    perpendicular[1] = movement->state.desired_direction[0];
    perpendicular[2] = 0;
    
    if (movement->random_strafe) {
        // Random strafe pattern
        Movement_RandomStrafe(movement, strafe);
    } else {
        // Sinusoidal strafe pattern
        float offset = sin(time * movement->strafe_frequency) * movement->strafe_amplitude;
        VectorScale(perpendicular, offset, strafe);
    }
    
    // Add forward movement
    VectorMA(strafe, 1.0f, movement->state.desired_direction, strafe);
}

/*
==================
Movement_CircleStrafe
==================
*/
void Movement_CircleStrafe(tactical_movement_t *movement, const vec3_t center, float radius, vec3_t move) {
    vec3_t to_center, tangent;
    float angle;
    
    if (!movement) {
        return;
    }
    
    // Calculate vector to center
    VectorSubtract(center, movement->state.position, to_center);
    to_center[2] = 0;
    
    float current_radius = VectorLength(to_center);
    VectorNormalize(to_center);
    
    // Calculate tangent (perpendicular to radial)
    tangent[0] = -to_center[1];
    tangent[1] = to_center[0];
    tangent[2] = 0;
    
    // Adjust for desired radius
    if (current_radius > radius) {
        // Move inward
        VectorScale(to_center, 0.3f, move);
    } else if (current_radius < radius * 0.8f) {
        // Move outward
        VectorScale(to_center, -0.3f, move);
    } else {
        VectorClear(move);
    }
    
    // Add circular movement
    VectorMA(move, 1.0f, tangent, move);
}

/*
==================
Movement_SerpentinePattern
==================
*/
void Movement_SerpentinePattern(tactical_movement_t *movement, vec3_t move) {
    float time;
    vec3_t side;
    
    if (!movement) {
        return;
    }
    
    time = level.time * 0.001f * 1000 * 0.001f;
    
    // Create S-curve pattern
    side[0] = -movement->state.desired_direction[1];
    side[1] = movement->state.desired_direction[0];
    side[2] = 0;
    
    float curve = sin(time * 3) * cos(time * 1.5f);
    VectorScale(side, curve * 150, move);
}

/*
==================
Movement_RandomStrafe
==================
*/
void Movement_RandomStrafe(tactical_movement_t *movement, vec3_t move) {
    float current_time;
    vec3_t random_dir;
    
    if (!movement) {
        return;
    }
    
    current_time = level.time * 0.001f * 1000 * 0.001f;
    
    // Change direction randomly
    if (current_time - movement->last_direction_change > STRAFE_CHANGE_TIME * 0.001f) {
        random_dir[0] = crandom();
        random_dir[1] = crandom();
        random_dir[2] = 0;
        VectorNormalize(random_dir);
        
        VectorScale(random_dir, movement->strafe_amplitude, move);
        movement->last_direction_change = current_time;
    }
}

/*
==================
Movement_InitiateDodge
==================
*/
void Movement_InitiateDodge(tactical_movement_t *movement, const vec3_t threat_dir) {
    if (!movement || movement->dodge.in_progress) {
        return;
    }
    
    movement->dodge.type = Movement_SelectDodgeType(movement, threat_dir);
    movement->dodge.start_time = level.time * 0.001f * 1000 * 0.001f;
    movement->dodge.in_progress = qtrue;
    movement->dodge.attempt_count++;
    
    // Calculate dodge direction based on type
    switch (movement->dodge.type) {
        case DODGE_TYPE_SIDESTEP:
            // Perpendicular to threat
            movement->dodge.direction[0] = -threat_dir[1];
            movement->dodge.direction[1] = threat_dir[0];
            movement->dodge.direction[2] = 0;
            movement->dodge.intensity = 1.0f;
            movement->dodge.duration = 0.3f;
            break;
            
        case DODGE_TYPE_DUCK:
            VectorClear(movement->dodge.direction);
            movement->state.is_crouching = qtrue;
            movement->dodge.duration = 0.5f;
            break;
            
        case DODGE_TYPE_JUMP:
            VectorCopy(threat_dir, movement->dodge.direction);
            VectorScale(movement->dodge.direction, -1, movement->dodge.direction);
            movement->dodge.direction[2] = 1.0f;
            movement->dodge.intensity = 1.5f;
            movement->dodge.duration = 0.6f;
            break;
            
        case DODGE_TYPE_DIAGONAL:
            movement->dodge.direction[0] = -threat_dir[1] + threat_dir[0];
            movement->dodge.direction[1] = threat_dir[0] + threat_dir[1];
            movement->dodge.direction[2] = 0;
            VectorNormalize(movement->dodge.direction);
            movement->dodge.intensity = 1.2f;
            movement->dodge.duration = 0.4f;
            break;
            
        case DODGE_TYPE_BACKPEDAL:
            VectorScale(threat_dir, -1, movement->dodge.direction);
            movement->dodge.intensity = 0.8f;
            movement->dodge.duration = 0.5f;
            break;
            
        case DODGE_TYPE_SLIDE:
            if (movement->state.speed > SLIDE_MIN_SPEED) {
                VectorCopy(movement->state.velocity, movement->dodge.direction);
                VectorNormalize(movement->dodge.direction);
                movement->state.is_sliding = qtrue;
                movement->dodge.intensity = 1.3f;
                movement->dodge.duration = 0.8f;
            }
            break;
            
        default:
            movement->dodge.in_progress = qfalse;
            break;
    }
}

/*
==================
Movement_ExecuteDodge
==================
*/
void Movement_ExecuteDodge(tactical_movement_t *movement, vec3_t dodge_vector) {
    float elapsed;
    float progress;
    
    if (!movement || !movement->dodge.in_progress) {
        return;
    }
    
    elapsed = level.time * 0.001f * 1000 * 0.001f - movement->dodge.start_time;
    progress = elapsed / movement->dodge.duration;
    
    if (progress >= 1.0f) {
        // Dodge complete
        movement->dodge.in_progress = qfalse;
        movement->state.is_crouching = qfalse;
        movement->state.is_sliding = qfalse;
        movement->dodge.success_count++;
        return;
    }
    
    // Apply dodge movement with easing
    float ease = 1.0f - (progress * progress); // Quadratic ease out
    VectorScale(movement->dodge.direction, movement->dodge.intensity * ease * 400, dodge_vector);
}

/*
==================
Movement_SelectDodgeType
==================
*/
dodge_type_t Movement_SelectDodgeType(tactical_movement_t *movement, const vec3_t threat_dir) {
    if (!movement) {
        return DODGE_TYPE_NONE;
    }
    
    // Select based on current state and style
    if (movement->state.in_air) {
        return DODGE_TYPE_NONE; // Can't dodge in air
    }
    
    if (movement->state.speed > SLIDE_MIN_SPEED && movement->style == MOVE_STYLE_PARKOUR) {
        return DODGE_TYPE_SLIDE;
    }
    
    // Vertical threat - duck or jump
    if (fabsf(threat_dir[2]) > 0.5f) {
        if (threat_dir[2] > 0) {
            return DODGE_TYPE_DUCK;
        } else {
            return DODGE_TYPE_JUMP;
        }
    }
    
    // Horizontal threat - sidestep or diagonal
    if (movement->style == MOVE_STYLE_EVASIVE) {
        return (random() > 0.5f) ? DODGE_TYPE_DIAGONAL : DODGE_TYPE_SIDESTEP;
    }
    
    return DODGE_TYPE_SIDESTEP;
}

/*
==================
Movement_ApplyTechnique
==================
*/
void Movement_ApplyTechnique(tactical_movement_t *movement, movement_technique_t technique) {
    vec3_t move;
    
    if (!movement || !movement_global.movement_advanced->integer) {
        return;
    }
    
    VectorClear(move);
    
    switch (technique) {
        case MOVE_TECH_STRAFE_JUMP:
            if (Movement_CanStrafeJump(&movement->state)) {
                Movement_ExecuteStrafeJump(movement, move);
            }
            break;
            
        case MOVE_TECH_BUNNY_HOP:
            if (Movement_CanBunnyHop(&movement->state)) {
                Movement_ExecuteBunnyHop(movement, move);
            }
            break;
            
        case MOVE_TECH_ROCKET_JUMP:
            if (Movement_CanRocketJump(&movement->state, 10)) { // Check ammo
                Movement_ExecuteRocketJump(movement, movement->state.angles);
            }
            break;
            
        case MOVE_TECH_WALL_JUMP:
            if (Movement_CanWallJump(&movement->parkour)) {
                Movement_ExecuteWallJump(movement, move);
            }
            break;
            
        case MOVE_TECH_AIR_CONTROL:
            if (movement->state.in_air) {
                Movement_AirControl(movement, move);
            }
            break;
            
        default:
            break;
    }
    
    movement->last_technique_time = level.time * 0.001f * 1000 * 0.001f;
}

/*
==================
Movement_CanStrafeJump
==================
*/
qboolean Movement_CanStrafeJump(const movement_state_t *state) {
    return state && state->on_ground && state->speed > 200;
}

/*
==================
Movement_ExecuteStrafeJump
==================
*/
void Movement_ExecuteStrafeJump(tactical_movement_t *movement, vec3_t move) {
    vec3_t forward, right;
    float angle;
    
    if (!movement) {
        return;
    }
    
    // Calculate strafe jump angle (approximately 45 degrees for max speed)
    angle = 45 * (movement->strafe_pattern % 2 == 0 ? 1 : -1);
    
    AngleVectors(movement->state.angles, forward, right, NULL);
    
    // Combine forward and strafe for diagonal movement
    VectorScale(forward, cos(DEG2RAD(angle)), move);
    VectorMA(move, sin(DEG2RAD(angle)), right, move);
    VectorNormalize(move);
    
    // Add jump
    move[2] = 270; // Jump velocity
    
    // consecutive_jumps removed
}

/*
==================
Movement_CanBunnyHop
==================
*/
qboolean Movement_CanBunnyHop(const movement_state_t *state) {
    if (!state) {
        return qfalse;
    }
    
    // Check if landing timing is right for bunny hop
    return state->on_ground && state->ground_time < 0.1f && state->speed > 250;
}

/*
==================
Movement_ExecuteBunnyHop
==================
*/
void Movement_ExecuteBunnyHop(tactical_movement_t *movement, vec3_t move) {
    vec3_t wishdir;
    
    if (!movement) {
        return;
    }
    
    // Maintain momentum direction
    VectorCopy(movement->state.velocity, wishdir);
    wishdir[2] = 0;
    VectorNormalize(wishdir);
    
    // Apply strafe for speed gain
    wishdir[0] += crandom() * 0.1f;
    wishdir[1] += crandom() * 0.1f;
    VectorNormalize(wishdir);
    
    VectorScale(wishdir, movement->state.max_speed * 1.1f, move);
    move[2] = 270; // Jump
    
    movement->parkour.momentum *= 1.05f; // Increase momentum
}

/*
==================
Movement_CanRocketJump
==================
*/
qboolean Movement_CanRocketJump(const movement_state_t *state, int ammo) {
    return state && state->on_ground && ammo > 0;
}

/*
==================
Movement_ExecuteRocketJump
==================
*/
void Movement_ExecuteRocketJump(tactical_movement_t *movement, vec3_t angles) {
    vec3_t aim_angles;
    
    if (!movement) {
        return;
    }
    
    // Aim down and slightly behind
    VectorCopy(angles, aim_angles);
    aim_angles[PITCH] = 80; // Look down
    
    // Would trigger rocket fire here
    // Apply upward and forward velocity
    movement->state.velocity[2] = 600;
    movement->state.velocity[0] *= 1.5f;
    movement->state.velocity[1] *= 1.5f;
}

/*
==================
Movement_AirControl
==================
*/
void Movement_AirControl(tactical_movement_t *movement, vec3_t move) {
    vec3_t wishdir;
    float wishspeed;
    
    if (!movement) {
        return;
    }
    
    // Get desired direction
    VectorCopy(movement->state.desired_direction, wishdir);
    wishspeed = VectorNormalize(wishdir);
    
    if (wishspeed > 30) {
        wishspeed = 30; // Air acceleration limit
    }
    
    // Apply air control
    VectorScale(wishdir, wishspeed, move);
}

/*
==================
Movement_UpdateParkour
==================
*/
void Movement_UpdateParkour(tactical_movement_t *movement) {
    trace_t trace;
    vec3_t end;
    
    if (!movement) {
        return;
    }
    
    // Check for wall run opportunities
    if (movement->state.in_air && movement->state.speed > 200) {
        // Check left wall
        VectorCopy(movement->state.position, end);
        end[0] -= 32;
        trap_Trace(&trace, movement->state.position, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID);
        
        if (trace.fraction < 1.0f && Movement_CanWallRun(&movement->state, trace.plane.normal)) {
            movement->parkour.wall_run_available = qtrue;
            VectorCopy(trace.plane.normal, movement->parkour.wall_normal);
        } else {
            // Check right wall
            VectorCopy(movement->state.position, end);
            end[0] += 32;
            trap_Trace(&trace, movement->state.position, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID);
            
            if (trace.fraction < 1.0f && Movement_CanWallRun(&movement->state, trace.plane.normal)) {
                movement->parkour.wall_run_available = qtrue;
                VectorCopy(trace.plane.normal, movement->parkour.wall_normal);
            }
        }
    } else {
        movement->parkour.wall_run_available = qfalse;
        movement->parkour.wall_run_time = 0;
    }
    
    // Update momentum decay
    if (movement->state.on_ground) {
        movement->parkour.momentum *= 0.98f;
    }
    movement->parkour.momentum = CLAMP(movement->parkour.momentum, 0.5f, 2.0f);
}

/*
==================
Movement_CanWallRun
==================
*/
qboolean Movement_CanWallRun(const movement_state_t *state, const vec3_t wall_normal) {
    vec3_t velocity_dir;
    float dot;
    
    if (!state || state->on_ground) {
        return qfalse;
    }
    
    // Check if moving somewhat parallel to wall
    VectorCopy(state->velocity, velocity_dir);
    velocity_dir[2] = 0;
    VectorNormalize(velocity_dir);
    
    dot = DotProduct(velocity_dir, wall_normal);
    
    // Wall should be mostly perpendicular to movement
    return (fabsf(dot) < 0.5f && state->speed > 200);
}

/*
==================
Movement_ExecuteWallRun
==================
*/
void Movement_ExecuteWallRun(tactical_movement_t *movement, vec3_t move) {
    vec3_t run_dir;
    
    if (!movement) {
        return;
    }
    
    // Calculate run direction along wall
    CrossProduct(movement->parkour.wall_normal, vec3_origin, run_dir);
    run_dir[2] = 0;
    VectorNormalize(run_dir);
    
    // Apply wall run movement
    VectorScale(run_dir, movement->state.max_speed * movement->parkour.momentum, move);
    
    // Slight upward movement to maintain height
    move[2] = 50;
    
    movement->parkour.wall_run_time += 0.05f;
    movement->parkour.style_points += 0.1f;
    
    // Can't wall run forever
    if (movement->parkour.wall_run_time > 2.0f) {
        movement->parkour.wall_run_available = qfalse;
        movement->parkour.can_wall_jump = qtrue;
    }
}

/*
==================
Movement_CalculateTurnRate
==================
*/
float Movement_CalculateTurnRate(float current_speed) {
    // Slower turn rate at higher speeds
    float base_rate = 180; // degrees per second
    float speed_factor = 1.0f - (current_speed / 800.0f);
    speed_factor = CLAMP(speed_factor, 0.3f, 1.0f);
    
    return base_rate * speed_factor;
}

/*
==================
Movement_SmoothDirection
==================
*/
void Movement_SmoothDirection(vec3_t current_dir, const vec3_t desired_dir, float rate, vec3_t result) {
    float angle_diff;
    vec3_t temp;
    
    // Calculate angle difference
    angle_diff = acos(DotProduct(current_dir, desired_dir));
    
    if (angle_diff < 0.01f) {
        VectorCopy(desired_dir, result);
        return;
    }
    
    // Interpolate
    float interp = rate * 0.05f / angle_diff;
    interp = CLAMP(interp, 0, 1);
    
    VectorScale(current_dir, 1.0f - interp, temp);
    VectorMA(temp, interp, desired_dir, result);
    VectorNormalize(result);
}

/*
==================
Movement_NextWaypoint
==================
*/
void Movement_NextWaypoint(tactical_movement_t *movement) {
    if (!movement || !movement->path.num_waypoints) {
        return;
    }
    
    // Move to next waypoint in path
    movement->path.current_waypoint++;
    
    // Check if we've reached the end
    if (movement->path.current_waypoint >= movement->path.num_waypoints) {
        // Path complete - clear navigation
        movement->path.num_waypoints = 0;
        movement->path.current_waypoint = 0;
        movement->path.is_valid = qfalse;
        return;
    }
    
    // Update navigation to next waypoint
    movement->path.total_distance = Distance(
        movement->state.position,
        movement->path.waypoints[movement->path.current_waypoint].position);
}

/*
==================
Movement_PredictiveStrafe
==================
*/
void Movement_PredictiveStrafe(tactical_movement_t *movement, const vec3_t threat_pos, vec3_t move) {
    vec3_t threat_dir, right, predicted_pos;
    vec3_t velocity;
    float strafe_amount, prediction_time;
    int strafe_dir;
    
    if (!movement) {
        return;
    }
    
    // Calculate threat direction
    VectorSubtract(threat_pos, movement->state.position, threat_dir);
    threat_dir[2] = 0;
    VectorNormalize(threat_dir);
    
    // Get perpendicular strafe direction
    right[0] = threat_dir[1];
    right[1] = -threat_dir[0];
    right[2] = 0;
    
    // Predict where we'll be in 0.5 seconds
    prediction_time = 0.5f;
    VectorCopy(movement->state.velocity, velocity);
    VectorMA(movement->state.position, prediction_time, velocity, predicted_pos);
    
    // Choose strafe direction based on obstacle prediction
    trace_t trace;
    vec3_t test_pos;
    
    // Test right strafe
    VectorMA(predicted_pos, 100, right, test_pos);
    trap_Trace(&trace, movement->state.position, NULL, NULL, test_pos, 
               ENTITYNUM_NONE, MASK_SOLID);
    float right_clear = trace.fraction;
    
    // Test left strafe
    VectorMA(predicted_pos, -100, right, test_pos);
    trap_Trace(&trace, movement->state.position, NULL, NULL, test_pos,
               ENTITYNUM_NONE, MASK_SOLID);
    float left_clear = trace.fraction;
    
    // Choose direction with more clearance
    if (right_clear > left_clear) {
        strafe_dir = 1;
    } else if (left_clear > right_clear) {
        strafe_dir = -1;
    } else {
        // Alternate strafe direction to be unpredictable
        strafe_dir = ((int)(level.time * 0.001f) % 2) ? 1 : -1;
    }
    
    // Calculate strafe amount based on threat distance
    float threat_dist = VectorLength(threat_dir);
    strafe_amount = movement->state.max_speed;
    
    // Closer threats require faster strafing
    if (threat_dist < 500) {
        strafe_amount *= 1.2f;
    }
    
    // Apply strafe movement
    VectorScale(right, strafe_amount * strafe_dir, move);
    
    // Add slight forward/backward movement for unpredictability
    float forward_amount = sin(level.time * 0.001f * 2.0f) * 100;
    VectorMA(move, forward_amount, threat_dir, move);
}

/*
==================
Movement_CanWallJump
==================
*/
qboolean Movement_CanWallJump(const parkour_state_t *parkour) {
    if (!parkour) {
        return qfalse;
    }
    
    // Can wall jump if:
    // 1. We have a valid wall normal
    // 2. We're not on the ground
    // 3. Wall jump is available (cooldown expired)
    // 4. We have momentum
    
    if (parkour->wall_normal[0] == 0 && parkour->wall_normal[1] == 0) {
        return qfalse;  // No wall detected
    }
    
    if (!parkour->can_wall_jump) {
        return qfalse;  // On cooldown
    }
    
    if (parkour->momentum < 0.3f) {
        return qfalse;  // Not enough momentum
    }
    
    return qtrue;
}

/*
==================
Movement_ExecuteWallJump
==================
*/
void Movement_ExecuteWallJump(tactical_movement_t *movement, vec3_t move) {
    vec3_t jump_dir;
    float jump_force;
    
    if (!movement || !Movement_CanWallJump(&movement->parkour)) {
        return;
    }
    
    // Jump direction is away from wall plus upward
    VectorCopy(movement->parkour.wall_normal, jump_dir);
    VectorNormalize(jump_dir);
    
    // Calculate jump force based on momentum
    jump_force = 400 + (movement->parkour.momentum * 200);
    
    // Apply horizontal jump
    VectorScale(jump_dir, jump_force, move);
    
    // Add upward component
    move[2] = 300 + (movement->parkour.momentum * 100);
    
    // Update parkour state
    movement->parkour.can_wall_jump = qfalse;
    movement->parkour.can_wall_jump = qfalse;
    movement->parkour.momentum = CLAMP(movement->parkour.momentum + 0.2f, 0, 1);
    movement->parkour.style_points += 1.0f;
    
    // Chain bonus for multiple wall jumps
    if (movement->parkour.trick_combo > 1) {
        movement->parkour.style_points += movement->parkour.trick_combo * 0.5f;
    }
    
    // Reset wall run availability after wall jump
    movement->parkour.wall_run_available = qtrue;
    movement->parkour.wall_run_time = 0;
}

