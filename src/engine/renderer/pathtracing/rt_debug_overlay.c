/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

RTX Debug Visualization Overlay Implementation
Provides visual debugging for RT surface participation and lighting
===========================================================================
*/

#include "rt_debug_overlay.h"
#include "rt_rtx.h"
#include "rt_pathtracer.h"
#include "../core/tr_local.h"
#include "../vulkan/vk.h"

// Helper macros
#ifndef CLAMP
#define CLAMP(x, min, max) ((x) < (min) ? (min) : (x) > (max) ? (max) : (x))
#endif

// Global debug overlay state
rtxDebugOverlay_t rtxDebugOverlay;

// External references
extern rtxState_t rtx;
extern cvar_t *r_rtx_debug;

// Color palette for debug visualization
static const vec4_t debugColors[] = {
    // RT Participation colors
    { 0.0f, 1.0f, 0.0f, 0.8f },    // Bright Green - Full RT
    { 0.0f, 0.5f, 0.0f, 0.8f },    // Dark Green - RT with LOD
    { 1.0f, 1.0f, 0.0f, 0.8f },    // Yellow - Dynamic
    { 1.0f, 0.5f, 0.0f, 0.8f },    // Orange - Emissive
    { 1.0f, 0.0f, 0.0f, 0.8f },    // Red - Excluded
    { 0.0f, 0.3f, 1.0f, 0.8f },    // Blue - Static GI / probe lit
    { 0.5f, 0.0f, 1.0f, 0.8f },    // Purple - Reserved hybrid
    { 0.5f, 0.5f, 0.5f, 0.8f },    // Gray - No lighting
    { 0.0f, 1.0f, 1.0f, 0.8f },    // Cyan - Reflective
    { 1.0f, 0.0f, 1.0f, 0.8f },    // Magenta - Transparent
};

/*
================
RTX_InitDebugOverlay

Initialize debug overlay system
================
*/
void RTX_InitDebugOverlay(void) {
    Com_Memset(&rtxDebugOverlay, 0, sizeof(rtxDebugOverlay));
    
    // Allocate surface info array
    rtxDebugOverlay.maxSurfaces = 65536;
    rtxDebugOverlay.surfaceInfo = ri.Hunk_Alloc(
        sizeof(surfaceDebugInfo_t) * rtxDebugOverlay.maxSurfaces, h_low);
    
    // Allocate ray density map (256x256 for now)
    rtxDebugOverlay.densityMapWidth = 256;
    rtxDebugOverlay.densityMapHeight = 256;
    rtxDebugOverlay.rayDensityMap = ri.Hunk_Alloc(
        sizeof(float) * rtxDebugOverlay.densityMapWidth * rtxDebugOverlay.densityMapHeight, h_low);
    
    // Set default values
    rtxDebugOverlay.overlayAlpha = 0.8f;
    rtxDebugOverlay.showLegend = qtrue;
    rtxDebugOverlay.animateColors = qfalse;
    
    // Register console commands
    ri.Cmd_AddCommand("rtx_debug_overlay", RTX_DebugOverlay_f);
    ri.Cmd_AddCommand("rtx_debug_dump", RTX_DebugDumpSurfaces_f);
    
    ri.Printf(PRINT_ALL, "RTX Debug Overlay initialized\n");
}

/*
================
RTX_ShutdownDebugOverlay

Cleanup debug overlay resources
================
*/
void RTX_ShutdownDebugOverlay(void) {
    Com_Memset(&rtxDebugOverlay, 0, sizeof(rtxDebugOverlay));
}

/*
================
RTX_ResetDebugOverlay

Reset debug overlay for new frame/level
================
*/
void RTX_ResetDebugOverlay(void) {
    // Only reset per-frame data, not persistent world stats
    rtxDebugOverlay.numSurfaces = 0;
    // DON'T reset surfacesInBLAS - this is persistent world data
    // DON'T reset instancesInTLAS - this is persistent world data
    rtxDebugOverlay.dynamicSurfaces = 0;
    rtxDebugOverlay.excludedSurfaces = 0;
    rtxDebugOverlay.frameAccumCount = 0;
    rtxDebugOverlay.totalRayHits = 0;

    RTX_ClearRayDensityMap();
}

