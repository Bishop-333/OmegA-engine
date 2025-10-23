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

#include "tactical_combat.h"
#include "../../../engine/core/qcommon.h"
#include "../game_interface.h"
#include "../game_entities.h"
#include "../ai_system.h"
#include "../../server/server.h"
#include <math.h>
#include <string.h>

// Global combat state
static struct {
    qboolean initialized;
    int total_combat_systems;
    tactical_combat_t *systems[MAX_CLIENTS];
    cvar_t *combat_debug;
    cvar_t *combat_prediction;
    cvar_t *combat_aggression;
} combat_global;

// Weapon effectiveness tables
static const float weapon_ranges[MAX_WEAPONS] = {
    0,      // WP_NONE
    50,     // WP_GAUNTLET
    800,    // WP_MACHINEGUN
    600,    // WP_SHOTGUN
    400,    // WP_GRENADE_LAUNCHER
    600,    // WP_ROCKET_LAUNCHER
    1200,   // WP_LIGHTNING
    2000,   // WP_RAILGUN
    500,    // WP_PLASMAGUN
    1000,   // WP_BFG
    0       // WP_GRAPPLING_HOOK
};

static const float weapon_dps[MAX_WEAPONS] = {
    0,      // WP_NONE
    50,     // WP_GAUNTLET
    100,    // WP_MACHINEGUN
    110,    // WP_SHOTGUN
    100,    // WP_GRENADE_LAUNCHER
    120,    // WP_ROCKET_LAUNCHER
    140,    // WP_LIGHTNING
    100,    // WP_RAILGUN
    130,    // WP_PLASMAGUN
    200,    // WP_BFG
    0       // WP_GRAPPLING_HOOK
};

/*
==================
Combat_Init
==================
*/
void Combat_Init(void) {
    if (combat_global.initialized) {
        return;
    }
    
    memset(&combat_global, 0, sizeof(combat_global));
    
    combat_global.combat_debug = Cvar_Get("ai_combat_debug", "0", 0);
    combat_global.combat_prediction = Cvar_Get("ai_combat_prediction", "1", CVAR_ARCHIVE);
    combat_global.combat_aggression = Cvar_Get("ai_combat_aggression", "0.5", CVAR_ARCHIVE);
    
    combat_global.initialized = qtrue;
    
    Com_Printf("Tactical Combat System Initialized\n");
}

/*
==================
Combat_Shutdown
==================
*/
void Combat_Shutdown(void) {
    int i;
    
    if (!combat_global.initialized) {
        return;
    }
    
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (combat_global.systems[i]) {
            Combat_Destroy(combat_global.systems[i]);
        }
    }
    
    combat_global.initialized = qfalse;
    Com_Printf("Tactical Combat System Shutdown\n");
}

/*
==================
Combat_Create
==================
*/
tactical_combat_t *Combat_Create(combat_style_t style) {
    tactical_combat_t *combat;
    int i;
    
    combat = (tactical_combat_t *)Z_Malloc(sizeof(tactical_combat_t));
    memset(combat, 0, sizeof(tactical_combat_t));
    
    combat->style = style;
    combat->current_state = COMBAT_STATE_IDLE;
    combat->engagement = Combat_GetStyleParameters(style);
    
    // Create neural network for combat decisions
    int layers[] = {64, 128, 64, 10};  // Input: threats + state, Output: actions
    combat->combat_network = NN_CreateNetwork(NN_TYPE_COMBAT, layers, 4);
    
    // Initialize performance metrics
    combat->accuracy = 0.5f;
    combat->dodge_success_rate = 0.5f;
    combat->kill_death_ratio = 1.0f;
    combat->damage_efficiency = 1.0f;
    
    // Add to global list
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!combat_global.systems[i]) {
            combat_global.systems[i] = combat;
            combat_global.total_combat_systems++;
            break;
        }
    }
    
    Com_DPrintf("Created tactical combat system with style %d\n", style);
    
    return combat;
}

/*
==================
Combat_Destroy
==================
*/
void Combat_Destroy(tactical_combat_t *combat) {
    int i;
    
    if (!combat) {
        return;
    }
    
    // Remove from global list
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (combat_global.systems[i] == combat) {
            combat_global.systems[i] = NULL;
            combat_global.total_combat_systems--;
            break;
        }
    }
    
    if (combat->combat_network) {
        NN_DestroyNetwork(combat->combat_network);
    }
    
    Z_Free(combat);
}

