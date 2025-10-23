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

#include "ai_main.h"
#include "../../engine/core/qcommon.h"
#include "game_interface.h"
#include "game_entities.h"
#include "ai_system.h"
#include "character/bot_character.h"
#include <string.h>

// Global stubs for compilation

extern void ClientThink_real(gentity_t *ent);
#ifndef GAME_IMPLEMENTATION
level_locals_t level = {0};
gentity_t *g_entities = NULL;
#endif

// Global AI manager
ai_manager_t ai_manager;

/*
==================
AI_Init
==================
*/
void AI_Init(void) {
    if (ai_manager.initialized) {
        return;
    }
    
    memset(&ai_manager, 0, sizeof(ai_manager));
    
    // Register CVARs
    ai_manager.ai_enabled = Cvar_Get("ai_enable", "1", CVAR_ARCHIVE);
    ai_manager.ai_debug = Cvar_Get("ai_debug", "0", 0);
    ai_manager.ai_skill = Cvar_Get("ai_skill", "2", CVAR_ARCHIVE);
    ai_manager.ai_learning = Cvar_Get("ai_learning", "1", CVAR_ARCHIVE);
    ai_manager.ai_teamplay = Cvar_Get("ai_teamplay", "1", CVAR_ARCHIVE);
    ai_manager.ai_think_time = Cvar_Get("ai_think_time", "50", CVAR_ARCHIVE);
    
    Com_Printf("=== Quake3e-HD Advanced AI System v%s ===\n", AI_VERSION);
    
    // Initialize bot character system
    BotChar_Init();
    
    // Initialize neural subsystems
    NN_Init();
    PPO_Init();
    Skill_InitSystem();
    Combat_Init();
    Cover_InitSystem();
    Movement_Init();
    Strategy_Init();
    Team_InitCoordination();
    Perception_Init();
    
    // Create shared managers
    ai_manager.cover_manager = Cover_CreateManager();
    
    // Don't analyze map yet - defer until game world is ready
    // Cover_AnalyzeMap will be called from AI_MapLoaded() instead
    
    // Create team coordinators
    for (int i = 0; i < 4; i++) {
        ai_manager.team_coordinators[i] = Team_CreateCoordinator(i);
    }
    
    ai_manager.initialized = qtrue;
    
    Com_Printf("AI System initialized successfully\n");
    Com_Printf("- Neural Networks: Enabled\n");
    Com_Printf("- Reinforcement Learning: %s\n", ai_manager.ai_learning->integer ? "Enabled" : "Disabled");
    Com_Printf("- Team Coordination: %s\n", ai_manager.ai_teamplay->integer ? "Enabled" : "Disabled");
    Com_Printf("- Cover Points Found: %d\n", 
               ai_manager.cover_manager ? ai_manager.cover_manager->num_cover_points : 0);
}

/*
==================
AI_MapLoaded

Called when a map is loaded and the game world is ready
==================
*/
void AI_MapLoaded(void) {
    if (!ai_manager.initialized) {
        return;
    }
    
    // Now it's safe to analyze the map for cover points
    if (ai_manager.cover_manager) {
        Cover_AnalyzeMap(ai_manager.cover_manager);
        Com_Printf("AI: Analyzed map, found %d cover points\n", 
                   ai_manager.cover_manager->num_cover_points);
    }
}

