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

#include "ai_perception.h"
#include "../game_interface.h"
#include "../ai_system.h"
#include "../game_entities.h"
#include "../ai_constants.h"
#include "../../../engine/core/qcommon.h"
#include <math.h>
#include <string.h>

// Temporary stub for bg_itemlist - normally defined in game module
#ifndef GAME_IMPLEMENTATION
gitem_t bg_itemlist[1] = {{0}};
#endif

static struct {
    qboolean initialized;
    cvar_t *perception_debug;
    cvar_t *perception_range;
    cvar_t *perception_fov;
} perception_global;

/*
==================
Perception_Init
==================
*/
void Perception_Init(void) {
    if (perception_global.initialized) {
        return;
    }
    
    memset(&perception_global, 0, sizeof(perception_global));
    
    perception_global.perception_debug = Cvar_Get("ai_perception_debug", "0", 0);
    perception_global.perception_range = Cvar_Get("ai_perception_range", "2000", CVAR_ARCHIVE);
    perception_global.perception_fov = Cvar_Get("ai_perception_fov", "120", CVAR_ARCHIVE);
    
    perception_global.initialized = qtrue;
    
    Com_Printf("Perception System Initialized\n");
}

/*
==================
Perception_Shutdown
==================
*/
void Perception_Shutdown(void) {
    if (!perception_global.initialized) {
        return;
    }
    
    perception_global.initialized = qfalse;
    Com_Printf("Perception System Shutdown\n");
}

/*
==================
Perception_Create
==================
*/
perception_system_t *Perception_Create(void) {
    perception_system_t *perception;
    
    perception = (perception_system_t *)Z_Malloc(sizeof(perception_system_t));
    memset(perception, 0, sizeof(perception_system_t));
    
    // Set default filter values
    perception->filter.max_vision_range = perception_global.perception_range->value;
    perception->filter.fov_angle = perception_global.perception_fov->value;
    perception->filter.peripheral_sensitivity = 0.5f;
    perception->filter.motion_detection_threshold = 50.0f;
    perception->filter.sound_sensitivity = 1.0f;
    perception->filter.use_fog_of_war = qtrue;
    perception->filter.simulate_distractions = qfalse;
    
    // Set memory decay
    perception->memory.memory_decay_rate = 0.1f; // Forget 10% per second
    
    return perception;
}

/*
==================
Perception_Destroy
==================
*/
void Perception_Destroy(perception_system_t *perception) {
    if (!perception) {
        return;
    }
    
    Z_Free(perception);
}

/*
==================
Perception_Update
==================
*/
void Perception_Update(perception_system_t *perception, int client_num) {
    float current_time;
    int start_time;
    
    if (!perception) {
        return;
    }
    
    current_time = level.time * 0.001f;
    start_time = Sys_Milliseconds();
    
    // Update self state first
    Perception_UpdateSelfState(perception, client_num);
    
    // Update vision
    if (current_time - perception->last_vision_update > PERCEPTION_UPDATE_RATE * 0.001f) {
        Perception_UpdateVision(perception, client_num);
        perception->last_vision_update = current_time;
    }
    
    // Update hearing
    if (current_time - perception->last_hearing_update > PERCEPTION_UPDATE_RATE * 0.5f * 0.001f) {
        Perception_UpdateHearing(perception, client_num);
        perception->last_hearing_update = current_time;
    }
    
    // Update spatial awareness
    Perception_UpdateSpatialAwareness(perception);
    
    // Update threat assessment
    Perception_UpdateThreatAssessment(perception);
    
    // Update memory
    Perception_UpdateMemory(perception);
    
    // Process damage events
    Perception_ProcessDamageEvents(perception, client_num);
    
    perception->last_perception_time = current_time;
    perception->perception_time_ms = Sys_Milliseconds() - start_time;
}

/*
==================
Perception_UpdateSelfState
==================
*/
void Perception_UpdateSelfState(perception_system_t *perception, int client_num) {
    gentity_t *self;
    
    if (!perception || client_num < 0 || client_num >= MAX_CLIENTS) {
        return;
    }
    
    self = &g_entities[client_num];
    if (!self || !self->inuse || !self->client) {
        return;
    }
    
    // Update position and movement
    VectorCopy(self->s.pos.trBase, perception->self_state.position);
    VectorCopy(self->s.pos.trDelta, perception->self_state.velocity);
    VectorCopy(self->client->ps.viewangles, perception->self_state.angles);
    
    // Update stats
    perception->self_state.health = self->health;
    perception->self_state.armor = self->client->ps.stats[STAT_ARMOR];
    perception->self_state.weapon = self->client->ps.weapon;
    perception->self_state.team = self->client->sess.sessionTeam;
    
    // Update ammo
    for (int i = 0; i < MAX_WEAPONS; i++) {
        perception->self_state.ammo[i] = self->client->ps.ammo[i];
    }
    
    // Update movement state
    perception->self_state.speed = VectorLength(perception->self_state.velocity);
    perception->self_state.on_ground = (self->s.groundEntityNum != ENTITYNUM_NONE);
    perception->self_state.in_water = (self->waterlevel > 0);
    perception->self_state.in_air = !perception->self_state.on_ground && !perception->self_state.in_water;
}

