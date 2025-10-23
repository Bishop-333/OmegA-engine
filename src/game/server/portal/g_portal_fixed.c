/*
===========================================================================
Portal System - Fixed Implementation
Corrects issues with orange/blue portal placement and connection
===========================================================================
*/

#include "g_portal.h"
#include "../../../engine/common/q_shared.h"

// External declarations
extern portalInfo_t g_portals[MAX_PORTAL_PAIRS * 2];
extern playerPortalState_t g_playerPortalStates[MAX_CLIENTS];
extern gentity_t *g_entities;
extern level_locals_t level;

// Trap function declarations
void trap_Trace(trace_t *results, const vec3_t start, const vec3_t mins, 
                const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask);
void trap_LinkEntity(gentity_t *ent);
void trap_UnlinkEntity(gentity_t *ent);
void G_Printf(const char *fmt, ...);
gentity_t *G_Spawn(void);
void G_FreeEntity(gentity_t *ent);
void AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);
void SnapVector(float *v);

/*
================
G_FirePortalFixed

Fixed version of portal firing that properly traces and places portals
================
*/
void G_FirePortalFixed(gentity_t *player, portalType_t type) {
    vec3_t start, end, forward, right, up;
    vec3_t portalOrigin, portalNormal;
    trace_t trace;
    float distance;
    
    if (!player || !player->client) {
        G_Printf("G_FirePortalFixed: Invalid player\n");
        return;
    }
    
    // Get player view position
    VectorCopy(player->client->ps.origin, start);
    start[2] += player->client->ps.viewheight;
    
    // Get view direction
    AngleVectors(player->client->ps.viewangles, forward, right, up);
    
    // Trace forward to find surface
    VectorMA(start, 4096, forward, end);
    
    // Do the trace
    trap_Trace(&trace, start, NULL, NULL, end, player->s.number, MASK_SOLID | MASK_PLAYERSOLID);
    
    // Check if we hit something
    if (trace.fraction >= 1.0f) {
        G_Printf("Portal trace didn't hit anything\n");
        return;
    }
    
    // Check if surface is valid for portal
    if (trace.surfaceFlags & SURF_SKY) {
        G_Printf("Cannot place portal on sky\n");
        return;
    }
    
    if (trace.surfaceFlags & SURF_NOIMPACT) {
        G_Printf("Cannot place portal on this surface\n");
        return;
    }
    
    // Calculate portal position - offset from wall
    VectorCopy(trace.endpos, portalOrigin);
    VectorCopy(trace.plane.normal, portalNormal);
    
    // Offset portal slightly from wall to prevent z-fighting
    VectorMA(portalOrigin, 2.0f, portalNormal, portalOrigin);
    
    // Calculate distance for feedback
    distance = Distance(start, portalOrigin);
    
    G_Printf("^%dPlacing %s portal at distance %.0f units\n", 
             type == PORTAL_ORANGE ? 3 : 4,  // Yellow for orange, blue for blue
             type == PORTAL_ORANGE ? "ORANGE" : "BLUE",
             distance);
    
    // Create the portal
    G_CreatePortalFixed(portalOrigin, portalNormal, player, type);
    
    // Play sound effect (placeholder)
    // G_AddEvent(player, EV_FIRE_WEAPON, 0);
}

