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

#include "team_coordination.h"
#include "../game_interface.h"
#include "../game_entities.h"
#include "../ai_system.h"
#include "../../../engine/core/qcommon.h"
#include <math.h>
#include <string.h>

// External globals are defined in game_entities.h

static struct {
    qboolean initialized;
    team_coordinator_t *coordinators[4]; // Max 4 teams
    int coordinator_count;
    cvar_t *team_coordination;
    cvar_t *team_communication;
    cvar_t *team_formations;
} team_global;

/*
==================
Team_InitCoordination
==================
*/
void Team_InitCoordination(void) {
    if (team_global.initialized) {
        return;
    }
    
    memset(&team_global, 0, sizeof(team_global));
    
    team_global.team_coordination = Cvar_Get("ai_team_coordination", "1", CVAR_ARCHIVE);
    team_global.team_communication = Cvar_Get("ai_team_communication", "1", CVAR_ARCHIVE);
    team_global.team_formations = Cvar_Get("ai_team_formations", "1", CVAR_ARCHIVE);
    
    team_global.initialized = qtrue;
    
    Com_Printf("Team Coordination System Initialized\n");
}

/*
==================
Team_ShutdownCoordination
==================
*/
void Team_ShutdownCoordination(void) {
    int i;
    
    if (!team_global.initialized) {
        return;
    }
    
    for (i = 0; i < 4; i++) {
        if (team_global.coordinators[i]) {
            Team_DestroyCoordinator(team_global.coordinators[i]);
        }
    }
    
    team_global.initialized = qfalse;
    Com_Printf("Team Coordination System Shutdown\n");
}

/*
==================
Team_CreateCoordinator
==================
*/
team_coordinator_t *Team_CreateCoordinator(int team_id) {
    team_coordinator_t *coordinator;
    
    if (team_id < 0 || team_id >= 4) {
        return NULL;
    }
    
    coordinator = (team_coordinator_t *)Z_Malloc(sizeof(team_coordinator_t));
    memset(coordinator, 0, sizeof(team_coordinator_t));
    
    coordinator->team_id = team_id;
    coordinator->strategic_planner = Strategy_CreatePlanner();
    coordinator->commander_id = -1;
    
    // Initialize tactics
    coordinator->tactics.coordination_level = 0.5f;
    coordinator->tactics.coordinated_attack = qfalse;
    coordinator->tactics.synchronized_movement = qtrue;
    
    team_global.coordinators[team_id] = coordinator;
    team_global.coordinator_count++;
    
    Com_DPrintf("Created team coordinator for team %d\n", team_id);
    
    return coordinator;
}

/*
==================
Team_DestroyCoordinator
==================
*/
void Team_DestroyCoordinator(team_coordinator_t *coordinator) {
    if (!coordinator) {
        return;
    }
    
    if (coordinator->strategic_planner) {
        Strategy_DestroyPlanner(coordinator->strategic_planner);
    }
    
    if (coordinator->team_id >= 0 && coordinator->team_id < 4) {
        team_global.coordinators[coordinator->team_id] = NULL;
        team_global.coordinator_count--;
    }
    
    Z_Free(coordinator);
}

/*
==================
Team_AddMember
==================
*/
void Team_AddMember(team_coordinator_t *coordinator, int client_id, team_role_t role) {
    team_member_t *member;
    
    if (!coordinator || coordinator->num_members >= MAX_TEAM_SIZE) {
        return;
    }
    
    // Check if already a member
    for (int i = 0; i < coordinator->num_members; i++) {
        if (coordinator->members[i].client_id == client_id) {
            return;
        }
    }
    
    member = &coordinator->members[coordinator->num_members];
    memset(member, 0, sizeof(team_member_t));
    
    member->client_id = client_id;
    member->role = role;
    member->alive = qtrue;
    member->squad_id = -1;
    member->skill_level = 1.0f;
    member->effectiveness = 1.0f;
    
    coordinator->num_members++;
    
    // Assign to squad if available
    if (coordinator->num_squads > 0) {
        // Find squad with room
        for (int i = 0; i < coordinator->num_squads; i++) {
            if (coordinator->squads[i].num_members < MAX_SQUAD_SIZE) {
                Team_AssignToSquad(coordinator, client_id, i);
                break;
            }
        }
    }
    
    Com_DPrintf("Added member %d to team %d with role %d\n", client_id, coordinator->team_id, role);
}

