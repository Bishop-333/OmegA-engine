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

#include "strategic_planning.h"
#include "../game_interface.h"
#include "../game_entities.h"
#include "../ai_system.h"
#include "../ai_constants.h"
#include "../../../engine/core/qcommon.h"
#include <math.h>
#include <string.h>

// Globals are defined in game_entities.h

static struct {
    qboolean initialized;
    strategic_planner_t *planners[MAX_CLIENTS];
    int planner_count;
    cvar_t *strategy_debug;
    cvar_t *strategy_adaptability;
    cvar_t *strategy_lookahead;
} strategy_global;

/*
==================
Strategy_Init
==================
*/
void Strategy_Init(void) {
    if (strategy_global.initialized) {
        return;
    }
    
    memset(&strategy_global, 0, sizeof(strategy_global));
    
    strategy_global.strategy_debug = Cvar_Get("ai_strategy_debug", "0", 0);
    strategy_global.strategy_adaptability = Cvar_Get("ai_strategy_adaptability", "0.7", CVAR_ARCHIVE);
    strategy_global.strategy_lookahead = Cvar_Get("ai_strategy_lookahead", "10", CVAR_ARCHIVE);
    
    strategy_global.initialized = qtrue;
    
    Com_Printf("Strategic Planning System Initialized\n");
}

/*
==================
Strategy_Shutdown
==================
*/
void Strategy_Shutdown(void) {
    int i;
    
    if (!strategy_global.initialized) {
        return;
    }
    
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (strategy_global.planners[i]) {
            Strategy_DestroyPlanner(strategy_global.planners[i]);
        }
    }
    
    strategy_global.initialized = qfalse;
    Com_Printf("Strategic Planning System Shutdown\n");
}

/*
==================
Strategy_CreatePlanner
==================
*/
strategic_planner_t *Strategy_CreatePlanner(void) {
    strategic_planner_t *planner;
    int i;
    
    planner = (strategic_planner_t *)Z_Malloc(sizeof(strategic_planner_t));
    memset(planner, 0, sizeof(strategic_planner_t));
    
    // Create neural network for strategic decisions
    int layers[] = {128, 256, 128, 8}; // Input: situation, Output: strategy scores
    planner->strategy_network = NN_CreateNetwork(NN_TYPE_DECISION, layers, 4);
    
    // Initialize default weights
    planner->current_plan.weights.aggression = 0.5f;
    planner->current_plan.weights.defense = 0.5f;
    planner->current_plan.weights.objective_focus = 0.7f;
    planner->current_plan.weights.resource_control = 0.6f;
    planner->current_plan.weights.team_coordination = 0.8f;
    planner->current_plan.weights.risk_tolerance = 0.5f;
    planner->current_plan.weights.adaptability = strategy_global.strategy_adaptability->value;
    
    // Add to global list
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!strategy_global.planners[i]) {
            strategy_global.planners[i] = planner;
            strategy_global.planner_count++;
            break;
        }
    }
    
    Com_DPrintf("Created strategic planner\n");
    
    return planner;
}

/*
==================
Strategy_DestroyPlanner
==================
*/
void Strategy_DestroyPlanner(strategic_planner_t *planner) {
    int i;
    
    if (!planner) {
        return;
    }
    
    // Remove from global list
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (strategy_global.planners[i] == planner) {
            strategy_global.planners[i] = NULL;
            strategy_global.planner_count--;
            break;
        }
    }
    
    if (planner->strategy_network) {
        NN_DestroyNetwork(planner->strategy_network);
    }
    
    Z_Free(planner);
}

