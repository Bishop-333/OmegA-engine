/*
===========================================================================
Portal System Implementation for Quake3e-HD
===========================================================================
*/

#include "g_local.h"
#include "g_portal.h"
#include "../../../engine/common/q_shared.h"

portalInfo_t g_portals[MAX_PORTAL_PAIRS * 2];
playerPortalState_t g_playerPortalStates[MAX_CLIENTS];

// External declarations
extern gentity_t g_entities[MAX_GENTITIES];
extern level_locals_t level;
extern gclient_t g_clients[MAX_CLIENTS];

static int G_FindFreePortalSlot(void) {
    int i;
    for (i = 0; i < MAX_PORTAL_PAIRS * 2; i++) {
        if (!g_portals[i].inUse) {
            return i;
        }
    }
    return -1;
}

static int G_FindPlayerPortal(int clientNum, portalType_t type) {
    int i;
    for (i = 0; i < MAX_PORTAL_PAIRS * 2; i++) {
        if (g_portals[i].inUse && 
            g_portals[i].ownerNum == clientNum && 
            g_portals[i].type == type) {
            return i;
        }
    }
    return -1;
}

static void G_SetupPortalOrientation(portalInfo_t *portal, vec3_t normal) {
    vec3_t up, right;
    
    VectorCopy(normal, portal->surfaceNormal);
    VectorNegate(normal, portal->portalForward);
    
    if (fabs(normal[2]) > 0.9f) {
        VectorSet(up, 1, 0, 0);
    } else {
        VectorSet(up, 0, 0, 1);
    }
    
    CrossProduct(up, portal->portalForward, portal->portalRight);
    VectorNormalize(portal->portalRight);
    CrossProduct(portal->portalForward, portal->portalRight, portal->portalUp);
    VectorNormalize(portal->portalUp);
    
    portal->rotationMatrix[0][0] = portal->portalRight[0];
    portal->rotationMatrix[0][1] = portal->portalRight[1];
    portal->rotationMatrix[0][2] = portal->portalRight[2];
    portal->rotationMatrix[1][0] = portal->portalUp[0];
    portal->rotationMatrix[1][1] = portal->portalUp[1];
    portal->rotationMatrix[1][2] = portal->portalUp[2];
    portal->rotationMatrix[2][0] = portal->portalForward[0];
    portal->rotationMatrix[2][1] = portal->portalForward[1];
    portal->rotationMatrix[2][2] = portal->portalForward[2];
}

void G_InitPortalSystem(void) {
    memset(g_portals, 0, sizeof(g_portals));
    memset(g_playerPortalStates, 0, sizeof(g_playerPortalStates));
    
    G_Printf("^2Portal system initialized\n");
    G_Printf("  Max portal pairs: %d\n", MAX_PORTAL_PAIRS);
    G_Printf("  Portal radius: %.0f\n", PORTAL_RADIUS);
    G_Printf("  Activation time: %d ms\n", PORTAL_ACTIVATION_TIME);
}

void G_ShutdownPortalSystem(void) {
    int i;
    for (i = 0; i < MAX_PORTAL_PAIRS * 2; i++) {
        if (g_portals[i].inUse && g_portals[i].entityNum >= 0) {
            gentity_t *ent = &g_entities[g_portals[i].entityNum];
            if (ent->inuse) {
                G_FreeEntity(ent);
            }
        }
    }
    
    memset(g_portals, 0, sizeof(g_portals));
    memset(g_playerPortalStates, 0, sizeof(g_playerPortalStates));
}

qboolean G_TracePortalSurface(vec3_t start, vec3_t end, vec3_t outOrigin, vec3_t outNormal) {
    trace_t trace;
    vec3_t dir, impactPoint;
    
    trap_Trace(&trace, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID);
    
    if (trace.fraction >= 1.0f) {
        return qfalse;
    }
    
    if (!G_IsValidPortalSurface(&trace)) {
        return qfalse;
    }
    
    VectorSubtract(end, start, dir);
    VectorNormalize(dir);
    VectorMA(trace.endpos, 2.0f, trace.plane.normal, impactPoint);
    
    VectorCopy(impactPoint, outOrigin);
    VectorCopy(trace.plane.normal, outNormal);
    
    return qtrue;
}

