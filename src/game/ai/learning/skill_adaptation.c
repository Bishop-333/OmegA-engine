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

#include "skill_adaptation.h"
#include "../../../engine/core/qcommon.h"
#include "../game_interface.h"
#include "../ai_system.h"
#include "../game_memory.h"
#include <math.h>
#include <string.h>

static struct {
    qboolean initialized;
    skill_profile_t *profiles[MAX_CLIENTS];
    adaptation_state_t states[MAX_CLIENTS];
    int profile_count;
    float global_skill_offset;
    cvar_t *skill_adapt;
    cvar_t *skill_min;
    cvar_t *skill_max;
    cvar_t *skill_rate;
} skill_global;

/*
==================
Skill_InitSystem
==================
*/
void Skill_InitSystem(void) {
    if (skill_global.initialized) {
        return;
    }
    
    memset(&skill_global, 0, sizeof(skill_global));
    
    // Register cvars
    skill_global.skill_adapt = Cvar_Get("ai_skill_adapt", "1", CVAR_ARCHIVE);
    skill_global.skill_min = Cvar_Get("ai_skill_min", "0.5", CVAR_ARCHIVE);
    skill_global.skill_max = Cvar_Get("ai_skill_max", "5.0", CVAR_ARCHIVE);
    skill_global.skill_rate = Cvar_Get("ai_skill_rate", "0.1", CVAR_ARCHIVE);
    
    skill_global.initialized = qtrue;
    
    Com_Printf("Dynamic Skill Adaptation System Initialized\n");
}

/*
==================
Skill_ShutdownSystem
==================
*/
void Skill_ShutdownSystem(void) {
    int i;
    
    if (!skill_global.initialized) {
        return;
    }
    
    // Free all profiles
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (skill_global.profiles[i]) {
            Skill_DestroyProfile(skill_global.profiles[i]);
        }
    }
    
    skill_global.initialized = qfalse;
    Com_Printf("Dynamic Skill Adaptation System Shutdown\n");
}

/*
==================
Skill_CreateProfile
==================
*/
skill_profile_t *Skill_CreateProfile(float initial_skill) {
    skill_profile_t *profile;
    int i;
    
    profile = (skill_profile_t *)Z_Malloc(sizeof(skill_profile_t));
    memset(profile, 0, sizeof(skill_profile_t));
    
    // Initialize skill levels
    profile->base_skill_level = initial_skill;
    profile->current_skill_level = initial_skill;
    profile->target_skill_level = initial_skill;
    
    // Initialize skill components based on overall skill
    profile->aim_skill = initial_skill;
    profile->movement_skill = initial_skill * 0.9f;
    profile->tactical_skill = initial_skill * 0.8f;
    profile->reaction_skill = initial_skill;
    profile->prediction_skill = initial_skill * 0.7f;
    profile->resource_management = initial_skill * 0.85f;
    profile->teamwork_skill = initial_skill * 0.75f;
    
    // Adaptation parameters
    profile->learning_rate = skill_global.skill_rate ? skill_global.skill_rate->value : 0.1f;
    profile->momentum = 0.8f;
    profile->adaptation_speed = 0.05f;
    profile->confidence = 0.5f;
    
    // Performance targets
    profile->desired_win_rate = 0.45f;  // Slightly below 50% for player satisfaction
    
    // Skill bounds
    profile->min_skill = skill_global.skill_min ? skill_global.skill_min->value : 0.5f;
    profile->max_skill = skill_global.skill_max ? skill_global.skill_max->value : 5.0f;
    profile->adaptive_enabled = skill_global.skill_adapt ? skill_global.skill_adapt->integer : qtrue;
    profile->smooth_transitions = qtrue;
    
    // Initialize metric weights
    profile->player_metrics.weights[METRIC_KILL_DEATH_RATIO] = 0.3f;
    profile->player_metrics.weights[METRIC_ACCURACY] = 0.2f;
    profile->player_metrics.weights[METRIC_DAMAGE_EFFICIENCY] = 0.15f;
    profile->player_metrics.weights[METRIC_OBJECTIVE_COMPLETION] = 0.15f;
    profile->player_metrics.weights[METRIC_SURVIVAL_TIME] = 0.1f;
    profile->player_metrics.weights[METRIC_ITEM_CONTROL] = 0.05f;
    profile->player_metrics.weights[METRIC_MOVEMENT_SKILL] = 0.03f;
    profile->player_metrics.weights[METRIC_REACTION_TIME] = 0.02f;
    
    // Same weights for bot metrics
    memcpy(profile->bot_metrics.weights, profile->player_metrics.weights, sizeof(profile->player_metrics.weights));
    
    // Add to global list
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!skill_global.profiles[i]) {
            skill_global.profiles[i] = profile;
            skill_global.profile_count++;
            break;
        }
    }
    
    Com_DPrintf("Created skill profile with initial level %.2f\n", initial_skill);
    
    return profile;
}

