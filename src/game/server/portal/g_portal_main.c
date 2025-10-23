/*
===========================================================================
Portal System - Main Game Module Integration
Provides the main entry point and initialization for the portal system
===========================================================================
*/

#include "g_portal.h"
#include "../../../engine/common/q_shared.h"
#include "../../api/g_public.h"

// External declarations from syscalls
extern void trap_LocateGameData( gentity_t *gEnts, int numGEntities, int sizeofGEntity_t,
                                playerState_t *clients, int sizeofGClient_t );
extern void trap_Cvar_Register( vmCvar_t *cvar, const char *var_name, const char *value, int flags );
extern void trap_Cvar_Update( vmCvar_t *cvar );
extern void trap_SendServerCommand( int clientNum, const char *text );
extern void trap_SetConfigstring( int num, const char *string );

// Global entity storage
gentity_t g_entities[MAX_GENTITIES];
level_locals_t level;
gclient_t g_clients[MAX_CLIENTS];

// Portal system cvars
vmCvar_t g_portalDebug;
vmCvar_t g_portalRadius;
vmCvar_t g_portalSpeed;
vmCvar_t g_portalMaxRange;
vmCvar_t g_portalActivationTime;
vmCvar_t g_portalFallDamageImmunity;

// Forward declarations
void G_RegisterCvars( void );
void G_InitGame( int levelTime, int randomSeed, int restart );
void G_RunFrame( int levelTime );
void G_ShutdownGame( int restart );

/*
================
dllEntry

Entry point from engine
================
*/
extern void dllEntry( intptr_t (QDECL *syscallptr)( intptr_t arg,... ) );

/*
================
vmMain

This is the main entry point for the game module
It's called by the engine for various game events
================
*/
Q_EXPORT intptr_t vmMain( int command, int arg0, int arg1, int arg2, int arg3, int arg4, 
                         int arg5, int arg6, int arg7, int arg8, int arg9, int arg10, int arg11 ) {
    switch ( command ) {
    case GAME_INIT:
        G_InitGame( arg0, arg1, arg2 );
        return 0;
        
    case GAME_SHUTDOWN:
        G_ShutdownGame( arg0 );
        return 0;
        
    case GAME_CLIENT_CONNECT:
        return (intptr_t)ClientConnect( arg0, arg1, arg2 );
        
    case GAME_CLIENT_BEGIN:
        ClientBegin( arg0 );
        return 0;
        
    case GAME_CLIENT_USERINFO_CHANGED:
        ClientUserinfoChanged( arg0 );
        return 0;
        
    case GAME_CLIENT_DISCONNECT:
        ClientDisconnect( arg0 );
        return 0;
        
    case GAME_CLIENT_COMMAND:
        ClientCommand( arg0 );
        return 0;
        
    case GAME_CLIENT_THINK:
        ClientThink( arg0 );
        return 0;
        
    case GAME_RUN_FRAME:
        G_RunFrame( arg0 );
        return 0;
        
    case GAME_CONSOLE_COMMAND:
        return ConsoleCommand();
        
    default:
        return -1;
    }
}

/*
================
G_RegisterCvars

Register portal system cvars
================
*/
void G_RegisterCvars( void ) {
    trap_Cvar_Register( &g_portalDebug, "g_portalDebug", "0", CVAR_SERVERINFO );
    trap_Cvar_Register( &g_portalRadius, "g_portalRadius", "32", CVAR_SERVERINFO | CVAR_LATCH );
    trap_Cvar_Register( &g_portalSpeed, "g_portalSpeed", "400", CVAR_SERVERINFO );
    trap_Cvar_Register( &g_portalMaxRange, "g_portalMaxRange", "4096", CVAR_SERVERINFO );
    trap_Cvar_Register( &g_portalActivationTime, "g_portalActivationTime", "1000", CVAR_SERVERINFO );
    trap_Cvar_Register( &g_portalFallDamageImmunity, "g_portalFallDamageImmunity", "3000", CVAR_SERVERINFO );
}

/*
================
G_InitGame

Initialize the game module
================
*/
void G_InitGame( int levelTime, int randomSeed, int restart ) {
    int i;
    
    G_Printf( "------- Game Initialization -------\n" );
    G_Printf( "gamename: %s\n", GAMEVERSION );
    G_Printf( "gamedate: %s\n", __DATE__ );
    
    srand( randomSeed );
    
    // Register cvars
    G_RegisterCvars();
    
    // Clear all entities
    memset( g_entities, 0, sizeof(g_entities) );
    memset( g_clients, 0, sizeof(g_clients) );
    memset( &level, 0, sizeof(level) );
    
    // Set up entity pointers
    for ( i = 0; i < MAX_GENTITIES; i++ ) {
        g_entities[i].s.number = i;
    }
    
    // Connect clients to entities
    for ( i = 0; i < MAX_CLIENTS; i++ ) {
        g_entities[i].client = &g_clients[i];
    }
    
    // Let the server know about our data
    trap_LocateGameData( g_entities, MAX_GENTITIES, sizeof(gentity_t),
                        &g_clients[0].ps, sizeof(gclient_t) );
    
    // Initialize level
    level.time = levelTime;
    level.startTime = levelTime;
    
    // Initialize portal system
    G_InitPortalSystem();
    
    G_Printf( "------- Game Initialization Complete -------\n" );
}

