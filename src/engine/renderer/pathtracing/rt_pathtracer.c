#include <limits.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "../core/tr_local.h"
#include "rt_rtx.h"
#include "../lighting/tr_light_dynamic.h"
#ifdef USE_VULKAN
#include "../vulkan/vk.h"
#endif

#ifdef USE_VULKAN
static void RT_DestroySceneLightBuffer(void);
#endif
static void RT_Status_f(void);

pathTracer_t rt;

cvar_t *rt_enable;
cvar_t *rt_mode;
cvar_t *rt_quality;
cvar_t *rt_bounces;
cvar_t *rt_samples;
cvar_t *rt_denoise;
cvar_t *rt_temporal;
cvar_t *r_rt_mode;
cvar_t *r_rt_backend;
cvar_t *rt_probes;
cvar_t *rt_cache;
cvar_t *rt_debug;
cvar_t *rt_staticLights;
cvar_t *rt_gpuValidate;

static qboolean rtBackendActive = qfalse;
static qboolean rtBackendInitFailureLogged = qfalse;
static qboolean rtBackendHardwareWarned = qfalse;
static char rtBackendLastChoice[MAX_QPATH] = "auto";
static int rtBackendLastEnableState = 0;
static int rtBackendLastTracerEnableState = 0;
static uint32_t g_seed = 1u;

#ifndef VectorDistance
static ID_INLINE float VectorDistance(const vec3_t p1, const vec3_t p2) {
    vec3_t delta;
    VectorSubtract(p2, p1, delta);
    return VectorLength(delta);
}
#endif

static float FastRandom(void) {
	g_seed = 1664525u * g_seed + 1013904223u;
	return (float)(g_seed & 0x00FFFFFFu) / 16777215.0f;
}

static float RT_SafeRadius( float radius ) {
	const float minRadius = 16.0f;
	const float maxRadius = 131072.0f;

	if ( radius < minRadius ) {
		return minRadius;
	}

	if ( radius > maxRadius ) {
		return maxRadius;
	}

	return radius;
}

static float RT_ComputeSpotCosFromFov( float fovDegrees ) {
	if ( fovDegrees <= 0.0f ) {
		return 1.0f;
	}

	float radians = DEG2RAD( 0.5f * fovDegrees );
	float cosTheta = cosf( radians );

	if ( cosTheta > 1.0f ) {
		cosTheta = 1.0f;
	} else if ( cosTheta < -1.0f ) {
		cosTheta = -1.0f;
	}

	return cosTheta;
}

static uint32_t RT_ComputeSceneLightHash( const rtSceneLight_t *lights, int count ) {
	if ( !lights || count <= 0 ) {
		return 0u;
	}

	return (uint32_t)Com_BlockChecksum( lights, count * (int)sizeof( rtSceneLight_t ) );
}

static void VectorLerp( const vec3_t from, const vec3_t to, float lerp, vec3_t out ) {
	out[0] = from[0] + ( to[0] - from[0] ) * lerp;
	out[1] = from[1] + ( to[1] - from[1] ) * lerp;
	out[2] = from[2] + ( to[2] - from[2] ) * lerp;
}

static char rtBackendStatusMessage[128] = "Software backend inactive";

const char *RT_GetBackendStatus(void) {
	return rtBackendStatusMessage;
}

void RT_SetBackendStatus( const char *fmt, ... ) {
	if ( !fmt || !fmt[0] ) {
		Q_strncpyz( rtBackendStatusMessage, "Software backend inactive", sizeof( rtBackendStatusMessage ) );
		return;
	}

	va_list args;
	va_start( args, fmt );
	Q_vsnprintf( rtBackendStatusMessage, sizeof( rtBackendStatusMessage ), fmt, args );
	va_end( args );
}

void RT_ResetBackendLogs( void ) {
	rtBackendInitFailureLogged = qfalse;
	rtBackendHardwareWarned = qfalse;
}

#define RT_BACKEND_INDEX_COMPUTE   0
#define RT_BACKEND_INDEX_HARDWARE  1
#define RT_BACKEND_RMSE_THRESHOLD  0.0025f

static const char *rtValidationMaps[] = {
    "q3dm1",
    "q3dm7",
    "q3dm17",
    NULL
};

static qboolean RT_MapIsValidationTarget(const char *mapName);
static void RT_GetValidationMapName(char *buffer, size_t bufferSize);
static void RT_RecordBackendValidation(const float *rgba, int width, int height, qboolean validated);
static void RT_ReportBackendParity(void);

#ifdef USE_VULKAN
static void RT_DestroySceneLightBuffer(void);
static uint32_t rtLastUploadedLightHash = 0u;
#endif

static void RT_SelectBackend(void) {
    const char *backendStr = r_rt_backend ? r_rt_backend->string : "auto";
    if (!backendStr || !backendStr[0]) {
        backendStr = "auto";
    }

    if (r_rt_backend && r_rt_backend->modified) {
        ri.Printf(PRINT_ALL, "r_rt_backend set to '%s'\n", backendStr);
        r_rt_backend->modified = qfalse;
        RT_ResetBackendLogs();
    }
#ifdef USE_VULKAN
    if (rtx_enable && rtx_enable->modified) {
        ri.Printf(PRINT_ALL, "rtx_enable set to %d\n", rtx_enable->integer);
        rtx_enable->modified = qfalse;
        RT_ResetBackendLogs();
    }
#endif

    if (Q_stricmp(backendStr, rtBackendLastChoice) != 0) {
        Q_strncpyz(rtBackendLastChoice, backendStr, sizeof(rtBackendLastChoice));
        RT_ResetBackendLogs();
    }

    int enableState = ri.Cvar_VariableIntegerValue("rtx_enable");
    if (enableState != rtBackendLastEnableState) {
        rtBackendLastEnableState = enableState;
        RT_ResetBackendLogs();
    }

    qboolean tracerEnabled = (rt_enable && rt_enable->integer) ? qtrue : qfalse;
    int tracerState = tracerEnabled ? 1 : 0;
    if (tracerState != rtBackendLastTracerEnableState) {
        rtBackendLastTracerEnableState = tracerState;
        RT_ResetBackendLogs();
    }

    if (rtBackendActive && rt.useRTX) {
        RT_SetBackendStatus("RTX hardware backend active (%s)", backendStr);
    }

    qboolean forceHardware = !Q_stricmp(backendStr, "hardware");
    qboolean wantHardware = tracerEnabled && (enableState != 0) &&
        (forceHardware || !Q_stricmp(backendStr, "auto"));

    if (!wantHardware) {
        if (rtBackendActive) {
            RT_ShutdownBackend();
        } else {
            rt.useRTX = qfalse;
            RT_SetBackendStatus("Software backend active (backend=%s)", backendStr);
        }
        return;
    }

#ifdef USE_VULKAN
    if (RTX_IsAvailable()) {
        if (!rtBackendActive) {
            ri.Printf(PRINT_ALL, "RTX hardware backend enabled\n");
            if (tr.world) {
                if (rtx.numBLAS == 0 || rtx.tlas.numInstances == 0) {
                    RTX_PopulateWorld();
                } else {
                    RTX_RequestWorldRefit();
                }
            }
            rt.sceneLightBufferDirty = qtrue;
            RT_SetBackendStatus("RTX hardware backend active (%s)", backendStr);
        }
        rtBackendActive = qtrue;
        rt.useRTX = qtrue;
        RT_ResetBackendLogs();
        return;
    }

    if (!rtBackendInitFailureLogged) {
        if (RTX_Init()) {
            if (RTX_IsAvailable()) {
                if (!rtBackendActive) {
                    ri.Printf(PRINT_ALL, "RTX hardware backend enabled\n");
                    if (tr.world) {
                        if (rtx.numBLAS == 0 || rtx.tlas.numInstances == 0) {
                            RTX_PopulateWorld();
                        } else {
                            RTX_RequestWorldRefit();
                        }
                    }
                    rt.sceneLightBufferDirty = qtrue;
                    RT_SetBackendStatus("RTX hardware backend active (%s)", backendStr);
                }
                rtBackendActive = qtrue;
                rt.useRTX = qtrue;
                RT_ResetBackendLogs();
                return;
            }
        } else {
            rtBackendInitFailureLogged = qtrue;
        }
    }
#else
    if (!rtBackendHardwareWarned) {
        ri.Printf(PRINT_WARNING, "RTX: hardware backend requested but Vulkan RTX is not available in this build; using software path\n");
        rtBackendHardwareWarned = qtrue;
    }
#endif

    if (rtBackendActive) {
        RT_ShutdownBackend();
    } else {
        rt.useRTX = qfalse;
        RT_SetBackendStatus("Software backend active (RTX unavailable)");
    }

#ifdef USE_VULKAN
    if (forceHardware && !rtBackendHardwareWarned) {
        ri.Printf(PRINT_WARNING, "RTX: hardware backend requested but RTX initialization failed; using software path\n");
        rtBackendHardwareWarned = qtrue;
    }
#endif
}

static qboolean RT_MapIsValidationTarget(const char *mapName) {
    if (!mapName || !mapName[0]) {
        return qfalse;
    }

    for (int i = 0; rtValidationMaps[i]; ++i) {
        if (!Q_stricmp(mapName, rtValidationMaps[i])) {
            return qtrue;
        }
    }

    return qfalse;
}

static void RT_GetValidationMapName(char *buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) {
        return;
    }

    buffer[0] = '\0';

    if (!tr.world || !tr.world->name[0]) {
        Q_strncpyz(buffer, "unknown", bufferSize);
        return;
    }

    const char *name = tr.world->name;
    const char *slash = strrchr(name, '/');
    const char *base = slash ? slash + 1 : name;

    Q_strncpyz(buffer, base, bufferSize);

    char *dot = strrchr(buffer, '.');
    if (dot) {
        *dot = '\0';
    }
}

static void RT_RecordBackendValidation(const float *rgba, int width, int height, qboolean validated) {
    if (!rt_gpuValidate || rt_gpuValidate->integer <= 0) {
        rt.backendValidation[RT_BACKEND_INDEX_COMPUTE].valid = qfalse;
        rt.backendValidation[RT_BACKEND_INDEX_HARDWARE].valid = qfalse;
        rt.backendRMSEDelta = 0.0;
        rt.backendMaxErrorDelta = 0.0;
        rt.backendParityFrame = 0;
        rt.backendParityMap[0] = '\0';
        return;
    }

    if (!rgba || width <= 0 || height <= 0) {
        return;
    }

    char mapName[MAX_QPATH];
    RT_GetValidationMapName(mapName, sizeof(mapName));

    if (!RT_MapIsValidationTarget(mapName)) {
        return;
    }

#ifdef USE_VULKAN
    qboolean hardwareActive = (rt.useRTX && RTX_IsAvailable());
#else
    qboolean hardwareActive = qfalse;
#endif

    int backendIndex = hardwareActive ? RT_BACKEND_INDEX_HARDWARE : RT_BACKEND_INDEX_COMPUTE;
    rtBackendValidation_t *entry = &rt.backendValidation[backendIndex];

    entry->hardware = hardwareActive;
    entry->valid = validated;
    Q_strncpyz(entry->map, mapName, sizeof(entry->map));
    entry->width = width;
    entry->height = height;
    entry->frame = rt.currentFrame;
    entry->samples = validated ? rt.validationSamples : 0;
    entry->rmse = validated ? rt.validationRMSE : 0.0;
    entry->maxError = validated ? rt.validationMaxError : 0.0;

    size_t pixelCount = (size_t)width * (size_t)height;
    size_t byteCount = pixelCount * 4 * sizeof(float);
    if (byteCount > (size_t)INT_MAX) {
        byteCount = (size_t)INT_MAX;
    }
    entry->hash = Com_BlockChecksum(rgba, (int)byteCount);

    if (entry->valid) {
        RT_ReportBackendParity();
    }
}

static void RT_ReportBackendParity(void) {
    rtBackendValidation_t *hardware = &rt.backendValidation[RT_BACKEND_INDEX_HARDWARE];
    rtBackendValidation_t *compute = &rt.backendValidation[RT_BACKEND_INDEX_COMPUTE];

    if (!hardware->valid || !compute->valid) {
        return;
    }

    if (Q_stricmp(hardware->map, compute->map) != 0) {
        return;
    }

    if (hardware->width != compute->width || hardware->height != compute->height) {
        return;
    }

    int newestFrame = (hardware->frame > compute->frame) ? hardware->frame : compute->frame;

    if (newestFrame == rt.backendParityFrame &&
        !Q_stricmp(rt.backendParityMap, hardware->map)) {
        return;
    }

    double rmseDelta = hardware->rmse - compute->rmse;
    double maxDelta = hardware->maxError - compute->maxError;

    rt.backendParityFrame = newestFrame;
    rt.backendRMSEDelta = rmseDelta;
    rt.backendMaxErrorDelta = maxDelta;
    Q_strncpyz(rt.backendParityMap, hardware->map, sizeof(rt.backendParityMap));

    if (rmseDelta > RT_BACKEND_RMSE_THRESHOLD) {
        ri.Printf(PRINT_WARNING,
                  "rt_gpuValidate: Hardware backend RMSE regression on %s (HW=%.5f, Compute=%.5f, Δ=%.5f)\n",
                  hardware->map,
                  (float)hardware->rmse,
                  (float)compute->rmse,
                  (float)rmseDelta);
    } else {
        ri.Printf(PRINT_DEVELOPER,
                  "rt_gpuValidate: Backend parity on %s (HW=%.5f, Compute=%.5f, Δ=%.5f, maxΔ=%.5f)\n",
                  hardware->map,
                  (float)hardware->rmse,
                  (float)compute->rmse,
                  (float)rmseDelta,
                  (float)maxDelta);
    }
}

