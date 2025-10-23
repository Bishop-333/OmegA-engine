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

#ifndef TACTICAL_COMBAT_H
#define TACTICAL_COMBAT_H

#include "../../../engine/common/q_shared.h"
#include "../neural/nn_core.h"

#define MAX_THREATS 16
#define MAX_COMBAT_ZONES 32
#define MAX_ENGAGEMENT_RANGE 2000
#define OPTIMAL_ENGAGEMENT_RANGE 500
#define MAX_PREDICTION_TIME 2.0f

typedef enum {
    COMBAT_STATE_IDLE,
    COMBAT_STATE_SEARCHING,
    COMBAT_STATE_ENGAGING,
    COMBAT_STATE_PURSUING,
    COMBAT_STATE_RETREATING,
    COMBAT_STATE_FLANKING,
    COMBAT_STATE_SUPPRESSING,
    COMBAT_STATE_AMBUSHING,
    COMBAT_STATE_DEFENDING,
    COMBAT_STATE_EVADING
} combat_state_t;

typedef enum {
    COMBAT_STYLE_AGGRESSIVE,
    COMBAT_STYLE_DEFENSIVE,
    COMBAT_STYLE_BALANCED,
    COMBAT_STYLE_SNIPER,
    COMBAT_STYLE_RUSHER,
    COMBAT_STYLE_SUPPORT,
    COMBAT_STYLE_GUERRILLA,
    COMBAT_STYLE_TACTICAL
} combat_style_t;

typedef enum {
    THREAT_LEVEL_NONE,
    THREAT_LEVEL_LOW,
    THREAT_LEVEL_MEDIUM,
    THREAT_LEVEL_HIGH,
    THREAT_LEVEL_CRITICAL
} threat_level_t;

typedef struct threat_info_s {
    int entity_num;
    vec3_t position;
    vec3_t velocity;
    vec3_t predicted_position;
    float distance;
    float threat_score;
    threat_level_t threat_level;
    int weapon;
    float health;
    float armor;
    qboolean visible;
    qboolean can_hit_me;
    qboolean i_can_hit;
    float time_visible;
    float last_seen_time;
    float accuracy_against_me;
    int damage_dealt_to_me;
    int damage_dealt_by_me;
} threat_info_t;

typedef struct combat_zone_s {
    vec3_t center;
    float radius;
    float danger_level;
    int enemy_count;
    int ally_count;
    float control_strength;
    qboolean contested;
    float importance;
} combat_zone_t;

typedef struct engagement_params_s {
    float optimal_range;
    float min_range;
    float max_range;
    float aim_accuracy;
    float prediction_time;
    float burst_duration;
    float suppression_time;
    qboolean use_splash_damage;
    qboolean prefer_direct_hit;
    float strafe_speed;
    float dodge_probability;
} engagement_params_t;

typedef struct combat_decision_s {
    combat_state_t recommended_state;
    int primary_target;
    int secondary_target;
    vec3_t movement_destination;
    vec3_t aim_position;
    int weapon_choice;
    float confidence;
    qboolean should_retreat;
    qboolean should_take_cover;
    qboolean should_flank;
    float aggression_level;
} combat_decision_t;

typedef struct combat_memory_s {
    threat_info_t threats[MAX_THREATS];
    int threat_count;
    int last_attacker;
    float last_damage_time;
    vec3_t last_enemy_position;
    float enemy_last_seen;
    int kills_this_life;
    int deaths_to_current_enemy;
    float combat_start_time;
    float time_under_fire;
    float time_in_combat;
} combat_memory_t;

typedef struct tactical_combat_s {
    combat_state_t current_state;
    combat_state_t previous_state;
    combat_style_t style;  // Renamed from combat_style for consistency
    combat_memory_t memory;
    combat_decision_t decision;
    engagement_params_t engagement;
    
    // Neural network for combat decisions
    nn_network_t *combat_network;
    
    // Performance tracking
    float accuracy;
    float dodge_success_rate;
    
    // Character-specific traits
    float aggression;           // How aggressive the bot is (0-1)
    float reaction_delay;       // Reaction time in milliseconds
    float fire_threshold;       // Threshold for firing (0-1)
    float kill_death_ratio;
    float damage_efficiency;
    
    // Timing
    float state_change_time;
    float last_decision_time;
    
    // Current combat state
    qboolean firing;           // Currently firing weapon
    int current_weapon;        // Current weapon equipped
    
    // Flags
    qboolean under_fire;
    qboolean low_health;
    qboolean low_ammo;
    qboolean has_advantage;
    qboolean outnumbered;
    
    // Threat tracking
    int threat_count;          // Number of current threats
    float time_in_combat;      // Time spent in combat state
} tactical_combat_t;

