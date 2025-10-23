/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Game Entity Definitions for AI System
Provides full entity structure definitions needed by AI modules
===========================================================================
*/

#ifndef GAME_ENTITIES_H
#define GAME_ENTITIES_H

#include "../../engine/common/q_shared.h"
#include "../shared/bg_public.h"

// Only include g_public.h if not already included
#ifndef G_PUBLIC_H
#include "../api/g_public.h"
#endif

// Define missing constants
#ifndef MAX_NETNAME
#define MAX_NETNAME 36
#endif

// Client connection state
typedef enum {
    CON_DISCONNECTED,
    CON_CONNECTING,
    CON_CONNECTED
} clientConnected_t;

// Team state
typedef enum {
    TEAM_BEGIN,
    TEAM_ACTIVE
} playerTeamStateState_t;

// Player team state
typedef struct {
    playerTeamStateState_t state;
    int location;
    int captures;
    int basedefense;
    int carrierdefense;
    int flagrecovery;
    int fragcarrier;
    int assists;
    float lasthurtcarrier;
    float lastreturnedflag;
    float flagsince;
    float lastfraggedcarrier;
} playerTeamState_t;

// Spectator state
typedef enum {
    SPECTATOR_NOT,
    SPECTATOR_FREE,
    SPECTATOR_FOLLOW,
    SPECTATOR_SCOREBOARD
} spectatorState_t;

// Client persistent data structure
typedef struct clientPersistant_s {
    clientConnected_t connected;
    usercmd_t cmd;
    qboolean localClient;
    qboolean initialSpawn;
    qboolean predictItemPickup;
    qboolean pmoveFixed;
    char netname[MAX_NETNAME];
    int maxHealth;
    int enterTime;
    playerTeamState_t teamState;
    int voteCount;
    int teamVoteCount;
    qboolean teamInfo;
} clientPersistant_t;

// Client session data structure
typedef struct clientSession_s {
    team_t sessionTeam;
    int spectatorTime;
    spectatorState_t spectatorState;
    int spectatorClient;
    int wins, losses;
    qboolean teamLeader;
} clientSession_t;

// Complete gentity_t definition
typedef struct gentity_s {
    entityState_t s;
    entityShared_t r;
    
    struct gclient_s *client;
    qboolean inuse;
    char *classname;
    int spawnflags;
    qboolean neverFree;
    int flags;
    char *model;
    char *model2;
    int freetime;
    int eventTime;
    qboolean freeAfterEvent;
    qboolean unlinkAfterEvent;
    qboolean physicsObject;
    float physicsBounce;
    int clipmask;
    char *target;
    char *targetname;
    char *team;
    char *targetShaderName;
    char *targetShaderNewName;
    struct gentity_s *target_ent;
    float speed;
    vec3_t movedir;
    int nextthink;
    void (*think)(struct gentity_s *ent);
    void (*reached)(struct gentity_s *ent);
    void (*blocked)(struct gentity_s *ent, struct gentity_s *other);
    void (*touch)(struct gentity_s *self, struct gentity_s *other, trace_t *trace);
    void (*use)(struct gentity_s *self, struct gentity_s *other, struct gentity_s *activator);
    void (*pain)(struct gentity_s *self, struct gentity_s *attacker, int damage);
    void (*die)(struct gentity_s *self, struct gentity_s *inflictor, struct gentity_s *attacker, int damage, int mod);
    int pain_debounce_time;
    int fly_sound_debounce_time;
    int last_move_time;
    int health;
    int takedamage;
    int damage;
    int splashDamage;
    int splashRadius;
    int methodOfDeath;
    int splashMethodOfDeath;
    int count;
    struct gentity_s *chain;
    struct gentity_s *enemy;
    struct gentity_s *activator;
    struct gentity_s *teamchain;
    struct gentity_s *teammaster;
    int watertype;
    int waterlevel;
    int noise_index;
    float wait;
    float random;
    gitem_t *item;
    int genericValue1;
    int genericValue2;
    int genericValue3;
    char *message;
    struct gentity_s *parent;
} gentity_t;

