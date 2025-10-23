/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

RTX Debug Overlay Rendering Integration
Hooks into the rendering pipeline to apply debug visualization
===========================================================================
*/

#include "rt_debug_overlay.h"
#include "rt_rtx.h"
#include "../core/tr_local.h"
#include "../vulkan/vk.h"

// External references
extern rtxDebugOverlay_t rtxDebugOverlay;
extern cvar_t *r_rtx_debug;

/*
================
RTX_UpdateDebugMode

Check rtx_debug cvar and update debug mode if changed
================
*/
void RTX_UpdateDebugMode(void) {
    if (!r_rtx_debug) {
        return;
    }
    
    int newMode = r_rtx_debug->integer;
    if (newMode != rtxDebugOverlay.mode) {
        RTX_SetDebugMode((rtxDebugMode_t)newMode);
    }
    
    // Update animation phase
    if (rtxDebugOverlay.animateColors) {
        rtxDebugOverlay.animationPhase += 0.05f;
    }
}

/*
================
RTX_ApplyDebugOverlayToSurface

Apply debug overlay coloring to a surface during rendering
================
*/
void RTX_ApplyDebugOverlayToSurface(drawSurf_t *drawSurf, shader_t *shader) {
    if (!rtxDebugOverlay.enabled || !drawSurf) {
        return;
    }
    
    // Don't try to analyze the surface structure - it's not an msurface_t
    // drawSurf->surface is a generic surface pointer (could be srfSurfaceFace_t, srfGridMesh_t, etc.)
    // We can only safely use the shader information
    
    if (!shader) {
        return;
    }
    
    // Ensure we have space for surface info
    if (rtxDebugOverlay.numSurfaces >= rtxDebugOverlay.maxSurfaces) {
        return;
    }
    
    // Create surface debug info from shader properties only
    surfaceDebugInfo_t localInfo;
    surfaceDebugInfo_t *info = &localInfo;
    
    // Initialize with default values
    Com_Memset(info, 0, sizeof(*info));
    
    // Analyze shader properties
    if (shader->surfaceFlags & SURF_SKY) {
        info->rtFlags |= SURF_RT_SKY;
    }
    if (shader->sort > SS_OPAQUE) {
        info->rtFlags |= SURF_RT_TRANSPARENT;
    }
    if (shader->surfaceFlags & SURF_NODLIGHT) {
        info->rtFlags &= ~SURF_RT_RECEIVES_GI;
    }
    
    // Set default material properties
    info->roughness = 0.8f;
    info->metallic = 0.0f;
    
    // Check if surface type indicates special handling
    if (drawSurf->surface) {
        surfaceType_t surfType = *(surfaceType_t*)drawSurf->surface;
        if (surfType == SF_FACE || surfType == SF_GRID) {
            info->rtFlags |= SURF_RT_IN_BLAS;
        }
    }
    
    // Get debug color based on mode
    vec4_t debugColor;
    RTX_GetDebugColor(info, debugColor);
    
    // Apply strong debug tint to make it visible
    // Use a more aggressive approach for visibility
    if (shader && shader->stages[0]) {
        shaderStage_t *stage = shader->stages[0];
        
        // Apply a strong tint based on debug mode
        // This ensures the overlay is visible
        byte r = (byte)(debugColor[0] * 255);
        byte g = (byte)(debugColor[1] * 255); 
        byte b = (byte)(debugColor[2] * 255);
        byte a = 200; // Strong alpha for visibility
        
        // Set constant color
        stage->bundle[0].constantColor.rgba[0] = r;
        stage->bundle[0].constantColor.rgba[1] = g;
        stage->bundle[0].constantColor.rgba[2] = b;
        stage->bundle[0].constantColor.rgba[3] = a;
        
        // Blend the debug color with existing rendering
        stage->bundle[0].rgbGen = CGEN_CONST;
        stage->bundle[0].alphaGen = AGEN_CONST;
        
        // Force the stage to be active with modified colors
        stage->active = qtrue;
    }
}