/*
==================
Team_UpdateMember
==================
*/
void Team_UpdateMember(team_coordinator_t *coordinator, int client_id) {
    team_member_t *member;
    gentity_t *ent;
    
    if (!coordinator) {
        return;
    }
    
    member = Team_GetMember(coordinator, client_id);
    if (!member) {
        return;
    }
    
    ent = &g_entities[client_id];
    if (!ent || !ent->inuse || !ent->client) {
        member->alive = qfalse;
        return;
    }
    
    // Update member state
    VectorCopy(ent->s.pos.trBase, member->position);
    VectorCopy(ent->s.pos.trDelta, member->velocity);
    member->health = ent->health;
    member->armor = ent->client->ps.stats[STAT_ARMOR];
    member->weapon = ent->client->ps.weapon;
    member->alive = (ent->health > 0);
    
    // Copy ammo counts
    for (int i = 0; i < MAX_WEAPONS; i++) {
        member->ammo[i] = ent->client->ps.ammo[i];
    }
    
    member->last_update_time = level.time * 0.001f;
}

/*
==================
Team_GetMember
==================
*/
team_member_t *Team_GetMember(team_coordinator_t *coordinator, int client_id) {
    if (!coordinator) {
        return NULL;
    }
    
    for (int i = 0; i < coordinator->num_members; i++) {
        if (coordinator->members[i].client_id == client_id) {
            return &coordinator->members[i];
        }
    }
    
    return NULL;
}

/*
==================
Team_CreateSquad
==================
*/
squad_t *Team_CreateSquad(team_coordinator_t *coordinator, const char *name) {
    squad_t *squad;
    
    if (!coordinator || coordinator->num_squads >= MAX_SQUADS) {
        return NULL;
    }
    
    squad = &coordinator->squads[coordinator->num_squads];
    memset(squad, 0, sizeof(squad_t));
    
    squad->id = coordinator->num_squads;
    Q_strncpyz(squad->name, name, sizeof(squad->name));
    squad->state = SQUAD_STATE_IDLE;
    squad->formation = FORMATION_WEDGE;
    squad->spread_distance = 100;
    squad->engagement_range = 500;
    squad->leader_id = -1;
    squad->cohesion = 1.0f;
    squad->effectiveness = 1.0f;
    
    coordinator->num_squads++;
    
    Com_DPrintf("Created squad '%s' for team %d\n", name, coordinator->team_id);
    
    return squad;
}

/*
==================
Team_AssignToSquad
==================
*/
void Team_AssignToSquad(team_coordinator_t *coordinator, int client_id, int squad_id) {
    team_member_t *member;
    squad_t *squad;
    
    if (!coordinator || squad_id < 0 || squad_id >= coordinator->num_squads) {
        return;
    }
    
    member = Team_GetMember(coordinator, client_id);
    squad = &coordinator->squads[squad_id];
    
    if (!member || squad->num_members >= MAX_SQUAD_SIZE) {
        return;
    }
    
    // Remove from previous squad
    if (member->squad_id >= 0) {
        Team_RemoveFromSquad(coordinator, client_id);
    }
    
    // Add to new squad
    squad->members[squad->num_members] = member;
    squad->num_members++;
    member->squad_id = squad_id;
    
    // Assign as leader if first member
    if (squad->num_members == 1) {
        squad->leader_id = client_id;
    }
}

/*
==================
Team_RemoveFromSquad
==================
*/
void Team_RemoveFromSquad(team_coordinator_t *coordinator, int client_id) {
    team_member_t *member;
    squad_t *squad;
    int i, j;
    
    if (!coordinator) {
        return;
    }
    
    member = Team_GetMember(coordinator, client_id);
    if (!member || member->squad_id < 0) {
        return;
    }
    
    squad = &coordinator->squads[member->squad_id];
    
    // Find and remove member from squad
    for (i = 0; i < squad->num_members; i++) {
        if (squad->members[i] == member) {
            // Shift remaining members
            for (j = i; j < squad->num_members - 1; j++) {
                squad->members[j] = squad->members[j + 1];
            }
            squad->num_members--;
            break;
        }
    }
    
    // Select new leader if needed
    if (squad->leader_id == client_id && squad->num_members > 0) {
        squad->leader_id = squad->members[0]->client_id;
    }
    
    member->squad_id = -1;
}

/*
==================
Team_SetFormation
==================
*/
void Team_SetFormation(squad_t *squad, formation_type_t formation) {
    if (!squad) {
        return;
    }
    
    squad->formation = formation;
    
    // Adjust spread based on formation
    switch (formation) {
        case FORMATION_LINE:
            squad->spread_distance = 150;
            break;
        case FORMATION_COLUMN:
            squad->spread_distance = 50;
            break;
        case FORMATION_WEDGE:
            squad->spread_distance = 100;
            break;
        case FORMATION_DIAMOND:
            squad->spread_distance = 80;
            break;
        case FORMATION_SPREAD:
            squad->spread_distance = 200;
            break;
        default:
            squad->spread_distance = 100;
            break;
    }
}

