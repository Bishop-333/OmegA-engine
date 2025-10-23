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

#ifndef STRATEGIC_PLANNING_H
#define STRATEGIC_PLANNING_H

#include "../../../engine/common/q_shared.h"

// Weapon constants from q_shared.h
#ifndef MAX_WEAPONS
#define MAX_WEAPONS 16
#endif
#include "../neural/nn_core.h"

#define MAX_STRATEGIC_GOALS 32
#define MAX_TACTICAL_OBJECTIVES 64
#define MAX_MAP_REGIONS 128
#define MAX_RESOURCE_TYPES 16
#define STRATEGY_UPDATE_INTERVAL 1000
#define PLAN_HORIZON 30000 // 30 seconds

typedef enum {
    STRATEGY_AGGRESSIVE,
    STRATEGY_DEFENSIVE,
    STRATEGY_BALANCED,
    STRATEGY_CONTROL,
    STRATEGY_GUERRILLA,
    STRATEGY_SUPPORT,
    STRATEGY_OBJECTIVE_FOCUSED,
    STRATEGY_ELIMINATION
} strategy_type_t;

typedef enum {
    GOAL_TYPE_ELIMINATE,
    GOAL_TYPE_CAPTURE,
    GOAL_TYPE_DEFEND,
    GOAL_TYPE_CONTROL,
    GOAL_TYPE_COLLECT,
    GOAL_TYPE_ESCORT,
    GOAL_TYPE_SURVIVE,
    GOAL_TYPE_DOMINATE
} strategic_goal_type_t;

typedef enum {
    OBJECTIVE_PRIORITY_CRITICAL,
    OBJECTIVE_PRIORITY_HIGH,
    OBJECTIVE_PRIORITY_MEDIUM,
    OBJECTIVE_PRIORITY_LOW,
    OBJECTIVE_PRIORITY_OPTIONAL
} objective_priority_t;

typedef enum {
    OBJ_TYPE_ATTACK,
    OBJ_TYPE_DEFEND,
    OBJ_TYPE_CAPTURE,
    OBJ_TYPE_SUPPORT,
    OBJ_TYPE_MOVE,
    OBJ_TYPE_PATROL,
    OBJ_TYPE_SCOUT,
    OBJ_TYPE_COLLECT
} objective_type_t;

typedef enum {
    REGION_TYPE_SPAWN,
    REGION_TYPE_CHOKE_POINT,
    REGION_TYPE_POWER_POSITION,
    REGION_TYPE_RESOURCE_AREA,
    REGION_TYPE_OBJECTIVE,
    REGION_TYPE_CONTESTED,
    REGION_TYPE_SAFE,
    REGION_TYPE_DANGER
} region_type_t;

typedef struct strategic_goal_s {
    strategic_goal_type_t type;
    objective_priority_t priority;
    vec3_t target_position;
    int target_entity;
    float start_time;
    float deadline;
    float progress;
    qboolean completed;
    qboolean failed;
    int assigned_agents[MAX_CLIENTS];
    int num_assigned;
    float value;
    float cost;
    float success_probability;
} strategic_goal_t;

typedef struct tactical_objective_s {
    int id;
    strategic_goal_t *parent_goal;
    vec3_t position;
    float radius;
    objective_priority_t priority;
    float time_limit;
    int required_agents;
    int assigned_agents[MAX_CLIENTS];
    int num_assigned;
    qboolean active;
    qboolean completed;
    float completion_reward;
    float failure_penalty;
    int type;
    int target_entity;
    float deadline;
    float progress;
    qboolean success;
    float creation_time;
    float completion_time;
} tactical_objective_t;

typedef struct map_region_s {
    vec3_t center;
    vec3_t mins;
    vec3_t maxs;
    region_type_t type;
    float strategic_value;
    float control_strength;
    int controlling_team;
    int friendly_count;
    int enemy_count;
    float danger_level;
    float last_update_time;
    int connected_regions[8];
    int num_connections;
    qboolean is_contested;
} map_region_t;

typedef struct resource_state_s {
    int health_packs;
    int armor_shards;
    int mega_health;
    int mega_armor;
    int quad_damage;
    int invisibility;
    int regeneration;
    int haste;
    float powerup_respawn_times[8];
    vec3_t powerup_positions[8];
    int ammo[MAX_WEAPONS];
    float resource_control_percentage;
} resource_state_t;

typedef struct situation_assessment_s {
    float team_strength;
    float enemy_strength;
    float positional_advantage;
    float resource_advantage;
    float momentum;
    int team_alive;
    int enemy_alive;
    float average_team_health;
    float average_enemy_health;
    qboolean losing;
    qboolean winning;
    qboolean stalemate;
    float time_remaining;
    int score_difference;
} situation_assessment_t;

typedef struct strategy_weights_s {
    float aggression;
    float defense;
    float objective_focus;
    float resource_control;
    float team_coordination;
    float risk_tolerance;
    float adaptability;
} strategy_weights_t;

typedef struct strategic_plan_s {
    strategy_type_t current_strategy;
    strategic_goal_t goals[MAX_STRATEGIC_GOALS];
    int num_goals;
    tactical_objective_t objectives[MAX_TACTICAL_OBJECTIVES];
    int num_objectives;
    float plan_start_time;
    float plan_duration;
    float plan_confidence;
    qboolean needs_replanning;
    strategy_weights_t weights;
    qboolean active;
    float creation_time;
    float execution_time;
    int completed_objectives;
    int failed_objectives;
    float effectiveness;
} strategic_plan_t;

