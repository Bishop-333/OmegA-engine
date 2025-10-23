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
extern cvar_t *r_maxLights;

// Forward declarations
static void R_CalculateFrustumBounds(renderLight_t *light);
static void R_BuildLightProjectionMatrix(renderLight_t *light);

// Helper function for matrix identity
static void MatrixIdentity(mat3_t m) {
    m[0] = 1; m[1] = 0; m[2] = 0;
    m[3] = 0; m[4] = 1; m[5] = 0;
    m[6] = 0; m[7] = 0; m[8] = 1;
}

// Helper function to find which BSP area a point is in
static int R_FindPointArea(vec3_t point) {
    mnode_t *node;
    cplane_t *plane;
    float d;
    
    if (!tr.world || !tr.world->nodes) {
        return -1;
    }
    
    node = tr.world->nodes;
    while (node->contents == -1) {  // -1 means it's a node, not a leaf
        plane = node->plane;
        d = DotProduct(point, plane->normal) - plane->dist;
        
        if (d < 0) {
            node = node->children[1];
        } else {
            node = node->children[0];
        }
        
        if (!node) {
            return -1;
        }
    }
    
    // Now node is a leaf
    return node->area;
}

/*
===============
R_CreateLight

Create a new light
===============
*/
renderLight_t* R_CreateLight(void) {
    if (tr_lightSystem.numLights >= MAX_RENDER_LIGHTS) {
        ri.Printf(PRINT_WARNING, "R_CreateLight: MAX_RENDER_LIGHTS hit\n");
        return NULL;
    }
    
    renderLight_t *light = &tr_lightSystem.lights[tr_lightSystem.numLights++];
    Com_Memset(light, 0, sizeof(renderLight_t));
    
    // Set defaults
    light->index = tr_lightSystem.numLights - 1;
    light->type = RL_OMNI;
    light->intensity = 1.0f;
    light->constant = 1.0f;
    light->linear = 0.0f;
    light->quadratic = 1.0f;
    light->cutoffDistance = 512.0f;
    light->shadowBias = 0.005f;
    light->shadowSoftness = 1.0f;
    light->areaNum = -1;
    VectorSet(light->color, 1.0f, 1.0f, 1.0f);
    MatrixIdentity(light->axis);
    
    // Spot light defaults
    light->fovX = 90.0f;
    light->fovY = 90.0f;
    light->nearClip = 1.0f;
    light->farClip = 1000.0f;
    
    return light;
}

/*
===============
R_FreeLight

Free a light and its resources
===============
*/
void R_FreeLight(renderLight_t *light) {
    if (!light) {
        return;
    }
    
    // Clear all interactions
    R_ClearLightInteractions(light);
    
    // Remove from area list if needed
    if (light->areaNum >= 0) {
        R_RemoveLightFromArea(light);
    }
    
    // Clear the light structure
    Com_Memset(light, 0, sizeof(renderLight_t));
}

/*
===============
RE_AddDynamicLight

Add light from game (replaces RE_AddLightToScene)
===============
*/
void RE_AddDynamicLight(const vec3_t origin, float radius, float r, float g, float b) {
    renderLight_t *light;
    
    light = R_CreateLight();
    if (!light) {
        return;
    }
    
    VectorCopy(origin, light->origin);
    light->radius = radius;
    light->cutoffDistance = radius;
    VectorSet(light->color, r, g, b);
    light->type = RL_OMNI;
    light->isStatic = qfalse;
    
    // Calculate bounds
    VectorSet(light->mins, 
              origin[0] - radius, 
              origin[1] - radius, 
              origin[2] - radius);
    VectorSet(light->maxs, 
              origin[0] + radius, 
              origin[1] + radius, 
              origin[2] + radius);
    
    // Add to active list
    if (tr_lightSystem.numActiveLights < MAX_RENDER_LIGHTS) {
        tr_lightSystem.activeLights[tr_lightSystem.numActiveLights++] = light;
    }
}

/*
===============
R_CreatePointLight

Create a point light
===============
*/
renderLight_t* R_CreatePointLight(const vec3_t origin, float radius, const vec3_t color) {
    renderLight_t *light = R_CreateLight();
    if (!light) {
        return NULL;
    }
    
    light->type = RL_OMNI;
    VectorCopy(origin, light->origin);
    light->radius = radius;
    light->cutoffDistance = radius;
    VectorCopy(color, light->color);
    
    R_UpdateLight(light);
    return light;
}