qboolean G_IsValidPortalSurface(trace_t *trace) {
    if (trace->surfaceFlags & SURF_NOIMPACT) {
        return qfalse;
    }
    
    if (trace->surfaceFlags & SURF_SKY) {
        return qfalse;
    }
    
    if (trace->contents & (CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA)) {
        return qfalse;
    }
    
    if (trace->surfaceFlags & SURF_NOPORTAL) {
        return qfalse;
    }
    
    if (DotProduct(trace->plane.normal, trace->plane.normal) < 0.9f) {
        return qfalse;
    }
    
    return qtrue;
}

void G_CreatePortal(vec3_t origin, vec3_t normal, gentity_t *owner, portalType_t type) {
    gentity_t *portal;
    portalInfo_t *info;
    int slot, oldPortal;
    int clientNum = owner->client - level.clients;
    
    oldPortal = G_FindPlayerPortal(clientNum, type);
    if (oldPortal >= 0) {
        G_RemovePortal(oldPortal);
    }
    
    slot = G_FindFreePortalSlot();
    if (slot < 0) {
        G_Printf("No free portal slots available\n");
        return;
    }
    
    portal = G_Spawn();
    if (!portal) {
        return;
    }
    
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
    
    VectorCopy(origin, info->origin);
    G_SetupPortalOrientation(info, normal);
    
    portal->classname = "portal";
    portal->s.eType = ET_PORTAL;
    portal->s.modelindex = 0;
    portal->s.generic1 = type;
    portal->s.time = level.time;
    portal->s.time2 = PORTAL_ACTIVATION_TIME;
    
    VectorCopy(origin, portal->s.origin);
    VectorCopy(origin, portal->s.pos.trBase);
    VectorCopy(normal, portal->s.origin2);
    
    portal->s.constantLight = type == PORTAL_ORANGE ? 
        (255 | (128 << 8) | (0 << 16) | (200 << 24)) :
        (0 | (128 << 8) | (255 << 16) | (200 << 24));
    
    portal->think = G_PortalThink;
    portal->nextthink = level.time + 100;
    portal->touch = G_PortalTouch;
    portal->r.contents = CONTENTS_TRIGGER;
    portal->r.svFlags = SVF_PORTAL;
    portal->parent = owner;
    portal->genericValue1 = slot;
    
    vec3_t mins, maxs;
    VectorSet(mins, -PORTAL_RADIUS, -PORTAL_RADIUS, -PORTAL_RADIUS);
    VectorSet(maxs, PORTAL_RADIUS, PORTAL_RADIUS, PORTAL_RADIUS);
    VectorAdd(origin, mins, portal->r.absmin);
    VectorAdd(origin, maxs, portal->r.absmax);
    
    trap_LinkEntity(portal);
    
    if (type == PORTAL_ORANGE) {
        g_playerPortalStates[clientNum].activeOrangePortal = slot;
    } else {
        g_playerPortalStates[clientNum].activeBluePortal = slot;
    }
    
    int otherType = (type == PORTAL_ORANGE) ? PORTAL_BLUE : PORTAL_ORANGE;
    int otherPortal = G_FindPlayerPortal(clientNum, otherType);
    if (otherPortal >= 0) {
        info->linkedPortalNum = g_portals[otherPortal].entityNum;
        g_portals[otherPortal].linkedPortalNum = info->entityNum;
        
        info->state = PORTAL_STATE_ACTIVE;
        g_portals[otherPortal].state = PORTAL_STATE_ACTIVE;
        
        portal->s.otherEntityNum = g_portals[otherPortal].entityNum;
        gentity_t *other = &g_entities[g_portals[otherPortal].entityNum];
        if (other->inuse) {
            other->s.otherEntityNum = info->entityNum;
            VectorCopy(info->origin, other->s.origin2);
        }
    }
    
    G_Printf("Created %s portal at (%.0f, %.0f, %.0f)\n", 
        type == PORTAL_ORANGE ? "orange" : "blue",
        origin[0], origin[1], origin[2]);
}

