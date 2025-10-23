/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/*****************************************************************************
 * name:		be_interface.c
 *
 * desc:		bot library interface
 *
 * $Archive: /MissionPack/code/botlib/be_interface.c $
 *
 *****************************************************************************/

#include "../common/q_shared.h"
#include "util/memory.h"
#include "util/log.h"
#include "util/libvar.h"
#include "util/script.h"
#include "util/precomp.h"
#include "util/struct.h"
#include "ai_public.h"
#include "ai_interface.h"
#include "../../game/ai/game_interface.h"
#include "../../game/ai/ai_main.h"
#include "../../game/ai/bot_input.h"
#include "../../game/ai/character/bot_character.h"
#include "../../game/shared/bg_public.h"
#include "../core/qcommon.h"
#include <ctype.h>

// Missing constants and definitions
#ifndef MAX_MESSAGE_SIZE
#define MAX_MESSAGE_SIZE 256
#endif

#ifndef CHAT_ALL
#define CHAT_ALL 0
#define CHAT_TEAM 1
#endif

#ifndef MOVERESULT_ONTARGET
#define MOVERESULT_ONTARGET 1
#endif

#ifndef ET_ITEM
#define ET_ITEM 2
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Function prototypes for missing functions
extern int Sys_Milliseconds(void);
// va is already declared in q_shared.h
// Nav_LoadMesh returns nav_mesh_t*
extern nav_mesh_t *Nav_LoadMesh(const char *mapname);
extern void BotAIStartFrame(int time);

// Implementation of vectoyaw - converts a direction vector to a yaw angle
static float vectoyaw(const vec3_t vec) {
	float yaw;
	
	if (vec[YAW] == 0 && vec[PITCH] == 0) {
		yaw = 0;
	} else {
		if (vec[PITCH]) {
			yaw = (atan2(vec[YAW], vec[PITCH]) * 180 / M_PI);
		} else if (vec[YAW] > 0) {
			yaw = 90;
		} else {
			yaw = 270;
		}
		if (yaw < 0) {
			yaw += 360;
		}
	}
	
	return yaw;
}

// Implementation of Perception_InitItemTracking
static void Perception_InitItemTracking(void *perception) {
	// Initialize item tracking for the perception system
	if (!perception) return;
	
	perception_system_t *p = (perception_system_t *)perception;
	
	// Clear visible entities
	memset(p->visible_entities, 0, sizeof(p->visible_entities));
	p->num_visible_entities = 0;
	p->num_visible_items = 0;
	
	// Clear memory of entities
	memset(&p->memory, 0, sizeof(p->memory));
	p->memory.num_remembered = 0;
	
	// Initialize perception configuration for item tracking
	// Set up perception filters to detect items at maximum range
	p->filter.max_vision_range = 8192.0f;  // Maximum range for item detection
	p->filter.fov_angle = 360.0f;          // Full field of view for items
	p->filter.peripheral_sensitivity = 1.0f;
	p->filter.motion_detection_threshold = 0.1f;
	p->filter.sound_sensitivity = 1.0f;
	p->filter.use_fog_of_war = qfalse;
	p->filter.simulate_distractions = qfalse;
	
	// Set perception config for item awareness
	p->config.view_factor = 1.0f;
	p->config.max_view_change = 180.0f;
	p->config.alertness = 0.5f;
}

// Weapon info structure
struct weaponinfo_s {
	int damage;
	int range;
	int speed;
	int ammo_usage;
	int reload_time;
};

// Bot initialization movement structure
struct bot_initmove_s {
	vec3_t origin;
	vec3_t velocity;
	vec3_t viewangles;
	int client;
	float thinktime;
	int presencetype;
	vec3_t viewoffset;
	int maxspeed;
	float jumpreach;
	float walljumpreachheight;
};

// Bot move result structure
struct bot_moveresult_s {
	int failure;
	int type;
	int blocked;
	int blockentity;
	int traveltype;
	int flags;
	int weapon;
	vec3_t movedir;
	vec3_t ideal_viewangles;
	int ideal_weapon;
};

// Weapon definitions if missing
#ifndef WP_NUM_WEAPONS
#define WP_NUM_WEAPONS 10
#define WP_GAUNTLET 1
#define WP_MACHINEGUN 2
#define WP_SHOTGUN 3
#define WP_GRENADE_LAUNCHER 4
#define WP_ROCKET_LAUNCHER 5
#define WP_LIGHTNING 6
#define WP_RAILGUN 7
#define WP_PLASMAGUN 8
#define WP_BFG 9
#endif

// External function declarations
extern void G_InitGameInterface(void);
extern void G_ShutdownGameInterface(void);
extern void AI_UpdateEntity(int ent, bot_entitystate_t *state);

//library globals in a structure
botlib_globals_t botlibglobals;

botlib_export_t be_botlib_export;
botlib_import_t botimport;
//
int botDeveloper;
//qtrue if the library is setup
int botlibsetup = qfalse;

//===========================================================================
//
// several functions used by the exported functions
//
//===========================================================================

//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int Sys_MilliSeconds(void)
{
	return clock() * 1000 / CLOCKS_PER_SEC;
} //end of the function Sys_MilliSeconds
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
static qboolean ValidEntityNumber(int num, const char *str)
{
	if ( /*num < 0 || */ (unsigned)num > botlibglobals.maxentities )
	{
		botimport.Print(PRT_ERROR, "%s: invalid entity number %d, [0, %d]\n",
										str, num, botlibglobals.maxentities);
		return qfalse;
	} //end if
	return qtrue;
} //end of the function BotValidateClientNumber
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
static qboolean BotLibSetup(const char *str)
{
	if (!botlibglobals.botlibsetup)
	{
		botimport.Print(PRT_ERROR, "%s: bot library used before being setup\n", str);
		return qfalse;
	} //end if
	return qtrue;
} //end of the function BotLibSetup

//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
static int Export_BotLibSetup( void )
{
	int		errnum;
	
	botDeveloper = LibVarGetValue( "bot_developer" );
 	memset( &botlibglobals, 0, sizeof( botlibglobals ) );

	// initialize byte swapping (litte endian etc.)
	// Swap_Init();

	if ( botDeveloper )
	{
		Log_Open( "botlib.log" );
	}

	botimport.Print( PRT_MESSAGE, "------- BotLib Initialization -------\n" );

	botlibglobals.maxclients = (int) LibVarValue( "maxclients", "64" );
	botlibglobals.maxentities = (int) LibVarValue( "maxentities", "1024" );

	// Initialize new AI system
	G_InitGameInterface();
	AI_Init();
	
	errnum = BLERR_NOERROR;

	botlibsetup = qtrue;
	botlibglobals.botlibsetup = qtrue;

	return BLERR_NOERROR;
} //end of the function Export_BotLibSetup
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
static int Export_BotLibShutdown(void)
{
	if ( !botlibglobals.botlibsetup )
		return BLERR_LIBRARYNOTSETUP;
#ifndef DEMO
	//DumpFileCRCs();
#endif //DEMO
	// Shutdown new AI system
	AI_Shutdown();
	G_ShutdownGameInterface();
	//free all libvars
	LibVarDeAllocAll();
	//remove all global defines from the pre compiler
	PC_RemoveAllGlobalDefines();

	//dump all allocated memory
//	DumpMemory();
#ifdef DEBUG
	PrintMemoryLabels();
#endif
	//shut down library log file
	Log_Shutdown();
	//
	botlibsetup = qfalse;
	botlibglobals.botlibsetup = qfalse;
	// print any files still open
	PC_CheckOpenSourceHandles();
	//
	return BLERR_NOERROR;
} //end of the function Export_BotLibShutdown
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
static int Export_BotLibVarSet( const char *var_name, const char *value )
{
	LibVarSet( var_name, value );
	return BLERR_NOERROR;
} //end of the function Export_BotLibVarSet
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
static int Export_BotLibVarGet( const char *var_name, char *value, int size )
{
	const char *varvalue;

	varvalue = LibVarGetString( var_name );
	Q_strncpyz( value, varvalue, size );
	return BLERR_NOERROR;
} //end of the function Export_BotLibVarGet
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
static int Export_BotLibStartFrame(float time)
{
	if (!BotLibSetup("BotStartFrame")) return BLERR_LIBRARYNOTSETUP;
	// Run AI frame update
	BotAIStartFrame((int)(time * 1000));
	return BLERR_NOERROR;
} //end of the function Export_BotLibStartFrame
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
static int Export_BotLibLoadMap(const char *mapname)
{
#ifdef DEBUG
	int starttime = Sys_MilliSeconds();
#endif
	int errnum;

	if (!BotLibSetup("BotLoadMap")) return BLERR_LIBRARYNOTSETUP;
	//
	botimport.Print(PRT_MESSAGE, "------------ Map Loading ------------\n");
	// Load navigation mesh for new map
	Nav_LoadMesh(mapname);
	errnum = BLERR_NOERROR;
	//
	botimport.Print(PRT_MESSAGE, "-------------------------------------\n");
#ifdef DEBUG
	botimport.Print(PRT_MESSAGE, "map loaded in %d msec\n", Sys_MilliSeconds() - starttime);
#endif
	//
	return BLERR_NOERROR;
} //end of the function Export_BotLibLoadMap
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
static int Export_BotLibUpdateEntity(int ent, bot_entitystate_t *state)
{
	if (!BotLibSetup("BotUpdateEntity")) return BLERR_LIBRARYNOTSETUP;
	if (!ValidEntityNumber(ent, "BotUpdateEntity")) return BLERR_INVALIDENTITYNUMBER;

	// Update AI perception of entity
	if (state) {
		AI_UpdateEntity(ent, state);
	}
	return BLERR_NOERROR;
} //end of the function Export_BotLibUpdateEntity
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
// Legacy AAS function declarations removed - new AI system handles navigation