/*
================
RTX_RenderDebugOverlay

Main render function called each frame
This needs to dispatch a compute shader to apply the overlay
================
*/
void RTX_RenderDebugOverlay(void) {
    // Update debug mode from cvar
    RTX_UpdateDebugMode();
    
    if (!rtxDebugOverlay.enabled) {
        return;
    }
    
    // Reset per-frame stats
    RTX_ResetDebugOverlay();
    
    // NOTE: This function needs to be called BEFORE vk_end_frame()
    // Currently it's called too late in RC_END_OF_LIST
    // The overlay compute shader should be dispatched here
    
    static int lastLoggedMode = -1;
    if (rtxDebugOverlay.mode != lastLoggedMode) {
        lastLoggedMode = rtxDebugOverlay.mode;
        ri.Printf(PRINT_DEVELOPER, "RTX Debug Overlay Mode: %s\n",
                  RTX_GetDebugModeName(rtxDebugOverlay.mode));
    }
    
    // Draw legend if enabled (currently prints to console)
    RTX_DrawDebugLegend();
    
    // Update ray density map if in that mode
    if (rtxDebugOverlay.mode == RTX_DEBUG_RAY_DENSITY) {
        RTX_UpdateRayDensityMap();
    }
}

/*
================
RTX_CreateDebugVisualizationPipeline

Create a Vulkan pipeline for debug overlay rendering
================
*/
qboolean RTX_CreateDebugVisualizationPipeline(void) {
    // This would create a specialized Vulkan pipeline for debug overlay
    // For now, we modify existing surface colors instead
    return qtrue;
}

/*
================
RTX_BeginFrameDebugOverlay

Called at the beginning of each frame
================
*/
void RTX_BeginFrameDebugOverlay(void) {
    // Check if debug mode changed
    RTX_UpdateDebugMode();
    
    if (rtxDebugOverlay.enabled) {
        // Clear per-frame data
        RTX_ResetDebugOverlay();
        
        // Increment frame counter for temporal effects
        rtxDebugOverlay.frameAccumCount++;
    }
}

/*
================
RTX_EndFrameDebugOverlay

Called at the end of each frame
================
*/
void RTX_EndFrameDebugOverlay(void) {
    if (rtxDebugOverlay.enabled) {
        // Normalize ray density map
        if (rtxDebugOverlay.mode == RTX_DEBUG_RAY_DENSITY) {
            float maxDensity = 0.0f;
            int mapSize = rtxDebugOverlay.densityMapWidth * rtxDebugOverlay.densityMapHeight;
            
            // Find max density
            for (int i = 0; i < mapSize; i++) {
                if (rtxDebugOverlay.rayDensityMap[i] > maxDensity) {
                    maxDensity = rtxDebugOverlay.rayDensityMap[i];
                }
            }
            
            // Normalize
            if (maxDensity > 0.0f) {
                float invMax = 1.0f / maxDensity;
                for (int i = 0; i < mapSize; i++) {
                    rtxDebugOverlay.rayDensityMap[i] *= invMax;
                }
            }
        }
    }
}

/*
================
RTX_DebugTraceRay

Record ray trace for debug visualization
================
*/
void RTX_DebugTraceRay(const vec3_t origin, const vec3_t direction, 
                       const vec3_t hitPoint, qboolean hit) {
    if (!rtxDebugOverlay.enabled) {
        return;
    }
    
    if (rtxDebugOverlay.mode == RTX_DEBUG_RAY_DENSITY && hit) {
        RTX_RecordRayHit(hitPoint, direction);
    }
}

/*
================
RTX_GetSurfaceDebugInfo

Get debug info for a specific surface
================
*/
surfaceDebugInfo_t* RTX_GetSurfaceDebugInfo(const msurface_t *surf) {
    if (!surf || !rtxDebugOverlay.enabled) {
        return NULL;
    }
    
    uint32_t surfaceId = (uint32_t)(uintptr_t)surf;
    surfaceId = surfaceId % rtxDebugOverlay.maxSurfaces;
    
    surfaceDebugInfo_t *info = &rtxDebugOverlay.surfaceInfo[surfaceId];
    
    // Analyze if needed
    if (info->instanceId == 0) {
        RTX_AnalyzeSurface(surf, info);
        info->instanceId = surfaceId;
    }
    
    return info;
}