/*
==================
Combat_UpdateThreats
==================
*/
void Combat_UpdateThreats(tactical_combat_t *combat, const vec3_t origin) {
    int i;
    gentity_t *ent;
    threat_info_t *threat;
    vec3_t dir;
    
    if (!combat) {
        return;
    }
    
    combat->threat_count = 0;
    
    // Scan for potential threats
    for (i = 0; i < MAX_CLIENTS; i++) {
        ent = &g_entities[i];
        
        if (!ent->inuse || !ent->client) {
            continue;
        }
        
        if (ent->health <= 0) {
            continue;
        }
        
        // Check if entity is an enemy (simplified - would need team check)
        if (combat->threat_count >= MAX_THREATS) {
            break;
        }
        
        threat = &combat->memory.threats[combat->threat_count];
        threat->entity_num = i;
        VectorCopy(ent->s.pos.trBase, threat->position);
        VectorCopy(ent->s.pos.trDelta, threat->velocity);
        
        VectorSubtract(threat->position, origin, dir);
        threat->distance = VectorLength(dir);
        
        threat->health = ent->health;
        threat->armor = ent->client->ps.stats[STAT_ARMOR];
        threat->weapon = ent->client->ps.weapon;
        
        // Check visibility
        threat->visible = Combat_HasLineOfSight(origin, threat->position);
        
        if (threat->visible) {
            threat->time_visible += 0.05f;  // Assuming 20Hz update
            threat->last_seen_time = ((int)(level.time * 0.001f * 1000)) * 0.001f;
        } else {
            threat->time_visible = 0;
        }
        
        // Predict future position
        if (combat_global.combat_prediction->integer) {
            Combat_PredictTargetPosition(threat, combat->engagement.prediction_time, threat->predicted_position);
        } else {
            VectorCopy(threat->position, threat->predicted_position);
        }
        
        // Evaluate threat level
        Combat_EvaluateThreat(combat, threat);
        
        combat->threat_count++;
    }
    
    // Update combat flags
    combat->outnumbered = (combat->threat_count > 2);
    combat->under_fire = qfalse;
    
    for (i = 0; i < combat->threat_count; i++) {
        if (combat->memory.threats[i].can_hit_me && combat->memory.threats[i].visible) {
            combat->under_fire = qtrue;
            break;
        }
    }
}

/*
==================
Combat_EvaluateThreat
==================
*/
void Combat_EvaluateThreat(tactical_combat_t *combat, threat_info_t *threat) {
    float score = 0;
    
    if (!combat || !threat) {
        return;
    }
    
    // Distance factor (closer = more dangerous)
    float distance_factor = 1.0f - (threat->distance / MAX_ENGAGEMENT_RANGE);
    distance_factor = CLAMP(distance_factor, 0, 1);
    score += distance_factor * 30;
    
    // Weapon danger
    float weapon_danger = weapon_dps[threat->weapon] / 200.0f;
    score += weapon_danger * 25;
    
    // Health/armor factor (weaker enemies are easier targets)
    float health_factor = 1.0f - ((threat->health + threat->armor * 0.5f) / 200.0f);
    health_factor = CLAMP(health_factor, 0, 1);
    score += health_factor * 20;
    
    // Visibility
    if (threat->visible) {
        score += 15;
        
        // Can they hit me?
        if (threat->distance < weapon_ranges[threat->weapon]) {
            threat->can_hit_me = qtrue;
            score += 20;
        }
    }
    
    // Can I hit them?
    if (threat->distance < weapon_ranges[combat->decision.weapon_choice]) {
        threat->i_can_hit = qtrue;
        score += 10;
    }
    
    // Previous engagement history
    if (threat->damage_dealt_to_me > 0) {
        score += threat->damage_dealt_to_me / 10.0f;
    }
    
    threat->threat_score = score;
    
    // Determine threat level
    if (score < 20) {
        threat->threat_level = THREAT_LEVEL_LOW;
    } else if (score < 40) {
        threat->threat_level = THREAT_LEVEL_MEDIUM;
    } else if (score < 60) {
        threat->threat_level = THREAT_LEVEL_HIGH;
    } else {
        threat->threat_level = THREAT_LEVEL_CRITICAL;
    }
}