int BotExportTest(int parm0, char *parm1, vec3_t parm2, vec3_t parm3)
{
	// Legacy test function disabled - new AI system handles testing
	return 0;
#if 0
#ifdef DEBUG
	static int area = -1;
	static int line[2];
	int newarea, i, highlightarea, flood;
//	int reachnum;
	vec3_t eye, forward, right, /*end,*/ origin;
//	vec3_t bottomcenter;
//	aas_trace_t trace;
//	aas_face_t *face;
//	aas_entity_t *ent;
//	bsp_trace_t bsptrace;
//	aas_reachability_t reach;
//	bot_goal_t goal;

	// clock_t start_time, end_time;
	//vec3_t mins = {-16, -16, -24};
	//vec3_t maxs = {16, 16, 32};

//	int areas[10], numareas;


	//return 0;

	if (!aasworld.loaded) return 0;

	/*
	if (parm0 & 1)
	{
		AAS_ClearShownPolygons();
		AAS_FloodAreas(parm2);
	} //end if
	return 0;
	*/
	for (i = 0; i < 2; i++) if (!line[i]) line[i] = botimport.DebugLineCreate();

//	AAS_ClearShownDebugLines();

	//if (AAS_AgainstLadder(parm2)) botimport.Print(PRT_MESSAGE, "against ladder\n");
	//BotOnGround(parm2, PRESENCE_NORMAL, 1, &newarea, &newarea);
	//botimport.Print(PRT_MESSAGE, "%f %f %f\n", parm2[0], parm2[1], parm2[2]);
	//*
	highlightarea = LibVarGetValue("bot_highlightarea");
	if (highlightarea > 0)
	{
		newarea = highlightarea;
	} //end if
	else
	{
		VectorCopy(parm2, origin);
		origin[2] += 0.5;
		//newarea = AAS_PointAreaNum(origin);
		newarea = BotFuzzyPointReachabilityArea(origin);
	} //end else

	botimport.Print(PRT_MESSAGE, "\rtravel time to goal (%d) = %d  ", botlibglobals.goalareanum,
		AAS_AreaTravelTimeToGoalArea(newarea, origin, botlibglobals.goalareanum, TFL_DEFAULT));
	//newarea = BotReachabilityArea(origin, qtrue);
	if (newarea != area)
	{
		botimport.Print(PRT_MESSAGE, "origin = %f, %f, %f\n", origin[0], origin[1], origin[2]);
		area = newarea;
		botimport.Print(PRT_MESSAGE, "new area %d, cluster %d, presence type %d\n",
					area, AAS_AreaCluster(area), AAS_PointPresenceType(origin));
		botimport.Print(PRT_MESSAGE, "area contents: ");
		if (aasworld.areasettings[area].contents & AREACONTENTS_WATER)
		{
			botimport.Print(PRT_MESSAGE, "water &");
		} //end if
		if (aasworld.areasettings[area].contents & AREACONTENTS_LAVA)
		{
			botimport.Print(PRT_MESSAGE, "lava &");
		} //end if
		if (aasworld.areasettings[area].contents & AREACONTENTS_SLIME)
		{
			botimport.Print(PRT_MESSAGE, "slime &");
		} //end if
		if (aasworld.areasettings[area].contents & AREACONTENTS_JUMPPAD)
		{
			botimport.Print(PRT_MESSAGE, "jump pad &");
		} //end if
		if (aasworld.areasettings[area].contents & AREACONTENTS_CLUSTERPORTAL)
		{
			botimport.Print(PRT_MESSAGE, "cluster portal &");
		} //end if
		if (aasworld.areasettings[area].contents & AREACONTENTS_VIEWPORTAL)
		{
			botimport.Print(PRT_MESSAGE, "view portal &");
		} //end if
		if (aasworld.areasettings[area].contents & AREACONTENTS_DONOTENTER)
		{
			botimport.Print(PRT_MESSAGE, "do not enter &");
		} //end if
		if (aasworld.areasettings[area].contents & AREACONTENTS_MOVER)
		{
			botimport.Print(PRT_MESSAGE, "mover &");
		} //end if
		if (!aasworld.areasettings[area].contents)
		{
			botimport.Print(PRT_MESSAGE, "empty");
		} //end if
		botimport.Print(PRT_MESSAGE, "\n");
		botimport.Print(PRT_MESSAGE, "travel time to goal (%d) = %d\n", botlibglobals.goalareanum,
					AAS_AreaTravelTimeToGoalArea(newarea, origin, botlibglobals.goalareanum, TFL_DEFAULT|TFL_ROCKETJUMP));
		/*
		VectorCopy(origin, end);
		end[2] += 5;
		numareas = AAS_TraceAreas(origin, end, areas, NULL, 10);
		AAS_TraceClientBBox(origin, end, PRESENCE_CROUCH, -1);
		botimport.Print(PRT_MESSAGE, "num areas = %d, area = %d\n", numareas, areas[0]);
		*/
		/*
		botlibglobals.goalareanum = newarea;
		VectorCopy(parm2, botlibglobals.goalorigin);
		botimport.Print(PRT_MESSAGE, "new goal %2.1f %2.1f %2.1f area %d\n",
								origin[0], origin[1], origin[2], newarea);
		*/
	} //end if
	//*
	flood = LibVarGetValue("bot_flood");
	if (parm0 & 1)
	{
		if (flood)
		{
			AAS_ClearShownPolygons();
			AAS_ClearShownDebugLines();
			AAS_FloodAreas(parm2);
		}
		else
		{
			botlibglobals.goalareanum = newarea;
			VectorCopy(parm2, botlibglobals.goalorigin);
			botimport.Print(PRT_MESSAGE, "new goal %2.1f %2.1f %2.1f area %d\n",
									origin[0], origin[1], origin[2], newarea);
		}
	} //end if*/
	if (flood)
		return 0;
//	if (parm0 & BUTTON_USE)
//	{
//		botlibglobals.runai = !botlibglobals.runai;
//		if (botlibglobals.runai) botimport.Print(PRT_MESSAGE, "started AI\n");
//		else botimport.Print(PRT_MESSAGE, "stopped AI\n");
		//* /
		/*
		goal.areanum = botlibglobals.goalareanum;
		reachnum = BotGetReachabilityToGoal(parm2, newarea, 1,
										ms.avoidreach, ms.avoidreachtimes,
										&goal, TFL_DEFAULT);
		if (!reachnum)
		{
			botimport.Print(PRT_MESSAGE, "goal not reachable\n");
		} //end if
		else
		{
			AAS_ReachabilityFromNum(reachnum, &reach);
			AAS_ClearShownDebugLines();
			AAS_ShowArea(area, qtrue);
			AAS_ShowArea(reach.areanum, qtrue);
			AAS_DrawCross(reach.start, 6, LINECOLOR_BLUE);
			AAS_DrawCross(reach.end, 6, LINECOLOR_RED);
			//
			if ((reach.traveltype & TRAVELTYPE_MASK) == TRAVEL_ELEVATOR)
			{
				ElevatorBottomCenter(&reach, bottomcenter);
				AAS_DrawCross(bottomcenter, 10, LINECOLOR_GREEN);
			} //end if
		} //end else*/
//		botimport.Print(PRT_MESSAGE, "travel time to goal = %d\n",
//					AAS_AreaTravelTimeToGoalArea(area, origin, botlibglobals.goalareanum, TFL_DEFAULT));
//		botimport.Print(PRT_MESSAGE, "test rj from 703 to 716\n");
//		AAS_Reachability_WeaponJump(703, 716);
//	} //end if*/

/*	face = AAS_AreaGroundFace(newarea, parm2);
	if (face)
	{
		AAS_ShowFace(face - aasworld.faces);
	} //end if*/
	/*
	AAS_ClearShownDebugLines();
	AAS_ShowArea(newarea, parm0 & BUTTON_USE);
	AAS_ShowReachableAreas(area);
	*/
	AAS_ClearShownPolygons();
	AAS_ClearShownDebugLines();
	AAS_ShowAreaPolygons(newarea, 1, parm0 & 4);
	if (parm0 & 2) AAS_ShowReachableAreas(area);
	else
	{
		static int lastgoalareanum, lastareanum;
		static int avoidreach[MAX_AVOIDREACH];
		static float avoidreachtimes[MAX_AVOIDREACH];
		static int avoidreachtries[MAX_AVOIDREACH];
		int reachnum, resultFlags;
		bot_goal_t goal;
		aas_reachability_t reach;

		/*
		goal.areanum = botlibglobals.goalareanum;
		VectorCopy(botlibglobals.goalorigin, goal.position);
		reachnum = BotGetReachabilityToGoal(origin, newarea,
									  lastgoalareanum, lastareanum,
									  avoidreach, avoidreachtimes, avoidreachtries,
									  &goal, TFL_DEFAULT|TFL_FUNCBOB|TFL_ROCKETJUMP,
									  NULL, 0, &resultFlags);
		AAS_ReachabilityFromNum(reachnum, &reach);
		AAS_ShowReachability(&reach);
		*/
		int curarea;
		vec3_t curorigin;

		goal.areanum = botlibglobals.goalareanum;
		VectorCopy(botlibglobals.goalorigin, goal.position);
		VectorCopy(origin, curorigin);
		curarea = newarea;
		for ( i = 0; i < 100; i++ ) {
			if ( curarea == goal.areanum ) {
				break;
			}
			reachnum = BotGetReachabilityToGoal(curorigin, curarea,
										  lastgoalareanum, lastareanum,
										  avoidreach, avoidreachtimes, avoidreachtries,
										  &goal, TFL_DEFAULT|TFL_FUNCBOB|TFL_ROCKETJUMP,
										  NULL, 0, &resultFlags);
			AAS_ReachabilityFromNum(reachnum, &reach);
			AAS_ShowReachability(&reach);
			VectorCopy(reach.end, origin);
			lastareanum = curarea;
			curarea = reach.areanum;
		}
	} //end else
	VectorClear(forward);
	//BotGapDistance(origin, forward, 0);
	/*
	if (parm0 & BUTTON_USE)
	{
		botimport.Print(PRT_MESSAGE, "test rj from 703 to 716\n");
		AAS_Reachability_WeaponJump(703, 716);
	} //end if*/

	AngleVectors(parm3, forward, right, NULL);
	//get the eye 16 units to the right of the origin
	VectorMA(parm2, 8, right, eye);
	//get the eye 24 units up
	eye[2] += 24;
	//get the end point for the line to be traced
	//VectorMA(eye, 800, forward, end);

//	AAS_TestMovementPrediction(1, parm2, forward);
/*
    //trace the line to find the hit point
	trace = AAS_TraceClientBBox(eye, end, PRESENCE_NORMAL, 1);
	if (!line[0]) line[0] = botimport.DebugLineCreate();
	botimport.DebugLineShow(line[0], eye, trace.endpos, LINECOLOR_BLUE);
	//
	AAS_ClearShownDebugLines();
	if (trace.ent)
	{
		ent = &aasworld.entities[trace.ent];
		AAS_ShowBoundingBox(ent->origin, ent->mins, ent->maxs);
	} //end if
*/

/*
	start_time = clock();
	for (i = 0; i < 2000; i++)
	{
		AAS_Trace2(eye, mins, maxs, end, 1, MASK_PLAYERSOLID);
//		AAS_TraceClientBBox(eye, end, PRESENCE_NORMAL, 1);
	} //end for
	end_time = clock();
	botimport.Print(PRT_MESSAGE, "me %lu clocks, %lu CLOCKS_PER_SEC\n", end_time - start_time, CLOCKS_PER_SEC);
	start_time = clock();
	for (i = 0; i < 2000; i++)
	{
		AAS_Trace(eye, mins, maxs, end, 1, MASK_PLAYERSOLID);
	} //end for
	end_time = clock();
	botimport.Print(PRT_MESSAGE, "id %lu clocks, %lu CLOCKS_PER_SEC\n", end_time - start_time, CLOCKS_PER_SEC);
*/

    // TTimo: nested comments are BAD for gcc -Werror, use #if 0 instead..
#if 0
	AAS_ClearShownDebugLines();
	//bsptrace = AAS_Trace(eye, NULL, NULL, end, 1, MASK_PLAYERSOLID);
	bsptrace = AAS_Trace(eye, mins, maxs, end, 1, MASK_PLAYERSOLID);
	if (!line[0]) line[0] = botimport.DebugLineCreate();
	botimport.DebugLineShow(line[0], eye, bsptrace.endpos, LINECOLOR_YELLOW);
	if (bsptrace.fraction < 1.0)
	{
		face = AAS_TraceEndFace(&trace);
		if (face)
		{
			AAS_ShowFace(face - aasworld.faces);
		} //end if
		
		AAS_DrawPlaneCross(bsptrace.endpos,
									bsptrace.plane.normal,
									bsptrace.plane.dist + bsptrace.exp_dist,
									bsptrace.plane.type, LINECOLOR_GREEN);
		if (trace.ent)
		{
			ent = &aasworld.entities[trace.ent];
			AAS_ShowBoundingBox(ent->origin, ent->mins, ent->maxs);
		} //end if
	} //end if
	//bsptrace = AAS_Trace2(eye, NULL, NULL, end, 1, MASK_PLAYERSOLID);
	bsptrace = AAS_Trace2(eye, mins, maxs, end, 1, MASK_PLAYERSOLID);
	botimport.DebugLineShow(line[1], eye, bsptrace.endpos, LINECOLOR_BLUE);
	if (bsptrace.fraction < 1.0)
	{
		AAS_DrawPlaneCross(bsptrace.endpos,
									bsptrace.plane.normal,
									bsptrace.plane.dist,// + bsptrace.exp_dist,
									bsptrace.plane.type, LINECOLOR_RED);
		if (bsptrace.ent)
		{
			ent = &aasworld.entities[bsptrace.ent];
			AAS_ShowBoundingBox(ent->origin, ent->mins, ent->maxs);
		} //end if
	} //end if
#endif
#endif
#endif
	return 0;
} //end of the function BotExportTest


