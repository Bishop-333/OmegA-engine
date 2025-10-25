/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

RTX Hardware Raytracing Implementation
Provides hardware acceleration for path tracing using RTX cores
===========================================================================
*/

#include "rt_rtx.h"
#include <stdarg.h>
#include "rt_pathtracer.h"
#include "../core/tr_local.h"
#ifdef USE_VULKAN
#include "../vulkan/vk.h"
#endif

#ifdef USE_VULKAN
extern void RTX_LoadWorldMap(void);
#endif

// Global RTX state
rtxState_t rtx;
static qboolean rtxInitialized = qfalse;
static char rtxLastStatus[128] = "RTX not initialised";

static void RTX_SetLastStatus(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Q_vsnprintf(rtxLastStatus, sizeof(rtxLastStatus), fmt, args);
    va_end(args);
}

const char *RTX_GetLastStatus(void)
{
    return rtxLastStatus;
}

// Forward declarations
void RTX_Status_f(void);

// CVARs
cvar_t *rtx_enable;
cvar_t *rtx_quality;
cvar_t *rtx_denoise;
cvar_t *rtx_dlss;
cvar_t *rtx_reflex;
cvar_t *rtx_gi_bounces;
cvar_t *rtx_reflection_quality;
cvar_t *rtx_shadow_quality;
cvar_t *rtx_debug;
cvar_t *rtx_notextures;
cvar_t *rtx_hybrid_intensity;
cvar_t *rtx_surface_debug;

cvar_t *r_rtx_enabled;
cvar_t *r_rtx_quality;
cvar_t *r_rtx_denoise;
cvar_t *r_rtx_dlss;
cvar_t *r_rtx_reflex;
cvar_t *r_rtx_gi_bounces;
cvar_t *r_rtx_hybrid_intensity;
cvar_t *r_rtx_debug;
cvar_t *r_rtx_notextures;
cvar_t *r_rtx_surface_debug;

// ============================================================================
// Initialization
// ============================================================================

