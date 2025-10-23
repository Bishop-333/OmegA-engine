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

#ifndef TR_LIGHT_DYNAMIC_H
#define TR_LIGHT_DYNAMIC_H

// Include the base light definitions
#include "tr_light.h"
#include "tr_interaction.h"

/*
================================================================================
Phase 5: Dynamic Lighting Data Structures

This file defines the enhanced dynamic lighting system that supports per-pixel
lighting, shadows, and multiple light types.
================================================================================
*/

// Light types and flags are defined in tr_light.h
// Additional dynamic light specific definitions follow
#define LIGHTFLAG_FORCESHADOWS      0x0010
#define LIGHTFLAG_PARALLEL          0x0020
#define LIGHTFLAG_POINTLIGHT        0x0040
#define LIGHTFLAG_SPOTLIGHT         0x0080
#define LIGHTFLAG_VOLUMETRIC        0x0100
#define LIGHTFLAG_STATIC            0x0200

// Forward declarations
typedef struct interaction_s interaction_t;
// renderLight_t is defined in tr_light.h

// interaction_s and interactionManager_t are defined in tr_interaction.h

// Light management constants
#define MAX_RENDER_LIGHTS   4096
#define MAX_INTERACTIONS    30000

// Light system management
typedef struct lightSystem_s {
    // Light storage
    renderLight_t       lights[MAX_RENDER_LIGHTS];
    int                 numLights;
    
    // Light lists
    renderLight_t       *activeLights[MAX_RENDER_LIGHTS];
    int                 numActiveLights;
    renderLight_t       *visibleLights[MAX_RENDER_LIGHTS];
    int                 numVisibleLights;
    
    // Area-based light lists (for PVS culling)
    renderLight_t       *areaLights[MAX_MAP_AREAS];
    
    // Interaction management
    interactionManager_t interactionMgr;
    
    // Frame counters
    int                 frameCount;
    int                 visCount;
    
    // Performance stats
    int                 totalLightTests;
    int                 totalLightCulled;
    int                 totalInteractionTests;
    int                 totalInteractionCulled;
    int                 interactionTime;      // Total time spent on interactions
    
} lightSystem_t;

// Global light system
extern lightSystem_t tr_lightSystem;

// Light creation and management
renderLight_t* R_AllocRenderLight(void);
void R_FreeRenderLight(renderLight_t *light);
void R_InitRenderLight(renderLight_t *light);
void R_UpdateRenderLight(renderLight_t *light);

// Light-surface interaction
interaction_t* R_CreateInteraction(renderLight_t *light, drawSurf_t *surf);
void R_FreeInteraction(interaction_t *inter);
void R_UpdateInteraction(interaction_t *inter);
void R_LinkInteraction(interaction_t *inter);
void R_UnlinkInteraction(interaction_t *inter);

// Light culling
void R_CullAllLights(void);
qboolean R_CullLightBounds(renderLight_t *light);
void R_AddLightToArea(renderLight_t *light, int areaNum);
void R_RemoveLightFromArea(renderLight_t *light);

// Light queries
renderLight_t* R_GetNearestLight(const vec3_t point);
int R_GatherLights(const vec3_t mins, const vec3_t maxs, renderLight_t **list, int maxLights);
qboolean R_LightAffectsSurface(renderLight_t *light, drawSurf_t *surf);

// System management
void R_InitLightSystem(void);
void R_ShutdownLightSystem(void);
void R_ClearLights(void);
void R_UpdateLightSystem(void);

// Debug visualization
void R_DrawLightVolumes(void);
void R_DrawLightInteractions(void);
void R_DrawShadowFrusta(void);

// Legacy compatibility

// Light properties
void R_SetLightColor(renderLight_t *light, float r, float g, float b);
void R_SetLightIntensity(renderLight_t *light, float intensity);
void R_SetLightAttenuation(renderLight_t *light, float constant, float linear, float quadratic);
void R_SetSpotLightAngles(renderLight_t *light, float fovX, float fovY);
void R_SetLightProjectionTexture(renderLight_t *light, const char *name);

// Performance monitoring
int R_GetActiveLightCount(void);
int R_GetVisibleLightCount(void);
int R_GetInteractionCount(void);

// Backend rendering functions (Phase 6)
void RB_SetupLightingShader(renderLight_t *light);
void RB_DrawInteraction(interaction_t *inter);
void RB_RenderBasePass(void);
void RB_RenderLightingPasses(void);
void RB_CalcLightGridColor(const vec3_t position, vec3_t color);
void RB_SetupEntityLighting(trRefEntity_t *ent);
void RB_LightingDebugVisualization(void);

#endif // TR_LIGHT_DYNAMIC_H
