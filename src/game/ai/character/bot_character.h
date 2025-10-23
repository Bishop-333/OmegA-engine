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

#ifndef BOT_CHARACTER_H
#define BOT_CHARACTER_H

#include "../../../engine/common/q_shared.h"

// Character indices from Quake 3 bot system
#define CHAR_ATTACK_SKILL           1
#define CHAR_REACTIONTIME           2
#define CHAR_AIM_ACCURACY_MG        3
#define CHAR_AIM_ACCURACY           7
#define CHAR_VIEW_FACTOR           16
#define CHAR_VIEW_MAXCHANGE        17
#define CHAR_MOVEMENT_SKILL        19
#define CHAR_ALERTNESS             36
#define CHAR_CAMPER                37
#define CHAR_JUMPER                38
#define CHAR_FIRETHROTTLE          39
#define CHAR_CROUCHER              44
#define CHAR_WALKER                45
#define CHAR_WEAPONJUMPING         46
#define BOT_CHAR_MAX               50

typedef struct bot_character_s {
    char name[MAX_NAME_LENGTH];
    char filename[MAX_QPATH];
    int skill_level;
    
    // Character attributes (0-1 range)
    float characteristics[BOT_CHAR_MAX];
    
    // Derived attributes
    float aggression;
    float accuracy;
    float reaction_time;
    float movement_skill;
    float camping;
    float alertness;
    
    // Valid flag
    qboolean valid;
} bot_character_t;

// Character loading functions
void BotChar_Init(void);
void BotChar_Shutdown(void);
bot_character_t *BotChar_LoadCharacter(const char *charname, int skill);
void BotChar_FreeCharacter(bot_character_t *character);
float BotChar_GetFloat(bot_character_t *character, int index);
int BotChar_GetInt(bot_character_t *character, int index);
const char *BotChar_GetString(bot_character_t *character, int index);

// Default character profiles
bot_character_t *BotChar_GetDefaultCharacter(int skill);
void BotChar_CreateDefaultProfiles(void);

#endif // BOT_CHARACTER_H

