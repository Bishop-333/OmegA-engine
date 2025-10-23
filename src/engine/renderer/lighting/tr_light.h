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

#ifndef TR_LIGHT_H
#define TR_LIGHT_H

// Forward declaration 
struct viewParms_s;

// Matrix types (used by light structures)
typedef vec_t mat3_t[9];   // 3x3 matrix
typedef vec_t mat4_t[16];  // 4x4 matrix

// Light types
typedef enum {
    RL_OMNI,        // Point light (sphere)
    RL_PROJ,        // Projected/spot light (frustum)
    RL_DIRECTIONAL, // Sun/directional light
    RL_AMBIENT,     // Ambient light volume
    RL_FOG          // Fog volume
} renderLightType_t;

// Light flags
#define LIGHTFLAG_NOSHADOWS         0x0001
#define LIGHTFLAG_NOSPECULAR        0x0002
#define LIGHTFLAG_NODIFFUSE         0x0004
#define LIGHTFLAG_NOSELFSHADOW      0x0008
#define LIGHTFLAG_FORCESHADOWS      0x0010
#define LIGHTFLAG_PARALLEL          0x0020
#define LIGHTFLAG_POINTLIGHT        0x0040
#define LIGHTFLAG_SPOTLIGHT         0x0080

// Forward declarations
struct interaction_s;
struct drawSurf_s;

// Main light structure
typedef struct renderLight_s {
    // Identification
    int                 index;              // Light index
    int                 flags;              // LIGHTFLAG_* flags
    renderLightType_t   type;               // Light type
    
    // Transform
    vec3_t              origin;             // World position
    mat3_t              axis;               // Orientation
    
    // Light volume (for culling)
    vec3_t              mins;               // AABB minimum
    vec3_t              maxs;               // AABB maximum
    float               radius;             // Bounding sphere radius
    
    // Light properties
    vec3_t              color;              // RGB color (normalized)
    float               intensity;          // Brightness multiplier
    
    // Attenuation
    float               constant;           // Constant attenuation
    float               linear;             // Linear attenuation
    float               quadratic;          // Quadratic attenuation
    float               cutoffDistance;     // Maximum influence distance
    
    // Projection (for spot/projected lights)
    float               fovX;               // Horizontal FOV
    float               fovY;               // Vertical FOV
    vec3_t              target;             // Look-at point
    float               nearClip;           // Near clip plane
    float               farClip;            // Far clip plane
    mat4_t              projectionMatrix;   // Light projection matrix
    mat4_t              viewMatrix;         // Light view matrix
    
    // Texture projection
    image_t             *projectionImage;   // Cookie/gobo texture
    image_t             *falloffImage;      // Falloff texture
    
    // Shadow properties
    float               shadowBias;         // Shadow depth bias
    float               shadowSoftness;     // Soft shadow radius
    
    // Culling info
    cplane_t            frustum[6];         // Light frustum for culling
    int                 areaNum;            // BSP area containing light
    byte                *areaPVS;           // Areas potentially affected
    
    // Frame data
    int                 visFrame;           // Last frame processed
    int                 shadowFrame;        // Last shadow update
    int                 viewCount;          // Last frame this light was visible
    struct renderLight_s *areaNext;        // Next light in same area
    
    // Interaction chains
    struct interaction_s *firstInteraction; // First surface affected by this light
    struct interaction_s *lastInteraction;  // Last surface (for fast append)
    int                 numInteractions;    // Total interaction count
    
    // Performance stats
    int                 numShadowCasters;   // Surfaces casting shadows
    int                 numLitSurfaces;     // Surfaces being lit
    
    // Dynamic properties
    qboolean            isStatic;           // Static light (can be cached)
    qboolean            needsUpdate;        // Interactions need rebuilding
    int                 lastUpdateFrame;    // Frame of last update
    
    // Screen-space optimization
    int                 scissorRect[4];     // Screen-space scissor bounds
    
} renderLight_t;

// Light management functions
renderLight_t* R_CreateLight(void);
void R_UpdateLight(renderLight_t *light);
void R_FreeLight(renderLight_t *light);
void R_ClearLightInteractions(renderLight_t *light);

// Light culling
void R_CullLights(struct viewParms_s *view);
qboolean R_LightInPVS(renderLight_t *light, struct viewParms_s *view);
qboolean R_SurfaceInLightVolume(struct drawSurf_s *surf, renderLight_t *light);

// Light system initialization
void R_InitLightingSystem(void);
void R_InitLightingCVars(void);
void R_ShutdownLightingSystem(void);

// Conversion from old system
void RE_AddDynamicLight(const vec3_t origin, float radius, float r, float g, float b);

// Debug visualization
void R_DrawLightVolumes(void);
void R_DrawInteractions(void);

// Light grid for spatial queries
typedef struct lightGrid_s {
    vec3_t              mins;
    vec3_t              maxs;
    vec3_t              cellSize;
    int                 gridSize[3];
    renderLight_t       ***cells;  // 3D grid of light lists
} lightGrid_t;

void R_BuildLightGrid(void);
void R_ClearLightGrid(void);
renderLight_t** R_GetLightsInCell(vec3_t point);
int R_GetNearbyLights(vec3_t point, renderLight_t **lightList, int maxLights);
void R_ComputeSceneLighting(const vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir);

#endif // TR_LIGHT_H
