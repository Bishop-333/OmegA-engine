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
extern cvar_t *r_showLightVolumes;

// Forward declarations for missing functions
void R_DrawLightGrid(void);
int R_PointInArea(vec3_t point);
void R_DrawString(int x, int y, const char *str, vec4_t color);
void R_AddRefEntityToScene(const refEntity_t *ent);

// Debug colors
static vec4_t colorRed = {1.0f, 0.0f, 0.0f, 0.3f};
static vec4_t colorGreen = {0.0f, 1.0f, 0.0f, 0.3f};
static vec4_t colorBlue = {0.0f, 0.0f, 1.0f, 0.3f};
static vec4_t colorYellow = {1.0f, 1.0f, 0.0f, 0.3f};
static vec4_t colorMagenta = {1.0f, 0.0f, 1.0f, 0.3f};
static vec4_t colorCyan = {0.0f, 1.0f, 1.0f, 0.3f};
static vec4_t colorWhite = {1.0f, 1.0f, 1.0f, 0.3f};

/*
===============
R_DebugLine

Draw a debug line
===============
*/
static void R_DebugLine(vec3_t start, vec3_t end, vec4_t color) {
    refEntity_t ent;
    
    memset(&ent, 0, sizeof(ent));
    ent.reType = RT_POLY;
    ent.radius = 1;
    VectorCopy(start, ent.origin);
    VectorCopy(end, ent.oldorigin);
    ent.shader.rgba[0] = color[0] * 255;
    ent.shader.rgba[1] = color[1] * 255;
    ent.shader.rgba[2] = color[2] * 255;
    ent.shader.rgba[3] = color[3] * 255;
    
    R_AddRefEntityToScene(&ent);
}

/*
===============
R_DebugBounds

Draw debug bounding box
===============
*/
void R_DebugBounds(vec3_t mins, vec3_t maxs, vec4_t color) {
    vec3_t corners[8];
    int i;
    
    // Calculate corners
    for (i = 0; i < 8; i++) {
        corners[i][0] = (i & 1) ? maxs[0] : mins[0];
        corners[i][1] = (i & 2) ? maxs[1] : mins[1];
        corners[i][2] = (i & 4) ? maxs[2] : mins[2];
    }
    
    // Draw edges
    R_DebugLine(corners[0], corners[1], color);
    R_DebugLine(corners[0], corners[2], color);
    R_DebugLine(corners[0], corners[4], color);
    R_DebugLine(corners[1], corners[3], color);
    R_DebugLine(corners[1], corners[5], color);
    R_DebugLine(corners[2], corners[3], color);
    R_DebugLine(corners[2], corners[6], color);
    R_DebugLine(corners[3], corners[7], color);
    R_DebugLine(corners[4], corners[5], color);
    R_DebugLine(corners[4], corners[6], color);
    R_DebugLine(corners[5], corners[7], color);
    R_DebugLine(corners[6], corners[7], color);
}

/*
===============
R_DebugSphere

Draw debug sphere
===============
*/
void R_DebugSphere(vec3_t center, float radius, vec4_t color) {
    int i, j;
    vec3_t points[16];
    float angle;
    
    // Draw horizontal circle
    for (i = 0; i < 16; i++) {
        angle = (i * 2 * M_PI) / 16;
        points[i][0] = center[0] + radius * cos(angle);
        points[i][1] = center[1] + radius * sin(angle);
        points[i][2] = center[2];
    }
    
    for (i = 0; i < 16; i++) {
        R_DebugLine(points[i], points[(i + 1) % 16], color);
    }
    
    // Draw vertical circle (XZ plane)
    for (i = 0; i < 16; i++) {
        angle = (i * 2 * M_PI) / 16;
        points[i][0] = center[0] + radius * cos(angle);
        points[i][1] = center[1];
        points[i][2] = center[2] + radius * sin(angle);
    }
    
    for (i = 0; i < 16; i++) {
        R_DebugLine(points[i], points[(i + 1) % 16], color);
    }
    
    // Draw vertical circle (YZ plane)
    for (i = 0; i < 16; i++) {
        angle = (i * 2 * M_PI) / 16;
        points[i][0] = center[0];
        points[i][1] = center[1] + radius * cos(angle);
        points[i][2] = center[2] + radius * sin(angle);
    }
    
    for (i = 0; i < 16; i++) {
        R_DebugLine(points[i], points[(i + 1) % 16], color);
    }
}

/*
===============
R_DebugFrustum

Draw debug frustum for spot lights
===============
*/
void R_DebugFrustum(renderLight_t *light, vec4_t color) {
    vec3_t corners[8];
    vec3_t forward, right, up;
    float nearWidth, nearHeight;
    float farWidth, farHeight;
    int i;
    
    if (light->type != RL_PROJ) {
        return;
    }
    
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
    
    // Calculate dimensions
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
    
    // Draw frustum edges
    // Near plane
    R_DebugLine(corners[0], corners[1], color);
    R_DebugLine(corners[1], corners[2], color);
    R_DebugLine(corners[2], corners[3], color);
    R_DebugLine(corners[3], corners[0], color);
    
    // Far plane
    R_DebugLine(corners[4], corners[5], color);
    R_DebugLine(corners[5], corners[6], color);
    R_DebugLine(corners[6], corners[7], color);
    R_DebugLine(corners[7], corners[4], color);
    
    // Connecting edges
    R_DebugLine(corners[0], corners[4], color);
    R_DebugLine(corners[1], corners[5], color);
    R_DebugLine(corners[2], corners[6], color);
    R_DebugLine(corners[3], corners[7], color);
    
    // Draw line from origin to target
    R_DebugLine(light->origin, light->target, color);
}