static qboolean RT_TraceShadowRaySoftware(const vec3_t origin, const vec3_t direction, float maxDist) {
    ray_t ray;
    hitInfo_t hit;

    VectorCopy(origin, ray.origin);
    VectorCopy(direction, ray.direction);
    ray.tMin = 0.001f;
    ray.tMax = maxDist;

    return RT_TraceRay(&ray, &hit);
}

#ifdef USE_VULKAN
static VkDeviceSize RT_GetSceneLightCapacity(void) {
    return (VkDeviceSize)sizeof(rtxLightGpu_t) * (VkDeviceSize)RT_MAX_SCENE_LIGHTS;
}

static void RT_InitSceneLightBuffer(void) {
    if (!vk.device || !vk.physical_device) {
        return;
    }

    if (rt.sceneLightBuffer != VK_NULL_HANDLE && rt.sceneLightBufferMemory != VK_NULL_HANDLE) {
        return;
    }

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .size = RT_GetSceneLightCapacity(),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL
    };

    if (vkCreateBuffer(vk.device, &bufferInfo, NULL, &rt.sceneLightBuffer) != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RT_InitSceneLightBuffer: failed to create buffer of size %llu\n",
                  (unsigned long long)bufferInfo.size);
        rt.sceneLightBuffer = VK_NULL_HANDLE;
        rt.sceneLightBufferMemory = VK_NULL_HANDLE;
        rt.sceneLightBufferSize = 0;
        return;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(vk.device, rt.sceneLightBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = vk_find_memory_type(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };

    if (vkAllocateMemory(vk.device, &allocInfo, NULL, &rt.sceneLightBufferMemory) != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RT_InitSceneLightBuffer: failed to allocate %llu bytes for scene lights\n",
                  (unsigned long long)allocInfo.allocationSize);
        vkDestroyBuffer(vk.device, rt.sceneLightBuffer, NULL);
        rt.sceneLightBuffer = VK_NULL_HANDLE;
        rt.sceneLightBufferMemory = VK_NULL_HANDLE;
        rt.sceneLightBufferSize = 0;
        return;
    }

    if (vkBindBufferMemory(vk.device, rt.sceneLightBuffer, rt.sceneLightBufferMemory, 0) != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RT_InitSceneLightBuffer: vkBindBufferMemory failed\n");
        vkFreeMemory(vk.device, rt.sceneLightBufferMemory, NULL);
        vkDestroyBuffer(vk.device, rt.sceneLightBuffer, NULL);
        rt.sceneLightBuffer = VK_NULL_HANDLE;
        rt.sceneLightBufferMemory = VK_NULL_HANDLE;
        rt.sceneLightBufferSize = 0;
        return;
    }

    rt.sceneLightBufferSize = bufferInfo.size;
    rt.sceneLightBufferDirty = qtrue;
}

static void RT_DestroySceneLightBuffer(void) {
    if (!vk.device) {
        return;
    }

    if (rt.sceneLightBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk.device, rt.sceneLightBuffer, NULL);
        rt.sceneLightBuffer = VK_NULL_HANDLE;
    }

    if (rt.sceneLightBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vk.device, rt.sceneLightBufferMemory, NULL);
        rt.sceneLightBufferMemory = VK_NULL_HANDLE;
    }

    rt.sceneLightBufferSize = 0;
    rt.sceneLightBufferDirty = qtrue;
    rtLastUploadedLightHash = 0u;
}

VkBuffer RT_GetSceneLightBuffer(void) {
    return rt.sceneLightBuffer;
}

VkDeviceSize RT_GetSceneLightBufferSize(void) {
    VkDeviceSize count = (VkDeviceSize)(rt.numSceneLights > 0 ? rt.numSceneLights : 1);
    VkDeviceSize desired = count * (VkDeviceSize)sizeof(rtxLightGpu_t);
    if (rt.sceneLightBufferSize > 0 && desired > rt.sceneLightBufferSize) {
        desired = rt.sceneLightBufferSize;
    }
    return desired;
}

static void RT_FillGpuLight(const rtSceneLight_t *src, rtxLightGpu_t *dst) {
    vec3_t dir;
    VectorCopy(src->direction, dir);
    if (VectorNormalize(dir) <= 0.0f) {
        VectorSet(dir, 0.0f, 0.0f, -1.0f);
    }

    dst->position[0] = src->origin[0];
    dst->position[1] = src->origin[1];
    dst->position[2] = src->origin[2];
    dst->position[3] = src->radius;

    dst->direction[0] = dir[0];
    dst->direction[1] = dir[1];
    dst->direction[2] = dir[2];
    dst->direction[3] = src->spotCos;

    float intensity = (src->intensity > 0.0f) ? src->intensity : 1.0f;
    dst->color[0] = src->color[0] * intensity;
    dst->color[1] = src->color[1] * intensity;
    dst->color[2] = src->color[2] * intensity;
    dst->color[3] = (float)src->type;

    float safeRadius = (src->radius > 0.0f) ? src->radius : 1.0f;
    dst->attenuation[0] = 1.0f / safeRadius;
    dst->attenuation[1] = src->castsShadows ? 1.0f : 0.0f;
    dst->attenuation[2] = src->isStatic ? 1.0f : 0.0f;
    dst->attenuation[3] = intensity;
}

void RT_UpdateSceneLightBuffer(void) {
    if (!vk.device) {
        return;
    }

    if (rt.sceneLightBuffer == VK_NULL_HANDLE || rt.sceneLightBufferMemory == VK_NULL_HANDLE) {
        RT_InitSceneLightBuffer();
        if (rt.sceneLightBuffer == VK_NULL_HANDLE || rt.sceneLightBufferMemory == VK_NULL_HANDLE) {
            return;
        }
    }

    if (!rt.sceneLightBufferDirty && rtLastUploadedLightHash == rt.sceneLightHash) {
        return;
    }

    size_t lightCount = (size_t)(rt.numSceneLights > 0 ? rt.numSceneLights : 0);
    size_t uploadCount = lightCount > 0 ? lightCount : 1;
    size_t uploadBytes = uploadCount * sizeof(rtxLightGpu_t);

    if (uploadBytes > (size_t)rt.sceneLightBufferSize && rt.sceneLightBufferSize > 0) {
        uploadBytes = (size_t)rt.sceneLightBufferSize;
        uploadCount = uploadBytes / sizeof(rtxLightGpu_t);
        if (uploadCount == 0) {
            uploadCount = 1;
            uploadBytes = sizeof(rtxLightGpu_t);
        }
    }

    rtxLightGpu_t gpuLights[RT_MAX_SCENE_LIGHTS];
    Com_Memset(gpuLights, 0, sizeof(gpuLights));

    for (size_t i = 0; i < lightCount && i < RT_MAX_SCENE_LIGHTS; ++i) {
        RT_FillGpuLight(&rt.sceneLights[i], &gpuLights[i]);
    }

    void *mapped = NULL;
    VkResult result = vkMapMemory(vk.device, rt.sceneLightBufferMemory, 0, uploadBytes, 0, &mapped);
    if (result != VK_SUCCESS || !mapped) {
        ri.Printf(PRINT_WARNING, "RT_UpdateSceneLightBuffer: vkMapMemory failed (%d)\n", result);
        return;
    }

    Com_Memcpy(mapped, gpuLights, uploadBytes);
    vkUnmapMemory(vk.device, rt.sceneLightBufferMemory);

    rt.sceneLightBufferDirty = qfalse;
    rtLastUploadedLightHash = rt.sceneLightHash;
}
#endif

/*
===============
RT_InitPathTracer

Initialize the path tracing system
===============
*/
void RT_InitPathTracer(void) {
    Com_Memset(&rt, 0, sizeof(rt));
    RT_SetBackendStatus("Software backend initialising");
    
    // Register CVARs
    rt_enable = ri.Cvar_Get("rt_enable", "0", CVAR_ARCHIVE);
    rt_mode = ri.Cvar_Get("rt_mode", "dynamic", CVAR_ARCHIVE);
    rt_quality = ri.Cvar_Get("rt_quality", "2", CVAR_ARCHIVE);
    rt_bounces = ri.Cvar_Get("rt_bounces", "2", CVAR_ARCHIVE);
    rt_samples = ri.Cvar_Get("rt_samples", "1", CVAR_ARCHIVE);
    rt_denoise = ri.Cvar_Get("rt_denoise", "1", CVAR_ARCHIVE);
    rt_temporal = ri.Cvar_Get("rt_temporal", "1", CVAR_ARCHIVE);
    r_rt_backend = ri.Cvar_Get("r_rt_backend", "auto", CVAR_ARCHIVE);
    rt_probes = ri.Cvar_Get("rt_probes", "1", CVAR_ARCHIVE);
    rt_cache = ri.Cvar_Get("rt_cache", "1", CVAR_ARCHIVE);
    rt_debug = ri.Cvar_Get("rt_debug", "0", CVAR_CHEAT);
    rt_staticLights = ri.Cvar_Get("rt_staticLights", "1", CVAR_ARCHIVE);
    rt_gpuValidate = ri.Cvar_Get("rt_gpuValidate", "0", CVAR_ARCHIVE);
    r_rt_mode = rt_mode;
    
    ri.Cvar_SetDescription(rt_mode, "Path tracing mode: 'off', 'dynamic', or 'all'");
    ri.Cvar_SetDescription(r_rt_backend, "Ray tracing backend: 'auto', 'hardware', or 'software'");
    ri.Cvar_SetDescription(rt_gpuValidate, "Frame validation stride for CPU reference and backend parity checks (0 disables validation).");
    
    // Set default quality
    rt.quality = RT_QUALITY_MEDIUM;
    rt.mode = RT_MODE_DYNAMIC;
    rt.maxBounces = 2;
    rt.samplesPerPixel = 1;
    rt.enabled = qfalse;
    rt.useRTX = qfalse;
    
    // Parse mode CVAR
    if (!Q_stricmp(rt_mode->string, "all")) {
        rt.mode = RT_MODE_ALL;
    } else if (!Q_stricmp(rt_mode->string, "dynamic")) {
        rt.mode = RT_MODE_DYNAMIC;
    } else {
        rt.mode = RT_MODE_OFF;
    }
    
    // Allocate static light storage
    rt.maxStaticLights = RT_MAX_STATIC_LIGHTS;
    rt.staticLights = ri.Hunk_Alloc(sizeof(staticLight_t) * rt.maxStaticLights, h_low);
    rt.numStaticLights = 0;
    rt.numDynamicLights = 0;
    rt.numSceneLights = 0;
    rt.sceneLightHash = 0;
    rt.temporalWidth = 0;
    rt.temporalHeight = 0;
    rt.temporalEnabled = qtrue;
    
    // Initialize random seed
    g_seed = ri.Milliseconds();
    
    // Register console command
    ri.Cmd_AddCommand("rt_status", RT_Status_f);

#ifdef USE_VULKAN
    RT_InitSceneLightBuffer();
    RT_UpdateSceneLightBuffer();
#endif

    RT_InitDenoiser();

    RT_SelectBackend();
    
    ri.Printf(PRINT_ALL, "Path tracer initialized (mode: %s, backend: %s)\n",
        rt_mode->string, (rt.useRTX ? "RTX Hardware" : "Software"));
}

void RT_ShutdownBackend(void) {
#ifdef USE_VULKAN
    if (rtBackendActive) {
        ri.Printf(PRINT_ALL, "RTX hardware backend disabled\n");
    }
    RTX_Shutdown();
#endif
    rtBackendActive = qfalse;
    rt.useRTX = qfalse;
    RT_ResetBackendLogs();
    rt.sceneLightBufferDirty = qtrue;
    RT_SetBackendStatus("Software backend active");
}

/*
===============
RT_ShutdownPathTracer

Shutdown and free resources
===============
*/
void RT_ShutdownPathTracer(void) {
    RT_ShutdownBackend();

#ifdef USE_VULKAN
    RT_DestroySceneLightBuffer();
#endif

    if (rt.lightCache) {
        ri.Free(rt.lightCache);
        rt.lightCache = NULL;
    }
    
    if (rt.probes) {
        ri.Free(rt.probes);
        rt.probes = NULL;
    }
    
    if (rt.accumBuffer) {
        ri.Free(rt.accumBuffer);
        rt.accumBuffer = NULL;
    }

    if (rt.varianceBuffer) {
        ri.Free(rt.varianceBuffer);
        rt.varianceBuffer = NULL;
    }

    if (rt.sampleBuffer) {
        ri.Free(rt.sampleBuffer);
        rt.sampleBuffer = NULL;
    }

    if (rt.denoisedBuffer) {
        ri.Free(rt.denoisedBuffer);
        rt.denoisedBuffer = NULL;
    }
}

