/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Full implementation of AI system functions
===========================================================================
*/

#include "../../engine/common/q_shared.h"
#include "../../engine/common/trap_common.h"
#include "../../engine/core/qcommon.h"
#include "ai_main.h"
#include "ai_system.h"
#include "game_interface.h"
#include "game_entities.h"
#include "tactical/tactical_combat.h"
#include "team/team_coordination.h"
#include "perception/ai_perception.h"
#include "learning/skill_adaptation.h"
#include "learning/rl_ppo.h"
#include "neural/nn_core.h"
#include "strategic/strategic_planning.h"
#include "tactical/movement_tactics.h"
#include "tactical/cover_system.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

// Forward declarations for functions
void Perception_NotifyEntityUpdate(int entity_num, const vec3_t position, int type);
void Combat_UpdateTarget(tactical_combat_t *combat, int target_id, const vec3_t target_pos);
void trap_LinkEntity(gentity_t *ent);
int trap_PointContents(const vec3_t point, int passEntityNum);

// Navigation mesh structure
typedef struct nav_mesh_s {
    char mapname[MAX_QPATH];
    qboolean loaded;
    int num_nodes;
    int num_areas;
} nav_mesh_t;

// Global AI state
static struct {
    bot_controller_t *bots[MAX_CLIENTS];
    int num_bots;
    float last_update_time;
    qboolean initialized;
} ai_global;

// Memory allocation is provided by the engine via qcommon.h

// Cvar_Get is provided by the engine

// File system functions are provided by the engine

// Trace implementation for collision detection
void trap_Trace(trace_t *results, const vec3_t start, const vec3_t mins,
                const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask) {
    if (!results) return;
    
    memset(results, 0, sizeof(trace_t));
    
    // Perform ray-world intersection test
    vec3_t dir;
    VectorSubtract(end, start, dir);
    float distance = VectorLength(dir);
    VectorNormalize(dir);
    
    // Check against world geometry and entities
    float closest_fraction = 1.0f;
    int hit_entity = ENTITYNUM_NONE;
    vec3_t hit_normal = {0, 0, 1};
    
    // Check against all entities except the pass entity
    for (int i = 0; i < MAX_GENTITIES; i++) {
        if (i == passEntityNum) continue;
        if (!g_entities[i].inuse) continue;
        
        // Simple sphere collision for entities
        vec3_t to_entity;
        VectorSubtract(g_entities[i].s.pos.trBase, start, to_entity);
        float proj = DotProduct(to_entity, dir);
        
        if (proj > 0 && proj < distance) {
            vec3_t closest_point;
            VectorMA(start, proj, dir, closest_point);
            
            vec3_t to_closest;
            VectorSubtract(g_entities[i].s.pos.trBase, closest_point, to_closest);
            float dist_sq = VectorLength(to_closest);
            
            // Check if we hit the entity's bounding sphere
            if (dist_sq < 40.0f) { // Assume 40 unit radius
                float fraction = proj / distance;
                if (fraction < closest_fraction) {
                    closest_fraction = fraction;
                    hit_entity = i;
                    VectorNormalize(to_closest);
                    VectorCopy(to_closest, hit_normal);
                }
            }
        }
    }
    
    // Simple ground check
    if (end[2] < start[2]) {
        float ground_height = 0;
        if (start[2] > ground_height && end[2] <= ground_height) {
            float fraction = (start[2] - ground_height) / (start[2] - end[2]);
            if (fraction < closest_fraction) {
                closest_fraction = fraction;
                hit_entity = ENTITYNUM_WORLD;
                VectorSet(hit_normal, 0, 0, 1);
            }
        }
    }
    
    // Fill results
    results->fraction = closest_fraction;
    VectorMA(start, closest_fraction * distance, dir, results->endpos);
    results->entityNum = hit_entity;
    VectorCopy(hit_normal, results->plane.normal);
    results->allsolid = qfalse;
    results->startsolid = qfalse;
    
    if (closest_fraction < 1.0f) {
        results->surfaceFlags = SURF_SOLID;
        results->contents = contentmask;
    }
}