/*
===============
R_DrawLightVolumes

Draw all light volumes for debugging
===============
*/
void R_DrawLightVolumes(void) {
    int i;
    renderLight_t *light;
    vec4_t color;
    
    if (!r_showLightVolumes || !r_showLightVolumes->integer) {
        return;
    }
    
    for (i = 0; i < tr_lightSystem.numVisibleLights; i++) {
        light = tr_lightSystem.visibleLights[i];
        
        // Choose color based on light properties
        VectorCopy(light->color, color);
        color[3] = 0.25f;
        
        switch (light->type) {
        case RL_OMNI:
            R_DebugSphere(light->origin, light->radius, color);
            break;
        case RL_PROJ:
            R_DebugFrustum(light, color);
            break;
        case RL_DIRECTIONAL:
            // Draw directional indicator
            {
                vec3_t end;
                VectorMA(light->origin, 1000, light->target, end);
                R_DebugLine(light->origin, end, color);
            }
            break;
        default:
            R_DebugBounds(light->mins, light->maxs, color);
            break;
        }
        
        // Draw light origin
        R_DebugSphere(light->origin, 5, colorWhite);
    }
}

/*
===============
R_DrawInteractions

Draw all light-surface interactions for debugging
===============
*/
void R_DrawInteractions(void) {
    int i;
    renderLight_t *light;
    interaction_t *inter;
    vec4_t color;
    
    if (!r_showLightVolumes || r_showLightVolumes->integer < 3) {
        return;
    }
    
    for (i = 0; i < tr_lightSystem.numVisibleLights; i++) {
        light = tr_lightSystem.visibleLights[i];
        
        // Set color based on light
        VectorCopy(light->color, color);
        color[3] = 0.1f;
        
        inter = light->firstInteraction;
        while (inter) {
            if (!inter->culled && !inter->isEmpty) {
                // Draw interaction bounds
                if (inter->receivesLight) {
                    R_DebugBounds(inter->bounds[0], inter->bounds[1], colorGreen);
                }
                if (inter->castsShadow) {
                    R_DebugBounds(inter->bounds[0], inter->bounds[1], colorRed);
                }
            }
            inter = inter->lightNext;
        }
    }
}

/*
===============
R_DrawShadowFrusta

Draw shadow frustums for debugging
===============
*/
void R_DrawShadowFrusta(void) {
    int i;
    renderLight_t *light;
    
    if (!r_showLightVolumes || r_showLightVolumes->integer < 2) {
        return;
    }
    
    for (i = 0; i < tr_lightSystem.numVisibleLights; i++) {
        light = tr_lightSystem.visibleLights[i];
        
        if (light->type == RL_PROJ || light->type == RL_DIRECTIONAL) {
            // Visualize projected/directional light volumes
            R_DebugFrustum(light, colorRed);
        }
    }
}

/*
===============
R_DrawLightStats

Draw on-screen statistics about the lighting system
===============
*/
void R_DrawLightStats(int x, int y) {
    char str[256];
    vec4_t color = {1, 1, 1, 1};
    
    if (!r_showLightVolumes || r_showLightVolumes->integer < 3) {
        return;
    }
    
    y += 20;
    Com_sprintf(str, sizeof(str), "Active Lights: %d", tr_lightSystem.numActiveLights);
    R_DrawString(x, y, str, color);
    
    y += 20;
    Com_sprintf(str, sizeof(str), "Visible Lights: %d", tr_lightSystem.numVisibleLights);
    R_DrawString(x, y, str, color);
    
    y += 20;
    Com_sprintf(str, sizeof(str), "Total Interactions: %d", 
                tr_lightSystem.interactionMgr.numInteractions);
    R_DrawString(x, y, str, color);
    
    y += 20;
    Com_sprintf(str, sizeof(str), "Culled Interactions: %d", 
                tr_lightSystem.interactionMgr.numCulled);
    R_DrawString(x, y, str, color);
    
    y += 20;
    Com_sprintf(str, sizeof(str), "Static Cached: %d", 
                tr_lightSystem.interactionMgr.numStaticCached);
    R_DrawString(x, y, str, color);
    
}

/*
===============
R_LightingDebugVisualization

Main entry point for all lighting debug visualization
===============
*/
void R_LightingDebugVisualization(void) {
    // Draw light volumes
    R_DrawLightVolumes();
    
    // Draw interactions
    R_DrawInteractions();
    
    // Draw shadow frustums
    R_DrawShadowFrusta();
    
    // Draw light grid
    R_DrawLightGrid();
    
    // Draw stats
    R_DrawLightStats(10, 100);
}