/*
================
RTX_Init

Initialize RTX hardware raytracing
================
*/
qboolean RTX_Init(void) {
    if (rtxInitialized) {
        ri.Printf(PRINT_ALL, "RTX: hardware raytracing already initialized\n");
        return rtx.available ? qtrue : qfalse;
    }

    Com_Memset(&rtx, 0, sizeof(rtx));
    RTX_SetLastStatus("RTX initialising");
    
    // Register CVARs
    rtx_enable = ri.Cvar_Get("rtx_enable", "1", CVAR_ARCHIVE | CVAR_LATCH);  // Enable by default
    rtx_quality = ri.Cvar_Get("rtx_quality", "2", CVAR_ARCHIVE);
    rtx_denoise = ri.Cvar_Get("rtx_denoise", "1", CVAR_ARCHIVE);
    rtx_dlss = ri.Cvar_Get("rtx_dlss", "0", CVAR_ARCHIVE);
    rtx_reflex = ri.Cvar_Get("rtx_reflex", "0", CVAR_ARCHIVE);
    rtx_gi_bounces = ri.Cvar_Get("rtx_gi_bounces", "2", CVAR_ARCHIVE);
    rtx_reflection_quality = ri.Cvar_Get("rtx_reflection_quality", "2", CVAR_ARCHIVE);
    rtx_shadow_quality = ri.Cvar_Get("rtx_shadow_quality", "2", CVAR_ARCHIVE);
    rtx_debug = ri.Cvar_Get("rtx_debug", "0", CVAR_CHEAT);
    rtx_notextures = ri.Cvar_Get("rtx_notextures", "0", CVAR_ARCHIVE);
    rtx_hybrid_intensity = ri.Cvar_Get("rtx_hybrid_intensity", "1.0", CVAR_ARCHIVE);
    rtx_surface_debug = ri.Cvar_Get("rtx_surface_debug", "0", CVAR_CHEAT);
    
    // Always register console command so users can check RTX status
    ri.Cmd_AddCommand("rtx_status", RTX_Status_f);

    r_rtx_enabled = rtx_enable;
    r_rtx_quality = rtx_quality;
    r_rtx_denoise = rtx_denoise;
    r_rtx_dlss = rtx_dlss;
    r_rtx_reflex = rtx_reflex;
    r_rtx_gi_bounces = rtx_gi_bounces;
    r_rtx_hybrid_intensity = rtx_hybrid_intensity;
    r_rtx_debug = rtx_debug;
    r_rtx_notextures = rtx_notextures;
    r_rtx_surface_debug = rtx_surface_debug;
    
    if (!rtx_enable->integer) {
        ri.Printf(PRINT_ALL, "RTX: Hardware raytracing disabled (rtx_enable = 0)\n");
        ri.Printf(PRINT_ALL, "RTX: Set rtx_enable 1 and vid_restart to enable RTX support\n");
        ri.Printf(PRINT_ALL, "RTX: Use 'rtx_status' command to check GPU capabilities\n");
        RTX_SetLastStatus("RTX disabled via rtx_enable");
        return qfalse;
    }
    
    ri.Printf(PRINT_ALL, "Initializing RTX hardware raytracing...\n");
    
    // Initialize Vulkan RT directly since we're Vulkan-only
    if (RTX_InitVulkanRT()) {
        rtx.available = qtrue;
        ri.Printf(PRINT_ALL, "RTX: Vulkan Ray Tracing initialized successfully\n");
        RTX_SetLastStatus("Vulkan RT initialised");
    } else {
        ri.Printf(PRINT_WARNING, "RTX: Vulkan RT initialization failed\n");
        RTX_SetLastStatus("Vulkan RT initialization failed");
    }
    
    if (!rtx.available) {
        ri.Printf(PRINT_WARNING, "RTX: No hardware raytracing support detected\n");
        RTX_SetLastStatus("RTX unavailable on current hardware");
        return qfalse;
    }
    
    // Initialize RT pipeline system
    if (!RTX_InitializePipeline()) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to initialize pipeline system\n");
        RTX_ShutdownVulkanRT();
        rtx.available = qfalse;
        RTX_SetLastStatus("RTX pipeline initialization failed");
        return qfalse;
    }
    
    // Initialize material cache
    RTX_InitMaterialCache();
    
    // Allocate BLAS pool
    rtx.maxBLAS = 1024;
    rtx.blasPool = ri.Hunk_Alloc(sizeof(rtxBLAS_t) * rtx.maxBLAS, h_low);
    rtx.numBLAS = 0;
    
    // Create main TLAS
    rtx.tlas.instances = ri.Hunk_Alloc(sizeof(rtxInstance_t) * RTX_MAX_INSTANCES, h_low);
    rtx.tlas.numInstances = 0;
    rtx.tlas.activeHandle = 0;
    rtx.tlas.handles[0] = NULL;
    rtx.tlas.handles[1] = NULL;
    rtx.tlas.dirtyTransforms = qfalse;

    rtx.refitQueueCount = 0;
    rtx.refitQueueOverflow = qfalse;
    
    // Initialize denoiser if available
    if (rtx_denoise->integer && (rtx.features & RTX_FEATURE_DENOISER)) {
        // Use Vulkan render dimensions if available, fallback to glConfig
        int width = vk.renderWidth ? vk.renderWidth : glConfig.vidWidth;
        int height = vk.renderHeight ? vk.renderHeight : glConfig.vidHeight;
        if (RTX_InitDenoiser(width, height)) {
            ri.Printf(PRINT_ALL, "RTX: Hardware denoiser initialized\n");
        }
    }
    
    // Initialize DLSS if available and requested
    if (rtx_dlss->integer) {
        if (RTX_InitDLSS()) {
            ri.Printf(PRINT_ALL, "RTX: DLSS initialized\n");
        }
    }
    
    ri.Printf(PRINT_ALL, "RTX: Initialization complete - use 'rtx_status' for details\n");

    rtxInitialized = qtrue;
    RTX_SetLastStatus("RTX initialised successfully");

    return qtrue;
}

/*
================
RTX_Shutdown

Cleanup RTX resources
================
*/
void RTX_Shutdown(void) {
    if (!rtx.available) {
        return;
    }

    RTX_SetLastStatus("RTX shutdown");
    
    // Cleanup denoiser
    if (rtx.denoiser.enabled) {
        RTX_ShutdownDenoiser();
    }
    
    // Cleanup DLSS
    if (rtx_dlss && rtx_dlss->integer) {
        RTX_ShutdownDLSS();
    }
    
    // Cleanup material cache
    RTX_ShutdownMaterialCache();
    
    // Destroy BLAS pool
    for (int i = 0; i < rtx.numBLAS; i++) {
        RTX_DestroyBLAS(&rtx.blasPool[i]);
    }
    
    // Destroy TLAS
    RTX_DestroyTLAS(&rtx.tlas);
    
    // Shutdown pipeline system
    RTX_ShutdownPipeline();
    
    // Shutdown Vulkan RT
    RTX_ShutdownVulkanRT();
    
    Com_Memset(&rtx, 0, sizeof(rtx));
    rtxInitialized = qfalse;
}