#ifdef USE_VULKAN
void RT_RecordBackendCommands(VkCommandBuffer cmd) {
    if (!cmd) {
        return;
    }

    if (!rt_enable || !rt_enable->integer) {
        return;
    }

    if (!rtBackendActive || !rt.useRTX) {
        return;
    }

    if (!RTX_IsAvailable()) {
        return;
    }

    RTX_RecordCommands(cmd);
}

void RT_ApplyBackendDebugOverlay(VkCommandBuffer cmd, VkImage colorImage) {
    if (!cmd) {
        return;
    }

    if (!rtBackendActive || !rt.useRTX) {
        return;
    }

    if (!rtx_debug || rtx_debug->integer <= 0) {
        return;
    }

    if (!RTX_IsAvailable()) {
        return;
    }

    RTX_ApplyDebugOverlayCompute(cmd, colorImage);
}
#endif

/*
===============
RT_BuildAccelerationStructure

Build BSP acceleration structure for ray tracing
Uses existing BSP tree with additional optimization
===============
*/
void RT_BuildAccelerationStructure(void) {
    if (!tr.world) {
        return;
    }
    
    // We'll use the existing BSP tree directly
    // No need to build a separate structure
    rt.bspTree = (rtBspNode_t *)tr.world->nodes;
    rt.numNodes = tr.world->numnodes;
    
    // Allocate static light array if needed
    if (!rt.staticLights) {
        rt.maxStaticLights = RT_MAX_STATIC_LIGHTS;
        rt.staticLights = ri.Hunk_Alloc(sizeof(staticLight_t) * rt.maxStaticLights, h_low);
    }
    
    // Extract static lights if mode is set to all
    const char *modeStr = rt_mode ? rt_mode->string : "dynamic";
    if (!Q_stricmp(modeStr, "all")) {
        RT_ExtractStaticLights();
    }
    
    // Initialize light cache
    RT_InitLightCache();
    
    // Initialize probe grid if enabled
    if (rt_probes && rt_probes->integer) {
        vec3_t worldMins, worldMaxs;
        VectorCopy(tr.world->nodes[0].mins, worldMins);
        VectorCopy(tr.world->nodes[0].maxs, worldMaxs);
        RT_InitProbeGrid(worldMins, worldMaxs);
    }
}

/*
===============
RT_RayTriangleIntersect

Möller-Trumbore ray-triangle intersection
Optimized for SSE when available
===============
*/
qboolean RT_RayTriangleIntersect(const ray_t *ray, const vec3_t v0, const vec3_t v1, const vec3_t v2, float *t, vec2_t *uv) {
    vec3_t edge1, edge2, h, s, q;
    float a, f, u, v;
    
    VectorSubtract(v1, v0, edge1);
    VectorSubtract(v2, v0, edge2);
    
    CrossProduct(ray->direction, edge2, h);
    a = DotProduct(edge1, h);
    
    if (a > -0.00001f && a < 0.00001f) {
        return qfalse;
    }
    
    f = 1.0f / a;
    VectorSubtract(ray->origin, v0, s);
    u = f * DotProduct(s, h);
    
    if (u < 0.0f || u > 1.0f) {
        return qfalse;
    }
    
    CrossProduct(s, edge1, q);
    v = f * DotProduct(ray->direction, q);
    
    if (v < 0.0f || u + v > 1.0f) {
        return qfalse;
    }
    
    float rayT = f * DotProduct(edge2, q);
    
    if (rayT > ray->tMin && rayT < ray->tMax) {
        *t = rayT;
        if (uv) {
            uv[0][0] = u;
            uv[0][1] = v;
        }
        return qtrue;
    }
    
    return qfalse;
}

/*
===============
RT_RayBoxIntersect

Fast ray-AABB intersection using slab method
===============
*/
qboolean RT_RayBoxIntersect(const ray_t *ray, const vec3_t mins, const vec3_t maxs, float *tMin, float *tMax) {
    float t1, t2;
    float tNear = ray->tMin;
    float tFar = ray->tMax;
    
    for (int i = 0; i < 3; i++) {
        if (fabs(ray->direction[i]) < 0.00001f) {
            if (ray->origin[i] < mins[i] || ray->origin[i] > maxs[i]) {
                return qfalse;
            }
        } else {
            t1 = (mins[i] - ray->origin[i]) / ray->direction[i];
            t2 = (maxs[i] - ray->origin[i]) / ray->direction[i];
            
            if (t1 > t2) {
                float temp = t1;
                t1 = t2;
                t2 = temp;
            }
            
            if (t1 > tNear) tNear = t1;
            if (t2 < tFar) tFar = t2;
            
            if (tNear > tFar || tFar < 0) {
                return qfalse;
            }
        }
    }
    
    if (tMin) *tMin = tNear;
    if (tMax) *tMax = tFar;
    
    return qtrue;
}

/*
===============
RT_TraceSurface

Test ray against a surface (triangle mesh or patch)
===============
*/
static qboolean RT_TraceSurface(const ray_t *ray, msurface_t *surf, hitInfo_t *hit) {
    if (!surf || !surf->data) {
        return qfalse;
    }
    
    surfaceType_t *surface = surf->data;
    
    switch (*surface) {
    case SF_FACE: {
        srfSurfaceFace_t *face = (srfSurfaceFace_t *)surface;
        
        // Test all triangles in the face
        int *indices = (int *)((byte *)face + face->ofsIndices);
        
        for (int i = 0; i < face->numIndices; i += 3) {
            vec3_t v0, v1, v2;
            VectorCopy(face->points[indices[i]], v0);
            VectorCopy(face->points[indices[i+1]], v1);
            VectorCopy(face->points[indices[i+2]], v2);
            
            float t;
            vec2_t uv;
            if (RT_RayTriangleIntersect(ray, v0, v1, v2, &t, &uv)) {
                if (t < hit->t) {
                    hit->t = t;
                    VectorMA(ray->origin, t, ray->direction, hit->point);
                    
                    // Calculate normal
                    vec3_t edge1, edge2;
                    VectorSubtract(v1, v0, edge1);
                    VectorSubtract(v2, v0, edge2);
                    CrossProduct(edge1, edge2, hit->normal);
                    VectorNormalize(hit->normal);
                    
                    hit->shader = surf->shader;
                    hit->uv[0] = uv[0];
                    hit->uv[1] = uv[1];
                    
                    return qtrue;
                }
            }
        }
        break;
    }
    
    case SF_TRIANGLES: {
        srfTriangles_t *tris = (srfTriangles_t *)surface;
        
        // Quick bounds check
        float tMin, tMax;
        if (!RT_RayBoxIntersect(ray, tris->bounds[0], tris->bounds[1], &tMin, &tMax)) {
            return qfalse;
        }
        
        // Test all triangles
        for (int i = 0; i < tris->numIndexes; i += 3) {
            vec3_t v0, v1, v2;
            VectorCopy(tris->verts[tris->indexes[i]].xyz, v0);
            VectorCopy(tris->verts[tris->indexes[i+1]].xyz, v1);
            VectorCopy(tris->verts[tris->indexes[i+2]].xyz, v2);
            
            float t;
            vec2_t uv;
            if (RT_RayTriangleIntersect(ray, v0, v1, v2, &t, &uv)) {
                if (t < hit->t) {
                    hit->t = t;
                    VectorMA(ray->origin, t, ray->direction, hit->point);
                    
                    // Interpolate normal from vertices
                    vec3_t n0, n1, n2;
                    VectorCopy(tris->verts[tris->indexes[i]].normal, n0);
                    VectorCopy(tris->verts[tris->indexes[i+1]].normal, n1);
                    VectorCopy(tris->verts[tris->indexes[i+2]].normal, n2);
                    
                    hit->normal[0] = n0[0] * (1-uv[0]-uv[1]) + n1[0] * uv[0] + n2[0] * uv[1];
                    hit->normal[1] = n0[1] * (1-uv[0]-uv[1]) + n1[1] * uv[0] + n2[1] * uv[1];
                    hit->normal[2] = n0[2] * (1-uv[0]-uv[1]) + n1[2] * uv[0] + n2[2] * uv[1];
                    VectorNormalize(hit->normal);
                    
                    hit->shader = surf->shader;
                    hit->uv[0] = uv[0];
                    hit->uv[1] = uv[1];
                    
                    return qtrue;
                }
            }
        }
        break;
    }
    
    case SF_GRID: {
        // Grid meshes would need special handling
        // For now, skip them in path tracing
        break;
    }
    }
    
    return qfalse;
}

/*
===============
RT_TraceBSPNode

Traverse BSP tree to find ray intersection
Optimized for cache coherency
===============
*/
static qboolean RT_TraceBSPNode(const ray_t *ray, int nodeNum, hitInfo_t *hit) {
    if (nodeNum < 0) {
        // Leaf node - we're done
        return qfalse;
    }
    
    mnode_t *node = &tr.world->nodes[nodeNum];
    
    // Quick AABB test
    float tMin, tMax;
    if (!RT_RayBoxIntersect(ray, node->mins, node->maxs, &tMin, &tMax)) {
        return qfalse;
    }
    
    // If this is a leaf (contents != -1), test surfaces
    if (node->contents != -1) {
        return qfalse; // Actual leaf
    }
    
    // Calculate distance to splitting plane
    cplane_t *plane = node->plane;
    float d1 = DotProduct(ray->origin, plane->normal) - plane->dist;
    float d2 = DotProduct(ray->direction, plane->normal);
    
    qboolean hitFound = qfalse;
    
    // Determine which side(s) to traverse
    if (fabs(d2) < 0.00001f) {
        // Ray parallel to plane
        int side = (d1 >= 0) ? 0 : 1;
        hitFound = RT_TraceBSPNode(ray, node->children[side]->contents, hit);
    } else {
        // Calculate intersection point with plane
        float t = -d1 / d2;
        
        // Determine traversal order
        int nearSide = (d1 >= 0) ? 0 : 1;
        int farSide = 1 - nearSide;
        
        // Always check near side
        hitFound = RT_TraceBSPNode(ray, node->children[nearSide]->contents, hit);
        
        // Check far side if needed
        if (t > 0 && t < hit->t) {
            qboolean farHit = RT_TraceBSPNode(ray, node->children[farSide]->contents, hit);
            hitFound = hitFound || farHit;
        }
    }
    
    // Test surfaces at this node
    for (int i = 0; i < node->nummarksurfaces; i++) {
        msurface_t *surf = node->firstmarksurface[i];
        if (RT_TraceSurface(ray, surf, hit)) {
            hitFound = qtrue;
        }
    }
    
    return hitFound;
}

/*
===============
RT_TraceRay

Main ray tracing function
===============
*/
qboolean RT_TraceRay(const ray_t *ray, hitInfo_t *hit) {
    if (!tr.world) {
        return qfalse;
    }
    
    // Increment ray counter for statistics
    rt.raysTraced++;
    
    // Initialize hit info
    hit->t = ray->tMax;
    hit->shader = NULL;
    
    // Use RTX hardware acceleration if available
    if (rt.useRTX && RTX_IsAvailable()) {
        RTX_AcceleratePathTracing(ray, hit);
        if (hit->shader) {
            return qtrue;
        }
    }
    
    // Fallback to software BSP traversal
    return RT_TraceBSPNode(ray, 0, hit);
}

/*
===============
RT_TraceShadowRay

Fast shadow ray test - early exit on any hit
===============
*/
qboolean RT_TraceShadowRay(const vec3_t origin, const vec3_t target, float maxDist) {
    // Use RTX hardware shadow query if available
    if (RTX_IsAvailable()) {
        float visibility = 0.0f;
        RTX_ShadowRayQuery(origin, target, &visibility);
        return (visibility < 1.0f);
    }

    if (RTX_RayQuerySupported()) {
        rtShadowQuery_t query;
        VectorCopy(origin, query.origin);
        vec3_t dir;
        VectorSubtract(target, origin, dir);
        float dist = VectorNormalize(dir);
        if (dist <= 0.0f) {
            return qfalse;
        }
        VectorCopy(dir, query.direction);
        query.maxDistance = maxDist > 0.0f ? maxDist : dist;
        query.occluded = qfalse;
        if (RTX_DispatchShadowQueries(&query, 1)) {
            return query.occluded;
        }
    }
    
    // Fallback to software implementation
    vec3_t dir;
    VectorSubtract(target, origin, dir);
    VectorNormalize(dir);

    return RT_TraceShadowRaySoftware(origin, dir, maxDist);
}