// Complete gclient_t definition
typedef struct gclient_s {
    playerState_t ps;
    clientPersistant_t pers;
    clientSession_t sess;
    int ping;
    int lastCmdTime;
    int buttons;
    int oldbuttons;
    int latched_buttons;
    vec3_t oldOrigin;
    int damage_armor;
    int damage_blood;
    int damage_knockback;
    vec3_t damage_from;
    qboolean damage_fromWorld;
    int accurateCount;
    int accuracy_shots;
    int accuracy_hits;
    int lastkilled_client;
    int lasthurt_client;
    int lasthurt_mod;
    int respawnTime;
    int inactivityTime;
    qboolean inactivityWarning;
    int rewardTime;
    int airOutTime;
    int lastKillTime;
    qboolean fireHeld;
    gentity_t *hook;
    int switchTeamTime;
    int switchClassTime;
    int timeResidual;
    char *areabits;
} gclient_t;

// Level locals structure
typedef struct level_locals_s {
    int time;
    int previousTime;
    int framenum;
    int startTime;
    int clientConnected[MAX_CLIENTS];
    int maxclients;
    int warmupTime;
    int matchTime;
    int restartTime;
    int numConnectedClients;
    int sortedClients[MAX_CLIENTS];
    int follow1, follow2;
    int snd_fry;
    qboolean locationLinked;
    gentity_t *locationHead;
    int bodyQueIndex;
    gentity_t *bodyQue[8];
    int portalSequence;
} level_locals_t;

// External globals
extern level_locals_t level;
extern gentity_t *g_entities;

// Entity access macros
#define ENTITYNUM_NONE      (MAX_GENTITIES-1)
#define ENTITYNUM_WORLD     (MAX_GENTITIES-2)
#define ENTITYNUM_MAX_NORMAL (MAX_GENTITIES-2)

// Player state stats
#ifndef STAT_HEALTH
#define STAT_HEALTH         0
#define STAT_HOLDABLE_ITEM  1
#define STAT_WEAPONS        2
#define STAT_ARMOR          3
#define STAT_DEAD_YAW       4
#define STAT_CLIENTS_READY  5
#define STAT_MAX_HEALTH     6
#endif


// Content masks
#ifndef MASK_ALL
#define MASK_ALL            (-1)
#define MASK_SOLID          (CONTENTS_SOLID)
#define MASK_PLAYERSOLID    (CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_BODY)
#define MASK_DEADSOLID      (CONTENTS_SOLID|CONTENTS_PLAYERCLIP)
#define MASK_WATER          (CONTENTS_WATER|CONTENTS_LAVA|CONTENTS_SLIME)
#define MASK_OPAQUE         (CONTENTS_SOLID|CONTENTS_SLIME|CONTENTS_LAVA)
#define MASK_SHOT           (CONTENTS_SOLID|CONTENTS_BODY|CONTENTS_CORPSE)
#endif

// Surface flags
#ifndef SURF_SOLID
#define SURF_SOLID      0x1
#endif

// Entity types are defined in perception/ai_perception.h

// Goal types are defined in ai_main.h

// Combat stances
typedef enum {
    COMBAT_STANCE_NONE,
    COMBAT_STANCE_AGGRESSIVE,
    COMBAT_STANCE_DEFENSIVE,
    COMBAT_STANCE_TACTICAL,
    COMBAT_STANCE_EVASIVE
} combat_stance_t;

// Team strategies
typedef enum {
    TEAM_STRATEGY_NONE,
    TEAM_STRATEGY_ATTACK,
    TEAM_STRATEGY_DEFEND,
    TEAM_STRATEGY_CAPTURE,
    TEAM_STRATEGY_ESCORT,
    TEAM_STRATEGY_FLANK
} team_strategy_t;

// Team roles
typedef enum {
    ROLE_NONE,
    ROLE_LEADER,
    ROLE_ASSAULT,
    ROLE_SUPPORT,
    ROLE_SCOUT,
    ROLE_DEFENDER,
    ROLE_SNIPER
} team_role_t;

// Memory decay constants
#define MEMORY_DECAY_TIME   10.0f
#define MAX_MEMORY_ENTRIES  64
#define MAX_THREATS         16

// AI Debug cvar
extern cvar_t *ai_debug;

#endif // GAME_ENTITIES_H