void G_RemovePortal(int portalNum) {
    portalInfo_t *info;
    gentity_t *portal;
    
    if (portalNum < 0 || portalNum >= MAX_PORTAL_PAIRS * 2) {
        return;
    }
    
    info = &g_portals[portalNum];
    if (!info->inUse) {
        return;
    }
    
    if (info->linkedPortalNum >= 0) {
        gentity_t *linked = &g_entities[info->linkedPortalNum];
        if (linked->inuse) {
            linked->s.otherEntityNum = ENTITYNUM_NONE;
        }
        
        int linkedSlot = linked->genericValue1;
        if (linkedSlot >= 0 && linkedSlot < MAX_PORTAL_PAIRS * 2) {
            g_portals[linkedSlot].linkedPortalNum = -1;
            g_portals[linkedSlot].state = PORTAL_STATE_INACTIVE;
        }
    }
    
    if (info->entityNum >= 0) {
        portal = &g_entities[info->entityNum];
        if (portal->inuse) {
            info->state = PORTAL_STATE_CLOSING;
            info->stateChangeTime = level.time;
            portal->think = G_FreeEntity;
            portal->nextthink = level.time + PORTAL_CLOSE_TIME;
        }
    }
    
    if (info->type == PORTAL_ORANGE) {
        g_playerPortalStates[info->ownerNum].activeOrangePortal = -1;
    } else {
        g_playerPortalStates[info->ownerNum].activeBluePortal = -1;
    }
    
    info->inUse = qfalse;
}

void G_UpdatePortal(gentity_t *portal) {
    portalInfo_t *info;
    int slot = portal->genericValue1;
    
    if (slot < 0 || slot >= MAX_PORTAL_PAIRS * 2) {
        return;
    }
    
    info = &g_portals[slot];
    if (!info->inUse) {
        return;
    }
    
    switch (info->state) {
        case PORTAL_STATE_OPENING:
            if (level.time - info->stateChangeTime >= PORTAL_ACTIVATION_TIME) {
                info->state = (info->linkedPortalNum >= 0) ? 
                    PORTAL_STATE_ACTIVE : PORTAL_STATE_INACTIVE;
                info->stateChangeTime = level.time;
            }
            break;
            
        case PORTAL_STATE_CLOSING:
            if (level.time - info->stateChangeTime >= PORTAL_CLOSE_TIME) {
                G_FreeEntity(portal);
                info->inUse = qfalse;
            }
            break;
            
        default:
            break;
    }
}

void G_PortalThink(gentity_t *portal) {
    G_UpdatePortal(portal);
    portal->nextthink = level.time + 50;
}

void G_PortalTouch(gentity_t *portal, gentity_t *other, trace_t *trace) {
    portalInfo_t *info, *linkedInfo;
    gentity_t *exitPortal;
    int slot = portal->genericValue1;
    
    if (slot < 0 || slot >= MAX_PORTAL_PAIRS * 2) {
        return;
    }
    
    info = &g_portals[slot];
    if (!info->inUse || info->state != PORTAL_STATE_ACTIVE) {
        return;
    }
    
    if (info->linkedPortalNum < 0) {
        return;
    }
    
    exitPortal = &g_entities[info->linkedPortalNum];
    if (!exitPortal->inuse) {
        return;
    }
    
    int linkedSlot = exitPortal->genericValue1;
    if (linkedSlot < 0 || linkedSlot >= MAX_PORTAL_PAIRS * 2) {
        return;
    }
    
    linkedInfo = &g_portals[linkedSlot];
    if (!linkedInfo->inUse || linkedInfo->state != PORTAL_STATE_ACTIVE) {
        return;
    }
    
    if (!other->client && other->s.eType != ET_MISSILE) {
        return;
    }
    
    if (G_CheckPortalTeleport(other, portal)) {
        G_TeleportThroughPortal(other, portal, exitPortal);
    }
}