/*
================
RTX_AnalyzeSurface

Analyze a surface and determine its RT participation
================
*/
void RTX_AnalyzeSurface(const msurface_t *surf, surfaceDebugInfo_t *info) {
    // This function should not be called anymore
    // The surf parameter is not actually an msurface_t* in the draw pipeline
    // It's a generic surface pointer that could be various types
    // We keep this stub for compatibility
    
    if (!info) {
        return;
    }
    
    // Just set safe defaults
    Com_Memset(info, 0, sizeof(*info));
    info->roughness = 0.8f;
    info->metallic = 0.0f;
    info->emissiveIntensity = 0.0f;
    VectorSet(info->avgNormal, 0, 0, 1);
    info->rtFlags = SURF_RT_IN_BLAS;
}

/*
================
RTX_GetRTParticipationColor

Get color based on RT participation flags
================
*/
void RTX_GetRTParticipationColor(surfaceRTFlags_t flags, vec4_t outColor) {
    // Priority-based color selection
    if (flags & SURF_RT_SKY) {
        VectorSet(outColor, 0.7f, 0.9f, 1.0f);  // Light blue for sky
    } else if (flags & SURF_RT_EXCLUDED) {
        VectorSet(outColor, 1.0f, 0.0f, 0.0f);  // Red for excluded
    } else if (flags & SURF_RT_EMISSIVE) {
        VectorSet(outColor, 1.0f, 0.5f, 0.0f);  // Orange for emissive
    } else if (flags & SURF_RT_TRANSPARENT) {
        VectorSet(outColor, 1.0f, 0.0f, 1.0f);  // Magenta for transparent
    } else if (flags & SURF_RT_DYNAMIC) {
        VectorSet(outColor, 1.0f, 1.0f, 0.0f);  // Yellow for dynamic
    } else if (flags & SURF_RT_REFLECTIVE) {
        VectorSet(outColor, 0.0f, 1.0f, 1.0f);  // Cyan for reflective
    } else if (flags & SURF_RT_IN_TLAS) {
        if (flags & SURF_RT_LOD) {
            VectorSet(outColor, 0.0f, 0.5f, 0.0f);  // Dark green for LOD
        } else {
            VectorSet(outColor, 0.0f, 1.0f, 0.0f);  // Bright green for full RT
        }
    } else if (flags & SURF_RT_VERTEX_LIT) {
        VectorSet(outColor, 0.0f, 0.5f, 1.0f);  // Light blue for vertex lit
    } else {
        VectorSet(outColor, 0.5f, 0.5f, 0.5f);  // Gray for no lighting
    }
    
    outColor[3] = rtxDebugOverlay.overlayAlpha;
    
    // Animate colors if enabled
    if (rtxDebugOverlay.animateColors) {
        float pulse = 0.5f + 0.5f * sin(rtxDebugOverlay.animationPhase);
        VectorScale(outColor, 0.7f + 0.3f * pulse, outColor);
    }
}

/*
================
RTX_GetMaterialPropsColor

Visualize material properties as RGB
================
*/
void RTX_GetMaterialPropsColor(float roughness, float metallic, float emissive, vec4_t outColor) {
    outColor[0] = roughness;        // Red = roughness
    outColor[1] = metallic;          // Green = metallic
    outColor[2] = emissive;          // Blue = emissive
    outColor[3] = rtxDebugOverlay.overlayAlpha;
}

/*
================
RTX_GetLightingContribColor

Visualize lighting contributions
================
*/
void RTX_GetLightingContribColor(float direct, float indirect, float ambient, vec4_t outColor) {
    // Normalize contributions
    float total = direct + indirect + ambient;
    if (total > 0.0f) {
        direct /= total;
        indirect /= total;
        ambient /= total;
    }

    // Map to colors
    outColor[0] = direct;            // Red = direct lighting
    outColor[1] = indirect;          // Green = indirect/GI
    outColor[2] = ambient;           // Blue = probe/ambient contribution
    outColor[3] = rtxDebugOverlay.overlayAlpha;
}