/*
==================
Team_CalculateFormationPositions
==================
*/
void Team_CalculateFormationPositions(squad_t *squad, vec3_t positions[]) {
    vec3_t forward, right;
    vec3_t leader_pos;
    float angle;
    int i;
    
    if (!squad || squad->num_members == 0) {
        return;
    }
    
    // Get leader position as reference
    if (squad->leader_id >= 0) {
        for (i = 0; i < squad->num_members; i++) {
            if (squad->members[i]->client_id == squad->leader_id) {
                VectorCopy(squad->members[i]->position, leader_pos);
                break;
            }
        }
    } else {
        // Use center of squad
        VectorClear(leader_pos);
        for (i = 0; i < squad->num_members; i++) {
            VectorAdd(leader_pos, squad->members[i]->position, leader_pos);
        }
        VectorScale(leader_pos, 1.0f / squad->num_members, leader_pos);
    }
    
    // Calculate formation vectors
    VectorSubtract(squad->movement_destination, leader_pos, forward);
    forward[2] = 0;
    VectorNormalize(forward);
    
    right[0] = -forward[1];
    right[1] = forward[0];
    right[2] = 0;
    
    // Calculate positions based on formation
    switch (squad->formation) {
        case FORMATION_LINE:
            for (i = 0; i < squad->num_members; i++) {
                float offset = (i - squad->num_members / 2.0f) * squad->spread_distance;
                VectorMA(leader_pos, offset, right, positions[i]);
            }
            break;
            
        case FORMATION_COLUMN:
            for (i = 0; i < squad->num_members; i++) {
                float offset = -i * squad->spread_distance;
                VectorMA(leader_pos, offset, forward, positions[i]);
            }
            break;
            
        case FORMATION_WEDGE:
            positions[0][0] = leader_pos[0];
            positions[0][1] = leader_pos[1];
            positions[0][2] = leader_pos[2];
            for (i = 1; i < squad->num_members; i++) {
                float side = (i % 2 == 0) ? 1 : -1;
                float back = -(i / 2) * squad->spread_distance * 0.7f;
                float lateral = side * (i / 2) * squad->spread_distance;
                
                VectorMA(leader_pos, back, forward, positions[i]);
                VectorMA(positions[i], lateral, right, positions[i]);
            }
            break;
            
        case FORMATION_DIAMOND:
            if (squad->num_members >= 1) VectorCopy(leader_pos, positions[0]);
            if (squad->num_members >= 2) VectorMA(leader_pos, -squad->spread_distance, forward, positions[1]);
            if (squad->num_members >= 3) VectorMA(leader_pos, squad->spread_distance, right, positions[2]);
            if (squad->num_members >= 4) VectorMA(leader_pos, -squad->spread_distance, right, positions[3]);
            break;
            
        case FORMATION_CIRCLE:
            angle = 360.0f / squad->num_members;
            for (i = 0; i < squad->num_members; i++) {
                float rad = DEG2RAD(angle * i);
                positions[i][0] = leader_pos[0] + cos(rad) * squad->spread_distance;
                positions[i][1] = leader_pos[1] + sin(rad) * squad->spread_distance;
                positions[i][2] = leader_pos[2];
            }
            break;
            
        default:
            // No formation - maintain current positions
            for (i = 0; i < squad->num_members; i++) {
                VectorCopy(squad->members[i]->position, positions[i]);
            }
            break;
    }
}