/*
==================
Perception_UpdateVision
==================
*/
void Perception_UpdateVision(perception_system_t *perception, int client_num) {
    if (!perception) {
        return;
    }
    
    // Clear previous visible entities
    perception->num_visible_entities = 0;
    perception->num_visible_enemies = 0;
    perception->num_visible_allies = 0;
    perception->num_visible_items = 0;
    perception->rays_cast = 0;
    
    // Scan for entities
    Perception_ScanForEntities(perception, client_num);
    
    // Merge with memory
    Perception_MergeWithMemory(perception);
    
    // Apply fog of war if enabled
    if (perception->filter.use_fog_of_war) {
        Perception_ApplyFogOfWar(perception);
    }
}

/*
==================
Perception_ScanForEntities
==================
*/
void Perception_ScanForEntities(perception_system_t *perception, int client_num) {
    int i;
    gentity_t *ent;
    entity_info_t *info;
    vec3_t dir;
    float dist;
    
    if (!perception) {
        return;
    }
    
    // Scan all entities
    for (i = 0; i < MAX_GENTITIES && perception->num_visible_entities < MAX_VISIBLE_ENTITIES; i++) {
        ent = &g_entities[i];
        
        if (!ent->inuse || i == client_num) {
            continue;
        }
        
        // Calculate distance
        VectorSubtract(ent->s.pos.trBase, perception->self_state.position, dir);
        dist = VectorLength(dir);
        
        // Check range
        if (dist > perception->filter.max_vision_range) {
            continue;
        }
        
        // Check if in FOV
        VectorNormalize(dir);
        if (!Perception_IsInFOV(perception->self_state.angles, dir, perception->filter.fov_angle)) {
            // Check peripheral vision
            if (!Perception_IsInFOV(perception->self_state.angles, dir, PERIPHERAL_VISION_ANGLE)) {
                continue;
            }
            
            // Peripheral vision is less reliable
            if (random() > perception->filter.peripheral_sensitivity) {
                continue;
            }
        }
        
        // Check line of sight
        if (!Perception_HasLineOfSight(perception->self_state.position, ent->s.pos.trBase)) {
            perception->rays_cast++;
            continue;
        }
        perception->rays_cast++;
        
        // Add to visible entities
        info = &perception->visible_entities[perception->num_visible_entities];
        memset(info, 0, sizeof(entity_info_t));
        
        info->entity_num = i;
        VectorCopy(ent->s.pos.trBase, info->position);
        VectorCopy(ent->s.pos.trDelta, info->velocity);
        VectorCopy(ent->s.angles, info->angles);
        info->distance = dist;
        info->visible = qtrue;
        info->last_seen_time = level.time * 0.001f;
        
        // Determine entity type and properties
        if (ent->client) {
            info->entity_type = ENTITY_PLAYER;
            info->health = ent->health;
            info->armor = ent->client->ps.stats[STAT_ARMOR];
            info->weapon = ent->client->ps.weapon;
            info->team = ent->client->sess.sessionTeam;
            
            // Check if enemy or ally
            if (perception->self_state.team != TEAM_SPECTATOR) {
                if (info->team != perception->self_state.team) {
                    info->is_enemy = qtrue;
                    perception->num_visible_enemies++;
                } else {
                    info->is_ally = qtrue;
                    perception->num_visible_allies++;
                }
            }
        } else if (ent->s.eType == ET_ITEM) {
            info->entity_type = ENTITY_ITEM;
            info->item_type = ent->item - bg_itemlist;
            perception->num_visible_items++;
            
            // Categorize item
            if (ent->item->giType == IT_WEAPON) {
                info->entity_type = ENTITY_WEAPON;
            } else if (ent->item->giType == IT_HEALTH) {
                info->entity_type = ENTITY_HEALTH;
            } else if (ent->item->giType == IT_ARMOR) {
                info->entity_type = ENTITY_ARMOR;
            } else if (ent->item->giType == IT_POWERUP) {
                info->entity_type = ENTITY_POWERUP;
            }
        } else if (ent->s.eType == ET_MISSILE) {
            info->entity_type = ENTITY_PROJECTILE;
            info->owner = ent->r.ownerNum;
            
            // Predict impact
            Perception_PredictProjectileImpact(perception, info);
        }
        
        // Calculate visibility confidence
        info->visibility_confidence = Perception_CalculateVisibilityScore(perception, info);
        
        // Evaluate threat level
        info->threat_level = Perception_EvaluateThreat(perception, info);
        
        // Remember entity
        Perception_RememberEntity(perception, info);
        
        perception->num_visible_entities++;
    }
}