/*
==================
Strategy_CreatePlan
==================
*/
void Strategy_CreatePlan(strategic_planner_t *planner) {
    float current_time;
    
    if (!planner) {
        return;
    }
    
    current_time = ((int)(level.time * 0.001f * 1000)) * 0.001f;
    
    // Clear existing plan
    memset(&planner->current_plan, 0, sizeof(strategic_plan_t));
    planner->current_plan.plan_start_time = current_time;
    planner->current_plan.plan_duration = PLAN_HORIZON * 0.001f;
    
    // Assess current situation
    Strategy_AssessSituation(planner);
    
    // Select strategy based on situation
    planner->current_plan.current_strategy = Strategy_SelectStrategy(planner);
    
    // Apply strategy weights
    Strategy_ApplyStrategyWeights(planner, planner->current_plan.current_strategy);
    
    // Create high-level goals based on strategy
    switch (planner->current_plan.current_strategy) {
        case STRATEGY_AGGRESSIVE:
            Strategy_CreateGoal(planner, GOAL_TYPE_ELIMINATE);
            Strategy_CreateGoal(planner, GOAL_TYPE_DOMINATE);
            break;
            
        case STRATEGY_DEFENSIVE:
            Strategy_CreateGoal(planner, GOAL_TYPE_DEFEND);
            Strategy_CreateGoal(planner, GOAL_TYPE_SURVIVE);
            break;
            
        case STRATEGY_CONTROL:
            Strategy_CreateGoal(planner, GOAL_TYPE_CONTROL);
            Strategy_CreateGoal(planner, GOAL_TYPE_COLLECT);
            break;
            
        case STRATEGY_OBJECTIVE_FOCUSED:
            Strategy_CreateGoal(planner, GOAL_TYPE_CAPTURE);
            break;
            
        case STRATEGY_GUERRILLA:
            Strategy_CreateGoal(planner, GOAL_TYPE_ELIMINATE);
            Strategy_CreateGoal(planner, GOAL_TYPE_SURVIVE);
            break;
            
        default:
            Strategy_CreateGoal(planner, GOAL_TYPE_ELIMINATE);
            break;
    }
    
    // Prioritize goals
    Strategy_PrioritizeGoals(planner);
    
    // Decompose goals into tactical objectives
    for (int i = 0; i < planner->current_plan.num_goals; i++) {
        Strategy_DecomposeGoal(planner, &planner->current_plan.goals[i]);
    }
    
    // Calculate plan confidence
    planner->current_plan.plan_confidence = Strategy_PredictOutcome(planner);
    
    planner->last_plan_time = current_time;
    planner->plans_executed++;
    
    Com_DPrintf("Created strategic plan: strategy=%d, goals=%d, confidence=%.2f\n",
               planner->current_plan.current_strategy, 
               planner->current_plan.num_goals,
               planner->current_plan.plan_confidence);
}

/*
==================
Strategy_AssessSituation
==================
*/
void Strategy_AssessSituation(strategic_planner_t *planner) {
    situation_assessment_t *assessment;
    
    if (!planner) {
        return;
    }
    
    assessment = &planner->situation;
    memset(assessment, 0, sizeof(situation_assessment_t));
    
    // Analyze team and enemy strength
    Strategy_AnalyzeTeamStrength(planner, assessment);
    Strategy_AnalyzeEnemyStrength(planner, assessment);
    
    // Calculate advantages
    if (assessment->team_strength > 0) {
        assessment->positional_advantage = Strategy_GetMapControl(planner);
        assessment->resource_advantage = Strategy_CalculateResourceAdvantage(
            &planner->resources, NULL); // Would need enemy resources
    }
    
    // Calculate momentum
    Strategy_CalculateMomentum(planner, assessment);
    
    // Determine overall situation
    float strength_ratio = assessment->team_strength / MAX(assessment->enemy_strength, 0.1f);
    
    if (strength_ratio > 1.3f && assessment->momentum > 0.2f) {
        assessment->winning = qtrue;
    } else if (strength_ratio < 0.7f && assessment->momentum < -0.2f) {
        assessment->losing = qtrue;
    } else {
        assessment->stalemate = qtrue;
    }
    
    planner->last_assessment_time = ((int)(level.time * 0.001f * 1000)) * 0.001f;
}

/*
==================
Strategy_AnalyzeTeamStrength
==================
*/
void Strategy_AnalyzeTeamStrength(strategic_planner_t *planner, situation_assessment_t *assessment) {
    int i;
    float total_health = 0;
    float total_armor = 0;
    float weapon_power = 0;
    int alive_count = 0;
    
    if (!planner || !assessment) {
        return;
    }
    
    // Analyze friendly team (simplified - would need actual team data)
    for (i = 0; i < MAX_CLIENTS; i++) {
        gentity_t *ent = &g_entities[i];
        
        if (!ent->inuse || !ent->client) {
            continue;
        }
        
        // Check if friendly (would need team check)
        if (ent->health > 0) {
            alive_count++;
            total_health += ent->health;
            total_armor += ent->client->ps.stats[STAT_ARMOR];
            
            // Estimate weapon power
            weapon_power += ent->client->ps.weapon * 10;
        }
    }
    
    assessment->team_alive = alive_count;
    
    if (alive_count > 0) {
        assessment->average_team_health = total_health / alive_count;
        
        // Calculate overall strength
        assessment->team_strength = (total_health + total_armor * 0.5f) * 0.01f +
                                   weapon_power * 0.001f +
                                   alive_count * 10;
    }
}