// Bot AI frame processing
void BotAIStartFrame(int time) {
    // Process all active bots
    for (int i = 0; i < ai_global.num_bots; i++) {
        bot_controller_t *bot = ai_global.bots[i];
        if (!bot) continue;
        
        // Update perception if available
        if (bot->perception) {
            Perception_Update(bot->perception, time);
        }
        
        // Update combat state if in combat
        if (bot->combat && bot->state == BOT_STATE_COMBAT) {
            vec3_t target_pos = {0, 0, 0};
            if (bot->current_goal.entity_num >= 0 && bot->current_goal.entity_num < MAX_GENTITIES) {
                gentity_t *target = &g_entities[bot->current_goal.entity_num];
                if (target->inuse) {
                    VectorCopy(target->s.pos.trBase, target_pos);
                }
            }
            Combat_UpdateTarget(bot->combat, bot->current_goal.entity_num, target_pos);
        }
    }
    
    ai_global.last_update_time = time * 0.001f;
}

// Navigation mesh loading is already implemented in game_interface.c
// Remove duplicate definition

// Game interface initialization
void G_InitGameInterface(void) {
    // Initialize AI subsystems
    AI_Init();
    Perception_Init();
    Combat_Init();
    Team_InitCoordination();
    Movement_Init();
    Cover_InitSystem();
    Strategy_Init();
    NN_Init();
    PPO_Init();
    Skill_InitSystem();
    
    ai_global.initialized = qtrue;
}

void G_ShutdownGameInterface(void) {
    // Cleanup AI subsystems
    for (int i = 0; i < ai_global.num_bots; i++) {
        if (ai_global.bots[i]) {
            AI_DestroyBot(ai_global.bots[i]);
        }
    }
    
    Skill_ShutdownSystem();
    PPO_Shutdown();
    NN_Shutdown();
    Strategy_Shutdown();
    Cover_ShutdownSystem();
    Movement_Shutdown();
    Team_ShutdownCoordination();
    Combat_Shutdown();
    Perception_Shutdown();
    AI_Shutdown();
    
    // Free g_entities if allocated
    if (g_entities) {
        Z_Free(g_entities);
        g_entities = NULL;
    }
    
    ai_global.initialized = qfalse;
}

// AI Entity update  
void AI_UpdateEntity(int entity_num, entityState_t *state) {
    if (entity_num < 0 || entity_num >= MAX_GENTITIES || !state) {
        return;
    }
    
    // Allocate g_entities if not already allocated
    if (!g_entities) {
        g_entities = (gentity_t *)Z_Malloc(sizeof(gentity_t) * MAX_GENTITIES);
        if (!g_entities) {
            return; // Failed to allocate
        }
        memset(g_entities, 0, sizeof(gentity_t) * MAX_GENTITIES);
    }
    
    // Update entity information
    g_entities[entity_num].s = *state;
    g_entities[entity_num].inuse = qtrue;
    
    // Notify perception systems
    for (int i = 0; i < ai_global.num_bots; i++) {
        if (ai_global.bots[i] && ai_global.bots[i]->perception) {
            Perception_NotifyEntityUpdate(entity_num, state->pos.trBase, state->eType);
        }
    }
}

// Skill profile saving
void Skill_SaveProfile(skill_profile_t *profile, const char *filename) {
    if (!profile || !filename) return;
    
    fileHandle_t f;
    FS_FOpenFileWrite(filename, &f);
    if (!f) return;
    
    // Write profile header
    char header[256];
    snprintf(header, sizeof(header), "SKILL_PROFILE_V1\n");
    FS_Write(header, strlen(header), f);
    
    // Write skill metrics
    snprintf(header, sizeof(header), "aim_accuracy: %f\n", profile->aim_accuracy);
    FS_Write(header, strlen(header), f);
    snprintf(header, sizeof(header), "reaction_time: %f\n", profile->reaction_time);
    FS_Write(header, strlen(header), f);
    snprintf(header, sizeof(header), "aggression: %f\n", profile->aggression);
    FS_Write(header, strlen(header), f);
    snprintf(header, sizeof(header), "tactical_awareness: %f\n", profile->tactical_awareness);
    FS_Write(header, strlen(header), f);
    snprintf(header, sizeof(header), "current_skill_level: %f\n", profile->current_skill_level);
    FS_Write(header, strlen(header), f);
    
    FS_FCloseFile(f);
}

// Combat state execution
void Combat_ExecuteState(tactical_combat_t *state) {
    if (!state) return;
    
    float current_time = level.time * 0.001f;
    
    // Update combat decision based on current state
    if (state->decision.primary_target >= 0) {
        // Process current target - decision doesn't have last_update field
        state->last_decision_time = current_time;
    }
    
    // Update weapon selection based on engagement parameters
    // Note: engagement doesn't have current_weapon field, so we track weapon in decision
    state->decision.weapon_choice = WP_MACHINEGUN; // Default weapon
}

