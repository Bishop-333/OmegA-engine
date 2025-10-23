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

#include "bot_character.h"
#include "../../../engine/core/qcommon.h"

// External function declarations - now provided by qcommon.h
extern float Q_bound(float min, float val, float max);

#include <stdlib.h>
#include <string.h>

// Default character templates for different skill levels
static bot_character_t default_characters[5];
static qboolean initialized = qfalse;

/*
==================
BotChar_Init
==================
*/
void BotChar_Init(void) {
    if (initialized) {
        return;
    }
    
    Com_Printf("Initializing bot character system...\n");
    
    // Create default character profiles for each skill level
    BotChar_CreateDefaultProfiles();
    
    initialized = qtrue;
}

/*
==================
BotChar_Shutdown
==================
*/
void BotChar_Shutdown(void) {
    if (!initialized) {
        return;
    }
    
    initialized = qfalse;
}

/*
==================
BotChar_CreateDefaultProfiles
Creates default character profiles for different skill levels
==================
*/
void BotChar_CreateDefaultProfiles(void) {
    int i;
    
    for (i = 0; i < 5; i++) {
        bot_character_t *ch = &default_characters[i];
        float skill_factor = (i + 1) / 5.0f;
        
        memset(ch, 0, sizeof(bot_character_t));
        Com_sprintf(ch->name, sizeof(ch->name), "default_skill%d", i + 1);
        Com_sprintf(ch->filename, sizeof(ch->filename), "default");
        ch->skill_level = i + 1;
        ch->valid = qtrue;
        
        // Set default characteristics based on skill level
        ch->characteristics[CHAR_ATTACK_SKILL] = 0.3f + skill_factor * 0.6f;
        ch->characteristics[CHAR_REACTIONTIME] = 1.0f - skill_factor * 0.7f;
        ch->characteristics[CHAR_AIM_ACCURACY_MG] = 0.2f + skill_factor * 0.7f;
        ch->characteristics[CHAR_AIM_ACCURACY] = 0.2f + skill_factor * 0.7f;
        ch->characteristics[CHAR_VIEW_FACTOR] = 0.5f + skill_factor * 0.4f;
        ch->characteristics[CHAR_VIEW_MAXCHANGE] = 0.3f + skill_factor * 0.5f;
        ch->characteristics[CHAR_MOVEMENT_SKILL] = 0.3f + skill_factor * 0.6f;
        ch->characteristics[CHAR_ALERTNESS] = 0.3f + skill_factor * 0.6f;
        ch->characteristics[CHAR_CAMPER] = 0.5f - skill_factor * 0.2f;
        ch->characteristics[CHAR_JUMPER] = 0.2f + skill_factor * 0.5f;
        ch->characteristics[CHAR_FIRETHROTTLE] = 0.7f - skill_factor * 0.3f;
        ch->characteristics[CHAR_CROUCHER] = 0.1f + skill_factor * 0.3f;
        ch->characteristics[CHAR_WALKER] = 0.3f - skill_factor * 0.2f;
        ch->characteristics[CHAR_WEAPONJUMPING] = skill_factor * 0.5f;
        
        // Set derived attributes
        ch->aggression = ch->characteristics[CHAR_ATTACK_SKILL];
        ch->accuracy = ch->characteristics[CHAR_AIM_ACCURACY];
        ch->reaction_time = ch->characteristics[CHAR_REACTIONTIME];
        ch->movement_skill = ch->characteristics[CHAR_MOVEMENT_SKILL];
        ch->camping = ch->characteristics[CHAR_CAMPER];
        ch->alertness = ch->characteristics[CHAR_ALERTNESS];
    }
}