/*
==================
Skill_DestroyProfile
==================
*/
void Skill_DestroyProfile(skill_profile_t *profile) {
    int i;
    
    if (!profile) {
        return;
    }
    
    // Remove from global list
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (skill_global.profiles[i] == profile) {
            skill_global.profiles[i] = NULL;
            skill_global.profile_count--;
            break;
        }
    }
    
    Z_Free(profile);
}

/*
==================
Skill_UpdateMetrics
==================
*/
void Skill_UpdateMetrics(skill_profile_t *profile, performance_metric_t metric, float value) {
    skill_metrics_t *metrics;
    int idx;
    float sum, old_value;
    int i;
    
    if (!profile || metric >= METRIC_MAX) {
        return;
    }
    
    metrics = &profile->player_metrics;
    idx = metrics->history_index[metric];
    
    // Store in circular buffer
    old_value = metrics->history[metric][idx];
    metrics->history[metric][idx] = value;
    metrics->history_index[metric] = (idx + 1) % SKILL_WINDOW_SIZE;
    
    // Update moving average
    sum = 0;
    for (i = 0; i < SKILL_WINDOW_SIZE; i++) {
        sum += metrics->history[metric][i];
    }
    metrics->moving_average[metric] = sum / SKILL_WINDOW_SIZE;
    
    // Update variance
    float variance = 0;
    for (i = 0; i < SKILL_WINDOW_SIZE; i++) {
        float diff = metrics->history[metric][i] - metrics->moving_average[metric];
        variance += diff * diff;
    }
    metrics->variance[metric] = variance / SKILL_WINDOW_SIZE;
    
    // Update current value
    metrics->values[metric] = value;
}

/*
==================
Skill_AnalyzePerformance
==================
*/
void Skill_AnalyzePerformance(skill_profile_t *profile, adaptation_state_t *state) {
    float player_score = 0;
    float bot_score = 0;
    int i;
    
    if (!profile || !state) {
        return;
    }
    
    // Calculate weighted performance scores
    for (i = 0; i < METRIC_MAX; i++) {
        player_score += profile->player_metrics.moving_average[i] * profile->player_metrics.weights[i];
        bot_score += profile->bot_metrics.moving_average[i] * profile->bot_metrics.weights[i];
    }
    
    // Calculate performance gap
    profile->performance_gap = player_score - bot_score;
    
    // Analyze engagement
    state->engagement_score = Skill_AnalyzeEngagement(state);
    state->frustration_level = Skill_DetectFrustration(profile, state);
    state->boredom_level = Skill_DetectBoredom(profile, state);
    
    // Calculate performance trend
    float recent_performance = (state->recent_kd_ratio + state->recent_accuracy + state->recent_score_rate) / 3.0f;
    state->performance_trend = recent_performance - state->predicted_performance;
    state->predicted_performance = recent_performance;
    
    Com_DPrintf("Performance Analysis: Gap=%.2f, Engagement=%.2f, Frustration=%.2f, Boredom=%.2f\n",
               profile->performance_gap, state->engagement_score, state->frustration_level, state->boredom_level);
}

