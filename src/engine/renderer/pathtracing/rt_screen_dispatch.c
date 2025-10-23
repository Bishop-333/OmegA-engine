/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Full-Screen Ray Tracing Dispatch
Renders complete frames using path tracing
===========================================================================
*/

#include "rt_pathtracer.h"
#include "rt_rtx.h"
#include "../core/tr_local.h"

// Screen ray tracing state
typedef struct {
    float *colorBuffer;      // RGB float buffer
    float *depthBuffer;      // Depth buffer
    float *normalBuffer;     // World space normals
    float *albedoBuffer;     // Base color for denoising
    int width;
    int height;
    int currentSample;      // For progressive rendering
    qboolean initialized;
} screenRT_t;

static screenRT_t screenRT;

/*
================
RT_AllocateScreenBuffers

Allocate buffers for full-screen ray tracing
================
*/
void RT_AllocateScreenBuffers(int width, int height) {
    if (screenRT.initialized && screenRT.width == width && screenRT.height == height) {
        return;
    }
    
    // Free old buffers
    if (screenRT.colorBuffer) ri.Free(screenRT.colorBuffer);
    if (screenRT.depthBuffer) ri.Free(screenRT.depthBuffer);
    if (screenRT.normalBuffer) ri.Free(screenRT.normalBuffer);
    if (screenRT.albedoBuffer) ri.Free(screenRT.albedoBuffer);
    
    // Allocate new buffers
    int pixelCount = width * height;
    screenRT.colorBuffer = ri.Malloc(pixelCount * 3 * sizeof(float));
    screenRT.depthBuffer = ri.Malloc(pixelCount * sizeof(float));
    screenRT.normalBuffer = ri.Malloc(pixelCount * 3 * sizeof(float));
    screenRT.albedoBuffer = ri.Malloc(pixelCount * 3 * sizeof(float));
    
    if (screenRT.colorBuffer) {
        Com_Memset(screenRT.colorBuffer, 0, pixelCount * 3 * sizeof(float));
    }
    if (screenRT.depthBuffer) {
        Com_Memset(screenRT.depthBuffer, 0, pixelCount * sizeof(float));
    }
    if (screenRT.normalBuffer) {
        Com_Memset(screenRT.normalBuffer, 0, pixelCount * 3 * sizeof(float));
    }
    if (screenRT.albedoBuffer) {
        Com_Memset(screenRT.albedoBuffer, 0, pixelCount * 3 * sizeof(float));
    }
    
    screenRT.width = width;
    screenRT.height = height;
    screenRT.initialized = qtrue;
    screenRT.currentSample = 0;
    
    ri.Printf(PRINT_ALL, "RT: Allocated screen buffers %dx%d\n", width, height);
}

/*
================
RT_RenderScreenPixel

Render a single pixel using path tracing
================
*/
static void RT_RenderScreenPixel(int x, int y) {
    int pixelIndex = y * screenRT.width + x;
    
    // Generate primary ray
    ray_t ray;
    RT_BuildCameraRay(x, y, screenRT.width, screenRT.height, &ray);
    
    // Trace path
    vec3_t color;
    RT_TracePath(&ray, 0, color);

    // Accumulate temporal history
    RT_AccumulateSample(x, y, color);

    vec3_t displayColor;
    if (rt.temporalEnabled) {
        RT_GetAccumulatedColor(x, y, displayColor);
    } else {
        VectorCopy(color, displayColor);
    }
    
    // Store results
    screenRT.colorBuffer[pixelIndex * 3 + 0] = displayColor[0];
    screenRT.colorBuffer[pixelIndex * 3 + 1] = displayColor[1];
    screenRT.colorBuffer[pixelIndex * 3 + 2] = displayColor[2];
    
    // Trace for additional buffers (for denoising)
    hitInfo_t hit;
    if (RT_TraceRay(&ray, &hit)) {
        screenRT.depthBuffer[pixelIndex] = hit.t;
        VectorCopy(hit.normal, &screenRT.normalBuffer[pixelIndex * 3]);
        // Use albedo from hit info instead of shader
        VectorCopy(hit.albedo, &screenRT.albedoBuffer[pixelIndex * 3]);
    } else {
        screenRT.depthBuffer[pixelIndex] = ray.tMax;
        VectorClear(&screenRT.normalBuffer[pixelIndex * 3]);
        VectorSet(&screenRT.albedoBuffer[pixelIndex * 3], 0.5f, 0.7f, 1.0f); // Sky color
    }
}

static void RT_UpdateColorBufferFromSource(const float *source) {
    if (!screenRT.initialized || !screenRT.colorBuffer || !source) {
        return;
    }

    int pixelCount = screenRT.width * screenRT.height;
    for (int i = 0; i < pixelCount; i++) {
        if (rt.sampleBuffer && rt.sampleBuffer[i] <= 0) {
            continue;
        }

        int base = i * 3;
        screenRT.colorBuffer[base + 0] = source[base + 0];
        screenRT.colorBuffer[base + 1] = source[base + 1];
        screenRT.colorBuffer[base + 2] = source[base + 2];
    }
}