/*
==================
Strategy_AnalyzeEnemyStrength
==================
*/
void Strategy_AnalyzeEnemyStrength(strategic_planner_t *planner, situation_assessment_t *assessment) {
    int i;
    float total_health = 0;
    float total_armor = 0;
    float weapon_power = 0;
    int alive_count = 0;
    
    if (!planner || !assessment) {
        return;
    }
    
    // Analyze enemy team (simplified - would need actual team data)
    for (i = 0; i < MAX_CLIENTS; i++) {
        gentity_t *ent = &g_entities[i];
        
        if (!ent->inuse || !ent->client) {
            continue;
        }
        
        // Check if enemy (would need team check)
        if (ent->health > 0) {
            alive_count++;
            
            // Use last known values if not visible
            if (planner->memory.enemy_last_seen[i] > 0) {
                total_health += ent->health;
                total_armor += ent->client->ps.stats[STAT_ARMOR];
                weapon_power += ent->client->ps.weapon * 10;
                
                // Update memory
                VectorCopy(ent->s.pos.trBase, planner->memory.enemy_positions_history[i][0]);
                planner->memory.enemy_last_seen[i] = ((int)(level.time * 0.001f * 1000)) * 0.001f;
            } else {
                // Estimate based on average
                total_health += 100;
                total_armor += 50;
                weapon_power += 50;
            }
        }
    }
    
    assessment->enemy_alive = alive_count;
    
    if (alive_count > 0) {
        assessment->average_enemy_health = total_health / alive_count;
        
        // Calculate overall strength
        assessment->enemy_strength = (total_health + total_armor * 0.5f) * 0.01f +
                                    weapon_power * 0.001f +
                                    alive_count * 10;
    }
}

/*
==================
Strategy_CalculateMomentum
==================
*/
void Strategy_CalculateMomentum(strategic_planner_t *planner, situation_assessment_t *assessment) {
    static float previous_team_strength = 0;
    static float previous_enemy_strength = 0;
    static float previous_score_diff = 0;
    
    if (!planner || !assessment) {
        return;
    }
    
    // Calculate momentum based on changes in strength and score
    float team_strength_change = assessment->team_strength - previous_team_strength;
    float enemy_strength_change = assessment->enemy_strength - previous_enemy_strength;
    float score_change = assessment->score_difference - previous_score_diff;
    
    assessment->momentum = (team_strength_change - enemy_strength_change) * 0.1f +
                          score_change * 0.05f;
    
    // Clamp momentum
    assessment->momentum = CLAMP(assessment->momentum, -1.0f, 1.0f);
    
    // Update previous values
    previous_team_strength = assessment->team_strength;
    previous_enemy_strength = assessment->enemy_strength;
    previous_score_diff = assessment->score_difference;
}

/*
==================
Strategy_SelectStrategy
==================
*/
strategy_type_t Strategy_SelectStrategy(strategic_planner_t *planner) {
    float input[128];
    float output[8];
    int best_strategy = STRATEGY_BALANCED;
    float best_score = 0;
    
    if (!planner) {
        return STRATEGY_BALANCED;
    }
    
    // Prepare neural network input
    memset(input, 0, sizeof(input));
    
    // Encode situation
    input[0] = planner->situation.team_strength / 100.0f;
    input[1] = planner->situation.enemy_strength / 100.0f;
    input[2] = planner->situation.positional_advantage;
    input[3] = planner->situation.resource_advantage;
    input[4] = planner->situation.momentum;
    input[5] = planner->situation.winning ? 1.0f : 0.0f;
    input[6] = planner->situation.losing ? 1.0f : 0.0f;
    input[7] = planner->situation.team_alive / (float)MAX_CLIENTS;
    input[8] = planner->situation.enemy_alive / (float)MAX_CLIENTS;
    input[9] = planner->situation.time_remaining / 600.0f; // Normalize to 10 minutes
    input[10] = (planner->situation.score_difference + 50) / 100.0f; // Normalize score diff
    
    // Add memory information
    for (int i = 0; i < 8; i++) {
        input[11 + i] = planner->memory.strategy_effectiveness[i];
    }
    
    // Get neural network decision
    NN_Forward(planner->strategy_network, input, output);
    
    // Select best strategy
    for (int i = 0; i < 8; i++) {
        // Apply adaptability factor
        float score = output[i];
        
        // Boost previously successful strategies
        if (planner->memory.successful_strategies[i] > planner->memory.failed_strategies[i]) {
            score *= 1.2f;
        }
        
        // Penalize repeatedly failed strategies
        if (planner->memory.failed_strategies[i] > planner->memory.successful_strategies[i] * 2) {
            score *= 0.5f;
        }
        
        if (score > best_score) {
            best_score = score;
            best_strategy = i;
        }
    }
    
    // Override based on critical situations
    if (planner->situation.losing && planner->situation.momentum < -0.5f) {
        // Desperate situation - go aggressive or guerrilla
        best_strategy = (random() > 0.5f) ? STRATEGY_AGGRESSIVE : STRATEGY_GUERRILLA;
    } else if (planner->situation.winning && planner->situation.time_remaining < 60) {
        // Winning with little time left - play defensive
        best_strategy = STRATEGY_DEFENSIVE;
    }
    
    return (strategy_type_t)best_strategy;
}