/*
===============
R_CreateSpotLight

Create a spot light
===============
*/
renderLight_t* R_CreateSpotLight(const vec3_t origin, const vec3_t target, 
                                  float fov, float radius, const vec3_t color) {
    renderLight_t *light = R_CreateLight();
    if (!light) {
        return NULL;
    }
    
    light->type = RL_PROJ;
    VectorCopy(origin, light->origin);
    VectorCopy(target, light->target);
    light->fovX = fov;
    light->fovY = fov;
    light->farClip = radius;
    light->cutoffDistance = radius;
    VectorCopy(color, light->color);
    
    R_UpdateLight(light);
    return light;
}

/*
===============
R_CreateDirectionalLight

Create a directional light (sun)
===============
*/
renderLight_t* R_CreateDirectionalLight(const vec3_t direction, const vec3_t color) {
    renderLight_t *light = R_CreateLight();
    if (!light) {
        return NULL;
    }
    
    light->type = RL_DIRECTIONAL;
    VectorCopy(direction, light->target);
    VectorCopy(color, light->color);
    light->flags |= LIGHTFLAG_PARALLEL;
    
    // Directional lights have infinite bounds
    VectorSet(light->mins, -99999, -99999, -99999);
    VectorSet(light->maxs, 99999, 99999, 99999);
    
    R_UpdateLight(light);
    return light;
}

/*
===============
R_UpdateLight

Update light properties
===============
*/
void R_UpdateLight(renderLight_t *light) {
    vec3_t forward, right, up;
    
    if (!light) {
        return;
    }
    
    // Recalculate bounds
    switch (light->type) {
    case RL_OMNI:
        VectorSet(light->mins, 
                 light->origin[0] - light->radius,
                 light->origin[1] - light->radius,
                 light->origin[2] - light->radius);
        VectorSet(light->maxs,
                 light->origin[0] + light->radius,
                 light->origin[1] + light->radius,
                 light->origin[2] + light->radius);
        break;
        
    case RL_PROJ:
        // Calculate frustum bounds
        R_CalculateFrustumBounds(light);
        break;
        
    case RL_DIRECTIONAL:
        // Infinite bounds
        VectorSet(light->mins, -99999, -99999, -99999);
        VectorSet(light->maxs, 99999, 99999, 99999);
        break;
        
    default:
        break;
    }
    
    // Update view matrix for projected lights
    if (light->type == RL_PROJ) {
        VectorSubtract(light->target, light->origin, forward);
        VectorNormalize(forward);
        
        // Build orthonormal basis
        if (fabs(forward[2]) < 0.9f) {
            VectorSet(up, 0, 0, 1);
        } else {
            VectorSet(up, 1, 0, 0);
        }
        
        CrossProduct(forward, up, right);
        VectorNormalize(right);
        CrossProduct(right, forward, up);
        VectorNormalize(up);
        
        // Build view matrix
        light->viewMatrix[0] = right[0];
        light->viewMatrix[4] = right[1];
        light->viewMatrix[8] = right[2];
        light->viewMatrix[12] = -DotProduct(right, light->origin);
        
        light->viewMatrix[1] = up[0];
        light->viewMatrix[5] = up[1];
        light->viewMatrix[9] = up[2];
        light->viewMatrix[13] = -DotProduct(up, light->origin);
        
        light->viewMatrix[2] = -forward[0];
        light->viewMatrix[6] = -forward[1];
        light->viewMatrix[10] = -forward[2];
        light->viewMatrix[14] = DotProduct(forward, light->origin);
        
        light->viewMatrix[3] = 0;
        light->viewMatrix[7] = 0;
        light->viewMatrix[11] = 0;
        light->viewMatrix[15] = 1;
        
        // Build projection matrix
        R_BuildLightProjectionMatrix(light);
    }
    
    // Find which BSP area the light is in
    if (tr.world && tr.world->numClusters) {
        light->areaNum = R_FindPointArea(light->origin);
        R_AddLightToArea(light, light->areaNum);
    }
    
    light->needsUpdate = qtrue;
    light->lastUpdateFrame = tr_lightSystem.frameCount;
}