/*
==================
Perception_IsInFOV
==================
*/
qboolean Perception_IsInFOV(const vec3_t view_angles, const vec3_t to_target, float fov) {
    vec3_t forward, angles;
    float dot;
    
    // Get forward vector from view angles
    AngleVectors(view_angles, forward, NULL, NULL);
    
    // Calculate dot product
    dot = DotProduct(forward, to_target);
    
    // Check if within FOV
    float fov_cos = cos(DEG2RAD(fov * 0.5f));
    return (dot > fov_cos);
}

/*
==================
Perception_HasLineOfSight
==================
*/
qboolean Perception_HasLineOfSight(const vec3_t from, const vec3_t to) {
    trace_t trace;
    vec3_t start, end;
    
    // Adjust for eye height
    VectorCopy(from, start);
    start[2] += 56; // Approximate eye height
    
    VectorCopy(to, end);
    
    trap_Trace(&trace, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SHOT);
    
    return (trace.fraction >= 0.95f);
}

/*
==================
Perception_CalculateVisibilityScore
==================
*/
float Perception_CalculateVisibilityScore(perception_system_t *perception, entity_info_t *entity) {
    float score = 1.0f;
    
    if (!perception || !entity) {
        return 0;
    }
    
    // Distance factor
    float dist_factor = 1.0f - (entity->distance / perception->filter.max_vision_range);
    score *= dist_factor;
    
    // Motion detection
    float speed = VectorLength(entity->velocity);
    if (speed > perception->filter.motion_detection_threshold) {
        score *= 1.2f; // Moving targets are easier to spot
    }
    
    // Size factor (simplified - would need actual bounds)
    if (entity->entity_type == ENTITY_PLAYER) {
        score *= 1.0f;
    } else if (entity->entity_type == ENTITY_ITEM) {
        score *= 0.7f; // Items are smaller
    }
    
    // Lighting (simplified - would need actual lighting data)
    score *= 0.8f; // Assume moderate lighting
    
    return CLAMP(score, 0, 1);
}

/*
==================
Perception_UpdateSpatialAwareness
==================
*/
void Perception_UpdateSpatialAwareness(perception_system_t *perception) {
    trace_t trace;
    vec3_t end;
    int i;
    float min_wall_dist = 999999;
    
    if (!perception) {
        return;
    }
    
    // Check surrounding space
    for (i = 0; i < 8; i++) {
        float angle = i * 45;
        float rad = DEG2RAD(angle);
        
        VectorCopy(perception->self_state.position, end);
        end[0] += cos(rad) * 200;
        end[1] += sin(rad) * 200;
        
        trap_Trace(&trace, perception->self_state.position, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID);
        
        float dist = trace.fraction * 200;
        if (dist < min_wall_dist) {
            min_wall_dist = dist;
        }
    }
    
    perception->spatial.nearest_wall_distance = min_wall_dist;
    perception->spatial.open_space_ratio = min_wall_dist / 200.0f;
    
    // Check if cornered
    perception->spatial.cornered = (perception->spatial.open_space_ratio < 0.3f);
    
    // Find escape direction
    if (perception->spatial.cornered) {
        Perception_FindEscapeRoutes(perception);
    }
    
    // Check height advantage
    perception->spatial.has_high_ground = qfalse;
    for (i = 0; i < perception->num_visible_enemies; i++) {
        entity_info_t *enemy = &perception->visible_entities[i];
        if (enemy->is_enemy) {
            float height_diff = perception->self_state.position[2] - enemy->position[2];
            if (height_diff > 50) {
                perception->spatial.has_high_ground = qtrue;
                perception->spatial.height_advantage = height_diff;
                break;
            }
        }
    }
}

