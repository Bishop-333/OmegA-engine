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

#ifndef COVER_SYSTEM_H
#define COVER_SYSTEM_H

#include "../../../engine/common/q_shared.h"

#define MAX_COVER_POINTS 256
#define MAX_COVER_SEARCH_RADIUS 1000
#define COVER_POINT_SPACING 64
#define COVER_HEIGHT_THRESHOLD 48
#define COVER_MIN_WIDTH 32
#define COVER_EVALUATION_SAMPLES 8

typedef enum {
    COVER_TYPE_NONE,
    COVER_TYPE_LOW,      // Crouch cover
    COVER_TYPE_HIGH,     // Standing cover
    COVER_TYPE_CORNER,   // Corner peek
    COVER_TYPE_PILLAR,   // 360 degree cover
    COVER_TYPE_WINDOW,   // Window/opening
    COVER_TYPE_DOOR,     // Doorway
    COVER_TYPE_EDGE      // Map edge/dropoff
} cover_type_t;

typedef enum {
    COVER_QUALITY_POOR,
    COVER_QUALITY_FAIR,
    COVER_QUALITY_GOOD,
    COVER_QUALITY_EXCELLENT
} cover_quality_t;

typedef struct cover_point_s {
    vec3_t position;
    vec3_t normal;           // Direction cover faces
    cover_type_t type;
    cover_quality_t quality;
    float height;            // Cover height
    float width;             // Cover width
    float protection_angle;  // Angle of protection (degrees)
    int protection_directions; // Bitfield of protected directions
    qboolean is_corner;
    qboolean allows_peek_left;
    qboolean allows_peek_right;
    qboolean allows_peek_over;
    qboolean allows_blind_fire;
    float last_used_time;
    int times_used;
    float danger_level;      // Current danger at this position
    vec3_t threat_direction; // Primary threat direction
    int connected_covers[4]; // Indices of nearby cover points
    int num_connections;
} cover_point_t;

typedef struct cover_search_params_s {
    vec3_t search_origin;
    float search_radius;
    vec3_t threat_position;
    vec3_t preferred_direction;
    float min_distance_from_threat;
    float max_distance_from_threat;
    cover_type_t preferred_type;
    qboolean require_los_to_threat;
    qboolean allow_exposed_movement;
    float time_pressure;     // How urgently cover is needed
} cover_search_params_t;

typedef struct cover_evaluation_s {
    float protection_score;
    float position_score;
    float tactical_score;
    float accessibility_score;
    float total_score;
    qboolean is_valid;
    float distance_to_cover;
    float distance_to_threat;
    float exposure_time;     // Time exposed while moving to cover
} cover_evaluation_t;

typedef struct cover_manager_s {
    cover_point_t cover_points[MAX_COVER_POINTS];
    int num_cover_points;
    int cover_grid[64][64][16]; // Spatial hash for fast lookups
    float last_analysis_time;
    qboolean needs_update;
    
    // Dynamic cover
    int dynamic_covers[32];
    int num_dynamic_covers;
    
    // Statistics
    int total_cover_uses;
    float average_cover_quality;
    int successful_cover_uses;
} cover_manager_t;

typedef struct cover_state_s {
    int current_cover_index;
    cover_point_t *current_cover;
    float time_in_cover;
    float last_peek_time;
    int peek_count;
    qboolean is_suppressed;
    qboolean needs_new_cover;
    vec3_t peek_position;
    vec3_t lean_angles;
    float exposure_percentage;
} cover_state_t;

// Core functions
void Cover_InitSystem(void);
void Cover_ShutdownSystem(void);
cover_manager_t *Cover_CreateManager(void);
void Cover_DestroyManager(cover_manager_t *manager);

// Cover analysis and generation
void Cover_AnalyzeMap(cover_manager_t *manager);
void Cover_FindCoverPoints(cover_manager_t *manager, const vec3_t origin, float radius);
qboolean Cover_ValidateCoverPoint(const vec3_t position, cover_type_t *type, float *quality);
void Cover_UpdateDynamicCover(cover_manager_t *manager);
void Cover_AddDynamicCover(cover_manager_t *manager, const vec3_t position, cover_type_t type);

// Cover search and selection
cover_point_t *Cover_FindBestCover(cover_manager_t *manager, const cover_search_params_t *params);
cover_point_t *Cover_FindNearestCover(cover_manager_t *manager, const vec3_t origin);
cover_point_t *Cover_FindSafestCover(cover_manager_t *manager, const vec3_t origin, const vec3_t threat);
cover_point_t *Cover_FindTacticalCover(cover_manager_t *manager, const vec3_t origin, const vec3_t objective);
void Cover_GetCoverChain(cover_manager_t *manager, const vec3_t start, const vec3_t end, cover_point_t **chain, int *chain_length);

// Cover evaluation
cover_evaluation_t Cover_EvaluatePoint(const cover_point_t *cover, const cover_search_params_t *params);
float Cover_CalculateProtection(const cover_point_t *cover, const vec3_t threat_pos);
float Cover_CalculateExposure(const vec3_t from, const vec3_t to, const vec3_t threat_pos);
qboolean Cover_ProvidesProtectionFrom(const cover_point_t *cover, const vec3_t threat_pos);
float Cover_GetOptimalDistance(cover_type_t type, int weapon);

// Cover usage
void Cover_EnterCover(cover_state_t *state, cover_point_t *cover);
void Cover_ExitCover(cover_state_t *state);
void Cover_UpdateInCover(cover_state_t *state, float delta_time);
qboolean Cover_ShouldPeek(cover_state_t *state);
qboolean Cover_ShouldRelocate(cover_state_t *state, float danger_level);

// Peeking and leaning
void Cover_CalculatePeekPosition(const cover_point_t *cover, const vec3_t target, vec3_t peek_pos);
void Cover_CalculateLeanAngles(const cover_point_t *cover, qboolean left, vec3_t angles);
float Cover_GetExposureForPeek(const cover_point_t *cover, const vec3_t target);
qboolean Cover_CanBlindFire(const cover_point_t *cover, const vec3_t target);

// Movement between cover
void Cover_PlanMovementPath(cover_manager_t *manager, const vec3_t start, const vec3_t end, vec3_t *waypoints, int *num_waypoints);
float Cover_CalculateMovementRisk(cover_manager_t *manager, const vec3_t from, const vec3_t to, const vec3_t threat);
qboolean Cover_IsPathExposed(const vec3_t from, const vec3_t to, const vec3_t threat);
void Cover_GetBoundingPath(cover_manager_t *manager, const vec3_t start, const vec3_t end, vec3_t *waypoints, int *num_waypoints);

// Utility functions
void Cover_DebugDraw(cover_manager_t *manager);
void Cover_DebugDrawPoint(const cover_point_t *cover);
const char *Cover_GetTypeName(cover_type_t type);
const char *Cover_GetQualityName(cover_quality_t quality);
void Cover_GetNearbyCovers(cover_manager_t *manager, const vec3_t origin, float radius, cover_point_t **covers, int *count);

// Spatial queries
int Cover_GetGridIndex(const vec3_t position, int *x, int *y, int *z);
void Cover_AddToGrid(cover_manager_t *manager, int cover_index);
void Cover_RemoveFromGrid(cover_manager_t *manager, int cover_index);
void Cover_QueryGrid(cover_manager_t *manager, const vec3_t mins, const vec3_t maxs, int *indices, int *count);

#endif // COVER_SYSTEM_H

