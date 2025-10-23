/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

RTX Debug Visualization Overlay
Provides visual debugging for RT surface participation and lighting
===========================================================================
*/

#ifndef RT_DEBUG_OVERLAY_H
#define RT_DEBUG_OVERLAY_H

#include "../core/tr_local.h"
#include "rt_rtx.h"

// Debug visualization modes
typedef enum {
    RTX_DEBUG_OFF = 0,
    RTX_DEBUG_RT_PARTICIPATION = 1,    // Show which surfaces are in TLAS/BLAS
    RTX_DEBUG_MATERIAL_PROPS = 2,      // Visualize PBR material properties
    RTX_DEBUG_LIGHTING_CONTRIB = 3,    // Show lighting contribution types
    RTX_DEBUG_RAY_DENSITY = 4,         // Heatmap of ray intersections
    RTX_DEBUG_SURFACE_NORMALS = 5,     // World-space normal visualization
    RTX_DEBUG_INSTANCE_ID = 6,         // Unique color per instance
    RTX_DEBUG_RANDOM_NOISE = 7,        // Randomized color per pixel for verification
    RTX_DEBUG_MODE_COUNT
} rtxDebugMode_t;

// Surface participation flags for analysis
typedef enum {
    SURF_RT_NONE           = 0,
    SURF_RT_IN_BLAS        = (1 << 0),  // Surface is in BLAS
    SURF_RT_IN_TLAS        = (1 << 1),  // Instance is in TLAS
    SURF_RT_DYNAMIC        = (1 << 2),  // Dynamic/updated per frame
    SURF_RT_EMISSIVE       = (1 << 3),  // Emissive material
    SURF_RT_TRANSPARENT    = (1 << 4),  // Transparent/alpha tested
    SURF_RT_VERTEX_LIT     = (1 << 5),  // Vertex lighting only
    SURF_RT_SKY            = (1 << 6),  // Sky surface
    SURF_RT_EXCLUDED       = (1 << 7),  // Explicitly excluded from RT
    SURF_RT_LOD            = (1 << 8),  // Using LOD/simplified geometry
    SURF_RT_RECEIVES_GI    = (1 << 9),  // Receives global illumination
    SURF_RT_CASTS_SHADOWS  = (1 << 10), // Casts shadows
    SURF_RT_REFLECTIVE     = (1 << 11), // Reflective surface
} surfaceRTFlags_t;

// Debug overlay data per surface
typedef struct {
    surfaceRTFlags_t    rtFlags;
    float               rayHitDensity;      // Normalized 0-1
    float               giContribution;     // Amount of GI vs direct
    float               roughness;
    float               metallic;
    float               emissiveIntensity;
    vec3_t              avgNormal;
    uint32_t            instanceId;
    uint32_t            materialId;
} surfaceDebugInfo_t;

// Debug overlay state
typedef struct {
    qboolean            enabled;
    rtxDebugMode_t      mode;
    
    // Surface analysis cache
    surfaceDebugInfo_t  *surfaceInfo;
    uint32_t            numSurfaces;
    uint32_t            maxSurfaces;
    
    // Ray density accumulation
    float               *rayDensityMap;
    uint32_t            densityMapWidth;
    uint32_t            densityMapHeight;
    uint32_t            frameAccumCount;
    
    // Visualization settings
    float               overlayAlpha;
    qboolean            showLegend;
    qboolean            animateColors;
    float               animationPhase;
    
    // Performance stats
    uint32_t            surfacesInBLAS;
    uint32_t            instancesInTLAS;
    uint32_t            dynamicSurfaces;
    uint32_t            excludedSurfaces;
    uint32_t            totalRayHits;      // Total ray hits recorded this frame
} rtxDebugOverlay_t;

// Global debug overlay state
extern rtxDebugOverlay_t rtxDebugOverlay;

// Initialization
void RTX_InitDebugOverlay(void);
void RTX_ShutdownDebugOverlay(void);
void RTX_ResetDebugOverlay(void);

// Surface analysis
void RTX_AnalyzeSurface(const msurface_t *surf, surfaceDebugInfo_t *info);
void RTX_AnalyzeEntity(const trRefEntity_t *ent, surfaceDebugInfo_t *info);
void RTX_UpdateSurfaceRTFlags(uint32_t surfaceId, surfaceRTFlags_t flags);

// Ray density tracking
void RTX_RecordRayHit(const vec3_t hitPoint, const vec3_t normal);
void RTX_RecordRayHitWithCamera(const vec3_t hitPoint, const vec3_t normal,
                                 const vec3_t cameraPos, const vec3_t cameraForward,
                                 const vec3_t cameraRight, const vec3_t cameraUp,
                                 float fovX, float fovY);
void RTX_UpdateRayDensityMap(void);
void RTX_ClearRayDensityMap(void);

// Color mapping functions
void RTX_GetDebugColor(const surfaceDebugInfo_t *info, vec4_t outColor);
void RTX_GetRTParticipationColor(surfaceRTFlags_t flags, vec4_t outColor);
void RTX_GetMaterialPropsColor(float roughness, float metallic, float emissive, vec4_t outColor);
void RTX_GetLightingContribColor(float direct, float indirect, float ambient, vec4_t outColor);
void RTX_GetRayDensityColor(float density, vec4_t outColor);
void RTX_GetNormalColor(const vec3_t normal, vec4_t outColor);
void RTX_GetInstanceColor(uint32_t instanceId, vec4_t outColor);

// Rendering
void RTX_BeginFrameDebugOverlay(void);
void RTX_EndFrameDebugOverlay(void);
void RTX_RenderDebugOverlay(void);
void RTX_DrawDebugLegend(void);
void RTX_ApplyDebugOverlayToSurface(drawSurf_t *drawSurf, shader_t *shader);

// Mode control
void RTX_SetDebugMode(rtxDebugMode_t mode);
void RTX_CycleDebugMode(void);
const char* RTX_GetDebugModeName(rtxDebugMode_t mode);

// Console commands
void RTX_DebugOverlay_f(void);
void RTX_DebugDumpSurfaces_f(void);

// Update stats
void RTX_UpdateDebugStats(int surfacesInBLAS, int instancesInTLAS);

#endif // RT_DEBUG_OVERLAY_H