/*
===============
RT_EvaluateBRDF

Evaluate Cook-Torrance BRDF for physically-based shading
Optimized with approximations for real-time performance
===============
*/
void RT_EvaluateBRDF(const vec3_t wi, const vec3_t wo, const vec3_t normal, 
                     const vec3_t albedo, float roughness, float metallic, vec3_t result) {
    // Simplified BRDF for performance
    float NdotL = DotProduct(normal, wi);
    float NdotV = DotProduct(normal, wo);
    
    if (NdotL <= 0 || NdotV <= 0) {
        VectorClear(result);
        return;
    }
    
    // Half vector
    vec3_t H;
    VectorAdd(wi, wo, H);
    VectorNormalize(H);
    
    float NdotH = DotProduct(normal, H);
    float VdotH = DotProduct(wo, H);
    
    // Fresnel (Schlick approximation)
    vec3_t F0;
    if (metallic > 0.5f) {
        VectorCopy(albedo, F0);
    } else {
        VectorSet(F0, 0.04f, 0.04f, 0.04f);
    }
    
    float fresnel = F0[0] + (1.0f - F0[0]) * pow(1.0f - VdotH, 5.0f);
    
    // Distribution (GGX)
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (alpha2 - 1.0f) + 1.0f;
    float D = alpha2 / (M_PI * denom * denom);
    
    // Geometry (Smith)
    float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    float G1L = NdotL / (NdotL * (1.0f - k) + k);
    float G1V = NdotV / (NdotV * (1.0f - k) + k);
    float G = G1L * G1V;
    
    // Combine terms
    float specular = (D * G * fresnel) / (4.0f * NdotL * NdotV + 0.001f);
    float diffuse = (1.0f - fresnel) * (1.0f - metallic) / M_PI;
    
    result[0] = albedo[0] * (diffuse + specular) * NdotL;
    result[1] = albedo[1] * (diffuse + specular) * NdotL;
    result[2] = albedo[2] * (diffuse + specular) * NdotL;
}

/*
===============
RT_SampleBRDF

Importance sample the BRDF for next ray direction
===============
*/
void RT_SampleBRDF(const vec3_t wo, const vec3_t normal, float roughness, 
                   vec3_t wi, float *pdf, vec3_t result) {
    // For now, use cosine-weighted hemisphere sampling
    // This is simple and works well for diffuse surfaces
    RT_CosineSampleHemisphere(normal, wi);
    
    // PDF for cosine-weighted sampling
    *pdf = DotProduct(wi, normal) / M_PI;
    
    // The result would be the sampled direction
    VectorCopy(wi, result);
}

/*
===============
RT_EvaluateDirectLighting

Calculate direct lighting from all light sources
Optimized with light culling and caching
===============
*/
void RT_EvaluateDirectLighting(const hitInfo_t *hit, const vec3_t wo, vec3_t result) {
    VectorClear(result);
    
    if (!hit->shader || rt.mode == RT_MODE_OFF) {
        return;
    }
    
    if (rt.numSceneLights <= 0) {
        return;
    }
    
    vec3_t albedo = {1, 1, 1};
    float roughness = 0.5f;
    float metallic = 0.0f;
    vec3_t shadowOrigin;
    VectorMA(hit->point, 0.001f, hit->normal, shadowOrigin);

    int totalLights = rt.numSceneLights;
    rtLightEval_t *evaluations = (rtLightEval_t *)ri.Hunk_AllocateTempMemory(totalLights * sizeof(rtLightEval_t));
    rtShadowQuery_t *shadowQueries = (rtShadowQuery_t *)ri.Hunk_AllocateTempMemory(totalLights * sizeof(rtShadowQuery_t));
    int evalCount = 0;
    int queryCount = 0;

    for (int i = 0; i < totalLights; i++) {
        const rtSceneLight_t *light = &rt.sceneLights[i];
        if (light->intensity <= 0.0f) {
            continue;
        }

        vec3_t lightDir;
        float distance = RT_DIRECTIONAL_MAX_DISTANCE;
        qboolean valid = qtrue;

        switch (light->type) {
        case RT_LIGHT_TYPE_POINT:
        case RT_LIGHT_TYPE_SPOT:
            VectorSubtract(light->origin, hit->point, lightDir);
            distance = VectorLength(lightDir);
            if (distance <= 0.0f || distance > light->radius) {
                valid = qfalse;
                break;
            }
            VectorScale(lightDir, 1.0f / distance, lightDir);

            if (light->type == RT_LIGHT_TYPE_SPOT) {
                float dot = DotProduct(lightDir, light->direction);
                if (dot < light->spotCos) {
                    valid = qfalse;
                    break;
                }
            }
            break;
        case RT_LIGHT_TYPE_DIRECTIONAL:
            VectorCopy(light->direction, lightDir);
            if (VectorNormalize(lightDir) <= 0.0f) {
                valid = qfalse;
            }
            distance = RT_DIRECTIONAL_MAX_DISTANCE;
            break;
        default:
            valid = qfalse;
            break;
        }

        if (!valid) {
            continue;
        }

        rtLightEval_t *eval = &evaluations[evalCount];
        eval->light = light;
        VectorCopy(lightDir, eval->direction);
        eval->distance = distance;
        eval->queryIndex = -1;

        if (light->castsShadows) {
            rtShadowQuery_t *query = &shadowQueries[queryCount];
            VectorCopy(shadowOrigin, query->origin);
            VectorCopy(lightDir, query->direction);
            query->maxDistance = distance;
            query->occluded = qfalse;
            eval->queryIndex = queryCount;
            queryCount++;
        }

        evalCount++;
    }

    if (queryCount > 0) {
        qboolean gpuHandled = qfalse;
        if (RTX_RayQuerySupported()) {
            gpuHandled = RTX_DispatchShadowQueries(shadowQueries, queryCount);
        }

        if (!gpuHandled) {
            for (int i = 0; i < queryCount; i++) {
                shadowQueries[i].occluded = RT_TraceShadowRaySoftware(
                    shadowQueries[i].origin,
                    shadowQueries[i].direction,
                    shadowQueries[i].maxDistance);
            }
        }
    }

    for (int i = 0; i < evalCount; i++) {
        const rtLightEval_t *eval = &evaluations[i];
        const rtSceneLight_t *light = eval->light;

        if (eval->queryIndex >= 0 && shadowQueries[eval->queryIndex].occluded) {
            continue;
        }

        vec3_t brdf;
        RT_EvaluateBRDF(eval->direction, wo, hit->normal, albedo, roughness, metallic, brdf);

        float attenuation = 1.0f;
        if (light->type == RT_LIGHT_TYPE_POINT || light->type == RT_LIGHT_TYPE_SPOT) {
            attenuation = 1.0f - (eval->distance / light->radius);
            attenuation = attenuation * attenuation;
        }

        vec3_t lightContrib;
        VectorCopy(light->color, lightContrib);
        VectorScale(lightContrib, light->intensity * attenuation, lightContrib);

        result[0] += lightContrib[0] * brdf[0];
        result[1] += lightContrib[1] * brdf[1];
        result[2] += lightContrib[2] * brdf[2];
    }

    ri.Hunk_FreeTempMemory(shadowQueries);
    ri.Hunk_FreeTempMemory(evaluations);
}

/*
===============
RT_EvaluateIndirectLighting

Calculate indirect lighting using path tracing
Limited bounces for performance
===============
*/
void RT_EvaluateIndirectLighting(const hitInfo_t *hit, const vec3_t wo, int depth, vec3_t result) {
    VectorClear(result);
    
    // Russian roulette for path termination
    if (depth > 2) {
        float p = 0.5f; // Termination probability
        if (FastRandom() > p) {
            return;
        }
    }
    
    // Sample new direction
    vec3_t wi;
    float pdf;
    vec3_t sample;
    RT_SampleBRDF(wo, hit->normal, 0.5f, wi, &pdf, sample);
    
    // Trace secondary ray
    ray_t ray;
    VectorCopy(hit->point, ray.origin);
    VectorCopy(wi, ray.direction);
    ray.tMin = 0.001f;
    ray.tMax = 10000.0f;
    ray.depth = depth + 1;
    
    hitInfo_t nextHit;
    if (RT_TraceRay(&ray, &nextHit)) {
        // Recursive evaluation
        vec3_t Li;
        RT_TracePath(&ray, depth + 1, Li);
        
        // Apply BRDF and PDF
        vec3_t brdf;
        RT_EvaluateBRDF(wi, wo, hit->normal, (vec3_t){1,1,1}, 0.5f, 0.0f, brdf);
        
        float NdotL = DotProduct(hit->normal, wi);
        if (NdotL > 0 && pdf > 0.001f) {
            VectorScale(Li, NdotL / pdf, result);
        }
    }
}

/*
===============
RT_TracePath

Main path tracing function - traces a complete light path
===============
*/
void RT_TracePath(const ray_t *ray, int depth, vec3_t result) {
    VectorClear(result);
    
    if (depth > rt.maxBounces) {
        return;
    }
    
    hitInfo_t hit;
    if (!RT_TraceRay(ray, &hit)) {
        // Sky color or environment
        VectorSet(result, 0.5f, 0.7f, 1.0f);
        return;
    }
    
    // View direction (opposite of ray)
    vec3_t wo;
    VectorScale(ray->direction, -1, wo);
    
    // Direct lighting
    vec3_t direct;
    RT_EvaluateDirectLighting(&hit, wo, direct);
    
    // Indirect lighting (if quality allows)
    vec3_t indirect = {0, 0, 0};
    if (rt.quality >= RT_QUALITY_HIGH && depth < rt.maxBounces) {
        RT_EvaluateIndirectLighting(&hit, wo, depth, indirect);
    }
    
    // Combine
    VectorAdd(direct, indirect, result);
}

/*
===============
RT_CosineSampleHemisphere

Generate cosine-weighted sample on hemisphere
===============
*/
void RT_CosineSampleHemisphere(const vec3_t normal, vec3_t result) {
    // Generate random point on unit disk
    float r1 = FastRandom();
    float r2 = FastRandom();
    
    float theta = 2.0f * M_PI * r1;
    float r = sqrt(r2);
    
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(1.0f - r2);
    
    // Transform to world space
    vec3_t tangent, bitangent;
    
    // Build orthonormal basis
    if (fabs(normal[0]) < 0.9f) {
        VectorSet(tangent, 1, 0, 0);
    } else {
        VectorSet(tangent, 0, 1, 0);
    }
    
    CrossProduct(normal, tangent, bitangent);
    VectorNormalize(bitangent);
    CrossProduct(bitangent, normal, tangent);
    
    // Transform sample to world space
    result[0] = x * tangent[0] + y * bitangent[0] + z * normal[0];
    result[1] = x * tangent[1] + y * bitangent[1] + z * normal[1];
    result[2] = x * tangent[2] + y * bitangent[2] + z * normal[2];
}

/*
===============
RT_EvaluateStaticLighting

Calculate lighting from static light sources (extracted from BSP)
===============
*/
void RT_EvaluateStaticLighting(const hitInfo_t *hit, const vec3_t wo, vec3_t result) {
    VectorClear(result);
    
    if (!hit->shader || rt.numStaticLights == 0) {
        return;
    }
    
    // Get material properties from shader
    vec3_t albedo = {1, 1, 1};
    float roughness = 0.5f;
    float metallic = 0.0f;
    
    // Test all static lights
    for (int i = 0; i < rt.numStaticLights; i++) {
        staticLight_t *sl = &rt.staticLights[i];
        
        // Calculate light direction and distance
        vec3_t lightDir;
        VectorSubtract(sl->origin, hit->point, lightDir);
        float dist = VectorLength(lightDir);
        
        // Skip if out of range
        if (dist > sl->radius) {
            continue;
        }
        
        VectorNormalize(lightDir);
        
        // Check spotlight cone if applicable
        if (sl->type == 1) { // Spotlight
            float dot = DotProduct(lightDir, sl->direction);
            if (dot < cos(sl->spotAngle * M_PI / 180.0f)) {
                continue; // Outside cone
            }
        }
        
        // Shadow test if enabled
        if (sl->castShadows && RT_TraceShadowRay(hit->point, sl->origin, dist)) {
            continue; // In shadow
        }
        
        // Calculate BRDF
        vec3_t brdf;
        RT_EvaluateBRDF(lightDir, wo, hit->normal, albedo, roughness, metallic, brdf);
        
        // Apply light color and attenuation
        float atten = 1.0f - (dist / sl->radius);
        atten = atten * atten; // Quadratic falloff
        
        // Apply intensity and color
        vec3_t lightContrib;
        VectorScale(sl->color, sl->intensity * atten, lightContrib);
        
        // Multiply by BRDF
        result[0] += lightContrib[0] * brdf[0];
        result[1] += lightContrib[1] * brdf[1];
        result[2] += lightContrib[2] * brdf[2];
    }
}