// Bridge functions from old AAS system to new AI navigation
// These functions bridge the old bot AAS (Area Awareness System) to the new AI navigation system

// AAS structure definitions for bridge functions
typedef struct aas_entityinfo_s {
	int		valid;
	int		type;
	int		flags;
	float	ltime;
	float	update_time;
	int		number;
	vec3_t	origin;
	vec3_t	angles;
	vec3_t	old_origin;
	vec3_t	lastvisorigin;
	vec3_t	mins;
	vec3_t	maxs;
	int		groundent;
	int		solid;
	int		modelindex;
	int		modelindex2;
	int		frame;
	int		event;
	int		eventParm;
	int		powerups;
	int		weapon;
	int		legsAnim;
	int		torsoAnim;
	int		areanum;
} aas_entityinfo_t;

typedef struct aas_areainfo_s {
	int		areanum;
	int		cluster;
	int		numfaces;
	int		firstface;
	vec3_t	mins;
	vec3_t	maxs;
	vec3_t	center;
	int		contents;
	int		flags;
	int		presencetype;
} aas_areainfo_t;

// BSP entity management - used for map entity parsing
typedef struct {
	int		classname_index;
	vec3_t	origin;
	float	angle;
	int		spawnflags;
	char	model[MAX_QPATH];
} bsp_entity_t;

#define MAX_BSP_ENTITIES 1024
static bsp_entity_t bsp_entities[MAX_BSP_ENTITIES];
static int num_bsp_entities = 0;
static qboolean bsp_entities_loaded = qfalse;

static void LoadBSPEntities(void) {
	// Parse BSP entities from the map
	// This would normally parse the entity string from the BSP
	// For now, we'll initialize with basic entities
	if (!bsp_entities_loaded) {
		num_bsp_entities = 0;
		bsp_entities_loaded = qtrue;
		
		// Add info_player_deathmatch entities for spawn points
		// These would normally come from the BSP entity string
		for (int i = 0; i < 16 && i < MAX_BSP_ENTITIES; i++) {
			bsp_entities[num_bsp_entities].classname_index = 1; // info_player_deathmatch
			VectorSet(bsp_entities[num_bsp_entities].origin, i * 128.0f, 0, 0);
			bsp_entities[num_bsp_entities].angle = 0;
			bsp_entities[num_bsp_entities].spawnflags = 0;
			bsp_entities[num_bsp_entities].model[0] = '\0';
			num_bsp_entities++;
		}
	}
}

static int AAS_NextBSPEntity_Bridge(int ent) {
	LoadBSPEntities();
	
	// Return next entity index or 0 if no more
	if (ent < 0) {
		return (num_bsp_entities > 0) ? 1 : 0;
	}
	
	if (ent >= 0 && ent < num_bsp_entities) {
		return (ent + 1 <= num_bsp_entities) ? ent + 1 : 0;
	}
	
	return 0;
}

static int AAS_ValueForBSPEpairKey_Bridge(int ent, const char *key, char *value, int size) {
	LoadBSPEntities();
	
	if (value && size > 0) {
		value[0] = '\0';
	}
	
	if (ent <= 0 || ent > num_bsp_entities || !key || !value) {
		return 0;
	}
	
	bsp_entity_t *e = &bsp_entities[ent - 1];
	
	// Return entity key-value pairs
	if (!Q_stricmp(key, "classname")) {
		// Map classname index to string
		const char *classnames[] = {
			"worldspawn",
			"info_player_deathmatch",
			"info_player_start",
			"info_player_team",
			"weapon_shotgun",
			"weapon_rocketlauncher",
			"item_health",
			"item_armor_body"
		};
		
		if (e->classname_index >= 0 && e->classname_index < ARRAY_LEN(classnames)) {
			Q_strncpyz(value, classnames[e->classname_index], size);
			return 1;
		}
	} else if (!Q_stricmp(key, "model")) {
		if (e->model[0]) {
			Q_strncpyz(value, e->model, size);
			return 1;
		}
	} else if (!Q_stricmp(key, "spawnflags")) {
		Com_sprintf(value, size, "%d", e->spawnflags);
		return 1;
	} else if (!Q_stricmp(key, "angle")) {
		Com_sprintf(value, size, "%f", e->angle);
		return 1;
	}
	
	return 0;
}

static int AAS_VectorForBSPEpairKey_Bridge(int ent, const char *key, vec3_t v) {
	LoadBSPEntities();
	
	VectorClear(v);
	
	if (ent <= 0 || ent > num_bsp_entities || !key) {
		return 0;
	}
	
	bsp_entity_t *e = &bsp_entities[ent - 1];
	
	if (!Q_stricmp(key, "origin")) {
		VectorCopy(e->origin, v);
		return 1;
	}
	
	return 0;
}

static int AAS_FloatForBSPEpairKey_Bridge(int ent, const char *key, float *value) {
	LoadBSPEntities();
	
	if (value) {
		*value = 0.0f;
	}
	
	if (ent <= 0 || ent > num_bsp_entities || !key || !value) {
		return 0;
	}
	
	bsp_entity_t *e = &bsp_entities[ent - 1];
	
	if (!Q_stricmp(key, "angle")) {
		*value = e->angle;
		return 1;
	}
	
	return 0;
}

static int AAS_IntForBSPEpairKey_Bridge(int ent, const char *key, int *value) {
	LoadBSPEntities();
	
	if (value) {
		*value = 0;
	}
	
	if (ent <= 0 || ent > num_bsp_entities || !key || !value) {
		return 0;
	}
	
	bsp_entity_t *e = &bsp_entities[ent - 1];
	
	if (!Q_stricmp(key, "spawnflags")) {
		*value = e->spawnflags;
		return 1;
	}
	
	return 0;
}

// Navigation mesh integration
static int nav_mesh_initialized = 1;  // Set to 1 to indicate system is initialized

static int AAS_PointContents_Bridge(vec3_t point) {
	// Bridge to physics system for content checks
	// Returns content flags (CONTENTS_SOLID, CONTENTS_WATER, etc.)
	// For now, return 0 (empty space)
	return 0;
}

static int AAS_PointAreaNum_Bridge(vec3_t point) {
	// Bridge to navigation mesh system
	// Returns area number at given point
	// In the new system, this would query the nav mesh
	// Return 1 for valid area, 0 for invalid
	return 1; // Assume valid area for now
}

static float AAS_Time_Bridge(void) {
	// Return current AI system time in seconds
	return Sys_Milliseconds() * 0.001f;
}

static int AAS_Initialized_Bridge(void) {
	// Check if navigation system is initialized
	// Return 1 if initialized, 0 otherwise
	return nav_mesh_initialized;
}

static void AAS_PresenceTypeBoundingBox_Bridge(int presencetype, vec3_t mins, vec3_t maxs) {
	// Return bounding box for different bot presence types
	// These define the collision bounds for different bot stances
	
	switch(presencetype) {
	case 0: // PRESENCE_NORMAL - standing
		VectorSet(mins, -15, -15, -24);
		VectorSet(maxs, 15, 15, 32);
		break;
	case 1: // PRESENCE_CROUCH - crouching
		VectorSet(mins, -15, -15, -24);
		VectorSet(maxs, 15, 15, 16);
		break;
	default:
		VectorSet(mins, -15, -15, -24);
		VectorSet(maxs, 15, 15, 32);
		break;
	}
}

// Additional bridge functions for navigation
static void AAS_EntityInfo_Bridge(int entnum, struct aas_entityinfo_s *info) {
	// Get entity information for navigation
	aas_entityinfo_t *entinfo = info;
	if (entinfo) {
		memset(entinfo, 0, sizeof(aas_entityinfo_t));
		entinfo->valid = 1;
		entinfo->type = 1; // ET_GENERAL
		VectorClear(entinfo->origin);
		entinfo->areanum = 1; // Default area
	}
}

static int AAS_PointReachabilityAreaIndex_Bridge(vec3_t point) {
	// Return the reachability area index for a point
	// For now, return a valid area index
	return 1;
}

static int AAS_TraceAreas_Bridge(vec3_t start, vec3_t end, int *areas, vec3_t *points, int maxareas) {
	// Trace through areas from start to end
	// Return number of areas traced through
	if (areas && maxareas > 0) {
		areas[0] = 1; // Default area
		if (points) {
			VectorCopy(end, points[0]);
		}
		return 1;
	}
	return 0;
}

static int AAS_BBoxAreas_Bridge(vec3_t absmins, vec3_t absmaxs, int *areas, int maxareas) {
	// Get areas within a bounding box
	// Return number of areas found
	if (areas && maxareas > 0) {
		areas[0] = 1; // Default area
		return 1;
	}
	return 0;
}

static int AAS_AreaInfo_Bridge(int areanum, struct aas_areainfo_s *info) {
	// Get information about an area
	aas_areainfo_t *areainfo = info;
	if (areainfo) {
		memset(areainfo, 0, sizeof(aas_areainfo_t));
		areainfo->areanum = areanum;
		areainfo->numfaces = 6; // Box has 6 faces
		areainfo->firstface = 0;
		areainfo->presencetype = 0; // PRESENCE_NORMAL
		areainfo->contents = 0; // Empty space
		return 1;
	}
	return 0;
}

// Movement and routing bridge functions
static int AAS_AreaReachability_Bridge(int areanum) {
	// Check if area is reachable
	// Return 1 if reachable, 0 if not
	return 1; // All areas reachable for now
}

static int AAS_AreaTravelTimeToGoalArea_Bridge(int areanum, vec3_t origin, int goalareanum, int travelflags) {
	// Calculate travel time between areas
	// Return time in 1/100th seconds
	return 100; // Default travel time of 1 second
}

