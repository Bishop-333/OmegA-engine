/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Bot Input System Header
===========================================================================
*/

#ifndef BOT_INPUT_H
#define BOT_INPUT_H

#include "../../engine/common/q_shared.h"
#include "ai_main.h"

// Bot input functions
void Bot_UpdateInput(bot_controller_t *bot, usercmd_t *ucmd);
int Bot_SelectBestWeapon(bot_controller_t *bot);
void Bot_ProcessServerCommand(bot_controller_t *bot, const char *text);
void Bot_UpdateConfigString(bot_controller_t *bot, int num, const char *string);
void Bot_UpdatePlayerInfo(bot_controller_t *bot, int clientNum, const char *info);
void Bot_UpdateItemInfo(bot_controller_t *bot, int itemNum, const char *info);
void Bot_UpdateScores(bot_controller_t *bot, const char *scores);
void Bot_ClientThink(int clientNum, usercmd_t *ucmd);
void Bot_UpdateFromGame(bot_controller_t *bot);

#endif // BOT_INPUT_H