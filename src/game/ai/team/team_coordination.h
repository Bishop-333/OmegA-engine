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

#ifndef TEAM_COORDINATION_H
#define TEAM_COORDINATION_H

#include "../../../engine/common/q_shared.h"

// Weapon constants from q_shared.h
#ifndef MAX_WEAPONS
#define MAX_WEAPONS 16
#endif
#include "../strategic/strategic_planning.h"

#define MAX_TEAM_SIZE 16
#define MAX_SQUADS 4
#define MAX_SQUAD_SIZE 4
#define MAX_TEAM_OBJECTIVES 8
#define MAX_TEAM_MESSAGES 32
#define COORDINATION_UPDATE_INTERVAL 500

typedef enum {
    TEAM_ROLE_LEADER,
    TEAM_ROLE_ASSAULT,
    TEAM_ROLE_SUPPORT,
    TEAM_ROLE_SNIPER,
    TEAM_ROLE_SCOUT,
    TEAM_ROLE_DEFENDER,
    TEAM_ROLE_MEDIC,
    TEAM_ROLE_ENGINEER
} team_role_t;

typedef enum {
    SQUAD_STATE_IDLE,
    SQUAD_STATE_MOVING,
    SQUAD_STATE_ENGAGING,
    SQUAD_STATE_DEFENDING,
    SQUAD_STATE_REGROUPING,
    SQUAD_STATE_FLANKING,
    SQUAD_STATE_SUPPORTING,
    SQUAD_STATE_RETREATING
} squad_state_t;

typedef enum {
    FORMATION_NONE,
    FORMATION_LINE,
    FORMATION_COLUMN,
    FORMATION_WEDGE,
    FORMATION_DIAMOND,
    FORMATION_ECHELON,
    FORMATION_VEE,
    FORMATION_CIRCLE,
    FORMATION_SPREAD
} formation_type_t;

typedef enum {
    MSG_TYPE_COMMAND,
    MSG_TYPE_STATUS,
    MSG_TYPE_REQUEST,
    MSG_TYPE_ALERT,
    MSG_TYPE_RESPONSE,
    MSG_TYPE_COORDINATION
} message_type_t;

typedef enum {
    CMD_ATTACK,
    CMD_DEFEND,
    CMD_REGROUP,
    CMD_FOLLOW,
    CMD_HOLD,
    CMD_RETREAT,
    CMD_FLANK_LEFT,
    CMD_FLANK_RIGHT,
    CMD_PROVIDE_COVER,
    CMD_SUPPRESS
} command_type_t;

typedef struct team_member_s {
    int client_id;
    team_role_t role;
    vec3_t position;
    vec3_t velocity;
    float health;
    float armor;
    int weapon;
    int ammo[MAX_WEAPONS];
    qboolean alive;
    qboolean in_combat;
    int current_target;
    int squad_id;
    float skill_level;
    float effectiveness;
    float last_update_time;
    
    // Task assignment
    tactical_objective_t *assigned_objective;
    vec3_t assigned_position;
    int covering_member;
    
    // Communication
    float last_communication_time;
    int pending_messages;
} team_member_t;

typedef struct squad_s {
    int id;
    char name[32];
    team_member_t *members[MAX_SQUAD_SIZE];
    int num_members;
    int leader_id;
    
    squad_state_t state;
    formation_type_t formation;
    vec3_t rally_point;
    vec3_t movement_destination;
    
    tactical_objective_t *objective;
    float objective_progress;
    
    // Squad tactics
    vec3_t attack_vector;
    vec3_t defend_position;
    float spread_distance;
    float engagement_range;
    
    // Performance
    int kills;
    int deaths;
    float damage_dealt;
    float damage_taken;
    float cohesion;
    float effectiveness;
} squad_t;

typedef struct team_message_s {
    message_type_t type;
    int sender_id;
    int recipient_id; // -1 for broadcast
    float timestamp;
    command_type_t command;
    vec3_t position;
    int target_entity;
    float priority;
    char text[128];
    qboolean acknowledged;
} team_message_t;

typedef struct team_objective_s {
    tactical_objective_t *objective;
    squad_t *assigned_squad;
    float priority;
    float progress;
    float start_time;
    float deadline;
    qboolean completed;
    int participants[MAX_TEAM_SIZE];
    int num_participants;
} team_objective_t;

typedef struct team_tactics_s {
    qboolean coordinated_attack;
    qboolean synchronized_movement;
    qboolean crossfire_enabled;
    qboolean suppression_active;
    qboolean flanking_maneuver;
    vec3_t primary_attack_vector;
    vec3_t secondary_attack_vector;
    float coordination_level;
    float risk_tolerance;
} team_tactics_t;

typedef struct team_coordinator_s {
    team_member_t members[MAX_TEAM_SIZE];
    int num_members;
    int team_id;
    
    squad_t squads[MAX_SQUADS];
    int num_squads;
    
    // Leadership
    int commander_id;
    int squad_leaders[MAX_SQUADS];
    
    // Objectives and planning
    strategic_planner_t *strategic_planner;
    team_objective_t objectives[MAX_TEAM_OBJECTIVES];
    int num_objectives;
    
    // Communication
    team_message_t message_queue[MAX_TEAM_MESSAGES];
    int message_count;
    int message_head;
    int message_tail;
    
    // Team tactics
    team_tactics_t tactics;
    
    // Performance tracking
    float team_effectiveness;
    float coordination_quality;
    int total_kills;
    int total_deaths;
    float win_probability;
    
    // Timing
    float last_coordination_time;
    float last_tactics_update;
} team_coordinator_t;

