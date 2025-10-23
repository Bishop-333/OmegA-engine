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

#ifndef AI_PERCEPTION_H
#define AI_PERCEPTION_H

#include "../../../engine/common/q_shared.h"

// Weapon constants from q_shared.h
#ifndef MAX_WEAPONS
#define MAX_WEAPONS 16
#endif

#define MAX_VISIBLE_ENTITIES 32
#define MAX_AUDIBLE_SOUNDS 16
#define MAX_DAMAGE_EVENTS 8
#define PERCEPTION_UPDATE_RATE 100 // milliseconds
#define VISION_CONE_ANGLE 120
#define HEARING_RANGE 1000
#define PERIPHERAL_VISION_ANGLE 160

typedef enum {
    ENTITY_PLAYER,
    ENTITY_ITEM,
    ENTITY_PROJECTILE,
    ENTITY_POWERUP,
    ENTITY_WEAPON,
    ENTITY_HEALTH,
    ENTITY_ARMOR,
    ENTITY_AMMO,
    ENTITY_OBJECTIVE,
    ENTITY_HAZARD
} entity_type_t;

typedef enum {
    SOUND_FOOTSTEP,
    SOUND_COMBAT,
    SOUND_ITEM_PICKUP,
    SOUND_JUMP,
    SOUND_PAIN,
    SOUND_DEATH,
    SOUND_WEAPON_FIRE,
    SOUND_EXPLOSION,
    SOUND_ENVIRONMENTAL
} sound_type_t;

typedef struct entity_info_s {
    int entity_num;
    entity_type_t entity_type;
    vec3_t position;
    vec3_t velocity;
    vec3_t angles;
    float distance;
    float threat_level;
    qboolean visible;
    qboolean is_enemy;
    qboolean is_ally;
    float last_seen_time;
    float visibility_confidence;
    
    // Player-specific
    int health;
    int armor;
    int weapon;
    int team;
    
    // Item-specific
    int item_type;
    float respawn_time;
    
    // Projectile-specific
    int owner;
    float impact_time;
    vec3_t predicted_impact;
} entity_info_t;

typedef struct sound_event_s {
    sound_type_t type;
    vec3_t origin;
    float volume;
    float timestamp;
    int source_entity;
    float distance;
    vec3_t direction;
    float confidence;
} sound_event_t;

typedef struct damage_event_s {
    int attacker;
    vec3_t damage_origin;
    vec3_t damage_direction;
    int damage_amount;
    int damage_type;
    float timestamp;
} damage_event_t;

typedef struct self_state_s {
    vec3_t position;
    vec3_t velocity;
    vec3_t angles;
    int health;
    int armor;
    int weapon;
    int ammo[MAX_WEAPONS];
    int powerups;
    float speed;
    qboolean on_ground;
    qboolean in_water;
    qboolean in_air;
    int team;
} self_state_t;

typedef struct spatial_awareness_s {
    float height_advantage;
    float open_space_ratio;
    int nearby_cover_points;
    float nearest_wall_distance;
    vec3_t escape_direction;
    qboolean cornered;
    qboolean has_high_ground;
    float map_control_estimate;
} spatial_awareness_t;

typedef struct threat_assessment_s {
    int primary_threat;
    int secondary_threat;
    float overall_threat_level;
    int threat_count;
    vec3_t threat_center;
    float time_to_impact; // For projectiles
    qboolean under_fire;
    qboolean flanked;
    qboolean outnumbered;
} threat_assessment_t;

typedef struct perception_filter_s {
    float max_vision_range;
    float fov_angle;
    float peripheral_sensitivity;
    float motion_detection_threshold;
    float sound_sensitivity;
    qboolean use_fog_of_war;
    qboolean simulate_distractions;
} perception_filter_t;

typedef struct perception_memory_s {
    entity_info_t remembered_entities[MAX_VISIBLE_ENTITIES];
    int num_remembered;
    float memory_decay_rate;
    float last_update_time;
} perception_memory_t;

typedef struct perception_config_s {
    float view_factor;          // Character-specific view factor (0-1)
    float max_view_change;      // Maximum view angle change per frame in degrees
    float alertness;            // Character alertness level (0-1)
} perception_config_t;

// Player info for tracking other players
typedef struct player_info_s {
    qboolean valid;
    char name[MAX_NAME_LENGTH];
    int team;
    int score;
    qboolean is_bot;
} player_info_t;

