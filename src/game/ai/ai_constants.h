/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

AI System Constants
===========================================================================
*/

#ifndef AI_CONSTANTS_H
#define AI_CONSTANTS_H

// Perception constants
#define MAX_HEARING_RANGE           1024.0f
#define SOUND_MEMORY_TIME           5000    // milliseconds
#define SOUND_AMBIENT               0
#define SOUND_COMBAT                1
#define SOUND_FOOTSTEP              2
#define SOUND_ITEM_PICKUP           3
#define SOUND_WEAPON_FIRE           4

// Cover system constants
#define MAX_COVER_CONNECTIONS       8
#define QUALITY_EXCELLENT           5
#define QUALITY_GOOD                4
#define QUALITY_AVERAGE             3
#define QUALITY_POOR                2
#define QUALITY_TERRIBLE            1

// Threat level constants
#define THREAT_NONE                 0
#define THREAT_LOW                  1
#define THREAT_MEDIUM               2
#define THREAT_HIGH                 3
#define THREAT_CRITICAL             4

// Planning constants
#define PLAN_MAX_AGE                30000   // 30 seconds

// Team constants
#define MAX_SQUAD_SIZE              8
#define MAX_FORMATIONS              16

// Navigation constants
#define NAV_MAX_WAYPOINTS           256
#define NAV_MAX_PATH_LENGTH         64

#endif // AI_CONSTANTS_H