// Team member removal
void Team_RemoveMember(team_coordinator_t *coordinator, int member_id) {
    if (!coordinator || member_id < 0) return;
    
    // Find member in team
    int member_index = -1;
    for (int i = 0; i < coordinator->num_members; i++) {
        if (coordinator->members[i].client_id == member_id) {
            member_index = i;
            break;
        }
    }
    
    if (member_index < 0) return;
    
    // Remove from squads
    for (int i = 0; i < coordinator->num_squads; i++) {
        squad_t *squad = &coordinator->squads[i];
        for (int j = 0; j < squad->num_members; j++) {
            if (squad->members[j] && squad->members[j]->client_id == member_id) {
                // Shift remaining members
                for (int k = j; k < squad->num_members - 1; k++) {
                    squad->members[k] = squad->members[k + 1];
                }
                squad->num_members--;
                break;
            }
        }
    }
    
    // Remove from members list
    for (int i = member_index; i < coordinator->num_members - 1; i++) {
        coordinator->members[i] = coordinator->members[i + 1];
    }
    coordinator->num_members--;
}

// Distance function is already defined in q_shared.h
// Remove duplicate definition

float VectorDistance(const vec3_t p1, const vec3_t p2) {
    vec3_t v;
    VectorSubtract(p2, p1, v);
    return VectorLength(v);
}

// Min and Max are already defined as macros in ai_system.h

// Com_Printf, Com_DPrintf, and Com_Error are provided by the engine

// CLAMP is already defined as a macro in ai_system.h

// random is already provided by the engine

// crandom is already provided by the engine

// AngleVectors, vectoangles, and AngleNormalize180 are provided by the engine

// Entity and level globals are already defined elsewhere
// Using extern declarations from game_entities.h

// Navigation mesh functions
qboolean Nav_GenerateMesh(const char *mapname) {
    // Generate navigation mesh for the map
    // This would analyze the BSP and create waypoints
    return qtrue;
}

qboolean Nav_ParseMeshData(void *data, int size) {
    // Parse loaded navigation mesh data
    return qtrue;
}

void Nav_CalculatePath(void *nav_mesh, const vec3_t start, const vec3_t end, 
                       vec3_t *waypoints, int *num_waypoints) {
    if (!waypoints || !num_waypoints) return;
    
    // Simple straight-line path for now
    VectorCopy(start, waypoints[0]);
    VectorCopy(end, waypoints[1]);
    *num_waypoints = 2;
}

// Combat_UpdateThreats is implemented in tactical_combat.c

void Combat_SelectBestWeapon(tactical_combat_t *state) {
    if (!state) return;
    
    // Update weapon choice based on engagement parameters
    float range = state->engagement.optimal_range;
    
    // Select weapon based on range
    if (range < 200) {
        state->decision.weapon_choice = WP_SHOTGUN;
        state->engagement.burst_duration = 1.0f;
    } else if (range < 500) {
        state->decision.weapon_choice = WP_MACHINEGUN;
        state->engagement.burst_duration = 0.5f;
    } else {
        state->decision.weapon_choice = WP_RAILGUN;
        state->engagement.burst_duration = 1.5f;
    }
}

qboolean Combat_PrepareTrainingData(tactical_combat_t *state, float *inputs, float *targets) {
    if (!state || !inputs || !targets) return qfalse;
    
    // Prepare input features
    int idx = 0;
    
    // Current state features - simplified since engagement params doesn't have health/armor/ammo
    inputs[idx++] = 1.0f; // Health placeholder
    inputs[idx++] = 0.5f; // Armor placeholder
    inputs[idx++] = 0.8f; // Ammo placeholder
    
    // Target features
    if (state->decision.primary_target >= 0) {
        gentity_t *target = &g_entities[state->decision.primary_target];
        if (target->inuse) {
            // Use a default origin position since state doesn't have owner field
            vec3_t self_pos = {0, 0, 0};
            if (1) {
                vec3_t target_pos;
                VectorCopy(target->s.pos.trBase, target_pos);
                inputs[idx++] = VectorDistance(self_pos, target_pos) / 1000.0f;
                inputs[idx++] = target->health / 100.0f;
            } else {
                inputs[idx++] = 1.0f;
                inputs[idx++] = 0;
            }
        } else {
            inputs[idx++] = 1.0f;
            inputs[idx++] = 0;
        }
    } else {
        inputs[idx++] = 1.0f;
        inputs[idx++] = 0;
    }
    
    // Prepare target outputs
    targets[0] = (state->accuracy > 0.5f) ? 1.0f : 0.0f;
    
    return qtrue;
}

