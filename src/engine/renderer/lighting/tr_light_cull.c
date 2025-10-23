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

#include "../core/tr_local.h"
#include "tr_light.h"
#include "tr_interaction.h"
#include "tr_light_dynamic.h"

// External references
extern lightSystem_t tr_lightSystem;

// Allow lights to remain visible slightly beyond their cutoff radius so
// large probes and emissive volumes do not pop.
static const float LIGHT_CULL_HYSTERESIS = 8192.0f;

/*
===============
R_CullLights

Cull lights against view frustum and PVS
===============
*/
void R_CullLights(viewParms_t *view) {
    int i;
    renderLight_t *light;
    float distance;
    vec3_t delta;
    
    tr_lightSystem.numVisibleLights = 0;
    
    for (i = 0; i < tr_lightSystem.numActiveLights; i++) {
        light = tr_lightSystem.activeLights[i];
        
        // Distance cull
        VectorSubtract(light->origin, view->or.origin, delta);
        distance = VectorLength(delta);
        if (distance > light->cutoffDistance + LIGHT_CULL_HYSTERESIS) {
            continue;
        }
        
        // Frustum cull
        {
            vec3_t bounds[2];
            VectorCopy(light->mins, bounds[0]);
            VectorCopy(light->maxs, bounds[1]);
            if (R_CullBox(bounds)) {
                continue;
            }
        }
        
        // PVS cull
        if (!R_LightInPVS(light, view)) {
            continue;
        }
        
        // Add to visible list
        tr_lightSystem.visibleLights[tr_lightSystem.numVisibleLights++] = light;
        light->viewCount = tr.viewCount;
    }
}

/*
===============
R_LightInPVS

Check if light affects PVS
===============
*/
qboolean R_LightInPVS(renderLight_t *light, viewParms_t *view) {
    int lightCluster;
    int viewCluster;
    byte *vis;
    
    // Directional lights are always visible
    if (light->type == RL_DIRECTIONAL) {
        return qtrue;
    }
    
    // No PVS data available
    if (!tr.world || !tr.world->vis || !tr.world->numClusters) {
        return qtrue;
    }
    
    // Get light's cluster
    lightCluster = R_PointToCluster(light->origin);
    if (lightCluster < 0) {
        return qtrue; // Outside world, always visible
    }
    
    // Get view's cluster
    viewCluster = R_PointToCluster(view->or.origin);
    if (viewCluster < 0) {
        return qtrue; // Outside world, always visible
    }
    
    // Check PVS
    vis = ri.CM_ClusterPVS(viewCluster);
    if (vis[lightCluster >> 3] & (1 << (lightCluster & 7))) {
        return qtrue;
    }
    
    // For large radius lights, check if any part of the volume is visible
    if (light->radius > 256) {
        vec3_t testPoint;
        int testCluster;
        
        // Test corners of light volume
        for (int i = 0; i < 8; i++) {
            testPoint[0] = (i & 1) ? light->maxs[0] : light->mins[0];
            testPoint[1] = (i & 2) ? light->maxs[1] : light->mins[1];
            testPoint[2] = (i & 4) ? light->maxs[2] : light->mins[2];
            
            testCluster = R_PointToCluster(testPoint);
            if (testCluster >= 0 && (vis[testCluster >> 3] & (1 << (testCluster & 7)))) {
                return qtrue;
            }
        }
    }
    
    return qfalse;
}

/*
===============
R_SurfaceInLightVolume

Check if a surface is within light volume
===============
*/
qboolean R_SurfaceInLightVolume(drawSurf_t *surf, renderLight_t *light) {
    vec3_t surfCenter;
    vec3_t delta;
    float distance;
    
    // Quick bounds check
    if (!surf->bounds[0][0] && !surf->bounds[0][1] && !surf->bounds[0][2] &&
        !surf->bounds[1][0] && !surf->bounds[1][1] && !surf->bounds[1][2]) {
        // No bounds available, assume it's affected
        return qtrue;
    }
    
    // Check AABB overlap
    if (surf->bounds[0][0] > light->maxs[0] ||
        surf->bounds[0][1] > light->maxs[1] ||
        surf->bounds[0][2] > light->maxs[2] ||
        surf->bounds[1][0] < light->mins[0] ||
        surf->bounds[1][1] < light->mins[1] ||
        surf->bounds[1][2] < light->mins[2]) {
        return qfalse;
    }
    
    // For point lights, do radius check
    if (light->type == RL_OMNI) {
        VectorAdd(surf->bounds[0], surf->bounds[1], surfCenter);
        VectorScale(surfCenter, 0.5f, surfCenter);
        
        VectorSubtract(surfCenter, light->origin, delta);
        distance = VectorLength(delta);
        
        // Account for surface size
        float surfRadius = Distance(surf->bounds[0], surf->bounds[1]) * 0.5f;
        if (distance - surfRadius > light->radius) {
            return qfalse;
        }
    }
    
    return qtrue;
}

/*
===============
R_CullInteractions

Cull interactions for a light
===============
*/
void R_CullLightInteractions(renderLight_t *light, viewParms_t *view) {
    interaction_t *inter;
    int numCulled = 0;
    
    inter = light->firstInteraction;
    while (inter) {
        // Reset culled flag
        inter->culled = qfalse;
        
        // Empty interactions are always culled
        if (inter->isEmpty) {
            inter->culled = qtrue;
            numCulled++;
            inter = inter->lightNext;
            continue;
        }
        
        // Frustum cull interaction bounds
        if (R_CullBox(inter->bounds)) {
            inter->culled = qtrue;
            numCulled++;
        }
        
        // Calculate scissor rectangle for visible interactions
        if (!inter->culled) {
            R_CalculateInteractionScissor(inter, view);
        }
        
        inter = inter->lightNext;
    }
    
    tr_lightSystem.interactionMgr.numCulled += numCulled;
}