/*
==================
Combat_GetPrimaryThreat
==================
*/
threat_info_t *Combat_GetPrimaryThreat(tactical_combat_t *combat) {
    int i;
    threat_info_t *primary = NULL;
    float highest_score = 0;
    
    if (!combat || combat->threat_count == 0) {
        return NULL;
    }
    
    for (i = 0; i < combat->threat_count; i++) {
        if (combat->memory.threats[i].threat_score > highest_score) {
            highest_score = combat->memory.threats[i].threat_score;
            primary = &combat->memory.threats[i];
        }
    }
    
    return primary;
}

/*
==================
Combat_MakeDecision
==================
*/
void Combat_MakeDecision(tactical_combat_t *combat) {
    threat_info_t *primary_threat;
    float input[64];
    float output[10];
    int i;
    
    if (!combat) {
        return;
    }
    
    primary_threat = Combat_GetPrimaryThreat(combat);
    
    // Prepare neural network input
    memset(input, 0, sizeof(input));
    
    // Encode threat information
    if (primary_threat) {
        input[0] = primary_threat->distance / MAX_ENGAGEMENT_RANGE;
        input[1] = primary_threat->threat_score / 100.0f;
        input[2] = primary_threat->visible ? 1.0f : 0.0f;
        input[3] = primary_threat->health / 200.0f;
        input[4] = primary_threat->armor / 200.0f;
        input[5] = (float)primary_threat->weapon / MAX_WEAPONS;
    }
    
    // Encode combat state
    input[6] = (float)combat->current_state / COMBAT_STATE_EVADING;
    input[7] = combat->accuracy;
    input[8] = combat->dodge_success_rate;
    input[9] = combat->under_fire ? 1.0f : 0.0f;
    input[10] = combat->low_health ? 1.0f : 0.0f;
    input[11] = combat->low_ammo ? 1.0f : 0.0f;
    input[12] = combat->outnumbered ? 1.0f : 0.0f;
    input[13] = (float)combat->threat_count / MAX_THREATS;
    
    // Get neural network decision
    NN_Forward(combat->combat_network, input, output);
    
    // Interpret output
    combat->decision.confidence = output[0];
    combat->decision.aggression_level = output[1];
    combat->decision.should_retreat = output[2] > 0.5f;
    combat->decision.should_take_cover = output[3] > 0.5f;
    combat->decision.should_flank = output[4] > 0.5f;
    
    // Select combat state based on situation
    combat->decision.recommended_state = Combat_SelectState(combat);
    
    // Select target
    if (primary_threat) {
        combat->decision.primary_target = primary_threat->entity_num;
        
        // Calculate aim point with prediction
        Combat_CalculateAimPoint(combat, primary_threat, combat->decision.aim_position);
        
        // Select weapon
        combat->decision.weapon_choice = Combat_SelectWeapon(combat, primary_threat);
    }
    
    combat->last_decision_time = ((int)(level.time * 0.001f * 1000)) * 0.001f;
}

/*
==================
Combat_SelectState
==================
*/
combat_state_t Combat_SelectState(tactical_combat_t *combat) {
    threat_info_t *primary_threat;
    
    if (!combat) {
        return COMBAT_STATE_IDLE;
    }
    
    primary_threat = Combat_GetPrimaryThreat(combat);
    
    // No threats
    if (!primary_threat) {
        if (combat->memory.enemy_last_seen > 0 && 
            ((int)(level.time * 0.001f * 1000)) * 0.001f - combat->memory.enemy_last_seen < 5.0f) {
            return COMBAT_STATE_SEARCHING;
        }
        return COMBAT_STATE_IDLE;
    }
    
    // Critical situations
    if (combat->low_health && combat->under_fire) {
        return COMBAT_STATE_RETREATING;
    }
    
    if (combat->decision.should_retreat) {
        return COMBAT_STATE_RETREATING;
    }
    
    // Tactical decisions
    if (combat->decision.should_flank && !combat->under_fire) {
        return COMBAT_STATE_FLANKING;
    }
    
    if (primary_threat->visible) {
        // Choose engagement style based on combat style
        switch (combat->style) {
            case COMBAT_STYLE_AGGRESSIVE:
            case COMBAT_STYLE_RUSHER:
                if (primary_threat->distance > OPTIMAL_ENGAGEMENT_RANGE) {
                    return COMBAT_STATE_PURSUING;
                }
                return COMBAT_STATE_ENGAGING;
                
            case COMBAT_STYLE_DEFENSIVE:
            case COMBAT_STYLE_SNIPER:
                if (primary_threat->distance < 200) {
                    return COMBAT_STATE_EVADING;
                }
                return COMBAT_STATE_ENGAGING;
                
            case COMBAT_STYLE_GUERRILLA:
                if (combat->time_in_combat > 3.0f) {
                    return COMBAT_STATE_RETREATING;
                }
                return COMBAT_STATE_AMBUSHING;
                
            case COMBAT_STYLE_SUPPORT:
                return COMBAT_STATE_SUPPRESSING;
                
            default:
                return COMBAT_STATE_ENGAGING;
        }
    } else {
        // Enemy not visible
        if (combat->memory.enemy_last_seen > 0) {
            return COMBAT_STATE_SEARCHING;
        }
    }
    
    return COMBAT_STATE_ENGAGING;
}