/*
================
RTX_IsAvailable

Check if RTX is available
================
*/
qboolean RTX_IsAvailable(void) {
    return rtx.available && rtx_enable->integer;
}

/*
================
RTX_IsEnabled

Return whether the RTX hardware path is allowed by configuration.
================
*/
qboolean RTX_IsEnabled(void) {
    return (rtx_enable && rtx_enable->integer) ? qtrue : qfalse;
}

/*
================
RTX_GetFeatures

Get available RTX features
================
*/
unsigned int RTX_GetFeatures(void) {
    return rtx.features;
}

/*
================
RTX_GetHybridIntensity

Return the configured hybrid composite intensity, clamped to a sensible range.
================
*/
float RTX_GetHybridIntensity(void) {
    float intensity = 1.0f;

    if ( r_rtx_hybrid_intensity ) {
        intensity = r_rtx_hybrid_intensity->value;
    } else if ( rtx_hybrid_intensity ) {
        intensity = rtx_hybrid_intensity->value;
    }

    return Com_Clamp( 0.0f, 8.0f, intensity );
}

// ============================================================================
// Acceleration Structure Management
// ============================================================================

/*
================
RTX_CreateBLAS

Create Bottom Level Acceleration Structure for a mesh
================
*/
rtxBLAS_t* RTX_CreateBLAS(const vec3_t *vertices, int numVerts,
                          const unsigned int *indices, int numIndices,
                          const uint32_t *triangleMaterials,
                          qboolean isDynamic) {
    rtxBLAS_t *blas;
    
    if (rtx.numBLAS >= rtx.maxBLAS) {
        ri.Printf(PRINT_WARNING, "RTX: BLAS pool exhausted\n");
        return NULL;
    }
    
    blas = &rtx.blasPool[rtx.numBLAS++];
    Com_Memset(blas, 0, sizeof(rtxBLAS_t));
    
    // Store geometry data
    blas->numVertices = numVerts;
    blas->numTriangles = numIndices / 3;
    blas->isDynamic = isDynamic;
    
    // Allocate and copy vertex data
    blas->vertices = ri.Hunk_Alloc(sizeof(vec3_t) * numVerts, h_low);
    Com_Memcpy(blas->vertices, vertices, sizeof(vec3_t) * numVerts);
    
    // Allocate and copy index data
    blas->indices = ri.Hunk_Alloc(sizeof(unsigned int) * numIndices, h_low);
    Com_Memcpy(blas->indices, indices, sizeof(unsigned int) * numIndices);

    if (triangleMaterials && blas->numTriangles > 0) {
        blas->triangleMaterials = ri.Hunk_Alloc(sizeof(uint32_t) * blas->numTriangles, h_low);
        Com_Memcpy(blas->triangleMaterials, triangleMaterials, sizeof(uint32_t) * blas->numTriangles);
    } else {
        blas->triangleMaterials = NULL;
    }
    
    // Calculate AABB
    VectorCopy(vertices[0], blas->aabbMin);
    VectorCopy(vertices[0], blas->aabbMax);
    
    for (int i = 1; i < numVerts; i++) {
        for (int j = 0; j < 3; j++) {
            if (vertices[i][j] < blas->aabbMin[j]) {
                blas->aabbMin[j] = vertices[i][j];
            }
            if (vertices[i][j] > blas->aabbMax[j]) {
                blas->aabbMax[j] = vertices[i][j];
            }
        }
    }
    
    // Set build flags
    if (isDynamic) {
        blas->buildFlags = 0x01; // Allow update
    } else {
        blas->buildFlags = 0x02; // Prefer fast trace
    }
    
    // Vulkan RT BLAS creation is handled in rt_rtx_impl.c
    
    return blas;
}

/*
================
RTX_DestroyBLAS

Destroy a BLAS
================
*/
void RTX_DestroyBLAS(rtxBLAS_t *blas) {
    if (!blas) {
        return;
    }
    
    // Vulkan RT cleanup is handled in rt_rtx_impl.c
    
    // Clear the structure
    Com_Memset(blas, 0, sizeof(rtxBLAS_t));
}

/*
================
RTX_CreateTLAS

Create Top Level Acceleration Structure
================
*/
rtxTLAS_t* RTX_CreateTLAS(int maxInstances) {
    // Not implemented yet
    return NULL;
}