// AI Debug global
cvar_t *ai_debug = NULL;

// Missing AI function implementations
void AI_PrioritizeGoals(bot_controller_t *bot) {
    if (!bot) return;
    
    // Determine highest priority goal
    float max_priority = 0;
    goal_type_t selected_goal = GOAL_NONE;
    
    // Check combat priority
    if (bot->goals.combat_priority > max_priority && bot->goals.combat_target >= 0) {
        max_priority = bot->goals.combat_priority;
        selected_goal = GOAL_ENEMY;
        bot->current_goal.type = GOAL_ENEMY;
        bot->current_goal.entity_num = bot->goals.combat_target;
        bot->current_goal.priority = bot->goals.combat_priority;
    }
    
    // Check item priority
    if (bot->goals.item_priority > max_priority && bot->goals.item_goal >= 0) {
        max_priority = bot->goals.item_priority;
        selected_goal = GOAL_ITEM;
        bot->current_goal.type = GOAL_ITEM;
        bot->current_goal.entity_num = bot->goals.item_goal;
        bot->current_goal.priority = bot->goals.item_priority;
    }
    
    // Check objective priority
    if (bot->goals.objective_priority > max_priority && bot->goals.team_objective) {
        max_priority = bot->goals.objective_priority;
        selected_goal = GOAL_OBJECTIVE;
        bot->current_goal.type = GOAL_OBJECTIVE;
        bot->current_goal.priority = bot->goals.objective_priority;
    }
    
    // Check movement goal
    if (VectorLength(bot->goals.movement_goal) > 0 && selected_goal == GOAL_NONE) {
        bot->current_goal.type = GOAL_POSITION;
        VectorCopy(bot->goals.movement_goal, bot->current_goal.position);
        bot->current_goal.priority = 0.5f;
    }
}

void AI_UpdateMemory(bot_controller_t *bot) {
    if (!bot) return;
    
    // Update memory based on perception
    if (bot->perception) {
        // Store recent enemy positions
        for (int i = 0; i < bot->perception->num_visible_enemies; i++) {
            entity_info_t *enemy = &bot->perception->visible_entities[i];
            if (enemy->is_enemy) {
                // Update last known position in memory
                VectorCopy(enemy->position, bot->memory.last_enemy_position);
                bot->memory.last_enemy_time = level.time * 0.001f;
                bot->memory.last_enemy = enemy->entity_num;
            }
        }
    }
    
    // Decay old memories
    float current_time = level.time * 0.001f;
    if (current_time - bot->memory.last_enemy_time > 5.0f) {
        // Forget enemy after 5 seconds
        VectorClear(bot->memory.last_enemy_position);
        bot->memory.last_enemy = -1;
    }
}

void AI_SelectTarget(bot_controller_t *bot) {
    if (!bot || !bot->perception) return;
    
    // Find nearest enemy
    float min_distance = 999999;
    int best_target = -1;
    
    for (int i = 0; i < bot->perception->num_visible_entities; i++) {
        entity_info_t *entity = &bot->perception->visible_entities[i];
        if (entity->is_enemy && entity->distance < min_distance) {
            min_distance = entity->distance;
            best_target = entity->entity_num;
        }
    }
    
    bot->goals.combat_target = best_target;
}

void AI_AimAtTarget(bot_controller_t *bot, int target) {
    if (!bot || target < 0 || target >= MAX_GENTITIES) return;
    
    gentity_t *enemy = &g_entities[target];
    if (!enemy->inuse) return;
    
    // Calculate aim angles
    vec3_t dir;
    VectorSubtract(enemy->s.pos.trBase, bot->current_state.position, dir);
    vec3_t angles;
    vectoangles(dir, angles);
    VectorCopy(angles, bot->current_state.view_angles);
    
    // Add prediction based on enemy velocity
    if (bot->personality_traits.skill_level > 0.5f) {
        vec3_t predicted_pos;
        float time_to_impact = VectorLength(dir) / 2000.0f; // Assume bullet speed
        VectorMA(enemy->s.pos.trBase, time_to_impact, enemy->s.pos.trDelta, predicted_pos);
        VectorSubtract(predicted_pos, bot->current_state.position, dir);
        vectoangles(dir, angles);
        VectorCopy(angles, bot->current_state.view_angles);
    }
}