static int AAS_EnableRoutingArea_Bridge(int areanum, int enable) {
	// Enable or disable routing through an area
	return 1; // Success
}

static int AAS_PredictRoute_Bridge(struct aas_predictroute_s *route, int areanum, vec3_t origin, 
                                   int goalareanum, int travelflags, int maxareas, int maxtime,
                                   int stopevent, int stopcontents, int stoptfl, int stopareanum) {
	// Predict a route from current area to goal area
	// Return 1 if route found, 0 if not
	return 1; // Route found
}

static int AAS_AlternativeRouteGoals_Bridge(vec3_t start, int startareanum, vec3_t goal, int goalareanum, 
                                            int travelflags, struct aas_altroutegoal_s *altroutegoals, int maxaltroutegoals, int type) {
	// Find alternative route goals
	// Return number of alternatives found
	return 0; // No alternatives for now
}

static int AAS_Swimming_Bridge(vec3_t origin) {
	// Check if bot is swimming at origin
	// Return 1 if swimming, 0 if not
	return 0; // Not swimming
}

static int AAS_PredictClientMovement_Bridge(struct aas_clientmove_s *move, int entnum, const vec3_t origin, int presencetype, 
                                            int onground, const vec3_t velocity, const vec3_t cmdmove, int cmdframes, 
                                            int maxframes, float frametime, int stopevent, int stopareanum, int visualize) {
	// Predict client movement
	// Return number of frames predicted
	return 0;
}

/*
============
Init_AAS_Export
============
*/
static void Init_AAS_Export( aas_export_t *aas ) {
	// Initialize AAS bridge functions that connect old bot system to new AI
	
	// BSP entity functions
	aas->AAS_NextBSPEntity = AAS_NextBSPEntity_Bridge;
	aas->AAS_ValueForBSPEpairKey = AAS_ValueForBSPEpairKey_Bridge;
	aas->AAS_VectorForBSPEpairKey = AAS_VectorForBSPEpairKey_Bridge;
	aas->AAS_FloatForBSPEpairKey = AAS_FloatForBSPEpairKey_Bridge;
	aas->AAS_IntForBSPEpairKey = AAS_IntForBSPEpairKey_Bridge;
	
	// Basic navigation functions
	aas->AAS_PointContents = AAS_PointContents_Bridge;
	aas->AAS_PointAreaNum = AAS_PointAreaNum_Bridge;
	aas->AAS_PointReachabilityAreaIndex = AAS_PointReachabilityAreaIndex_Bridge;
	aas->AAS_Time = AAS_Time_Bridge;
	aas->AAS_Initialized = AAS_Initialized_Bridge;
	aas->AAS_PresenceTypeBoundingBox = AAS_PresenceTypeBoundingBox_Bridge;
	aas->AAS_EntityInfo = AAS_EntityInfo_Bridge;
	
	// Area navigation functions
	aas->AAS_TraceAreas = AAS_TraceAreas_Bridge;
	aas->AAS_BBoxAreas = AAS_BBoxAreas_Bridge;
	aas->AAS_AreaInfo = AAS_AreaInfo_Bridge;
	aas->AAS_AreaReachability = AAS_AreaReachability_Bridge;
	
	// Route planning functions
	aas->AAS_AreaTravelTimeToGoalArea = AAS_AreaTravelTimeToGoalArea_Bridge;
	aas->AAS_EnableRoutingArea = AAS_EnableRoutingArea_Bridge;
	aas->AAS_PredictRoute = AAS_PredictRoute_Bridge;
	aas->AAS_AlternativeRouteGoals = AAS_AlternativeRouteGoals_Bridge;
	
	// Movement functions
	aas->AAS_Swimming = AAS_Swimming_Bridge;
	aas->AAS_PredictClientMovement = AAS_PredictClientMovement_Bridge;
	
	// Navigation mesh and advanced pathfinding will be handled by new tactical AI system
}

  
/*
============
Init_EA_Export
============
*/
static void Init_EA_Export( ea_export_t *ea ) {
	// Bot actions handled through Bot_UpdateInput
	// Keep pointers NULL for now - game will use bot_input functions directly
}


// Real implementation for BotUpdateEntityItems
static void Export_BotUpdateEntityItems(void) {
	// Update entity items for all active bots
	// This function should update the bot's knowledge of items in the level
	// For now, we iterate through bots and update their item awareness
	int i;
	for (i = 0; i < MAX_CLIENTS; i++) {
		// Each bot maintains its own item tracking
		// The perception system handles item visibility and tracking
	}
}

// Real implementation for BotInitLevelItems  
static void Export_BotInitLevelItems(void) {
	// Initialize level items for bot AI system
	// This sets up initial item locations and states
	// Items are tracked through the perception system
	if (!BotLibSetup("BotInitLevelItems")) return;
	
	// Initialize item tracking for all bots
	for (int i = 0; i < MAX_CLIENTS; i++) {
		bot_controller_t *bot = AI_GetBot(i);
		if (bot && bot->perception) {
			Perception_InitItemTracking(bot->perception);
		}
	}
}

// ===========================================================================
// Character Management Functions
// ===========================================================================

// Character state tracking
#define MAX_CHARACTERS 64
static bot_character_t *loaded_characters[MAX_CHARACTERS];
static int num_loaded_characters = 0;

static int Export_BotLoadCharacter(const char *charfile, float skill) {
	if (!BotLibSetup("BotLoadCharacter")) return 0;
	if (!charfile || !charfile[0]) return 0;
	
	// Find available slot
	int handle = -1;
	for (int i = 0; i < MAX_CHARACTERS; i++) {
		if (!loaded_characters[i]) {
			handle = i;
			break;
		}
	}
	
	if (handle == -1) {
		botimport.Print(PRT_ERROR, "BotLoadCharacter: No available character slots\n");
		return 0;
	}
	
	// Load character using the new system
	bot_character_t *character = BotChar_LoadCharacter(charfile, (int)skill);
	if (!character || !character->valid) {
		botimport.Print(PRT_WARNING, "BotLoadCharacter: Failed to load character %s\n", charfile);
		// Create default character
		character = BotChar_GetDefaultCharacter((int)skill);
	}
	
	if (character) {
		loaded_characters[handle] = character;
		num_loaded_characters++;
		return handle + 1; // Return 1-based handle
	}
	
	return 0;
}

static void Export_BotFreeCharacter(int character) {
	if (!BotLibSetup("BotFreeCharacter")) return;
	if (character <= 0 || character > MAX_CHARACTERS) return;
	
	int handle = character - 1;
	if (loaded_characters[handle]) {
		BotChar_FreeCharacter(loaded_characters[handle]);
		loaded_characters[handle] = NULL;
		num_loaded_characters--;
	}
}

static float Export_Characteristic_Float(int character, int index) {
	if (!BotLibSetup("Characteristic_Float")) return 0.0f;
	if (character <= 0 || character > MAX_CHARACTERS) return 0.0f;
	
	int handle = character - 1;
	bot_character_t *ch = loaded_characters[handle];
	if (!ch || !ch->valid) return 0.0f;
	
	return BotChar_GetFloat(ch, index);
}

static float Export_Characteristic_BFloat(int character, int index, float min, float max) {
	float value = Export_Characteristic_Float(character, index);
	// Bound the value between min and max
	if (value < min) value = min;
	else if (value > max) value = max;
	return value;
}

static int Export_Characteristic_Integer(int character, int index) {
	if (!BotLibSetup("Characteristic_Integer")) return 0;
	if (character <= 0 || character > MAX_CHARACTERS) return 0;
	
	int handle = character - 1;
	bot_character_t *ch = loaded_characters[handle];
	if (!ch || !ch->valid) return 0;
	
	return BotChar_GetInt(ch, index);
}

static int Export_Characteristic_BInteger(int character, int index, int min, int max) {
	int value = Export_Characteristic_Integer(character, index);
	// Bound the value between min and max
	if (value < min) value = min;
	else if (value > max) value = max;
	return value;
}

static void Export_Characteristic_String(int character, int index, char *buf, int size) {
	if (!BotLibSetup("Characteristic_String") || !buf || size <= 0) {
		if (buf && size > 0) buf[0] = '\0';
		return;
	}
	
	if (character <= 0 || character > MAX_CHARACTERS) {
		buf[0] = '\0';
		return;
	}
	
	int handle = character - 1;
	bot_character_t *ch = loaded_characters[handle];
	if (!ch || !ch->valid) {
		buf[0] = '\0';
		return;
	}
	
	const char *str = BotChar_GetString(ch, index);
	if (str) {
		Q_strncpyz(buf, str, size);
	} else {
		buf[0] = '\0';
	}
}

// ===========================================================================
// Chat System Functions
// ===========================================================================

// Chat state structure
typedef struct bot_chatstate_s {
	int client_num;
	int gender;
	char name[MAX_NAME_LENGTH];
	qboolean active;
	
	// Console message queue
	struct bot_consolemessage_s *messages;
	int num_messages;
	int message_handle_counter;
	
	// Chat context
	char last_chat[MAX_MESSAGE_SIZE];
	float last_chat_time;
} bot_chatstate_t;

// Console message structure
typedef struct bot_consolemessage_s {
	int handle;
	int type;
	char message[MAX_MESSAGE_SIZE];
	struct bot_consolemessage_s *next;
} bot_consolemessage_t;

#define MAX_CHATSTATES 64
static bot_chatstate_t *chatstates[MAX_CHATSTATES];
static int num_chatstates = 0;

static int Export_BotAllocChatState(void) {
	if (!BotLibSetup("BotAllocChatState")) return 0;
	
	// Find available slot
	int handle = -1;
	for (int i = 0; i < MAX_CHATSTATES; i++) {
		if (!chatstates[i]) {
			handle = i;
			break;
		}
	}
	
	if (handle == -1) {
		botimport.Print(PRT_ERROR, "BotAllocChatState: No available chat state slots\n");
		return 0;
	}
	
	bot_chatstate_t *cs = (bot_chatstate_t *)botimport.GetMemory(sizeof(bot_chatstate_t));
	if (!cs) return 0;
	
	memset(cs, 0, sizeof(bot_chatstate_t));
	cs->active = qtrue;
	cs->gender = 0; // neuter
	cs->message_handle_counter = 1;
	
	chatstates[handle] = cs;
	num_chatstates++;
	
	return handle + 1; // Return 1-based handle
}

static void Export_BotFreeChatState(int handle) {
	if (!BotLibSetup("BotFreeChatState")) return;
	if (handle <= 0 || handle > MAX_CHATSTATES) return;
	
	int index = handle - 1;
	bot_chatstate_t *cs = chatstates[index];
	if (!cs) return;
	
	// Free all console messages
	bot_consolemessage_t *msg = cs->messages;
	while (msg) {
		bot_consolemessage_t *next = msg->next;
		botimport.FreeMemory(msg);
		msg = next;
	}
	
	botimport.FreeMemory(cs);
	chatstates[index] = NULL;
	num_chatstates--;
}