/*
==================
Perception_UpdateThreatAssessment
==================
*/
void Perception_UpdateThreatAssessment(perception_system_t *perception) {
    int i;
    float max_threat = 0;
    float second_threat = 0;
    
    if (!perception) {
        return;
    }
    
    perception->threats.primary_threat = -1;
    perception->threats.secondary_threat = -1;
    perception->threats.threat_count = 0;
    perception->threats.overall_threat_level = 0;
    VectorClear(perception->threats.threat_center);
    
    // Identify threats
    for (i = 0; i < perception->num_visible_entities; i++) {
        entity_info_t *entity = &perception->visible_entities[i];
        
        if (!entity->is_enemy) {
            continue;
        }
        
        perception->threats.threat_count++;
        
        // Update threat center
        VectorAdd(perception->threats.threat_center, entity->position, perception->threats.threat_center);
        
        // Track primary and secondary threats
        if (entity->threat_level > max_threat) {
            second_threat = max_threat;
            perception->threats.secondary_threat = perception->threats.primary_threat;
            
            max_threat = entity->threat_level;
            perception->threats.primary_threat = entity->entity_num;
        } else if (entity->threat_level > second_threat) {
            second_threat = entity->threat_level;
            perception->threats.secondary_threat = entity->entity_num;
        }
        
        perception->threats.overall_threat_level += entity->threat_level;
    }
    
    // Average threat center
    if (perception->threats.threat_count > 0) {
        VectorScale(perception->threats.threat_center, 
                   1.0f / perception->threats.threat_count, 
                   perception->threats.threat_center);
    }
    
    // Check tactical situation
    perception->threats.outnumbered = (perception->threats.threat_count > 2);
    perception->threats.under_fire = (perception->num_damage_events > 0);
    
    // Check if flanked
    if (perception->threats.threat_count >= 2) {
        vec3_t dir1, dir2;
        VectorSubtract(perception->visible_entities[0].position, perception->self_state.position, dir1);
        VectorSubtract(perception->visible_entities[1].position, perception->self_state.position, dir2);
        VectorNormalize(dir1);
        VectorNormalize(dir2);
        
        float dot = DotProduct(dir1, dir2);
        perception->threats.flanked = (dot < -0.3f); // Enemies at >90 degree angle
    }
}

/*
==================
Perception_EvaluateThreat
==================
*/
float Perception_EvaluateThreat(perception_system_t *perception, entity_info_t *entity) {
    float threat = 0;
    
    if (!perception || !entity) {
        return 0;
    }
    
    if (entity->entity_type == ENTITY_PLAYER && entity->is_enemy) {
        // Base threat on health and weapon
        threat = 50;
        
        // Distance factor (closer = more dangerous)
        float dist_factor = 1.0f - (entity->distance / 1000.0f);
        threat *= (1.0f + dist_factor);
        
        // Weapon threat
        threat += entity->weapon * 5;
        
        // Health factor (healthier enemies are more dangerous)
        threat *= (entity->health / 100.0f);
        
        // Visibility factor
        threat *= entity->visibility_confidence;
    } else if (entity->entity_type == ENTITY_PROJECTILE) {
        // Projectile threat based on proximity and time to impact
        if (entity->impact_time < 2.0f && entity->impact_time > 0) {
            threat = 100 / MAX(entity->impact_time, 0.1f);
        }
    }
    
    return threat;
}

/*
==================
Perception_PredictProjectileImpact
==================
*/
void Perception_PredictProjectileImpact(perception_system_t *perception, entity_info_t *projectile) {
    vec3_t trajectory;
    float speed;
    float time_to_impact;
    
    if (!perception || !projectile) {
        return;
    }
    
    speed = VectorLength(projectile->velocity);
    if (speed < 1) {
        return;
    }
    
    // Simple linear prediction
    VectorCopy(projectile->velocity, trajectory);
    VectorNormalize(trajectory);
    
    // Check if projectile is heading towards us
    vec3_t to_self;
    VectorSubtract(perception->self_state.position, projectile->position, to_self);
    float dist = VectorNormalize(to_self);
    
    float dot = DotProduct(trajectory, to_self);
    if (dot > 0.7f) { // Projectile is heading roughly towards us
        time_to_impact = dist / speed;
        projectile->impact_time = time_to_impact;
        
        // Predict impact position
        VectorMA(projectile->position, time_to_impact, projectile->velocity, projectile->predicted_impact);
    }
}

/*
==================
Perception_RememberEntity
==================
*/
void Perception_RememberEntity(perception_system_t *perception, entity_info_t *entity) {
    int i;
    entity_info_t *memory = NULL;
    
    if (!perception || !entity) {
        return;
    }
    
    // Find existing memory or create new
    for (i = 0; i < perception->memory.num_remembered; i++) {
        if (perception->memory.remembered_entities[i].entity_num == entity->entity_num) {
            memory = &perception->memory.remembered_entities[i];
            break;
        }
    }
    
    if (!memory && perception->memory.num_remembered < MAX_VISIBLE_ENTITIES) {
        memory = &perception->memory.remembered_entities[perception->memory.num_remembered];
        perception->memory.num_remembered++;
    }
    
    if (memory) {
        memcpy(memory, entity, sizeof(entity_info_t));
        memory->last_seen_time = level.time * 0.001f;
    }
}