/*
================
G_CreatePortalFixed

Fixed version that properly creates and links portals
================
*/
void G_CreatePortalFixed(vec3_t origin, vec3_t normal, gentity_t *owner, portalType_t type) {
    gentity_t *portal;
    portalInfo_t *info;
    int slot, oldPortal, linkedSlot;
    int clientNum;
    
    if (!owner || !owner->client) {
        G_Printf("G_CreatePortalFixed: Invalid owner\n");
        return;
    }
    
    clientNum = owner->client - level.clients;
    
    // Remove old portal of same type
    oldPortal = -1;
    for (int i = 0; i < MAX_PORTAL_PAIRS * 2; i++) {
        if (g_portals[i].inUse && 
            g_portals[i].ownerNum == clientNum && 
            g_portals[i].type == type) {
            oldPortal = i;
            break;
        }
    }
    
    if (oldPortal >= 0) {
        G_Printf("Removing old %s portal\n", type == PORTAL_ORANGE ? "orange" : "blue");
        G_RemovePortalFixed(oldPortal);
    }
    
    // Find free slot
    slot = -1;
    for (int i = 0; i < MAX_PORTAL_PAIRS * 2; i++) {
        if (!g_portals[i].inUse) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        G_Printf("No free portal slots!\n");
        return;
    }
    
    // Spawn portal entity
    portal = G_Spawn();
    if (!portal) {
        G_Printf("Failed to spawn portal entity\n");
        return;
    }
    
    // Setup portal info
    info = &g_portals[slot];
    memset(info, 0, sizeof(portalInfo_t));
    
    info->inUse = qtrue;
    info->type = type;
    info->state = PORTAL_STATE_OPENING;
    info->entityNum = portal->s.number;
    info->ownerNum = clientNum;
    info->radius = PORTAL_RADIUS;
    info->creationTime = level.time;
    info->stateChangeTime = level.time;
    info->linkedPortalNum = -1;  // Not linked yet
    
    VectorCopy(origin, info->origin);
    VectorCopy(normal, info->surfaceNormal);
    
    // Setup portal orientation
    VectorNegate(normal, info->portalForward);
    
    // Calculate up vector
    vec3_t up;
    if (fabs(normal[2]) > 0.9f) {
        // Portal on floor/ceiling - use world X as reference
        VectorSet(up, 1, 0, 0);
    } else {
        // Portal on wall - use world Z as up
        VectorSet(up, 0, 0, 1);
    }
    
    // Calculate right vector
    CrossProduct(up, info->portalForward, info->portalRight);
    VectorNormalize(info->portalRight);
    
    // Recalculate up to be perpendicular
    CrossProduct(info->portalForward, info->portalRight, info->portalUp);
    VectorNormalize(info->portalUp);
    
    // Setup entity
    portal->classname = "portal";
    portal->s.eType = ET_PORTAL;
    portal->s.generic1 = type;  // Portal type
    portal->s.time = level.time;
    portal->s.time2 = PORTAL_ACTIVATION_TIME;
    portal->s.otherEntityNum = ENTITYNUM_NONE;  // No link yet
    portal->genericValue1 = slot;  // Store our slot
    
    VectorCopy(origin, portal->s.origin);
    VectorCopy(origin, portal->s.pos.trBase);
    VectorCopy(origin, portal->r.currentOrigin);
    VectorCopy(normal, portal->s.origin2);  // Store normal for client
    
    // Set portal color (pack RGBA into int)
    if (type == PORTAL_ORANGE) {
        // Orange portal
        portal->s.constantLight = (255 << 0) | (128 << 8) | (0 << 16) | (200 << 24);
    } else {
        // Blue portal
        portal->s.constantLight = (0 << 0) | (128 << 8) | (255 << 16) | (200 << 24);
    }
    
    // Set bounds
    vec3_t mins, maxs;
    VectorSet(mins, -PORTAL_RADIUS, -PORTAL_RADIUS, -PORTAL_RADIUS);
    VectorSet(maxs, PORTAL_RADIUS, PORTAL_RADIUS, PORTAL_RADIUS);
    VectorCopy(mins, portal->r.mins);
    VectorCopy(maxs, portal->r.maxs);
    VectorAdd(origin, mins, portal->r.absmin);
    VectorAdd(origin, maxs, portal->r.absmax);
    
    // Setup functions
    portal->think = G_PortalThink;
    portal->nextthink = level.time + 100;
    portal->touch = G_PortalTouch;
    portal->parent = owner;
    
    portal->r.contents = CONTENTS_TRIGGER;
    portal->r.svFlags = SVF_PORTAL;
    
    // Link into world
    trap_LinkEntity(portal);
    
    // Update player's portal state
    if (type == PORTAL_ORANGE) {
        g_playerPortalStates[clientNum].activeOrangePortal = slot;
    } else {
        g_playerPortalStates[clientNum].activeBluePortal = slot;
    }
    
    // Check if we can link with other portal
    portalType_t otherType = (type == PORTAL_ORANGE) ? PORTAL_BLUE : PORTAL_ORANGE;
    linkedSlot = -1;
    
    for (int i = 0; i < MAX_PORTAL_PAIRS * 2; i++) {
        if (g_portals[i].inUse && 
            g_portals[i].ownerNum == clientNum && 
            g_portals[i].type == otherType) {
            linkedSlot = i;
            break;
        }
    }
    
    if (linkedSlot >= 0) {
        // Link the portals!
        portalInfo_t *otherInfo = &g_portals[linkedSlot];
        gentity_t *otherPortal = &g_entities[otherInfo->entityNum];
        
        // Update portal links
        info->linkedPortalNum = otherInfo->entityNum;
        otherInfo->linkedPortalNum = info->entityNum;
        
        // Update states to active
        info->state = PORTAL_STATE_ACTIVE;
        otherInfo->state = PORTAL_STATE_ACTIVE;
        
        // Update entity links
        portal->s.otherEntityNum = otherInfo->entityNum;
        otherPortal->s.otherEntityNum = info->entityNum;
        
        // Store each other's positions for rendering
        VectorCopy(otherInfo->origin, portal->s.angles2);
        VectorCopy(info->origin, otherPortal->s.angles2);
        
        G_Printf("^2Portals LINKED! Orange and Blue portals are now connected.\n");
    } else {
        G_Printf("Portal created but not linked (need both colors)\n");
    }
    
    G_Printf("Created %s portal (slot %d, entity %d) at (%.0f, %.0f, %.0f)\n",
             type == PORTAL_ORANGE ? "ORANGE" : "BLUE",
             slot, portal->s.number,
             origin[0], origin[1], origin[2]);
}