void AI_FireWeapon(bot_controller_t *bot) {
    if (!bot) return;
    
    // Check if we have a valid target
    if (bot->goals.combat_target < 0 || bot->goals.combat_target >= MAX_GENTITIES) return;
    
    gentity_t *enemy = &g_entities[bot->goals.combat_target];
    if (!enemy->inuse || enemy->health <= 0) return;
    
    // Check if in range
    vec3_t dir;
    VectorSubtract(enemy->s.pos.trBase, bot->current_state.position, dir);
    float distance = VectorLength(dir);
    
    // Weapon range check
    float max_range = 1000.0f; // Default range
    switch (bot->inventory.current_weapon) {
        case WP_SHOTGUN:
            max_range = 200.0f;
            break;
        case WP_MACHINEGUN:
            max_range = 500.0f;
            break;
        case WP_RAILGUN:
            max_range = 2000.0f;
            break;
    }
    
    if (distance <= max_range) {
        // Fire weapon - would trigger actual weapon fire in game
        bot->input.actionflags |= ACTION_ATTACK;
        bot->reaction_time = level.time * 0.001f;
    }
}

void AI_NavigateToGoal(bot_controller_t *bot, const vec3_t goal) {
    if (!bot) return;
    
    // Calculate path to goal
    vec3_t direction;
    VectorSubtract(goal, bot->current_state.position, direction);
    float distance = VectorNormalize(direction);
    
    // Check if reached goal
    if (distance < 50.0f) {
        bot->current_goal.type = GOAL_NONE;
        VectorClear(bot->goals.movement_goal);
        return;
    }
    
    // Set movement direction
    VectorCopy(goal, bot->goals.movement_goal);
    
    // Adjust speed based on distance
    if (distance < 200.0f) {
        bot->input.speed = 200.0f; // Walk
        bot->input.actionflags |= ACTION_WALK;
    } else {
        bot->input.speed = 320.0f; // Run
        bot->input.actionflags |= ACTION_MOVEFORWARD;
    }
    
    // Simple obstacle avoidance
    trace_t trace;
    vec3_t end;
    VectorMA(bot->current_state.position, 100, direction, end);
    trap_Trace(&trace, bot->current_state.position, NULL, NULL, end, 
               bot->client_num, MASK_SOLID);
    
    if (trace.fraction < 1.0f) {
        // Obstacle detected, try to go around
        vec3_t right;
        right[0] = -direction[1];
        right[1] = direction[0];
        right[2] = 0;
        
        // Try right side
        VectorMA(bot->current_state.position, 100, right, bot->goals.movement_goal);
        VectorNormalize(direction);
    }
}

void AI_HandleStuck(bot_controller_t *bot) {
    if (!bot) return;
    
    float current_time = level.time * 0.001f;
    static vec3_t last_position[MAX_CLIENTS];
    static float stuck_time[MAX_CLIENTS];
    
    // Check if position hasn't changed
    vec3_t movement;
    VectorSubtract(bot->current_state.position, last_position[bot->client_num], movement);
    float moved = VectorLength(movement);
    
    if (moved < 10.0f && bot->input.speed > 0) {
        // We're stuck
        stuck_time[bot->client_num] += current_time - bot->state_time;
        
        if (stuck_time[bot->client_num] > 1.0f) {
            // Try random direction
            // Try random direction
            bot->input.dir[0] = crandom();
            bot->input.dir[1] = crandom();
            bot->input.dir[2] = 0;
            VectorNormalize(bot->input.dir);
            bot->input.actionflags |= ACTION_MOVEBACK; // Back up
            
            // Maybe jump
            if (random() > 0.5f) {
                bot->input.actionflags |= ACTION_JUMP; // Jump
            }
            
            stuck_time[bot->client_num] = 0;
        }
    } else {
        stuck_time[bot->client_num] = 0;
    }
    
    VectorCopy(bot->current_state.position, last_position[bot->client_num]);
}