qboolean G_CheckPortalTeleport(gentity_t *ent, gentity_t *portal) {
    vec3_t delta;
    portalInfo_t *info = &g_portals[portal->genericValue1];
    
    VectorSubtract(ent->s.origin, info->origin, delta);
    float dist = DotProduct(delta, info->portalForward);
    
    if (dist > 0 && dist < 10.0f) {
        float lateral = VectorLength(delta) - fabs(dist);
        if (lateral < info->radius) {
            return qtrue;
        }
    }
    
    return qfalse;
}

void G_TransformVelocityThroughPortal(vec3_t velocity, vec3_t enterNormal, vec3_t exitNormal) {
    vec3_t inVel, outVel;
    float speed;
    vec3_t enterForward, exitForward;
    matrix3_t transform;
    
    VectorCopy(velocity, inVel);
    speed = VectorLength(inVel);
    
    VectorNegate(enterNormal, enterForward);
    VectorNegate(exitNormal, exitForward);
    
    float dot = DotProduct(enterForward, exitForward);
    if (dot < -0.99f) {
        VectorScale(exitForward, speed, velocity);
        return;
    }
    
    vec3_t axis;
    CrossProduct(enterForward, exitForward, axis);
    float angle = acos(dot);
    
    if (VectorLength(axis) > 0.001f) {
        VectorNormalize(axis);
        AxisToAngles(axis, transform);
        RotatePointAroundVector(outVel, axis, inVel, angle * (180.0f / M_PI));
        VectorCopy(outVel, velocity);
    }
}

void G_TeleportThroughPortal(gentity_t *ent, gentity_t *enterPortal, gentity_t *exitPortal) {
    portalInfo_t *enterInfo = &g_portals[enterPortal->genericValue1];
    portalInfo_t *exitInfo = &g_portals[exitPortal->genericValue1];
    vec3_t relativePos, newPos, newVelocity;
    vec3_t offset;
    
    VectorSubtract(ent->s.origin, enterInfo->origin, relativePos);
    
    float forward = DotProduct(relativePos, enterInfo->portalForward);
    float right = DotProduct(relativePos, enterInfo->portalRight);
    float up = DotProduct(relativePos, enterInfo->portalUp);
    
    VectorScale(exitInfo->portalForward, -forward - 5.0f, offset);
    VectorMA(offset, -right, exitInfo->portalRight, offset);
    VectorMA(offset, up, exitInfo->portalUp, offset);
    VectorAdd(exitInfo->origin, offset, newPos);
    
    if (ent->client) {
        VectorCopy(ent->client->ps.velocity, newVelocity);
        G_TransformVelocityThroughPortal(newVelocity, enterInfo->surfaceNormal, exitInfo->surfaceNormal);
        
        vec3_t angles;
        VectorCopy(ent->client->ps.viewangles, angles);
        float yawDiff = vectoyaw(exitInfo->portalForward) - vectoyaw(enterInfo->portalForward) + 180.0f;
        angles[YAW] += yawDiff;
        
        TeleportPlayer(ent, newPos, angles);
        VectorCopy(newVelocity, ent->client->ps.velocity);
        
        int clientNum = ent->client - level.clients;
        g_playerPortalStates[clientNum].lastPortalExitTime = level.time;
        g_playerPortalStates[clientNum].fallDamageImmunityEndTime = level.time + FALL_DAMAGE_IMMUNITY_TIME;
        VectorCopy(newVelocity, g_playerPortalStates[clientNum].lastPortalExitVelocity);
    } else if (ent->s.eType == ET_MISSILE) {
        VectorCopy(newPos, ent->s.origin);
        VectorCopy(newPos, ent->s.pos.trBase);
        
        VectorCopy(ent->s.pos.trDelta, newVelocity);
        G_TransformVelocityThroughPortal(newVelocity, enterInfo->surfaceNormal, exitInfo->surfaceNormal);
        VectorCopy(newVelocity, ent->s.pos.trDelta);
        
        ent->s.pos.trTime = level.time;
        trap_LinkEntity(ent);
    }
}