/*
==================
AI_Shutdown
==================
*/
void AI_Shutdown(void) {
    int i;
    
    if (!ai_manager.initialized) {
        return;
    }
    
    // Save training data if in training mode
    if (ai_manager.training_mode) {
        AI_SaveTrainingData();
    }
    
    // Destroy all bots
    for (i = 0; i < MAX_AI_CLIENTS; i++) {
        if (ai_manager.bots[i]) {
            AI_DestroyBot(ai_manager.bots[i]);
        }
    }
    
    // Destroy team coordinators
    for (i = 0; i < 4; i++) {
        if (ai_manager.team_coordinators[i]) {
            Team_DestroyCoordinator(ai_manager.team_coordinators[i]);
        }
    }
    
    // Destroy shared managers
    if (ai_manager.cover_manager) {
        Cover_DestroyManager(ai_manager.cover_manager);
    }
    
    // Shutdown subsystems
    Perception_Shutdown();
    Team_ShutdownCoordination();
    Strategy_Shutdown();
    Movement_Shutdown();
    Cover_ShutdownSystem();
    Combat_Shutdown();
    Skill_ShutdownSystem();
    PPO_Shutdown();
    NN_Shutdown();
    
    ai_manager.initialized = qfalse;
    
    Com_Printf("AI System shutdown complete\n");
}

/*
==================
AI_CreateBot
==================
*/
bot_controller_t *AI_CreateBot(int clientNum, const char *name, bot_personality_t personality) {
    bot_controller_t *bot;
    float initial_skill;
    bot_character_t *character = NULL;
    
    if (clientNum < 0 || clientNum >= MAX_AI_CLIENTS) {
        return NULL;
    }
    
    // Check if bot already exists
    if (ai_manager.bots[clientNum]) {
        AI_DestroyBot(ai_manager.bots[clientNum]);
    }
    
    bot = (bot_controller_t *)Z_Malloc(sizeof(bot_controller_t));
    memset(bot, 0, sizeof(bot_controller_t));
    
    // Initialize identity
    bot->client_num = clientNum;
    Q_strncpyz(bot->name, name, sizeof(bot->name));
    bot->personality = personality;
    bot->state = BOT_STATE_SPAWNING;
    
    
    // Load bot character configuration using new system
    character = BotChar_LoadCharacter(name, (int)ai_manager.ai_skill->value);
    
    if (character && character->valid) {
        bot->character_handle = (int)character; // Store pointer as handle
        
        // Read character attributes to configure neural AI
        float aggression = character->aggression;
        float reaction = character->reaction_time;
        float accuracy = character->accuracy;
        
        // Map characteristics to personality if not explicitly set
        if (personality == BOT_PERSONALITY_RANDOM) {
            if (aggression > 0.7f) {
                personality = BOT_PERSONALITY_AGGRESSIVE;
            } else if (aggression < 0.3f) {
                personality = BOT_PERSONALITY_DEFENSIVE;
            } else if (accuracy > 0.7f) {
                personality = BOT_PERSONALITY_TACTICAL;
            } else {
                personality = BOT_PERSONALITY_SUPPORT;
            }
            bot->personality = personality;
        }
        
        Com_Printf("Loaded character configuration for bot '%s'\n", name);
    } else {
        Com_Printf("Using default character configuration for bot '%s'\n", name);
        // Create default character
        character = BotChar_GetDefaultCharacter((int)ai_manager.ai_skill->value);
        bot->character_handle = (int)character;
    }
    
    // Create subsystems
    bot->perception = Perception_Create();
    
    // Set combat style based on personality
    combat_style_t combat_style = COMBAT_STYLE_BALANCED;
    movement_style_t movement_style = MOVE_STYLE_NORMAL;
    
    switch (personality) {
        case BOT_PERSONALITY_AGGRESSIVE:
            combat_style = COMBAT_STYLE_AGGRESSIVE;
            movement_style = MOVE_STYLE_AGGRESSIVE;
            break;
        case BOT_PERSONALITY_DEFENSIVE:
            combat_style = COMBAT_STYLE_DEFENSIVE;
            movement_style = MOVE_STYLE_TACTICAL;
            break;
        case BOT_PERSONALITY_TACTICAL:
            combat_style = COMBAT_STYLE_TACTICAL;
            movement_style = MOVE_STYLE_TACTICAL;
            break;
        case BOT_PERSONALITY_SUPPORT:
            combat_style = COMBAT_STYLE_SUPPORT;
            movement_style = MOVE_STYLE_NORMAL;
            break;
        case BOT_PERSONALITY_SCOUT:
            combat_style = COMBAT_STYLE_GUERRILLA;
            movement_style = MOVE_STYLE_EVASIVE;
            break;
        default:
            break;
    }
    
    bot->combat = Combat_Create(combat_style);
    bot->movement = Movement_Create(movement_style);
    
    // Create skill profile based on character configuration or defaults
    initial_skill = ai_manager.ai_skill->value;
    
    // If we have character configuration, adjust skill based on loaded characteristics
    if (bot->character_handle > 0) {
        bot_character_t *ch = (bot_character_t *)bot->character_handle;
        if (ch && ch->valid) {
            float char_skill = initial_skill;
            
            float aim_skill = ch->accuracy;
            float attack_skill = ch->aggression;
            float move_skill = ch->movement_skill;
            
            // Average the skills to get overall skill level (1-5 scale)
            char_skill = 1.0f + (aim_skill + attack_skill + move_skill) * 1.33f;  // Scale 0-1 to 1-5
            char_skill = CLAMP(char_skill, 1.0f, 5.0f);
            
            initial_skill = char_skill;
        }
    }
    
    bot->skill_profile = Skill_CreateProfile(initial_skill);
    
    // Create learning agent if enabled
    if (ai_manager.ai_learning->integer) {
        bot->learning_agent = PPO_CreateAgent(128, 32); // State size, action size
        bot->learning_enabled = qtrue;
    }
    
    // Initialize reaction time based on skill
    bot->reaction_time = Skill_GetReactionDelay(bot->skill_profile);
    
    // Apply character-specific configurations to neural AI components
    if (bot->character_handle > 0) {
        AI_ApplyCharacterTraits(bot);
    }
    
    // Add to manager
    ai_manager.bots[clientNum] = bot;
    ai_manager.num_bots++;
    
    // Add to team coordinator if team play enabled
    if (ai_manager.ai_teamplay->integer && bot->team >= 0 && bot->team < 4) {
        team_coordinator_t *coordinator = ai_manager.team_coordinators[bot->team];
        if (coordinator) {
            team_role_t role = TEAM_ROLE_ASSAULT; // Default role
            
            // Assign role based on personality
            switch (personality) {
                case BOT_PERSONALITY_AGGRESSIVE:
                    role = TEAM_ROLE_ASSAULT;
                    break;
                case BOT_PERSONALITY_DEFENSIVE:
                    role = TEAM_ROLE_DEFENDER;
                    break;
                case BOT_PERSONALITY_SUPPORT:
                    role = TEAM_ROLE_SUPPORT;
                    break;
                case BOT_PERSONALITY_SCOUT:
                    role = TEAM_ROLE_SCOUT;
                    break;
                default:
                    break;
            }
            
            Team_AddMember(coordinator, clientNum, role);
        }
    }
    
    Com_Printf("Created bot '%s' (client %d) with %s personality\n", 
               name, clientNum, 
               personality == BOT_PERSONALITY_AGGRESSIVE ? "aggressive" :
               personality == BOT_PERSONALITY_DEFENSIVE ? "defensive" :
               personality == BOT_PERSONALITY_TACTICAL ? "tactical" :
               personality == BOT_PERSONALITY_SUPPORT ? "support" :
               personality == BOT_PERSONALITY_SCOUT ? "scout" : "random");
    
    return bot;
}