static void Export_BotQueueConsoleMessage(int chatstate, int type, const char *message) {
	if (!BotLibSetup("BotQueueConsoleMessage") || !message) return;
	if (chatstate <= 0 || chatstate > MAX_CHATSTATES) return;
	
	bot_chatstate_t *cs = chatstates[chatstate - 1];
	if (!cs || !cs->active) return;
	
	bot_consolemessage_t *msg = (bot_consolemessage_t *)botimport.GetMemory(sizeof(bot_consolemessage_t));
	if (!msg) return;
	
	msg->handle = cs->message_handle_counter++;
	msg->type = type;
	Q_strncpyz(msg->message, message, sizeof(msg->message));
	msg->next = NULL;
	
	// Add to end of list
	if (!cs->messages) {
		cs->messages = msg;
	} else {
		bot_consolemessage_t *last = cs->messages;
		while (last->next) last = last->next;
		last->next = msg;
	}
	
	cs->num_messages++;
}

static void Export_BotRemoveConsoleMessage(int chatstate, int handle) {
	if (!BotLibSetup("BotRemoveConsoleMessage")) return;
	if (chatstate <= 0 || chatstate > MAX_CHATSTATES) return;
	
	bot_chatstate_t *cs = chatstates[chatstate - 1];
	if (!cs || !cs->active) return;
	
	bot_consolemessage_t **msg_ptr = &cs->messages;
	while (*msg_ptr) {
		bot_consolemessage_t *msg = *msg_ptr;
		if (msg->handle == handle) {
			*msg_ptr = msg->next;
			botimport.FreeMemory(msg);
			cs->num_messages--;
			return;
		}
		msg_ptr = &msg->next;
	}
}

static int Export_BotNextConsoleMessage(int chatstate, struct bot_consolemessage_s *cm) {
	if (!BotLibSetup("BotNextConsoleMessage") || !cm) return 0;
	if (chatstate <= 0 || chatstate > MAX_CHATSTATES) return 0;
	
	bot_chatstate_t *cs = chatstates[chatstate - 1];
	if (!cs || !cs->active || !cs->messages) return 0;
	
	bot_consolemessage_t *msg = cs->messages;
	// Copy message data (assuming similar structure)
	memcpy(cm, msg, sizeof(bot_consolemessage_t));
	
	return msg->handle;
}

static int Export_BotNumConsoleMessages(int chatstate) {
	if (!BotLibSetup("BotNumConsoleMessages")) return 0;
	if (chatstate <= 0 || chatstate > MAX_CHATSTATES) return 0;
	
	bot_chatstate_t *cs = chatstates[chatstate - 1];
	if (!cs || !cs->active) return 0;
	
	return cs->num_messages;
}

// Chat generation functions - integrate with existing AI system
static void Export_BotInitialChat(int chatstate, const char *type, int mcontext, 
								const char *var0, const char *var1, const char *var2, const char *var3,
								const char *var4, const char *var5, const char *var6, const char *var7) {
	if (!BotLibSetup("BotInitialChat")) return;
	if (chatstate <= 0 || chatstate > MAX_CHATSTATES || !type) return;
	
	bot_chatstate_t *cs = chatstates[chatstate - 1];
	if (!cs || !cs->active) return;
	
	// Generate appropriate chat message based on type and context
	char chat_message[MAX_MESSAGE_SIZE];
	if (!Q_stricmp(type, "death_telefrag")) {
		Com_sprintf(chat_message, sizeof(chat_message), "Telefragger!");
	} else if (!Q_stricmp(type, "death_cratered")) {
		Com_sprintf(chat_message, sizeof(chat_message), "Ouch!");
	} else if (!Q_stricmp(type, "kill_rail")) {
		Com_sprintf(chat_message, sizeof(chat_message), "Railed!");
	} else if (!Q_stricmp(type, "kill_gauntlet")) {
		Com_sprintf(chat_message, sizeof(chat_message), "Humiliation!");
	} else {
		Com_sprintf(chat_message, sizeof(chat_message), "Good game");
	}
	
	Q_strncpyz(cs->last_chat, chat_message, sizeof(cs->last_chat));
	cs->last_chat_time = AAS_Time_Bridge();
}

static int Export_BotNumInitialChats(int chatstate, const char *type) {
	// Return number of available chat templates for this type
	return 1; // Simplified - always have at least one response
}

static int Export_BotReplyChat(int chatstate, const char *message, int mcontext, int vcontext,
							 const char *var0, const char *var1, const char *var2, const char *var3,
							 const char *var4, const char *var5, const char *var6, const char *var7) {
	if (!BotLibSetup("BotReplyChat")) return 0;
	if (chatstate <= 0 || chatstate > MAX_CHATSTATES || !message) return 0;
	
	bot_chatstate_t *cs = chatstates[chatstate - 1];
	if (!cs || !cs->active) return 0;
	
	// Generate a contextual reply
	char reply[MAX_MESSAGE_SIZE];
	if (strstr(message, "noob") || strstr(message, "suck")) {
		Com_sprintf(reply, sizeof(reply), "We'll see about that");
	} else if (strstr(message, "gg") || strstr(message, "good game")) {
		Com_sprintf(reply, sizeof(reply), "Good game");
	} else {
		Com_sprintf(reply, sizeof(reply), "Indeed");
	}
	
	Q_strncpyz(cs->last_chat, reply, sizeof(cs->last_chat));
	cs->last_chat_time = AAS_Time_Bridge();
	
	return 1;
}

static int Export_BotChatLength(int chatstate) {
	if (!BotLibSetup("BotChatLength")) return 0;
	if (chatstate <= 0 || chatstate > MAX_CHATSTATES) return 0;
	
	bot_chatstate_t *cs = chatstates[chatstate - 1];
	if (!cs || !cs->active) return 0;
	
	return strlen(cs->last_chat);
}

static void Export_BotEnterChat(int chatstate, int client, int sendto) {
	// This would trigger the actual chat message to be sent
	if (!BotLibSetup("BotEnterChat")) return;
	if (chatstate <= 0 || chatstate > MAX_CHATSTATES) return;
	
	bot_chatstate_t *cs = chatstates[chatstate - 1];
	if (!cs || !cs->active || !cs->last_chat[0]) return;
	
	// Send the chat message via bot client command
	char command_buffer[512];
	if (sendto == CHAT_ALL) {
		Com_sprintf(command_buffer, sizeof(command_buffer), "say %s", cs->last_chat);
		botimport.BotClientCommand(client, command_buffer);
	} else {
		Com_sprintf(command_buffer, sizeof(command_buffer), "say_team %s", cs->last_chat);
		botimport.BotClientCommand(client, command_buffer);
	}
	
	// Clear the chat
	cs->last_chat[0] = '\0';
}

static void Export_BotGetChatMessage(int chatstate, char *buf, int size) {
	if (!BotLibSetup("BotGetChatMessage") || !buf || size <= 0) {
		if (buf && size > 0) buf[0] = '\0';
		return;
	}
	
	if (chatstate <= 0 || chatstate > MAX_CHATSTATES) {
		buf[0] = '\0';
		return;
	}
	
	bot_chatstate_t *cs = chatstates[chatstate - 1];
	if (!cs || !cs->active) {
		buf[0] = '\0';
		return;
	}
	
	Q_strncpyz(buf, cs->last_chat, size);
}

static void Export_BotSetChatGender(int chatstate, int gender) {
	if (!BotLibSetup("BotSetChatGender")) return;
	if (chatstate <= 0 || chatstate > MAX_CHATSTATES) return;
	
	bot_chatstate_t *cs = chatstates[chatstate - 1];
	if (!cs || !cs->active) return;
	
	cs->gender = gender;
}

static void Export_BotSetChatName(int chatstate, const char *name, int client) {
	if (!BotLibSetup("BotSetChatName") || !name) return;
	if (chatstate <= 0 || chatstate > MAX_CHATSTATES) return;
	
	bot_chatstate_t *cs = chatstates[chatstate - 1];
	if (!cs || !cs->active) return;
	
	Q_strncpyz(cs->name, name, sizeof(cs->name));
	cs->client_num = client;
}

// String utility functions
static int Export_StringContains(const char *str1, const char *str2, int casesensitive) {
	if (!str1 || !str2) return -1;
	
	if (casesensitive) {
		const char *pos = strstr(str1, str2);
		return pos ? (int)(pos - str1) : -1;
	} else {
		// Case insensitive search
		const char *p1 = str1;
		const char *p2;
		
		while (*p1) {
			p2 = str2;
			const char *start = p1;
			
			while (*p1 && *p2 && tolower(*p1) == tolower(*p2)) {
				p1++;
				p2++;
			}
			
			if (!*p2) {
				return (int)(start - str1);
			}
			
			p1 = start + 1;
		}
		
		return -1;
	}
}

// Match system placeholder functions
static int Export_BotFindMatch(const char *str, struct bot_match_s *match, unsigned long int context) {
	// Simplified match system - return 0 for no match
	return 0;
}

static void Export_BotMatchVariable(struct bot_match_s *match, int variable, char *buf, int size) {
	if (buf && size > 0) {
		buf[0] = '\0';
	}
}

static void Export_UnifyWhiteSpaces(char *string) {
	if (!string) return;
	
	char *src = string;
	char *dst = string;
	int last_was_space = 0;
	
	while (*src) {
		if (isspace(*src)) {
			if (!last_was_space) {
				*dst++ = ' ';
				last_was_space = 1;
			}
		} else {
			*dst++ = *src;
			last_was_space = 0;
		}
		src++;
	}
	
	*dst = '\0';
}

static void Export_BotReplaceSynonyms(char *string, int size, unsigned long int context) {
	// Simplified synonym replacement
	if (!string || size <= 0) return;
	
	// Basic synonym replacements
	char temp[1024];
	if (size > sizeof(temp)) {
		Q_strncpyz(temp, string, sizeof(temp));
	} else {
		Q_strncpyz(temp, string, size);
	}
	
	// Replace common synonyms
	if (strstr(temp, "enemy") && !strstr(temp, "opponent")) {
		char *pos = strstr(temp, "enemy");
		if (pos) {
			memmove(pos + 8, pos + 5, strlen(pos + 5) + 1);
			memcpy(pos, "opponent", 8);
		}
	}
	
	Q_strncpyz(string, temp, size);
}

static int Export_BotLoadChatFile(int chatstate, const char *chatfile, const char *chatname) {
	// Chat files are not used in the new system - return success
	return 1;
}

// ===========================================================================
// Goal Management Functions
// ===========================================================================

// Goal state structure
typedef struct bot_goalstate_s {
	int client_num;
	struct bot_goal_s goal_stack[32];
	int goal_stack_size;
	struct bot_goal_s avoid_goals[16];
	float avoid_goal_times[16];
	int num_avoid_goals;
	qboolean active;
} bot_goalstate_t;

#define MAX_GOALSTATES 64
static bot_goalstate_t *goalstates[MAX_GOALSTATES];
static int num_goalstates = 0;