/*
================
RTX_GetRayDensityColor

Get heatmap color for ray density
================
*/
void RTX_GetRayDensityColor(float density, vec4_t outColor) {
    // Heatmap gradient: Blue -> Green -> Yellow -> Red
    density = CLAMP(density, 0.0f, 1.0f);
    
    if (density < 0.25f) {
        // Blue to Green
        float t = density * 4.0f;
        outColor[0] = 0.0f;
        outColor[1] = t;
        outColor[2] = 1.0f - t;
    } else if (density < 0.5f) {
        // Green to Yellow
        float t = (density - 0.25f) * 4.0f;
        outColor[0] = t;
        outColor[1] = 1.0f;
        outColor[2] = 0.0f;
    } else if (density < 0.75f) {
        // Yellow to Orange
        float t = (density - 0.5f) * 4.0f;
        outColor[0] = 1.0f;
        outColor[1] = 1.0f - t * 0.5f;
        outColor[2] = 0.0f;
    } else {
        // Orange to Red
        float t = (density - 0.75f) * 4.0f;
        outColor[0] = 1.0f;
        outColor[1] = 0.5f - t * 0.5f;
        outColor[2] = 0.0f;
    }
    
    outColor[3] = rtxDebugOverlay.overlayAlpha;
}

/*
================
RTX_GetNormalColor

Visualize surface normals as RGB
================
*/
void RTX_GetNormalColor(const vec3_t normal, vec4_t outColor) {
    // Map normalized world space normals to RGB
    outColor[0] = normal[0] * 0.5f + 0.5f;
    outColor[1] = normal[1] * 0.5f + 0.5f;
    outColor[2] = normal[2] * 0.5f + 0.5f;
    outColor[3] = rtxDebugOverlay.overlayAlpha;
}

/*
================
RTX_GetInstanceColor

Generate unique color per instance ID
================
*/
void RTX_GetInstanceColor(uint32_t instanceId, vec4_t outColor) {
    // Generate pseudo-random color from instance ID
    uint32_t hash = instanceId * 2654435761u;
    
    outColor[0] = ((hash >> 0) & 0xFF) / 255.0f;
    outColor[1] = ((hash >> 8) & 0xFF) / 255.0f;
    outColor[2] = ((hash >> 16) & 0xFF) / 255.0f;
    outColor[3] = rtxDebugOverlay.overlayAlpha;
}

/*
================
RTX_GetDebugColor

Main function to get debug color based on current mode
================
*/
void RTX_GetDebugColor(const surfaceDebugInfo_t *info, vec4_t outColor) {
    if (!info) {
        outColor[0] = 1; outColor[1] = 0; outColor[2] = 1; outColor[3] = 1; // Magenta for error
        return;
    }
    
    switch (rtxDebugOverlay.mode) {
        case RTX_DEBUG_RT_PARTICIPATION:
            RTX_GetRTParticipationColor(info->rtFlags, outColor);
            break;
            
        case RTX_DEBUG_MATERIAL_PROPS:
            RTX_GetMaterialPropsColor(info->roughness, info->metallic, 
                                     info->emissiveIntensity, outColor);
            break;
            
        case RTX_DEBUG_LIGHTING_CONTRIB:
            // TEMPORARY: Use test values to verify visualization works
            // TODO: These should come from ray tracing shaders
            {
                // Force test values to debug the visualization
                float testDirect = 0.33f;    // 33% direct (red)
                float testIndirect = 0.33f;  // 33% indirect/GI (green)
                float testAmbient = 0.34f;   // 34% ambient/probe (blue)

                RTX_GetLightingContribColor(testDirect, testIndirect, testAmbient, outColor);

                // Force override the colors directly for testing
                outColor[0] = 0.33f;  // Red
                outColor[1] = 0.33f;  // Green
                outColor[2] = 0.34f;  // Blue
                outColor[3] = 1.0f;   // Alpha
            }
            break;
            
        case RTX_DEBUG_RAY_DENSITY:
            RTX_GetRayDensityColor(info->rayHitDensity, outColor);
            break;
            
        case RTX_DEBUG_SURFACE_NORMALS:
            RTX_GetNormalColor(info->avgNormal, outColor);
            break;
            
        case RTX_DEBUG_INSTANCE_ID:
            RTX_GetInstanceColor(info->instanceId, outColor);
            break;

        case RTX_DEBUG_RANDOM_NOISE:
        {
            uint32_t seed = (uint32_t)(uintptr_t)info ^ (uint32_t)rtxDebugOverlay.frameAccumCount * 1664525u;
            seed ^= seed >> 13;
            seed *= 1274126177u;
            outColor[0] = ((seed >>  0) & 0xFF) / 255.0f;
            outColor[1] = ((seed >>  8) & 0xFF) / 255.0f;
            outColor[2] = ((seed >> 16) & 0xFF) / 255.0f;
            outColor[3] = rtxDebugOverlay.overlayAlpha;
            break;
        }

        default:
            outColor[0] = 1; outColor[1] = 1; outColor[2] = 1; outColor[3] = 0; // Transparent white
            break;
    }
}