/*
==================
AI_DestroyBot
==================
*/
void AI_DestroyBot(bot_controller_t *bot) {
    if (!bot) {
        return;
    }
    
    // Remove from team
    if (ai_manager.ai_teamplay->integer && bot->team >= 0 && bot->team < 4) {
        team_coordinator_t *coordinator = ai_manager.team_coordinators[bot->team];
        if (coordinator) {
            Team_RemoveMember(coordinator, bot->client_num);
        }
    }
    
    // Free character data if it exists
    if (bot->character_handle > 0) {
        bot_character_t *ch = (bot_character_t *)bot->character_handle;
        BotChar_FreeCharacter(ch);
        bot->character_handle = 0;
    }
    
    // Destroy subsystems
    if (bot->perception) {
        Perception_Destroy(bot->perception);
    }
    
    if (bot->combat) {
        Combat_Destroy(bot->combat);
    }
    
    if (bot->movement) {
        Movement_Destroy(bot->movement);
    }
    
    if (bot->skill_profile) {
        Skill_DestroyProfile(bot->skill_profile);
    }
    
    if (bot->learning_agent) {
        PPO_DestroyAgent(bot->learning_agent);
    }
    
    // Remove from manager
    if (bot->client_num >= 0 && bot->client_num < MAX_AI_CLIENTS) {
        ai_manager.bots[bot->client_num] = NULL;
        ai_manager.num_bots--;
    }
    
    Z_Free(bot);
}