/*
===============
R_CalculateFrustumBounds

Calculate bounding box for a frustum light
===============
*/
void R_CalculateFrustumBounds(renderLight_t *light) {
    vec3_t corners[8];
    vec3_t forward, right, up;
    float nearWidth, nearHeight;
    float farWidth, farHeight;
    int i;
    
    // Calculate frustum corners
    VectorSubtract(light->target, light->origin, forward);
    VectorNormalize(forward);
    
    if (fabs(forward[2]) < 0.9f) {
        VectorSet(up, 0, 0, 1);
    } else {
        VectorSet(up, 1, 0, 0);
    }
    
    CrossProduct(forward, up, right);
    VectorNormalize(right);
    CrossProduct(right, forward, up);
    VectorNormalize(up);
    
    // Calculate dimensions at near and far planes
    nearWidth = light->nearClip * tan(DEG2RAD(light->fovX * 0.5f));
    nearHeight = light->nearClip * tan(DEG2RAD(light->fovY * 0.5f));
    farWidth = light->farClip * tan(DEG2RAD(light->fovX * 0.5f));
    farHeight = light->farClip * tan(DEG2RAD(light->fovY * 0.5f));
    
    // Near plane corners
    VectorMA(light->origin, light->nearClip, forward, corners[0]);
    VectorMA(corners[0], -nearWidth, right, corners[0]);
    VectorMA(corners[0], -nearHeight, up, corners[0]);
    
    VectorMA(light->origin, light->nearClip, forward, corners[1]);
    VectorMA(corners[1], nearWidth, right, corners[1]);
    VectorMA(corners[1], -nearHeight, up, corners[1]);
    
    VectorMA(light->origin, light->nearClip, forward, corners[2]);
    VectorMA(corners[2], nearWidth, right, corners[2]);
    VectorMA(corners[2], nearHeight, up, corners[2]);
    
    VectorMA(light->origin, light->nearClip, forward, corners[3]);
    VectorMA(corners[3], -nearWidth, right, corners[3]);
    VectorMA(corners[3], nearHeight, up, corners[3]);
    
    // Far plane corners
    VectorMA(light->origin, light->farClip, forward, corners[4]);
    VectorMA(corners[4], -farWidth, right, corners[4]);
    VectorMA(corners[4], -farHeight, up, corners[4]);
    
    VectorMA(light->origin, light->farClip, forward, corners[5]);
    VectorMA(corners[5], farWidth, right, corners[5]);
    VectorMA(corners[5], -farHeight, up, corners[5]);
    
    VectorMA(light->origin, light->farClip, forward, corners[6]);
    VectorMA(corners[6], farWidth, right, corners[6]);
    VectorMA(corners[6], farHeight, up, corners[6]);
    
    VectorMA(light->origin, light->farClip, forward, corners[7]);
    VectorMA(corners[7], -farWidth, right, corners[7]);
    VectorMA(corners[7], farHeight, up, corners[7]);
    
    // Calculate bounding box
    VectorCopy(corners[0], light->mins);
    VectorCopy(corners[0], light->maxs);
    
    for (i = 1; i < 8; i++) {
        AddPointToBounds(corners[i], light->mins, light->maxs);
    }
    
    // Include light origin
    AddPointToBounds(light->origin, light->mins, light->maxs);
}

/*
===============
R_BuildLightProjectionMatrix

Build projection matrix for spot light
===============
*/
void R_BuildLightProjectionMatrix(renderLight_t *light) {
    float xScale, yScale, zScale;
    
    if (light->type != RL_PROJ) {
        return;
    }
    
    xScale = 1.0f / tan(DEG2RAD(light->fovX * 0.5f));
    yScale = 1.0f / tan(DEG2RAD(light->fovY * 0.5f));
    zScale = light->farClip / (light->farClip - light->nearClip);
    
    Com_Memset(light->projectionMatrix, 0, sizeof(light->projectionMatrix));
    light->projectionMatrix[0] = xScale;
    light->projectionMatrix[5] = yScale;
    light->projectionMatrix[10] = zScale;
    light->projectionMatrix[11] = 1.0f;
    light->projectionMatrix[14] = -light->nearClip * zScale;
}