/*
================
G_RemovePortalFixed

Fixed version of portal removal
================
*/
void G_RemovePortalFixed(int portalNum) {
    portalInfo_t *info;
    gentity_t *portal;
    
    if (portalNum < 0 || portalNum >= MAX_PORTAL_PAIRS * 2) {
        return;
    }
    
    info = &g_portals[portalNum];
    if (!info->inUse) {
        return;
    }
    
    // Unlink from other portal
    if (info->linkedPortalNum >= 0) {
        // Find linked portal's slot
        for (int i = 0; i < MAX_PORTAL_PAIRS * 2; i++) {
            if (g_portals[i].inUse && g_portals[i].entityNum == info->linkedPortalNum) {
                g_portals[i].linkedPortalNum = -1;
                g_portals[i].state = PORTAL_STATE_INACTIVE;
                
                // Update linked entity
                gentity_t *linked = &g_entities[g_portals[i].entityNum];
                if (linked->inuse) {
                    linked->s.otherEntityNum = ENTITYNUM_NONE;
                }
                break;
            }
        }
    }
    
    // Remove entity
    if (info->entityNum >= 0 && info->entityNum < MAX_GENTITIES) {
        portal = &g_entities[info->entityNum];
        if (portal->inuse) {
            trap_UnlinkEntity(portal);
            G_FreeEntity(portal);
        }
    }
    
    // Clear player's portal reference
    if (info->type == PORTAL_ORANGE) {
        g_playerPortalStates[info->ownerNum].activeOrangePortal = -1;
    } else {
        g_playerPortalStates[info->ownerNum].activeBluePortal = -1;
    }
    
    // Clear portal info
    info->inUse = qfalse;
}

/*
================
G_TeleportThroughPortalFixed

Fixed teleportation with proper velocity transformation
================
*/
void G_TeleportThroughPortalFixed(gentity_t *ent, gentity_t *enterPortal, gentity_t *exitPortal) {
    vec3_t velocity, angles;
    vec3_t exitPoint;
    portalInfo_t *enterInfo, *exitInfo;
    int enterSlot, exitSlot;
    
    if (!ent || !enterPortal || !exitPortal) {
        return;
    }
    
    enterSlot = enterPortal->genericValue1;
    exitSlot = exitPortal->genericValue1;
    
    if (enterSlot < 0 || enterSlot >= MAX_PORTAL_PAIRS * 2 ||
        exitSlot < 0 || exitSlot >= MAX_PORTAL_PAIRS * 2) {
        return;
    }
    
    enterInfo = &g_portals[enterSlot];
    exitInfo = &g_portals[exitSlot];
    
    if (!enterInfo->inUse || !exitInfo->inUse) {
        return;
    }
    
    // Calculate exit point
    VectorCopy(exitInfo->origin, exitPoint);
    VectorMA(exitPoint, PORTAL_RADIUS + 10, exitInfo->surfaceNormal, exitPoint);
    
    // Transform velocity
    if (ent->client) {
        VectorCopy(ent->client->ps.velocity, velocity);
        
        // Simple velocity reflection for now
        // More complex transformation would consider portal orientations
        float speed = VectorLength(velocity);
        VectorCopy(exitInfo->surfaceNormal, velocity);
        VectorScale(velocity, speed, velocity);
        
        // Teleport player
        VectorCopy(exitPoint, ent->client->ps.origin);
        VectorCopy(velocity, ent->client->ps.velocity);
        
        // Set view angles to face away from exit portal
        vectoangles(exitInfo->surfaceNormal, angles);
        SetClientViewAngle(ent, angles);
        
        // Set teleport flag
        ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
        
        // Record exit time to prevent immediate re-entry
        if (ent->client) {
            int clientNum = ent->client - level.clients;
            g_playerPortalStates[clientNum].lastPortalExitTime = level.time;
        }
        
        G_Printf("Player teleported through portal!\n");
    }
}