/*
==================
Team_CoordinateActions
==================
*/
void Team_CoordinateActions(team_coordinator_t *coordinator) {
    float current_time;
    
    if (!coordinator || !team_global.team_coordination->integer) {
        return;
    }
    
    current_time = level.time * 0.001f;
    
    // Check update interval
    if (current_time - coordinator->last_coordination_time < COORDINATION_UPDATE_INTERVAL * 0.001f) {
        return;
    }
    
    // Update all members
    for (int i = 0; i < coordinator->num_members; i++) {
        Team_UpdateMember(coordinator, coordinator->members[i].client_id);
    }
    
    // Process messages
    Team_ProcessMessages(coordinator);
    
    // Update strategic plan
    if (coordinator->strategic_planner) {
        if (Strategy_NeedsReplanning(coordinator->strategic_planner)) {
            Strategy_CreatePlan(coordinator->strategic_planner);
        }
        Strategy_UpdatePlan(coordinator->strategic_planner);
    }
    
    // Distribute objectives to squads
    Team_DistributeObjectives(coordinator);
    
    // Update squad states
    for (int i = 0; i < coordinator->num_squads; i++) {
        squad_t *squad = &coordinator->squads[i];
        
        // Maintain formation if enabled
        if (team_global.team_formations->integer) {
            Team_MaintainFormation(squad);
        }
        
        // Execute squad tactics
        switch (squad->state) {
            case SQUAD_STATE_MOVING:
                Team_SynchronizeMovement(coordinator);
                break;
                
            case SQUAD_STATE_ENGAGING:
                if (coordinator->tactics.coordinated_attack) {
                    Team_CoordinateAttack(coordinator, squad->members[0]->current_target);
                }
                break;
                
            case SQUAD_STATE_DEFENDING:
                Team_ProvideSuppression(coordinator, squad, squad->defend_position);
                break;
                
            case SQUAD_STATE_FLANKING:
                Team_ExecuteFlanking(coordinator, squad, squad->attack_vector);
                break;
                
            default:
                break;
        }
    }
    
    // Evaluate and adjust tactics
    Team_EvaluatePerformance(coordinator);
    
    coordinator->last_coordination_time = current_time;
}

/*
==================
Team_DistributeObjectives
==================
*/
void Team_DistributeObjectives(team_coordinator_t *coordinator) {
    int squad_idx = 0;
    
    if (!coordinator || !coordinator->strategic_planner) {
        return;
    }
    
    // Assign objectives from strategic plan to squads
    for (int i = 0; i < coordinator->strategic_planner->current_plan.num_objectives; i++) {
        tactical_objective_t *obj = &coordinator->strategic_planner->current_plan.objectives[i];
        
        if (!obj->active || obj->completed) {
            continue;
        }
        
        // Find available squad
        if (squad_idx < coordinator->num_squads) {
            squad_t *squad = &coordinator->squads[squad_idx];
            squad->objective = obj;
            squad->objective_progress = 0;
            
            // Set squad state based on objective
            if (obj->parent_goal) {
                switch (obj->parent_goal->type) {
                    case GOAL_TYPE_ELIMINATE:
                        squad->state = SQUAD_STATE_ENGAGING;
                        break;
                    case GOAL_TYPE_DEFEND:
                        squad->state = SQUAD_STATE_DEFENDING;
                        VectorCopy(obj->position, squad->defend_position);
                        break;
                    case GOAL_TYPE_CAPTURE:
                        squad->state = SQUAD_STATE_MOVING;
                        VectorCopy(obj->position, squad->movement_destination);
                        break;
                    default:
                        squad->state = SQUAD_STATE_MOVING;
                        break;
                }
            }
            
            squad_idx++;
        }
    }
}

/*
==================
Team_MaintainFormation
==================
*/
void Team_MaintainFormation(squad_t *squad) {
    vec3_t desired_positions[MAX_SQUAD_SIZE];
    vec3_t move_dir;
    float distance;
    
    if (!squad || squad->formation == FORMATION_NONE) {
        return;
    }
    
    // Calculate desired positions
    Team_CalculateFormationPositions(squad, desired_positions);
    
    // Move members towards formation positions
    for (int i = 0; i < squad->num_members; i++) {
        team_member_t *member = squad->members[i];
        
        VectorSubtract(desired_positions[i], member->position, move_dir);
        distance = VectorLength(move_dir);
        
        // Check if out of formation
        if (distance > squad->spread_distance * 0.3f) {
            VectorNormalize(move_dir);
            VectorCopy(move_dir, member->assigned_position);
            
            // Mark as needing to move
            squad->cohesion *= 0.98f; // Reduce cohesion when out of formation
        }
    }
    
    // Restore cohesion over time
    squad->cohesion = MIN(squad->cohesion + 0.01f, 1.0f);
}