/*
==================
Combat_CalculateAimPoint
==================
*/
void Combat_CalculateAimPoint(tactical_combat_t *combat, threat_info_t *target, vec3_t aim_point) {
    vec3_t velocity_component;
    float lead_time;
    float projectile_speed;
    
    if (!combat || !target) {
        return;
    }
    
    // Start with predicted position
    VectorCopy(target->predicted_position, aim_point);
    
    // Add weapon-specific lead calculation
    switch (combat->decision.weapon_choice) {
        case WP_ROCKET_LAUNCHER:
            projectile_speed = 900;
            break;
        case WP_GRENADE_LAUNCHER:
            projectile_speed = 700;
            aim_point[2] += 20;  // Arc compensation
            break;
        case WP_PLASMAGUN:
            projectile_speed = 2000;
            break;
        default:
            // Hitscan weapons don't need lead
            return;
    }
    
    // Calculate lead time
    lead_time = target->distance / projectile_speed;
    lead_time *= combat->engagement.prediction_time;
    
    // Add velocity lead
    VectorScale(target->velocity, lead_time, velocity_component);
    VectorAdd(aim_point, velocity_component, aim_point);
    
    // Apply skill-based accuracy modifier
    if (combat->accuracy < 1.0f) {
        float spread = (1.0f - combat->accuracy) * 50;
        aim_point[0] += crandom() * spread;
        aim_point[1] += crandom() * spread;
        aim_point[2] += crandom() * spread;
    }
}

/*
==================
Combat_PredictTargetPosition
==================
*/
void Combat_PredictTargetPosition(const threat_info_t *target, float time, vec3_t predicted_pos) {
    vec3_t velocity_change;
    
    if (!target) {
        return;
    }
    
    // Simple linear prediction
    VectorScale(target->velocity, time, velocity_change);
    VectorAdd(target->position, velocity_change, predicted_pos);
    
    // Account for gravity if target is in air (simplified)
    if (target->position[2] > 0 && target->velocity[2] != 0) {
        predicted_pos[2] -= 0.5f * 800 * time * time;  // Gravity
    }
}

/*
==================
Combat_SelectWeapon
==================
*/
int Combat_SelectWeapon(tactical_combat_t *combat, threat_info_t *target) {
    int best_weapon = WP_MACHINEGUN;
    float best_score = 0;
    float score;
    int weapon;
    
    if (!combat || !target) {
        return best_weapon;
    }
    
    for (weapon = WP_GAUNTLET; weapon < MAX_WEAPONS; weapon++) {
        // Skip if no ammo (simplified - would need actual ammo check)
        if (weapon == WP_GRAPPLING_HOOK) {
            continue;
        }
        
        score = 0;
        
        // Range effectiveness
        float range_eff = 1.0f - fabsf(target->distance - weapon_ranges[weapon]) / weapon_ranges[weapon];
        range_eff = CLAMP(range_eff, 0, 1);
        score += range_eff * 40;
        
        // DPS
        score += weapon_dps[weapon] / 200.0f * 30;
        
        // Situational modifiers
        if (combat->style == COMBAT_STYLE_SNIPER && weapon == WP_RAILGUN) {
            score += 20;
        }
        
        if (combat->style == COMBAT_STYLE_AGGRESSIVE && 
            (weapon == WP_ROCKET_LAUNCHER || weapon == WP_LIGHTNING)) {
            score += 15;
        }
        
        if (target->distance < 200 && weapon == WP_SHOTGUN) {
            score += 25;
        }
        
        if (score > best_score) {
            best_score = score;
            best_weapon = weapon;
        }
    }
    
    return best_weapon;
}