// Core functions
void Team_InitCoordination(void);
void Team_ShutdownCoordination(void);
team_coordinator_t *Team_CreateCoordinator(int team_id);
void Team_DestroyCoordinator(team_coordinator_t *coordinator);

// Member management
void Team_AddMember(team_coordinator_t *coordinator, int client_id, team_role_t role);
void Team_RemoveMember(team_coordinator_t *coordinator, int client_id);
void Team_UpdateMember(team_coordinator_t *coordinator, int client_id);
team_member_t *Team_GetMember(team_coordinator_t *coordinator, int client_id);
void Team_AssignRole(team_coordinator_t *coordinator, int client_id, team_role_t role);

// Squad management
squad_t *Team_CreateSquad(team_coordinator_t *coordinator, const char *name);
void Team_DisbandSquad(team_coordinator_t *coordinator, int squad_id);
void Team_AssignToSquad(team_coordinator_t *coordinator, int client_id, int squad_id);
void Team_RemoveFromSquad(team_coordinator_t *coordinator, int client_id);
void Team_SetSquadLeader(squad_t *squad, int leader_id);
void Team_UpdateSquadState(squad_t *squad, squad_state_t state);

// Formation management
void Team_SetFormation(squad_t *squad, formation_type_t formation);
void Team_CalculateFormationPositions(squad_t *squad, vec3_t positions[]);
void Team_MaintainFormation(squad_t *squad);
float Team_GetFormationCohesion(squad_t *squad);
qboolean Team_IsInFormation(squad_t *squad);

// Coordination and tactics
void Team_CoordinateActions(team_coordinator_t *coordinator);
void Team_UpdateTactics(team_coordinator_t *coordinator);
void Team_ExecuteStrategy(team_coordinator_t *coordinator);
void Team_SynchronizeMovement(team_coordinator_t *coordinator);
void Team_CoordinateAttack(team_coordinator_t *coordinator, int target_id);

// Objective management
void Team_AssignObjective(team_coordinator_t *coordinator, tactical_objective_t *objective);
void Team_DistributeObjectives(team_coordinator_t *coordinator);
void Team_UpdateObjectiveProgress(team_coordinator_t *coordinator);
qboolean Team_IsObjectiveComplete(team_objective_t *team_obj);

// Communication
void Team_SendMessage(team_coordinator_t *coordinator, team_message_t *message);
void Team_BroadcastCommand(team_coordinator_t *coordinator, command_type_t command);
void Team_ProcessMessages(team_coordinator_t *coordinator);
void Team_RequestSupport(team_coordinator_t *coordinator, int requester_id, const vec3_t position);
void Team_ReportStatus(team_coordinator_t *coordinator, int member_id);

// Role-specific functions
void Team_LeaderDecision(team_coordinator_t *coordinator, int leader_id);
void Team_AssaultAdvance(team_coordinator_t *coordinator, team_member_t *member);
void Team_SupportProvide(team_coordinator_t *coordinator, team_member_t *member);
void Team_SniperPosition(team_coordinator_t *coordinator, team_member_t *member);
void Team_ScoutRecon(team_coordinator_t *coordinator, team_member_t *member);

// Tactical maneuvers
void Team_ExecuteFlanking(team_coordinator_t *coordinator, squad_t *squad, const vec3_t target);
void Team_SetupCrossfire(team_coordinator_t *coordinator, const vec3_t target);
void Team_ProvideSuppression(team_coordinator_t *coordinator, squad_t *squad, const vec3_t area);
void Team_ExecutePincer(team_coordinator_t *coordinator, const vec3_t target);
void Team_CoverAndAdvance(team_coordinator_t *coordinator, squad_t *squad);

// Situational responses
void Team_RespondToThreat(team_coordinator_t *coordinator, const vec3_t threat_pos, int threat_level);
void Team_HandleCasualty(team_coordinator_t *coordinator, int casualty_id);
void Team_Regroup(team_coordinator_t *coordinator, const vec3_t rally_point);
void Team_EmergencyRetreat(team_coordinator_t *coordinator);

// Performance evaluation
void Team_EvaluatePerformance(team_coordinator_t *coordinator);
float Team_CalculateEffectiveness(team_coordinator_t *coordinator);
float Team_GetCoordinationQuality(team_coordinator_t *coordinator);
void Team_AdjustTactics(team_coordinator_t *coordinator, float performance);

// Utility functions
float Team_GetAverageSkill(team_coordinator_t *coordinator);
int Team_GetAliveMembers(team_coordinator_t *coordinator);
vec3_t *Team_GetCenterPosition(team_coordinator_t *coordinator, vec3_t center);
float Team_GetSpread(team_coordinator_t *coordinator);
qboolean Team_CanSeeEachOther(team_member_t *member1, team_member_t *member2);

// Debug and visualization
void Team_DebugDraw(team_coordinator_t *coordinator);
void Team_DrawFormation(squad_t *squad);
void Team_PrintStatus(team_coordinator_t *coordinator);

#endif // TEAM_COORDINATION_H