/*
================
G_ShutdownGame

Shutdown the game module
================
*/
void G_ShutdownGame( int restart ) {
    G_Printf( "==== ShutdownGame ====\n" );
    
    // Shutdown portal system
    G_ShutdownPortalSystem();
    
    // Write session data and perform cleanup
    // This would normally save persistent data
}

/*
================
G_RunFrame

Run one frame of the game
================
*/
void G_RunFrame( int levelTime ) {
    int i;
    gentity_t *ent;
    
    // Update level time
    level.previousTime = level.time;
    level.time = levelTime;
    level.framenum++;
    
    // Update cvars
    trap_Cvar_Update( &g_portalDebug );
    trap_Cvar_Update( &g_portalRadius );
    trap_Cvar_Update( &g_portalSpeed );
    trap_Cvar_Update( &g_portalMaxRange );
    trap_Cvar_Update( &g_portalActivationTime );
    trap_Cvar_Update( &g_portalFallDamageImmunity );
    
    // Run entity thinks
    for ( i = 0, ent = &g_entities[0]; i < MAX_GENTITIES; i++, ent++ ) {
        if ( !ent->inuse ) {
            continue;
        }
        
        // Clear events
        ent->s.event = 0;
        
        // Check for removal
        if ( ent->freeAfterEvent ) {
            G_FreeEntity( ent );
            continue;
        }
        
        // Run think function
        if ( ent->think && ent->nextthink && ent->nextthink <= level.time ) {
            ent->nextthink = 0;
            ent->think( ent );
        }
    }
    
    // Update portal system
    G_UpdatePortalSystem();
    
    // Run client frames
    for ( i = 0; i < MAX_CLIENTS; i++ ) {
        if ( level.clients[i].pers.connected == CON_CONNECTED ) {
            ClientThink( i );
        }
    }
}

/*
================
G_Spawn

Allocate a new entity
================
*/
gentity_t *G_Spawn( void ) {
    int i;
    gentity_t *e;
    
    e = NULL;
    for ( i = MAX_CLIENTS; i < MAX_GENTITIES; i++ ) {
        e = &g_entities[i];
        if ( !e->inuse ) {
            break;
        }
    }
    
    if ( i == MAX_GENTITIES ) {
        G_Error( "G_Spawn: no free entities" );
        return NULL;
    }
    
    // Initialize the entity
    memset( e, 0, sizeof(*e) );
    e->inuse = qtrue;
    e->s.number = i;
    e->r.ownerNum = ENTITYNUM_NONE;
    e->classname = "noclass";
    e->s.eType = ET_GENERAL;
    e->s.eFlags = 0;
    
    return e;
}

/*
================
G_FreeEntity

Mark an entity as free
================
*/
void G_FreeEntity( gentity_t *ent ) {
    if ( !ent || !ent->inuse ) {
        return;
    }
    
    trap_UnlinkEntity( ent );
    
    memset( ent, 0, sizeof(*ent) );
    ent->classname = "freed";
    ent->freetime = level.time;
    ent->inuse = qfalse;
}

/*
================
G_TempEntity

Spawn a temporary entity
================
*/
gentity_t *G_TempEntity( vec3_t origin, int event ) {
    gentity_t *e;
    vec3_t snapped;
    
    e = G_Spawn();
    e->s.eType = ET_EVENTS + event;
    
    e->classname = "tempEntity";
    e->eventTime = level.time;
    e->freeAfterEvent = qtrue;
    
    VectorCopy( origin, snapped );
    SnapVector( snapped );
    G_SetOrigin( e, snapped );
    
    trap_LinkEntity( e );
    
    return e;
}

/*
================
G_SetOrigin

Set entity origin and update bounds
================
*/
void G_SetOrigin( gentity_t *ent, vec3_t origin ) {
    VectorCopy( origin, ent->s.pos.trBase );
    ent->s.pos.trType = TR_STATIONARY;
    ent->s.pos.trTime = 0;
    ent->s.pos.trDuration = 0;
    VectorClear( ent->s.pos.trDelta );
    
    VectorCopy( origin, ent->r.currentOrigin );
    VectorCopy( origin, ent->s.origin );
}