/*
================
RTX_SetDebugMode

Set debug visualization mode
================
*/
void RTX_SetDebugMode(rtxDebugMode_t mode) {
    if (mode >= 0 && mode < RTX_DEBUG_MODE_COUNT) {
        rtxDebugOverlay.mode = mode;
        rtxDebugOverlay.enabled = (mode != RTX_DEBUG_OFF);
        
        // Update the rtx_debug cvar to match
        if (r_rtx_debug) {
            ri.Cvar_SetValue("r_rtx_debug", (float)mode);
        }
        
        ri.Printf(PRINT_ALL, "RTX Debug Mode: %s\n", RTX_GetDebugModeName(mode));
    }
}

/*
================
RTX_CycleDebugMode

Cycle through debug modes
================
*/
void RTX_CycleDebugMode(void) {
    rtxDebugMode_t nextMode = (rtxDebugOverlay.mode + 1) % RTX_DEBUG_MODE_COUNT;
    RTX_SetDebugMode(nextMode);
}

/*
================
RTX_GetDebugModeName

Get human-readable name for debug mode
================
*/
const char* RTX_GetDebugModeName(rtxDebugMode_t mode) {
    switch (mode) {
        case RTX_DEBUG_OFF:               return "Off";
        case RTX_DEBUG_RT_PARTICIPATION:  return "RT Participation";
        case RTX_DEBUG_MATERIAL_PROPS:    return "Material Properties";
        case RTX_DEBUG_LIGHTING_CONTRIB:  return "Lighting Contributions";
        case RTX_DEBUG_RAY_DENSITY:       return "Ray Density Heatmap";
        case RTX_DEBUG_SURFACE_NORMALS:   return "Surface Normals";
        case RTX_DEBUG_INSTANCE_ID:       return "Instance IDs";
        case RTX_DEBUG_RANDOM_NOISE:      return "Random Verification";
        default:                          return "Unknown";
    }
}