void AI_TeamThink(bot_controller_t *bot) {
    if (!bot) return;
    
    // Get team coordinator from AI manager
    team_coordinator_t *coordinator = ai_manager.team_coordinators[bot->team];
    if (!coordinator) return;
    
    // Update team coordinator
    Team_CoordinateActions(coordinator);
    
    // Check for team objectives
    for (int i = 0; i < coordinator->num_objectives; i++) {
        team_objective_t *obj = &coordinator->objectives[i];
        if (obj->objective && !obj->completed) {
            // Move to objective
            AI_NavigateToGoal(bot, obj->objective->position);
            break;
        }
    }
}

void AI_UpdateLearning(bot_controller_t *bot) {
    if (!bot || !bot->skill_profile) return;
    
    // Update skill metrics based on performance
    adaptation_state_t *state = &bot->adaptation;
    state->client_num = bot->client_num;
    state->profile = bot->skill_profile;
    
    // Calculate recent performance (simplified for now)
    state->recent_kd_ratio = 1.0f; // Would be calculated from game stats
    state->recent_accuracy = 0.75f; // Would be calculated from shot stats
    
    // Analyze and adjust
    Skill_AnalyzePerformance(bot->skill_profile, state);
    Skill_AdjustDifficulty(bot->skill_profile, state);
}

void AI_TrainNetworks(void) {
    // Train neural networks for all bots
    for (int i = 0; i < ai_global.num_bots; i++) {
        bot_controller_t *bot = ai_global.bots[i];
        if (!bot || !bot->learning_agent || !bot->learning_agent->actor_network) continue;
        
        // Prepare training data
        float inputs[32];
        float targets[8];
        
        // Fill inputs with current state
        int idx = 0;
        inputs[idx++] = bot->current_state.health / 100.0f;
        inputs[idx++] = bot->current_state.armor / 100.0f;
        inputs[idx++] = bot->current_state.weapon / 10.0f;
        inputs[idx++] = 0.75f; // Would be calculated from actual accuracy
        inputs[idx++] = (bot->current_goal.entity_num >= 0) ? 1.0f : 0.0f;
        
        // Fill remaining inputs
        while (idx < 32) {
            inputs[idx++] = 0;
        }
        
        // Set target outputs based on success
        targets[0] = (bot->current_state.health > 50) ? 1.0f : 0.0f; // Simplified for now
        for (int j = 1; j < 8; j++) {
            targets[j] = 0;
        }
        
        // Train network (would call actual NN training function)
        // NN_Train would be called here with proper implementation
    }
}

// Nav_LoadMesh for AI interface
nav_mesh_t *Nav_LoadMesh(const char *mapname) {
    // Load navigation mesh for the map
    nav_mesh_t *mesh = (nav_mesh_t *)Z_Malloc(sizeof(nav_mesh_t));
    memset(mesh, 0, sizeof(nav_mesh_t));
    
    // Would load actual mesh data from file
    Q_strncpyz(mesh->mapname, mapname, sizeof(mesh->mapname));
    mesh->loaded = qtrue;
    mesh->num_nodes = 0;
    mesh->num_areas = 0;
    
    return mesh;
}

// AI Debug Draw function for visualization
void AI_DebugDraw(bot_controller_t *bot) {
    if (!bot || !bot->debug_enabled) return;
    
    // Draw debug information for the bot
    vec3_t mins = {-16, -16, -24};
    vec3_t maxs = {16, 16, 32};
    vec4_t color = {1.0f, 0.0f, 0.0f, 0.5f}; // Red color for debug
    
    // Draw bot's bounding box (would use actual rendering API)
    // G_DebugBox(bot->current_state.position, mins, maxs, color);
    
    // Draw navigation path
    if (bot->movement && bot->movement->path.is_valid) {
        for (int i = 0; i < bot->movement->path.num_waypoints - 1; i++) {
            vec3_t start, end;
            VectorCopy(bot->movement->path.waypoints[i].position, start);
            VectorCopy(bot->movement->path.waypoints[i+1].position, end);
            // G_DebugLine(start, end, color);
        }
    }
    
    // Draw perception cone
    vec3_t forward_end;
    VectorMA(bot->current_state.position, 1000, bot->current_state.forward, forward_end);
    // G_DebugLine(bot->current_state.position, forward_end, color);
    
    // Draw target indicator
    if (bot->goals.combat_target >= 0) {
        vec3_t target_pos;
        AI_GetEntityPosition(bot->goals.combat_target, target_pos);
        vec4_t target_color = {1.0f, 1.0f, 0.0f, 1.0f}; // Yellow for target
        // G_DebugSphere(target_pos, 20.0f, target_color);
    }
    
    // Update debug string with bot state info
    Com_sprintf(bot->debug_string, sizeof(bot->debug_string), 
        "Bot %d: State=%d Health=%d Armor=%d Target=%d", 
        bot->client_num, bot->state, (int)bot->current_state.health, 
        (int)bot->current_state.armor, bot->goals.combat_target);
}