/*
==================
Strategy_CreateGoal
==================
*/
strategic_goal_t *Strategy_CreateGoal(strategic_planner_t *planner, strategic_goal_type_t type) {
    strategic_goal_t *goal;
    
    if (!planner || planner->current_plan.num_goals >= MAX_STRATEGIC_GOALS) {
        return NULL;
    }
    
    goal = &planner->current_plan.goals[planner->current_plan.num_goals];
    memset(goal, 0, sizeof(strategic_goal_t));
    
    goal->type = type;
    goal->start_time = ((int)(level.time * 0.001f * 1000)) * 0.001f;
    goal->deadline = goal->start_time + PLAN_HORIZON * 0.001f;
    
    // Set goal parameters based on type
    switch (type) {
        case GOAL_TYPE_ELIMINATE:
            goal->priority = OBJECTIVE_PRIORITY_HIGH;
            goal->value = 100;
            goal->cost = 50;
            goal->success_probability = 0.7f;
            break;
            
        case GOAL_TYPE_CAPTURE:
            goal->priority = OBJECTIVE_PRIORITY_CRITICAL;
            goal->value = 150;
            goal->cost = 70;
            goal->success_probability = 0.6f;
            break;
            
        case GOAL_TYPE_DEFEND:
            goal->priority = OBJECTIVE_PRIORITY_HIGH;
            goal->value = 80;
            goal->cost = 40;
            goal->success_probability = 0.8f;
            break;
            
        case GOAL_TYPE_CONTROL:
            goal->priority = OBJECTIVE_PRIORITY_MEDIUM;
            goal->value = 120;
            goal->cost = 60;
            goal->success_probability = 0.65f;
            break;
            
        case GOAL_TYPE_COLLECT:
            goal->priority = OBJECTIVE_PRIORITY_MEDIUM;
            goal->value = 60;
            goal->cost = 30;
            goal->success_probability = 0.85f;
            break;
            
        case GOAL_TYPE_SURVIVE:
            goal->priority = OBJECTIVE_PRIORITY_HIGH;
            goal->value = 70;
            goal->cost = 20;
            goal->success_probability = 0.75f;
            break;
            
        case GOAL_TYPE_DOMINATE:
            goal->priority = OBJECTIVE_PRIORITY_LOW;
            goal->value = 200;
            goal->cost = 100;
            goal->success_probability = 0.4f;
            break;
            
        default:
            goal->priority = OBJECTIVE_PRIORITY_MEDIUM;
            goal->value = 50;
            goal->cost = 50;
            goal->success_probability = 0.5f;
            break;
    }
    
    planner->current_plan.num_goals++;
    
    return goal;
}

/*
==================
Strategy_PrioritizeGoals
==================
*/
void Strategy_PrioritizeGoals(strategic_planner_t *planner) {
    int i, j;
    strategic_goal_t temp;
    
    if (!planner || planner->current_plan.num_goals == 0) {
        return;
    }
    
    // Calculate dynamic priority for each goal
    for (i = 0; i < planner->current_plan.num_goals; i++) {
        strategic_goal_t *goal = &planner->current_plan.goals[i];
        
        // Adjust value based on situation
        goal->value = Strategy_CalculateGoalValue(goal, &planner->situation);
        
        // Adjust cost based on current position (would need actual position)
        goal->cost = Strategy_CalculateGoalCost(goal, vec3_origin);
        
        // Update success probability
        goal->success_probability = Strategy_CalculateSuccessProbability(goal, &planner->situation);
    }
    
    // Sort goals by priority and value/cost ratio
    for (i = 0; i < planner->current_plan.num_goals - 1; i++) {
        for (j = i + 1; j < planner->current_plan.num_goals; j++) {
            strategic_goal_t *goal1 = &planner->current_plan.goals[i];
            strategic_goal_t *goal2 = &planner->current_plan.goals[j];
            
            float score1 = (goal1->value / MAX(goal1->cost, 1)) * goal1->success_probability;
            float score2 = (goal2->value / MAX(goal2->cost, 1)) * goal2->success_probability;
            
            // Weight by priority
            score1 *= (5 - goal1->priority);
            score2 *= (5 - goal2->priority);
            
            if (score2 > score1) {
                // Swap goals
                memcpy(&temp, goal1, sizeof(strategic_goal_t));
                memcpy(goal1, goal2, sizeof(strategic_goal_t));
                memcpy(goal2, &temp, sizeof(strategic_goal_t));
            }
        }
    }
}