/*
==================
Team_CoordinateAttack
==================
*/
void Team_CoordinateAttack(team_coordinator_t *coordinator, int target_id) {
    vec3_t target_pos;
    gentity_t *target;
    int attackers_assigned = 0;
    
    if (!coordinator || target_id < 0) {
        return;
    }
    
    target = &g_entities[target_id];
    if (!target->inuse) {
        return;
    }
    
    VectorCopy(target->s.pos.trBase, target_pos);
    
    // Coordinate fire concentration
    for (int i = 0; i < coordinator->num_members; i++) {
        team_member_t *member = &coordinator->members[i];
        
        if (!member->alive || member->in_combat) {
            continue;
        }
        
        // Assign target
        member->current_target = target_id;
        
        // Position for crossfire if multiple attackers
        if (attackers_assigned > 0) {
            float angle = (attackers_assigned * 90) % 360;
            vec3_t attack_pos;
            
            attack_pos[0] = target_pos[0] + cos(DEG2RAD(angle)) * 300;
            attack_pos[1] = target_pos[1] + sin(DEG2RAD(angle)) * 300;
            attack_pos[2] = target_pos[2];
            
            VectorCopy(attack_pos, member->assigned_position);
        }
        
        attackers_assigned++;
        
        // Limit coordinated attackers
        if (attackers_assigned >= 3) {
            break;
        }
    }
    
    // Send attack command
    team_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_TYPE_COMMAND;
    msg.command = CMD_ATTACK;
    msg.target_entity = target_id;
    msg.priority = 1.0f;
    Team_BroadcastCommand(coordinator, CMD_ATTACK);
}

/*
==================
Team_ExecuteFlanking
==================
*/
void Team_ExecuteFlanking(team_coordinator_t *coordinator, squad_t *squad, const vec3_t target) {
    vec3_t left_flank, right_flank;
    vec3_t direction;
    int half_squad;
    
    if (!coordinator || !squad || squad->num_members < 2) {
        return;
    }
    
    // Calculate flanking vectors
    VectorSubtract(target, squad->rally_point, direction);
    direction[2] = 0;
    VectorNormalize(direction);
    
    // Left flank
    left_flank[0] = target[0] - direction[1] * 200;
    left_flank[1] = target[1] + direction[0] * 200;
    left_flank[2] = target[2];
    
    // Right flank
    right_flank[0] = target[0] + direction[1] * 200;
    right_flank[1] = target[1] - direction[0] * 200;
    right_flank[2] = target[2];
    
    // Split squad for pincer movement
    half_squad = squad->num_members / 2;
    
    for (int i = 0; i < squad->num_members; i++) {
        if (i < half_squad) {
            VectorCopy(left_flank, squad->members[i]->assigned_position);
        } else {
            VectorCopy(right_flank, squad->members[i]->assigned_position);
        }
    }
    
    squad->state = SQUAD_STATE_FLANKING;
}

/*
==================
Team_ProvideSuppression
==================
*/
void Team_ProvideSuppression(team_coordinator_t *coordinator, squad_t *squad, const vec3_t area) {
    vec3_t suppress_pos;
    int suppressor_count = 0;
    
    if (!coordinator || !squad) {
        return;
    }
    
    // Assign suppression roles
    for (int i = 0; i < squad->num_members; i++) {
        team_member_t *member = squad->members[i];
        
        if (!member->alive) {
            continue;
        }
        
        // Support and heavy weapons provide suppression
        if (member->role == TEAM_ROLE_SUPPORT || member->weapon == WP_MACHINEGUN) {
            // Position for suppression
            suppress_pos[0] = area[0] + crandom() * 50;
            suppress_pos[1] = area[1] + crandom() * 50;
            suppress_pos[2] = area[2];
            
            VectorCopy(suppress_pos, member->assigned_position);
            suppressor_count++;
        } else if (suppressor_count > 0) {
            // Others advance under suppression
            member->covering_member = squad->members[i - 1]->client_id;
        }
    }
    
    coordinator->tactics.suppression_active = (suppressor_count > 0);
}

/*
==================
Team_SendMessage
==================
*/
void Team_SendMessage(team_coordinator_t *coordinator, team_message_t *message) {
    if (!coordinator || !message || !team_global.team_communication->integer) {
        return;
    }
    
    if (coordinator->message_count >= MAX_TEAM_MESSAGES) {
        // Remove oldest message
        coordinator->message_head = (coordinator->message_head + 1) % MAX_TEAM_MESSAGES;
        coordinator->message_count--;
    }
    
    // Add message to queue
    memcpy(&coordinator->message_queue[coordinator->message_tail], message, sizeof(team_message_t));
    message->timestamp = level.time * 0.001f;
    
    coordinator->message_tail = (coordinator->message_tail + 1) % MAX_TEAM_MESSAGES;
    coordinator->message_count++;
}

/*
==================
Team_BroadcastCommand
==================
*/
void Team_BroadcastCommand(team_coordinator_t *coordinator, command_type_t command) {
    team_message_t msg;
    
    if (!coordinator) {
        return;
    }
    
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_TYPE_COMMAND;
    msg.command = command;
    msg.sender_id = coordinator->commander_id;
    msg.recipient_id = -1; // Broadcast
    msg.priority = 0.8f;
    
    Team_SendMessage(coordinator, &msg);
}