/*
==================
AI_Frame
==================
*/
void AI_Frame(int levelTime) {
    int i;
    int start_time;
    
    if (!ai_manager.initialized || !ai_manager.ai_enabled->integer) {
        return;
    }
    
    start_time = Sys_Milliseconds();
    
    // Update team coordinators
    if (ai_manager.ai_teamplay->integer) {
        for (i = 0; i < 4; i++) {
            if (ai_manager.team_coordinators[i]) {
                Team_CoordinateActions(ai_manager.team_coordinators[i]);
            }
        }
    }
    
    // Update each bot
    for (i = 0; i < MAX_AI_CLIENTS; i++) {
        bot_controller_t *bot = ai_manager.bots[i];
        
        if (!bot) {
            continue;
        }
        
        // Check if it's time to think
        if (levelTime >= bot->next_think_time) {
            AI_BotThink(bot, levelTime);
            bot->next_think_time = levelTime + ai_manager.ai_think_time->integer;
            bot->think_cycles++;
        }
    }
    
    // Train neural networks periodically
    if (ai_manager.training_mode && ai_manager.total_thinks % 100 == 0) {
        AI_TrainNetworks();
    }
    
    // Update performance metrics
    int frame_time = Sys_Milliseconds() - start_time;
    ai_manager.total_think_time += frame_time;
    ai_manager.total_thinks++;
    
    if (ai_manager.total_thinks > 0) {
        ai_manager.average_think_time = ai_manager.total_think_time / ai_manager.total_thinks;
    }
    
    // Debug output
    if (ai_manager.ai_debug->integer > 1) {
        Com_DPrintf("AI Frame: %d bots, %dms\n", ai_manager.num_bots, frame_time);
    }
}

/*
==================
AI_BotThink
==================
*/
void AI_BotThink(bot_controller_t *bot, int levelTime) {
    int start_time;
    
    if (!bot) {
        return;
    }
    
    start_time = Sys_Milliseconds();
    
    // Update perception
    AI_UpdatePerception(bot);
    
    // Update memory
    AI_UpdateMemory(bot);
    
    // Make decisions
    AI_MakeDecisions(bot);
    
    // State-specific thinking
    switch (bot->state) {
        case BOT_STATE_COMBAT:
            AI_CombatThink(bot);
            break;
            
        case BOT_STATE_MOVING:
            AI_MovementThink(bot);
            break;
            
        case BOT_STATE_SEARCHING:
            AI_MovementThink(bot);
            // Look around while moving
            bot->input.viewangles[YAW] += sin(levelTime * 0.001f) * 30;
            break;
            
        case BOT_STATE_RETREATING:
            AI_MovementThink(bot);
            // Find cover
            if (ai_manager.cover_manager) {
                cover_search_params_t params;
                memset(&params, 0, sizeof(params));
                VectorCopy(bot->perception->self_state.position, params.search_origin);
                params.search_radius = 500;
                VectorCopy(bot->memory.last_damage_origin, params.threat_position);
                params.time_pressure = 0.8f;
                
                cover_point_t *cover = Cover_FindBestCover(ai_manager.cover_manager, &params);
                if (cover) {
                    VectorCopy(cover->position, bot->goals.movement_goal);
                    Cover_EnterCover(&bot->cover_state, cover);
                }
            }
            break;
            
        case BOT_STATE_OBJECTIVE:
            if (ai_manager.ai_teamplay->integer) {
                AI_TeamThink(bot);
            }
            AI_MovementThink(bot);
            break;
            
        case BOT_STATE_IDLE:
            // Look for something to do
            AI_UpdateGoals(bot);
            break;
            
        default:
            break;
    }
    
    // Update bot input based on decisions
    AI_UpdateBotInput(bot);
    
    // Execute actions
    AI_ExecuteBotActions(bot);
    
    // Update learning if enabled
    if (bot->learning_enabled && bot->learning_agent) {
        AI_UpdateLearning(bot);
    }
    
    // Adapt to player skill
    AI_AdaptToPlayer(bot);
    
    // Record think time
    bot->think_time = (Sys_Milliseconds() - start_time) * 0.001f;
    
    // Debug output
    if (bot->debug_enabled || ai_manager.ai_debug->integer) {
        AI_DebugDraw(bot);
    }
}