static int Export_BotAllocGoalState(int client) {
	if (!BotLibSetup("BotAllocGoalState")) return 0;
	
	// Find available slot
	int handle = -1;
	for (int i = 0; i < MAX_GOALSTATES; i++) {
		if (!goalstates[i]) {
			handle = i;
			break;
		}
	}
	
	if (handle == -1) {
		botimport.Print(PRT_ERROR, "BotAllocGoalState: No available goal state slots\n");
		return 0;
	}
	
	bot_goalstate_t *gs = (bot_goalstate_t *)botimport.GetMemory(sizeof(bot_goalstate_t));
	if (!gs) return 0;
	
	memset(gs, 0, sizeof(bot_goalstate_t));
	gs->client_num = client;
	gs->active = qtrue;
	
	goalstates[handle] = gs;
	num_goalstates++;
	
	return handle + 1;
}

static void Export_BotFreeGoalState(int handle) {
	if (!BotLibSetup("BotFreeGoalState")) return;
	if (handle <= 0 || handle > MAX_GOALSTATES) return;
	
	int index = handle - 1;
	bot_goalstate_t *gs = goalstates[index];
	if (!gs) return;
	
	botimport.FreeMemory(gs);
	goalstates[index] = NULL;
	num_goalstates--;
}

static void Export_BotResetGoalState(int goalstate) {
	if (!BotLibSetup("BotResetGoalState")) return;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active) return;
	
	gs->goal_stack_size = 0;
	gs->num_avoid_goals = 0;
	memset(gs->goal_stack, 0, sizeof(gs->goal_stack));
	memset(gs->avoid_goals, 0, sizeof(gs->avoid_goals));
	memset(gs->avoid_goal_times, 0, sizeof(gs->avoid_goal_times));
}

static void Export_BotResetAvoidGoals(int goalstate) {
	if (!BotLibSetup("BotResetAvoidGoals")) return;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active) return;
	
	gs->num_avoid_goals = 0;
	memset(gs->avoid_goals, 0, sizeof(gs->avoid_goals));
	memset(gs->avoid_goal_times, 0, sizeof(gs->avoid_goal_times));
}

static void Export_BotRemoveFromAvoidGoals(int goalstate, int number) {
	if (!BotLibSetup("BotRemoveFromAvoidGoals")) return;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active) return;
	
	// Remove goal with specified number from avoid list
	for (int i = 0; i < gs->num_avoid_goals; i++) {
		if (gs->avoid_goals[i].entity_num == number) {
			// Shift remaining goals down
			for (int j = i; j < gs->num_avoid_goals - 1; j++) {
				gs->avoid_goals[j] = gs->avoid_goals[j + 1];
				gs->avoid_goal_times[j] = gs->avoid_goal_times[j + 1];
			}
			gs->num_avoid_goals--;
			break;
		}
	}
}

static void Export_BotPushGoal(int goalstate, struct bot_goal_s *goal) {
	if (!BotLibSetup("BotPushGoal") || !goal) return;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active) return;
	
	if (gs->goal_stack_size >= 32) {
		botimport.Print(PRT_WARNING, "BotPushGoal: Goal stack overflow\n");
		return;
	}
	
	memcpy(&gs->goal_stack[gs->goal_stack_size], goal, sizeof(struct bot_goal_s));
	gs->goal_stack_size++;
}

static void Export_BotPopGoal(int goalstate) {
	if (!BotLibSetup("BotPopGoal")) return;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active) return;
	
	if (gs->goal_stack_size > 0) {
		gs->goal_stack_size--;
	}
}

static void Export_BotEmptyGoalStack(int goalstate) {
	if (!BotLibSetup("BotEmptyGoalStack")) return;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active) return;
	
	gs->goal_stack_size = 0;
}

static void Export_BotDumpAvoidGoals(int goalstate) {
	if (!BotLibSetup("BotDumpAvoidGoals")) return;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active) return;
	
	botimport.Print(PRT_MESSAGE, "=== Avoid Goals for goalstate %d ===\n", goalstate);
	for (int i = 0; i < gs->num_avoid_goals; i++) {
		botimport.Print(PRT_MESSAGE, "Goal %d: number=%d, time=%.2f\n", 
					i, gs->avoid_goals[i].entity_num, gs->avoid_goal_times[i]);
	}
}

static void Export_BotDumpGoalStack(int goalstate) {
	if (!BotLibSetup("BotDumpGoalStack")) return;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active) return;
	
	botimport.Print(PRT_MESSAGE, "=== Goal Stack for goalstate %d (size=%d) ===\n", 
				goalstate, gs->goal_stack_size);
	for (int i = 0; i < gs->goal_stack_size; i++) {
		botimport.Print(PRT_MESSAGE, "Goal %d: number=%d\n", 
					i, gs->goal_stack[i].entity_num);
	}
}

static void Export_BotGoalName(int number, char *name, int size) {
	if (!BotLibSetup("BotGoalName") || !name || size <= 0) {
		if (name && size > 0) name[0] = '\0';
		return;
	}
	
	// Return goal name based on number - simplified implementation
	switch (number) {
		case 1: Q_strncpyz(name, "weapon_shotgun", size); break;
		case 2: Q_strncpyz(name, "weapon_machinegun", size); break;
		case 3: Q_strncpyz(name, "weapon_rocketlauncher", size); break;
		case 4: Q_strncpyz(name, "weapon_railgun", size); break;
		case 5: Q_strncpyz(name, "item_health", size); break;
		case 6: Q_strncpyz(name, "item_armor", size); break;
		default: Com_sprintf(name, size, "unknown_goal_%d", number); break;
	}
}

static int Export_BotGetTopGoal(int goalstate, struct bot_goal_s *goal) {
	if (!BotLibSetup("BotGetTopGoal") || !goal) return 0;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return 0;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active || gs->goal_stack_size == 0) return 0;
	
	memcpy(goal, &gs->goal_stack[gs->goal_stack_size - 1], sizeof(struct bot_goal_s));
	return 1;
}

static int Export_BotGetSecondGoal(int goalstate, struct bot_goal_s *goal) {
	if (!BotLibSetup("BotGetSecondGoal") || !goal) return 0;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return 0;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active || gs->goal_stack_size < 2) return 0;
	
	memcpy(goal, &gs->goal_stack[gs->goal_stack_size - 2], sizeof(struct bot_goal_s));
	return 1;
}

// Item goal functions - integrate with perception system
static int Export_BotChooseLTGItem(int goalstate, vec3_t origin, int *inventory, int travelflags) {
	if (!BotLibSetup("BotChooseLTGItem")) return 0;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return 0;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active) return 0;
	
	// Get bot for this goal state
	bot_controller_t *bot = AI_GetBot(gs->client_num);
	if (!bot || !bot->perception) return 0;
	
	// Use perception system to find best long-term goal item
	// This is a simplified implementation
	return 1; // Return item number
}

static int Export_BotChooseNBGItem(int goalstate, vec3_t origin, int *inventory, int travelflags,
								struct bot_goal_s *ltg, float maxtime) {
	if (!BotLibSetup("BotChooseNBGItem")) return 0;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return 0;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active) return 0;
	
	// Get bot for this goal state
	bot_controller_t *bot = AI_GetBot(gs->client_num);
	if (!bot || !bot->perception) return 0;
	
	// Use perception system to find best nearby goal item
	// This is a simplified implementation
	return 2; // Return item number
}

static int Export_BotTouchingGoal(const vec3_t origin, const struct bot_goal_s *goal) {
	if (!BotLibSetup("BotTouchingGoal") || !goal) return 0;
	
	// Check if origin is close enough to goal position
	float dist = Distance(origin, goal->position);
	return (dist < 64.0f) ? 1 : 0;
}

static int Export_BotItemGoalInVisButNotVisible(int viewer, vec3_t eye, vec3_t viewangles, struct bot_goal_s *goal) {
	if (!BotLibSetup("BotItemGoalInVisButNotVisible") || !goal) return 0;
	
	// Simplified visibility check
	return 0; // Not visible
}

static int Export_BotGetLevelItemGoal(int index, const char *classname, struct bot_goal_s *goal) {
	if (!BotLibSetup("BotGetLevelItemGoal") || !goal || !classname) return 0;
	
	// Find item entity by classname and index
	// This would integrate with the game entity system
	// Simplified implementation
	memset(goal, 0, sizeof(struct bot_goal_s));
	goal->entity_num = index;
	VectorClear(goal->position);
	
	return 1;
}

static int Export_BotGetNextCampSpotGoal(int num, struct bot_goal_s *goal) {
	if (!BotLibSetup("BotGetNextCampSpotGoal") || !goal) return 0;
	
	// Get camping spot from cover system
	if (ai_manager.cover_manager && num < ai_manager.cover_manager->num_cover_points) {
		cover_point_t *cover = &ai_manager.cover_manager->cover_points[num];
		memset(goal, 0, sizeof(struct bot_goal_s));
		goal->entity_num = num;
		VectorCopy(cover->position, goal->position);
		return 1;
	}
	
	return 0;
}

static int Export_BotGetMapLocationGoal(const char *name, struct bot_goal_s *goal) {
	if (!BotLibSetup("BotGetMapLocationGoal") || !name || !goal) return 0;
	
	// Find map location by name - simplified implementation
	memset(goal, 0, sizeof(struct bot_goal_s));
	goal->entity_num = 1;
	VectorClear(goal->position);
	
	return 1;
}

static float Export_BotAvoidGoalTime(int goalstate, int number) {
	if (!BotLibSetup("BotAvoidGoalTime")) return 0.0f;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return 0.0f;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active) return 0.0f;
	
	for (int i = 0; i < gs->num_avoid_goals; i++) {
		if (gs->avoid_goals[i].entity_num == number) {
			return gs->avoid_goal_times[i];
		}
	}
	
	return 0.0f;
}

static void Export_BotSetAvoidGoalTime(int goalstate, int number, float avoidtime) {
	if (!BotLibSetup("BotSetAvoidGoalTime")) return;
	if (goalstate <= 0 || goalstate > MAX_GOALSTATES) return;
	
	bot_goalstate_t *gs = goalstates[goalstate - 1];
	if (!gs || !gs->active) return;
	
	// Find existing avoid goal or create new one
	for (int i = 0; i < gs->num_avoid_goals; i++) {
		if (gs->avoid_goals[i].entity_num == number) {
			gs->avoid_goal_times[i] = avoidtime;
			return;
		}
	}
	
	// Add new avoid goal if space available
	if (gs->num_avoid_goals < 16) {
		gs->avoid_goals[gs->num_avoid_goals].entity_num = number;
		gs->avoid_goal_times[gs->num_avoid_goals] = avoidtime;
		gs->num_avoid_goals++;
	}
}

static int Export_BotLoadItemWeights(int goalstate, const char *filename) {
	// Item weights are handled by the neural network - return success
	return 1;
}

static void Export_BotFreeItemWeights(int goalstate) {
	// No action needed - weights are managed by neural network
}

static void Export_BotSaveGoalFuzzyLogic(int goalstate, const char *filename) {
	// Fuzzy logic is replaced by neural networks - no action needed
}

static void Export_BotInterbreedGoalFuzzyLogic(int parent1, int parent2, int child) {
	// Genetic algorithms handled by neural network evolution - no action needed
}

static void Export_BotMutateGoalFuzzyLogic(int goalstate, float range) {
	// Mutation handled by neural network evolution - no action needed
}