/*
==================
Team_ProcessMessages
==================
*/
void Team_ProcessMessages(team_coordinator_t *coordinator) {
    team_message_t *msg;
    
    if (!coordinator || coordinator->message_count == 0) {
        return;
    }
    
    // Process up to 5 messages per frame
    for (int i = 0; i < 5 && coordinator->message_count > 0; i++) {
        msg = &coordinator->message_queue[coordinator->message_head];
        
        // Process based on message type
        switch (msg->type) {
            case MSG_TYPE_COMMAND:
                // Execute command
                if (msg->recipient_id == -1 || msg->recipient_id < MAX_CLIENTS) {
                    // Handle command for recipient(s)
                    Com_DPrintf("Team %d: Command %d from %d\n", 
                               coordinator->team_id, msg->command, msg->sender_id);
                }
                break;
                
            case MSG_TYPE_REQUEST:
                // Handle support requests
                if (msg->command == CMD_PROVIDE_COVER) {
                    Team_RequestSupport(coordinator, msg->sender_id, msg->position);
                }
                break;
                
            case MSG_TYPE_ALERT:
                // Handle threat alerts
                Team_RespondToThreat(coordinator, msg->position, 2);
                break;
                
            default:
                break;
        }
        
        // Remove processed message
        coordinator->message_head = (coordinator->message_head + 1) % MAX_TEAM_MESSAGES;
        coordinator->message_count--;
    }
}

/*
==================
Team_RespondToThreat
==================
*/
void Team_RespondToThreat(team_coordinator_t *coordinator, const vec3_t threat_pos, int threat_level) {
    squad_t *nearest_squad = NULL;
    float min_distance = 999999;
    
    if (!coordinator) {
        return;
    }
    
    // Find nearest squad to threat
    for (int i = 0; i < coordinator->num_squads; i++) {
        squad_t *squad = &coordinator->squads[i];
        
        if (squad->num_members == 0) {
            continue;
        }
        
        float dist = Distance(squad->members[0]->position, threat_pos);
        if (dist < min_distance) {
            min_distance = dist;
            nearest_squad = squad;
        }
    }
    
    if (!nearest_squad) {
        return;
    }
    
    // Respond based on threat level
    if (threat_level >= 3) {
        // High threat - multiple squads respond
        for (int i = 0; i < coordinator->num_squads; i++) {
            coordinator->squads[i].state = SQUAD_STATE_ENGAGING;
            VectorCopy(threat_pos, coordinator->squads[i].attack_vector);
        }
    } else if (threat_level >= 2) {
        // Medium threat - nearest squad responds
        nearest_squad->state = SQUAD_STATE_ENGAGING;
        VectorCopy(threat_pos, nearest_squad->attack_vector);
    } else {
        // Low threat - investigate
        nearest_squad->state = SQUAD_STATE_MOVING;
        VectorCopy(threat_pos, nearest_squad->movement_destination);
    }
}

/*
==================
Team_EvaluatePerformance
==================
*/
void Team_EvaluatePerformance(team_coordinator_t *coordinator) {
    float total_effectiveness = 0;
    int alive_count = 0;
    
    if (!coordinator) {
        return;
    }
    
    // Calculate team effectiveness
    for (int i = 0; i < coordinator->num_members; i++) {
        team_member_t *member = &coordinator->members[i];
        
        if (member->alive) {
            alive_count++;
            
            // Calculate member effectiveness
            float health_factor = member->health / 100.0f;
            float armor_factor = member->armor / 100.0f;
            float ammo_factor = 1.0f; // Simplified
            
            member->effectiveness = (health_factor + armor_factor * 0.5f + ammo_factor) / 2.5f;
            total_effectiveness += member->effectiveness;
        }
    }
    
    if (alive_count > 0) {
        coordinator->team_effectiveness = total_effectiveness / alive_count;
    } else {
        coordinator->team_effectiveness = 0;
    }
    
    // Calculate coordination quality
    float avg_cohesion = 0;
    for (int i = 0; i < coordinator->num_squads; i++) {
        avg_cohesion += coordinator->squads[i].cohesion;
    }
    
    if (coordinator->num_squads > 0) {
        coordinator->coordination_quality = avg_cohesion / coordinator->num_squads;
    }
    
    // Adjust tactics based on performance
    if (coordinator->team_effectiveness < 0.3f) {
        // Poor performance - defensive tactics
        coordinator->tactics.coordinated_attack = qfalse;
        coordinator->tactics.risk_tolerance = 0.2f;
    } else if (coordinator->team_effectiveness > 0.7f) {
        // Good performance - aggressive tactics
        coordinator->tactics.coordinated_attack = qtrue;
        coordinator->tactics.risk_tolerance = 0.7f;
    }
}