void G_FirePortal(gentity_t *player, portalType_t type) {
    vec3_t start, end, forward;
    vec3_t portalOrigin, portalNormal;
    
    if (!player->client) {
        return;
    }
    
    VectorCopy(player->client->ps.origin, start);
    start[2] += player->client->ps.viewheight;
    
    AngleVectors(player->client->ps.viewangles, forward, NULL, NULL);
    VectorMA(start, 8192, forward, end);
    
    if (G_TracePortalSurface(start, end, portalOrigin, portalNormal)) {
        gentity_t *projectile = fire_plasma(player, start, forward);
        if (projectile) {
            projectile->genericValue2 = type;
            projectile->genericValue3 = 1;
        }
        
        G_CreatePortal(portalOrigin, portalNormal, player, type);
    }
}

void G_ClosePlayerPortals(gentity_t *player) {
    int clientNum;
    
    if (!player->client) {
        return;
    }
    
    clientNum = player->client - level.clients;
    
    if (g_playerPortalStates[clientNum].activeOrangePortal >= 0) {
        G_RemovePortal(g_playerPortalStates[clientNum].activeOrangePortal);
    }
    
    if (g_playerPortalStates[clientNum].activeBluePortal >= 0) {
        G_RemovePortal(g_playerPortalStates[clientNum].activeBluePortal);
    }
}

void G_PortalProjectileImpact(gentity_t *projectile, trace_t *trace, portalType_t type) {
    vec3_t portalOrigin, portalNormal;
    gentity_t *owner;
    
    if (!projectile->parent || !projectile->parent->client) {
        return;
    }
    
    owner = projectile->parent;
    
    if (G_IsValidPortalSurface(trace)) {
        VectorMA(trace->endpos, 2.0f, trace->plane.normal, portalOrigin);
        VectorCopy(trace->plane.normal, portalNormal);
        G_CreatePortal(portalOrigin, portalNormal, owner, type);
    }
}

qboolean G_TraceThroughPortals(vec3_t start, vec3_t end, trace_t *trace, int passEntityNum) {
    int i;
    portalInfo_t *portal, *linkedPortal;
    vec3_t portalIntersect, relativePos, transformedEnd;
    trace_t portalTrace;
    
    trap_Trace(trace, start, NULL, NULL, end, passEntityNum, MASK_SHOT);
    
    if (trace->fraction >= 1.0f) {
        return qfalse;
    }
    
    for (i = 0; i < MAX_PORTAL_PAIRS * 2; i++) {
        portal = &g_portals[i];
        if (!portal->inUse || portal->state != PORTAL_STATE_ACTIVE) {
            continue;
        }
        
        if (portal->linkedPortalNum < 0) {
            continue;
        }
        
        gentity_t *linkedEnt = &g_entities[portal->linkedPortalNum];
        if (!linkedEnt->inuse) {
            continue;
        }
        
        int linkedSlot = linkedEnt->genericValue1;
        if (linkedSlot < 0 || linkedSlot >= MAX_PORTAL_PAIRS * 2) {
            continue;
        }
        
        linkedPortal = &g_portals[linkedSlot];
        
        trap_Trace(&portalTrace, start, NULL, NULL, portal->origin, passEntityNum, MASK_SHOT);
        
        if (portalTrace.fraction < trace->fraction) {
            VectorSubtract(portalTrace.endpos, portal->origin, relativePos);
            float dist = DotProduct(relativePos, portal->portalForward);
            
            if (fabs(dist) < 10.0f && VectorLength(relativePos) < portal->radius) {
                VectorSubtract(end, start, relativePos);
                G_TransformVelocityThroughPortal(relativePos, portal->surfaceNormal, linkedPortal->surfaceNormal);
                VectorAdd(linkedPortal->origin, relativePos, transformedEnd);
                
                trap_Trace(trace, linkedPortal->origin, NULL, NULL, transformedEnd, passEntityNum, MASK_SHOT);
                return qtrue;
            }
        }
    }
    
    return qfalse;
}