/*
==================
Strategy_DecomposeGoal
==================
*/
void Strategy_DecomposeGoal(strategic_planner_t *planner, strategic_goal_t *goal) {
    int num_objectives = 0;
    
    if (!planner || !goal) {
        return;
    }
    
    // Create tactical objectives based on goal type
    switch (goal->type) {
        case GOAL_TYPE_ELIMINATE:
            // Create search and destroy objectives
            num_objectives = 3;
            for (int i = 0; i < num_objectives && planner->current_plan.num_objectives < MAX_TACTICAL_OBJECTIVES; i++) {
                tactical_objective_t *obj = Strategy_CreateObjective(planner, goal);
                if (obj) {
                    obj->required_agents = 2;
                    obj->time_limit = 30.0f;
                    obj->completion_reward = 50;
                    obj->failure_penalty = 10;
                }
            }
            break;
            
        case GOAL_TYPE_CAPTURE:
            // Create capture point objectives
            num_objectives = 1;
            for (int i = 0; i < num_objectives && planner->current_plan.num_objectives < MAX_TACTICAL_OBJECTIVES; i++) {
                tactical_objective_t *obj = Strategy_CreateObjective(planner, goal);
                if (obj) {
                    obj->required_agents = 3;
                    obj->time_limit = 60.0f;
                    obj->completion_reward = 100;
                    obj->failure_penalty = 50;
                }
            }
            break;
            
        case GOAL_TYPE_DEFEND:
            // Create defensive position objectives
            num_objectives = 2;
            for (int i = 0; i < num_objectives && planner->current_plan.num_objectives < MAX_TACTICAL_OBJECTIVES; i++) {
                tactical_objective_t *obj = Strategy_CreateObjective(planner, goal);
                if (obj) {
                    obj->required_agents = 2;
                    obj->time_limit = 120.0f;
                    obj->completion_reward = 30;
                    obj->failure_penalty = 40;
                }
            }
            break;
            
        case GOAL_TYPE_CONTROL:
            // Create area control objectives
            num_objectives = 4;
            for (int i = 0; i < num_objectives && planner->current_plan.num_objectives < MAX_TACTICAL_OBJECTIVES; i++) {
                tactical_objective_t *obj = Strategy_CreateObjective(planner, goal);
                if (obj) {
                    obj->required_agents = 1;
                    obj->time_limit = 90.0f;
                    obj->completion_reward = 40;
                    obj->failure_penalty = 20;
                    
                    // Assign to different map regions
                    if (i < planner->num_regions) {
                        VectorCopy(planner->regions[i].center, obj->position);
                        obj->radius = 200;
                    }
                }
            }
            break;
            
        default:
            // Generic objective
            num_objectives = 1;
            Strategy_CreateObjective(planner, goal);
            break;
    }
}

/*
==================
Strategy_CreateObjective
==================
*/
tactical_objective_t *Strategy_CreateObjective(strategic_planner_t *planner, strategic_goal_t *goal) {
    tactical_objective_t *objective;
    
    if (!planner || !goal || planner->current_plan.num_objectives >= MAX_TACTICAL_OBJECTIVES) {
        return NULL;
    }
    
    objective = &planner->current_plan.objectives[planner->current_plan.num_objectives];
    memset(objective, 0, sizeof(tactical_objective_t));
    
    objective->id = planner->current_plan.num_objectives;
    objective->parent_goal = goal;
    objective->priority = goal->priority;
    objective->active = qtrue;
    
    // Set default position (would be refined based on map analysis)
    VectorCopy(goal->target_position, objective->position);
    objective->radius = 128;
    
    planner->current_plan.num_objectives++;
    
    return objective;
}