typedef struct perception_system_s {
    // Current perception
    entity_info_t visible_entities[MAX_VISIBLE_ENTITIES];
    int num_visible_entities;
    int num_visible_enemies;
    int num_visible_allies;
    int num_visible_items;
    
    // Current target tracking
    int current_enemy;          // Entity number of current enemy target
    
    // Player information
    player_info_t player_info[MAX_CLIENTS];
    
    sound_event_t sounds[MAX_AUDIBLE_SOUNDS];
    int num_sounds;
    
    damage_event_t damage_events[MAX_DAMAGE_EVENTS];
    int num_damage_events;
    
    // Self awareness
    self_state_t self_state;
    spatial_awareness_t spatial;
    threat_assessment_t threats;
    
    // Memory
    perception_memory_t memory;
    
    // Configuration
    perception_filter_t filter;
    perception_config_t config;  // Character-specific configuration
    
    // Timing
    float last_perception_time;
    float last_vision_update;
    float last_hearing_update;
    
    // Performance
    int rays_cast;
    float perception_time_ms;
} perception_system_t;

// Core functions
void Perception_Init(void);
void Perception_Shutdown(void);
perception_system_t *Perception_Create(void);
void Perception_Destroy(perception_system_t *perception);
void Perception_Reset(perception_system_t *perception);

// Update functions
void Perception_Update(perception_system_t *perception, int client_num);
void Perception_UpdateVision(perception_system_t *perception, int client_num);
void Perception_UpdateHearing(perception_system_t *perception, int client_num);
void Perception_UpdateSelfState(perception_system_t *perception, int client_num);
void Perception_UpdateSpatialAwareness(perception_system_t *perception);
void Perception_UpdateThreatAssessment(perception_system_t *perception);
void Perception_UpdateMemory(perception_system_t *perception);

// Vision
qboolean Perception_CanSeeEntity(perception_system_t *perception, int client_num, int entity_num);
float Perception_GetVisibility(perception_system_t *perception, const vec3_t position);
void Perception_ScanForEntities(perception_system_t *perception, int client_num);
qboolean Perception_IsInFOV(const vec3_t view_angles, const vec3_t to_target, float fov);
float Perception_CalculateVisibilityScore(perception_system_t *perception, entity_info_t *entity);

// Hearing
void Perception_ProcessSound(perception_system_t *perception, sound_type_t type, const vec3_t origin, float volume, int source);
float Perception_CalculateAudibility(perception_system_t *perception, const vec3_t origin, float volume);
void Perception_LocalizeSound(perception_system_t *perception, sound_event_t *sound);

// Damage perception
void Perception_RegisterDamage(perception_system_t *perception, int attacker, const vec3_t origin, int amount, int type);
void Perception_ProcessDamageEvents(perception_system_t *perception, int client_num);

// Threat assessment
float Perception_EvaluateThreat(perception_system_t *perception, entity_info_t *entity);
void Perception_IdentifyThreats(perception_system_t *perception);
void Perception_PredictProjectileImpact(perception_system_t *perception, entity_info_t *projectile);

// Spatial analysis
void Perception_AnalyzeSpace(perception_system_t *perception);
void Perception_FindEscapeRoutes(perception_system_t *perception);
float Perception_GetHeightAdvantage(const vec3_t position1, const vec3_t position2);
qboolean Perception_IsExposed(perception_system_t *perception);

// Memory functions
void Perception_RememberEntity(perception_system_t *perception, entity_info_t *entity);
entity_info_t *Perception_RecallEntity(perception_system_t *perception, int entity_num);
void Perception_ForgetOldMemories(perception_system_t *perception);
void Perception_MergeWithMemory(perception_system_t *perception);

// Filtering and configuration
void Perception_SetFilter(perception_system_t *perception, perception_filter_t *filter);
void Perception_ApplyFogOfWar(perception_system_t *perception);
void Perception_SimulateDistraction(perception_system_t *perception, const vec3_t origin, float intensity);

// Query functions
entity_info_t *Perception_GetNearestEnemy(perception_system_t *perception);
entity_info_t *Perception_GetNearestItem(perception_system_t *perception, int item_type);
entity_info_t *Perception_GetMostDangerousThreat(perception_system_t *perception);
int Perception_CountVisibleEnemies(perception_system_t *perception);
qboolean Perception_IsUnderAttack(perception_system_t *perception);

// Utility functions
float Perception_GetEntityDistance(perception_system_t *perception, int entity_num);
void Perception_GetEntityDirection(perception_system_t *perception, int entity_num, vec3_t direction);
qboolean Perception_HasLineOfSight(const vec3_t from, const vec3_t to);
void Perception_EstimateEntityState(perception_system_t *perception, entity_info_t *entity, float time);

// Debug functions
void Perception_DebugDraw(perception_system_t *perception);
void Perception_PrintStats(perception_system_t *perception);

#endif // AI_PERCEPTION_H