/*
==================
Skill_AdjustDifficulty
==================
*/
void Skill_AdjustDifficulty(skill_profile_t *profile, adaptation_state_t *state) {
    float target_skill;
    float adjustment;
    int current_time;
    
    if (!profile || !state || !profile->adaptive_enabled) {
        return;
    }
    
    current_time = Sys_Milliseconds();
    
    // Check if enough time has passed
    if (current_time - state->last_update_time < SKILL_UPDATE_INTERVAL) {
        return;
    }
    
    state->last_update_time = current_time;
    
    // Compute optimal skill level
    target_skill = Skill_ComputeOptimalLevel(profile, state);
    
    // Apply momentum to smooth changes
    profile->target_skill_level = profile->target_skill_level * profile->momentum + 
                                  target_skill * (1.0f - profile->momentum);
    
    // Clamp to bounds
    profile->target_skill_level = CLAMP(profile->target_skill_level, profile->min_skill, profile->max_skill);
    
    // Adjust individual skill components
    float skill_delta = profile->target_skill_level - profile->current_skill_level;
    
    // Different components adapt at different rates
    profile->aim_skill += skill_delta * 1.0f;
    profile->movement_skill += skill_delta * 0.8f;
    profile->tactical_skill += skill_delta * 0.6f;
    profile->reaction_skill += skill_delta * 1.2f;
    profile->prediction_skill += skill_delta * 0.5f;
    profile->resource_management += skill_delta * 0.7f;
    profile->teamwork_skill += skill_delta * 0.4f;
    
    // Clamp all components
    profile->aim_skill = CLAMP(profile->aim_skill, profile->min_skill, profile->max_skill);
    profile->movement_skill = CLAMP(profile->movement_skill, profile->min_skill, profile->max_skill);
    profile->tactical_skill = CLAMP(profile->tactical_skill, profile->min_skill, profile->max_skill);
    profile->reaction_skill = CLAMP(profile->reaction_skill, profile->min_skill, profile->max_skill);
    profile->prediction_skill = CLAMP(profile->prediction_skill, profile->min_skill, profile->max_skill);
    profile->resource_management = CLAMP(profile->resource_management, profile->min_skill, profile->max_skill);
    profile->teamwork_skill = CLAMP(profile->teamwork_skill, profile->min_skill, profile->max_skill);
    
    Com_DPrintf("Skill Adjusted: %.2f -> %.2f (target: %.2f)\n", 
               profile->current_skill_level, profile->target_skill_level, target_skill);
}

/*
==================
Skill_ComputeOptimalLevel
==================
*/
float Skill_ComputeOptimalLevel(skill_profile_t *profile, adaptation_state_t *state) {
    float optimal_level = profile->current_skill_level;
    float win_rate;
    float adjustment = 0;
    
    if (!profile || !state) {
        return optimal_level;
    }
    
    // Calculate win rate
    if (state->matches_played > 0) {
        win_rate = (float)state->matches_won / state->matches_played;
    } else {
        win_rate = 0.5f;
    }
    
    // Primary adjustment based on win rate
    float win_rate_error = profile->desired_win_rate - win_rate;
    adjustment += win_rate_error * 2.0f;
    
    // Adjust based on engagement metrics
    if (state->frustration_level > 0.7f) {
        // Player is frustrated, decrease difficulty
        adjustment -= 0.5f * state->frustration_level;
    } else if (state->boredom_level > 0.7f) {
        // Player is bored, increase difficulty
        adjustment += 0.5f * state->boredom_level;
    }
    
    // Consider performance gap
    adjustment += profile->performance_gap * 0.3f;
    
    // Consider performance trend
    if (state->performance_trend > 0) {
        // Player improving, can increase difficulty
        adjustment += state->performance_trend * 0.2f;
    } else if (state->performance_trend < -0.5f) {
        // Player struggling, decrease difficulty
        adjustment += state->performance_trend * 0.3f;
    }
    
    // Apply learning rate
    adjustment *= profile->learning_rate;
    
    optimal_level = profile->current_skill_level + adjustment;
    
    return optimal_level;
}