/*
================
RTX_DestroyTLAS

Destroy a TLAS
================
*/
void RTX_DestroyTLAS(rtxTLAS_t *tlas) {
    if (!tlas) {
        return;
    }
    
    // Vulkan RT cleanup is handled in rt_rtx_impl.c
    
    // Clear the structure
    Com_Memset(tlas, 0, sizeof(rtxTLAS_t));
}

/*
================
RTX_UpdateBLAS

Update dynamic BLAS with new vertex positions
================
*/
void RTX_UpdateBLAS(rtxBLAS_t *blas, const vec3_t *vertices) {
    if (!blas || !blas->isDynamic) {
        return;
    }
    
    // Update vertex data
    Com_Memcpy(blas->vertices, vertices, sizeof(vec3_t) * blas->numVertices);
    
    // Recalculate AABB
    VectorCopy(vertices[0], blas->aabbMin);
    VectorCopy(vertices[0], blas->aabbMax);
    
    for (int i = 1; i < blas->numVertices; i++) {
        for (int j = 0; j < 3; j++) {
            if (vertices[i][j] < blas->aabbMin[j]) {
                blas->aabbMin[j] = vertices[i][j];
            }
            if (vertices[i][j] > blas->aabbMax[j]) {
                blas->aabbMax[j] = vertices[i][j];
            }
        }
    }
    
    // Vulkan RT BLAS update is handled in rt_rtx_impl.c
}

/*
================
RTX_QueueInstanceRefit

Schedule a TLAS instance for transform and/or BLAS rebuild
================
*/
qboolean RTX_QueueInstanceRefit(int instanceIndex, const float *transform, qboolean rebuildBLAS) {
    if (instanceIndex < 0 || instanceIndex >= rtx.tlas.numInstances) {
        return qfalse;
    }

    if (rtx.refitQueueCount >= RTX_MAX_REFIT_QUEUE) {
        if (!rtx.refitQueueOverflow) {
            ri.Printf(PRINT_WARNING, "RTX: Refit queue overflow (%d entries)\n", RTX_MAX_REFIT_QUEUE);
            rtx.refitQueueOverflow = qtrue;
        }
        return qfalse;
    }

    rtxRefitRequest_t *req = &rtx.refitQueue[rtx.refitQueueCount++];
    req->instanceIndex = instanceIndex;
    req->rebuildBLAS = rebuildBLAS ? qtrue : qfalse;
    req->hasTransform = transform ? qtrue : qfalse;

    if (transform) {
        Com_Memcpy(req->transform, transform, sizeof(req->transform));
        rtx.tlas.dirtyTransforms = qtrue;
    }

    rtx.tlas.needsRebuild = qtrue;
    return qtrue;
}

/*
================
RTX_ProcessPendingRefits

Flush queued dynamic updates before TLAS rebuild
================
*/
void RTX_ProcessPendingRefits(void) {
    if (rtx.refitQueueCount <= 0) {
        return;
    }

    for (int i = 0; i < rtx.refitQueueCount; i++) {
        const rtxRefitRequest_t *req = &rtx.refitQueue[i];

        if (req->instanceIndex < 0 || req->instanceIndex >= rtx.tlas.numInstances) {
            continue;
        }

        rtxInstance_t *instance = &rtx.tlas.instances[req->instanceIndex];

        if (req->hasTransform) {
            Com_Memcpy(instance->transform, req->transform, sizeof(req->transform));
        }

        if (req->rebuildBLAS && instance->blas) {
            RTX_DestroyBLASGPU(instance->blas);
            if (!RTX_BuildBLASGPU(instance->blas)) {
                ri.Printf(PRINT_WARNING, "RTX: Failed to rebuild dynamic BLAS for instance %d\n", req->instanceIndex);
            }
        }
    }

    rtx.refitQueueCount = 0;
    rtx.refitQueueOverflow = qfalse;
    rtx.tlas.dirtyTransforms = qfalse;
}