/*
================
RTX_DrawDebugLegend

Draw color legend for current debug mode
================
*/
void RTX_DrawDebugLegend(void) {
    if (!rtxDebugOverlay.showLegend || !rtxDebugOverlay.enabled) {
        return;
    }
    
    int x = 10;
    int y = 10;
    int boxSize = 16;
    int spacing = 20;
    
    // Since we don't have direct drawing functions in refimport_t,
    // we'll need to implement this differently or skip the legend for now
    // TODO: Use console print or find alternative drawing method
    
    char title[128];
    Com_sprintf(title, sizeof(title), "RTX Debug Mode: %s", 
               RTX_GetDebugModeName(rtxDebugOverlay.mode));
    ri.Printf(PRINT_ALL, "%s\n", title);
    
    // Draw legend items based on mode
    switch (rtxDebugOverlay.mode) {
        case RTX_DEBUG_RT_PARTICIPATION:
            {
                struct {
                    const char *label;
                    vec4_t color;
                } legend[] = {
                    { "Full RT", { 0.0f, 1.0f, 0.0f, 1.0f } },
                    { "RT + LOD", { 0.0f, 0.5f, 0.0f, 1.0f } },
                    { "Dynamic", { 1.0f, 1.0f, 0.0f, 1.0f } },
                    { "Emissive", { 1.0f, 0.5f, 0.0f, 1.0f } },
                    { "Excluded", { 1.0f, 0.0f, 0.0f, 1.0f } },
                    { "Probe Lit (static GI)", { 0.0f, 0.3f, 1.0f, 1.0f } },
                    { "Reflective", { 0.0f, 1.0f, 1.0f, 1.0f } },
                    { "No Lighting", { 0.5f, 0.5f, 0.5f, 1.0f } }
                };
                
                for (int i = 0; i < ARRAY_LEN(legend); i++) {
                    // Print to console instead of drawing
                    ri.Printf(PRINT_ALL, "  %s\n", legend[i].label);
                }
            }
            break;
            
        case RTX_DEBUG_MATERIAL_PROPS:
            ri.Printf(PRINT_ALL, "  Red: Roughness\n");
            ri.Printf(PRINT_ALL, "  Green: Metallic\n");
            ri.Printf(PRINT_ALL, "  Blue: Emissive\n");
            break;
            
        case RTX_DEBUG_LIGHTING_CONTRIB:
            ri.Printf(PRINT_ALL, "  Red: Direct Light\n");
            ri.Printf(PRINT_ALL, "  Green: Indirect/GI Bounce\n");
            ri.Printf(PRINT_ALL, "  Blue: Ambient/Probe Contribution\n");
            break;
            
        case RTX_DEBUG_RAY_DENSITY:
            ri.Printf(PRINT_ALL, "  Blue: Low Density\n");
            ri.Printf(PRINT_ALL, "  Green: Medium\n");
            ri.Printf(PRINT_ALL, "  Yellow: High\n");
            ri.Printf(PRINT_ALL, "  Red: Very High\n");
            break;

        case RTX_DEBUG_RANDOM_NOISE:
            ri.Printf(PRINT_ALL, "  Random per-surface coloration for RTX verification\n");
            break;
    }
    
    // Print statistics to console - query actual RTX state
    ri.Printf(PRINT_ALL, "RTX_DrawDebugOverlay: surfacesInBLAS=%d\n", rtxDebugOverlay.surfacesInBLAS);
    char stats[256];
    Com_sprintf(stats, sizeof(stats),
               "RTX Stats - BLAS: %d | TLAS: %d | Surfaces: %d | Dynamic: %d",
               rtx.numBLAS,                        // Actual BLAS count
               rtx.tlas.numInstances,              // Actual TLAS instance count
               rtxDebugOverlay.surfacesInBLAS,    // Surfaces processed
               rtxDebugOverlay.dynamicSurfaces);
    ri.Printf(PRINT_ALL, "%s\n", stats);
}

/*
================
RTX_UpdateDebugStats

Update debug overlay statistics
================
*/
void RTX_UpdateDebugStats(int surfacesInBLAS, int instancesInTLAS) {
    ri.Printf(PRINT_ALL, "RTX_UpdateDebugStats: surfaces=%d, instances=%d\n", surfacesInBLAS, instancesInTLAS);
    rtxDebugOverlay.surfacesInBLAS = surfacesInBLAS;
    rtxDebugOverlay.instancesInTLAS = instancesInTLAS;
}

/*
================
RTX_DebugOverlay_f

Console command for debug overlay control
================
*/
void RTX_DebugOverlay_f(void) {
    int argc = ri.Cmd_Argc();
    
    if (argc < 2) {
        ri.Printf(PRINT_ALL, "Usage: rtx_debug_overlay <mode|cycle|legend|alpha>\n");
        ri.Printf(PRINT_ALL, "Modes: 0=off, 1=rt_participation, 2=materials, 3=lighting\n");
        ri.Printf(PRINT_ALL, "       4=ray_density, 5=normals, 6=instances, 7=random\n");
        ri.Printf(PRINT_ALL, "Current mode: %s\n", RTX_GetDebugModeName(rtxDebugOverlay.mode));
        return;
    }
    
    const char *cmd = ri.Cmd_Argv(1);
    
    if (!Q_stricmp(cmd, "cycle")) {
        RTX_CycleDebugMode();
    } else if (!Q_stricmp(cmd, "legend")) {
        rtxDebugOverlay.showLegend = !rtxDebugOverlay.showLegend;
        ri.Printf(PRINT_ALL, "Legend %s\n", rtxDebugOverlay.showLegend ? "enabled" : "disabled");
    } else if (!Q_stricmp(cmd, "alpha")) {
        if (argc > 2) {
            rtxDebugOverlay.overlayAlpha = atof(ri.Cmd_Argv(2));
            rtxDebugOverlay.overlayAlpha = CLAMP(rtxDebugOverlay.overlayAlpha, 0.0f, 1.0f);
            ri.Printf(PRINT_ALL, "Overlay alpha: %.2f\n", rtxDebugOverlay.overlayAlpha);
        }
    } else {
        int mode = atoi(cmd);
        RTX_SetDebugMode((rtxDebugMode_t)mode);
    }
}