/*
==================
Skill_AnalyzeEngagement
==================
*/
float Skill_AnalyzeEngagement(adaptation_state_t *state) {
    float engagement = 0.5f;
    
    if (!state) {
        return engagement;
    }
    
    // Factors that increase engagement:
    // - Balanced K/D ratio (near 1.0)
    float kd_balance = 1.0f - fabsf(state->recent_kd_ratio - 1.0f) / 2.0f;
    engagement += kd_balance * 0.3f;
    
    // - Good accuracy (but not perfect)
    if (state->recent_accuracy > 0.2f && state->recent_accuracy < 0.8f) {
        engagement += (state->recent_accuracy - 0.2f) * 0.3f;
    }
    
    // - Consistent scoring
    engagement += MIN(state->recent_score_rate / 100.0f, 1.0f) * 0.2f;
    
    // - Match participation
    float participation = (float)state->matches_played / MAX(state->session_time / 300000.0f, 1.0f);
    engagement += MIN(participation, 1.0f) * 0.2f;
    
    engagement = CLAMP(engagement, 0, 1);
    
    return engagement;
}

/*
==================
Skill_DetectFrustration
==================
*/
float Skill_DetectFrustration(skill_profile_t *profile, adaptation_state_t *state) {
    float frustration = 0;
    
    if (!profile || !state) {
        return frustration;
    }
    
    // Death streaks
    if (state->recent_kd_ratio < 0.3f) {
        frustration += (0.3f - state->recent_kd_ratio) * 2.0f;
    }
    
    // Low accuracy
    if (state->recent_accuracy < 0.15f) {
        frustration += (0.15f - state->recent_accuracy) * 3.0f;
    }
    
    // Negative performance trend
    if (state->performance_trend < -0.3f) {
        frustration += -state->performance_trend;
    }
    
    // Large negative performance gap
    if (profile->performance_gap < -1.0f) {
        frustration += -profile->performance_gap * 0.3f;
    }
    
    frustration = CLAMP(frustration, 0, 1);
    
    return frustration;
}

/*
==================
Skill_DetectBoredom
==================
*/
float Skill_DetectBoredom(skill_profile_t *profile, adaptation_state_t *state) {
    float boredom = 0;
    
    if (!profile || !state) {
        return boredom;
    }
    
    // Too easy - high K/D ratio
    if (state->recent_kd_ratio > 3.0f) {
        boredom += (state->recent_kd_ratio - 3.0f) * 0.3f;
    }
    
    // Very high accuracy (might indicate enemies too easy)
    if (state->recent_accuracy > 0.7f) {
        boredom += (state->recent_accuracy - 0.7f) * 2.0f;
    }
    
    // Large positive performance gap
    if (profile->performance_gap > 2.0f) {
        boredom += (profile->performance_gap - 2.0f) * 0.3f;
    }
    
    // Low variance in metrics (repetitive gameplay)
    float avg_variance = 0;
    for (int i = 0; i < METRIC_MAX; i++) {
        avg_variance += profile->player_metrics.variance[i];
    }
    avg_variance /= METRIC_MAX;
    
    if (avg_variance < 0.1f) {
        boredom += (0.1f - avg_variance) * 5.0f;
    }
    
    boredom = CLAMP(boredom, 0, 1);
    
    return boredom;
}

/*
==================
Skill_InterpolateLevel
==================
*/
void Skill_InterpolateLevel(skill_profile_t *profile, float delta_time) {
    float interpolation_speed = 0.5f;  // Skills per second
    float max_change;
    
    if (!profile || !profile->smooth_transitions) {
        return;
    }
    
    max_change = interpolation_speed * delta_time;
    
    // Smoothly interpolate towards target
    if (fabsf(profile->target_skill_level - profile->current_skill_level) > 0.01f) {
        float diff = profile->target_skill_level - profile->current_skill_level;
        float change = CLAMP(diff, -max_change, max_change);
        profile->current_skill_level += change;
    }
}

/*
==================
Skill_GetAimAssist
==================
*/
float Skill_GetAimAssist(skill_profile_t *profile) {
    if (!profile) {
        return 0;
    }
    
    // Lower skill = more aim assist (inverted)
    float assist = (profile->max_skill - profile->aim_skill) / profile->max_skill;
    return assist * 0.5f;  // Max 50% aim assist
}