/*
==================
Strategy_ApplyStrategyWeights
==================
*/
void Strategy_ApplyStrategyWeights(strategic_planner_t *planner, strategy_type_t strategy) {
    strategy_weights_t *weights;
    
    if (!planner) {
        return;
    }
    
    weights = &planner->current_plan.weights;
    
    // Set weights based on strategy type
    switch (strategy) {
        case STRATEGY_AGGRESSIVE:
            weights->aggression = 0.9f;
            weights->defense = 0.2f;
            weights->objective_focus = 0.5f;
            weights->resource_control = 0.4f;
            weights->team_coordination = 0.6f;
            weights->risk_tolerance = 0.8f;
            break;
            
        case STRATEGY_DEFENSIVE:
            weights->aggression = 0.2f;
            weights->defense = 0.9f;
            weights->objective_focus = 0.6f;
            weights->resource_control = 0.7f;
            weights->team_coordination = 0.8f;
            weights->risk_tolerance = 0.3f;
            break;
            
        case STRATEGY_CONTROL:
            weights->aggression = 0.4f;
            weights->defense = 0.6f;
            weights->objective_focus = 0.5f;
            weights->resource_control = 0.9f;
            weights->team_coordination = 0.7f;
            weights->risk_tolerance = 0.5f;
            break;
            
        case STRATEGY_GUERRILLA:
            weights->aggression = 0.7f;
            weights->defense = 0.3f;
            weights->objective_focus = 0.4f;
            weights->resource_control = 0.5f;
            weights->team_coordination = 0.4f;
            weights->risk_tolerance = 0.7f;
            break;
            
        case STRATEGY_OBJECTIVE_FOCUSED:
            weights->aggression = 0.5f;
            weights->defense = 0.5f;
            weights->objective_focus = 1.0f;
            weights->resource_control = 0.6f;
            weights->team_coordination = 0.9f;
            weights->risk_tolerance = 0.6f;
            break;
            
        default: // STRATEGY_BALANCED
            weights->aggression = 0.5f;
            weights->defense = 0.5f;
            weights->objective_focus = 0.7f;
            weights->resource_control = 0.6f;
            weights->team_coordination = 0.7f;
            weights->risk_tolerance = 0.5f;
            break;
    }
    
    // Keep adaptability from configuration
    weights->adaptability = strategy_global.strategy_adaptability->value;
}

/*
==================
Strategy_PredictOutcome
==================
*/
float Strategy_PredictOutcome(strategic_planner_t *planner) {
    float confidence = 0.5f;
    
    if (!planner) {
        return confidence;
    }
    
    // Base confidence on situation assessment
    if (planner->situation.winning) {
        confidence = 0.7f;
    } else if (planner->situation.losing) {
        confidence = 0.3f;
    }
    
    // Adjust based on strategy effectiveness history
    int strategy_idx = (int)planner->current_plan.current_strategy;
    if (strategy_idx >= 0 && strategy_idx < 8) {
        confidence *= planner->memory.strategy_effectiveness[strategy_idx];
    }
    
    // Adjust based on goal achievability
    float total_probability = 0;
    for (int i = 0; i < planner->current_plan.num_goals; i++) {
        total_probability += planner->current_plan.goals[i].success_probability;
    }
    
    if (planner->current_plan.num_goals > 0) {
        float avg_probability = total_probability / planner->current_plan.num_goals;
        confidence = confidence * 0.5f + avg_probability * 0.5f;
    }
    
    return CLAMP(confidence, 0, 1);
}

/*
==================
Strategy_GetMapControl
==================
*/
float Strategy_GetMapControl(strategic_planner_t *planner) {
    int controlled = 0;
    int contested = 0;
    int total = 0;
    
    if (!planner) {
        return 0.5f;
    }
    
    for (int i = 0; i < planner->num_regions; i++) {
        map_region_t *region = &planner->regions[i];
        total++;
        
        if (region->control_strength > 0.6f) {
            controlled++;
        } else if (region->is_contested) {
            contested++;
        }
    }
    
    if (total == 0) {
        return 0.5f;
    }
    
    return (controlled + contested * 0.5f) / total;
}

/*
==================
Strategy_CalculateResourceAdvantage
==================
*/
float Strategy_CalculateResourceAdvantage(const resource_state_t *friendly, const resource_state_t *enemy) {
    float friendly_value = 0;
    float enemy_value = 0;
    
    if (friendly) {
        friendly_value = friendly->health_packs * 10 +
                        friendly->armor_shards * 5 +
                        friendly->mega_health * 50 +
                        friendly->mega_armor * 40 +
                        friendly->quad_damage * 100 +
                        friendly->resource_control_percentage;
    }
    
    if (enemy) {
        enemy_value = enemy->health_packs * 10 +
                     enemy->armor_shards * 5 +
                     enemy->mega_health * 50 +
                     enemy->mega_armor * 40 +
                     enemy->quad_damage * 100 +
                     enemy->resource_control_percentage;
    }
    
    if (enemy_value < 1) {
        return 1.0f;
    }
    
    return friendly_value / (friendly_value + enemy_value);
}

/*
==================
Strategy_CalculateGoalValue
==================
*/
float Strategy_CalculateGoalValue(const strategic_goal_t *goal, const situation_assessment_t *situation) {
    float base_value = goal->value;
    
    if (!goal || !situation) {
        return base_value;
    }
    
    // Adjust value based on situation
    switch (goal->type) {
        case GOAL_TYPE_ELIMINATE:
            if (situation->losing) {
                base_value *= 1.3f; // More valuable when losing
            }
            break;
            
        case GOAL_TYPE_DEFEND:
            if (situation->winning) {
                base_value *= 1.2f; // More valuable when winning
            }
            break;
            
        case GOAL_TYPE_CAPTURE:
            if (situation->time_remaining < 120) {
                base_value *= 1.5f; // More valuable near end of match
            }
            break;
            
        case GOAL_TYPE_SURVIVE:
            if (situation->team_alive < 3) {
                base_value *= 1.4f; // More valuable with few teammates
            }
            break;
            
        default:
            break;
    }
    
    return base_value;
}

