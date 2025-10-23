/*
===========================================================================
Portal System - Local Game Definitions
Defines the game structures needed by the portal system
===========================================================================
*/

#ifndef G_LOCAL_H
#define G_LOCAL_H

#include "../../../engine/common/q_shared.h"
#include "../../api/g_public.h"

// Game entity structure
typedef struct gentity_s {
    entityState_t    s;              // communicated by server to clients
    entityShared_t   r;              // shared by both the server system and game
    
    // DO NOT MODIFY ANYTHING ABOVE THIS, THE SERVER
    // EXPECTS THE FIELDS IN THAT ORDER!
    
    struct gclient_s *client;        // NULL if not a client
    
    qboolean         inuse;
    
    char             *classname;
    int              spawnflags;
    
    qboolean         neverFree;      // if true, FreeEntity will only unlink
    int              flags;           // FL_* variables
    
    char             *model;
    char             *model2;
    int              freetime;       // level.time when the object was freed
    
    int              eventTime;      // events will be cleared EVENT_VALID_MSEC after set
    qboolean         freeAfterEvent;
    qboolean         unlinkAfterEvent;
    
    qboolean         physicsObject;  // if true, it can be pushed by movers and fall off edges
    float            physicsBounce;  // 1.0 = continuous bounce, 0.0 = no bounce
    int              clipmask;       // brushes with this content value will be collided against
    
    // movers
    char             *target;
    char             *targetname;
    char             *team;
    char             *targetShaderName;
    char             *targetShaderNewName;
    struct gentity_s *target_ent;
    
    float            speed;
    vec3_t           movedir;
    
    int              nextthink;
    void             (*think)(struct gentity_s *self);
    void             (*reached)(struct gentity_s *self);
    void             (*blocked)(struct gentity_s *self, struct gentity_s *other);
    void             (*touch)(struct gentity_s *self, struct gentity_s *other, trace_t *trace);
    void             (*use)(struct gentity_s *self, struct gentity_s *other, struct gentity_s *activator);
    void             (*pain)(struct gentity_s *self, struct gentity_s *attacker, int damage);
    void             (*die)(struct gentity_s *self, struct gentity_s *inflictor, struct gentity_s *attacker, int damage, int mod);
    
    int              pain_debounce_time;
    int              fly_sound_debounce_time;  // wind tunnel
    int              last_move_time;
    
    int              health;
    int              takedamage;
    
    int              damage;
    int              splashDamage;   // quad will increase this without increasing radius
    int              splashRadius;
    int              methodOfDeath;
    int              splashMethodOfDeath;
    
    int              count;
    
    struct gentity_s *chain;
    struct gentity_s *enemy;
    struct gentity_s *activator;
    struct gentity_s *teamchain;     // next entity in team
    struct gentity_s *teammaster;    // master of the team
    
    int              watertype;
    int              waterlevel;
    
    int              noise_index;
    
    float            wait;
    float            random;
    
    int              genericValue1;
    int              genericValue2;
    int              genericValue3;
    
    char             *message;
    
    struct gentity_s *parent;
    
} gentity_t;

// Client persistent data
typedef struct {
    clientConnected_t connected;
    usercmd_t         cmd;           // we would lose angles if not persistant
    qboolean          localClient;   // true if "localhost" is the client
    qboolean          predictItemPickup; // based on cg_predictItems userinfo
    char              netname[MAX_NETNAME];
    int               enterTime;     // level.time the client entered the game
    int               connectTime;   // level.time the client first connected
    playerTeamState_t teamState;     // status in teamplay games
    int               voteCount;     // to prevent people from constantly calling votes
    int               teamVoteCount; // to prevent people from constantly calling votes
    qboolean          teamInfo;      // send team overlay updates?
} clientPersistant_t;

// Client session data
typedef struct {
    team_t            sessionTeam;
    int               spectatorTime; // for determining next-in-line to play
    spectatorState_t  spectatorState;
    int               spectatorClient; // for chasecam and follow mode
    int               wins, losses;  // tournament stats
    qboolean          ghost;         // ghost mode
    qboolean          teamLeader;    // true when this is a team leader
} clientSession_t;

// Client structure
typedef struct gclient_s {
    // ps MUST be the first element, because the server expects it
    playerState_t    ps;             // communicated by server to clients
    
    // the rest of the structure is private to game
    clientPersistant_t pers;
    clientSession_t  sess;
    
    qboolean         readyToExit;    // wishes to leave the intermission
    
    qboolean         noclip;
    
    int              lastCmdTime;    // level.time of last usercmd_t, for EF_CONNECTION
    int              buttons;
    int              oldbuttons;
    int              latched_buttons;
    
    vec3_t           oldOrigin;
    
    // damage feedback
    int              damage_armor;   // damage taken out of armor
    int              damage_blood;   // damage taken out of health
    int              damage_knockback; // impact damage
    vec3_t           damage_from;    // origin for vector calculation
    qboolean         damage_fromWorld; // if true, don't use the damage_from vector
    
    int              accurateCount;  // for "impressive" reward sound
    
    int              accuracy_shots;  // total number of shots
    int              accuracy_hits;   // total number of hits
    
    //
    int              lastkilled_client; // last client that this client killed
    int              lasthurt_client;  // last client that damaged this client
    int              lasthurt_mod;     // type of damage the client did
    
    // timers
    int              respawnTime;    // can respawn when time > this, force after g_forcerespwan
    int              inactivityTime;  // kick players when time > this
    qboolean         inactivityWarning; // qtrue if the five second warning has been given
    int              rewardTime;     // clear the EF_AWARD_IMPRESSIVE, etc when time > this
    
    int              airOutTime;
    
    int              lastKillTime;
    qboolean         fireHeld;       // used for hook
    gentity_t        *hook;           // grapple hook if out
    
    int              switchTeamTime;  // time the player switched teams
    
    // timeResidual is used to handle events that happen every second
    // like health / armor countdowns and regeneration
    int              timeResidual;
    
    char             *areabits;
    
} gclient_t;