/*
==================
Perception_UpdateMemory
==================
*/
void Perception_UpdateMemory(perception_system_t *perception) {
    int i;
    float current_time;
    
    if (!perception) {
        return;
    }
    
    current_time = level.time * 0.001f;
    
    // Decay old memories
    for (i = 0; i < perception->memory.num_remembered; i++) {
        entity_info_t *memory = &perception->memory.remembered_entities[i];
        float age = current_time - memory->last_seen_time;
        
        // Reduce confidence over time
        memory->visibility_confidence *= (1.0f - perception->memory.memory_decay_rate * age);
        
        // Forget very old or low confidence memories
        if (memory->visibility_confidence < 0.1f || age > 10.0f) {
            // Remove from memory
            if (i < perception->memory.num_remembered - 1) {
                memmove(&perception->memory.remembered_entities[i],
                       &perception->memory.remembered_entities[i + 1],
                       (perception->memory.num_remembered - i - 1) * sizeof(entity_info_t));
            }
            perception->memory.num_remembered--;
            i--;
        }
    }
    
    perception->memory.last_update_time = current_time;
}

/*
==================
Perception_GetNearestEnemy
==================
*/
entity_info_t *Perception_GetNearestEnemy(perception_system_t *perception) {
    entity_info_t *nearest = NULL;
    float min_dist = 999999;
    int i;
    
    if (!perception) {
        return NULL;
    }
    
    for (i = 0; i < perception->num_visible_entities; i++) {
        entity_info_t *entity = &perception->visible_entities[i];
        
        if (entity->is_enemy && entity->distance < min_dist) {
            min_dist = entity->distance;
            nearest = entity;
        }
    }
    
    return nearest;
}

/*
==================
Perception_FindEscapeRoutes
==================
*/
void Perception_FindEscapeRoutes(perception_system_t *perception) {
    vec3_t best_dir;
    float best_score = -999;
    int i;
    
    if (!perception) {
        return;
    }
    
    // Check 8 directions
    for (i = 0; i < 8; i++) {
        vec3_t dir, end;
        trace_t trace;
        float angle = i * 45;
        float rad = DEG2RAD(angle);
        float score = 0;
        
        dir[0] = cos(rad);
        dir[1] = sin(rad);
        dir[2] = 0;
        
        VectorMA(perception->self_state.position, 500, dir, end);
        trap_Trace(&trace, perception->self_state.position, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID);
        
        // Score based on openness
        score = trace.fraction * 500;
        
        // Penalize directions toward threats
        if (perception->threats.threat_count > 0) {
            vec3_t to_threat;
            VectorSubtract(perception->threats.threat_center, perception->self_state.position, to_threat);
            VectorNormalize(to_threat);
            
            float dot = DotProduct(dir, to_threat);
            score -= dot * 200; // Prefer directions away from threats
        }
        
        if (score > best_score) {
            best_score = score;
            VectorCopy(dir, best_dir);
        }
    }
    
    VectorCopy(best_dir, perception->spatial.escape_direction);
}