/*
================
RTX_DebugDumpSurfaces_f

Dump surface analysis to console
================
*/
void RTX_DebugDumpSurfaces_f(void) {
    ri.Printf(PRINT_ALL, "=== RTX Surface Analysis ===\n");
    ri.Printf(PRINT_ALL, "Total Surfaces: %d\n", rtxDebugOverlay.numSurfaces);
    ri.Printf(PRINT_ALL, "In BLAS: %d\n", rtxDebugOverlay.surfacesInBLAS);
    ri.Printf(PRINT_ALL, "In TLAS: %d\n", rtxDebugOverlay.instancesInTLAS);
    ri.Printf(PRINT_ALL, "Dynamic: %d\n", rtxDebugOverlay.dynamicSurfaces);
    ri.Printf(PRINT_ALL, "Excluded: %d\n", rtxDebugOverlay.excludedSurfaces);
    
    // Dump first 10 surfaces for debugging
    int count = MIN(10, rtxDebugOverlay.numSurfaces);
    for (int i = 0; i < count; i++) {
        surfaceDebugInfo_t *info = &rtxDebugOverlay.surfaceInfo[i];
        ri.Printf(PRINT_ALL, "Surface %d: flags=0x%08X rough=%.2f metal=%.2f emit=%.2f\n",
                 i, info->rtFlags, info->roughness, info->metallic, info->emissiveIntensity);
    }
}

/*
================
RTX_RecordRayHit

Record a ray hit for density visualization
================
*/
void RTX_RecordRayHit(const vec3_t hitPoint, const vec3_t normal) {
    if (!rtxDebugOverlay.rayDensityMap) {
        return;
    }

    // For now, use a simplified approach that doesn't require tr_refdef
    // We'll use a basic projection based on camera position that should be
    // passed in from the ray tracing system

    // Simple hash-based mapping to density grid for visualization
    // This is a placeholder that provides some visual feedback without
    // requiring access to the full view transformation

    // Hash the world position to get a pseudo-random but consistent mapping
    unsigned int hashX = (unsigned int)(hitPoint[0] * 100.0f) ^
                        (unsigned int)(hitPoint[1] * 97.0f) ^
                        (unsigned int)(hitPoint[2] * 101.0f);
    unsigned int hashY = (unsigned int)(hitPoint[0] * 103.0f) ^
                        (unsigned int)(hitPoint[1] * 107.0f) ^
                        (unsigned int)(hitPoint[2] * 109.0f);

    // Map to density grid
    int x = abs((int)hashX) % rtxDebugOverlay.densityMapWidth;
    int y = abs((int)hashY) % rtxDebugOverlay.densityMapHeight;

    // Update density map cell
    if (x >= 0 && x < rtxDebugOverlay.densityMapWidth &&
        y >= 0 && y < rtxDebugOverlay.densityMapHeight) {
        int idx = y * rtxDebugOverlay.densityMapWidth + x;

        // Simple weight based on hit normal
        float weight = 1.0f;
        if (normal) {
            // Weight by how much the normal faces "up" (assuming Y is up)
            weight = 0.5f + normal[1] * 0.5f; // Range [0, 1]
        }

        // Accumulate weighted density
        rtxDebugOverlay.rayDensityMap[idx] += weight;

        // Track total hits for statistics
        rtxDebugOverlay.totalRayHits++;
    }
}

