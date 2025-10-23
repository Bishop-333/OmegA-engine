/*
===========================================================================
Portal System Utility Functions
Helper functions for portal placement and management
===========================================================================
*/

#include "g_portal.h"
#include "../../../engine/common/q_shared.h"

// External declarations
extern gentity_t *g_entities;
extern level_locals_t level;

/*
================
Distance

Calculate distance between two points
================
*/
float Distance(const vec3_t p1, const vec3_t p2) {
    vec3_t v;
    VectorSubtract(p2, p1, v);
    return VectorLength(v);
}

/*
================
vectoangles

Convert a direction vector to angles
================
*/
void vectoangles(const vec3_t value1, vec3_t angles) {
    float forward;
    float yaw, pitch;
    
    if (value1[1] == 0 && value1[0] == 0) {
        yaw = 0;
        if (value1[2] > 0) {
            pitch = 90;
        } else {
            pitch = 270;
        }
    } else {
        if (value1[0]) {
            yaw = (atan2(value1[1], value1[0]) * 180 / M_PI);
        } else if (value1[1] > 0) {
            yaw = 90;
        } else {
            yaw = 270;
        }
        if (yaw < 0) {
            yaw += 360;
        }
        
        forward = sqrt(value1[0] * value1[0] + value1[1] * value1[1]);
        pitch = (atan2(value1[2], forward) * 180 / M_PI);
        if (pitch < 0) {
            pitch += 360;
        }
    }
    
    angles[PITCH] = -pitch;
    angles[YAW] = yaw;
    angles[ROLL] = 0;
}

/*
================
SetClientViewAngle

Set client's view angles
================
*/
void SetClientViewAngle(gentity_t *ent, vec3_t angle) {
    int i;
    
    if (!ent || !ent->client) {
        return;
    }
    
    // Set the delta angles
    for (i = 0; i < 3; i++) {
        int cmdAngle = ((int)(angle[i] * 65536.0f / 360.0f) & 65535);
        ent->client->ps.delta_angles[i] = cmdAngle - ent->client->pers.cmd.angles[i];
    }
    
    VectorCopy(angle, ent->s.angles);
    VectorCopy(ent->s.angles, ent->client->ps.viewangles);
}

/*
================
G_ValidatePortalPlacement

Check if a portal can be placed at the given location
Returns qtrue if valid, qfalse otherwise
================
*/
qboolean G_ValidatePortalPlacement(vec3_t origin, vec3_t normal, float radius) {
    trace_t trace;
    vec3_t testPoints[4];
    vec3_t right, up;
    int i;
    
    // Generate perpendicular vectors
    if (fabs(normal[2]) > 0.9f) {
        VectorSet(up, 1, 0, 0);
    } else {
        VectorSet(up, 0, 0, 1);
    }
    
    vec3_t forward;
    VectorNegate(normal, forward);
    CrossProduct(up, forward, right);
    VectorNormalize(right);
    CrossProduct(forward, right, up);
    VectorNormalize(up);
    
    // Test 4 points around the portal edge
    VectorMA(origin, radius, right, testPoints[0]);
    VectorMA(origin, -radius, right, testPoints[1]);
    VectorMA(origin, radius, up, testPoints[2]);
    VectorMA(origin, -radius, up, testPoints[3]);
    
    // Check each test point
    for (i = 0; i < 4; i++) {
        vec3_t end;
        VectorMA(testPoints[i], -10, normal, end);
        
        trap_Trace(&trace, testPoints[i], NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID);
        
        if (trace.fraction >= 1.0f) {
            // No solid surface behind this point
            return qfalse;
        }
        
        // Check if normal is similar (within 45 degrees)
        if (DotProduct(trace.plane.normal, normal) < 0.7f) {
            // Surface normal too different
            return qfalse;
        }
    }
    
    return qtrue;
}

/*
================
G_FindNearestPortalSurface

Find the best surface for portal placement near the given point
================
*/
qboolean G_FindNearestPortalSurface(vec3_t point, vec3_t normal, vec3_t outOrigin, vec3_t outNormal) {
    trace_t traces[6];
    vec3_t directions[6] = {
        {1, 0, 0}, {-1, 0, 0},
        {0, 1, 0}, {0, -1, 0},
        {0, 0, 1}, {0, 0, -1}
    };
    int i, bestTrace = -1;
    float bestDot = -1.0f;
    
    // Trace in 6 cardinal directions
    for (i = 0; i < 6; i++) {
        vec3_t end;
        VectorMA(point, 128, directions[i], end);
        
        trap_Trace(&traces[i], point, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID);
        
        if (traces[i].fraction < 1.0f && traces[i].fraction > 0.1f) {
            // Check surface validity
            if (!(traces[i].surfaceFlags & (SURF_SKY | SURF_NOIMPACT))) {
                float dot = DotProduct(traces[i].plane.normal, normal);
                if (dot > bestDot) {
                    bestDot = dot;
                    bestTrace = i;
                }
            }
        }
    }
    
    if (bestTrace >= 0) {
        VectorCopy(traces[bestTrace].endpos, outOrigin);
        VectorCopy(traces[bestTrace].plane.normal, outNormal);
        
        // Offset from surface
        VectorMA(outOrigin, 2.0f, outNormal, outOrigin);
        return qtrue;
    }
    
    return qfalse;
}