/*
===============
RT_ExtractStaticLights

Extract static lights from BSP data
===============
*/
void RT_ExtractStaticLights(void) {
    if (!rt_staticLights->integer || !tr.world) {
        return;
    }
    
    rt.numStaticLights = 0;
    
    // Extract lights from entity string
    const char *entities = tr.world->entityString;
    const char *p = entities;
    char key[256], value[256];
    
    while (p && *p) {
        // Find next entity
        p = strstr(p, "{");
        if (!p) break;
        p++;
        
        qboolean isLight = qfalse;
        vec3_t origin = {0, 0, 0};
        vec3_t color = {1, 1, 1};
        float intensity = 300.0f;
        float radius = 300.0f;
        int lightType = 0; // 0 = point, 1 = spot
        vec3_t direction = {0, 0, -1};
        float spotAngle = 45.0f;
        
        // Parse entity
        while (p && *p && *p != '}') {
            // Skip whitespace
            while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
                p++;
            }
            
            if (*p == '}') break;
            
            // Read key
            const char *keyStart = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                p++;
            }
            int keyLen = MIN(p - keyStart, sizeof(key) - 1);
            strncpy(key, keyStart, keyLen);
            key[keyLen] = '\0';
            
            // Skip whitespace
            while (*p && (*p == ' ' || *p == '\t')) {
                p++;
            }
            
            // Read value
            const char *valStart = p;
            while (*p && *p != '\n' && *p != '\r') {
                p++;
            }
            int valLen = MIN(p - valStart, sizeof(value) - 1);
            strncpy(value, valStart, valLen);
            value[valLen] = '\0';
            
            // Parse key-value
            if (!Q_stricmp(key, "classname")) {
                if (strstr(value, "light")) {
                    isLight = qtrue;
                }
            } else if (!Q_stricmp(key, "origin")) {
                sscanf(value, "%f %f %f", &origin[0], &origin[1], &origin[2]);
            } else if (!Q_stricmp(key, "light")) {
                intensity = atof(value);
            } else if (!Q_stricmp(key, "_color")) {
                sscanf(value, "%f %f %f", &color[0], &color[1], &color[2]);
            } else if (!Q_stricmp(key, "radius")) {
                radius = atof(value);
            } else if (!Q_stricmp(key, "target")) {
                lightType = 1; // Spotlight
            } else if (!Q_stricmp(key, "angle")) {
                spotAngle = atof(value);
            }
        }
        
        // Add light if valid
        if (isLight && rt.numStaticLights < rt.maxStaticLights) {
            staticLight_t *sl = &rt.staticLights[rt.numStaticLights++];
            VectorCopy(origin, sl->origin);
            VectorCopy(color, sl->color);
            sl->intensity = intensity / 100.0f; // Scale to reasonable range
            sl->radius = radius;
            sl->type = lightType;
            VectorCopy(direction, sl->direction);
            sl->spotAngle = spotAngle;
            sl->castShadows = qtrue;
        }
    }
    
    if (rt.numStaticLights > 0) {
        ri.Printf(PRINT_ALL, "Extracted %d static lights from BSP\n", rt.numStaticLights);
    }
}

/*
===============
RT_UniformSampleHemisphere

Generate uniform sample on hemisphere
===============
*/
void RT_UniformSampleHemisphere(const vec3_t normal, vec3_t result) {
    float r1 = FastRandom();
    float r2 = FastRandom();
    
    float theta = 2.0f * M_PI * r1;
    float phi = acos(r2);
    
    float x = sin(phi) * cos(theta);
    float y = sin(phi) * sin(theta);
    float z = cos(phi);
    
    // Transform to world space (same as cosine sampling)
    vec3_t tangent, bitangent;
    
    if (fabs(normal[0]) < 0.9f) {
        VectorSet(tangent, 1, 0, 0);
    } else {
        VectorSet(tangent, 0, 1, 0);
    }
    
    CrossProduct(normal, tangent, bitangent);
    VectorNormalize(bitangent);
    CrossProduct(bitangent, normal, tangent);
    
    result[0] = x * tangent[0] + y * bitangent[0] + z * normal[0];
    result[1] = x * tangent[1] + y * bitangent[1] + z * normal[1];
    result[2] = x * tangent[2] + y * bitangent[2] + z * normal[2];
}

/*
===============
Light Cache Functions
Fast spatial hash for temporal coherence
===============
*/
void RT_InitLightCache(void) {
    if (rt.lightCache) {
        ri.Free(rt.lightCache);
    }
    
    rt.cacheSize = RT_CACHE_SIZE;
    rt.lightCache = ri.Malloc(sizeof(lightCacheEntry_t) * rt.cacheSize);
    Com_Memset(rt.lightCache, 0, sizeof(lightCacheEntry_t) * rt.cacheSize);
}

void RT_UpdateLightCache(const vec3_t pos, const vec3_t normal, const vec3_t irradiance) {
    // Simple spatial hash
    unsigned int hash = (unsigned int)(pos[0] * 73.0f + pos[1] * 179.0f + pos[2] * 283.0f);
    hash = hash % rt.cacheSize;
    
    lightCacheEntry_t *entry = &rt.lightCache[hash];
    
    // Update or replace entry
    if (VectorDistance(entry->position, pos) < 10.0f) {
        // Blend with existing
        float blend = 0.1f;
        VectorLerp(entry->irradiance, irradiance, blend, entry->irradiance);
        entry->confidence = MIN(1.0f, entry->confidence + 0.1f);
    } else {
        // Replace
        VectorCopy(pos, entry->position);
        VectorCopy(normal, entry->normal);
        VectorCopy(irradiance, entry->irradiance);
        entry->confidence = 0.5f;
    }
    
    entry->frameUpdated = rt.currentFrame;
    entry->sampleCount++;
}

qboolean RT_QueryLightCache(const vec3_t pos, const vec3_t normal, vec3_t irradiance) {
    unsigned int hash = (unsigned int)(pos[0] * 73.0f + pos[1] * 179.0f + pos[2] * 283.0f);
    hash = hash % rt.cacheSize;
    
    lightCacheEntry_t *entry = &rt.lightCache[hash];
    
    if (VectorDistance(entry->position, pos) < 10.0f &&
        DotProduct(entry->normal, normal) > 0.9f &&
        entry->confidence > 0.3f) {
        VectorCopy(entry->irradiance, irradiance);
        return qtrue;
    }
    
    return qfalse;
}

/*
===============
Probe Grid Functions
Irradiance probes for global illumination
===============
*/
void RT_InitProbeGrid(const vec3_t mins, const vec3_t maxs) {
    VectorCopy(mins, rt.probeGridOrigin);
    VectorSubtract(maxs, mins, rt.probeGridSize);
    
    // Calculate probe spacing
    float spacing = rt.probeGridSize[0] / RT_PROBE_GRID_SIZE;
    
    rt.numProbes = RT_PROBE_GRID_SIZE * RT_PROBE_GRID_SIZE * RT_PROBE_GRID_SIZE;
    rt.probes = ri.Malloc(sizeof(irradianceProbe_t) * rt.numProbes);
    
    // Initialize probe positions
    int index = 0;
    for (int z = 0; z < RT_PROBE_GRID_SIZE; z++) {
        for (int y = 0; y < RT_PROBE_GRID_SIZE; y++) {
            for (int x = 0; x < RT_PROBE_GRID_SIZE; x++) {
                irradianceProbe_t *probe = &rt.probes[index++];
                
                probe->position[0] = rt.probeGridOrigin[0] + x * spacing;
                probe->position[1] = rt.probeGridOrigin[1] + y * spacing;
                probe->position[2] = rt.probeGridOrigin[2] + z * spacing;
                
                // Clear irradiance
                for (int i = 0; i < 6; i++) {
                    VectorClear(probe->irradiance[i]);
                    probe->visibility[i] = 1.0f;
                }
                
                probe->lastUpdate = 0;
                probe->dynamic = qfalse;
            }
        }
    }
}

void RT_UpdateProbe(int probeIndex) {
    if (probeIndex < 0 || probeIndex >= rt.numProbes) {
        return;
    }
    
    irradianceProbe_t *probe = &rt.probes[probeIndex];
    
    // Sample irradiance in 6 directions (cube faces)
    vec3_t directions[6] = {
        {1, 0, 0}, {-1, 0, 0},
        {0, 1, 0}, {0, -1, 0},
        {0, 0, 1}, {0, 0, -1}
    };
    
    for (int i = 0; i < 6; i++) {
        ray_t ray;
        VectorCopy(probe->position, ray.origin);
        VectorCopy(directions[i], ray.direction);
        ray.tMin = 0.1f;
        ray.tMax = 1000.0f;
        ray.depth = 0;
        
        vec3_t irradiance;
        RT_TracePath(&ray, 0, irradiance);
        
        // Update probe irradiance with temporal filtering
        float blend = 0.1f;
        VectorLerp(probe->irradiance[i], irradiance, blend, probe->irradiance[i]);
    }
    
    probe->lastUpdate = rt.currentFrame;
}

void RT_SampleProbeGrid(const vec3_t pos, const vec3_t normal, vec3_t result) {
    VectorClear(result);
    
    if (!rt.probes) {
        return;
    }
    
    // Find nearest probes
    vec3_t gridPos;
    VectorSubtract(pos, rt.probeGridOrigin, gridPos);
    
    float spacing = rt.probeGridSize[0] / RT_PROBE_GRID_SIZE;
    int x = (int)(gridPos[0] / spacing);
    int y = (int)(gridPos[1] / spacing);
    int z = (int)(gridPos[2] / spacing);
    
    // Clamp to grid bounds
    x = Com_Clamp(0, RT_PROBE_GRID_SIZE - 2, x);
    y = Com_Clamp(0, RT_PROBE_GRID_SIZE - 2, y);
    z = Com_Clamp(0, RT_PROBE_GRID_SIZE - 2, z);
    
    // Trilinear interpolation of 8 nearest probes
    float fx = (gridPos[0] / spacing) - x;
    float fy = (gridPos[1] / spacing) - y;
    float fz = (gridPos[2] / spacing) - z;
    
    for (int dz = 0; dz <= 1; dz++) {
        for (int dy = 0; dy <= 1; dy++) {
            for (int dx = 0; dx <= 1; dx++) {
                int index = (z + dz) * RT_PROBE_GRID_SIZE * RT_PROBE_GRID_SIZE +
                           (y + dy) * RT_PROBE_GRID_SIZE + (x + dx);
                
                if (index >= rt.numProbes) continue;
                
                irradianceProbe_t *probe = &rt.probes[index];
                
                // Weight based on position
                float weight = (dx ? fx : 1-fx) * (dy ? fy : 1-fy) * (dz ? fz : 1-fz);
                
                // Sample probe in direction of normal
                vec3_t probeIrradiance = {0, 0, 0};
                for (int i = 0; i < 6; i++) {
                    vec3_t directions[6] = {
                        {1, 0, 0}, {-1, 0, 0},
                        {0, 1, 0}, {0, -1, 0},
                        {0, 0, 1}, {0, 0, -1}
                    };
                    
                    float dot = DotProduct(normal, directions[i]);
                    if (dot > 0) {
                        VectorMA(probeIrradiance, dot, probe->irradiance[i], probeIrradiance);
                    }
                }
                
                VectorMA(result, weight, probeIrradiance, result);
            }
        }
    }
}

/*
===============
RT_RenderPathTracedLighting

Main rendering function - integrates with existing renderer
This is called per frame to add path traced lighting
===============
*/
void RT_RenderPathTracedLighting(void) {
    if (!rt.enabled || !rt_enable || !rt_enable->integer) {
        return;
    }
    
    if (!tr.world) {
        return;
    }
    
    // Check if path tracing is disabled
    if (rt.mode == RT_MODE_OFF) {
        return;
    }
    
    // Update frame counter
    rt.currentFrame++;
    
    // Handle different lighting modes
    switch (rt.mode) {
    case RT_MODE_DYNAMIC:
        // Only path trace dynamic lights
        // Base lighting comes from lightmaps
        break;
        
    case RT_MODE_ALL:
        // Path trace all lighting (static + dynamic)
        // This replaces lightmap lighting
        // Ensure static lights are extracted
        if (rt.numStaticLights == 0 && rt_staticLights->integer) {
            RT_ExtractStaticLights();
        }
        break;
        
    default:
        break;
    }
    
    // Update probes if needed
    if (rt_probes && rt_probes->integer && rt.numProbes > 0) {
        // Update a subset of probes each frame for performance
        int probesPerFrame = MAX(1, rt.numProbes / 16);
        for (int i = 0; i < probesPerFrame; i++) {
            int index = (rt.currentFrame * probesPerFrame + i) % rt.numProbes;
            RT_UpdateProbe(index);
        }
    }
    
    // Render debug visualization if enabled
    if (rt_debug && rt_debug->integer) {
        RT_RenderDebugVisualization();
        RT_DrawLightProbes();
        RT_DebugStats();
    }
    
    // The path tracer is now ready to be used by the main renderer
    // Surfaces will query RT_EvaluateDirectLighting based on the mode
}

/*
===============
Utility Functions
===============
*/
void RT_HammersleySequence(int i, int n, vec2_t result) {
    result[0] = (float)i / (float)n;
    result[1] = RT_RadicalInverse(i);
}

float RT_RadicalInverse(unsigned int bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return (float)bits * 2.3283064365386963e-10f; // / 0x100000000
}

void RT_GenerateRay(int x, int y, int sample, ray_t *ray) {
    // Generate camera ray for pixel (x, y) with jitter for anti-aliasing
    // This would use the view parameters from tr.refdef
    
    // For now, just return a default ray
    VectorCopy(tr.refdef.vieworg, ray->origin);
    VectorCopy(tr.refdef.viewaxis[0], ray->direction);
    ray->tMin = 0.1f;
    ray->tMax = 10000.0f;
    ray->depth = 0;
}

void RT_GetAccumulatedColor(int x, int y, vec3_t result) {
    if (!rt.accumBuffer || !rt.sampleBuffer ||
        x < 0 || y < 0 ||
        x >= rt.temporalWidth || y >= rt.temporalHeight) {
        VectorClear(result);
        return;
    }

    int pixelIndex = y * rt.temporalWidth + x;
    int base = pixelIndex * 3;
    int samples = rt.sampleBuffer[pixelIndex];

    if (samples <= 0) {
        VectorClear(result);
        return;
    }

    result[0] = rt.accumBuffer[base + 0];
    result[1] = rt.accumBuffer[base + 1];
    result[2] = rt.accumBuffer[base + 2];
}