/*
================
RTX_AddInstance

Add an instance to the TLAS
================
*/
void RTX_AddInstance(rtxTLAS_t *tlas, rtxBLAS_t *blas,
                     const float *transform, material_t *material) {
    rtxInstance_t *instance;
    
    if (!tlas || !blas) {
        return;
    }
    
    if (tlas->numInstances >= RTX_MAX_INSTANCES) {
        ri.Printf(PRINT_WARNING, "RTX: Maximum instances reached\n");
        return;
    }
    
    instance = &tlas->instances[tlas->numInstances++];
    instance->blas = blas;
    instance->material = material;
    instance->instanceID = tlas->numInstances - 1;
    instance->mask = 0xFF;
    instance->shaderOffset = 0;
    instance->flags = 0;
    
    // Copy transform matrix
    if (transform) {
        Com_Memcpy(instance->transform, transform, sizeof(float) * 12);
    } else {
        // Identity matrix
        instance->transform[0] = 1.0f;
        instance->transform[1] = 0.0f;
        instance->transform[2] = 0.0f;
        instance->transform[3] = 0.0f;
        
        instance->transform[4] = 0.0f;
        instance->transform[5] = 1.0f;
        instance->transform[6] = 0.0f;
        instance->transform[7] = 0.0f;
        
        instance->transform[8] = 0.0f;
        instance->transform[9] = 0.0f;
        instance->transform[10] = 1.0f;
        instance->transform[11] = 0.0f;
    }
    
    tlas->needsRebuild = qtrue;
}

/*
================
RTX_BuildTLAS

Build/rebuild the TLAS
================
*/
void RTX_BuildTLAS(rtxTLAS_t *tlas) {
    if (!tlas) {
        return;
    }

    RTX_ProcessPendingRefits();

    if (!tlas->needsRebuild) {
        return;
    }
    
    // Build acceleration structure using Vulkan RT
    RTX_BuildAccelerationStructureVK();
    
    tlas->needsRebuild = qfalse;
}

void RTX_PrepareForWorld(void) {
#ifdef USE_VULKAN
    RTX_ResetTLASGPU();
#endif

    if (rtx.blasPool && rtx.maxBLAS > 0) {
        for (int i = 0; i < rtx.numBLAS; i++) {
            RTX_DestroyBLASGPU(&rtx.blasPool[i]);
        }
        Com_Memset(rtx.blasPool, 0, sizeof(rtxBLAS_t) * rtx.maxBLAS);
    }

    rtx.numBLAS = 0;

    if (rtx.tlas.instances) {
        Com_Memset(rtx.tlas.instances, 0, sizeof(rtxInstance_t) * RTX_MAX_INSTANCES);
    }

    rtx.tlas.numInstances = 0;
    rtx.tlas.needsRebuild = qfalse;
    rtx.tlas.dirtyTransforms = qfalse;
    rtx.tlas.handle = NULL;
    rtx.tlas.handles[0] = NULL;
    rtx.tlas.handles[1] = NULL;
    rtx.tlas.activeHandle = 0;

    rtx.refitQueueCount = 0;
    rtx.refitQueueOverflow = qfalse;

#ifdef USE_VULKAN
    rt.sceneLightBufferDirty = qtrue;
    RT_UpdateSceneLightBuffer();
#endif
}

void RTX_RequestWorldRefit(void) {
    if (!rtx.tlas.instances || rtx.tlas.numInstances <= 0) {
        return;
    }

    for (int i = 0; i < rtx.tlas.numInstances; i++) {
        RTX_QueueInstanceRefit(i, rtx.tlas.instances[i].transform, qfalse);
    }

    rtx.tlas.needsRebuild = qtrue;
}

void RTX_PopulateWorld(void) {
#ifdef USE_VULKAN
    if (!rtx_enable || !rtx_enable->integer) {
        return;
    }

    if (!tr.world) {
        return;
    }

    if (!RTX_IsAvailable()) {
        ri.Printf(PRINT_DEVELOPER, "RTX: Skipping world population - hardware backend unavailable\n");
        return;
    }

    if (rtx.numBLAS == 0 || rtx.tlas.numInstances == 0) {
        RTX_LoadWorldMap();
    } else {
        RTX_RequestWorldRefit();
    }

    if (rtx.tlas.numInstances > 0) {
        rtx.tlas.needsRebuild = qtrue;
    }
#endif
}

// ============================================================================
// Ray Dispatch
// ============================================================================