/*
===============
R_CalculateInteractionScissor

Calculate screen-space scissor rectangle for an interaction
===============
*/
void R_CalculateInteractionScissor(interaction_t *inter, viewParms_t *view) {
    vec3_t corners[8];
    vec2_t screenMin, screenMax;
    vec4_t clip;
    vec3_t world;
    int i;
    
    // Initialize scissor to full screen
    inter->scissorRect[0] = view->viewportX;
    inter->scissorRect[1] = view->viewportY;
    inter->scissorRect[2] = view->viewportX + view->viewportWidth;
    inter->scissorRect[3] = view->viewportY + view->viewportHeight;
    
    // Calculate corners of interaction bounds
    for (i = 0; i < 8; i++) {
        corners[i][0] = (i & 1) ? inter->bounds[1][0] : inter->bounds[0][0];
        corners[i][1] = (i & 2) ? inter->bounds[1][1] : inter->bounds[0][1];
        corners[i][2] = (i & 4) ? inter->bounds[1][2] : inter->bounds[0][2];
    }
    
    // Initialize screen bounds
    screenMin[0] = screenMin[1] = 999999;
    screenMax[0] = screenMax[1] = -999999;
    
    // Project corners to screen space
    for (i = 0; i < 8; i++) {
        VectorCopy(corners[i], world);
        
        // Transform to clip space
        clip[0] = DotProduct(world, view->or.axis[0]);
        clip[1] = DotProduct(world, view->or.axis[1]);
        clip[2] = DotProduct(world, view->or.axis[2]);
        clip[3] = 1.0f;
        
        // Apply projection
        clip[0] = clip[0] * view->projectionMatrix[0] + clip[2] * view->projectionMatrix[8];
        clip[1] = clip[1] * view->projectionMatrix[5] + clip[2] * view->projectionMatrix[9];
        clip[2] = clip[2] * view->projectionMatrix[10] + view->projectionMatrix[14];
        clip[3] = clip[2]; // For perspective divide
        
        // Skip if behind near plane
        if (clip[3] <= 0) {
            continue;
        }
        
        // Perspective divide
        clip[0] /= clip[3];
        clip[1] /= clip[3];
        
        // Convert to screen coordinates
        float x = (clip[0] * 0.5f + 0.5f) * view->viewportWidth + view->viewportX;
        float y = (clip[1] * 0.5f + 0.5f) * view->viewportHeight + view->viewportY;
        
        // Update bounds
        if (x < screenMin[0]) screenMin[0] = x;
        if (y < screenMin[1]) screenMin[1] = y;
        if (x > screenMax[0]) screenMax[0] = x;
        if (y > screenMax[1]) screenMax[1] = y;
    }
    
    // Clamp to viewport
    if (screenMin[0] < view->viewportX) screenMin[0] = view->viewportX;
    if (screenMin[1] < view->viewportY) screenMin[1] = view->viewportY;
    if (screenMax[0] > view->viewportX + view->viewportWidth) 
        screenMax[0] = view->viewportX + view->viewportWidth;
    if (screenMax[1] > view->viewportY + view->viewportHeight) 
        screenMax[1] = view->viewportY + view->viewportHeight;
    
    // Store scissor rectangle
    inter->scissorRect[0] = (int)screenMin[0];
    inter->scissorRect[1] = (int)screenMin[1];
    inter->scissorRect[2] = (int)screenMax[0];
    inter->scissorRect[3] = (int)screenMax[1];
}

/*
===============
R_OptimizeLightCulling

Optimize light culling using spatial structures
===============
*/
void R_OptimizeLightCulling(void) {
    // Build light grid for spatial queries
    R_BuildLightGrid();
    
    // Sort lights by importance/distance
    // This could help with early rejection of less important lights
    // TODO: Implement light importance sorting
}

/*
===============
R_GetLightsInCell

Get lights affecting a grid cell
===============
*/
renderLight_t** R_GetLightsInCell(vec3_t point) {
    static renderLight_t *cellLights[32];
    int numLights = 0;
    int i;
    vec3_t delta;
    float distance;
    
    // Check all visible lights
    for (i = 0; i < tr_lightSystem.numVisibleLights && numLights < 32; i++) {
        renderLight_t *light = tr_lightSystem.visibleLights[i];
        
        // Check if point is in light volume
        if (light->type == RL_DIRECTIONAL) {
            cellLights[numLights++] = light;
        } else if (light->type == RL_OMNI) {
            VectorSubtract(point, light->origin, delta);
            distance = VectorLength(delta);
            if (distance <= light->radius) {
                cellLights[numLights++] = light;
            }
        } else if (light->type == RL_PROJ) {
            // Check if point is in frustum
            if (point[0] >= light->mins[0] && point[0] <= light->maxs[0] &&
                point[1] >= light->mins[1] && point[1] <= light->maxs[1] &&
                point[2] >= light->mins[2] && point[2] <= light->maxs[2]) {
                cellLights[numLights++] = light;
            }
        }
    }
    
    // Null terminate
    if (numLights < 32) {
        cellLights[numLights] = NULL;
    }
    
    return cellLights;
}