// Core combat functions
void Combat_Init(void);
void Combat_Shutdown(void);
tactical_combat_t *Combat_Create(combat_style_t style);
void Combat_Destroy(tactical_combat_t *combat);
void Combat_Reset(tactical_combat_t *combat);

// Threat assessment
void Combat_UpdateThreats(tactical_combat_t *combat, const vec3_t origin);
void Combat_EvaluateThreat(tactical_combat_t *combat, threat_info_t *threat);
threat_info_t *Combat_GetPrimaryThreat(tactical_combat_t *combat);
threat_info_t *Combat_GetNearestThreat(tactical_combat_t *combat);
threat_info_t *Combat_GetWeakestThreat(tactical_combat_t *combat);
float Combat_CalculateThreatScore(const threat_info_t *threat);

// Combat decision making
void Combat_MakeDecision(tactical_combat_t *combat);
combat_state_t Combat_SelectState(tactical_combat_t *combat);
void Combat_UpdateState(tactical_combat_t *combat, combat_state_t new_state);
void Combat_ExecuteState(tactical_combat_t *combat);

// Engagement tactics
void Combat_EngageTarget(tactical_combat_t *combat, threat_info_t *target);
void Combat_CalculateAimPoint(tactical_combat_t *combat, threat_info_t *target, vec3_t aim_point);
void Combat_PredictTargetPosition(const threat_info_t *target, float time, vec3_t predicted_pos);
float Combat_CalculateHitProbability(tactical_combat_t *combat, threat_info_t *target);
void Combat_SelectEngagementRange(tactical_combat_t *combat, threat_info_t *target);

// Weapon selection
int Combat_SelectWeapon(tactical_combat_t *combat, threat_info_t *target);
float Combat_GetWeaponEffectiveness(int weapon, float distance, qboolean has_armor);
int Combat_GetBestWeaponForRange(float distance);
qboolean Combat_ShouldSwitchWeapon(tactical_combat_t *combat, threat_info_t *target);

// Movement tactics
void Combat_CalculateDodgeVector(tactical_combat_t *combat, vec3_t dodge);
void Combat_CalculateStrafePattern(tactical_combat_t *combat, vec3_t strafe);
void Combat_CalculateRetreatPath(tactical_combat_t *combat, vec3_t retreat);
void Combat_CalculateFlankingRoute(tactical_combat_t *combat, threat_info_t *target, vec3_t flank);
qboolean Combat_ShouldJump(tactical_combat_t *combat);
qboolean Combat_ShouldCrouch(tactical_combat_t *combat);

// Combat zones
void Combat_UpdateZones(combat_zone_t *zones, int *zone_count);
combat_zone_t *Combat_GetSafestZone(combat_zone_t *zones, int zone_count);
combat_zone_t *Combat_GetOptimalZone(tactical_combat_t *combat, combat_zone_t *zones, int zone_count);
float Combat_EvaluatePosition(const vec3_t position, tactical_combat_t *combat);

// Suppression and area denial
void Combat_SuppressionFire(tactical_combat_t *combat, const vec3_t target_area);
void Combat_AreaDenial(tactical_combat_t *combat, const vec3_t area, float radius);
qboolean Combat_IsSuppressed(tactical_combat_t *combat);
float Combat_GetSuppressionLevel(tactical_combat_t *combat);

// Combat styles
void Combat_SetStyle(tactical_combat_t *combat, combat_style_t style);
void Combat_AdaptStyle(tactical_combat_t *combat);
engagement_params_t Combat_GetStyleParameters(combat_style_t style);

// Utility functions
float Combat_GetEffectiveRange(int weapon);
float Combat_CalculateDamagePerSecond(int weapon, float accuracy);
float Combat_EstimateTimeToKill(float health, float armor, int weapon, float accuracy);
qboolean Combat_HasLineOfSight(const vec3_t from, const vec3_t to);
qboolean Combat_IsInCover(const vec3_t position);

#endif // TACTICAL_COMBAT_H