/*
================
RT_RenderFullScreen

Main full-screen ray tracing function
This replaces the entire rasterization pipeline
================
*/
void RT_RenderFullScreen(void) {
    if (!rt_enable || !rt_enable->integer) {
        return;
    }
    
    // Ensure buffers are allocated
    RT_AllocateScreenBuffers(glConfig.vidWidth, glConfig.vidHeight);
    RT_InitTemporalBuffers();
    
    // Progressive rendering - render in tiles for interactivity
    int tileSize = 32;
    int tilesX = (screenRT.width + tileSize - 1) / tileSize;
    int tilesY = (screenRT.height + tileSize - 1) / tileSize;
    int totalTiles = tilesX * tilesY;
    
    // Render a subset of tiles per frame for responsiveness
    int tilesPerFrame = MAX(1, totalTiles / 16);
    int startTile = (screenRT.currentSample * tilesPerFrame) % totalTiles;
    
    for (int i = 0; i < tilesPerFrame && startTile + i < totalTiles; i++) {
        int tileIndex = startTile + i;
        int tileX = tileIndex % tilesX;
        int tileY = tileIndex / tilesX;
        
        int startX = tileX * tileSize;
        int startY = tileY * tileSize;
        int endX = MIN(startX + tileSize, screenRT.width);
        int endY = MIN(startY + tileSize, screenRT.height);
        
        // Render tile
        for (int y = startY; y < endY; y++) {
            for (int x = startX; x < endX; x++) {
                RT_RenderScreenPixel(x, y);
            }
        }
    }
    
    screenRT.currentSample++;

    const float *source = NULL;
    if (rt_denoise && rt_denoise->integer && rt.accumBuffer && rt.denoisedBuffer) {
        RT_DenoiseFrame(rt.accumBuffer, rt.denoisedBuffer, screenRT.width, screenRT.height);
        source = rt.denoisedBuffer;
    } else if (rt.accumBuffer) {
        source = rt.accumBuffer;
    }

    RT_UpdateColorBufferFromSource(source);
}

/*
================
RT_CopyToFramebuffer

Copy ray traced results to OpenGL/Vulkan framebuffer
================
*/
void RT_CopyToFramebuffer(void) {
    if (!screenRT.initialized || !screenRT.colorBuffer) {
        return;
    }
    
#ifdef USE_VULKAN
    // Vulkan path - copy to swapchain image
    // TODO: Implement Vulkan screen texture upload
    // vk_upload_screen_texture(screenRT.colorBuffer, screenRT.width, screenRT.height);
    ri.Printf(PRINT_WARNING, "RT_CopyToFramebuffer: Vulkan path not implemented\n");
#else
    // OpenGL path - draw fullscreen quad with RT results
    GL_Bind(tr.screenImageRT);
    
    // Upload RT buffer to texture
    qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, 
                  screenRT.width, screenRT.height, 0,
                  GL_RGB, GL_FLOAT, screenRT.colorBuffer);
    
    // Draw fullscreen quad
    GL_State(GLS_DEPTHTEST_DISABLE);
    
    qglBegin(GL_QUADS);
    qglTexCoord2f(0, 0);
    qglVertex2f(-1, -1);
    qglTexCoord2f(1, 0);
    qglVertex2f(1, -1);
    qglTexCoord2f(1, 1);
    qglVertex2f(1, 1);
    qglTexCoord2f(0, 1);
    qglVertex2f(-1, 1);
    qglEnd();
#endif
}

/*
================
RT_ScreenDispatchCommands

Process RT commands for screen rendering
================
*/
void RT_ScreenDispatchCommands(void) {
    // Check if we should replace rasterization
    if (!rt_enable || !rt_enable->integer) {
        return;
    }
    
    if (Q_stricmp(rt_mode->string, "replace") == 0) {
        // Full replacement mode - skip rasterization entirely
        RT_RenderFullScreen();
        RT_CopyToFramebuffer();
    } else if (Q_stricmp(rt_mode->string, "hybrid") == 0) {
        // Hybrid mode - rasterize geometry, ray trace lighting
        // This is handled by the existing point-query system
    }
}

/*
================
RT_FreeScreenBuffers

Free screen RT buffers
================
*/
void RT_FreeScreenBuffers(void) {
    if (screenRT.colorBuffer) ri.Free(screenRT.colorBuffer);
    if (screenRT.depthBuffer) ri.Free(screenRT.depthBuffer);
    if (screenRT.normalBuffer) ri.Free(screenRT.normalBuffer);
    if (screenRT.albedoBuffer) ri.Free(screenRT.albedoBuffer);
    
    Com_Memset(&screenRT, 0, sizeof(screenRT));
}

void RT_ResetScreenProgress(void) {
    screenRT.currentSample = 0;

    if (!screenRT.initialized || !screenRT.colorBuffer) {
        return;
    }

    int pixelCount = screenRT.width * screenRT.height;
    Com_Memset(screenRT.colorBuffer, 0, pixelCount * 3 * sizeof(float));
}
