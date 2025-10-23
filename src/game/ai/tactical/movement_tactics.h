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

#ifndef MOVEMENT_TACTICS_H
#define MOVEMENT_TACTICS_H

#include "../../../engine/common/q_shared.h"

// Forward declaration
#define MAX_WAYPOINTS 64
#define MAX_MOVEMENT_PREDICTIONS 8
#define DODGE_REACTION_TIME 200
#define STRAFE_CHANGE_TIME 500
#define WALL_JUMP_ANGLE 45
#define SLIDE_MIN_SPEED 320
#define BUNNY_HOP_TIMING 50

typedef enum {
    MOVE_STYLE_NORMAL,
    MOVE_STYLE_AGGRESSIVE,
    MOVE_STYLE_EVASIVE,
    MOVE_STYLE_STEALTH,
    MOVE_STYLE_PARKOUR,
    MOVE_STYLE_TACTICAL,
    MOVE_STYLE_RETREAT
} movement_style_t;

typedef enum {
    MOVE_TECH_NONE,
    MOVE_TECH_STRAFE_JUMP,
    MOVE_TECH_BUNNY_HOP,
    MOVE_TECH_ROCKET_JUMP,
    MOVE_TECH_PLASMA_CLIMB,
    MOVE_TECH_WALL_JUMP,
    MOVE_TECH_CIRCLE_JUMP,
    MOVE_TECH_SLIDE,
    MOVE_TECH_AIR_CONTROL
} movement_technique_t;

typedef enum {
    DODGE_TYPE_NONE,
    DODGE_TYPE_SIDESTEP,
    DODGE_TYPE_DUCK,
    DODGE_TYPE_JUMP,
    DODGE_TYPE_DIAGONAL,
    DODGE_TYPE_BACKPEDAL,
    DODGE_TYPE_SLIDE,
    DODGE_TYPE_ROLL
} dodge_type_t;

typedef struct movement_prediction_s {
    vec3_t position;
    vec3_t velocity;
    float time;
    float probability;
    qboolean on_ground;
    qboolean can_shoot;
} movement_prediction_t;

typedef struct waypoint_s {
    vec3_t position;
    vec3_t normal;
    float arrival_time;
    float wait_time;
    movement_technique_t technique;
    qboolean requires_jump;
    qboolean requires_crouch;
    float speed_modifier;
    int flags;
} waypoint_t;

typedef struct movement_path_s {
    waypoint_t waypoints[MAX_WAYPOINTS];
    int num_waypoints;
    int current_waypoint;
    float total_distance;
    float estimated_time;
    float danger_level;
    qboolean is_valid;
} movement_path_t;

typedef struct dodge_info_s {
    dodge_type_t type;
    vec3_t direction;
    float intensity;
    float duration;
    float start_time;
    qboolean in_progress;
    int success_count;
    int attempt_count;
} dodge_info_t;

typedef struct movement_state_s {
    vec3_t position;
    vec3_t velocity;
    vec3_t acceleration;
    vec3_t angles;
    vec3_t desired_direction;
    float speed;
    float max_speed;
    qboolean on_ground;
    qboolean against_wall;
    qboolean in_air;
    qboolean is_sliding;
    qboolean is_crouching;
    float ground_time;
    float air_time;
    float last_jump_time;
    int consecutive_jumps;
    
    // Character-specific traits
    float jump_frequency;       // How often the bot jumps (0-1)
    float crouch_frequency;     // How often the bot crouches (0-1)
    float walk_frequency;       // How often the bot walks vs runs (0-1)
} movement_state_t;

typedef struct parkour_state_s {
    qboolean wall_run_available;
    vec3_t wall_normal;
    float wall_run_time;
    float wall_run_height;
    qboolean can_wall_jump;
    vec3_t last_wall_jump_normal;
    float momentum;
    int trick_combo;
    float style_points;
} parkour_state_t;

typedef struct tactical_movement_s {
    movement_style_t style;
    vec3_t desired_velocity;  // Desired movement velocity
    movement_state_t state;
    movement_path_t path;
    dodge_info_t dodge;
    parkour_state_t parkour;
    
    // Predictions
    movement_prediction_t predictions[MAX_MOVEMENT_PREDICTIONS];
    int num_predictions;
    
    // Timing
    float last_direction_change;
    float last_technique_time;
    float strafe_pattern_time;
    
    // Performance
    float average_speed;
    float peak_speed;
    float distance_traveled;
    float evasion_success_rate;
    
    // Patterns
    int strafe_pattern;
    float strafe_amplitude;
    float strafe_frequency;
    qboolean random_strafe;
} tactical_movement_t;

// Core functions
void Movement_Init(void);
void Movement_Shutdown(void);
tactical_movement_t *Movement_Create(movement_style_t style);
void Movement_Destroy(tactical_movement_t *movement);
void Movement_Reset(tactical_movement_t *movement);

