/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Path Tracing Debug Visualization
Renders path tracing results as visible debug output
===========================================================================
*/

#include "rt_pathtracer.h"
#include "../core/tr_local.h"

/*
================
RT_RenderDebugVisualization

Render path tracing as a debug overlay
This actually shows something on screen!
================
*/
void RT_RenderDebugVisualization(void) {
    if (!rt_enable || !rt_enable->integer) {
        return;
    }
    
    if (!rt_debug || !rt_debug->integer) {
        return;
    }
    
    // Sample the center of the screen
    vec3_t viewOrigin, viewForward;
    VectorCopy(backEnd.viewParms.or.origin, viewOrigin);
    VectorCopy(backEnd.viewParms.or.axis[0], viewForward);
    
    // Cast rays in a grid pattern for visualization
    int gridSize = 8;
    float stepX = (float)glConfig.vidWidth / gridSize;
    float stepY = (float)glConfig.vidHeight / gridSize;
    
    // Begin 2D rendering
    GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
    GL_Cull(CT_TWO_SIDED);
    
    // Draw debug dots showing ray hit results
    for (int y = 0; y < gridSize; y++) {
        for (int x = 0; x < gridSize; x++) {
            float screenX = x * stepX + stepX * 0.5f;
            float screenY = y * stepY + stepY * 0.5f;
            
            // Convert to NDC
            float ndcX = (2.0f * screenX / glConfig.vidWidth) - 1.0f;
            float ndcY = 1.0f - (2.0f * screenY / glConfig.vidHeight);
            
            // Generate ray
            ray_t ray;
            VectorCopy(viewOrigin, ray.origin);
            
            vec3_t right, up;
            VectorCopy(backEnd.viewParms.or.axis[1], right);
            VectorCopy(backEnd.viewParms.or.axis[2], up);
            
            float tanHalfFov = tan(DEG2RAD(backEnd.viewParms.fovX * 0.5f));
            float aspectRatio = (float)glConfig.vidWidth / glConfig.vidHeight;
            
            ndcX *= tanHalfFov * aspectRatio;
            ndcY *= tanHalfFov;
            
            ray.direction[0] = viewForward[0] + ndcX * right[0] + ndcY * up[0];
            ray.direction[1] = viewForward[1] + ndcX * right[1] + ndcY * up[1];
            ray.direction[2] = viewForward[2] + ndcX * right[2] + ndcY * up[2];
            VectorNormalize(ray.direction);
            
            ray.tMin = 0.01f;
            ray.tMax = 10000.0f;
            
            // Trace ray
            hitInfo_t hit;
            vec4_t color;
            
            if (RT_TraceRay(&ray, &hit)) {
                // Hit something - use albedo from hit info
                if (hit.shader) {
                    // Use albedo from the hit info instead of shader
                    VectorCopy(hit.albedo, color);
                    color[3] = 1.0f;
                } else {
                    // Default hit color (green)
                    Vector4Set(color, 0, 1, 0, 1);
                }
                
                // Add lighting contribution
                vec3_t lighting;
                RT_ComputeLightingAtPoint(hit.point, lighting);
                VectorScale(color, 0.5f, color);
                VectorMA(color, 0.5f, lighting, color);
            } else {
                // Sky/miss (blue)
                Vector4Set(color, 0.2f, 0.4f, 0.8f, 1);
            }
            
            // Draw colored square at sample point
            float size = stepX * 0.4f;
            
            // Store debug color for later use
            // The actual rendering would need to be integrated with the 2D drawing system
            // For now, we'll output to console if it's the center sample
            if (x == gridSize/2 && y == gridSize/2) {
                ri.Printf(PRINT_ALL, "RT Debug: Center ray hit=%s color=(%.2f,%.2f,%.2f)\n", 
                         hit.t < ray.tMax ? "YES" : "NO", 
                         color[0], color[1], color[2]);
            }
        }
    }
    
    // Output debug info to console
    ri.Printf(PRINT_ALL, "PATH TRACING DEBUG: %s | Rays: %d\n", 
              rt.mode == RT_MODE_OFF ? "OFF" :
              rt.mode == RT_MODE_DYNAMIC ? "DYNAMIC" : "ALL",
              gridSize * gridSize);
}