void RT_BuildCameraRay(int x, int y, int width, int height, ray_t *ray) {
    if (!ray || width <= 0 || height <= 0) {
        return;
    }

    const viewParms_t *vp = &backEnd.viewParms;
    vec3_t forward, right, up;

    VectorCopy(vp->or.axis[0], forward);
    VectorCopy(vp->or.axis[1], right);
    VectorCopy(vp->or.axis[2], up);

    float ndcX = ((2.0f * ((float)x + 0.5f)) / (float)width) - 1.0f;
    float ndcY = 1.0f - ((2.0f * ((float)y + 0.5f)) / (float)height);

    float tanHalfFov = tanf(DEG2RAD(vp->fovX * 0.5f));
    float aspectRatio = (height > 0) ? ((float)width / (float)height) : 1.0f;

    ndcX *= tanHalfFov * aspectRatio;
    ndcY *= tanHalfFov;

    VectorCopy(vp->or.origin, ray->origin);

    ray->direction[0] = forward[0] + ndcX * right[0] + ndcY * up[0];
    ray->direction[1] = forward[1] + ndcX * right[1] + ndcY * up[1];
    ray->direction[2] = forward[2] + ndcX * right[2] + ndcY * up[2];
    VectorNormalize(ray->direction);

    ray->tMin = 0.001f;
    ray->tMax = 10000.0f;
    ray->depth = 0;
    ray->ior = 1.0f;
}

void RT_AccumulateSample(int x, int y, const vec3_t color) {
    if (!rt.accumBuffer || !rt.varianceBuffer || !rt.sampleBuffer ||
        x < 0 || y < 0 ||
        x >= rt.temporalWidth || y >= rt.temporalHeight) {
        return;
    }

    int pixelIndex = y * rt.temporalWidth + x;
    int base = pixelIndex * 3;

    if (!rt.temporalEnabled) {
        rt.accumBuffer[base + 0] = color[0];
        rt.accumBuffer[base + 1] = color[1];
        rt.accumBuffer[base + 2] = color[2];
        rt.varianceBuffer[base + 0] = 0.0f;
        rt.varianceBuffer[base + 1] = 0.0f;
        rt.varianceBuffer[base + 2] = 0.0f;
        rt.sampleBuffer[pixelIndex] = 1;
        return;
    }

    int samples = ++rt.sampleBuffer[pixelIndex];

    for (int c = 0; c < 3; c++) {
        float mean = rt.accumBuffer[base + c];
        float delta = color[c] - mean;
        mean += delta / samples;
        float delta2 = color[c] - mean;

        rt.accumBuffer[base + c] = mean;
        rt.varianceBuffer[base + c] += delta * delta2;
    }
}

void RT_ProcessGpuFrame(const float *rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) {
        return;
    }

    if (width != glConfig.vidWidth || height != glConfig.vidHeight) {
        static qboolean warned = qfalse;
        if (!warned) {
            ri.Printf(PRINT_DEVELOPER, "RT_ProcessGpuFrame: Skipping validation/temporal integration due to resolution mismatch (%dx%d vs %dx%d)\n",
                      width, height, glConfig.vidWidth, glConfig.vidHeight);
            warned = qtrue;
        }
        return;
    }

    RT_InitTemporalBuffers();

    if (!rt.accumBuffer || !rt.varianceBuffer || !rt.sampleBuffer) {
        return;
    }

    if (rt.temporalWidth != width || rt.temporalHeight != height) {
        RT_ResetAccumulation();
        if (rt.temporalWidth != width || rt.temporalHeight != height) {
            return;
        }
    }

    const qboolean validate = (rt_gpuValidate && rt_gpuValidate->integer > 0);
    const int validationStride = validate ? MAX(1, rt_gpuValidate->integer) : 0;

    size_t pixelCount = (size_t)width * (size_t)height;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (size_t)y * width + x;
            size_t base = idx * 4;

            vec3_t sample = {
                rgba[base + 0],
                rgba[base + 1],
                rgba[base + 2]
            };

            for (int c = 0; c < 3; ++c) {
                if (!isfinite(sample[c]) || fabsf(sample[c]) > 1e6f) {
                    sample[c] = 0.0f;
                }
            }

            RT_AccumulateSample(x, y, sample);
        }
    }

    if (rt_denoise && rt_denoise->integer && rt.denoisedBuffer) {
        RT_DenoiseFrame(rt.accumBuffer, rt.denoisedBuffer, width, height);
    } else if (rt.denoisedBuffer) {
        size_t bytes = pixelCount * 3 * sizeof(float);
        Com_Memcpy(rt.denoisedBuffer, rt.accumBuffer, bytes);
    }

    rt.currentFrame++;

    if (validate && validationStride > 0) {
        double sumSq = 0.0;
        double maxErr = 0.0;
        int samples = 0;
        vec3_t cpuColor;

        for (int y = 0; y < height; y += validationStride) {
            for (int x = 0; x < width; x += validationStride) {
                ray_t ray;
                RT_BuildCameraRay(x, y, width, height, &ray);
                RT_TracePath(&ray, 0, cpuColor);

                size_t idx = (size_t)y * width + x;
                size_t base = idx * 4;
                float gpuColor[3] = {
                    rgba[base + 0],
                    rgba[base + 1],
                    rgba[base + 2]
                };

                for (int c = 0; c < 3; ++c) {
                    if (!isfinite(gpuColor[c]) || fabsf(gpuColor[c]) > 1e6f) {
                        gpuColor[c] = 0.0f;
                    }
                    double diff = (double)cpuColor[c] - (double)gpuColor[c];
                    sumSq += diff * diff;
                    double absDiff = fabs(diff);
                    if (absDiff > maxErr) {
                        maxErr = absDiff;
                    }
                }
                samples++;
            }
        }

        if (samples > 0) {
            rt.validationRMSE = sqrt(sumSq / (double)(samples * 3));
            rt.validationMaxError = maxErr;
            rt.validationSamples = samples;
        } else {
            rt.validationRMSE = 0.0;
            rt.validationMaxError = 0.0;
            rt.validationSamples = 0;
        }
    } else {
        rt.validationRMSE = 0.0;
        rt.validationMaxError = 0.0;
        rt.validationSamples = 0;
    }

    qboolean backendValidated = (validate && validationStride > 0 && rt.validationSamples > 0);
    RT_RecordBackendValidation(rgba, width, height, backendValidated);
}

/*
===============
RT_Status_f

Console command to display path tracing status
===============
*/
void RT_Status_f(void) {
    const char *modeStr = "Unknown";
    const char *qualityStr = "Unknown";
    
    switch (rt.mode) {
    case RT_MODE_OFF:
        modeStr = "Off";
        break;
    case RT_MODE_DYNAMIC:
        modeStr = "Dynamic Lights Only";
        break;
    case RT_MODE_ALL:
        modeStr = "All Lighting (Static + Dynamic)";
        break;
    }
    
    switch (rt.quality) {
    case RT_QUALITY_OFF:
        qualityStr = "Off";
        break;
    case RT_QUALITY_LOW:
        qualityStr = "Low";
        break;
    case RT_QUALITY_MEDIUM:
        qualityStr = "Medium";
        break;
    case RT_QUALITY_HIGH:
        qualityStr = "High";
        break;
    case RT_QUALITY_ULTRA:
        qualityStr = "Ultra";
        break;
    }
    
    ri.Printf(PRINT_ALL, "\n==== Path Tracing Status ====\n");
    ri.Printf(PRINT_ALL, "Enabled: %s\n", rt.enabled ? "Yes" : "No");
    ri.Printf(PRINT_ALL, "Backend: %s\n", RT_GetBackendStatus());
    ri.Printf(PRINT_ALL, "RTX Active: %s\n", (rt.useRTX && RTX_IsAvailable()) ? "Yes" : "No");
    ri.Printf(PRINT_ALL, "Scene Lights: %d (dynamic %d, static %d)\n",
              rt.numSceneLights, rt.numDynamicLights, rt.numStaticLights);
    ri.Printf(PRINT_ALL, "Light Buffer: %s\n",
              rt.sceneLightBufferDirty ? "Pending upload" : "Synced");
    ri.Printf(PRINT_ALL, "Mode: %s\n", modeStr);
    ri.Printf(PRINT_ALL, "Quality: %s\n", qualityStr);
    ri.Printf(PRINT_ALL, "Max Bounces: %d\n", rt.maxBounces);
    ri.Printf(PRINT_ALL, "Samples Per Pixel: %d\n", rt.samplesPerPixel);
    ri.Printf(PRINT_ALL, "Backend: %s\n", (rt.useRTX && RTX_IsAvailable()) ? "RTX Hardware" : "Software");
    ri.Printf(PRINT_ALL, "RTX Available: %s\n", RTX_IsAvailable() ? "Yes" : "No");
    ri.Printf(PRINT_ALL, "Static Lights: %d / %d\n", rt.numStaticLights, rt.maxStaticLights);
    ri.Printf(PRINT_ALL, "Frame: %d\n", rt.currentFrame);
    ri.Printf(PRINT_ALL, "Temporal Accumulation: %s (%dx%d)\n",
              rt.temporalEnabled ? "On" : "Off", rt.temporalWidth, rt.temporalHeight);
    ri.Printf(PRINT_ALL, "\nCVARs:\n");
    ri.Printf(PRINT_ALL, "  rt_enable: %d\n", rt_enable ? rt_enable->integer : 0);
    ri.Printf(PRINT_ALL, "  rt_mode: %s\n", rt_mode ? rt_mode->string : "not set");
    ri.Printf(PRINT_ALL, "  rt_quality: %d\n", rt_quality ? rt_quality->integer : 0);
    ri.Printf(PRINT_ALL, "  rt_bounces: %d\n", rt_bounces ? rt_bounces->integer : 0);
    ri.Printf(PRINT_ALL, "  rt_samples: %d\n", rt_samples ? rt_samples->integer : 0);
    ri.Printf(PRINT_ALL, "  rt_temporal: %d\n", rt_temporal ? rt_temporal->integer : 0);
    ri.Printf(PRINT_ALL, "  rt_backend: %s\n", r_rt_backend ? r_rt_backend->string : "auto");
    ri.Printf(PRINT_ALL, "  rt_staticLights: %d\n", rt_staticLights ? rt_staticLights->integer : 0);
    ri.Printf(PRINT_ALL, "  rt_debug: %d\n", rt_debug ? rt_debug->integer : 0);

    if (rt_gpuValidate && rt_gpuValidate->integer > 0) {
        int stride = MAX(1, rt_gpuValidate->integer);
#ifdef USE_VULKAN
        qboolean hardwareActive = (rt.useRTX && RTX_IsAvailable());
#else
        qboolean hardwareActive = qfalse;
#endif
        const char *backendLabel = hardwareActive ? "RTX HW" : "Compute";

        ri.Printf(PRINT_ALL, "\nValidation (stride=%d, active=%s)\n", stride, backendLabel);
        ri.Printf(PRINT_ALL, "  Last frame RMSE: %.5f  Max: %.5f  Samples: %d\n",
                  (float)rt.validationRMSE,
                  (float)rt.validationMaxError,
                  rt.validationSamples);

        const rtBackendValidation_t *compute = &rt.backendValidation[RT_BACKEND_INDEX_COMPUTE];
        const rtBackendValidation_t *hardware = &rt.backendValidation[RT_BACKEND_INDEX_HARDWARE];

        if (compute->hash) {
            ri.Printf(PRINT_ALL,
                      "  Compute backend: hash=%08X map=%s RMSE=%.5f Max=%.5f Samples=%d\n",
                      compute->hash,
                      compute->map[0] ? compute->map : "unknown",
                      (float)compute->rmse,
                      (float)compute->maxError,
                      compute->samples);
        }

        if (hardware->hash) {
            ri.Printf(PRINT_ALL,
                      "  RTX backend:     hash=%08X map=%s RMSE=%.5f Max=%.5f Samples=%d\n",
                      hardware->hash,
                      hardware->map[0] ? hardware->map : "unknown",
                      (float)hardware->rmse,
                      (float)hardware->maxError,
                      hardware->samples);
        }

        if (rt.backendParityMap[0] && hardware->valid && compute->valid) {
            ri.Printf(PRINT_ALL,
                      "  ΔRMSE=%.5f  ΔMax=%.5f (map=%s)\n",
                      (float)rt.backendRMSEDelta,
                      (float)rt.backendMaxErrorDelta,
                      rt.backendParityMap);
        }
    } else {
        ri.Printf(PRINT_ALL, "\nValidation: disabled\n");
    }

    ri.Printf(PRINT_ALL, "=============================\n");
}

