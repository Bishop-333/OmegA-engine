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

#ifndef SKILL_ADAPTATION_H
#define SKILL_ADAPTATION_H

#include "../../../engine/common/q_shared.h"

#define SKILL_WINDOW_SIZE 50
#define SKILL_UPDATE_INTERVAL 30000  // 30 seconds
#define SKILL_MIN_LEVEL 0.1f
#define SKILL_MAX_LEVEL 10.0f
#define SKILL_ADAPTATION_RATE 0.1f

typedef enum {
    METRIC_KILL_DEATH_RATIO,
    METRIC_ACCURACY,
    METRIC_DAMAGE_EFFICIENCY,
    METRIC_OBJECTIVE_COMPLETION,
    METRIC_SURVIVAL_TIME,
    METRIC_ITEM_CONTROL,
    METRIC_MOVEMENT_SKILL,
    METRIC_REACTION_TIME,
    METRIC_MAX
} performance_metric_t;

typedef struct skill_metrics_s {
    float values[METRIC_MAX];
    float weights[METRIC_MAX];
    float history[METRIC_MAX][SKILL_WINDOW_SIZE];
    int history_index[METRIC_MAX];
    float moving_average[METRIC_MAX];
    float variance[METRIC_MAX];
} skill_metrics_t;

typedef struct skill_profile_s {
    float base_skill_level;
    float current_skill_level;
    float target_skill_level;
    
    // Skill components
    float aim_skill;
    float movement_skill;
    float tactical_skill;
    float reaction_skill;
    float prediction_skill;
    float resource_management;
    float teamwork_skill;
    
    // Character-specific attributes from bot config
    float aim_accuracy;         // From character file
    float reaction_time;        // From character file
    float aggression;           // From character file
    float tactical_awareness;   // From character file
    float movement_prediction;  // Derived from skills
    
    // Adaptation parameters
    float learning_rate;
    float momentum;
    float adaptation_speed;
    float confidence;
    
    // Performance tracking
    skill_metrics_t player_metrics;
    skill_metrics_t bot_metrics;
    float performance_gap;
    float desired_win_rate;
    
    // Skill caps
    float min_skill;
    float max_skill;
    qboolean adaptive_enabled;
    qboolean smooth_transitions;
} skill_profile_t;

typedef struct adaptation_state_s {
    int client_num;
    skill_profile_t *profile;
    
    // Real-time tracking
    int last_update_time;
    int matches_played;
    int matches_won;
    float session_time;
    
    // Recent performance
    float recent_kd_ratio;
    float recent_accuracy;
    float recent_score_rate;
    
    // Engagement metrics
    float engagement_score;
    float frustration_level;
    float boredom_level;
    
    // Prediction
    float predicted_performance;
    float performance_trend;
} adaptation_state_t;

// Core functions
void Skill_InitSystem(void);
void Skill_ShutdownSystem(void);
skill_profile_t *Skill_CreateProfile(float initial_skill);
void Skill_DestroyProfile(skill_profile_t *profile);

// Skill adjustment
void Skill_UpdateMetrics(skill_profile_t *profile, performance_metric_t metric, float value);
void Skill_AnalyzePerformance(skill_profile_t *profile, adaptation_state_t *state);
void Skill_AdjustDifficulty(skill_profile_t *profile, adaptation_state_t *state);
float Skill_ComputeOptimalLevel(skill_profile_t *profile, adaptation_state_t *state);

// Metric computation
float Skill_ComputeKDRatio(int kills, int deaths);
float Skill_ComputeAccuracy(int shots_fired, int shots_hit);
float Skill_ComputeDamageEfficiency(float damage_dealt, float damage_received);
float Skill_ComputeMovementSkill(const vec3_t velocity_history[], int history_size);
float Skill_ComputeReactionTime(int reaction_samples[], int sample_count);

// Engagement analysis
float Skill_AnalyzeEngagement(adaptation_state_t *state);
float Skill_DetectFrustration(skill_profile_t *profile, adaptation_state_t *state);
float Skill_DetectBoredom(skill_profile_t *profile, adaptation_state_t *state);
void Skill_BalanceChallenge(skill_profile_t *profile, float engagement_score);

// Skill component getters
float Skill_GetAimAssist(skill_profile_t *profile);
float Skill_GetReactionDelay(skill_profile_t *profile);
float Skill_GetMovementSpeed(skill_profile_t *profile);
float Skill_GetTacticalAwareness(skill_profile_t *profile);
float Skill_GetPredictionAccuracy(skill_profile_t *profile);

// Smooth transitions
void Skill_InterpolateLevel(skill_profile_t *profile, float delta_time);
void Skill_ApplySmoothTransition(skill_profile_t *profile, float target_level, float transition_time);

// Persistence
void Skill_SaveProfile(skill_profile_t *profile, const char *filename);
skill_profile_t *Skill_LoadProfile(const char *filename);

// Debug and monitoring
void Skill_PrintMetrics(skill_profile_t *profile);
void Skill_DrawDebugInfo(skill_profile_t *profile, float x, float y);
const char *Skill_GetDifficultyName(float skill_level);

// Presets
skill_profile_t *Skill_GetNoobProfile(void);
skill_profile_t *Skill_GetBeginnerProfile(void);
skill_profile_t *Skill_GetIntermediateProfile(void);
skill_profile_t *Skill_GetAdvancedProfile(void);
skill_profile_t *Skill_GetExpertProfile(void);
skill_profile_t *Skill_GetProProfile(void);

#endif // SKILL_ADAPTATION_H