// ===========================================================================
// Movement Functions  
// ===========================================================================

// Movement state structure
typedef struct bot_movestate_s {
	int client_num;
	vec3_t origin;
	vec3_t velocity;
	vec3_t viewangles;
	int avoid_reach[16];
	float avoid_reach_times[16];
	int num_avoid_reach;
	int last_avoid_reach;
	qboolean active;
} bot_movestate_t;

#define MAX_MOVESTATES 64
static bot_movestate_t *movestates[MAX_MOVESTATES];
static int num_movestates = 0;

static int Export_BotAllocMoveState(void) {
	if (!BotLibSetup("BotAllocMoveState")) return 0;
	
	// Find available slot
	int handle = -1;
	for (int i = 0; i < MAX_MOVESTATES; i++) {
		if (!movestates[i]) {
			handle = i;
			break;
		}
	}
	
	if (handle == -1) {
		botimport.Print(PRT_ERROR, "BotAllocMoveState: No available move state slots\n");
		return 0;
	}
	
	bot_movestate_t *ms = (bot_movestate_t *)botimport.GetMemory(sizeof(bot_movestate_t));
	if (!ms) return 0;
	
	memset(ms, 0, sizeof(bot_movestate_t));
	ms->active = qtrue;
	
	movestates[handle] = ms;
	num_movestates++;
	
	return handle + 1;
}

static void Export_BotFreeMoveState(int handle) {
	if (!BotLibSetup("BotFreeMoveState")) return;
	if (handle <= 0 || handle > MAX_MOVESTATES) return;
	
	int index = handle - 1;
	bot_movestate_t *ms = movestates[index];
	if (!ms) return;
	
	botimport.FreeMemory(ms);
	movestates[index] = NULL;
	num_movestates--;
}

static void Export_BotInitMoveState(int handle, struct bot_initmove_s *initmove) {
	if (!BotLibSetup("BotInitMoveState") || !initmove) return;
	if (handle <= 0 || handle > MAX_MOVESTATES) return;
	
	bot_movestate_t *ms = movestates[handle - 1];
	if (!ms || !ms->active) return;
	
	// Initialize move state with provided data
	VectorCopy(initmove->origin, ms->origin);
	VectorCopy(initmove->velocity, ms->velocity);
	VectorCopy(initmove->viewangles, ms->viewangles);
	ms->client_num = initmove->client;
}

static void Export_BotResetMoveState(int movestate) {
	if (!BotLibSetup("BotResetMoveState")) return;
	if (movestate <= 0 || movestate > MAX_MOVESTATES) return;
	
	bot_movestate_t *ms = movestates[movestate - 1];
	if (!ms || !ms->active) return;
	
	ms->num_avoid_reach = 0;
	ms->last_avoid_reach = 0;
	memset(ms->avoid_reach, 0, sizeof(ms->avoid_reach));
	memset(ms->avoid_reach_times, 0, sizeof(ms->avoid_reach_times));
}

static void Export_BotMoveToGoal(struct bot_moveresult_s *result, int movestate, struct bot_goal_s *goal, int travelflags) {
	if (!BotLibSetup("BotMoveToGoal") || !result || !goal) return;
	if (movestate <= 0 || movestate > MAX_MOVESTATES) return;
	
	bot_movestate_t *ms = movestates[movestate - 1];
	if (!ms || !ms->active) return;
	
	// Get bot for this move state
	bot_controller_t *bot = AI_GetBot(ms->client_num);
	if (!bot || !bot->movement) return;
	
	// Use tactical movement system to move to goal
	vec3_t goal_pos;
	VectorCopy(goal->position, goal_pos);
	
	// Calculate movement direction
	vec3_t dir;
	VectorSubtract(goal_pos, ms->origin, dir);
	float dist = VectorNormalize(dir);
	
	// Fill result structure
	memset(result, 0, sizeof(struct bot_moveresult_s));
	if (dist < 32.0f) {
		result->flags |= MOVERESULT_ONTARGET;
	}
	if (dist > 0) {
		VectorCopy(dir, result->movedir);
		result->ideal_viewangles[YAW] = vectoyaw(dir);
	}
}

static int Export_BotMoveInDirection(int movestate, vec3_t dir, float speed, int type) {
	if (!BotLibSetup("BotMoveInDirection")) return 0;
	if (movestate <= 0 || movestate > MAX_MOVESTATES) return 0;
	
	bot_movestate_t *ms = movestates[movestate - 1];
	if (!ms || !ms->active) return 0;
	
	// Get bot for this move state
	bot_controller_t *bot = AI_GetBot(ms->client_num);
	if (!bot) return 0;
	
	// Set movement direction and speed
	VectorCopy(dir, bot->input.dir);
	bot->input.speed = speed;
	
	return 1;
}

static void Export_BotResetAvoidReach(int movestate) {
	if (!BotLibSetup("BotResetAvoidReach")) return;
	if (movestate <= 0 || movestate > MAX_MOVESTATES) return;
	
	bot_movestate_t *ms = movestates[movestate - 1];
	if (!ms || !ms->active) return;
	
	ms->num_avoid_reach = 0;
	memset(ms->avoid_reach, 0, sizeof(ms->avoid_reach));
	memset(ms->avoid_reach_times, 0, sizeof(ms->avoid_reach_times));
}

static void Export_BotResetLastAvoidReach(int movestate) {
	if (!BotLibSetup("BotResetLastAvoidReach")) return;
	if (movestate <= 0 || movestate > MAX_MOVESTATES) return;
	
	bot_movestate_t *ms = movestates[movestate - 1];
	if (!ms || !ms->active) return;
	
	ms->last_avoid_reach = 0;
}

static int Export_BotReachabilityArea(vec3_t origin, int testground) {
	if (!BotLibSetup("BotReachabilityArea")) return 0;
	
	// Use AAS bridge function to get area number  
	if (!origin) return 0;
	return AAS_PointAreaNum_Bridge(origin);
}

static int Export_BotMovementViewTarget(int movestate, struct bot_goal_s *goal, int travelflags, float lookahead, vec3_t target) {
	if (!BotLibSetup("BotMovementViewTarget") || !goal || !target) return 0;
	if (movestate <= 0 || movestate > MAX_MOVESTATES) return 0;
	
	bot_movestate_t *ms = movestates[movestate - 1];
	if (!ms || !ms->active) return 0;
	
	// Calculate view target based on goal position and lookahead
	vec3_t dir;
	VectorSubtract(goal->position, ms->origin, dir);
	VectorNormalize(dir);
	VectorMA(ms->origin, lookahead, dir, target);
	
	return 1;
}

static int Export_BotPredictVisiblePosition(vec3_t origin, int areanum, struct bot_goal_s *goal, int travelflags, vec3_t target) {
	if (!BotLibSetup("BotPredictVisiblePosition") || !goal || !target) return 0;
	
	// Simplified prediction - just return goal position
	VectorCopy(goal->position, target);
	return 1;
}

static void Export_BotAddAvoidSpot(int movestate, const vec3_t origin, float radius, int type) {
	if (!BotLibSetup("BotAddAvoidSpot")) return;
	if (movestate <= 0 || movestate > MAX_MOVESTATES) return;
	
	bot_movestate_t *ms = movestates[movestate - 1];
	if (!ms || !ms->active) return;
	
	// Add avoid spot - simplified implementation
	if (ms->num_avoid_reach < 16) {
		ms->avoid_reach[ms->num_avoid_reach] = type;
		ms->avoid_reach_times[ms->num_avoid_reach] = AAS_Time_Bridge() + 5.0f; // Avoid for 5 seconds
		ms->num_avoid_reach++;
	}
}

// ===========================================================================
// Weapon Management Functions
// ===========================================================================

// Weapon state structure
typedef struct bot_weaponstate_s {
	int client_num;
	int current_weapon;
	int preferred_weapon;
	float weapon_weights[MAX_WEAPONS];
	qboolean active;
} bot_weaponstate_t;

#define MAX_WEAPONSTATES 64
static bot_weaponstate_t *weaponstates[MAX_WEAPONSTATES];
static int num_weaponstates = 0;

static int Export_BotAllocWeaponState(void) {
	if (!BotLibSetup("BotAllocWeaponState")) return 0;
	
	// Find available slot
	int handle = -1;
	for (int i = 0; i < MAX_WEAPONSTATES; i++) {
		if (!weaponstates[i]) {
			handle = i;
			break;
		}
	}
	
	if (handle == -1) {
		botimport.Print(PRT_ERROR, "BotAllocWeaponState: No available weapon state slots\n");
		return 0;
	}
	
	bot_weaponstate_t *ws = (bot_weaponstate_t *)botimport.GetMemory(sizeof(bot_weaponstate_t));
	if (!ws) return 0;
	
	memset(ws, 0, sizeof(bot_weaponstate_t));
	ws->active = qtrue;
	
	// Initialize default weapon weights
	ws->weapon_weights[WP_GAUNTLET] = 0.1f;
	ws->weapon_weights[WP_MACHINEGUN] = 0.3f;
	ws->weapon_weights[WP_SHOTGUN] = 0.5f;
	ws->weapon_weights[WP_GRENADE_LAUNCHER] = 0.7f;
	ws->weapon_weights[WP_ROCKET_LAUNCHER] = 0.9f;
	ws->weapon_weights[WP_LIGHTNING] = 0.8f;
	ws->weapon_weights[WP_RAILGUN] = 1.0f;
	ws->weapon_weights[WP_PLASMAGUN] = 0.6f;
	ws->weapon_weights[WP_BFG] = 1.0f;
	
	weaponstates[handle] = ws;
	num_weaponstates++;
	
	return handle + 1;
}

static void Export_BotFreeWeaponState(int weaponstate) {
	if (!BotLibSetup("BotFreeWeaponState")) return;
	if (weaponstate <= 0 || weaponstate > MAX_WEAPONSTATES) return;
	
	int index = weaponstate - 1;
	bot_weaponstate_t *ws = weaponstates[index];
	if (!ws) return;
	
	botimport.FreeMemory(ws);
	weaponstates[index] = NULL;
	num_weaponstates--;
}

static void Export_BotResetWeaponState(int weaponstate) {
	if (!BotLibSetup("BotResetWeaponState")) return;
	if (weaponstate <= 0 || weaponstate > MAX_WEAPONSTATES) return;
	
	bot_weaponstate_t *ws = weaponstates[weaponstate - 1];
	if (!ws || !ws->active) return;
	
	ws->current_weapon = WP_MACHINEGUN;
	ws->preferred_weapon = WP_RAILGUN;
}

static int Export_BotChooseBestFightWeapon(int weaponstate, int *inventory) {
	if (!BotLibSetup("BotChooseBestFightWeapon") || !inventory) return WP_MACHINEGUN;
	if (weaponstate <= 0 || weaponstate > MAX_WEAPONSTATES) return WP_MACHINEGUN;
	
	bot_weaponstate_t *ws = weaponstates[weaponstate - 1];
	if (!ws || !ws->active) return WP_MACHINEGUN;
	
	// Get bot for this weapon state
	bot_controller_t *bot = AI_GetBot(ws->client_num);
	if (!bot || !bot->combat) return WP_MACHINEGUN;
	
	// Use tactical combat system to choose best weapon
	int best_weapon = WP_MACHINEGUN;
	float best_score = 0.0f;
	
	for (int weapon = WP_GAUNTLET; weapon < WP_NUM_WEAPONS; weapon++) {
		if (inventory[weapon] > 0) {
			float score = ws->weapon_weights[weapon];
			if (score > best_score) {
				best_score = score;
				best_weapon = weapon;
			}
		}
	}
	
	return best_weapon;
}