/*
===============
Temporal accumulation helpers
===============
*/
void RT_InitTemporalBuffers(void) {
    const int width = glConfig.vidWidth;
    const int height = glConfig.vidHeight;

    if (width <= 0 || height <= 0) {
        return;
    }

    if (rt.temporalWidth == width && rt.temporalHeight == height &&
        rt.accumBuffer && rt.varianceBuffer && rt.sampleBuffer && rt.denoisedBuffer) {
        return;
    }

    if (rt.accumBuffer) {
        ri.Free(rt.accumBuffer);
        rt.accumBuffer = NULL;
    }
    if (rt.varianceBuffer) {
        ri.Free(rt.varianceBuffer);
        rt.varianceBuffer = NULL;
    }
    if (rt.sampleBuffer) {
        ri.Free(rt.sampleBuffer);
        rt.sampleBuffer = NULL;
    }
    if (rt.denoisedBuffer) {
        ri.Free(rt.denoisedBuffer);
        rt.denoisedBuffer = NULL;
    }

    size_t pixelCount = (size_t)width * height;
    size_t colorBytes = pixelCount * 3 * sizeof(float);
    size_t sampleBytes = pixelCount * sizeof(int);

    rt.accumBuffer = ri.Malloc(colorBytes);
    rt.varianceBuffer = ri.Malloc(colorBytes);
    rt.denoisedBuffer = ri.Malloc(colorBytes);
    rt.sampleBuffer = ri.Malloc(sampleBytes);

    if (!rt.accumBuffer || !rt.varianceBuffer || !rt.denoisedBuffer || !rt.sampleBuffer) {
        ri.Printf(PRINT_WARNING, "RT_InitTemporalBuffers: failed to allocate %dx%d buffers\n", width, height);

        if (rt.accumBuffer) { ri.Free(rt.accumBuffer); rt.accumBuffer = NULL; }
        if (rt.varianceBuffer) { ri.Free(rt.varianceBuffer); rt.varianceBuffer = NULL; }
        if (rt.denoisedBuffer) { ri.Free(rt.denoisedBuffer); rt.denoisedBuffer = NULL; }
        if (rt.sampleBuffer) { ri.Free(rt.sampleBuffer); rt.sampleBuffer = NULL; }
        rt.temporalWidth = rt.temporalHeight = 0;
        return;
    }

    rt.temporalWidth = width;
    rt.temporalHeight = height;

    RT_ResetAccumulation();
}

void RT_ResetAccumulation(void) {
    int width = glConfig.vidWidth;
    int height = glConfig.vidHeight;

    if (!rt.accumBuffer || !rt.sampleBuffer || width <= 0 || height <= 0) {
        rt.temporalWidth = width > 0 ? width : 0;
        rt.temporalHeight = height > 0 ? height : 0;
        rt.currentFrame = 0;
        rt.validationRMSE = 0.0;
        rt.validationMaxError = 0.0;
        rt.validationSamples = 0;
        return;
    }

    size_t pixelCount = (size_t)width * height;

    if (rt.accumBuffer) {
        Com_Memset(rt.accumBuffer, 0, pixelCount * 3 * sizeof(float));
    }
    if (rt.varianceBuffer) {
        Com_Memset(rt.varianceBuffer, 0, pixelCount * 3 * sizeof(float));
    }
    if (rt.sampleBuffer) {
        Com_Memset(rt.sampleBuffer, 0, pixelCount * sizeof(int));
    }
    if (rt.denoisedBuffer) {
        Com_Memset(rt.denoisedBuffer, 0, pixelCount * 3 * sizeof(float));
    }

    rt.temporalWidth = width;
    rt.temporalHeight = height;
    rt.currentFrame = 0;
    rt.validationRMSE = 0.0;
    rt.validationMaxError = 0.0;
    rt.validationSamples = 0;
    RT_ResetScreenProgress();
}
/*
===============
RT_BeginFrame

Prepare path tracer for new frame
===============
*/
void RT_BeginFrame(void) {
    RT_SelectBackend();

    if (!rt_enable || !rt_enable->integer) {
        rt.enabled = qfalse;
        return;
    }
    
    rt.enabled = qtrue;
    
    // Parse rt_mode CVAR
    const char *modeStr = rt_mode->string;
    if (!Q_stricmp(modeStr, "off")) {
        rt.mode = RT_MODE_OFF;
    } else if (!Q_stricmp(modeStr, "dynamic")) {
        rt.mode = RT_MODE_DYNAMIC;
    } else if (!Q_stricmp(modeStr, "all")) {
        rt.mode = RT_MODE_ALL;
    } else {
        // Default to dynamic if invalid
        rt.mode = RT_MODE_DYNAMIC;
    }
    
    // Extract static lights if needed and not already done
    static void *lastWorld = NULL;
    if (rt.mode == RT_MODE_ALL && tr.world) {
        if (tr.world != lastWorld) {
            // New level loaded, extract static lights
            if (!rt.staticLights) {
                rt.maxStaticLights = RT_MAX_STATIC_LIGHTS;
                rt.staticLights = ri.Hunk_Alloc(sizeof(staticLight_t) * rt.maxStaticLights, h_low);
            }
            RT_ExtractStaticLights();
            lastWorld = tr.world;
        }
    }
    
    // Update quality settings
    rt.quality = (rtQuality_t)rt_quality->integer;
    rt.maxBounces = rt_bounces->integer;
    rt.samplesPerPixel = rt_samples->integer;

    RT_InitTemporalBuffers();
    rt.validationRMSE = 0.0;
    rt.validationMaxError = 0.0;
    rt.validationSamples = 0;

    static qboolean firstFrame = qtrue;
    static rtMode_t lastMode = RT_MODE_DYNAMIC;
    static int lastSamples = -1;
    static int lastBounces = -1;
    static rtQuality_t lastQualitySetting = RT_QUALITY_MEDIUM;
    static qboolean lastTemporalEnabled = qtrue;

    rt.temporalEnabled = (rt_temporal && rt_temporal->integer) ? qtrue : qfalse;

    if (lastTemporalEnabled != rt.temporalEnabled) {
        RT_ResetAccumulation();
        lastTemporalEnabled = rt.temporalEnabled;
    }

    if (firstFrame || lastMode != rt.mode) {
        RT_ResetAccumulation();
        lastMode = rt.mode;
        firstFrame = qfalse;
    }

    if (lastSamples != rt.samplesPerPixel || lastBounces != rt.maxBounces || lastQualitySetting != rt.quality) {
        RT_ResetAccumulation();
        lastSamples = rt.samplesPerPixel;
        lastBounces = rt.maxBounces;
        lastQualitySetting = rt.quality;
    }
    
    // Reset frame statistics
    rt.raysTraced = 0;
    rt.triangleTests = 0;
    rt.boxTests = 0;
}
/*
===============
RT_EndFrame

End of frame statistics and debug output
===============
*/
void RT_EndFrame(void) {
    if (rt_debug && rt_debug->integer && rt.enabled) {
        const char *modeStr = "Unknown";
        switch (rt.mode) {
        case RT_MODE_OFF:
            modeStr = "Off";
            break;
        case RT_MODE_DYNAMIC:
            modeStr = "Dynamic Only";
            break;
        case RT_MODE_ALL:
            modeStr = "All Lighting";
            break;
        }
        
        ri.Printf(PRINT_ALL, "Path Tracing: Mode=%s, Static Lights=%d, Rays=%d\n",
                  modeStr, rt.numStaticLights, rt.raysTraced);
    }

    if (rt.enabled && rt_gpuValidate && rt_gpuValidate->integer > 0 &&
        rt.validationSamples > 0) {
        int stride = MAX(1, rt_gpuValidate->integer);
#ifdef USE_VULKAN
        qboolean hardwareActive = (rt.useRTX && RTX_IsAvailable());
#else
        qboolean hardwareActive = qfalse;
#endif
        const char *backendLabel = hardwareActive ? "RTX HW" : "Compute";
        ri.Printf(PRINT_ALL,
                  "rt_gpuValidate (%s): stride=%d RMSE=%.5f max=%.5f (%d samples)\n",
                  backendLabel,
                  stride,
                  (float)rt.validationRMSE,
                  (float)rt.validationMaxError,
                  rt.validationSamples);
    }

    if (rt.backendParityMap[0] &&
        rt.backendValidation[RT_BACKEND_INDEX_COMPUTE].valid &&
        rt.backendValidation[RT_BACKEND_INDEX_HARDWARE].valid &&
        rt.backendParityFrame == rt.currentFrame) {
        ri.Printf(PRINT_DEVELOPER,
                  "rt_gpuValidate parity %s: ΔRMSE=%.5f ΔMax=%.5f (RTX=%08X, Compute=%08X)\n",
                  rt.backendParityMap,
                  (float)rt.backendRMSEDelta,
                  (float)rt.backendMaxErrorDelta,
                  rt.backendValidation[RT_BACKEND_INDEX_HARDWARE].hash,
                  rt.backendValidation[RT_BACKEND_INDEX_COMPUTE].hash);
    }
}

static qboolean RT_BuildDynamicFromRenderLight(const renderLight_t *light, rtDynamicLight_t *out) {
    if (!light || !out) {
        return qfalse;
    }

    vec3_t color;
    VectorCopy(light->color, color);
    float intensity = (light->intensity > 0.0f) ? light->intensity : 1.0f;
    float brightness = color[0] + color[1] + color[2];

    if (brightness <= 0.0f) {
        return qfalse;
    }

    out->type = RT_LIGHT_TYPE_POINT;
    VectorCopy(light->origin, out->origin);
    VectorCopy(color, out->color);
    VectorClear(out->direction);
    out->radius = RT_SafeRadius((light->cutoffDistance > 0.0f) ? light->cutoffDistance : light->radius);
    out->intensity = intensity;
    out->spotCos = -1.0f;
    out->castsShadows = (light->flags & LIGHTFLAG_NOSHADOWS) ? qfalse : qtrue;
    out->isStatic = light->isStatic ? qtrue : qfalse;
    out->additive = qfalse;

    switch (light->type) {
    case RL_OMNI:
        out->type = RT_LIGHT_TYPE_POINT;
        break;
    case RL_PROJ:
        out->type = RT_LIGHT_TYPE_SPOT;
        VectorSubtract(light->target, light->origin, out->direction);
        if (VectorNormalize(out->direction) <= 0.0f) {
            VectorSet(out->direction, 0.0f, 0.0f, -1.0f);
        }
        out->spotCos = RT_ComputeSpotCosFromFov(light->fovX);
        break;
    case RL_DIRECTIONAL:
        out->type = RT_LIGHT_TYPE_DIRECTIONAL;
        VectorCopy(light->target, out->direction);
        if (VectorNormalize(out->direction) <= 0.0f) {
            VectorSet(out->direction, 0.0f, 0.0f, -1.0f);
        }
        out->radius = RT_DIRECTIONAL_MAX_DISTANCE;
        out->spotCos = -1.0f;
        out->isStatic = qtrue;
        break;
    case RL_AMBIENT:
        out->type = RT_LIGHT_TYPE_POINT;
        out->castsShadows = qfalse;
        out->isStatic = qtrue;
        if (out->radius < 2048.0f) {
            out->radius = 2048.0f;
        }
        break;
    case RL_FOG:
    default:
        return qfalse;
    }

    out->radius = RT_SafeRadius(out->radius);
    return qtrue;
}

static qboolean RT_BuildDynamicFromLegacyDlight(const dlight_t *dlight, rtDynamicLight_t *out) {
    if (!dlight || !out) {
        return qfalse;
    }

    float brightness = dlight->color[0] + dlight->color[1] + dlight->color[2];
    if (brightness <= 0.0f) {
        return qfalse;
    }

    out->type = RT_LIGHT_TYPE_POINT;
    VectorCopy(dlight->origin, out->origin);
    VectorCopy(dlight->color, out->color);
    VectorClear(out->direction);
    out->radius = RT_SafeRadius(dlight->radius);
    out->intensity = brightness / 3.0f;
    if (out->intensity <= 0.0f) {
        out->intensity = 1.0f;
    }
    out->spotCos = -1.0f;
    out->castsShadows = dlight->additive ? qfalse : qtrue;
    out->isStatic = qfalse;
    out->additive = dlight->additive ? qtrue : qfalse;

    return qtrue;
}