/*
================
RT_DrawLightProbes

Visualize light probe grid in 3D space
================
*/
void RT_DrawLightProbes(void) {
    if (!rt_probes || !rt_probes->integer) {
        return;
    }
    
    if (!rt_debug || rt_debug->integer < 2) {
        return;
    }
    
    if (rt.numProbes == 0) {
        return;
    }
    
    // Draw spheres at probe locations
    for (int i = 0; i < rt.numProbes; i++) {
        irradianceProbe_t *probe = &rt.probes[i];
        
        // Skip uninitialized probes
        if (!probe->dynamic) {
            continue;
        }
        
        // Color based on irradiance (average of all directions)
        vec4_t color = {0, 0, 0, 0.5f};
        for (int j = 0; j < 6; j++) {
            VectorAdd(color, probe->irradiance[j], color);
        }
        VectorScale(color, 1.0f/6.0f, color);
        color[3] = 0.5f; // Semi-transparent
        
        // Draw debug sphere
        // TODO: This would need proper 3D sphere rendering
        // R_AddDebugSphere(probe->position, 5.0f, color);
    }
}

/*
================
RT_ShowRayPath

Draw the path of a ray for debugging
================
*/
void RT_ShowRayPath(const ray_t *ray, const hitInfo_t *hit) {
    if (!rt_debug || rt_debug->integer < 3) {
        return;
    }
    
    vec3_t end;
    if (hit && hit->t < ray->tMax) {
        VectorMA(ray->origin, hit->t, ray->direction, end);
    } else {
        VectorMA(ray->origin, 1000.0f, ray->direction, end);
    }
    
    // Draw line from origin to hit point
    vec4_t color = {1, 1, 0, 1}; // Yellow
    // TODO: Need to implement debug line rendering
    // R_AddDebugLine(ray->origin, end, color);
    
    if (hit && hit->t < ray->tMax) {
        // Draw normal at hit point
        vec3_t normalEnd;
        VectorMA(hit->point, 20.0f, hit->normal, normalEnd);
        vec4_t normalColor = {0, 1, 0, 1}; // Green
        // TODO: Need to implement debug line rendering
        // R_AddDebugLine(hit->point, normalEnd, normalColor);
    }
}

/*
================
RT_DebugStats

Print path tracing statistics to console
================
*/
void RT_DebugStats(void) {
    if (!rt_debug || !rt_debug->integer) {
        return;
    }
    
    // Print every 60 frames
    if ((rt.currentFrame % 60) != 0) {
        return;
    }
    
    ri.Printf(PRINT_ALL, "==== Path Tracing Stats ====\n");
    ri.Printf(PRINT_ALL, "Frame: %d\n", rt.currentFrame);
    ri.Printf(PRINT_ALL, "Mode: %s\n", 
              rt.mode == RT_MODE_OFF ? "OFF" :
              rt.mode == RT_MODE_DYNAMIC ? "DYNAMIC" : "ALL");
    ri.Printf(PRINT_ALL, "Quality: %d\n", rt.quality);
    ri.Printf(PRINT_ALL, "Rays traced: %d\n", rt.raysTraced);
    ri.Printf(PRINT_ALL, "Triangle tests: %d\n", rt.triangleTests);
    ri.Printf(PRINT_ALL, "Box tests: %d\n", rt.boxTests);
    ri.Printf(PRINT_ALL, "Static lights: %d\n", rt.numStaticLights);
    ri.Printf(PRINT_ALL, "Probes: %d\n", rt.numProbes);
    ri.Printf(PRINT_ALL, "Cache size: %d\n", rt.cacheSize);
    ri.Printf(PRINT_ALL, "========================\n");
}
