/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Bot Input System
Converts AI decisions to game commands
===========================================================================
*/

#include "ai_main.h"
#include "game_interface.h"
#include "game_entities.h"
#include "ai_system.h"
#include "../../engine/common/q_shared.h"

// Forward declarations
void Bot_UpdateConfigString(bot_controller_t *bot, int num, const char *string);
void Bot_UpdatePlayerInfo(bot_controller_t *bot, int clientNum, const char *info);
void Bot_UpdateItemInfo(bot_controller_t *bot, int itemNum, const char *info);
void Bot_UpdateScores(bot_controller_t *bot, const char *scores);
int Bot_SelectBestWeapon(bot_controller_t *bot);
void Bot_UpdateFromGame(bot_controller_t *bot);

/*
==================
Bot_UpdateInput

Convert bot controller state to usercmd_t for game input
==================
*/
void Bot_UpdateInput(bot_controller_t *bot, usercmd_t *ucmd) {
    vec3_t viewangles, move_dir;
    float forward_move, right_move;
    int weapon;
    
    if (!bot || !ucmd) {
        return;
    }
    
    // Clear the command
    memset(ucmd, 0, sizeof(usercmd_t));
    
    // Set view angles
    VectorCopy(bot->current_state.view_angles, viewangles);
    ucmd->angles[PITCH] = ANGLE2SHORT(viewangles[PITCH]);
    ucmd->angles[YAW] = ANGLE2SHORT(viewangles[YAW]);
    ucmd->angles[ROLL] = ANGLE2SHORT(viewangles[ROLL]);
    
    // Movement
    if (bot->movement) {
        VectorCopy(bot->movement->desired_velocity, move_dir);
    } else {
        VectorClear(move_dir);
    }
    forward_move = DotProduct(move_dir, bot->current_state.forward);
    right_move = DotProduct(move_dir, bot->current_state.right);
    
    // Scale to command range (-127 to 127)
    ucmd->forwardmove = (signed char)(forward_move * 127.0f / 320.0f);
    ucmd->rightmove = (signed char)(right_move * 127.0f / 320.0f);
    ucmd->upmove = (signed char)(move_dir[2] * 127.0f / 320.0f);
    
    // Buttons
    if (bot->combat && bot->combat->firing) {
        ucmd->buttons |= BUTTON_ATTACK;
    }
    
    if (((bot->current_state.velocity[2] > 0) ? qtrue : qfalse)) {
        ucmd->upmove = 127; // Jump
    }
    
    if (bot->current_state.ducking) {
        ucmd->upmove = -127; // Crouch
    }
    
    // Use button for interactions
    if (bot->current_goal.type == GOAL_ITEM ||
        bot->current_goal.type == GOAL_BUTTON) {
        float dist = Distance(bot->current_state.position, bot->current_goal.position);
        if (dist < 64) {
            ucmd->buttons |= BUTTON_USE;
        }
    }
    
    // Weapon selection
    weapon = Bot_SelectBestWeapon(bot);
    if (bot->combat && weapon != bot->combat->current_weapon) {
        ucmd->weapon = weapon;
        bot->combat->current_weapon = weapon;
    }
    
    // Special actions
    if (bot->state == BOT_STATE_RETREATING) {
        // Move backwards while shooting
        ucmd->forwardmove = -ucmd->forwardmove;
        ucmd->buttons |= BUTTON_ATTACK;
    }
    
    // Portal gun support
    if (bot->portal_state.wants_orange_portal) {
        ucmd->buttons |= BUTTON_PORTAL_ORANGE;
        bot->portal_state.wants_orange_portal = qfalse;
    }
    
    if (bot->portal_state.wants_blue_portal) {
        ucmd->buttons |= BUTTON_PORTAL_BLUE;
        bot->portal_state.wants_blue_portal = qfalse;
    }
    
    // Gesture/taunt
    if (bot->personality_traits.taunt_frequency > 0.5f && random() < 0.001f) {
        ucmd->buttons |= BUTTON_GESTURE;
    }
    
    // Server time
    ucmd->serverTime = game.time;
}