static void RT_RebuildSceneLights(void) {
    if (rt.mode == RT_MODE_OFF) {
        if (rt.numSceneLights != 0 || rt.sceneLightHash != 0) {
            rt.numSceneLights = 0;
            if (rt.sceneLightHash != 0) {
                rt.sceneLightHash = 0;
                RT_ResetAccumulation();
            }
        }
#ifdef USE_VULKAN
        rt.sceneLightBufferDirty = qtrue;
        RT_UpdateSceneLightBuffer();
#endif
        return;
    }

    int combined = 0;

    for (int i = 0; i < rt.numDynamicLights && combined < RT_MAX_SCENE_LIGHTS; i++) {
        const rtDynamicLight_t *src = &rt.dynamicLights[i];
        rtSceneLight_t *dst = &rt.sceneLights[combined++];

        dst->type = src->type;
        VectorCopy(src->origin, dst->origin);
        VectorCopy(src->color, dst->color);
        VectorCopy(src->direction, dst->direction);
        dst->radius = RT_SafeRadius(src->radius);
        dst->intensity = src->intensity;
        dst->spotCos = Com_Clamp(-1.0f, 1.0f, src->spotCos);
        if (dst->intensity <= 0.0f) {
            float fallback = fabsf(dst->color[0]) + fabsf(dst->color[1]) + fabsf(dst->color[2]);
            if (fallback > 0.0f) {
                dst->intensity = fallback / 3.0f;
            }
        }
        dst->castsShadows = src->castsShadows;
        dst->isStatic = src->isStatic;

        if (dst->type == RT_LIGHT_TYPE_DIRECTIONAL) {
            if (VectorNormalize(dst->direction) <= 0.0f) {
                VectorSet(dst->direction, 0.0f, 0.0f, -1.0f);
            }
            dst->radius = RT_DIRECTIONAL_MAX_DISTANCE;
        } else if (dst->type == RT_LIGHT_TYPE_SPOT) {
            if (VectorNormalize(dst->direction) <= 0.0f) {
                VectorSet(dst->direction, 0.0f, 0.0f, -1.0f);
            }
        } else {
            VectorClear(dst->direction);
        }
    }

    if (rt.mode == RT_MODE_ALL) {
        for (int i = 0; i < rt.numStaticLights && combined < RT_MAX_SCENE_LIGHTS; i++) {
            const staticLight_t *sl = &rt.staticLights[i];
            rtSceneLight_t *dst = &rt.sceneLights[combined++];

            dst->type = (sl->type == 1) ? RT_LIGHT_TYPE_SPOT : RT_LIGHT_TYPE_POINT;
            VectorCopy(sl->origin, dst->origin);
            VectorCopy(sl->color, dst->color);
            dst->radius = RT_SafeRadius(sl->radius);
            dst->intensity = sl->intensity;
            if (dst->intensity <= 0.0f) {
                float fallback = fabsf(dst->color[0]) + fabsf(dst->color[1]) + fabsf(dst->color[2]);
                if (fallback > 0.0f) {
                    dst->intensity = fallback / 3.0f;
                }
            }
            dst->castsShadows = sl->castShadows;
            dst->isStatic = qtrue;

            if (dst->type == RT_LIGHT_TYPE_SPOT) {
                VectorCopy(sl->direction, dst->direction);
                if (VectorNormalize(dst->direction) <= 0.0f) {
                    VectorSet(dst->direction, 0.0f, 0.0f, -1.0f);
                }
                dst->spotCos = RT_ComputeSpotCosFromFov(sl->spotAngle);
            } else {
                VectorClear(dst->direction);
                dst->spotCos = -1.0f;
            }
        }
    }

    rt.numSceneLights = combined;

    uint32_t newHash = RT_ComputeSceneLightHash(rt.sceneLights, rt.numSceneLights);
    if (newHash != rt.sceneLightHash) {
        rt.sceneLightHash = newHash;
        RT_ResetAccumulation();
    }
#ifdef USE_VULKAN
    rt.sceneLightBufferDirty = qtrue;
    RT_UpdateSceneLightBuffer();
#endif
}

void RT_UpdateDynamicLights(void) {
    rt.numDynamicLights = 0;

    if (rt.mode == RT_MODE_OFF) {
        RT_RebuildSceneLights();
        return;
    }

    qboolean appendedFromLightSystem = qfalse;

    R_UpdateLightSystem();

    if (tr_lightSystem.numVisibleLights > 0) {
        int limit = MIN(tr_lightSystem.numVisibleLights, RT_MAX_LIGHTS);
        for (int i = 0; i < limit && rt.numDynamicLights < RT_MAX_LIGHTS; i++) {
            renderLight_t *light = tr_lightSystem.visibleLights[i];
            if (!light) {
                continue;
            }

            if (light->viewCount && light->viewCount != tr.viewCount) {
                continue;
            }

            if (RT_BuildDynamicFromRenderLight(light, &rt.dynamicLights[rt.numDynamicLights])) {
                appendedFromLightSystem = qtrue;
                rt.numDynamicLights++;
            }
        }
    }

    if (!appendedFromLightSystem && tr.refdef.num_dlights > 0) {
        int legacyCount = MIN(tr.refdef.num_dlights, RT_MAX_LIGHTS);
        for (int i = 0; i < legacyCount && rt.numDynamicLights < RT_MAX_LIGHTS; i++) {
            if (RT_BuildDynamicFromLegacyDlight(&tr.refdef.dlights[i], &rt.dynamicLights[rt.numDynamicLights])) {
                rt.numDynamicLights++;
            }
        }
    }

    RT_RebuildSceneLights();
}

static ID_INLINE float RT_Luminance(const float *rgb) {
    return 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
}

void RT_InitDenoiser(void) {
    rt.denoiseSigma = 0.25f;
    rt.denoiseThreshold = 0.5f;
}

void RT_ApplyTemporalFilter(float *current, float *history, float *output, int width, int height) {
    if (!current || !output || width <= 0 || height <= 0) {
        return;
    }

    size_t pixelCount = (size_t)width * height;
    const float minAlpha = 0.05f;

    for (size_t i = 0; i < pixelCount; i++) {
        int base = (int)(i * 3);
        int samples = (rt.sampleBuffer) ? rt.sampleBuffer[i] : 0;

        if (samples <= 0) {
            if (history) {
                output[base + 0] = history[base + 0];
                output[base + 1] = history[base + 1];
                output[base + 2] = history[base + 2];
            } else {
                output[base + 0] = current[base + 0];
                output[base + 1] = current[base + 1];
                output[base + 2] = current[base + 2];
            }
            continue;
        }

        float alpha = 1.0f / (float)samples;
        alpha = Com_Clamp(minAlpha, 1.0f, alpha);

        for (int c = 0; c < 3; c++) {
            float prev = history ? history[base + c] : current[base + c];
            float curr = current[base + c];
            output[base + c] = prev + alpha * (curr - prev);
        }
    }
}

void RT_ApplySpatialFilter(float *input, float *output, int width, int height) {
    if (!input || !output || width <= 0 || height <= 0) {
        return;
    }

    const int radius = 1;
    const float sigmaSpatial = 1.0f;
    const float sigmaColor = 0.25f;
    const float varianceInfluence = 0.5f;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int index = y * width + x;
            int base = index * 3;

            if (!rt.sampleBuffer || rt.sampleBuffer[index] <= 0) {
                output[base + 0] = input[base + 0];
                output[base + 1] = input[base + 1];
                output[base + 2] = input[base + 2];
                continue;
            }

            float centerColor[3] = {
                input[base + 0],
                input[base + 1],
                input[base + 2]
            };

            float centerLuma = RT_Luminance(centerColor);
            float centerVariance = 0.0f;

            if (rt.varianceBuffer && rt.sampleBuffer[index] > 1) {
                float varianceSum = rt.varianceBuffer[base + 0] +
                                    rt.varianceBuffer[base + 1] +
                                    rt.varianceBuffer[base + 2];
                centerVariance = varianceSum / (3.0f * (rt.sampleBuffer[index] - 1));
            }

            vec3_t accum = {0.0f, 0.0f, 0.0f};
            float weightSum = 0.0f;

            for (int dy = -radius; dy <= radius; dy++) {
                int ny = y + dy;
                if (ny < 0) ny = 0;
                if (ny >= height) ny = height - 1;

                for (int dx = -radius; dx <= radius; dx++) {
                    int nx = x + dx;
                    if (nx < 0) nx = 0;
                    if (nx >= width) nx = width - 1;

                    int nIndex = ny * width + nx;
                    int nBase = nIndex * 3;

                    if (!rt.sampleBuffer || rt.sampleBuffer[nIndex] <= 0) {
                        continue;
                    }

                    float neighborColor[3] = {
                        input[nBase + 0],
                        input[nBase + 1],
                        input[nBase + 2]
                    };
                    float neighborLuma = RT_Luminance(neighborColor);

                    float neighborVariance = 0.0f;
                    if (rt.varianceBuffer && rt.sampleBuffer[nIndex] > 1) {
                        float nVarianceSum = rt.varianceBuffer[nBase + 0] +
                                             rt.varianceBuffer[nBase + 1] +
                                             rt.varianceBuffer[nBase + 2];
                        neighborVariance = nVarianceSum / (3.0f * (rt.sampleBuffer[nIndex] - 1));
                    }

                    float spatialDist2 = (float)(dx * dx + dy * dy);
                    float wSpatial = expf(-spatialDist2 / (2.0f * sigmaSpatial * sigmaSpatial));

                    float colorDiff = neighborLuma - centerLuma;
                    float wColor = expf(-(colorDiff * colorDiff) / (2.0f * sigmaColor * sigmaColor + 1e-6f));

                    float varianceTerm = centerVariance + neighborVariance + 1e-6f;
                    float wVariance = 1.0f / (1.0f + varianceTerm * varianceInfluence);

                    float weight = wSpatial * wColor * wVariance;

                    accum[0] += neighborColor[0] * weight;
                    accum[1] += neighborColor[1] * weight;
                    accum[2] += neighborColor[2] * weight;
                    weightSum += weight;
                }
            }

            if (weightSum > 1e-5f) {
                output[base + 0] = accum[0] / weightSum;
                output[base + 1] = accum[1] / weightSum;
                output[base + 2] = accum[2] / weightSum;
            } else {
                output[base + 0] = centerColor[0];
                output[base + 1] = centerColor[1];
                output[base + 2] = centerColor[2];
            }
        }
    }
}

void RT_DenoiseFrame(float *input, float *output, int width, int height) {
    if (!input || !output || width <= 0 || height <= 0) {
        return;
    }

    size_t pixelCount = (size_t)width * height;
    size_t bytes = pixelCount * 3 * sizeof(float);

    int denoiseLevel = rt_denoise ? rt_denoise->integer : 0;
    if (denoiseLevel <= 0 || !rt.temporalEnabled) {
        if (output != input) {
            Com_Memcpy(output, input, bytes);
        }
        return;
    }

    float *historyCopy = NULL;
    if (output) {
        historyCopy = (float *)ri.Hunk_AllocateTempMemory(bytes);
        if (historyCopy) {
            Com_Memcpy(historyCopy, output, bytes);
        }
    }

    float *temp = (float *)ri.Hunk_AllocateTempMemory(bytes);
    if (!temp) {
        if (historyCopy) {
            ri.Hunk_FreeTempMemory(historyCopy);
        }
        if (output != input) {
            Com_Memcpy(output, input, bytes);
        }
        return;
    }

    RT_ApplyTemporalFilter(input, historyCopy, temp, width, height);

    if (denoiseLevel > 1) {
        RT_ApplySpatialFilter(temp, output, width, height);
    } else {
        Com_Memcpy(output, temp, bytes);
    }

    ri.Hunk_FreeTempMemory(temp);
    if (historyCopy) {
        ri.Hunk_FreeTempMemory(historyCopy);
    }
}

void RT_ClearLightCache(void) {}
/*
===============
RT_DrawDebugRays

Visualize path traced rays for debugging
===============
*/
void RT_DrawDebugRays(void) {
    if (!rt_debug || !rt_debug->integer || !rt.enabled) {
        return;
    }
    
    // Draw static light positions if in ALL mode
    if (rt.mode == RT_MODE_ALL) {
        for (int i = 0; i < MIN(rt.numStaticLights, 50); i++) {
            staticLight_t *sl = &rt.staticLights[i];
            
            // Draw light as a small sphere or marker
            // This would integrate with the debug rendering system
            // For now just log the info
            if (i < 5) { // Only log first 5 to avoid spam
                ri.Printf(PRINT_ALL, "Static Light %d: pos=(%.1f,%.1f,%.1f) color=(%.2f,%.2f,%.2f) intensity=%.1f radius=%.1f\n",
                          i, sl->origin[0], sl->origin[1], sl->origin[2],
                          sl->color[0], sl->color[1], sl->color[2],
                          sl->intensity, sl->radius);
            }
        }
    }
}
/*
================
RT_ComputeLightingAtPoint

Computes lighting at a specific point using path tracing
================
*/
void RT_ComputeLightingAtPoint(const vec3_t point, vec3_t result) {
    vec3_t accumulated;
    int numSamples = 8;  // Number of hemisphere samples
    
    VectorClear(result);
    
    if (!rt.enabled || rt.mode == RT_MODE_OFF) {
        return;
    }
    
    // Sample lighting from multiple directions
    for (int i = 0; i < numSamples; i++) {
        ray_t ray;
        hitInfo_t hit;
        vec3_t sampleDir;
        
        // Generate random direction on hemisphere
        float theta = 2.0f * M_PI * (i + random()) / numSamples;
        float phi = acos(1.0f - 2.0f * random());
        
        sampleDir[0] = sin(phi) * cos(theta);
        sampleDir[1] = sin(phi) * sin(theta);
        sampleDir[2] = cos(phi);
        
        // Create ray from point
        VectorCopy(point, ray.origin);
        VectorCopy(sampleDir, ray.direction);
        ray.tMin = 0.001f;
        ray.tMax = 1000.0f;
        ray.depth = 0;
        
        // Trace ray and accumulate lighting
        if (RT_TraceRay(&ray, &hit)) {
            vec3_t lighting;
            VectorClear(lighting);
            
            // Evaluate direct lighting at hit point
            RT_EvaluateDirectLighting(&hit, sampleDir, lighting);
            
            // Accumulate with cosine weighting
            float cosTheta = DotProduct(hit.normal, sampleDir);
            if (cosTheta > 0) {
                VectorMA(result, cosTheta / numSamples, lighting, result);
            }
        }
    }
    
    // Add ambient term
    VectorMA(result, 0.1f, colorWhite, result);
}

void RT_DrawProbeGrid(void) {}
void RT_DrawLightCache(void) {}
qboolean RT_RayBSPIntersect(const ray_t *ray, rtBspNode_t *node, hitInfo_t *hit) { return qfalse; }