/*
==================
Strategy_CalculateGoalCost
==================
*/
float Strategy_CalculateGoalCost(const strategic_goal_t *goal, const vec3_t current_position) {
    float base_cost = goal->cost;
    
    if (!goal) {
        return base_cost;
    }
    
    // Add distance cost
    float distance = Distance(current_position, goal->target_position);
    base_cost += distance * 0.01f;
    
    // Add risk cost based on required agents
    base_cost += goal->num_assigned * 10;
    
    return base_cost;
}

/*
==================
Strategy_CalculateSuccessProbability
==================
*/
float Strategy_CalculateSuccessProbability(const strategic_goal_t *goal, const situation_assessment_t *situation) {
    float probability = goal->success_probability;
    
    if (!goal || !situation) {
        return probability;
    }
    
    // Adjust based on strength ratio
    float strength_ratio = situation->team_strength / MAX(situation->enemy_strength, 1);
    probability *= CLAMP(strength_ratio, 0.5f, 1.5f);
    
    // Adjust based on momentum
    probability += situation->momentum * 0.1f;
    
    // Adjust based on goal type and situation
    if (goal->type == GOAL_TYPE_DEFEND && situation->positional_advantage > 0.6f) {
        probability += 0.1f;
    }
    
    if (goal->type == GOAL_TYPE_ELIMINATE && situation->resource_advantage > 0.6f) {
        probability += 0.15f;
    }
    
    return CLAMP(probability, 0.1f, 0.95f);
}

/*
==================
Strategy_UpdatePlan
==================
*/
void Strategy_UpdatePlan(strategic_planner_t *planner) {
    float current_time;
    
    if (!planner || !planner->current_plan.active) {
        return;
    }
    
    current_time = level.time * 0.001f;
    
    // Update plan age
    planner->current_plan.execution_time = current_time - planner->current_plan.creation_time;
    
    // Update objectives
    for (int i = 0; i < planner->current_plan.num_objectives; i++) {
        tactical_objective_t *obj = &planner->current_plan.objectives[i];
        
        if (!obj->active || obj->completed) {
            continue;
        }
        
        // Check objective timeout
        if (current_time > obj->deadline) {
            obj->completed = qtrue;
            obj->success = qfalse;
            planner->current_plan.failed_objectives++;
            Com_DPrintf("Objective %d timed out\n", i);
            continue;
        }
        
        // Update objective progress
        switch (obj->type) {
            case OBJ_TYPE_ATTACK:
                // Check if target eliminated
                if (obj->target_entity >= 0) {
                    gentity_t *target = &g_entities[obj->target_entity];
                    if (!target->inuse || target->health <= 0) {
                        obj->completed = qtrue;
                        obj->success = qtrue;
                        obj->completion_time = current_time;
                        planner->current_plan.completed_objectives++;
                    }
                }
                break;
                
            case OBJ_TYPE_DEFEND:
                // Check if position still held
                if (obj->position[0] != 0 || obj->position[1] != 0) {
                    // Simple radius check for defense
                    qboolean position_held = qtrue;
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        gentity_t *ent = &g_entities[j];
                        if (!ent->inuse || !ent->client) continue;
                        // if (ent->client->sess.sessionTeam == planner->team_id) continue;
                        
                        vec3_t dist;
                        VectorSubtract(ent->s.pos.trBase, obj->position, dist);
                        if (VectorLength(dist) < obj->radius) {
                            position_held = qfalse;
                            break;
                        }
                    }
                    
                    if (!position_held) {
                        obj->completed = qtrue;
                        obj->success = qfalse;
                        planner->current_plan.failed_objectives++;
                    }
                }
                break;
                
            case OBJ_TYPE_CAPTURE:
                // Check if position captured
                if (obj->position[0] != 0 || obj->position[1] != 0) {
                    // Check if friendlies control the area
                    int friendly_count = 0;
                    int enemy_count = 0;
                    
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        gentity_t *ent = &g_entities[j];
                        if (!ent->inuse || !ent->client) continue;
                        
                        vec3_t dist;
                        VectorSubtract(ent->s.pos.trBase, obj->position, dist);
                        if (VectorLength(dist) < obj->radius) {
                            // if (ent->client->sess.sessionTeam == planner->team_id) {
                                friendly_count++;
                            // } else {
                            //     enemy_count++;
                            // }
                        }
                    }
                    
                    if (friendly_count > enemy_count && enemy_count == 0) {
                        obj->completed = qtrue;
                        obj->success = qtrue;
                        obj->completion_time = current_time;
                        planner->current_plan.completed_objectives++;
                    }
                }
                break;
                
            case OBJ_TYPE_SUPPORT:
                // Support objectives complete after duration
                if (current_time - obj->creation_time > 10.0f) {
                    obj->completed = qtrue;
                    obj->success = qtrue;
                    obj->completion_time = current_time;
                    planner->current_plan.completed_objectives++;
                }
                break;
                
            default:
                break;
        }
        
        // Update objective progress percentage
        if (!obj->completed) {
            float elapsed = current_time - obj->creation_time;
            float total = obj->deadline - obj->creation_time;
            obj->progress = CLAMP(elapsed / total, 0, 1);
        }
    }
    
    // Check if all objectives complete
    qboolean all_complete = qtrue;
    for (int i = 0; i < planner->current_plan.num_objectives; i++) {
        if (planner->current_plan.objectives[i].active && 
            !planner->current_plan.objectives[i].completed) {
            all_complete = qfalse;
            break;
        }
    }
    
    if (all_complete) {
        // Plan execution complete
        planner->current_plan.active = qfalse;
        planner->current_plan.execution_time = current_time - planner->current_plan.creation_time;
        
        // Calculate success rate
        float success_rate = 0;
        if (planner->current_plan.num_objectives > 0) {
            success_rate = (float)planner->current_plan.completed_objectives / 
                          planner->current_plan.num_objectives;
        }
        
        Com_DPrintf("Strategic plan completed with %.0f%% success rate\n", success_rate * 100);
        
        // Trigger replanning
        // planner->needs_replanning = qtrue;
    }
    
    // Update plan effectiveness
    if (planner->current_plan.num_objectives > 0) {
        planner->current_plan.effectiveness = 
            (float)planner->current_plan.completed_objectives / 
            (planner->current_plan.completed_objectives + planner->current_plan.failed_objectives + 1);
    }
}