/*
==================
Bot_SelectBestWeapon

Select the best weapon for current situation
==================
*/
int Bot_SelectBestWeapon(bot_controller_t *bot) {
    float dist_to_enemy = 999999;
    int best_weapon = WP_MACHINEGUN;
    
    if (!bot) {
        return WP_MACHINEGUN;
    }
    
    // Get distance to current enemy
    if (bot->perception && bot->perception->current_enemy >= 0) {
        gentity_t *enemy = &g_entities[bot->perception->current_enemy];
        if (enemy && enemy->inuse) {
            dist_to_enemy = G_Distance(&g_entities[bot->client_num], enemy);
        }
    }
    
    // Select weapon based on distance and ammo
    if (dist_to_enemy < 200 && bot->inventory.weapons[WP_SHOTGUN].has_weapon &&
        bot->inventory.weapons[WP_SHOTGUN].ammo > 0) {
        best_weapon = WP_SHOTGUN;
    }
    else if (dist_to_enemy < 400 && bot->inventory.weapons[WP_LIGHTNING].has_weapon &&
             bot->inventory.weapons[WP_LIGHTNING].ammo > 0) {
        best_weapon = WP_LIGHTNING;
    }
    else if (dist_to_enemy > 600 && bot->inventory.weapons[WP_RAILGUN].has_weapon &&
             bot->inventory.weapons[WP_RAILGUN].ammo > 10) {
        best_weapon = WP_RAILGUN;
    }
    else if (bot->inventory.weapons[WP_ROCKET_LAUNCHER].has_weapon &&
             bot->inventory.weapons[WP_ROCKET_LAUNCHER].ammo > 5 &&
             dist_to_enemy > 150) { // Avoid self-damage
        best_weapon = WP_ROCKET_LAUNCHER;
    }
    else if (bot->inventory.weapons[WP_PLASMAGUN].has_weapon &&
             bot->inventory.weapons[WP_PLASMAGUN].ammo > 20) {
        best_weapon = WP_PLASMAGUN;
    }
    
    return best_weapon;
}

/*
==================
Bot_ProcessServerCommand

Process server commands sent to bot
==================
*/
void Bot_ProcessServerCommand(bot_controller_t *bot, const char *text) {
    char command[MAX_STRING_CHARS];
    char *cmd;
    
    if (!bot || !text) {
        return;
    }
    
    Q_strncpyz(command, text, sizeof(command));
    cmd = strtok(command, " ");
    
    if (!cmd) {
        return;
    }
    
    // Handle various server commands
    if (!Q_stricmp(cmd, "cp")) {
        // Center print message
        char *message = strtok(NULL, "");
        if (message) {
            Com_DPrintf("Bot %d received message: %s\n", bot->client_num, message);
        }
    }
    else if (!Q_stricmp(cmd, "cs")) {
        // Config string update
        int num = atoi(strtok(NULL, " "));
        char *str = strtok(NULL, "");
        if (str) {
            Bot_UpdateConfigString(bot, num, str);
        }
    }
    else if (!Q_stricmp(cmd, "print")) {
        // Print message
        char *message = strtok(NULL, "");
        if (message) {
            Com_DPrintf("Bot %d: %s\n", bot->client_num, message);
        }
    }
}

/*
==================
Bot_UpdateConfigString

Update bot's knowledge of config strings
==================
*/
void Bot_UpdateConfigString(bot_controller_t *bot, int num, const char *string) {
    if (!bot || !string) {
        return;
    }
    
    // Handle different config string types
    if (num >= CS_PLAYERS && num < CS_PLAYERS + MAX_CLIENTS) {
        // Player info update
        int clientNum = num - CS_PLAYERS;
        Bot_UpdatePlayerInfo(bot, clientNum, string);
    }
    else if (num >= CS_ITEMS && num < CS_ITEMS + MAX_ITEMS) {
        // Item spawn info
        int itemNum = num - CS_ITEMS;
        Bot_UpdateItemInfo(bot, itemNum, string);
    }
    else if (num == CS_SCORES1 || num == CS_SCORES2) {
        // Score update
        Bot_UpdateScores(bot, string);
    }
}