/*
==================
Skill_GetReactionDelay
==================
*/
float Skill_GetReactionDelay(skill_profile_t *profile) {
    if (!profile) {
        return 200;
    }
    
    // Higher skill = lower reaction time
    float base_reaction = 500;  // ms
    float min_reaction = 50;    // ms
    
    float normalized_skill = profile->reaction_skill / profile->max_skill;
    float delay = base_reaction - (base_reaction - min_reaction) * normalized_skill;
    
    return delay;
}

/*
==================
Skill_GetMovementSpeed
==================
*/
float Skill_GetMovementSpeed(skill_profile_t *profile) {
    if (!profile) {
        return 1.0f;
    }
    
    // Movement speed multiplier based on skill
    float normalized_skill = profile->movement_skill / profile->max_skill;
    return 0.7f + 0.3f * normalized_skill;  // 70% to 100% speed
}

/*
==================
Skill_GetTacticalAwareness
==================
*/
float Skill_GetTacticalAwareness(skill_profile_t *profile) {
    if (!profile) {
        return 0.5f;
    }
    
    return profile->tactical_skill / profile->max_skill;
}

/*
==================
Skill_GetPredictionAccuracy
==================
*/
float Skill_GetPredictionAccuracy(skill_profile_t *profile) {
    if (!profile) {
        return 0.5f;
    }
    
    return profile->prediction_skill / profile->max_skill;
}

/*
==================
Skill_GetDifficultyName
==================
*/
const char *Skill_GetDifficultyName(float skill_level) {
    if (skill_level < 1.0f) return "Novice";
    if (skill_level < 2.0f) return "Easy";
    if (skill_level < 3.0f) return "Normal";
    if (skill_level < 4.0f) return "Hard";
    if (skill_level < 5.0f) return "Expert";
    if (skill_level < 7.0f) return "Master";
    if (skill_level < 9.0f) return "Legendary";
    return "Godlike";
}

/*
==================
Skill Preset Profiles
==================
*/
skill_profile_t *Skill_GetNoobProfile(void) {
    skill_profile_t *profile = Skill_CreateProfile(0.5f);
    profile->aim_skill = 0.3f;
    profile->movement_skill = 0.4f;
    profile->tactical_skill = 0.2f;
    profile->reaction_skill = 0.5f;
    profile->adaptive_enabled = qfalse;
    return profile;
}

skill_profile_t *Skill_GetBeginnerProfile(void) {
    skill_profile_t *profile = Skill_CreateProfile(1.0f);
    profile->aim_skill = 0.6f;
    profile->movement_skill = 0.7f;
    profile->tactical_skill = 0.5f;
    profile->reaction_skill = 0.8f;
    return profile;
}

skill_profile_t *Skill_GetIntermediateProfile(void) {
    skill_profile_t *profile = Skill_CreateProfile(2.5f);
    profile->aim_skill = 2.0f;
    profile->movement_skill = 2.3f;
    profile->tactical_skill = 2.5f;
    profile->reaction_skill = 2.2f;
    return profile;
}

skill_profile_t *Skill_GetAdvancedProfile(void) {
    skill_profile_t *profile = Skill_CreateProfile(4.0f);
    profile->aim_skill = 3.8f;
    profile->movement_skill = 4.0f;
    profile->tactical_skill = 4.2f;
    profile->reaction_skill = 3.5f;
    return profile;
}

skill_profile_t *Skill_GetExpertProfile(void) {
    skill_profile_t *profile = Skill_CreateProfile(6.0f);
    profile->aim_skill = 6.5f;
    profile->movement_skill = 5.8f;
    profile->tactical_skill = 6.2f;
    profile->reaction_skill = 5.5f;
    profile->prediction_skill = 6.8f;
    return profile;
}

skill_profile_t *Skill_GetProProfile(void) {
    skill_profile_t *profile = Skill_CreateProfile(9.0f);
    profile->aim_skill = 9.5f;
    profile->movement_skill = 8.8f;
    profile->tactical_skill = 9.2f;
    profile->reaction_skill = 8.5f;
    profile->prediction_skill = 9.8f;
    profile->resource_management = 9.5f;
    profile->teamwork_skill = 8.0f;
    profile->adaptive_enabled = qfalse;  // Pro level doesn't adapt
    return profile;
}