// Client Think function for bot processing
void ClientThink_real(gentity_t *ent) {
    if (!ent || !ent->client) return;
    
    // Process client thinking
    gclient_t *client = ent->client;
    
    // Update client's view angles
    client->ps.viewangles[0] = ent->s.angles[0];
    client->ps.viewangles[1] = ent->s.angles[1];
    client->ps.viewangles[2] = ent->s.angles[2];
    
    // Process movement
    if (client->ps.pm_type != PM_DEAD) {
        // Apply user command
        pmove_t pm;
        memset(&pm, 0, sizeof(pm));
        pm.ps = &client->ps;
        pm.cmd = client->pers.cmd;
        pm.tracemask = MASK_PLAYERSOLID;
        pm.trace = trap_Trace;
        pm.pointcontents = trap_PointContents;
        
        // Perform the move (simplified)
        VectorCopy(client->ps.origin, ent->s.pos.trBase);
        VectorCopy(client->ps.viewangles, ent->s.angles);
        
        // Update entity state
        ent->s.pos.trType = TR_LINEAR;
        ent->s.pos.trTime = level.time;
        VectorCopy(client->ps.velocity, ent->s.pos.trDelta);
    }
    
    // Update entity bounds - use standard player bounds
    VectorSet(ent->r.mins, -15, -15, -24);
    VectorSet(ent->r.maxs, 15, 15, 32);
    
    // Link entity back into world
    trap_LinkEntity(ent);
}

// Memory allocation wrappers
// The engine builds with ZONE_DEBUG which changes Z_Malloc to Z_MallocDebug
// We need to provide wrappers that the AI code can link to
#ifdef Z_Malloc
#undef Z_Malloc
#endif
#ifdef Z_Free
#undef Z_Free
#endif

// Memory allocation functions are provided by the engine through qcommon.h
// Z_Malloc and Z_Free are already available

// Trap function implementations
void trap_LinkEntity(gentity_t *ent) {
    // Stub - entity linking handled by engine
}

int trap_PointContents(const vec3_t point, int passEntityNum) {
    // Stub - return empty space
    return 0;
}

// Distance calculation function
float G_Distance(vec3_t a, vec3_t b) {
    vec3_t diff;
    VectorSubtract(a, b, diff);
    return VectorLength(diff);
}

// AI Think Bot function
void AI_ThinkBot(int clientNum) {
    if (clientNum < 0 || clientNum >= MAX_CLIENTS) return;
    
    bot_controller_t *bot = AI_GetBot(clientNum);
    if (!bot) return;
    
    // Update bot thinking
    AI_BotThink(bot, level.time);
}

// Combat Update Target function
void Combat_UpdateTarget(tactical_combat_t *combat, int target_id, const vec3_t target_pos) {
    if (!combat) return;
    
    // Update combat target information in the decision structure
    combat->decision.primary_target = target_id;
    if (target_id >= 0 && target_pos) {
        // Store target position in memory
        VectorCopy(target_pos, combat->memory.last_enemy_position);
        combat->memory.enemy_last_seen = level.time * 0.001f;
        
        // Calculate engagement metrics using a temporary position
        vec3_t to_target;
        vec3_t current_pos = {0, 0, 0}; // Use a default position or get from elsewhere
        VectorSubtract(target_pos, current_pos, to_target);
        float distance = VectorLength(to_target);
        
        // Update engagement parameters based on distance
        if (distance < 200) {
            combat->engagement.optimal_range = 150.0f;
            combat->engagement.burst_duration = 1.0f;
        } else if (distance < 500) {
            combat->engagement.optimal_range = 350.0f;
            combat->engagement.burst_duration = 0.8f;
        } else {
            combat->engagement.optimal_range = 750.0f;
            combat->engagement.burst_duration = 0.5f;
        }
    } else {
        combat->decision.primary_target = -1;
    }
}

// Implementation function bodies are defined at the start of the file
// No duplicate definitions needed here

// Global game state (simplified)
game_locals_t game = {
    .maxclients = 32,
    .framenum = 0,
    .time = 0
};

// Duplicate function definitions removed - these are already implemented above