/*
==================
Perception_UpdateHearing
==================
*/
void Perception_UpdateHearing(perception_system_t *perception, int client_num) {
    vec3_t sound_origin;
    float sound_volume;
    sound_type_t sound_type;
    float distance;
    vec3_t dir;
    
    if (!perception || client_num < 0 || client_num >= MAX_CLIENTS) {
        return;
    }
    
    // Get entity that made the sound
    gentity_t *source = &g_entities[client_num];
    if (!source->inuse || !source->client) {
        return;
    }
    
    VectorCopy(source->s.pos.trBase, sound_origin);
    
    // Calculate distance and volume falloff
    VectorSubtract(sound_origin, perception->self_state.position, dir);
    distance = VectorLength(dir);
    
    if (distance > MAX_HEARING_RANGE) {
        return; // Too far to hear
    }
    
    // Determine sound type based on entity state
    if (source->client->ps.weapon == WP_RAILGUN || 
        source->client->ps.weapon == WP_ROCKET_LAUNCHER) {
        sound_type = SOUND_WEAPON_FIRE;
        sound_volume = 1.0f;
    } else if (source->client->ps.velocity[2] > 200) {
        sound_type = SOUND_JUMP;
        sound_volume = 0.3f;
    } else if (VectorLength(source->client->ps.velocity) > 300) {
        sound_type = SOUND_FOOTSTEP;
        sound_volume = 0.2f;
    } else {
        sound_type = SOUND_AMBIENT;
        sound_volume = 0.1f;
    }
    
    // Apply distance falloff
    sound_volume *= (1.0f - distance / MAX_HEARING_RANGE);
    
    // Check if sound is blocked by geometry
    trace_t trace;
    trap_Trace(&trace, perception->self_state.position, NULL, NULL, 
               sound_origin, client_num, CONTENTS_SOLID);
    
    if (trace.fraction < 1.0f) {
        sound_volume *= 0.3f; // Muffled through walls
    }
    
    // Only process audible sounds
    if (sound_volume < 0.1f) {
        return;
    }
    
    // Add to sound buffer
    if (perception->num_sounds < MAX_AUDIBLE_SOUNDS) {
        sound_event_t *sound = &perception->sounds[perception->num_sounds];
        VectorCopy(sound_origin, sound->origin);
        sound->volume = sound_volume;
        sound->type = sound_type;
        sound->timestamp = level.time * 0.001f;
        sound->source_entity = client_num;
        
        perception->num_sounds++;
        
        // Update spatial awareness if significant sound
        if (sound_volume > 0.5f) {
            // Update open space ratio based on sound distance
            perception->spatial.open_space_ratio = MAX(perception->spatial.open_space_ratio,
                                                      distance / MAX_HEARING_RANGE);
        }
    }
    
    // Age out old sounds
    float current_time = level.time * 0.001f;
    int valid_sounds = 0;
    for (int i = 0; i < perception->num_sounds; i++) {
        if (current_time - perception->sounds[i].timestamp < (SOUND_MEMORY_TIME * 0.001f)) {
            if (i != valid_sounds) {
                perception->sounds[valid_sounds] = perception->sounds[i];
            }
            valid_sounds++;
        }
    }
    perception->num_sounds = valid_sounds;
}

/*
==================
Perception_ProcessDamageEvents
==================
*/
void Perception_ProcessDamageEvents(perception_system_t *perception, int client_num) {
    int i;
    float current_time;
    
    if (!perception) {
        return;
    }
    
    current_time = level.time * 0.001f;
    
    // Process damage indicators (would need actual damage event system)
    // For now, simulate by checking health changes
    gentity_t *self = &g_entities[client_num];
    if (!self->inuse || !self->client) {
        return;
    }
    
    int current_health = self->health;
    int health_diff = perception->self_state.health - current_health;
    
    if (health_diff > 0) {
        // Took damage
        damage_event_t *event = &perception->damage_events[perception->num_damage_events % MAX_DAMAGE_EVENTS];
        
        event->damage_amount = health_diff;
        event->timestamp = current_time;
        event->attacker = ENTITYNUM_NONE; // Would need actual attacker info
        
        // Estimate damage direction from recent threats
        if (perception->threats.threat_count > 0) {
            // Use most dangerous threat as likely attacker
            float max_danger = 0;
            int likely_attacker = -1;
            
            for (i = 0; i < perception->num_visible_entities; i++) {
                entity_info_t *entity = &perception->visible_entities[i];
                if (entity->is_enemy && entity->threat_level > max_danger) {
                    max_danger = entity->threat_level;
                    likely_attacker = entity->entity_num;
                }
            }
            
            if (likely_attacker >= 0) {
                event->attacker = likely_attacker;
                // Find the entity to get position
                for (i = 0; i < perception->num_visible_entities; i++) {
                    if (perception->visible_entities[i].entity_num == likely_attacker) {
                        VectorCopy(perception->visible_entities[i].position, event->damage_origin);
                        break;
                    }
                }
                VectorSubtract(event->damage_origin, perception->self_state.position, event->damage_direction);
                VectorNormalize(event->damage_direction);
            }
        }
        
        perception->num_damage_events++;
        
        // Update threat awareness
        perception->threats.under_fire = qtrue;
    }
    
    // Update self state health
    perception->self_state.health = current_health;
    
    // Calculate damage rate
    float time_window = 5.0f; // 5 second window
    int recent_damage = 0;
    int recent_events = 0;
    
    for (i = 0; i < MIN(perception->num_damage_events, MAX_DAMAGE_EVENTS); i++) {
        int idx = (perception->num_damage_events - 1 - i) % MAX_DAMAGE_EVENTS;
        if (current_time - perception->damage_events[idx].timestamp < time_window) {
            recent_damage += perception->damage_events[idx].damage_amount;
            recent_events++;
        } else {
            break;
        }
    }
    
    // Store overall threat level based on damage rate
    if (recent_events > 0) {
        perception->threats.overall_threat_level = recent_damage / time_window;
    } else {
        perception->threats.overall_threat_level = 0;
    }
}