/*
================
RTX_TraceScene

Main RTX ray tracing entry point
================
*/
void RTX_TraceScene(int width, int height) {
    rtxDispatchRays_t params;
    
    if (!RTX_IsAvailable()) {
        return;
    }
    
    // Build acceleration structures if needed
    if (rtx.tlas.needsRebuild) {
        RTX_BuildTLAS(&rtx.tlas);
    }
    
    // Setup dispatch parameters
    params.width = width;
    params.height = height;
    params.depth = 1;
    params.maxRecursion = rtx_gi_bounces->integer;
    
    // Select pipeline based on quality
    switch (rtx_quality->integer) {
    case 4: // Ultra
        params.shaderTable = &rtx.giPipeline.shaderTable;
        break;
    case 3: // High
        params.shaderTable = &rtx.primaryPipeline.shaderTable;
        break;
    default:
        params.shaderTable = &rtx.shadowPipeline.shaderTable;
        break;
    }
    
    // Dispatch rays
    RTX_BeginFrame();
    RTX_DispatchRays(&params);
    
    // Apply denoising if enabled
    if (rtx_denoise->integer && rtx.denoiser.enabled) {
        RTX_DenoiseFrame(rtx.colorBuffer, rtx.colorBuffer);
    }
    
    // Apply DLSS upscaling if enabled
    if (rtx_dlss->integer) {
        int renderWidth = width;
        int renderHeight = height;
        
        // Determine render resolution based on DLSS mode
        switch (rtx_dlss->integer) {
        case DLSS_MODE_ULTRA_PERFORMANCE:
            renderWidth /= 3;
            renderHeight /= 3;
            break;
        case DLSS_MODE_PERFORMANCE:
            renderWidth /= 2;
            renderHeight /= 2;
            break;
        case DLSS_MODE_BALANCED:
            renderWidth = (int)(width / 1.7f);
            renderHeight = (int)(height / 1.7f);
            break;
        case DLSS_MODE_QUALITY:
            renderWidth = (int)(width / 1.5f);
            renderHeight = (int)(height / 1.5f);
            break;
        }
        
        RTX_UpscaleWithDLSS(rtx.colorBuffer, rtx.colorBuffer, renderWidth, renderHeight);
    }
    
    RTX_EndFrame();
}

/*
================
RTX_DispatchRays

Vulkan ray dispatch
================
*/
void RTX_DispatchRays(const rtxDispatchRays_t *params) {
    RTX_DispatchRaysVK(params);
}

// ============================================================================
// Path Tracer Integration
// ============================================================================

/*
================
RTX_AcceleratePathTracing

Use RTX to accelerate path tracing ray queries
================
*/
void RTX_AcceleratePathTracing(const ray_t *ray, hitInfo_t *hit) {
    if (!RTX_IsAvailable()) {
        // Caller should handle software fallback
        return;
    }
    
    // Dispatch hardware ray query
    rtxDispatchRays_t params = {
        .width = 1,
        .height = 1,
        .depth = 1,
        .maxRecursion = rtx_gi_bounces ? rtx_gi_bounces->integer : 2
    };
    
    RTX_DispatchRaysVK(&params);
}

/*
================
RTX_ShadowRayQuery

Hardware-accelerated shadow ray test
================
*/
void RTX_ShadowRayQuery(const vec3_t origin, const vec3_t target, float *visibility) {
    vec3_t dir;
    float dist;
    
    if (!RTX_IsAvailable()) {
        // Caller should handle software fallback
        *visibility = 1.0f;
        return;
    }
    
    // Hardware shadow query would be performed via ray query intrinsics
    // This is typically done within the shader, not from CPU
    *visibility = 1.0f;  // Default to no occlusion for now
    
    // Calculate ray direction and distance
    VectorSubtract(target, origin, dir);
    dist = VectorNormalize(dir);
    
    // Hardware shadow query would go here
    // For now use software
    *visibility = RT_TraceShadowRay(origin, target, dist) ? 0.0f : 1.0f;
}

/*
================
RTX_AmbientOcclusionQuery

Hardware-accelerated ambient occlusion
================
*/
void RTX_AmbientOcclusionQuery(const vec3_t pos, const vec3_t normal, float *ao) {
    int numSamples = 16;
    int numHits = 0;
    vec3_t sampleDir;
    ray_t aoRay;
    hitInfo_t aoHit;
    
    if (!RTX_IsAvailable()) {
        // Software AO calculation
        VectorCopy(pos, aoRay.origin);
        aoRay.tMin = 0.01f;
        aoRay.tMax = 10.0f;
        aoRay.depth = 0;
        
        for (int i = 0; i < numSamples; i++) {
            RT_CosineSampleHemisphere(normal, sampleDir);
            VectorCopy(sampleDir, aoRay.direction);
            
            if (RT_TraceRay(&aoRay, &aoHit)) {
                numHits++;
            }
        }
        
        *ao = 1.0f - ((float)numHits / (float)numSamples);
        return;
    }
    
    // Hardware AO query would go here
    *ao = 1.0f;
}