/*
==================
AI_UpdatePerception
==================
*/
void AI_UpdatePerception(bot_controller_t *bot) {
    gentity_t *self;
    
    if (!bot || !bot->perception) {
        return;
    }
    
    self = &g_entities[bot->client_num];
    if (!self->inuse || !self->client) {
        return;
    }
    
    // Update perception system
    Perception_Update(bot->perception, bot->client_num);
    
    // Process sensory input
    AI_ProcessSensoryInput(bot);
}

/*
==================
AI_ProcessSensoryInput
==================
*/
void AI_ProcessSensoryInput(bot_controller_t *bot) {
    int i;
    
    if (!bot || !bot->perception) {
        return;
    }
    
    // Process visible entities
    for (i = 0; i < bot->perception->num_visible_entities; i++) {
        entity_info_t *entity = &bot->perception->visible_entities[i];
        
        // Track enemies
        if (entity->is_enemy && entity->entity_type == ENTITY_PLAYER) {
            bot->memory.last_enemy = entity->entity_num;
            VectorCopy(entity->position, bot->memory.last_enemy_position);
            bot->memory.last_enemy_time = level.time * 0.001f;
        }
        
        // Track items
        if (entity->entity_type == ENTITY_ITEM) {
            // Add to item memory
            if (bot->memory.num_items < MAX_ITEMS) {
                VectorCopy(entity->position, bot->memory.item_positions[bot->memory.num_items]);
                bot->memory.item_types[bot->memory.num_items] = entity->item_type;
                bot->memory.num_items++;
            }
        }
    }
    
    // Process sounds
    for (i = 0; i < bot->perception->num_sounds; i++) {
        sound_event_t *sound = &bot->perception->sounds[i];
        
        // React to combat sounds
        if (sound->type == SOUND_COMBAT) {
            // Investigate if not in combat
            if (bot->state != BOT_STATE_COMBAT) {
                VectorCopy(sound->origin, bot->goals.movement_goal);
                bot->state = BOT_STATE_SEARCHING;
            }
        }
    }
}

/*
==================
AI_MakeDecisions
==================
*/
void AI_MakeDecisions(bot_controller_t *bot) {
    if (!bot) {
        return;
    }
    
    // Update goals
    AI_UpdateGoals(bot);
    
    // Prioritize goals
    AI_PrioritizeGoals(bot);
    
    // Select state based on priorities
    AI_SelectState(bot);
    
    // Make tactical decisions
    if (bot->combat) {
        Combat_UpdateThreats(bot->combat, bot->perception->self_state.position);
        Combat_MakeDecision(bot->combat);
    }
}