/*
==================
BotChar_ParseCharacterFile
Parses a simple character file format
==================
*/
static qboolean BotChar_ParseCharacterFile(bot_character_t *character, const char *filename) {
    fileHandle_t f;
    int len;
    char *buf, *text, *token;
    char fullpath[MAX_QPATH];
    qboolean in_skill_block = qfalse;
    int target_skill = character->skill_level;
    
    // Try different paths for the character file
    Com_sprintf(fullpath, sizeof(fullpath), "botfiles/bots/%s_c.c", filename);
    len = FS_FOpenFileByMode(fullpath, &f, FS_READ);
    
    if (len <= 0) {
        Com_sprintf(fullpath, sizeof(fullpath), "botfiles/bots/%s.c", filename);
        len = FS_FOpenFileByMode(fullpath, &f, FS_READ);
    }
    
    if (len <= 0) {
        Com_sprintf(fullpath, sizeof(fullpath), "bots/%s_c.c", filename);
        len = FS_FOpenFileByMode(fullpath, &f, FS_READ);
    }
    
    if (len <= 0) {
        return qfalse;
    }
    
    buf = (char *)Z_Malloc(len + 1);
    FS_Read(buf, len, f);
    buf[len] = 0;
    FS_FCloseFile(f);
    
    // Simple parser for character files
    text = buf;
    while ((token = COM_ParseExt(&text, qtrue)) && token[0]) {
        if (!Q_stricmp(token, "skill")) {
            token = COM_ParseExt(&text, qfalse);
            if (token[0]) {
                int skill = atoi(token);
                // Check if this is the skill block we want
                if (skill == target_skill || target_skill < 0) {
                    in_skill_block = qtrue;
                    character->skill_level = skill;
                    
                    // Skip the opening brace
                    token = COM_ParseExt(&text, qtrue);
                    if (token[0] != '{') {
                        Z_Free(buf);
                        return qfalse;
                    }
                } else {
                    in_skill_block = qfalse;
                    // Skip this skill block
                    int brace_count = 0;
                    token = COM_ParseExt(&text, qtrue);
                    if (token[0] == '{') brace_count++;
                    
                    while (brace_count > 0 && (token = COM_ParseExt(&text, qtrue)) && token[0]) {
                        if (token[0] == '{') brace_count++;
                        else if (token[0] == '}') brace_count--;
                    }
                }
            }
        } else if (in_skill_block && token[0] >= '0' && token[0] <= '9') {
            // This is a characteristic index
            int index = atoi(token);
            if (index >= 0 && index < CHAR_MAX) {
                token = COM_ParseExt(&text, qfalse);
                if (token[0]) {
                    character->characteristics[index] = atof(token);
                }
            }
        } else if (in_skill_block && token[0] == '}') {
            // End of skill block
            break;
        }
    }
    
    Z_Free(buf);
    
    // Set derived attributes
    character->aggression = character->characteristics[CHAR_ATTACK_SKILL];
    character->accuracy = character->characteristics[CHAR_AIM_ACCURACY];
    character->reaction_time = character->characteristics[CHAR_REACTIONTIME];
    character->movement_skill = character->characteristics[CHAR_MOVEMENT_SKILL];
    character->camping = character->characteristics[CHAR_CAMPER];
    character->alertness = character->characteristics[CHAR_ALERTNESS];
    
    return qtrue;
}

/*
==================
BotChar_LoadCharacter
Loads a bot character from file or returns default
==================
*/
bot_character_t *BotChar_LoadCharacter(const char *charname, int skill) {
    bot_character_t *character;
    
    if (!initialized) {
        BotChar_Init();
    }
    
    // Allocate new character
    character = (bot_character_t *)Z_Malloc(sizeof(bot_character_t));
    memset(character, 0, sizeof(bot_character_t));
    
    Q_strncpyz(character->name, charname, sizeof(character->name));
    Q_strncpyz(character->filename, charname, sizeof(character->filename));
    character->skill_level = skill;
    
    // Try to load from file
    if (BotChar_ParseCharacterFile(character, charname)) {
        character->valid = qtrue;
        Com_DPrintf("Loaded character '%s' from file\n", charname);
        return character;
    }
    
    // Fall back to default character for this skill level
    if (skill >= 1 && skill <= 5) {
        memcpy(character, &default_characters[skill - 1], sizeof(bot_character_t));
        Q_strncpyz(character->name, charname, sizeof(character->name));
        character->valid = qtrue;
        Com_DPrintf("Using default character profile for '%s' at skill %d\n", charname, skill);
        return character;
    }
    
    // Use middle skill as final fallback
    memcpy(character, &default_characters[2], sizeof(bot_character_t));
    Q_strncpyz(character->name, charname, sizeof(character->name));
    character->skill_level = skill;
    character->valid = qtrue;
    
    return character;
}

/*
==================
BotChar_FreeCharacter
==================
*/
void BotChar_FreeCharacter(bot_character_t *character) {
    if (character) {
        Z_Free(character);
    }
}

/*
==================
BotChar_GetFloat
==================
*/
float BotChar_GetFloat(bot_character_t *character, int index) {
    if (!character || !character->valid || index < 0 || index >= CHAR_MAX) {
        return 0.5f; // Default middle value
    }
    
    return character->characteristics[index];
}

/*
==================
BotChar_GetInt
==================
*/
int BotChar_GetInt(bot_character_t *character, int index) {
    return (int)BotChar_GetFloat(character, index);
}

/*
==================
BotChar_GetString
==================
*/
const char *BotChar_GetString(bot_character_t *character, int index) {
    // Character files don't typically have string values
    // This is here for compatibility
    return "";
}

/*
==================
BotChar_GetDefaultCharacter
==================
*/
bot_character_t *BotChar_GetDefaultCharacter(int skill) {
    if (!initialized) {
        BotChar_Init();
    }
    
    if (skill < 1) skill = 1;
    if (skill > 5) skill = 5;
    
    return &default_characters[skill - 1];
}