/*
================
RTX_RecordRayHitWithCamera

Record a ray hit with explicit camera parameters for proper screen-space projection
================
*/
void RTX_RecordRayHitWithCamera(const vec3_t hitPoint, const vec3_t normal,
                                 const vec3_t cameraPos, const vec3_t cameraForward,
                                 const vec3_t cameraRight, const vec3_t cameraUp,
                                 float fovX, float fovY) {
    if (!rtxDebugOverlay.rayDensityMap) {
        return;
    }

    // Transform world point to view space
    vec3_t viewPos;
    VectorSubtract(hitPoint, cameraPos, viewPos);

    // Apply view rotation using camera axes
    vec3_t transformed;
    transformed[0] = DotProduct(viewPos, cameraRight);    // right
    transformed[1] = DotProduct(viewPos, cameraUp);       // up
    transformed[2] = -DotProduct(viewPos, cameraForward); // forward (negated for RH)

    // Skip if behind camera
    if (transformed[2] <= 0.1f) {
        return;
    }

    // Project to normalized device coordinates
    float tanHalfFovY = tan(fovY * 0.5f);
    float tanHalfFovX = tan(fovX * 0.5f);

    float ndcX = transformed[0] / (transformed[2] * tanHalfFovX);
    float ndcY = transformed[1] / (transformed[2] * tanHalfFovY);

    // Convert NDC to screen coordinates [0,1]
    float screenX = (ndcX + 1.0f) * 0.5f;
    float screenY = (1.0f - ndcY) * 0.5f; // Flip Y for screen coordinates

    // Calculate density map coordinates
    int x = (int)(screenX * rtxDebugOverlay.densityMapWidth);
    int y = (int)(screenY * rtxDebugOverlay.densityMapHeight);

    // Update density map cell with bounds checking
    if (x >= 0 && x < rtxDebugOverlay.densityMapWidth &&
        y >= 0 && y < rtxDebugOverlay.densityMapHeight) {
        int idx = y * rtxDebugOverlay.densityMapWidth + x;

        // Weight contribution by distance (closer hits contribute more)
        float distance = VectorLength(viewPos);
        float weight = 1.0f / (1.0f + distance * 0.001f); // Distance falloff

        // Also weight by normal facing camera (front-facing surfaces contribute more)
        vec3_t viewDir;
        VectorNormalize2(viewPos, viewDir);
        float normalDot = -DotProduct(normal, viewDir);
        if (normalDot > 0.0f) {
            weight *= normalDot;
        }

        // Accumulate weighted density
        rtxDebugOverlay.rayDensityMap[idx] += weight;

        // Track total hits for statistics
        rtxDebugOverlay.totalRayHits++;
    }
}

/*
================
RTX_UpdateRayDensityMap

Update and normalize the ray density map
================
*/
void RTX_UpdateRayDensityMap(void) {
    if (!rtxDebugOverlay.rayDensityMap) {
        return;
    }
    
    // Find max density for normalization
    float maxDensity = 0.0f;
    int mapSize = rtxDebugOverlay.densityMapWidth * rtxDebugOverlay.densityMapHeight;
    
    for (int i = 0; i < mapSize; i++) {
        if (rtxDebugOverlay.rayDensityMap[i] > maxDensity) {
            maxDensity = rtxDebugOverlay.rayDensityMap[i];
        }
    }
    
    // Normalize density values if we have data
    if (maxDensity > 0.0f) {
        float scale = 1.0f / maxDensity;
        for (int i = 0; i < mapSize; i++) {
            rtxDebugOverlay.rayDensityMap[i] *= scale;
        }
    }
    
    // Apply temporal smoothing (optional)
    // This could blend with previous frame's density map
    // for smoother visualization
}

/*
================
RTX_ClearRayDensityMap

Clear the ray density accumulation buffer
================
*/
void RTX_ClearRayDensityMap(void) {
    if (rtxDebugOverlay.rayDensityMap) {
        Com_Memset(rtxDebugOverlay.rayDensityMap, 0,
                  sizeof(float) * rtxDebugOverlay.densityMapWidth * 
                  rtxDebugOverlay.densityMapHeight);
    }
}
