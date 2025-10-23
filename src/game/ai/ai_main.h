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

#ifndef AI_MAIN_H
#define AI_MAIN_H

#include "../../engine/common/q_shared.h"
#include "../../engine/ai/ai_public.h"
#include "game_interface.h"
#include "neural/nn_core.h"
#include "learning/rl_ppo.h"
#include "learning/skill_adaptation.h"
#include "tactical/tactical_combat.h"
#include "tactical/cover_system.h"
#include "tactical/movement_tactics.h"
#include "strategic/strategic_planning.h"
#include "team/team_coordination.h"
#include "perception/ai_perception.h"

#define AI_THINK_TIME 50 // milliseconds
#define MAX_ITEMS 256
#define MAX_AI_CLIENTS MAX_CLIENTS
#define AI_VERSION "2.0"

typedef enum {
    BOT_STATE_SPAWNING,
    BOT_STATE_IDLE,
    BOT_STATE_COMBAT,
    BOT_STATE_MOVING,
    BOT_STATE_SEARCHING,
    BOT_STATE_RETREATING,
    BOT_STATE_OBJECTIVE,
    BOT_STATE_DEAD
} bot_state_t;

typedef enum {
    BOT_PERSONALITY_AGGRESSIVE,
    BOT_PERSONALITY_DEFENSIVE,
    BOT_PERSONALITY_TACTICAL,
    BOT_PERSONALITY_SUPPORT,
    BOT_PERSONALITY_SCOUT,
    BOT_PERSONALITY_RANDOM
} bot_personality_t;

typedef struct bot_memory_s {
    // Enemy tracking
    int last_enemy;
    vec3_t last_enemy_position;
    float last_enemy_time;
    int enemy_deaths[MAX_CLIENTS];
    int deaths_by_enemy[MAX_CLIENTS];
    
    // Item memory
    vec3_t item_positions[MAX_ITEMS];
    float item_respawn_times[MAX_ITEMS];
    int item_types[MAX_ITEMS];
    int num_items;
    
    // Navigation memory
    vec3_t visited_positions[100];
    int num_visited;
    vec3_t stuck_position;
    float stuck_time;
    
    // Combat memory
    vec3_t last_damage_origin;
    int last_attacker;
    float last_damage_time;
    int preferred_weapon;
    
    // Objective memory
    vec3_t objective_position;
    int objective_type;
    float objective_time;
} bot_memory_t;

typedef struct bot_goals_s {
    // Current goals
    vec3_t movement_goal;
    int combat_target;
    int item_goal;
    tactical_objective_t *team_objective;
    
    // Goal weights
    float combat_priority;
    float item_priority;
    float objective_priority;
    float survival_priority;
} bot_goals_t;

// AI Mode (simplified - only advanced neural AI)
typedef enum {
    AI_MODE_ADVANCED = 0   // Use new neural network AI
} ai_mode_t;


// Goal types
typedef enum {
    GOAL_NONE,
    GOAL_ITEM,
    GOAL_ENEMY,
    GOAL_POSITION,
    GOAL_BUTTON,
    GOAL_OBJECTIVE
} goal_type_t;

// Current goal structure
typedef struct bot_goal_s {
    goal_type_t type;
    vec3_t position;
    int entity_num;
    float priority;
} bot_goal_t;

// Portal state
typedef struct bot_portal_state_s {
    qboolean wants_orange_portal;
    qboolean wants_blue_portal;
    vec3_t orange_portal_pos;
    vec3_t blue_portal_pos;
    qboolean has_orange;
    qboolean has_blue;
} bot_portal_state_t;

// Personality traits
typedef struct bot_personality_traits_s {
    float aggression;
    float caution;
    float taunt_frequency;
    float teamwork;
    float skill_level;
} bot_personality_traits_t;

// Inventory tracking
typedef struct weapon_info_s {
    qboolean has_weapon;
    int ammo;
} weapon_info_t;

typedef struct bot_inventory_s {
    weapon_info_t weapons[MAX_WEAPONS];
    int current_weapon;
    qboolean powerups[8];  // Various powerups
    int armor;
    int health;
} bot_inventory_t;

// Team state
typedef struct bot_team_state_s {
    int team;
    int role;  // attacker, defender, support
    int squad_id;
    int squad_leader;
} bot_team_state_t;