// Movement planning
void Movement_PlanPath(tactical_movement_t *movement, const vec3_t destination);
void Movement_UpdatePath(tactical_movement_t *movement);
qboolean Movement_HasReachedWaypoint(tactical_movement_t *movement, const waypoint_t *waypoint);
void Movement_NextWaypoint(tactical_movement_t *movement);
void Movement_RecalculatePath(tactical_movement_t *movement);

// Movement execution
void Movement_Execute(tactical_movement_t *movement, vec3_t move_dir, float *speed);
void Movement_UpdateState(tactical_movement_t *movement, const vec3_t position, const vec3_t velocity);
void Movement_ApplyTechnique(tactical_movement_t *movement, movement_technique_t technique);
void Movement_CalculateAcceleration(tactical_movement_t *movement, vec3_t accel);

// Strafe patterns
void Movement_GenerateStrafePattern(tactical_movement_t *movement, vec3_t strafe);
void Movement_CircleStrafe(tactical_movement_t *movement, const vec3_t center, float radius, vec3_t move);
void Movement_SerpentinePattern(tactical_movement_t *movement, vec3_t move);
void Movement_RandomStrafe(tactical_movement_t *movement, vec3_t move);
void Movement_PredictiveStrafe(tactical_movement_t *movement, const vec3_t threat_pos, vec3_t move);

// Dodging
void Movement_InitiateDodge(tactical_movement_t *movement, const vec3_t threat_dir);
void Movement_ExecuteDodge(tactical_movement_t *movement, vec3_t dodge_vector);
dodge_type_t Movement_SelectDodgeType(tactical_movement_t *movement, const vec3_t threat_dir);
float Movement_CalculateDodgeEffectiveness(const dodge_info_t *dodge, const vec3_t threat_dir);
qboolean Movement_ShouldDodge(tactical_movement_t *movement, const vec3_t projectile_pos, const vec3_t projectile_vel);

// Advanced techniques
qboolean Movement_CanStrafeJump(const movement_state_t *state);
void Movement_ExecuteStrafeJump(tactical_movement_t *movement, vec3_t move);
qboolean Movement_CanBunnyHop(const movement_state_t *state);
void Movement_ExecuteBunnyHop(tactical_movement_t *movement, vec3_t move);
qboolean Movement_CanRocketJump(const movement_state_t *state, int ammo);
void Movement_ExecuteRocketJump(tactical_movement_t *movement, vec3_t angles);
void Movement_AirControl(tactical_movement_t *movement, vec3_t move);

// Parkour
void Movement_UpdateParkour(tactical_movement_t *movement);
qboolean Movement_CanWallRun(const movement_state_t *state, const vec3_t wall_normal);
void Movement_ExecuteWallRun(tactical_movement_t *movement, vec3_t move);
qboolean Movement_CanWallJump(const parkour_state_t *parkour);
void Movement_ExecuteWallJump(tactical_movement_t *movement, vec3_t move);
void Movement_CalculateTrickCombo(parkour_state_t *parkour);

// Prediction
void Movement_PredictMovement(tactical_movement_t *movement, float time);
void Movement_PredictPlayerMovement(const vec3_t position, const vec3_t velocity, float time, vec3_t predicted_pos);
float Movement_EstimateArrivalTime(const vec3_t from, const vec3_t to, float speed);
qboolean Movement_WillCollide(const movement_prediction_t *pred, const vec3_t obstacle_pos, float obstacle_radius);

// Collision avoidance
void Movement_AvoidObstacle(tactical_movement_t *movement, const vec3_t obstacle_pos, float radius, vec3_t avoid_dir);
void Movement_AvoidMultipleObstacles(tactical_movement_t *movement, vec3_t obstacles[], int count, vec3_t avoid_dir);
qboolean Movement_CheckCollision(const vec3_t from, const vec3_t to);
void Movement_SlideMove(tactical_movement_t *movement, vec3_t move);

// Utility functions
float Movement_GetOptimalSpeed(movement_style_t style, qboolean in_combat);
float Movement_CalculateTurnRate(float current_speed);
void Movement_SmoothDirection(vec3_t current_dir, const vec3_t desired_dir, float rate, vec3_t result);
qboolean Movement_IsPathClear(const vec3_t from, const vec3_t to);
float Movement_GetSurfaceFriction(const vec3_t position);

// Style management
void Movement_SetStyle(tactical_movement_t *movement, movement_style_t style);
void Movement_AdaptStyle(tactical_movement_t *movement, qboolean in_combat, float threat_level);
movement_technique_t Movement_SelectTechnique(tactical_movement_t *movement, const vec3_t destination);

#endif // MOVEMENT_TACTICS_H