/*
==================
AI_UpdateGoals
==================
*/
void AI_UpdateGoals(bot_controller_t *bot) {
    if (!bot) {
        return;
    }
    
    // Update combat priority
    if (bot->perception->num_visible_enemies > 0) {
        bot->goals.combat_priority = 0.8f;
        bot->goals.combat_target = bot->perception->visible_entities[0].entity_num;
    } else {
        bot->goals.combat_priority *= 0.95f; // Decay over time
    }
    
    // Update item priority
    if (bot->perception->self_state.health < 50) {
        bot->goals.item_priority = 0.9f;
    } else if (bot->perception->self_state.armor < 50) {
        bot->goals.item_priority = 0.6f;
    } else {
        bot->goals.item_priority = 0.3f;
    }
    
    // Update objective priority
    if (bot->goals.team_objective) {
        bot->goals.objective_priority = bot->goals.team_objective->priority * 0.2f;
    }
    
    // Update survival priority
    if (bot->perception->self_state.health < 30) {
        bot->goals.survival_priority = 1.0f;
    } else {
        bot->goals.survival_priority = 0.2f;
    }
}

/*
==================
AI_SelectState
==================
*/
void AI_SelectState(bot_controller_t *bot) {
    bot_state_t new_state;
    
    if (!bot) {
        return;
    }
    
    // Save previous state
    bot->previous_state = bot->state;
    
    // Determine new state based on priorities
    if (bot->goals.survival_priority > 0.8f) {
        new_state = BOT_STATE_RETREATING;
    } else if (bot->goals.combat_priority > 0.6f) {
        new_state = BOT_STATE_COMBAT;
    } else if (bot->goals.objective_priority > 0.5f) {
        new_state = BOT_STATE_OBJECTIVE;
    } else if (bot->goals.item_priority > 0.5f) {
        new_state = BOT_STATE_MOVING;
    } else {
        new_state = BOT_STATE_IDLE;
    }
    
    // Update state if changed
    if (new_state != bot->state) {
        bot->state = new_state;
        bot->state_time = level.time * 0.001f;
        
        if (ai_manager.ai_debug->integer) {
            Com_DPrintf("Bot %d state: %d -> %d\n", bot->client_num, bot->previous_state, bot->state);
        }
    }
}

/*
==================
AI_CombatThink
==================
*/
void AI_CombatThink(bot_controller_t *bot) {
    if (!bot || !bot->combat) {
        return;
    }
    
    // Select target
    AI_SelectTarget(bot);
    
    // Aim at target
    if (bot->goals.combat_target >= 0) {
        AI_AimAtTarget(bot);
        
        // Decide whether to fire
        if (bot->combat->decision.confidence > 0.3f) {
            AI_FireWeapon(bot);
        }
    }
    
    // Execute combat state
    Combat_ExecuteState(bot->combat);
    
    // Apply combat movement
    if (bot->movement) {
        vec3_t dodge;
        Combat_CalculateDodgeVector(bot->combat, dodge);
        Movement_Execute(bot->movement, dodge, &bot->input.speed);
    }
}

/*
==================
AI_MovementThink
==================
*/
void AI_MovementThink(bot_controller_t *bot) {
    vec3_t move_dir;
    float speed;
    
    if (!bot || !bot->movement) {
        return;
    }
    
    // Navigate to goal
    AI_NavigateToGoal(bot);
    
    // Execute movement
    Movement_Execute(bot->movement, move_dir, &speed);
    
    // Set bot input
    VectorCopy(move_dir, bot->input.dir);
    bot->input.speed = speed;
    
    // Check for stuck
    AI_HandleStuck(bot);
}