/*
==================
Combat_CalculateDodgeVector
==================
*/
void Combat_CalculateDodgeVector(tactical_combat_t *combat, vec3_t dodge) {
    threat_info_t *primary_threat;
    vec3_t perpendicular;
    vec3_t threat_dir;
    
    VectorClear(dodge);
    
    if (!combat) {
        return;
    }
    
    primary_threat = Combat_GetPrimaryThreat(combat);
    if (!primary_threat) {
        return;
    }
    
    // Calculate perpendicular to threat direction
    VectorSubtract(primary_threat->position, combat->decision.movement_destination, threat_dir);
    threat_dir[2] = 0;  // Keep it horizontal
    VectorNormalize(threat_dir);
    
    // Create perpendicular vector
    perpendicular[0] = -threat_dir[1];
    perpendicular[1] = threat_dir[0];
    perpendicular[2] = 0;
    
    // Random dodge direction
    if (random() > 0.5f) {
        VectorScale(perpendicular, -1, perpendicular);
    }
    
    // Scale by strafe speed
    VectorScale(perpendicular, combat->engagement.strafe_speed * 400, dodge);
    
    // Add some randomness
    dodge[0] += crandom() * 50;
    dodge[1] += crandom() * 50;
}

/*
==================
Combat_CalculateStrafePattern
==================
*/
void Combat_CalculateStrafePattern(tactical_combat_t *combat, vec3_t strafe) {
    float time;
    float pattern;
    
    VectorClear(strafe);
    
    if (!combat) {
        return;
    }
    
    time = ((int)(level.time * 0.001f * 1000)) * 0.001f;
    
    // Create a figure-8 or serpentine pattern
    switch (combat->style) {
        case COMBAT_STYLE_AGGRESSIVE:
            // Aggressive zigzag
            pattern = sin(time * 4) * 300;
            strafe[0] = pattern;
            strafe[1] = cos(time * 2) * 200;
            break;
            
        case COMBAT_STYLE_DEFENSIVE:
            // Wide arcs
            pattern = sin(time * 2) * 400;
            strafe[0] = pattern;
            strafe[1] = cos(time * 2) * 400;
            break;
            
        case COMBAT_STYLE_GUERRILLA:
            // Erratic movement
            pattern = sin(time * 6) * 250;
            strafe[0] = pattern + crandom() * 100;
            strafe[1] = cos(time * 3) * 250 + crandom() * 100;
            break;
            
        default:
            // Standard strafe
            pattern = sin(time * 3) * 300;
            strafe[0] = pattern;
            break;
    }
}

/*
==================
Combat_GetStyleParameters
==================
*/
engagement_params_t Combat_GetStyleParameters(combat_style_t style) {
    engagement_params_t params;
    
    // Default parameters
    params.optimal_range = OPTIMAL_ENGAGEMENT_RANGE;
    params.min_range = 100;
    params.max_range = 1000;
    params.aim_accuracy = 0.7f;
    params.prediction_time = 0.5f;
    params.burst_duration = 1.0f;
    params.suppression_time = 2.0f;
    params.use_splash_damage = qfalse;
    params.prefer_direct_hit = qtrue;
    params.strafe_speed = 1.0f;
    params.dodge_probability = 0.5f;
    
    // Style-specific modifications
    switch (style) {
        case COMBAT_STYLE_AGGRESSIVE:
            params.optimal_range = 300;
            params.min_range = 50;
            params.aim_accuracy = 0.6f;
            params.burst_duration = 2.0f;
            params.strafe_speed = 1.2f;
            params.dodge_probability = 0.3f;
            break;
            
        case COMBAT_STYLE_DEFENSIVE:
            params.optimal_range = 700;
            params.min_range = 300;
            params.aim_accuracy = 0.8f;
            params.prediction_time = 0.3f;
            params.strafe_speed = 0.8f;
            params.dodge_probability = 0.7f;
            break;
            
        case COMBAT_STYLE_SNIPER:
            params.optimal_range = 1500;
            params.min_range = 500;
            params.max_range = 2000;
            params.aim_accuracy = 0.95f;
            params.prediction_time = 1.0f;
            params.burst_duration = 0.5f;
            params.strafe_speed = 0.5f;
            break;
            
        case COMBAT_STYLE_RUSHER:
            params.optimal_range = 150;
            params.min_range = 0;
            params.max_range = 400;
            params.aim_accuracy = 0.5f;
            params.strafe_speed = 1.5f;
            params.dodge_probability = 0.2f;
            break;
            
        case COMBAT_STYLE_SUPPORT:
            params.optimal_range = 600;
            params.use_splash_damage = qtrue;
            params.prefer_direct_hit = qfalse;
            params.suppression_time = 4.0f;
            params.aim_accuracy = 0.6f;
            break;
            
        case COMBAT_STYLE_GUERRILLA:
            params.optimal_range = 400;
            params.burst_duration = 0.7f;
            params.strafe_speed = 1.3f;
            params.dodge_probability = 0.8f;
            params.prediction_time = 0.2f;
            break;
            
        case COMBAT_STYLE_TACTICAL:
            params.optimal_range = 500;
            params.aim_accuracy = 0.75f;
            params.prediction_time = 0.7f;
            params.use_splash_damage = qtrue;
            params.strafe_speed = 1.0f;
            params.dodge_probability = 0.6f;
            break;
            
        default:
            break;
    }
    
    return params;
}