typedef struct bot_state_info_s {
    vec3_t position;
    vec3_t velocity;
    vec3_t view_angles;
    vec3_t forward;
    vec3_t right;
    vec3_t up;
    float health;
    float armor;
    int weapon;
    qboolean on_ground;
    qboolean in_water;
    qboolean in_lava;
    qboolean ducking;
} bot_state_info_t;
typedef struct bot_controller_s {
    // Identity
    int client_num;
    bot_state_info_t current_state;
    char name[MAX_NAME_LENGTH];
    int team;
    bot_personality_t personality;
    
    // Character configuration
    int character_handle;       // Handle to loaded character configuration
    
    // State
    bot_state_t state;
    bot_state_t previous_state;
    float state_time;
    
    // Core systems
    perception_system_t *perception;
    tactical_combat_t *combat;
    tactical_movement_t *movement;
    cover_state_t cover_state;
    skill_profile_t *skill_profile;
    adaptation_state_t adaptation;
    
    // Learning
    ppo_agent_t *learning_agent;
    qboolean learning_enabled;
    
    // Memory
    bot_memory_t memory;
    bot_goals_t goals;
    
    // Current goal
    bot_goal_t current_goal;
    
    // Portal state
    bot_portal_state_t portal_state;
    
    // Personality traits
    bot_personality_traits_t personality_traits;
    
    // Inventory
    bot_inventory_t inventory;
    
    // Team state
    bot_team_state_t team_state;
    
    // Input/Output
    bot_input_t input; // Using bot_input_t from ai_public.h
    usercmd_t cmd;
    
    // Performance
    float think_time;
    float reaction_time;
    float next_think_time;
    int think_cycles;
    
    // Debugging
    qboolean debug_enabled;
    char debug_string[256];
} bot_controller_t;

typedef struct ai_manager_s {
    qboolean initialized;
    bot_controller_t *bots[MAX_AI_CLIENTS];
    int num_bots;
    
    
    // Shared systems
    cover_manager_t *cover_manager;
    team_coordinator_t *team_coordinators[4];
    
    // Global AI settings
    cvar_t *ai_enabled;
    cvar_t *ai_debug;
    cvar_t *ai_skill;
    cvar_t *ai_learning;
    cvar_t *ai_teamplay;
    cvar_t *ai_think_time;
    
    // Performance monitoring
    float total_think_time;
    int total_thinks;
    float average_think_time;
    
    // Training mode
    qboolean training_mode;
    char training_data_path[MAX_QPATH];
} ai_manager_t;

// Global AI manager
extern ai_manager_t ai_manager;

// Core AI functions
void AI_Init(void);
void AI_Shutdown(void);
void AI_Frame(int levelTime);
void AI_MapLoaded(void);  // Called when map is loaded and world is ready

// Bot management
bot_controller_t *AI_CreateBot(int clientNum, const char *name, bot_personality_t personality);
void AI_DestroyBot(bot_controller_t *bot);
void AI_SpawnBot(bot_controller_t *bot);
void AI_RemoveBot(int clientNum);
bot_controller_t *AI_GetBot(int clientNum);

// Bot thinking
void AI_BotThink(bot_controller_t *bot, int levelTime);
void AI_UpdateBotInput(bot_controller_t *bot);
void AI_ExecuteBotActions(bot_controller_t *bot);

// Decision making
void AI_MakeDecisions(bot_controller_t *bot);
void AI_UpdateGoals(bot_controller_t *bot);
void AI_SelectState(bot_controller_t *bot);
void AI_PrioritizeGoals(bot_controller_t *bot);

// Perception
void AI_UpdatePerception(bot_controller_t *bot);
void AI_ProcessSensoryInput(bot_controller_t *bot);
void AI_UpdateMemory(bot_controller_t *bot);

// Combat
void AI_CombatThink(bot_controller_t *bot);
void AI_SelectTarget(bot_controller_t *bot);
void AI_AimAtTarget(bot_controller_t *bot);
void AI_FireWeapon(bot_controller_t *bot);

// Movement
void AI_MovementThink(bot_controller_t *bot);
void AI_NavigateToGoal(bot_controller_t *bot);
void AI_AvoidObstacles(bot_controller_t *bot);
void AI_HandleStuck(bot_controller_t *bot);

// Team coordination
void AI_TeamThink(bot_controller_t *bot);
void AI_CoordinateWithTeam(bot_controller_t *bot);
void AI_ExecuteTeamRole(bot_controller_t *bot);

// Learning and adaptation
void AI_UpdateLearning(bot_controller_t *bot);
void AI_AdaptToPlayer(bot_controller_t *bot);
void AI_RecordExperience(bot_controller_t *bot);
void AI_TrainNetworks(void);

// Personality
void AI_SetPersonality(bot_controller_t *bot, bot_personality_t personality);
void AI_ApplyPersonalityTraits(bot_controller_t *bot);
float AI_GetPersonalityWeight(bot_controller_t *bot, const char *trait);
void AI_ApplyCharacterTraits(bot_controller_t *bot);

// Utility functions
float AI_GetBotSkill(bot_controller_t *bot);
qboolean AI_IsTeammate(bot_controller_t *bot, int clientNum);
qboolean AI_CanSeeEntity(bot_controller_t *bot, int entityNum);
float AI_GetDistanceToEntity(bot_controller_t *bot, int entityNum);
vec3_t *AI_GetEntityPosition(int entityNum, vec3_t pos);

// Debug functions
void AI_DebugDraw(bot_controller_t *bot);
void AI_PrintBotState(bot_controller_t *bot);
void AI_LogPerformance(void);
void AI_SaveTrainingData(void);

// Commands
void AI_Command_AddBot(const char *name, int skill, int team);
void AI_Command_RemoveBot(const char *name);
void AI_Command_RemoveAllBots(void);
void AI_Command_BotDebug(int mode);

#endif // AI_MAIN_H