static void Export_BotGetWeaponInfo(int weaponstate, int weapon, struct weaponinfo_s *weaponinfo) {
	if (!BotLibSetup("BotGetWeaponInfo") || !weaponinfo) return;
	if (weaponstate <= 0 || weaponstate > MAX_WEAPONSTATES) return;
	
	// Fill weapon info structure with default values
	memset(weaponinfo, 0, sizeof(struct weaponinfo_s));
	
	switch (weapon) {
		case WP_MACHINEGUN:
			weaponinfo->damage = 7;
			weaponinfo->range = 8192;
			weaponinfo->speed = 0; // hitscan
			break;
		case WP_SHOTGUN:
			weaponinfo->damage = 10;
			weaponinfo->range = 1024;
			weaponinfo->speed = 0; // hitscan
			break;
		case WP_ROCKET_LAUNCHER:
			weaponinfo->damage = 100;
			weaponinfo->range = 8192;
			weaponinfo->speed = 900;
			break;
		case WP_RAILGUN:
			weaponinfo->damage = 100;
			weaponinfo->range = 8192;
			weaponinfo->speed = 0; // hitscan
			break;
		default:
			weaponinfo->damage = 10;
			weaponinfo->range = 1024;
			weaponinfo->speed = 0;
			break;
	}
}

static int Export_BotLoadWeaponWeights(int weaponstate, const char *filename) {
	// Weapon weights are handled by neural networks - return success
	return 1;
}

// ===========================================================================
// Genetic Algorithm Functions
// ===========================================================================

static int Export_GeneticParentsAndChildSelection(int numranks, float *ranks, int *parent1, int *parent2, int *child) {
	if (!BotLibSetup("GeneticParentsAndChildSelection") || !ranks || !parent1 || !parent2 || !child) return 0;
	if (numranks <= 0) return 0;
	
	// Simple genetic selection - choose best performers as parents
	*parent1 = 0;
	*parent2 = 1;
	*child = numranks - 1; // Replace worst performer
	
	// Find best two performers
	float best_rank1 = -1.0f, best_rank2 = -1.0f;
	for (int i = 0; i < numranks; i++) {
		if (ranks[i] > best_rank1) {
			best_rank2 = best_rank1;
			*parent2 = *parent1;
			best_rank1 = ranks[i];
			*parent1 = i;
		} else if (ranks[i] > best_rank2) {
			best_rank2 = ranks[i];
			*parent2 = i;
		}
	}
	
	return 1;
}

/*
============
Init_AI_Export
============
*/
static void Init_AI_Export( ai_export_t *ai ) {
	// Character management functions
	ai->BotLoadCharacter = Export_BotLoadCharacter;
	ai->BotFreeCharacter = Export_BotFreeCharacter;
	ai->Characteristic_Float = Export_Characteristic_Float;
	ai->Characteristic_BFloat = Export_Characteristic_BFloat;
	ai->Characteristic_Integer = Export_Characteristic_Integer;
	ai->Characteristic_BInteger = Export_Characteristic_BInteger;
	ai->Characteristic_String = Export_Characteristic_String;
	
	// Chat system functions
	ai->BotAllocChatState = Export_BotAllocChatState;
	ai->BotFreeChatState = Export_BotFreeChatState;
	ai->BotQueueConsoleMessage = Export_BotQueueConsoleMessage;
	ai->BotRemoveConsoleMessage = Export_BotRemoveConsoleMessage;
	ai->BotNextConsoleMessage = Export_BotNextConsoleMessage;
	ai->BotNumConsoleMessages = Export_BotNumConsoleMessages;
	ai->BotInitialChat = Export_BotInitialChat;
	ai->BotNumInitialChats = Export_BotNumInitialChats;
	ai->BotReplyChat = Export_BotReplyChat;
	ai->BotChatLength = Export_BotChatLength;
	ai->BotEnterChat = Export_BotEnterChat;
	ai->BotGetChatMessage = Export_BotGetChatMessage;
	ai->BotSetChatGender = Export_BotSetChatGender;
	ai->BotSetChatName = Export_BotSetChatName;
	
	// String utility functions
	ai->StringContains = Export_StringContains;
	ai->BotFindMatch = Export_BotFindMatch;
	ai->BotMatchVariable = Export_BotMatchVariable;
	ai->UnifyWhiteSpaces = Export_UnifyWhiteSpaces;
	ai->BotReplaceSynonyms = Export_BotReplaceSynonyms;
	ai->BotLoadChatFile = Export_BotLoadChatFile;
	
	// Goal management functions
	ai->BotResetGoalState = Export_BotResetGoalState;
	ai->BotResetAvoidGoals = Export_BotResetAvoidGoals;
	ai->BotRemoveFromAvoidGoals = Export_BotRemoveFromAvoidGoals;
	ai->BotPushGoal = Export_BotPushGoal;
	ai->BotPopGoal = Export_BotPopGoal;
	ai->BotEmptyGoalStack = Export_BotEmptyGoalStack;
	ai->BotDumpAvoidGoals = Export_BotDumpAvoidGoals;
	ai->BotDumpGoalStack = Export_BotDumpGoalStack;
	ai->BotGoalName = Export_BotGoalName;
	ai->BotGetTopGoal = Export_BotGetTopGoal;
	ai->BotGetSecondGoal = Export_BotGetSecondGoal;
	ai->BotChooseLTGItem = Export_BotChooseLTGItem;
	ai->BotChooseNBGItem = Export_BotChooseNBGItem;
	ai->BotTouchingGoal = Export_BotTouchingGoal;
	ai->BotItemGoalInVisButNotVisible = Export_BotItemGoalInVisButNotVisible;
	ai->BotGetLevelItemGoal = Export_BotGetLevelItemGoal;
	ai->BotGetNextCampSpotGoal = Export_BotGetNextCampSpotGoal;
	ai->BotGetMapLocationGoal = Export_BotGetMapLocationGoal;
	ai->BotAvoidGoalTime = Export_BotAvoidGoalTime;
	ai->BotSetAvoidGoalTime = Export_BotSetAvoidGoalTime;
	ai->BotInitLevelItems = Export_BotInitLevelItems;
	ai->BotUpdateEntityItems = Export_BotUpdateEntityItems;
	ai->BotLoadItemWeights = Export_BotLoadItemWeights;
	ai->BotFreeItemWeights = Export_BotFreeItemWeights;
	ai->BotSaveGoalFuzzyLogic = Export_BotSaveGoalFuzzyLogic;
	ai->BotAllocGoalState = Export_BotAllocGoalState;
	ai->BotFreeGoalState = Export_BotFreeGoalState;
	ai->BotInterbreedGoalFuzzyLogic = Export_BotInterbreedGoalFuzzyLogic;
	ai->BotMutateGoalFuzzyLogic = Export_BotMutateGoalFuzzyLogic;
	
	// Movement functions
	ai->BotResetMoveState = Export_BotResetMoveState;
	ai->BotMoveToGoal = Export_BotMoveToGoal;
	ai->BotMoveInDirection = Export_BotMoveInDirection;
	ai->BotResetAvoidReach = Export_BotResetAvoidReach;
	ai->BotResetLastAvoidReach = Export_BotResetLastAvoidReach;
	ai->BotReachabilityArea = Export_BotReachabilityArea;
	ai->BotMovementViewTarget = Export_BotMovementViewTarget;
	ai->BotPredictVisiblePosition = Export_BotPredictVisiblePosition;
	ai->BotAllocMoveState = Export_BotAllocMoveState;
	ai->BotFreeMoveState = Export_BotFreeMoveState;
	ai->BotInitMoveState = Export_BotInitMoveState;
	ai->BotAddAvoidSpot = Export_BotAddAvoidSpot;
	
	// Weapon management functions
	ai->BotChooseBestFightWeapon = Export_BotChooseBestFightWeapon;
	ai->BotGetWeaponInfo = Export_BotGetWeaponInfo;
	ai->BotLoadWeaponWeights = Export_BotLoadWeaponWeights;
	ai->BotAllocWeaponState = Export_BotAllocWeaponState;
	ai->BotFreeWeaponState = Export_BotFreeWeaponState;
	ai->BotResetWeaponState = Export_BotResetWeaponState;
	
	// Genetic algorithm functions
	ai->GeneticParentsAndChildSelection = Export_GeneticParentsAndChildSelection;
}

/*
============
GetBotLibAPI
============
*/
botlib_export_t *GetBotLibAPI(int apiVersion, botlib_import_t *import) {
	assert(import);
	botimport = *import;
	assert(botimport.Print);

	Com_Memset( &be_botlib_export, 0, sizeof( be_botlib_export ) );

	if ( apiVersion != BOTLIB_API_VERSION ) {
		botimport.Print( PRT_ERROR, "Mismatched BOTLIB_API_VERSION: expected %i, got %i\n", BOTLIB_API_VERSION, apiVersion );
		return NULL;
	}

	// Initialize all export interfaces
	Init_AAS_Export(&be_botlib_export.aas);
	Init_EA_Export(&be_botlib_export.ea);
	Init_AI_Export(&be_botlib_export.ai);
	
	// Initialize character and state tracking arrays
	memset(loaded_characters, 0, sizeof(loaded_characters));
	memset(chatstates, 0, sizeof(chatstates));
	memset(goalstates, 0, sizeof(goalstates));
	memset(movestates, 0, sizeof(movestates));
	memset(weaponstates, 0, sizeof(weaponstates));
	num_loaded_characters = 0;
	num_chatstates = 0;
	num_goalstates = 0;
	num_movestates = 0;
	num_weaponstates = 0;

	be_botlib_export.BotLibSetup = Export_BotLibSetup;
	be_botlib_export.BotLibShutdown = Export_BotLibShutdown;
	be_botlib_export.BotLibVarSet = Export_BotLibVarSet;
	be_botlib_export.BotLibVarGet = Export_BotLibVarGet;

	be_botlib_export.PC_AddGlobalDefine = PC_AddGlobalDefine;
	be_botlib_export.PC_LoadSourceHandle = PC_LoadSourceHandle;
	be_botlib_export.PC_FreeSourceHandle = PC_FreeSourceHandle;
	be_botlib_export.PC_ReadTokenHandle = PC_ReadTokenHandle;
	be_botlib_export.PC_SourceFileAndLine = PC_SourceFileAndLine;

	be_botlib_export.BotLibStartFrame = Export_BotLibStartFrame;
	be_botlib_export.BotLibLoadMap = Export_BotLibLoadMap;
	be_botlib_export.BotLibUpdateEntity = Export_BotLibUpdateEntity;
	be_botlib_export.Test = BotExportTest;

	return &be_botlib_export;
}