/*
==================
AI_UpdateBotInput
==================
*/
void AI_UpdateBotInput(bot_controller_t *bot) {
    if (!bot) {
        return;
    }
    
    // Clear previous input
    memset(&bot->cmd, 0, sizeof(bot->cmd));
    
    // Apply movement from bot input dir and speed
    vec3_t forward, right, up;
    AngleVectors(bot->input.viewangles, forward, right, up);
    
    float forward_move = DotProduct(bot->input.dir, forward) * bot->input.speed;
    float right_move = DotProduct(bot->input.dir, right) * bot->input.speed;
    float up_move = bot->input.dir[2] * bot->input.speed;
    
    // Scale to command range (-127 to 127)
    bot->cmd.forwardmove = (signed char)(forward_move * 127.0f / 400.0f);
    bot->cmd.rightmove = (signed char)(right_move * 127.0f / 400.0f);
    bot->cmd.upmove = (signed char)(up_move * 127.0f / 400.0f);
    
    // Apply view angles
    for (int i = 0; i < 3; i++) {
        bot->cmd.angles[i] = ANGLE2SHORT(bot->input.viewangles[i]);
    }
    
    // Apply buttons
    bot->cmd.buttons = bot->input.actionflags;
    
    // Apply weapon
    bot->cmd.weapon = bot->input.weapon;
    
    // Set server time
    bot->cmd.serverTime = level.time;
}

/*
==================
AI_ExecuteBotActions
==================
*/
void AI_ExecuteBotActions(bot_controller_t *bot) {
    gentity_t *ent;
    
    if (!bot) {
        return;
    }
    
    ent = &g_entities[bot->client_num];
    if (!ent->inuse || !ent->client) {
        return;
    }
    
    // Apply the command to the bot's client
    // Copy the command to the entity's client
    if (ent->client) {
        ent->client->pers.cmd = bot->cmd;
    }
    ClientThink_real(ent);
}

/*
==================
AI_GetBot
==================
*/
bot_controller_t *AI_GetBot(int clientNum) {
    if (clientNum < 0 || clientNum >= MAX_AI_CLIENTS) {
        return NULL;
    }
    
    return ai_manager.bots[clientNum];
}

/*
==================
AI_AdaptToPlayer
==================
*/
void AI_AdaptToPlayer(bot_controller_t *bot) {
    if (!bot || !bot->skill_profile) {
        return;
    }
    
    // Update adaptation state
    bot->adaptation.client_num = bot->client_num;
    bot->adaptation.profile = bot->skill_profile;
    bot->adaptation.recent_kd_ratio = (float)bot->combat->memory.kills_this_life / 
                                      MAX(bot->memory.deaths_by_enemy[bot->memory.last_enemy], 1);
    
    // Adjust difficulty
    Skill_AdjustDifficulty(bot->skill_profile, &bot->adaptation);
    
    // Interpolate skill changes
    Skill_InterpolateLevel(bot->skill_profile, 0.05f);
    
    // Apply skill adjustments
    if (bot->combat) {
        bot->combat->accuracy = Skill_GetPredictionAccuracy(bot->skill_profile);
        bot->combat->reaction_delay = Skill_GetReactionDelay(bot->skill_profile);
    }
    
    if (bot->movement) {
        bot->movement->state.max_speed = 320 * Skill_GetMovementSpeed(bot->skill_profile);
    }
}