/*
==================
Team_SynchronizeMovement
==================
*/
void Team_SynchronizeMovement(team_coordinator_t *coordinator) {
    vec3_t center_of_mass, average_velocity;
    vec3_t formation_positions[MAX_TEAM_SIZE];
    int active_members = 0;
    
    if (!coordinator || coordinator->num_members == 0) {
        return;
    }
    
    // Calculate center of mass and average velocity
    VectorClear(center_of_mass);
    VectorClear(average_velocity);
    
    for (int i = 0; i < coordinator->num_members; i++) {
        team_member_t *member = &coordinator->members[i];
        if (member->alive) {
            VectorAdd(center_of_mass, member->position, center_of_mass);
            VectorAdd(average_velocity, member->velocity, average_velocity);
            active_members++;
        }
    }
    
    if (active_members == 0) {
        return;
    }
    
    // Calculate averages
    VectorScale(center_of_mass, 1.0f / active_members, center_of_mass);
    VectorScale(average_velocity, 1.0f / active_members, average_velocity);
    
    // Generate formation positions based on current formation
    float spacing = 100.0f;  // Units between members
    float angle_step = 360.0f / active_members;
    
    // Use simple line formation for now
    formation_type_t formation = FORMATION_LINE;
    switch (formation) {
        case FORMATION_LINE:
            // Line formation perpendicular to movement
            for (int i = 0; i < active_members; i++) {
                float offset = (i - active_members/2) * spacing;
                VectorCopy(center_of_mass, formation_positions[i]);
                formation_positions[i][0] += offset;
            }
            break;
            
        case FORMATION_WEDGE:
            // V-shaped formation
            for (int i = 0; i < active_members; i++) {
                float row = i / 2;
                float side = (i % 2) ? 1 : -1;
                VectorCopy(center_of_mass, formation_positions[i]);
                formation_positions[i][0] += side * row * spacing * 0.7f;
                formation_positions[i][1] -= row * spacing;
            }
            break;
            
        case FORMATION_CIRCLE:
            // Circular formation
            for (int i = 0; i < active_members; i++) {
                float angle = DEG2RAD(i * angle_step);
                VectorCopy(center_of_mass, formation_positions[i]);
                formation_positions[i][0] += cos(angle) * spacing * 1.5f;
                formation_positions[i][1] += sin(angle) * spacing * 1.5f;
            }
            break;
            
        case FORMATION_DIAMOND:
            // Diamond formation
            if (active_members >= 4) {
                // Front
                VectorCopy(center_of_mass, formation_positions[0]);
                formation_positions[0][1] += spacing;
                
                // Sides
                VectorCopy(center_of_mass, formation_positions[1]);
                formation_positions[1][0] -= spacing;
                
                VectorCopy(center_of_mass, formation_positions[2]);
                formation_positions[2][0] += spacing;
                
                // Back
                VectorCopy(center_of_mass, formation_positions[3]);
                formation_positions[3][1] -= spacing;
                
                // Fill remaining positions
                for (int i = 4; i < active_members; i++) {
                    VectorCopy(center_of_mass, formation_positions[i]);
                }
            } else {
                // Fallback to simple spread
                for (int i = 0; i < active_members; i++) {
                    VectorCopy(center_of_mass, formation_positions[i]);
                }
            }
            break;
            
        default:
            // Loose formation - maintain relative positions
            for (int i = 0; i < active_members; i++) {
                VectorCopy(coordinator->members[i].position, formation_positions[i]);
            }
            break;
    }
    
    // Apply synchronized movement to each member
    int formation_index = 0;
    for (int i = 0; i < coordinator->num_members; i++) {
        team_member_t *member = &coordinator->members[i];
        
        if (!member->alive) {
            continue;
        }
        
        // Calculate desired movement vector
        vec3_t desired_move;
        VectorSubtract(formation_positions[formation_index], member->position, desired_move);
        
        // Apply speed matching
        vec3_t speed_adjustment;
        VectorSubtract(average_velocity, member->velocity, speed_adjustment);
        VectorScale(speed_adjustment, 0.1f, speed_adjustment);  // Gradual adjustment
        
        // Combine formation movement with speed matching
        vec3_t total_adjustment;
        VectorAdd(desired_move, speed_adjustment, total_adjustment);
        
        // Store in assigned position as movement target
        VectorAdd(member->position, total_adjustment, member->assigned_position);
        
        formation_index++;
    }
    
    // Update coordination metrics
    coordinator->coordination_quality = 0;
    for (int i = 0; i < coordinator->num_members - 1; i++) {
        if (!coordinator->members[i].alive) continue;
        
        for (int j = i + 1; j < coordinator->num_members; j++) {
            if (!coordinator->members[j].alive) continue;
            
            vec3_t dist;
            VectorSubtract(coordinator->members[i].position, coordinator->members[j].position, dist);
            float separation = VectorLength(dist);
            
            // Good coordination if maintaining proper spacing
            if (separation > spacing * 0.8f && separation < spacing * 1.2f) {
                coordinator->coordination_quality += 0.1f;
            }
        }
    }
    
    coordinator->coordination_quality = CLAMP(coordinator->coordination_quality, 0, 1);
}