/*
==================
Perception_MergeWithMemory
==================
*/
void Perception_MergeWithMemory(perception_system_t *perception) {
    int i, j;
    float current_time;
    float memory_decay = 0.95f;
    
    if (!perception) {
        return;
    }
    
    current_time = level.time * 0.001f;
    
    // Merge current entities with memory
    for (i = 0; i < perception->num_visible_entities; i++) {
        entity_info_t *entity = &perception->visible_entities[i];
        int mem_slot = -1;
        
        // Find existing memory slot
        for (j = 0; j < perception->memory.num_remembered; j++) {
            if (perception->memory.remembered_entities[j].entity_num == entity->entity_num) {
                mem_slot = j;
                break;
            }
        }
        
        if (mem_slot >= 0) {
            // Update existing memory
            entity_info_t *mem = &perception->memory.remembered_entities[mem_slot];
            
            // Store previous position for velocity estimation
            // VectorCopy(mem->position, mem->position_history[mem->history_index]);
            // mem->history_index = (mem->history_index + 1) % POSITION_HISTORY_SIZE;
            
            // Update with current info
            VectorCopy(entity->position, mem->position);
            VectorCopy(entity->velocity, mem->velocity);
            mem->last_seen_time = current_time;
            mem->visibility_confidence = 1.0f;
            // mem->times_seen++;
            
            // Calculate average velocity over time
            // Simplified version
            VectorCopy(entity->velocity, mem->velocity);
            
        } else if (perception->memory.num_remembered < MAX_VISIBLE_ENTITIES) {
            // Add new memory
            entity_info_t *mem = &perception->memory.remembered_entities[perception->memory.num_remembered];
            
            mem->entity_num = entity->entity_num;
            mem->entity_type = entity->entity_type;
            VectorCopy(entity->position, mem->position);
            VectorCopy(entity->velocity, mem->velocity);
            mem->last_seen_time = current_time;
            mem->visibility_confidence = 1.0f;
            // mem->times_seen = 1;
            // mem->history_index = 0;
            // VectorCopy(entity->position, mem->position_history[0]);
            
            perception->memory.num_remembered++;
        }
    }
    
    // Decay confidence for entities not currently visible
    for (i = 0; i < perception->memory.num_remembered; i++) {
        entity_info_t *mem = &perception->memory.remembered_entities[i];
        qboolean currently_visible = qfalse;
        
        // Check if currently visible
        for (j = 0; j < perception->num_visible_entities; j++) {
            if (perception->visible_entities[j].entity_num == mem->entity_num) {
                currently_visible = qtrue;
                break;
            }
        }
        
        if (!currently_visible) {
            // Decay confidence
            float time_since_seen = current_time - mem->last_seen_time;
            mem->visibility_confidence *= pow(memory_decay, time_since_seen);
            
            // Predict position based on last known velocity
            if (mem->visibility_confidence > 0.3f) {
                vec3_t predicted_pos;
                VectorMA(mem->position, time_since_seen, mem->velocity, predicted_pos);
                VectorCopy(predicted_pos, mem->predicted_impact);
            }
        }
    }
    
    // Remove very low confidence memories
    int valid_memories = 0;
    for (i = 0; i < perception->memory.num_remembered; i++) {
        if (perception->memory.remembered_entities[i].visibility_confidence > 0.1f) {
            if (i != valid_memories) {
                perception->memory.remembered_entities[valid_memories] = perception->memory.remembered_entities[i];
            }
            valid_memories++;
        }
    }
    perception->memory.num_remembered = valid_memories;
}