// Level locals
typedef struct {
    int              framenum;
    int              time;           // in msec
    int              previousTime;   // so movers can back up when blocked
    
    int              startTime;      // level.time the map was started
    
    int              teamScores[TEAM_NUM_TEAMS];
    int              lastTeamLocationTime;  // last time of client team location update
    
    qboolean         newSession;     // don't use any old session data, because
                                     // we changed gametype
    
    qboolean         restarted;      // waiting for a map_restart to fire
    
    int              numConnectedClients;
    int              numNonSpectatorClients; // includes connecting clients
    int              numPlayingClients; // connected, non-spectators
    int              sortedClients[MAX_CLIENTS]; // sorted by score
    int              follow1, follow2; // clientNums for auto-follow spectators
    
    int              snd_fry;        // sound index for standing in lava
    
    int              warmupTime;     // restart match at this time
    
    int              score1, score2; // from configstrings
    int              redflag, blueflag; // flagstatus from configstrings
    int              flagStatus;
    
    qboolean         intermissionQueued; // intermission was qualified, but
                                         // wait INTERMISSION_DELAY_TIME before
                                         // actually going there so the last
                                         // frag can be watched.  Disable future
                                         // kills during this delay
    int              intermissiontime; // time the intermission was started
    char             *changemap;
    qboolean         readyToExit;    // at least one client wants to exit
    int              exitTime;
    vec3_t           intermission_origin; // also used for spectator spawns
    vec3_t           intermission_angle;
    
    qboolean         locationLinked; // target_locations get linked
    gentity_t        *locationHead;  // head of the location list
    int              bodyQueIndex;   // dead bodies
    gentity_t        *bodyQue[BODY_QUEUE_SIZE];
    
    int              portalSequence;
    
    struct gclient_s *clients;       // [maxclients]
    
} level_locals_t;

// Entity event types
typedef enum {
    ET_GENERAL,
    ET_PLAYER,
    ET_ITEM,
    ET_MISSILE,
    ET_MOVER,
    ET_BEAM,
    ET_PORTAL,
    ET_SPEAKER,
    ET_PUSH_TRIGGER,
    ET_TELEPORT_TRIGGER,
    ET_INVISIBLE,
    ET_GRAPPLE,
    ET_TEAM,
    ET_EVENTS
} entityType_t;

// Game version
#define GAMEVERSION "quake3e_portal"

// Body queue size
#define BODY_QUEUE_SIZE 8

// Connection states
typedef enum {
    CON_DISCONNECTED,
    CON_CONNECTING,
    CON_CONNECTED
} clientConnected_t;

// Spectator states
typedef enum {
    SPECTATOR_NOT,
    SPECTATOR_FREE,
    SPECTATOR_FOLLOW,
    SPECTATOR_SCOREBOARD
} spectatorState_t;

// Player team state
typedef struct {
    playerTeamStateState_t state;
    
    int          location;
    
    int          captures;
    int          basedefense;
    int          carrierdefense;
    int          flagrecovery;
    int          fragcarrier;
    int          assists;
    
    float        lasthurtcarrier;
    float        lastreturnedflag;
    float        flagsince;
    float        lastfraggedcarrier;
} playerTeamState_t;

// Player team state states
typedef enum {
    TEAM_BEGIN,
    TEAM_ACTIVE
} playerTeamStateState_t;

// Weapon indices
typedef enum {
    WP_NONE,
    WP_GAUNTLET,
    WP_MACHINEGUN,
    WP_SHOTGUN,
    WP_GRENADE_LAUNCHER,
    WP_ROCKET_LAUNCHER,
    WP_LIGHTNING,
    WP_RAILGUN,
    WP_PLASMAGUN,
    WP_BFG,
    WP_GRAPPLING_HOOK,
    WP_NUM_WEAPONS
} weapon_t;

// Means of death
typedef enum {
    MOD_UNKNOWN,
    MOD_SHOTGUN,
    MOD_GAUNTLET,
    MOD_MACHINEGUN,
    MOD_GRENADE,
    MOD_GRENADE_SPLASH,
    MOD_ROCKET,
    MOD_ROCKET_SPLASH,
    MOD_PLASMA,
    MOD_PLASMA_SPLASH,
    MOD_RAILGUN,
    MOD_LIGHTNING,
    MOD_BFG,
    MOD_BFG_SPLASH,
    MOD_WATER,
    MOD_SLIME,
    MOD_LAVA,
    MOD_CRUSH,
    MOD_TELEFRAG,
    MOD_FALLING,
    MOD_SUICIDE,
    MOD_TARGET_LASER,
    MOD_TRIGGER_HURT,
    MOD_GRAPPLE
} meansOfDeath_t;

#endif // G_LOCAL_H