typedef struct strategic_memory_s {
    float enemy_positions_history[MAX_CLIENTS][3][10]; // Last 10 positions
    float enemy_last_seen[MAX_CLIENTS];
    int enemy_weapon_usage[MAX_CLIENTS][MAX_WEAPONS];
    float enemy_skill_estimate[MAX_CLIENTS];
    float region_visit_times[MAX_MAP_REGIONS];
    int successful_strategies[8];
    int failed_strategies[8];
    float strategy_effectiveness[8];
} strategic_memory_t;

typedef struct strategic_planner_s {
    strategic_plan_t current_plan;
    strategic_plan_t backup_plan;
    situation_assessment_t situation;
    resource_state_t resources;
    strategic_memory_t memory;
    map_region_t regions[MAX_MAP_REGIONS];
    int num_regions;
    
    // Neural network for strategic decisions
    nn_network_t *strategy_network;
    
    // Timing
    float last_plan_time;
    float last_assessment_time;
    float last_region_update;
    
    // Performance metrics
    int plans_executed;
    int plans_succeeded;
    float average_plan_duration;
    float strategic_score;
} strategic_planner_t;

// Core functions
void Strategy_Init(void);
void Strategy_Shutdown(void);
strategic_planner_t *Strategy_CreatePlanner(void);
void Strategy_DestroyPlanner(strategic_planner_t *planner);

// Planning functions
void Strategy_CreatePlan(strategic_planner_t *planner);
void Strategy_UpdatePlan(strategic_planner_t *planner);
void Strategy_ExecutePlan(strategic_planner_t *planner);
void Strategy_EvaluatePlan(strategic_planner_t *planner);
qboolean Strategy_NeedsReplanning(strategic_planner_t *planner);

// Goal management
strategic_goal_t *Strategy_CreateGoal(strategic_planner_t *planner, strategic_goal_type_t type);
void Strategy_PrioritizeGoals(strategic_planner_t *planner);
void Strategy_AssignGoal(strategic_goal_t *goal, int agent_id);
void Strategy_UpdateGoalProgress(strategic_goal_t *goal, float progress);
qboolean Strategy_IsGoalAchievable(strategic_goal_t *goal, const situation_assessment_t *situation);

// Objective management
tactical_objective_t *Strategy_CreateObjective(strategic_planner_t *planner, strategic_goal_t *goal);
void Strategy_DecomposeGoal(strategic_planner_t *planner, strategic_goal_t *goal);
void Strategy_AssignObjectives(strategic_planner_t *planner);
void Strategy_UpdateObjectives(strategic_planner_t *planner);

// Situation assessment
void Strategy_AssessSituation(strategic_planner_t *planner);
void Strategy_AnalyzeEnemyStrength(strategic_planner_t *planner, situation_assessment_t *assessment);
void Strategy_AnalyzeTeamStrength(strategic_planner_t *planner, situation_assessment_t *assessment);
void Strategy_CalculateMomentum(strategic_planner_t *planner, situation_assessment_t *assessment);
float Strategy_PredictOutcome(strategic_planner_t *planner);

// Map analysis
void Strategy_AnalyzeMap(strategic_planner_t *planner);
void Strategy_UpdateRegions(strategic_planner_t *planner);
map_region_t *Strategy_GetMostValuableRegion(strategic_planner_t *planner);
map_region_t *Strategy_GetSafestRegion(strategic_planner_t *planner);
void Strategy_CalculateRegionControl(strategic_planner_t *planner);
float Strategy_GetMapControl(strategic_planner_t *planner);

// Resource management
void Strategy_UpdateResourceState(strategic_planner_t *planner);
void Strategy_PlanResourceCollection(strategic_planner_t *planner);
float Strategy_CalculateResourceAdvantage(const resource_state_t *friendly, const resource_state_t *enemy);
vec3_t *Strategy_GetNearestResource(strategic_planner_t *planner, const vec3_t origin, int resource_type);

// Strategy selection
strategy_type_t Strategy_SelectStrategy(strategic_planner_t *planner);
void Strategy_AdaptStrategy(strategic_planner_t *planner);
void Strategy_ApplyStrategyWeights(strategic_planner_t *planner, strategy_type_t strategy);
float Strategy_EvaluateStrategy(strategic_planner_t *planner, strategy_type_t strategy);

// Memory and learning
void Strategy_UpdateMemory(strategic_planner_t *planner);
void Strategy_RecordEnemyBehavior(strategic_planner_t *planner, int enemy_id, const vec3_t position, int weapon);
float Strategy_PredictEnemyAction(strategic_planner_t *planner, int enemy_id);
void Strategy_LearnFromOutcome(strategic_planner_t *planner, qboolean success);

// Utility functions
float Strategy_CalculateGoalValue(const strategic_goal_t *goal, const situation_assessment_t *situation);
float Strategy_CalculateGoalCost(const strategic_goal_t *goal, const vec3_t current_position);
float Strategy_CalculateSuccessProbability(const strategic_goal_t *goal, const situation_assessment_t *situation);
int Strategy_GetOptimalTeamSize(strategic_goal_type_t goal_type, float enemy_strength);

// Debug and visualization
void Strategy_DebugPlan(const strategic_planner_t *planner);
void Strategy_DrawRegions(const strategic_planner_t *planner);
void Strategy_PrintGoals(const strategic_planner_t *planner);

#endif // STRATEGIC_PLANNING_H