// ============================================================================
// Synchronization
// ============================================================================

/*
================
RTX_BeginFrame

Begin RTX frame
================
*/
void RTX_BeginFrame(void) {
    if (!rtx.available) {
        return;
    }
    
    rtx.frameCount++;
    
    // Reset per-frame stats
    rtx.buildTime = 0.0f;
    rtx.traceTime = 0.0f;
    rtx.denoiseTime = 0.0f;
}

/*
================
RTX_EndFrame

End RTX frame
================
*/
void RTX_EndFrame(void) {
    if (!rtx.available) {
        return;
    }
    
    // Wait for GPU completion if needed
    RTX_WaitForCompletion();
}

/*
================
RTX_WaitForCompletion

Wait for GPU ray tracing to complete
================
*/
void RTX_WaitForCompletion(void) {
    // Platform-specific GPU sync
}

// ============================================================================
// Debug
// ============================================================================

/*
================
RTX_DrawDebugOverlay

Draw RTX debug information
================
*/
void RTX_DrawDebugOverlay(void) {
    if (!rtx_debug->integer || !rtx.available) {
        return;
    }
    
    // Debug overlay drawing would go here
    // Currently disabled until proper text drawing API is available
    
    // Stats to display:
    // - RTX Status (Active/Inactive)
    // - Ray Tracing Tier
    // - BLAS Count
    // - TLAS Instances
    // - Build Time
    // - Trace Time
    // - Denoise Time
    // - DLSS Mode
}

/*
================
RTX_DumpStats

Dump RTX statistics to console
================
*/
void RTX_DumpStats(void) {
    if (!rtx.available) {
        ri.Printf(PRINT_ALL, "RTX not available\n");
        return;
    }
    
    ri.Printf(PRINT_ALL, "=== RTX Statistics ===\n");
    ri.Printf(PRINT_ALL, "Ray Tracing Tier: %d\n", rtx.rayTracingTier);
    ri.Printf(PRINT_ALL, "Features: 0x%08X\n", rtx.features);
    ri.Printf(PRINT_ALL, "BLAS Count: %d/%d\n", rtx.numBLAS, rtx.maxBLAS);
    ri.Printf(PRINT_ALL, "TLAS Instances: %d\n", rtx.tlas.numInstances);
    ri.Printf(PRINT_ALL, "Frame Count: %d\n", rtx.frameCount);
    ri.Printf(PRINT_ALL, "Build Time: %.2fms\n", rtx.buildTime);
    ri.Printf(PRINT_ALL, "Trace Time: %.2fms\n", rtx.traceTime);
    ri.Printf(PRINT_ALL, "Denoise Time: %.2fms\n", rtx.denoiseTime);
    ri.Printf(PRINT_ALL, "=====================\n");
}

// ============================================================================
// Vulkan Implementation Hooks
// ============================================================================

// Forward declarations for Vulkan RT implementation
extern qboolean RTX_InitVulkanRT(void);
extern void RTX_ShutdownVulkanRT(void);
extern void RTX_BuildAccelerationStructureVK(void);
extern void RTX_DispatchRaysVK(const rtxDispatchRays_t *params);


// These functions are implemented in rt_rtx_impl.c

// ============================================================================
// DLSS Stubs
// ============================================================================

// DLSS functions are implemented in rt_rtx_dlss.c
extern qboolean RTX_InitDLSS(void);
extern void RTX_SetDLSSMode(dlssMode_t mode);
extern void RTX_UpscaleWithDLSS(void *input, void *output, int inputWidth, int inputHeight);
extern void RTX_ShutdownDLSS(void);


// ============================================================================
// Denoiser Stubs
// ============================================================================

// Denoiser functions are implemented in rt_rtx_denoiser.c
extern qboolean RTX_InitDenoiser(int width, int height);
extern void RTX_DenoiseFrame(void *input, void *output);
extern void RTX_ShutdownDenoiser(void);


// ============================================================================
// Console Commands
// ============================================================================