/*
==================
Team_RequestSupport
==================
*/
void Team_RequestSupport(team_coordinator_t *coordinator, int requester_id, const vec3_t position) {
    team_member_t *requester = NULL;
    int best_supporter = -1;
    float best_score = -1;
    
    if (!coordinator || requester_id < 0 || requester_id >= coordinator->num_members) {
        return;
    }
    
    requester = &coordinator->members[requester_id];
    
    // Find best available supporter
    for (int i = 0; i < coordinator->num_members; i++) {
        if (i == requester_id) {
            continue;  // Can't support self
        }
        
        team_member_t *candidate = &coordinator->members[i];
        
        // Check if candidate can provide support
        if (!candidate->alive || candidate->health < 30) {
            continue;  // Too weak to help
        }
        
        // Calculate support score based on:
        // - Distance to requester
        // - Health/effectiveness
        // - Current engagement status
        vec3_t dist_vec;
        VectorSubtract(position, candidate->position, dist_vec);
        float distance = VectorLength(dist_vec);
        
        // Skip if too far away
        if (distance > 2000) {
            continue;
        }
        
        float distance_score = 1.0f - (distance / 2000.0f);
        float health_score = candidate->health / 100.0f;
        float availability_score = candidate->in_combat ? 0.5f : 1.0f;
        
        float total_score = distance_score * 0.5f + health_score * 0.3f + availability_score * 0.2f;
        
        if (total_score > best_score) {
            best_score = total_score;
            best_supporter = i;
        }
    }
    
    // Assign support if found
    if (best_supporter >= 0) {
        team_member_t *supporter = &coordinator->members[best_supporter];
        
        // Assign support position directly
        VectorCopy(position, supporter->assigned_position);
        supporter->covering_member = requester_id;
        // Support window would be tracked here
        // support_order.issued_time = level.time * 0.001f;
        // support_order.expiry_time = level.time * 0.001f + 30.0f;  // 30 second support window
        
        // Mark supporter as providing support
        // if (coordinator->num_orders < MAX_ORDERS) {
        //     coordinator->orders[coordinator->num_orders] = support_order;
        //     coordinator->num_orders++;
        //     
        //     supporter->current_order = ORDER_SUPPORT;
        //     VectorCopy(position, supporter->objective_position);
        //     
        //     coordinator->support_requests++;
        //     if (best_score > 0.6f) {
        //         coordinator->successful_supports++;
        //     }
        // }
    }
    
    // Broadcast support request to nearby squads
    for (int i = 0; i < coordinator->num_squads; i++) {
        squad_t *squad = &coordinator->squads[i];
        
        // Check if squad is close enough to help
        if (squad->num_members > 0) {
            vec3_t squad_dist;
            VectorSubtract(position, squad->members[0]->position, squad_dist);
            float squad_distance = VectorLength(squad_dist);
            
            if (squad_distance < 1000 && squad->members[0]->client_id != requester_id) {
                // Squad can potentially help
                // squad->priority_target = requester_id;
                // squad->objective = OBJECTIVE_SUPPORT;
                
                // Increase squad urgency
                if (requester->health < 50) {
                    squad->formation = FORMATION_SPREAD;  // Break formation for faster response
                }
            }
        }
    }
    
    // Log support request
    Com_DPrintf("Team member %d requested support at position (%.0f, %.0f, %.0f)\n",
                requester_id, position[0], position[1], position[2]);
    
    if (best_supporter >= 0) {
        Com_DPrintf("Team member %d responding to support request (score: %.2f)\n",
                    best_supporter, best_score);
    } else {
        Com_DPrintf("No team member available to provide support\n");
    }
}