/*
==================
AI_ApplyCharacterTraits
Applies character file traits to neural AI components
==================
*/
void AI_ApplyCharacterTraits(bot_controller_t *bot) {
    if (!bot || bot->character_handle <= 0) {
        return;
    }
    
    bot_character_t *ch = (bot_character_t *)bot->character_handle;
    if (!ch || !ch->valid) {
        return;
    }
    
    // Character traits from standard Quake 3 bot system
    float aggression = BotChar_GetFloat(ch, CHAR_ATTACK_SKILL);
    float alertness = BotChar_GetFloat(ch, CHAR_ALERTNESS);
    float camp = BotChar_GetFloat(ch, CHAR_CAMPER);
    float firethrottle = BotChar_GetFloat(ch, CHAR_FIRETHROTTLE);
    float jumper = BotChar_GetFloat(ch, CHAR_JUMPER);
    float reaction_time = BotChar_GetFloat(ch, CHAR_REACTIONTIME);
    float aim_accuracy = BotChar_GetFloat(ch, CHAR_AIM_ACCURACY);
    float weapon_jumping = BotChar_GetFloat(ch, CHAR_WEAPONJUMPING);
    float view_factor = BotChar_GetFloat(ch, CHAR_VIEW_FACTOR);
    float view_maxchange = BotChar_GetFloat(ch, CHAR_VIEW_MAXCHANGE);
    float croucher = BotChar_GetFloat(ch, CHAR_CROUCHER);
    float walker = BotChar_GetFloat(ch, CHAR_WALKER);
    
    // Apply traits to combat system
    if (bot->combat) {
        bot->combat->accuracy = aim_accuracy;
        bot->combat->aggression = aggression;
        bot->combat->reaction_delay = reaction_time * 1000.0f; // Convert to milliseconds
        bot->combat->fire_threshold = 1.0f - firethrottle; // Invert for threshold
        
        // Adjust combat style based on traits
        if (camp > 0.7f) {
            bot->combat->style = COMBAT_STYLE_DEFENSIVE;
        } else if (aggression > 0.7f) {
            bot->combat->style = COMBAT_STYLE_AGGRESSIVE;
        }
    }
    
    // Apply traits to movement system
    if (bot->movement) {
        bot->movement->state.jump_frequency = jumper;
        bot->movement->state.crouch_frequency = croucher;
        bot->movement->state.walk_frequency = walker;
        
        // Adjust movement style based on traits
        if (jumper > 0.7f && weapon_jumping > 0.5f) {
            bot->movement->style = MOVE_STYLE_AGGRESSIVE;
        } else if (walker > 0.7f || croucher > 0.7f) {
            bot->movement->style = MOVE_STYLE_TACTICAL;
        }
    }
    
    // Apply traits to perception system
    if (bot->perception) {
        bot->perception->config.view_factor = view_factor;
        bot->perception->config.max_view_change = view_maxchange * 180.0f; // Convert to degrees
        bot->perception->config.alertness = alertness;
    }
    
    // Apply traits to skill profile
    if (bot->skill_profile) {
        // Adjust skill components based on loaded characteristics
        bot->skill_profile->aim_accuracy = aim_accuracy;
        bot->skill_profile->reaction_time = reaction_time;
        bot->skill_profile->aggression = aggression;
        bot->skill_profile->tactical_awareness = alertness;
        bot->skill_profile->movement_prediction = 0.3f + aim_accuracy * 0.4f; // Derived from aim skill
    }
    
    Com_DPrintf("Applied character traits to bot %d: aggression=%.2f, accuracy=%.2f, reaction=%.2f\n",
                bot->client_num, aggression, aim_accuracy, reaction_time);
}

/*
==================
AI_SaveTrainingData
==================
*/
void AI_SaveTrainingData(void) {
    int i;
    char filename[MAX_QPATH];
    
    Com_Printf("Saving AI training data...\n");
    
    for (i = 0; i < MAX_AI_CLIENTS; i++) {
        bot_controller_t *bot = ai_manager.bots[i];
        
        if (!bot || !bot->learning_agent) {
            continue;
        }
        
        // Save PPO agent
        Com_sprintf(filename, sizeof(filename), "ai_training/bot_%d.ppo", i);
        PPO_SaveAgent(bot->learning_agent, filename);
        
        // Save skill profile
        Com_sprintf(filename, sizeof(filename), "ai_training/bot_%d_skill.dat", i);
        Skill_SaveProfile(bot->skill_profile, filename);
    }
    
    Com_Printf("Training data saved\n");
}

/*
==================
AI_GetEntityPosition
Gets the position of an entity
==================
*/
vec3_t *AI_GetEntityPosition(int entityNum, vec3_t pos) {
    // For now, return a stub position
    // In a full implementation, this would retrieve the actual entity position
    // from the game's entity system
    if (pos) {
        VectorClear(pos);
        
        // Basic stub implementation - would normally access entity state
        if (entityNum >= 0 && entityNum < MAX_GENTITIES) {
            // Would normally do: VectorCopy(g_entities[entityNum].s.origin, pos);
            pos[0] = 0;
            pos[1] = 0;
            pos[2] = 0;
        }
    }
    return pos;
}