/*
==================
Perception_ApplyFogOfWar
==================
*/
void Perception_ApplyFogOfWar(perception_system_t *perception) {
    int i;
    vec3_t view_origin;
    vec3_t view_angles;
    float fov;
    
    if (!perception) {
        return;
    }
    
    // Get view parameters
    VectorCopy(perception->self_state.position, view_origin);
    view_origin[2] += DEFAULT_VIEWHEIGHT; // Eye level
    VectorCopy(perception->self_state.angles, view_angles);
    fov = perception->filter.fov_angle;
    
    // Apply fog of war to each entity
    for (i = 0; i < perception->num_visible_entities; i++) {
        entity_info_t *entity = &perception->visible_entities[i];
        vec3_t to_entity;
        float distance;
        trace_t trace;
        
        // Calculate distance
        VectorSubtract(entity->position, view_origin, to_entity);
        distance = VectorLength(to_entity);
        
        // Check maximum view distance
        if (distance > perception->filter.max_vision_range) {
            entity->visibility_confidence = 0;
            entity->visible = qfalse;
            continue;
        }
        
        // Check field of view
        VectorNormalize(to_entity);
        vec3_t forward;
        AngleVectors(view_angles, forward, NULL, NULL);
        float dot = DotProduct(forward, to_entity);
        float half_fov = fov * 0.5f;
        
        if (dot < cos(DEG2RAD(half_fov))) {
            // Outside FOV - reduce visibility
            entity->visibility_confidence *= 0.3f;
        }
        
        // Check line of sight
        trap_Trace(&trace, view_origin, NULL, NULL, entity->position, 
                   ENTITYNUM_NONE, CONTENTS_SOLID);
        
        if (trace.fraction < 1.0f) {
            // Blocked by geometry
            entity->visibility_confidence = 0;
            entity->visible = qfalse;
            continue;
        }
        
        // Apply distance-based visibility falloff
        float visibility = 1.0f;
        if (distance > perception->filter.max_vision_range * 0.5f) {
            visibility = 1.0f - (distance - perception->filter.max_vision_range * 0.5f) / 
                                (perception->filter.max_vision_range * 0.5f);
        }
        
        // Apply lighting conditions (simplified)
        // In a real implementation, would check actual lighting
        float lighting = 1.0f;
        
        // Apply movement-based visibility
        float entity_speed = VectorLength(entity->velocity);
        if (entity_speed > 50) {
            visibility = MIN(visibility * 1.2f, 1.0f); // Moving targets easier to see
        }
        
        // Apply camouflage/stealth effects
        if (entity->entity_type == ENTITY_PLAYER) {
            gentity_t *ent = &g_entities[entity->entity_num];
            if (ent->inuse && ent->client) {
                // Check for invisibility powerup (if defined)
                // if (ent->client->ps.powerups[PW_INVIS]) {
                //     visibility *= 0.1f;
                // }
            }
        }
        
        // Final visibility calculation
        entity->visibility_confidence = visibility * lighting;
        entity->visible = (entity->visibility_confidence > 0.1f);
        
        // Update last seen time if visible
        if (entity->visible) {
            entity->last_seen_time = level.time * 0.001f;
        }
    }
    
    // Update fog of war map (grid-based visibility)
    int grid_size = 32; // Units per grid cell
    int grid_width = 256;
    int grid_height = 256;
    
    // Clear visibility grid (fog of war disabled for now)
    // memset(perception->fog_of_war.visibility_grid, 0, 
    //        sizeof(perception->fog_of_war.visibility_grid));
    
    // Cast rays to update visibility grid
    int num_rays = 36; // Cast 36 rays in a circle
    for (i = 0; i < num_rays; i++) {
        float angle = (360.0f / num_rays) * i;
        vec3_t ray_dir;
        
        ray_dir[0] = cos(DEG2RAD(angle));
        ray_dir[1] = sin(DEG2RAD(angle));
        ray_dir[2] = 0;
        
        // Cast ray and mark visible cells
        vec3_t ray_end;
        VectorMA(view_origin, perception->filter.max_vision_range, ray_dir, ray_end);
        
        trace_t trace;
        trap_Trace(&trace, view_origin, NULL, NULL, ray_end, 
                   ENTITYNUM_NONE, CONTENTS_SOLID);
        
        // Mark cells along ray as visible
        float ray_length = trace.fraction * perception->filter.max_vision_range;
        int steps = ray_length / grid_size;
        
        for (int step = 0; step <= steps; step++) {
            vec3_t pos;
            VectorMA(view_origin, step * grid_size, ray_dir, pos);
            
            // Convert to grid coordinates
            int grid_x = (pos[0] / grid_size) + grid_width / 2;
            int grid_y = (pos[1] / grid_size) + grid_height / 2;
            
            if (grid_x >= 0 && grid_x < grid_width && 
                grid_y >= 0 && grid_y < grid_height) {
                int index = grid_y * grid_width + grid_x;
                // if (index < MAX_VISIBILITY_CELLS) {
                //     perception->fog_of_war.visibility_grid[index] = 1.0f;
                // }
            }
        }
    }
    
    // perception->fog_of_war.last_update = level.time * 0.001f;
}

/*
==================
Perception_NotifyEntityUpdate
Notifies the perception system of an entity update
==================
*/
void Perception_NotifyEntityUpdate(int entity_num, const vec3_t position, int type) {
    // This is a stub for now
    // In a full implementation, this would update the perception system's
    // knowledge about entity positions and states
    
    // Basic validation
    if (entity_num < 0 || entity_num >= MAX_GENTITIES) {
        return;
    }
    
    // Would normally update perception state for all bots that can perceive this entity
    // For now, just a placeholder implementation
}