/*
==================
Strategy_NeedsReplanning
==================
*/
qboolean Strategy_NeedsReplanning(strategic_planner_t *planner) {
    float current_time;
    
    if (!planner) {
        return qfalse;
    }
    
    // Always needs planning if no active plan
    if (!planner->current_plan.active) {
        return qtrue;
    }
    
    current_time = level.time * 0.001f;
    
    // Check if explicitly flagged for replanning
    // if (planner->needs_replanning) {
    //     return qtrue;
    // }
    
    // Check if plan is too old
    if (current_time - planner->current_plan.creation_time > (PLAN_MAX_AGE * 0.001f)) {
        Com_DPrintf("Plan expired due to age\n");
        return qtrue;
    }
    
    // Check if plan effectiveness is too low
    if (planner->current_plan.effectiveness < 0.3f && 
        planner->current_plan.execution_time > 5.0f) {
        Com_DPrintf("Plan ineffective, replanning needed\n");
        return qtrue;
    }
    
    // Check if too many objectives have failed
    if (planner->current_plan.failed_objectives > 
        planner->current_plan.num_objectives * 0.5f) {
        Com_DPrintf("Too many failed objectives, replanning needed\n");
        return qtrue;
    }
    
    // Check if situation has changed significantly
    // situation_assessment_t current_situation;
    Strategy_AssessSituation(planner);
    
    // Compare key metrics
    // float strength_change = fabsf(current_situation.team_strength - 
    //                               planner->last_assessment.team_strength);
    // float enemy_change = fabsf(current_situation.enemy_strength - 
    //                           planner->last_assessment.enemy_strength);
    // float position_change = fabsf(current_situation.positional_advantage - 
    //                               planner->last_assessment.positional_advantage);
    
    // Significant change threshold
    const float CHANGE_THRESHOLD = 0.3f;
    
    // if (strength_change > CHANGE_THRESHOLD || 
    //     enemy_change > CHANGE_THRESHOLD || 
    //     position_change > CHANGE_THRESHOLD) {
    //     Com_DPrintf("Significant situation change detected, replanning needed\n");
    //     return qtrue;
    // }
    
    // Check for critical events
    if (planner->situation.enemy_strength > planner->situation.team_strength * 2) {
        Com_DPrintf("Critical threat detected, replanning needed\n");
        return qtrue;
    }
    
    // Check if all current objectives are complete or failed
    qboolean has_active_objectives = qfalse;
    for (int i = 0; i < planner->current_plan.num_objectives; i++) {
        if (planner->current_plan.objectives[i].active && 
            !planner->current_plan.objectives[i].completed) {
            has_active_objectives = qtrue;
            break;
        }
    }
    
    if (!has_active_objectives) {
        Com_DPrintf("No active objectives remaining, replanning needed\n");
        return qtrue;
    }
    
    return qfalse;
}

