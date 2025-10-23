/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

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
#include "tr_light_dynamic.h"

/*
================================================================================
Phase 6: Additive Lighting Backend - Simplified Implementation

This file provides the backend interface for per-pixel dynamic lighting.
The actual implementation will be renderer-specific (OpenGL/Vulkan).
================================================================================
*/

// Light uniform structure for GPU
typedef struct {
    vec4_t  position;       // xyz = position, w = radius
    vec4_t  color;          // rgb = color, a = intensity
    vec4_t  attenuation;    // x = constant, y = linear, z = quadratic, w = cutoff
    vec4_t  direction;      // For spot lights
} lightUniform_t;

// Global light uniforms buffer
static lightUniform_t lightUniforms[MAX_RENDER_LIGHTS];
static int currentLightUniform;

/*
================
RB_SetupLightingShader

Configure shader for per-pixel lighting
Note: Actual shader binding is renderer-specific
================
*/
void RB_SetupLightingShader(renderLight_t *light) {
    lightUniform_t *uniform;
    
    if (!light) {
        return;
    }
    
    // Setup light uniform
    uniform = &lightUniforms[currentLightUniform];
    
    VectorCopy(light->origin, uniform->position);
    uniform->position[3] = light->radius;
    
    VectorCopy(light->color, uniform->color);
    uniform->color[3] = light->intensity;
    
    uniform->attenuation[0] = light->constant;
    uniform->attenuation[1] = light->linear;
    uniform->attenuation[2] = light->quadratic;
    uniform->attenuation[3] = light->cutoffDistance;
    
    // Direction for spot lights
    if (light->type == RL_PROJ) {
        VectorSubtract(light->target, light->origin, uniform->direction);
        VectorNormalize(uniform->direction);
    }
    
    currentLightUniform++;
    
    // The actual shader binding will be handled by the renderer backend
}

/*
================
RB_DrawInteraction

Render a single light-surface interaction
Note: Actual rendering is deferred to the renderer backend
================
*/
void RB_DrawInteraction(interaction_t *inter) {
    if (!inter || !inter->surface) {
        return;
    }
    
    // The actual drawing will be handled by the renderer backend
    // This function primarily sets up the interaction for rendering
}

/*
================
RB_RenderBasePass

Render the base pass with ambient and lightmaps
Note: This is a placeholder for integration with the existing renderer
================
*/
void RB_RenderBasePass(void) {
    // The base pass is already handled by the existing renderer
    // This function exists for future integration
}

/*
================
RB_RenderLightingPasses

Main entry point for multi-pass lighting
Note: This provides the framework for additive lighting passes
================
*/
void RB_RenderLightingPasses(void) {
    int i;
    renderLight_t *light;
    interaction_t *inter;
    
    // Reset light uniform counter
    currentLightUniform = 0;
    
    // Check if we have any lights
    if (tr_lightSystem.numVisibleLights == 0) {
        return;
    }
    
    // Process each visible light
    for (i = 0; i < tr_lightSystem.numVisibleLights; i++) {
        light = tr_lightSystem.visibleLights[i];
        
        if (!light || light->viewCount != tr.viewCount) {
            continue;
        }
        
        // Regular lighting without legacy shadow volumes
        RB_SetupLightingShader(light);
        
        // Process all interactions for this light
        inter = light->firstInteraction;
        while (inter) {
            if (!inter->culled && inter->receivesLight && !inter->isEmpty) {
                RB_DrawInteraction(inter);
            }
            inter = inter->lightNext;
        }
    }
}

/*
================
RB_CalcLightGridColor

Calculate lighting from light grid for a position
================
*/
void RB_CalcLightGridColor(const vec3_t position, vec3_t color) {
    vec3_t ambient, directed, dummyDir;
    int i;

    R_ComputeSceneLighting(position, ambient, directed, dummyDir);

    // Convert from byte scale to 0..1 range expected by the backend
    if (tr.identityLightByte > 0) {
        float invByte = 1.0f / (float)tr.identityLightByte;
        VectorScale(ambient, invByte, ambient);
        VectorScale(directed, invByte, directed);
    }

    VectorCopy(ambient, color);
    VectorMA(color, 1.0f, directed, color);

    for (i = 0; i < 3; i++) {
        color[i] = Com_Clamp(0.0f, 1.0f, color[i]);
    }
}

/*
================
RB_SetupEntityLighting

Setup lighting for an entity
================
*/
void RB_SetupEntityLighting(trRefEntity_t *ent) {
    vec3_t lightColor;
    renderLight_t *nearest;
    
    // Calculate light grid color at entity position
    RB_CalcLightGridColor(ent->e.origin, lightColor);
    
    // Find nearest dynamic light
    nearest = R_GetNearestLight(ent->e.origin);
    if (nearest) {
        vec3_t toLight;
        float distance;
        float intensity;
        
        VectorSubtract(nearest->origin, ent->e.origin, toLight);
        distance = VectorLength(toLight);
        
        if (distance < nearest->radius) {
            intensity = 1.0f - (distance / nearest->radius);
            VectorMA(lightColor, intensity, nearest->color, lightColor);
        }
    }
    
    // Apply to entity
    VectorCopy(lightColor, ent->ambientLight);
    VectorCopy(lightColor, ent->directedLight);
}

/*
================
RB_LightingDebugVisualization

Debug visualization for lighting system
Note: Actual rendering depends on the renderer backend
================
*/
void RB_LightingDebugVisualization(void) {
    // Debug visualization will be implemented by the renderer backend
    // This function provides the interface for debugging
}