void G_UpdatePortalSystem(void) {
    int i;
    gentity_t *portal;
    
    // Update all active portals
    for (i = 0; i < MAX_PORTAL_PAIRS * 2; i++) {
        if (g_portals[i].inUse) {
            if (g_portals[i].entityNum >= 0 && g_portals[i].entityNum < MAX_GENTITIES) {
                portal = &g_entities[g_portals[i].entityNum];
                if (portal->inuse && portal->think) {
                    // Portal think is handled by entity system
                }
            }
        }
    }
}

void G_DebugPortalSystem(void) {
    int i, activeCount = 0, linkedCount = 0;
    
    G_Printf("\n^5===== Portal System Debug =====\n");
    
    for (i = 0; i < MAX_PORTAL_PAIRS * 2; i++) {
        if (g_portals[i].inUse) {
            activeCount++;
            if (g_portals[i].linkedPortalNum >= 0) {
                linkedCount++;
            }
            G_DebugDrawPortal(&g_portals[i]);
        }
    }
    
    G_Printf("^7Active portals: %d\n", activeCount);
    G_Printf("^7Linked portals: %d\n", linkedCount / 2);
    G_Printf("^5==============================\n");
}

void G_PrintPortalStats(void) {
    int i;
    int orangeCount = 0, blueCount = 0;
    int linkedPairs = 0;
    
    for (i = 0; i < MAX_PORTAL_PAIRS * 2; i++) {
        if (g_portals[i].inUse) {
            if (g_portals[i].type == PORTAL_ORANGE) {
                orangeCount++;
            } else {
                blueCount++;
            }
            if (g_portals[i].linkedPortalNum >= 0 && g_portals[i].type == PORTAL_ORANGE) {
                linkedPairs++;
            }
        }
    }
    
    G_Printf("\n^6===== Portal Statistics =====\n");
    G_Printf("^3Orange portals: %d\n", orangeCount);
    G_Printf("^4Blue portals: %d\n", blueCount);
    G_Printf("^2Linked pairs: %d\n", linkedPairs);
    G_Printf("^7Free slots: %d\n", (MAX_PORTAL_PAIRS * 2) - orangeCount - blueCount);
    G_Printf("^6============================\n");
}

void G_ProcessPortalCommands(gentity_t *ent, usercmd_t *ucmd) {
    if (!ent || !ent->client || !ucmd) {
        return;
    }
    
    // Check for portal firing commands (if using custom buttons)
    // This is a placeholder - actual button implementation depends on client mod
    
    // Store old buttons for edge detection
    ent->client->oldbuttons = ent->client->buttons;
    ent->client->buttons = ucmd->buttons;
}

qboolean G_CheckPortalFallDamageImmunity(gentity_t *ent) {
    int clientNum;
    
    if (!ent || !ent->client) {
        return qfalse;
    }
    
    clientNum = ent->client - g_clients;
    if (clientNum < 0 || clientNum >= MAX_CLIENTS) {
        return qfalse;
    }
    
    if (g_playerPortalStates[clientNum].fallDamageImmunityEndTime > level.time) {
        return qtrue;
    }
    
    return qfalse;
}

void G_UpdatePortalRendering(void) {
    // Placeholder for portal rendering updates
    // This would handle visual updates for portals
}

void G_DrawPortalDebugInfo(void) {
    // Placeholder for debug drawing
    // This would draw debug info in the 3D world
}

void G_AddPortalCommands(void) {
    // Register portal console commands
    // This would be called during initialization
}