/*
================
G_PortalViewTransform

Transform view position and angles through a portal pair
================
*/
void G_PortalViewTransform(vec3_t viewOrigin, vec3_t viewAngles, 
                          portalInfo_t *enterPortal, portalInfo_t *exitPortal,
                          vec3_t outOrigin, vec3_t outAngles) {
    vec3_t localPos, worldPos;
    vec3_t forward, right, up;
    vec3_t newForward, newRight, newUp;
    matrix3_t enterMatrix, exitMatrix, transform;
    
    // Get position relative to enter portal
    VectorSubtract(viewOrigin, enterPortal->origin, localPos);
    
    // Build transformation matrices
    VectorCopy(enterPortal->portalRight, enterMatrix[0]);
    VectorCopy(enterPortal->portalUp, enterMatrix[1]);
    VectorCopy(enterPortal->portalForward, enterMatrix[2]);
    
    VectorCopy(exitPortal->portalRight, exitMatrix[0]);
    VectorCopy(exitPortal->portalUp, exitMatrix[1]);
    VectorNegate(exitPortal->portalForward, exitMatrix[2]); // Flip for exit
    
    // Transform position
    float x = DotProduct(localPos, enterMatrix[0]);
    float y = DotProduct(localPos, enterMatrix[1]);
    float z = DotProduct(localPos, enterMatrix[2]);
    
    // Mirror through portal
    z = -z;
    
    // Transform to exit portal space
    VectorScale(exitMatrix[0], x, worldPos);
    VectorMA(worldPos, y, exitMatrix[1], worldPos);
    VectorMA(worldPos, z, exitMatrix[2], worldPos);
    VectorAdd(worldPos, exitPortal->origin, outOrigin);
    
    // Transform view angles
    AngleVectors(viewAngles, forward, right, up);
    
    // Transform to enter portal space
    vec3_t localForward, localRight, localUp;
    localForward[0] = DotProduct(forward, enterMatrix[0]);
    localForward[1] = DotProduct(forward, enterMatrix[1]);
    localForward[2] = DotProduct(forward, enterMatrix[2]);
    
    localRight[0] = DotProduct(right, enterMatrix[0]);
    localRight[1] = DotProduct(right, enterMatrix[1]);
    localRight[2] = DotProduct(right, enterMatrix[2]);
    
    localUp[0] = DotProduct(up, enterMatrix[0]);
    localUp[1] = DotProduct(up, enterMatrix[1]);
    localUp[2] = DotProduct(up, enterMatrix[2]);
    
    // Mirror through portal
    localForward[2] = -localForward[2];
    localRight[2] = -localRight[2];
    localUp[2] = -localUp[2];
    
    // Transform to world space from exit portal
    VectorScale(exitMatrix[0], localForward[0], newForward);
    VectorMA(newForward, localForward[1], exitMatrix[1], newForward);
    VectorMA(newForward, localForward[2], exitMatrix[2], newForward);
    
    VectorScale(exitMatrix[0], localRight[0], newRight);
    VectorMA(newRight, localRight[1], exitMatrix[1], newRight);
    VectorMA(newRight, localRight[2], exitMatrix[2], newRight);
    
    VectorScale(exitMatrix[0], localUp[0], newUp);
    VectorMA(newUp, localUp[1], exitMatrix[1], newUp);
    VectorMA(newUp, localUp[2], exitMatrix[2], newUp);
    
    // Convert back to angles
    vectoangles(newForward, outAngles);
}

/*
================
G_CheckPortalCollision

Check if an entity would collide with a portal
================
*/
qboolean G_CheckPortalCollision(vec3_t origin, vec3_t mins, vec3_t maxs, portalInfo_t *portal) {
    vec3_t portalMins, portalMaxs;
    
    // Calculate portal bounds
    VectorSet(portalMins, -portal->radius, -portal->radius, -portal->radius);
    VectorSet(portalMaxs, portal->radius, portal->radius, portal->radius);
    VectorAdd(portal->origin, portalMins, portalMins);
    VectorAdd(portal->origin, portalMaxs, portalMaxs);
    
    // Add entity bounds
    vec3_t entMins, entMaxs;
    VectorAdd(origin, mins, entMins);
    VectorAdd(origin, maxs, entMaxs);
    
    // Check AABB intersection
    if (entMins[0] > portalMaxs[0] || entMaxs[0] < portalMins[0]) return qfalse;
    if (entMins[1] > portalMaxs[1] || entMaxs[1] < portalMins[1]) return qfalse;
    if (entMins[2] > portalMaxs[2] || entMaxs[2] < portalMins[2]) return qfalse;
    
    // Additional check: is entity in front of portal?
    vec3_t toEntity;
    VectorSubtract(origin, portal->origin, toEntity);
    float dist = DotProduct(toEntity, portal->surfaceNormal);
    
    // Must be on the front side of the portal
    return (dist > -portal->radius && dist < portal->radius * 2);
}

/*
================
G_DebugDrawPortal

Draw debug visualization for a portal (for development)
================
*/
void G_DebugDrawPortal(portalInfo_t *portal) {
    if (!portal || !portal->inUse) {
        return;
    }
    
    char *colorStr = (portal->type == PORTAL_ORANGE) ? "^3" : "^4";
    char *stateStr = "UNKNOWN";
    
    switch (portal->state) {
        case PORTAL_STATE_INACTIVE: stateStr = "INACTIVE"; break;
        case PORTAL_STATE_OPENING: stateStr = "OPENING"; break;
        case PORTAL_STATE_ACTIVE: stateStr = "ACTIVE"; break;
        case PORTAL_STATE_CLOSING: stateStr = "CLOSING"; break;
        case PORTAL_STATE_CLOSED: stateStr = "CLOSED"; break;
    }
    
    G_Printf("%sPortal %s [%s] at (%.0f, %.0f, %.0f) linked=%s\n",
             colorStr,
             portal->type == PORTAL_ORANGE ? "ORANGE" : "BLUE",
             stateStr,
             portal->origin[0], portal->origin[1], portal->origin[2],
             portal->linkedPortalNum >= 0 ? "YES" : "NO");
}