/*
================
ClientConnect

Called when a client connects
================
*/
char *ClientConnect( int clientNum, qboolean firstTime, qboolean isBot ) {
    gclient_t *client;
    gentity_t *ent;
    
    ent = &g_entities[clientNum];
    client = &g_clients[clientNum];
    
    memset( client, 0, sizeof(*client) );
    
    client->pers.connected = CON_CONNECTING;
    client->pers.enterTime = level.time;
    client->pers.teamState.state = TEAM_BEGIN;
    
    // Initialize portal state for this client
    if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
        g_playerPortalStates[clientNum].activeOrangePortal = -1;
        g_playerPortalStates[clientNum].activeBluePortal = -1;
        g_playerPortalStates[clientNum].lastPortalExitTime = 0;
        g_playerPortalStates[clientNum].fallDamageImmunityEndTime = 0;
    }
    
    return NULL;
}

/*
================
ClientBegin

Called when a client is ready to play
================
*/
void ClientBegin( int clientNum ) {
    gentity_t *ent;
    gclient_t *client;
    
    ent = &g_entities[clientNum];
    client = &g_clients[clientNum];
    
    client->pers.connected = CON_CONNECTED;
    client->ps.clientNum = clientNum;
    
    ent->client = client;
    ent->inuse = qtrue;
    ent->classname = "player";
    ent->s.eType = ET_PLAYER;
    ent->s.number = clientNum;
    
    VectorSet( ent->r.mins, -15, -15, -24 );
    VectorSet( ent->r.maxs, 15, 15, 32 );
    
    trap_SendServerCommand( -1, va("print \"%s entered the game\n\"", 
                                   client->pers.netname) );
}

/*
================
ClientDisconnect

Called when a client disconnects
================
*/
void ClientDisconnect( int clientNum ) {
    gentity_t *ent;
    gclient_t *client;
    
    ent = &g_entities[clientNum];
    client = &g_clients[clientNum];
    
    if ( client->pers.connected == CON_CONNECTED ) {
        // Close any active portals
        G_ClosePlayerPortals( ent );
        
        trap_SendServerCommand( -1, va("print \"%s left the game\n\"", 
                                       client->pers.netname) );
    }
    
    trap_UnlinkEntity( ent );
    ent->inuse = qfalse;
    ent->classname = "disconnected";
    ent->client = NULL;
    
    memset( client, 0, sizeof(*client) );
}

/*
================
ClientThink

Called for each client frame
================
*/
void ClientThink( int clientNum ) {
    gentity_t *ent;
    gclient_t *client;
    usercmd_t ucmd;
    
    ent = &g_entities[clientNum];
    client = ent->client;
    
    if ( !client || client->pers.connected != CON_CONNECTED ) {
        return;
    }
    
    // Get current user command
    trap_GetUsercmd( clientNum, &ucmd );
    
    // Process portal commands
    G_ProcessPortalCommands( ent, &ucmd );
    
    // Update client view angles
    client->ps.commandTime = ucmd.serverTime;
    VectorCopy( ucmd.angles, client->ps.viewangles );
}

/*
================
ClientUserinfoChanged

Called when client userinfo changes
================
*/
void ClientUserinfoChanged( int clientNum ) {
    gentity_t *ent;
    gclient_t *client;
    char userinfo[MAX_INFO_STRING];
    char *s;
    
    ent = &g_entities[clientNum];
    client = ent->client;
    
    trap_GetUserinfo( clientNum, userinfo, sizeof(userinfo) );
    
    // Extract name
    s = Info_ValueForKey( userinfo, "name" );
    Q_strncpyz( client->pers.netname, s, sizeof(client->pers.netname) );
}

/*
================
ClientCommand

Process client commands
================
*/
void ClientCommand( int clientNum ) {
    gentity_t *ent;
    char cmd[MAX_TOKEN_CHARS];
    
    ent = &g_entities[clientNum];
    if ( !ent->client || ent->client->pers.connected != CON_CONNECTED ) {
        return;
    }
    
    trap_Argv( 0, cmd, sizeof(cmd) );
    
    // Handle portal commands
    if ( !Q_stricmp( cmd, "fireportal" ) ) {
        char arg[MAX_TOKEN_CHARS];
        trap_Argv( 1, arg, sizeof(arg) );
        
        if ( !Q_stricmp( arg, "orange" ) ) {
            G_FirePortalFixed( ent, PORTAL_ORANGE );
        } else if ( !Q_stricmp( arg, "blue" ) ) {
            G_FirePortalFixed( ent, PORTAL_BLUE );
        }
        return;
    }
    
    if ( !Q_stricmp( cmd, "closeportals" ) ) {
        G_ClosePlayerPortals( ent );
        return;
    }
    
    // Unknown command
    trap_SendServerCommand( clientNum, va("print \"Unknown command: %s\n\"", cmd) );
}

/*
================
ConsoleCommand

Process server console commands
================
*/
qboolean ConsoleCommand( void ) {
    char cmd[MAX_TOKEN_CHARS];
    
    trap_Argv( 0, cmd, sizeof(cmd) );
    
    if ( !Q_stricmp( cmd, "portaldebug" ) ) {
        G_DebugPortalSystem();
        return qtrue;
    }
    
    if ( !Q_stricmp( cmd, "portalstats" ) ) {
        G_PrintPortalStats();
        return qtrue;
    }
    
    return qfalse;
}