/*
==================
Bot_UpdatePlayerInfo

Update bot's knowledge of other players
==================
*/
void Bot_UpdatePlayerInfo(bot_controller_t *bot, int clientNum, const char *info) {
    char name[MAX_NAME_LENGTH];
    int team;
    
    if (!bot || !info || clientNum < 0 || clientNum >= MAX_CLIENTS) {
        return;
    }
    
    // Parse player info
    Q_strncpyz(name, Info_ValueForKey(info, "n"), sizeof(name));
    team = atoi(Info_ValueForKey(info, "t"));
    
    // Update perception system
    if (clientNum != bot->client_num) {
        if (bot->perception) {
            bot->perception->player_info[clientNum].valid = qtrue;
            bot->perception->player_info[clientNum].team = team;
            Q_strncpyz(bot->perception->player_info[clientNum].name, name,
                       sizeof(bot->perception->player_info[clientNum].name));
        }
    }
}

/*
==================
Bot_UpdateItemInfo

Update bot's knowledge of items
==================
*/
void Bot_UpdateItemInfo(bot_controller_t *bot, int itemNum, const char *info) {
    if (!bot || !info || itemNum < 0 || itemNum >= MAX_ITEMS) {
        return;
    }
    
    // Parse item spawn info and update memory
    // This would track item locations and respawn times
}

/*
==================
Bot_UpdateScores

Update bot's knowledge of game scores
==================
*/
void Bot_UpdateScores(bot_controller_t *bot, const char *scores) {
    if (!bot || !scores) {
        return;
    }
    
    // Parse score info
    // This helps bot understand if it's winning/losing
}

/*
==================
Bot_ClientThink

Main think function called each frame for bot
==================
*/
void Bot_ClientThink(int clientNum, usercmd_t *ucmd) {
    bot_controller_t *bot;
    
    bot = AI_GetBot(clientNum);
    if (!bot) {
        return;
    }
    
    // Update bot state from game
    Bot_UpdateFromGame(bot);
    
    // Run AI think
    AI_ThinkBot(bot, (float)game.time * 0.001f);
    
    // Convert decisions to input
    Bot_UpdateInput(bot, ucmd);
}

/*
==================
Bot_UpdateFromGame

Update bot state from game entities
==================
*/
void Bot_UpdateFromGame(bot_controller_t *bot) {
    gentity_t *ent;
    gclient_t *client;
    
    if (!bot) {
        return;
    }
    
    ent = &g_entities[bot->client_num];
    if (!ent || !ent->inuse || !ent->client) {
        return;
    }
    
    client = ent->client;
    
    // Update position and angles
    VectorCopy(client->ps.origin, bot->current_state.position);
    VectorCopy(client->ps.viewangles, bot->current_state.view_angles);
    VectorCopy(client->ps.velocity, bot->current_state.velocity);
    
    // Update health and armor
    bot->current_state.health = client->ps.stats[STAT_HEALTH];
    bot->current_state.armor = client->ps.stats[STAT_ARMOR];
    
    // Update weapons and ammo
    if (bot->combat) {
        bot->combat->current_weapon = client->ps.weapon;
    }
    bot->inventory.current_weapon = client->ps.weapon;
    
    for (int i = 0; i < MAX_WEAPONS; i++) {
        bot->inventory.weapons[i].has_weapon = (client->ps.stats[STAT_WEAPONS] & (1 << i)) != 0;
        bot->inventory.weapons[i].ammo = client->ps.ammo[i];
    }
    
    // Update powerups
    bot->inventory.powerups[PW_QUAD] = client->ps.powerups[PW_QUAD] > game.time;
    bot->inventory.powerups[PW_BATTLESUIT] = client->ps.powerups[PW_BATTLESUIT] > game.time;
    bot->inventory.powerups[PW_HASTE] = client->ps.powerups[PW_HASTE] > game.time;
    bot->inventory.powerups[PW_INVIS] = client->ps.powerups[PW_INVIS] > game.time;
    bot->inventory.powerups[PW_REGEN] = client->ps.powerups[PW_REGEN] > game.time;
    
    // Update team info
    bot->team_state.team = client->sess.sessionTeam;
    
    // Calculate view vectors
    AngleVectors(bot->current_state.view_angles, 
                 bot->current_state.forward,
                 bot->current_state.right,
                 bot->current_state.up);
}