/*
==================
Combat_HasLineOfSight
==================
*/
qboolean Combat_HasLineOfSight(const vec3_t from, const vec3_t to) {
    trace_t trace;
    
    // Simplified trace - in real implementation would use proper trace
    trap_Trace(&trace, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SHOT);
    
    return trace.fraction >= 1.0f;
}

/*
==================
Combat_CalculateFlankingRoute
==================
*/
void Combat_CalculateFlankingRoute(tactical_combat_t *combat, threat_info_t *target, vec3_t flank) {
    vec3_t side_vector;
    vec3_t target_dir;
    float angle;
    
    if (!combat || !target) {
        VectorClear(flank);
        return;
    }
    
    // Get direction to target
    VectorSubtract(target->position, combat->decision.movement_destination, target_dir);
    target_dir[2] = 0;
    VectorNormalize(target_dir);
    
    // Choose flanking side based on environment (simplified)
    angle = (random() > 0.5f) ? 90 : -90;
    angle = DEG2RAD(angle);
    
    // Rotate vector for flanking position
    side_vector[0] = target_dir[0] * cos(angle) - target_dir[1] * sin(angle);
    side_vector[1] = target_dir[0] * sin(angle) + target_dir[1] * cos(angle);
    side_vector[2] = 0;
    
    // Set flanking destination
    VectorMA(target->position, 300, side_vector, flank);
}

/*
==================
Combat_ShouldJump
==================
*/
qboolean Combat_ShouldJump(tactical_combat_t *combat) {
    if (!combat) {
        return qfalse;
    }
    
    // Jump to dodge rockets/grenades
    if (combat->under_fire && combat->engagement.dodge_probability > random()) {
        threat_info_t *threat = Combat_GetPrimaryThreat(combat);
        if (threat && (threat->weapon == WP_ROCKET_LAUNCHER || 
                      threat->weapon == WP_GRENADE_LAUNCHER)) {
            return qtrue;
        }
    }
    
    // Jump during aggressive rushes
    if (combat->current_state == COMBAT_STATE_PURSUING && 
        combat->style == COMBAT_STYLE_RUSHER) {
        return (random() < 0.3f);
    }
    
    return qfalse;
}

/*
==================
Combat_ShouldCrouch
==================
*/
qboolean Combat_ShouldCrouch(tactical_combat_t *combat) {
    if (!combat) {
        return qfalse;
    }
    
    // Crouch for accuracy when sniping
    if (combat->style == COMBAT_STYLE_SNIPER && 
        combat->current_state == COMBAT_STATE_ENGAGING) {
        return qtrue;
    }
    
    // Crouch behind cover
    if (combat->decision.should_take_cover) {
        return qtrue;
    }
    
    // Don't crouch when rushing or evading
    if (combat->current_state == COMBAT_STATE_PURSUING ||
        combat->current_state == COMBAT_STATE_EVADING) {
        return qfalse;
    }
    
    return qfalse;
}