/*
================
RTX_Status_f

Console command to display RTX status
================
*/
void RTX_Status_f(void) {
    const char *gpuType = "Unknown";
    
    // Always show GPU info from glConfig first
    ri.Printf(PRINT_ALL, "\n==== GPU Information ====\n");
    ri.Printf(PRINT_ALL, "Vendor: %s\n", glConfig.vendor_string ? (const char*)glConfig.vendor_string : "Unknown");
    ri.Printf(PRINT_ALL, "Renderer: %s\n", glConfig.renderer_string ? (const char*)glConfig.renderer_string : "Unknown");
    
    switch (rtx.gpuType) {
    case RTX_GPU_NVIDIA:
        gpuType = "NVIDIA";
        break;
    case RTX_GPU_AMD:
        gpuType = "AMD";
        break;
    case RTX_GPU_INTEL:
        gpuType = "Intel";
        break;
    default:
        gpuType = "Not Detected";
        break;
    }
    
    ri.Printf(PRINT_ALL, "\n==== RTX Hardware Status ====\n");
    ri.Printf(PRINT_ALL, "RTX Available: %s\n", rtx.available ? "Yes" : "No");
    ri.Printf(PRINT_ALL, "RTX GPU Type: %s\n", gpuType);
    ri.Printf(PRINT_ALL, "Features:\n");
    
    if (rtx.features & RTX_FEATURE_RAY_TRACING) {
        ri.Printf(PRINT_ALL, "  [x] Ray Tracing\n");
    } else {
        ri.Printf(PRINT_ALL, "  [ ] Ray Tracing\n");
    }
    
    if (rtx.features & RTX_FEATURE_DENOISER) {
        ri.Printf(PRINT_ALL, "  [x] Hardware Denoiser\n");
    } else {
        ri.Printf(PRINT_ALL, "  [ ] Hardware Denoiser\n");
    }
    
    if (rtx.features & RTX_FEATURE_DLSS) {
        ri.Printf(PRINT_ALL, "  [x] DLSS\n");
    } else {
        ri.Printf(PRINT_ALL, "  [ ] DLSS\n");
    }
    
    if (rtx.features & RTX_FEATURE_REFLEX) {
        ri.Printf(PRINT_ALL, "  [x] NVIDIA Reflex\n");
    } else {
        ri.Printf(PRINT_ALL, "  [ ] NVIDIA Reflex\n");
    }
    
    ri.Printf(PRINT_ALL, "\nCVARs:\n");
    ri.Printf(PRINT_ALL, "  rtx_enable: %d\n", rtx_enable ? rtx_enable->integer : 0);
    ri.Printf(PRINT_ALL, "  rtx_quality: %d\n", rtx_quality ? rtx_quality->integer : 0);
    ri.Printf(PRINT_ALL, "  rtx_denoise: %d\n", rtx_denoise ? rtx_denoise->integer : 0);
    ri.Printf(PRINT_ALL, "  rtx_dlss: %d\n", rtx_dlss ? rtx_dlss->integer : 0);

    ri.Printf(PRINT_ALL, "\n==== Backend Diagnostics ====\n");
    ri.Printf(PRINT_ALL, "Backend Status: %s\n", RT_GetBackendStatus());
    ri.Printf(PRINT_ALL, "RTX Last Event: %s\n", RTX_GetLastStatus());
    ri.Printf(PRINT_ALL, "RTX Active: %s\n", (rt.useRTX && RTX_IsAvailable()) ? "Yes" : "No");
    ri.Printf(PRINT_ALL, "Scene Lights: %d (dynamic %d, static %d)\n",
              rt.numSceneLights, rt.numDynamicLights, rt.numStaticLights);
    ri.Printf(PRINT_ALL, "BLAS Count: %d / %d\n", rtx.numBLAS, rtx.maxBLAS);
    ri.Printf(PRINT_ALL, "TLAS Instances: %d\n", rtx.tlas.numInstances);
    ri.Printf(PRINT_ALL, "GPU Build Time: %.2f ms  Trace Time: %.2f ms  Denoise: %.2f ms\n",
              rtx.buildTime, rtx.traceTime, rtx.denoiseTime);
    ri.Printf(PRINT_ALL, "CPU Trace Time: %.2f ms\n", rt.traceTime);
    ri.Printf(PRINT_ALL, "Light Buffer State: %s\n",
              rt.sceneLightBufferDirty ? "Pending upload" : "Synced");

    if (!rtx.available) {
        ri.Printf(PRINT_ALL, "\nNote: To enable RTX, ensure you have:\n");
        ri.Printf(PRINT_ALL, "  - NVIDIA RTX 20xx/30xx/40xx series GPU\n");
        ri.Printf(PRINT_ALL, "  - AMD RX 6xxx/7xxx series GPU\n");
        ri.Printf(PRINT_ALL, "  - Intel Arc GPU\n");
        ri.Printf(PRINT_ALL, "  - Latest graphics drivers installed\n");
    }
    
    ri.Printf(PRINT_ALL, "=============================\n");
}
