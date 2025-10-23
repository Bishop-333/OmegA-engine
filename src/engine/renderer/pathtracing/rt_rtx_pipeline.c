/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

RTX Ray Tracing Pipeline Management
Handles RT pipeline creation, shader binding table, and descriptor sets
===========================================================================
*/
// Force rebuild

#include "rt_rtx.h"
#include "rt_pathtracer.h"
#include "../core/tr_local.h"
#include "../vulkan/vk.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>


// External RTX state
extern rtxState_t rtx;
extern cvar_t *r_rtx_surface_debug;
extern cvar_t *r_rtx_debug;
extern VkBuffer RTX_GetMaterialBuffer(void);

// Pipeline management structures
typedef struct {
    VkShaderModule raygenShader;
    VkShaderModule missShader;
    VkShaderModule shadowMissShader;
    VkShaderModule closestHitShader;
    VkShaderModule rayQueryShader;
} rtxShaders_t;

typedef struct {
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkPipeline rayQueryPipeline;
} rtxPipelineInfo_t;

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceAddress deviceAddress;
    VkStridedDeviceAddressRegionKHR raygenRegion;
    VkStridedDeviceAddressRegionKHR missRegion;
    VkStridedDeviceAddressRegionKHR hitRegion;
    VkStridedDeviceAddressRegionKHR callableRegion;
    uint32_t handleSize;
    uint32_t handleSizeAligned;
    uint32_t groupCount;
} rtxSBT_t;

typedef struct {
    float origin[4];
    float direction[4];
    uint32_t occluded;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
} rtxShadowQueryGpu_t;

// Uniform buffer structures
typedef struct {
    float viewInverse[16];     // mat4
    float projInverse[16];     // mat4
    vec3_t position;
    float time;
    vec3_t forward;
    float fov;
    vec3_t right;
    float nearPlane;
    vec3_t up;
    float farPlane;
    vec2_t jitter;
    vec2_t previousJitter;
    float previousViewProjection[16];  // mat4
    uint32_t frameCount;
    uint32_t enablePathTracing;
    uint32_t maxBounces;
    uint32_t samplesPerPixel;
    uint32_t surfaceDebugMode;
    uint32_t _padSurfaceDebug[3];
} CameraUBO;

typedef struct {
    uint32_t enableShadows;
    uint32_t enableReflections;
    uint32_t enableGI;
    uint32_t enableAO;
    float shadowBias;
    float reflectionRoughnessCutoff;
    float giIntensity;
    float aoRadius;
    uint32_t debugMode;
    uint32_t enableDenoiser;
    uint32_t enableDLSS;
    uint32_t enableMotionBlur;
} RenderSettingsUBO;

// Debug options
typedef struct {
    uint32_t noTextures;
    uint32_t debugMode;
    uint32_t reserved1;
    uint32_t reserved2;
} DebugSettingsUBO;


typedef struct {
    vec3_t sunDirection;
    float sunIntensity;
    vec3_t sunColor;
    float skyIntensity;
    vec3_t fogColor;
    float fogDensity;
    float fogStart;
    float fogEnd;
    uint32_t useEnvironmentMap;
    uint32_t useProceduralSky;
    float time;
    float cloudCoverage;
} EnvironmentUBO;

// Material data for PBR
typedef struct {
    vec4_t albedo;
    vec4_t specular;
    vec4_t emission;
    float roughness;
    float metallic;
    float normalScale;
    float occlusionStrength;
    uint32_t albedoTexture;
    uint32_t normalTexture;
    uint32_t roughnessTexture;
    uint32_t metallicTexture;
    uint32_t emissionTexture;
    uint32_t occlusionTexture;
    uint32_t lightmapTexture;
    uint32_t flags;
} MaterialData;

// Global pipeline state
static struct {
    rtxShaders_t shaders;
    rtxPipelineInfo_t pipeline;
    rtxSBT_t sbt;
    
    // Descriptor resources
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    
    // Uniform buffers
    VkBuffer cameraUBO;
    VkDeviceMemory cameraUBOMemory;
    VkBuffer renderSettingsUBO;
    VkDeviceMemory renderSettingsUBOMemory;
    VkBuffer environmentUBO;
    VkDeviceMemory environmentUBOMemory;
    VkBuffer debugSettingsUBO;
    VkDeviceMemory debugSettingsUBOMemory;
    
    // Storage buffers
    VkBuffer instanceDataBuffer;
    VkDeviceMemory instanceDataBufferMemory;
    VkBuffer triangleMaterialBuffer;
    VkDeviceMemory triangleMaterialBufferMemory;
    uint32_t triangleMaterialCount;
    uint32_t triangleMaterialCapacity;
    VkBuffer rayQueryBuffer;
    VkDeviceMemory rayQueryBufferMemory;
    rtxShadowQueryGpu_t *rayQueryMapped;
    uint32_t rayQueryCapacity;
    
    // Texture arrays
    VkSampler textureSampler;
    uint32_t textureCount;
    VkImageView *textureViews;
    uint32_t lightmapCount;
    VkImageView *lightmapViews;
    
    // RT properties
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties;
} rtxPipeline;

// Function pointers for RT pipeline
static PFN_vkGetRayTracingShaderGroupHandlesKHR qvkGetRayTracingShaderGroupHandlesKHR;
static PFN_vkCmdTraceRaysKHR qvkCmdTraceRaysKHR;

static qboolean RTX_CreateRayQueryPipeline(VkDevice device);
static void RTX_DestroyRayQueryPipeline(void);
static void RTX_DestroyRayQueryBuffer(void);
static qboolean RTX_EnsureRayQueryCapacity(uint32_t count);

static void RTX_DestroyDescriptorSetLayoutSafe(VkDescriptorSetLayout *layout) {
    if (!layout || *layout == VK_NULL_HANDLE) {
        return;
    }

    if (qvkDestroyDescriptorSetLayout) {
        vkDestroyDescriptorSetLayout(vk.device, *layout, NULL);
    } else {
        ri.Printf(PRINT_WARNING, "RTX_ShutdownPipeline: qvkDestroyDescriptorSetLayout missing, skipping destroy\n");
    }

    *layout = VK_NULL_HANDLE;
}

/*
================
RTX_LoadShaderModule

Load compiled SPIR-V shader from disk
================
*/
static VkShaderModule RTX_LoadShaderModule(VkDevice device, const char *filename) {
    byte *shaderCode;
    int fileSize;
    VkShaderModule module = VK_NULL_HANDLE;
    
    // Attempt to load from RTX and compute shader directories
    const char *searchPaths[] = {
        "shaders/rtx/%s",
        "shaders/compute/%s"
    };
    char fullPath[MAX_QPATH];
    shaderCode = NULL;
    fileSize = 0;

    for (int i = 0; i < ARRAY_LEN(searchPaths); i++) {
        Com_sprintf(fullPath, sizeof(fullPath), searchPaths[i], filename);
        ri.Printf(PRINT_ALL, "RTX: Attempting to load shader: %s\n", fullPath);
        fileSize = ri.FS_ReadFile(fullPath, (void **)&shaderCode);
        if (fileSize > 0 && shaderCode) {
            ri.Printf(PRINT_ALL, "RTX: Successfully read %d bytes from %s\n", fileSize, filename);
            break;
        }
        if (shaderCode) {
            ri.FS_FreeFile(shaderCode);
            shaderCode = NULL;
        }
    }

    if (fileSize <= 0 || !shaderCode) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to open shader file: %s\n", filename);
        return VK_NULL_HANDLE;
    }
    
    // Create shader module
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fileSize,
        .pCode = (const uint32_t *)shaderCode
    };
    
    VkResult result = vkCreateShaderModule(device, &createInfo, NULL, &module);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: vkCreateShaderModule failed for %s (VkResult: %d, codeSize: %d)\n",
                  filename, result, fileSize);
        module = VK_NULL_HANDLE;
    } else {
        ri.Printf(PRINT_ALL, "RTX: Successfully created shader module for %s (%d bytes, handle=%p)\n",
                  filename, fileSize, (void*)module);
    }
    
    ri.FS_FreeFile(shaderCode);
    return module;
}

/*
================
RTX_CreateDescriptorSetLayout

Create descriptor set layout for RT resources
================
*/
static qboolean RTX_CreateDescriptorSetLayout(VkDevice device) {
    VkDescriptorSetLayoutBinding bindings[] = {
        // Binding 0: TLAS
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT
        },
        // Binding 1: Output color image
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        },
        // Binding 2: Albedo image
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        },
        // Binding 3: Normal image
        {
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        },
        // Binding 4: Motion vector image
        {
            .binding = 4,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        },
        // Binding 5: Depth image
        {
            .binding = 5,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        },
        // Binding 6: Camera UBO
        {
            .binding = 6,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT
        },
        // Binding 7: Render settings UBO
        {
            .binding = 7,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT
        },
        // Binding 8: Environment map
        {
            .binding = 8,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_MISS_BIT_KHR
        },
        // Binding 9: Environment data UBO
        {
            .binding = 9,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT
        },
        // Binding 10: Instance data buffer
        {
            .binding = 10,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT
        },
        // Binding 11: Material buffer
        {
            .binding = 11,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT
        },
        // Binding 12: Texture array
        {
            .binding = 12,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 256,  // Max textures
            .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
        },
        // Binding 13: Lightmap array
        {
            .binding = 13,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 64,   // Max lightmaps
            .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
        },
        // Binding 14: Light buffer
        {
            .binding = 14,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT
        },
        // Binding 15: Direct light contribution image
        {
            .binding = 15,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        },
        // Binding 16: Indirect light contribution image
        {
            .binding = 16,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        },
        // Binding 17: Lightmap contribution image
        {
            .binding = 17,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        },
        {
            .binding = 18,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 19,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 20,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT |
                          VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                          VK_SHADER_STAGE_RAYGEN_BIT_KHR
        }
    };
    
    // Enable variable descriptor counts for texture arrays
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = ARRAY_LEN(bindings)
    };
    
    VkDescriptorBindingFlags flags[ARRAY_LEN(bindings)] = {0};
    // Only allow partially bound for texture arrays, not variable count
    // since they're not the highest binding number
    flags[12] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    flags[13] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    flags[20] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    bindingFlags.pBindingFlags = flags;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &bindingFlags,
        .bindingCount = ARRAY_LEN(bindings),
        .pBindings = bindings
    };
    
    VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, 
                                                  &rtxPipeline.pipeline.descriptorSetLayout);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create descriptor set layout (result: %d)\n", result);
        return qfalse;
    }
    
    return qtrue;
}

/*
================
RTX_CreateDescriptorPool

Create descriptor pool for RT resources
================
*/
static qboolean RTX_CreateDescriptorPool(VkDevice device) {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 321 }, // 256 + 64 + 1
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 }
    };
    
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1,
        .poolSizeCount = ARRAY_LEN(poolSizes),
        .pPoolSizes = poolSizes
    };
    
    VkResult result = vkCreateDescriptorPool(device, &poolInfo, NULL, &rtxPipeline.descriptorPool);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create descriptor pool (result: %d)\n", result);
        return qfalse;
    }
    
    return qtrue;
}

/*
================
RTX_AllocateDescriptorSets

Allocate descriptor sets from pool
================
*/
static qboolean RTX_AllocateDescriptorSets(VkDevice device) {
    // No variable descriptor count needed since we're not using 
    // VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
    
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = NULL,
        .descriptorPool = rtxPipeline.descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &rtxPipeline.pipeline.descriptorSetLayout
    };
    
    VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &rtxPipeline.descriptorSet);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to allocate descriptor sets (result: %d)\n", result);
        return qfalse;
    }
    
    return qtrue;
}

/*
================
RTX_CreateUniformBuffers

Create uniform buffers for camera, settings, and environment
================
*/
static qboolean RTX_CreateUniformBuffers(VkDevice device, VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    
    // Camera UBO
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(CameraUBO),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    
    if (vkCreateBuffer(device, &bufferInfo, NULL, &rtxPipeline.cameraUBO) != VK_SUCCESS) {
        return qfalse;
    }
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, rtxPipeline.cameraUBO, &memReqs);
    
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = vk_find_memory_type(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    
    if (vkAllocateMemory(device, &allocInfo, NULL, &rtxPipeline.cameraUBOMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, rtxPipeline.cameraUBO, NULL);
        return qfalse;
    }
    
    vkBindBufferMemory(device, rtxPipeline.cameraUBO, rtxPipeline.cameraUBOMemory, 0);
    
    // Render settings UBO
    bufferInfo.size = sizeof(RenderSettingsUBO);
    if (vkCreateBuffer(device, &bufferInfo, NULL, &rtxPipeline.renderSettingsUBO) != VK_SUCCESS) {
        return qfalse;
    }
    
    vkGetBufferMemoryRequirements(device, rtxPipeline.renderSettingsUBO, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    
    if (vkAllocateMemory(device, &allocInfo, NULL, &rtxPipeline.renderSettingsUBOMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, rtxPipeline.renderSettingsUBO, NULL);
        return qfalse;
    }
    
    vkBindBufferMemory(device, rtxPipeline.renderSettingsUBO, rtxPipeline.renderSettingsUBOMemory, 0);
    
    // Environment UBO
    bufferInfo.size = sizeof(EnvironmentUBO);
    if (vkCreateBuffer(device, &bufferInfo, NULL, &rtxPipeline.environmentUBO) != VK_SUCCESS) {
        return qfalse;
    }
    
    vkGetBufferMemoryRequirements(device, rtxPipeline.environmentUBO, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    
    if (vkAllocateMemory(device, &allocInfo, NULL, &rtxPipeline.environmentUBOMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, rtxPipeline.environmentUBO, NULL);
        return qfalse;
    }
    
    vkBindBufferMemory(device, rtxPipeline.environmentUBO, rtxPipeline.environmentUBOMemory, 0);
    
    // Debug settings UBO
    bufferInfo.size = sizeof(DebugSettingsUBO);
    if (vkCreateBuffer(device, &bufferInfo, NULL, &rtxPipeline.debugSettingsUBO) != VK_SUCCESS) {
        return qfalse;
    }

    vkGetBufferMemoryRequirements(device, rtxPipeline.debugSettingsUBO, &memReqs);
    allocInfo.allocationSize = memReqs.size;

    if (vkAllocateMemory(device, &allocInfo, NULL, &rtxPipeline.debugSettingsUBOMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, rtxPipeline.debugSettingsUBO, NULL);
        return qfalse;
    }

    vkBindBufferMemory(device, rtxPipeline.debugSettingsUBO, rtxPipeline.debugSettingsUBOMemory, 0);

    return qtrue;
}

/*
================
RTX_CreateStorageBuffers

Create storage buffers for materials, lights, and instance data
================
*/
static qboolean RTX_CreateStorageBuffers(VkDevice device, VkPhysicalDevice physicalDevice) {
    (void)physicalDevice;
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(uint64_t) * 8 * RTX_MAX_INSTANCES,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(device, &bufferInfo, NULL, &rtxPipeline.instanceDataBuffer) != VK_SUCCESS) {
        return qfalse;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, rtxPipeline.instanceDataBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = vk_find_memory_type(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };

    if (vkAllocateMemory(device, &allocInfo, NULL, &rtxPipeline.instanceDataBufferMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, rtxPipeline.instanceDataBuffer, NULL);
        rtxPipeline.instanceDataBuffer = VK_NULL_HANDLE;
        return qfalse;
    }

    vkBindBufferMemory(device, rtxPipeline.instanceDataBuffer, rtxPipeline.instanceDataBufferMemory, 0);

    return qtrue;
}

/*
================
RTX_CreateTextureSampler

Create sampler for texture arrays
================
*/
static qboolean RTX_CreateTextureSampler(VkDevice device) {
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 16.0f,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };
    
    VkResult result = vkCreateSampler(device, &samplerInfo, NULL, &rtxPipeline.textureSampler);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create texture sampler (result: %d)\n", result);
        return qfalse;
    }
    
    return qtrue;
}

/*
================
RTX_CreateRTPipeline

Create ray tracing pipeline with all shader stages
================
*/
qboolean RTX_CreateRTPipeline(VkDevice device, VkPhysicalDevice physicalDevice) {
    VkResult result;
    
    // Load RT extension function pointers
    PFN_vkCreateRayTracingPipelinesKHR createPipelines = (PFN_vkCreateRayTracingPipelinesKHR)
        vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR");
    qvkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)
        vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR");
    qvkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)
        vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR");
    
    if (!createPipelines || !qvkGetRayTracingShaderGroupHandlesKHR || !qvkCmdTraceRaysKHR) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to load RT pipeline function pointers\n");
        return qfalse;
    }
    vk_register_ray_tracing_pipeline_dispatch(createPipelines);
    
    // Get RT pipeline properties
    rtxPipeline.rtProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &rtxPipeline.rtProperties
    };
    vkGetPhysicalDeviceProperties2(physicalDevice, &props2);
    
    // Load shader modules
    rtxPipeline.shaders.raygenShader = RTX_LoadShaderModule(device, "raygen.spv");
    rtxPipeline.shaders.missShader = RTX_LoadShaderModule(device, "miss.spv");
    rtxPipeline.shaders.shadowMissShader = RTX_LoadShaderModule(device, "shadow.spv");
    rtxPipeline.shaders.closestHitShader = RTX_LoadShaderModule(device, "closesthit.spv");
    
    if (!rtxPipeline.shaders.raygenShader || !rtxPipeline.shaders.missShader ||
        !rtxPipeline.shaders.shadowMissShader || !rtxPipeline.shaders.closestHitShader) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to load one or more shader modules\n");
        return qfalse;
    }
    
    // Create shader stages
    VkPipelineShaderStageCreateInfo shaderStages[] = {
        // Ray generation
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .module = rtxPipeline.shaders.raygenShader,
            .pName = "main"
        },
        // Miss
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
            .module = rtxPipeline.shaders.missShader,
            .pName = "main"
        },
        // Shadow miss
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
            .module = rtxPipeline.shaders.shadowMissShader,
            .pName = "main"
        },
        // Closest hit
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            .module = rtxPipeline.shaders.closestHitShader,
            .pName = "main"
        }
    };
    
    // Create shader groups
    VkRayTracingShaderGroupCreateInfoKHR shaderGroups[] = {
        // Group 0: Ray generation
        {
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
            .generalShader = 0,
            .closestHitShader = VK_SHADER_UNUSED_KHR,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        },
        // Group 1: Miss
        {
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
            .generalShader = 1,
            .closestHitShader = VK_SHADER_UNUSED_KHR,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        },
        // Group 2: Shadow miss
        {
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
            .generalShader = 2,
            .closestHitShader = VK_SHADER_UNUSED_KHR,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        },
        // Group 3: Hit
        {
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
            .generalShader = VK_SHADER_UNUSED_KHR,
            .closestHitShader = 3,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        }
    };
    
    // Create descriptor set layout
    if (!RTX_CreateDescriptorSetLayout(device)) {
        return qfalse;
    }
    
    // Create pipeline layout (shared between RT and compute pipelines)
    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(uint32_t)
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &rtxPipeline.pipeline.descriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };
    
    result = vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, 
                                    &rtxPipeline.pipeline.pipelineLayout);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create pipeline layout (result: %d)\n", result);
        return qfalse;
    }
    
    // Create ray tracing pipeline
    int reqRecursion = rtx_gi_bounces ? rtx_gi_bounces->integer : 2;
    if (reqRecursion < 1) reqRecursion = 1;
    if (rtxPipeline.rtProperties.maxRayRecursionDepth > 0 && reqRecursion > (int)rtxPipeline.rtProperties.maxRayRecursionDepth)
        reqRecursion = (int)rtxPipeline.rtProperties.maxRayRecursionDepth;
    VkRayTracingPipelineCreateInfoKHR pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
        .stageCount = ARRAY_LEN(shaderStages),
        .pStages = shaderStages,
        .groupCount = ARRAY_LEN(shaderGroups),
        .pGroups = shaderGroups,
        .maxPipelineRayRecursionDepth = (uint32_t)reqRecursion,
        .layout = rtxPipeline.pipeline.pipelineLayout
    };
    
    ri.Printf(PRINT_ALL, "RTX: Creating ray tracing pipeline with %d stages, %d groups, max recursion %d\n",
              pipelineInfo.stageCount, pipelineInfo.groupCount, pipelineInfo.maxPipelineRayRecursionDepth);

    result = qvkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1,
                                             &pipelineInfo, NULL, &rtxPipeline.pipeline.pipeline);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: vkCreateRayTracingPipelinesKHR failed with VkResult %d\n", result);
        // Add more detailed error info
        switch(result) {
            case VK_ERROR_OUT_OF_HOST_MEMORY:
                ri.Printf(PRINT_WARNING, "RTX: Out of host memory\n");
                break;
            case VK_ERROR_OUT_OF_DEVICE_MEMORY:
                ri.Printf(PRINT_WARNING, "RTX: Out of device memory\n");
                break;
            case VK_ERROR_INVALID_SHADER_NV:
                ri.Printf(PRINT_WARNING, "RTX: Invalid shader\n");
                break;
            default:
                ri.Printf(PRINT_WARNING, "RTX: Unknown error\n");
                break;
        }
        return qfalse;
    }

    ri.Printf(PRINT_ALL, "RTX: Ray tracing pipeline created successfully (handle=%p)\n",
              (void*)rtxPipeline.pipeline.pipeline);

    if (!RTX_CreateRayQueryPipeline(device)) {
        ri.Printf(PRINT_WARNING, "RTX: Ray query compute pipeline not available; CPU fallback will be used\n");
    }

    return qtrue;
}

static qboolean RTX_CreateRayQueryPipeline(VkDevice device) {
    if (!(rtx.features & RTX_FEATURE_RAY_QUERY)) {
        return qtrue;
    }

    if (rtxPipeline.pipeline.rayQueryPipeline != VK_NULL_HANDLE) {
        return qtrue;
    }

    if (!rtxPipeline.shaders.rayQueryShader) {
        rtxPipeline.shaders.rayQueryShader = RTX_LoadShaderModule(device, "shadow_queries.spv");
        if (!rtxPipeline.shaders.rayQueryShader) {
            ri.Printf(PRINT_WARNING, "RTX: Failed to load shadow query shader module\n");
            return qfalse;
        }
    }

    VkPipelineShaderStageCreateInfo stageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = rtxPipeline.shaders.rayQueryShader,
        .pName = "main"
    };

    VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = stageInfo,
        .layout = rtxPipeline.pipeline.pipelineLayout
    };

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                               NULL, &rtxPipeline.pipeline.rayQueryPipeline);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create ray query compute pipeline (result: %d)\n", result);
        if (rtxPipeline.pipeline.rayQueryPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, rtxPipeline.pipeline.rayQueryPipeline, NULL);
            rtxPipeline.pipeline.rayQueryPipeline = VK_NULL_HANDLE;
        }
        return qfalse;
    }

    return qtrue;
}

static void RTX_DestroyRayQueryPipeline(void) {
    if (rtxPipeline.pipeline.rayQueryPipeline) {
        vkDestroyPipeline(vk.device, rtxPipeline.pipeline.rayQueryPipeline, NULL);
        rtxPipeline.pipeline.rayQueryPipeline = VK_NULL_HANDLE;
    }
    if (rtxPipeline.shaders.rayQueryShader) {
        vkDestroyShaderModule(vk.device, rtxPipeline.shaders.rayQueryShader, NULL);
        rtxPipeline.shaders.rayQueryShader = VK_NULL_HANDLE;
    }
}

static void RTX_DestroyRayQueryBuffer(void) {
    if (rtxPipeline.rayQueryMapped && rtxPipeline.rayQueryBufferMemory) {
        vkUnmapMemory(vk.device, rtxPipeline.rayQueryBufferMemory);
        rtxPipeline.rayQueryMapped = NULL;
    }

    if (rtxPipeline.rayQueryBuffer) {
        vkDestroyBuffer(vk.device, rtxPipeline.rayQueryBuffer, NULL);
        rtxPipeline.rayQueryBuffer = VK_NULL_HANDLE;
    }
    if (rtxPipeline.rayQueryBufferMemory) {
        vkFreeMemory(vk.device, rtxPipeline.rayQueryBufferMemory, NULL);
        rtxPipeline.rayQueryBufferMemory = VK_NULL_HANDLE;
    }
    rtxPipeline.rayQueryCapacity = 0;
}

static qboolean RTX_EnsureRayQueryCapacity(uint32_t count) {
    if (!(rtx.features & RTX_FEATURE_RAY_QUERY)) {
        return qfalse;
    }

    if (count == 0) {
        return qtrue;
    }

    if (rtxPipeline.rayQueryCapacity >= count && rtxPipeline.rayQueryBuffer != VK_NULL_HANDLE) {
        return qtrue;
    }

    uint32_t newCapacity = rtxPipeline.rayQueryCapacity ? rtxPipeline.rayQueryCapacity : 64;
    while (newCapacity < count) {
        newCapacity = (newCapacity < UINT32_MAX / 2) ? newCapacity * 2 : count;
        if (newCapacity >= count) {
            break;
        }
    }

    RTX_DestroyRayQueryBuffer();

    VkDeviceSize bufferSize = sizeof(rtxShadowQueryGpu_t) * (VkDeviceSize)newCapacity;

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vk.device, &bufferInfo, NULL, &rtxPipeline.rayQueryBuffer) != VK_SUCCESS) {
        return qfalse;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(vk.device, rtxPipeline.rayQueryBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = vk_find_memory_type(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };

    if (vkAllocateMemory(vk.device, &allocInfo, NULL, &rtxPipeline.rayQueryBufferMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vk.device, rtxPipeline.rayQueryBuffer, NULL);
        rtxPipeline.rayQueryBuffer = VK_NULL_HANDLE;
        return qfalse;
    }

    vkBindBufferMemory(vk.device, rtxPipeline.rayQueryBuffer, rtxPipeline.rayQueryBufferMemory, 0);

    if (vkMapMemory(vk.device, rtxPipeline.rayQueryBufferMemory, 0, VK_WHOLE_SIZE, 0,
                    (void**)&rtxPipeline.rayQueryMapped) != VK_SUCCESS) {
        RTX_DestroyRayQueryBuffer();
        return qfalse;
    }

    rtxPipeline.rayQueryCapacity = newCapacity;

    VkDescriptorBufferInfo bufferDesc = {
        .buffer = rtxPipeline.rayQueryBuffer,
        .offset = 0,
        .range = bufferSize
    };

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = rtxPipeline.descriptorSet,
        .dstBinding = 19,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &bufferDesc
    };

    vkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);

    return qtrue;
}

/*
================
RTX_CreateShaderBindingTable

Create and populate shader binding table
================
*/
qboolean RTX_CreateShaderBindingTable(VkDevice device, VkPhysicalDevice physicalDevice) {
    VkResult result;
    
    // Calculate SBT properties
    uint32_t handleSize = rtxPipeline.rtProperties.shaderGroupHandleSize;
    uint32_t handleAlignment = rtxPipeline.rtProperties.shaderGroupHandleAlignment;
    uint32_t baseAlignment = rtxPipeline.rtProperties.shaderGroupBaseAlignment;
    
    rtxPipeline.sbt.handleSize = handleSize;
    // Ensure handleSizeAligned is at least baseAlignment to maintain alignment for all regions
    rtxPipeline.sbt.handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);
    if (rtxPipeline.sbt.handleSizeAligned < baseAlignment) {
        rtxPipeline.sbt.handleSizeAligned = baseAlignment;
    }
    rtxPipeline.sbt.groupCount = 4; // raygen, miss, shadow miss, hit
    
    ri.Printf(PRINT_ALL, "RTX: SBT Alignment - handleSize: %u, handleAlignment: %u, baseAlignment: %u, handleSizeAligned: %u\n",
              handleSize, handleAlignment, baseAlignment, rtxPipeline.sbt.handleSizeAligned);
    
    // Calculate SBT buffer size
    uint32_t sbtSize = rtxPipeline.sbt.groupCount * rtxPipeline.sbt.handleSizeAligned;
    sbtSize = (sbtSize + baseAlignment - 1) & ~(baseAlignment - 1);
    
    // Create SBT buffer
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sbtSize,
        .usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | 
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    
    result = vkCreateBuffer(device, &bufferInfo, NULL, &rtxPipeline.sbt.buffer);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create SBT buffer (result: %d)\n", result);
        return qfalse;
    }
    
    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, rtxPipeline.sbt.buffer, &memReqs);
    
    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    };
    
    // Ensure allocation is large enough to allow alignment
    VkDeviceSize alignedSize = memReqs.size + baseAlignment;
    
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &memoryAllocateFlagsInfo,
        .allocationSize = alignedSize,
        .memoryTypeIndex = vk_find_memory_type(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    
    result = vkAllocateMemory(device, &allocInfo, NULL, &rtxPipeline.sbt.memory);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to allocate SBT memory (result: %d)\n", result);
        vkDestroyBuffer(device, rtxPipeline.sbt.buffer, NULL);
        return qfalse;
    }
    
    vkBindBufferMemory(device, rtxPipeline.sbt.buffer, rtxPipeline.sbt.memory, 0);
    
    // Get shader group handles
    uint8_t *shaderHandles = Z_Malloc(handleSize * rtxPipeline.sbt.groupCount);
    result = qvkGetRayTracingShaderGroupHandlesKHR(device, rtxPipeline.pipeline.pipeline,
                                                   0, rtxPipeline.sbt.groupCount,
                                                   handleSize * rtxPipeline.sbt.groupCount,
                                                   shaderHandles);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to get shader group handles (result: %d)\n", result);
        Z_Free(shaderHandles);
        return qfalse;
    }
    
    // Map SBT memory and copy handles
    void *mapped;
    result = vkMapMemory(device, rtxPipeline.sbt.memory, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to map SBT memory (result: %d)\n", result);
        Z_Free(shaderHandles);
        return qfalse;
    }
    
    // Copy shader handles with proper alignment
    uint8_t *pData = (uint8_t*)mapped;
    for (uint32_t i = 0; i < rtxPipeline.sbt.groupCount; i++) {
        Com_Memcpy(pData + i * rtxPipeline.sbt.handleSizeAligned, 
                   shaderHandles + i * handleSize, handleSize);
    }
    
    vkUnmapMemory(device, rtxPipeline.sbt.memory);
    Z_Free(shaderHandles);
    
    // Get the buffer's device address
    VkDeviceAddress rawAddress = RTX_GetBufferDeviceAddressVK(rtxPipeline.sbt.buffer);
    if (!rawAddress) rawAddress = RTX_GetBufferDeviceAddress(rtxPipeline.sbt.buffer);
    
    // Since the buffer is already sized and aligned properly, use the raw address directly
    rtxPipeline.sbt.deviceAddress = rawAddress;
    
    // Setup strided device address regions
    rtxPipeline.sbt.raygenRegion = (VkStridedDeviceAddressRegionKHR){
        .deviceAddress = rtxPipeline.sbt.deviceAddress,
        .stride = rtxPipeline.sbt.handleSizeAligned,
        .size = rtxPipeline.sbt.handleSizeAligned
    };
    
    rtxPipeline.sbt.missRegion = (VkStridedDeviceAddressRegionKHR){
        .deviceAddress = rtxPipeline.sbt.deviceAddress + rtxPipeline.sbt.handleSizeAligned,
        .stride = rtxPipeline.sbt.handleSizeAligned,
        .size = rtxPipeline.sbt.handleSizeAligned * 2  // 2 miss shaders
    };
    
    rtxPipeline.sbt.hitRegion = (VkStridedDeviceAddressRegionKHR){
        .deviceAddress = rtxPipeline.sbt.deviceAddress + rtxPipeline.sbt.handleSizeAligned * 3,
        .stride = rtxPipeline.sbt.handleSizeAligned,
        .size = rtxPipeline.sbt.handleSizeAligned
    };
    
    ri.Printf(PRINT_ALL, "RTX: SBT Addresses - base: 0x%llx, raygen: 0x%llx, miss: 0x%llx, hit: 0x%llx\n",
              (unsigned long long)rtxPipeline.sbt.deviceAddress,
              (unsigned long long)rtxPipeline.sbt.raygenRegion.deviceAddress,
              (unsigned long long)rtxPipeline.sbt.missRegion.deviceAddress,
              (unsigned long long)rtxPipeline.sbt.hitRegion.deviceAddress);
    ri.Printf(PRINT_ALL, "RTX: Miss address %% 64 = %llu\n", 
              (unsigned long long)(rtxPipeline.sbt.missRegion.deviceAddress % 64));
    
    rtxPipeline.sbt.callableRegion = (VkStridedDeviceAddressRegionKHR){0};
    
    ri.Printf(PRINT_ALL, "RTX: Shader binding table created (size: %u bytes)\n", sbtSize);
    return qtrue;
}

/*
================
RTX_InitializePipeline

Initialize entire RT pipeline system
================
*/
qboolean RTX_InitializePipeline(void) {
    // Check if already initialized
    if (rtxPipeline.pipeline.pipeline != VK_NULL_HANDLE) {
        ri.Printf(PRINT_ALL, "RTX: Pipeline already initialized, skipping re-initialization\n");
        return qtrue;
    }

    ri.Printf(PRINT_ALL, "RTX: Initializing RT pipeline for the first time\n");

    if (!vk.device || !vk.physical_device) {
        ri.Printf(PRINT_WARNING, "RTX: Vulkan device not initialized\n");
        return qfalse;
    }

    // Create descriptor pool
    ri.Printf(PRINT_ALL, "RTX: Creating descriptor pool...\n");
    if (!RTX_CreateDescriptorPool(vk.device)) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create descriptor pool\n");
        return qfalse;
    }
    ri.Printf(PRINT_ALL, "RTX: Descriptor pool created successfully\n");

    // Create RT pipeline
    ri.Printf(PRINT_ALL, "RTX: Creating RT pipeline...\n");
    if (!RTX_CreateRTPipeline(vk.device, vk.physical_device)) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create RT pipeline\n");
        return qfalse;
    }
    ri.Printf(PRINT_ALL, "RTX: RT pipeline created successfully (handle=%p)\n", (void*)rtxPipeline.pipeline.pipeline);

    // Create shader binding table
    ri.Printf(PRINT_ALL, "RTX: Creating shader binding table...\n");
    if (!RTX_CreateShaderBindingTable(vk.device, vk.physical_device)) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create shader binding table\n");
        return qfalse;
    }
    ri.Printf(PRINT_ALL, "RTX: Shader binding table created successfully\n");

    // Allocate descriptor sets
    ri.Printf(PRINT_ALL, "RTX: Allocating descriptor sets...\n");
    if (!RTX_AllocateDescriptorSets(vk.device)) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to allocate descriptor sets\n");
        return qfalse;
    }
    ri.Printf(PRINT_ALL, "RTX: Descriptor sets allocated successfully\n");

    // Create uniform buffers
    ri.Printf(PRINT_ALL, "RTX: Creating uniform buffers...\n");
    if (!RTX_CreateUniformBuffers(vk.device, vk.physical_device)) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create uniform buffers\n");
        return qfalse;
    }
    ri.Printf(PRINT_ALL, "RTX: Uniform buffers created successfully\n");

    // Create storage buffers
    ri.Printf(PRINT_ALL, "RTX: Creating storage buffers...\n");
    if (!RTX_CreateStorageBuffers(vk.device, vk.physical_device)) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create storage buffers\n");
        return qfalse;
    }
    ri.Printf(PRINT_ALL, "RTX: Storage buffers created successfully\n");

    // Create texture sampler
    ri.Printf(PRINT_ALL, "RTX: Creating texture sampler...\n");
    if (!RTX_CreateTextureSampler(vk.device)) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create texture sampler\n");
        return qfalse;
    }
    ri.Printf(PRINT_ALL, "RTX: Texture sampler created successfully\n");
    
    ri.Printf(PRINT_ALL, "RTX: Pipeline system initialized successfully\n");
    return qtrue;
}

/*
================
RTX_ShutdownPipeline

Cleanup RT pipeline resources
================
*/
void RTX_ShutdownPipeline(void) {
    if (!vk.device) {
        return;
    }
    
    vkDeviceWaitIdle(vk.device);

    RTX_DestroyRayQueryPipeline();
    RTX_DestroyRayQueryBuffer();
    
    // Destroy shader modules
    if (rtxPipeline.shaders.raygenShader) {
        vkDestroyShaderModule(vk.device, rtxPipeline.shaders.raygenShader, NULL);
        rtxPipeline.shaders.raygenShader = VK_NULL_HANDLE;
    }
    if (rtxPipeline.shaders.missShader) {
        vkDestroyShaderModule(vk.device, rtxPipeline.shaders.missShader, NULL);
        rtxPipeline.shaders.missShader = VK_NULL_HANDLE;
    }
    if (rtxPipeline.shaders.shadowMissShader) {
        vkDestroyShaderModule(vk.device, rtxPipeline.shaders.shadowMissShader, NULL);
        rtxPipeline.shaders.shadowMissShader = VK_NULL_HANDLE;
    }
    if (rtxPipeline.shaders.closestHitShader) {
        vkDestroyShaderModule(vk.device, rtxPipeline.shaders.closestHitShader, NULL);
        rtxPipeline.shaders.closestHitShader = VK_NULL_HANDLE;
    }
    
    // Destroy pipeline
    if (rtxPipeline.pipeline.pipeline) {
        vkDestroyPipeline(vk.device, rtxPipeline.pipeline.pipeline, NULL);
        rtxPipeline.pipeline.pipeline = VK_NULL_HANDLE;
    }
    if (rtxPipeline.pipeline.pipelineLayout) {
        vkDestroyPipelineLayout(vk.device, rtxPipeline.pipeline.pipelineLayout, NULL);
        rtxPipeline.pipeline.pipelineLayout = VK_NULL_HANDLE;
    }
    RTX_DestroyDescriptorSetLayoutSafe(&rtxPipeline.pipeline.descriptorSetLayout);
    
    // Destroy SBT
    if (rtxPipeline.sbt.buffer) {
        vkDestroyBuffer(vk.device, rtxPipeline.sbt.buffer, NULL);
        rtxPipeline.sbt.buffer = VK_NULL_HANDLE;
    }
    if (rtxPipeline.sbt.memory) {
        vkFreeMemory(vk.device, rtxPipeline.sbt.memory, NULL);
        rtxPipeline.sbt.memory = VK_NULL_HANDLE;
    }
    
    // Destroy descriptor pool
    if (rtxPipeline.descriptorPool) {
        vkDestroyDescriptorPool(vk.device, rtxPipeline.descriptorPool, NULL);
        rtxPipeline.descriptorPool = VK_NULL_HANDLE;
    }
    
    // Destroy uniform buffers
    if (rtxPipeline.cameraUBO) {
        vkDestroyBuffer(vk.device, rtxPipeline.cameraUBO, NULL);
        vkFreeMemory(vk.device, rtxPipeline.cameraUBOMemory, NULL);
        rtxPipeline.cameraUBO = VK_NULL_HANDLE;
        rtxPipeline.cameraUBOMemory = VK_NULL_HANDLE;
    }
    if (rtxPipeline.renderSettingsUBO) {
        vkDestroyBuffer(vk.device, rtxPipeline.renderSettingsUBO, NULL);
        vkFreeMemory(vk.device, rtxPipeline.renderSettingsUBOMemory, NULL);
        rtxPipeline.renderSettingsUBO = VK_NULL_HANDLE;
        rtxPipeline.renderSettingsUBOMemory = VK_NULL_HANDLE;
    }
    if (rtxPipeline.environmentUBO) {
        vkDestroyBuffer(vk.device, rtxPipeline.environmentUBO, NULL);
        vkFreeMemory(vk.device, rtxPipeline.environmentUBOMemory, NULL);
        rtxPipeline.environmentUBO = VK_NULL_HANDLE;
        rtxPipeline.environmentUBOMemory = VK_NULL_HANDLE;
    }
    
    // Destroy storage buffers
    if (rtxPipeline.instanceDataBuffer) {
        vkDestroyBuffer(vk.device, rtxPipeline.instanceDataBuffer, NULL);
        vkFreeMemory(vk.device, rtxPipeline.instanceDataBufferMemory, NULL);
        rtxPipeline.instanceDataBuffer = VK_NULL_HANDLE;
        rtxPipeline.instanceDataBufferMemory = VK_NULL_HANDLE;
    }
    if (rtxPipeline.triangleMaterialBuffer) {
        vkDestroyBuffer(vk.device, rtxPipeline.triangleMaterialBuffer, NULL);
        rtxPipeline.triangleMaterialBuffer = VK_NULL_HANDLE;
    }
    if (rtxPipeline.triangleMaterialBufferMemory) {
        vkFreeMemory(vk.device, rtxPipeline.triangleMaterialBufferMemory, NULL);
        rtxPipeline.triangleMaterialBufferMemory = VK_NULL_HANDLE;
    }
    rtxPipeline.triangleMaterialCount = 0;
    rtxPipeline.triangleMaterialCapacity = 0;

    // Destroy sampler
    if (rtxPipeline.textureSampler) {
        vkDestroySampler(vk.device, rtxPipeline.textureSampler, NULL);
        rtxPipeline.textureSampler = VK_NULL_HANDLE;
    }

    Com_Memset(&rtxPipeline, 0, sizeof(rtxPipeline));
    ri.Printf(PRINT_ALL, "RTX: Pipeline shutdown complete\n");
}

/*
================
RTX_GetPipeline

Get current RT pipeline
================
*/
VkPipeline RTX_GetPipeline(void) {
    return rtxPipeline.pipeline.pipeline;
}

/*
================
RTX_GetPipelineLayout

Get current RT pipeline layout
================
*/
VkPipelineLayout RTX_GetPipelineLayout(void) {
    return rtxPipeline.pipeline.pipelineLayout;
}

/*
================
RTX_GetDescriptorSet

Get current RT descriptor set
================
*/
VkDescriptorSet RTX_GetDescriptorSet(void) {
    return rtxPipeline.descriptorSet;
}

VkPipeline RTX_GetRayQueryPipelineHandle(void) {
    return rtxPipeline.pipeline.rayQueryPipeline;
}

VkBuffer RTX_RayQueryGetBuffer(void) {
    return rtxPipeline.rayQueryBuffer;
}

VkDeviceSize RTX_RayQueryRecordSize(void) {
    return sizeof(rtxShadowQueryGpu_t);
}

qboolean RTX_RayQueryUpload(const rtShadowQuery_t *queries, int count) {
    if (!queries || count <= 0) {
        return qfalse;
    }

    if (!RTX_EnsureRayQueryCapacity((uint32_t)count)) {
        return qfalse;
    }

    if (!rtxPipeline.rayQueryMapped) {
        return qfalse;
    }

    rtxShadowQueryGpu_t *dst = rtxPipeline.rayQueryMapped;
    for (int i = 0; i < count; i++) {
        dst[i].origin[0] = queries[i].origin[0];
        dst[i].origin[1] = queries[i].origin[1];
        dst[i].origin[2] = queries[i].origin[2];
        dst[i].origin[3] = 1.0f;

        dst[i].direction[0] = queries[i].direction[0];
        dst[i].direction[1] = queries[i].direction[1];
        dst[i].direction[2] = queries[i].direction[2];
        dst[i].direction[3] = queries[i].maxDistance;

        dst[i].occluded = 0;
        dst[i].pad0 = 0;
        dst[i].pad1 = 0;
        dst[i].pad2 = 0;
    }

    return qtrue;
}

void RTX_RayQueryDownload(rtShadowQuery_t *queries, int count) {
    if (!queries || count <= 0 || !rtxPipeline.rayQueryMapped) {
        return;
    }

    rtxShadowQueryGpu_t *src = rtxPipeline.rayQueryMapped;
    for (int i = 0; i < count; i++) {
        queries[i].occluded = src[i].occluded ? qtrue : qfalse;
    }
}

/*
================
RTX_PrepareFrameData

Update per-frame UBOs and GPU buffers (materials, lights, instance data)
================
*/
void RTX_PrepareFrameData(VkCommandBuffer cmd)
{
    if (!vk.device) return;

    // 1) Update CameraUBO
    if (rtxPipeline.cameraUBOMemory) {
        CameraUBO cam = {0};
        // Build inverses from backend matrices if available
        // Use backEnd.viewParms for camera basis
        const viewParms_t *vp = &backEnd.viewParms;
        // Fill simple camera data
        VectorCopy(vp->or.origin, cam.position);
        VectorCopy(vp->or.axis[0], cam.forward);
        VectorCopy(vp->or.axis[1], cam.right);
        VectorCopy(vp->or.axis[2], cam.up);
        cam.nearPlane = vp->zNear;
        cam.farPlane = vp->zFar;
        cam.fov = backEnd.refdef.fov_x;
        cam.frameCount = tr.frameCount;
        cam.enablePathTracing = 1;
        cam.maxBounces = (uint32_t)(rtx_gi_bounces ? rtx_gi_bounces->integer : 2);
        cam.samplesPerPixel = 1;
        int debugModeInt = (r_rtx_debug) ? r_rtx_debug->integer : 0;
        if (debugModeInt == 0 && r_rtx_surface_debug) {
            debugModeInt = r_rtx_surface_debug->integer;
        }
        if (debugModeInt < 0) {
            debugModeInt = 0;
        }
        if (debugModeInt > 8) {
            debugModeInt = 8;
        }
        cam.surfaceDebugMode = (uint32_t)debugModeInt;
        void *p = NULL;
        if (vkMapMemory(vk.device, rtxPipeline.cameraUBOMemory, 0, sizeof(cam), 0, &p) == VK_SUCCESS) {
            Com_Memcpy(p, &cam, sizeof(cam));
            vkUnmapMemory(vk.device, rtxPipeline.cameraUBOMemory);
        }
    }

    // 2) Render settings
    if (rtxPipeline.renderSettingsUBOMemory) {
        RenderSettingsUBO rs = {0};
        rs.enableShadows = 1;
        rs.enableReflections = 1;
        rs.enableGI = 1;
        rs.enableAO = 1;
        rs.shadowBias = 0.001f;
        rs.reflectionRoughnessCutoff = 0.9f;
        rs.giIntensity = 1.0f;
        rs.aoRadius = 0.5f;
        int debugModeInt = (r_rtx_debug) ? r_rtx_debug->integer : 0;
        if (debugModeInt == 0 && r_rtx_surface_debug) {
            debugModeInt = r_rtx_surface_debug->integer;
        }
        if (debugModeInt < 0) {
            debugModeInt = 0;
        }
        if (debugModeInt > 8) {
            debugModeInt = 8;
        }
        rs.debugMode = (uint32_t)debugModeInt;
        rs.enableDenoiser = (rtx_denoise && rtx_denoise->integer) ? 1 : 0;
        rs.enableDLSS = (rtx_dlss && rtx_dlss->integer) ? 1 : 0;
        rs.enableMotionBlur = 0;
        void *p = NULL;
        if (vkMapMemory(vk.device, rtxPipeline.renderSettingsUBOMemory, 0, sizeof(rs), 0, &p) == VK_SUCCESS) {
            Com_Memcpy(p, &rs, sizeof(rs));
            vkUnmapMemory(vk.device, rtxPipeline.renderSettingsUBOMemory);
        }
    }

    // 3) Environment
    if (rtxPipeline.environmentUBOMemory) {
        EnvironmentUBO env = {0};
        // Simple directional light from sun
        VectorSet(env.sunDirection, 0.0f, 0.0f, -1.0f);
        env.sunIntensity = 5.0f;
        VectorSet(env.sunColor, 1.0f, 0.98f, 0.95f);
        env.skyIntensity = 1.0f;
        VectorSet(env.fogColor, 0.5f, 0.6f, 0.7f);
        env.fogDensity = 0.0f;
        env.fogStart = 0.0f;
        env.fogEnd = 0.0f;
        env.useEnvironmentMap = 0;
        env.useProceduralSky = 1;
        env.time = ri.Milliseconds() * 0.001f;
        env.cloudCoverage = 0.0f;
        void *p = NULL;
        if (vkMapMemory(vk.device, rtxPipeline.environmentUBOMemory, 0, sizeof(env), 0, &p) == VK_SUCCESS) {
            Com_Memcpy(p, &env, sizeof(env));
            vkUnmapMemory(vk.device, rtxPipeline.environmentUBOMemory);
        }
    }

    if (rtxPipeline.debugSettingsUBOMemory) {
        DebugSettingsUBO debugData = {0};
        debugData.noTextures = (r_rtx_debug && r_rtx_debug->integer == 2) ? 1u : 0u;
        debugData.debugMode = (r_rtx_debug) ? (uint32_t)MAX(r_rtx_debug->integer, 0) : 0u;

        void *p = NULL;
        if (vkMapMemory(vk.device, rtxPipeline.debugSettingsUBOMemory, 0, sizeof(debugData), 0, &p) == VK_SUCCESS) {
            Com_Memcpy(p, &debugData, sizeof(debugData));
            vkUnmapMemory(vk.device, rtxPipeline.debugSettingsUBOMemory);
        }
    }

    // 4) Upload material buffer if dirty
    RTX_BuildMaterialBuffer();
    if (cmd) {
        // Bind the material cache buffer into our descriptor (see descriptor update below)
        RTX_UploadMaterialBuffer(vk.device, cmd, VK_NULL_HANDLE);
    }

    // 5) Ensure unified light buffer is up to date
    RT_UpdateSceneLightBuffer();
}

/*
================
RTX_GetSBTRegions

Get shader binding table regions for ray dispatch
================
*/
void RTX_GetSBTRegions(VkStridedDeviceAddressRegionKHR *raygen,
                      VkStridedDeviceAddressRegionKHR *miss,
                      VkStridedDeviceAddressRegionKHR *hit,
                      VkStridedDeviceAddressRegionKHR *callable) {
    *raygen = rtxPipeline.sbt.raygenRegion;
    *miss = rtxPipeline.sbt.missRegion;
    *hit = rtxPipeline.sbt.hitRegion;
    *callable = rtxPipeline.sbt.callableRegion;
}

/*
================
RTX_UpdateDescriptorSets

Update descriptor set bindings
================
*/
void RTX_UpdateDescriptorSets(VkAccelerationStructureKHR tlas,
                             VkImageView colorImage, VkImageView albedoImage,
                             VkImageView normalImage, VkImageView motionImage,
                             VkImageView depthImage) {
    VkWriteDescriptorSet writes[24];
    uint32_t writeCount = 0;

    // Get lighting contribution image views
    VkImageView directLightView = NULL;
    VkImageView indirectLightView = NULL;
    VkImageView lightmapView = NULL;
    RTX_GetLightingContributionViews(&directLightView, &indirectLightView, &lightmapView);

    // Use color image as fallback if lighting buffers aren't created yet
    if (!directLightView) directLightView = colorImage;
    if (!indirectLightView) indirectLightView = colorImage;
    if (!lightmapView) lightmapView = colorImage;
    
    // TLAS binding
    VkWriteDescriptorSetAccelerationStructureKHR tlasInfo = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &tlas
    };
    
    writes[writeCount++] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = &tlasInfo,
        .dstSet = rtxPipeline.descriptorSet,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
    };
    
    // Storage images
    VkDescriptorImageInfo imageInfos[5] = {
        { .imageView = colorImage, .imageLayout = VK_IMAGE_LAYOUT_GENERAL },
        { .imageView = albedoImage, .imageLayout = VK_IMAGE_LAYOUT_GENERAL },
        { .imageView = normalImage, .imageLayout = VK_IMAGE_LAYOUT_GENERAL },
        { .imageView = motionImage, .imageLayout = VK_IMAGE_LAYOUT_GENERAL },
        { .imageView = depthImage, .imageLayout = VK_IMAGE_LAYOUT_GENERAL }
    };
    
    for (uint32_t i = 0; i < 5; i++) {
        writes[writeCount++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = rtxPipeline.descriptorSet,
            .dstBinding = 1 + i,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &imageInfos[i]
        };
    }
    
    // Uniform buffers - Camera (6) and RenderSettings (7)
    VkDescriptorBufferInfo bufferInfos[2] = {
        { .buffer = rtxPipeline.cameraUBO, .offset = 0, .range = sizeof(CameraUBO) },
        { .buffer = rtxPipeline.renderSettingsUBO, .offset = 0, .range = sizeof(RenderSettingsUBO) }
    };
    
    for (uint32_t i = 0; i < 2; i++) {
        writes[writeCount++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = rtxPipeline.descriptorSet,
            .dstBinding = 6 + i,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bufferInfos[i]
        };
    }
    
    // Binding 8: Environment map (use default image as placeholder)
    // Create a simple sampler for the environment map
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = VK_LOD_CLAMP_NONE
    };
    
    VkSampler envSampler;
    VkResult result = vkCreateSampler(vk.device, &samplerInfo, NULL, &envSampler);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create environment sampler\n");
        envSampler = VK_NULL_HANDLE;
    }
    
    image_t* envTexture = tr.defaultImage ? tr.defaultImage : tr.whiteImage;
    VkDescriptorImageInfo envImageInfo = {
        .sampler = envSampler,
        .imageView = envTexture ? envTexture->view : VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    
    writes[writeCount++] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = rtxPipeline.descriptorSet,
        .dstBinding = 8,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &envImageInfo
    };
    
    // Environment UBO at binding 9
    VkDescriptorBufferInfo envBufferInfo = {
        .buffer = rtxPipeline.environmentUBO,
        .offset = 0,
        .range = sizeof(EnvironmentUBO)
    };
    
    writes[writeCount++] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = rtxPipeline.descriptorSet,
        .dstBinding = 9,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &envBufferInfo
    };
    
    // Storage buffers
    RT_UpdateSceneLightBuffer();
    VkBuffer matBuf = RTX_GetMaterialBuffer();
    VkBuffer lightBuf = RT_GetSceneLightBuffer();
    VkDeviceSize lightRange = RT_GetSceneLightBufferSize();
    if (matBuf == VK_NULL_HANDLE || lightBuf == VK_NULL_HANDLE) {
        ri.Printf(PRINT_WARNING, "RTX: Shared buffers unavailable for descriptor update\n");
        return;
    }
    VkDescriptorBufferInfo storageBufferInfos[3] = {
        { .buffer = rtxPipeline.instanceDataBuffer, .offset = 0, .range = VK_WHOLE_SIZE },
        { .buffer = matBuf, .offset = 0, .range = VK_WHOLE_SIZE },
        { .buffer = lightBuf, .offset = 0, .range = lightRange ? lightRange : VK_WHOLE_SIZE }
    };
    
    writes[writeCount++] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = rtxPipeline.descriptorSet,
        .dstBinding = 10,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &storageBufferInfos[0]
    };
    
    writes[writeCount++] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = rtxPipeline.descriptorSet,
        .dstBinding = 11,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &storageBufferInfos[1]
    };
    
    writes[writeCount++] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = rtxPipeline.descriptorSet,
        .dstBinding = 14,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &storageBufferInfos[2]
    };

    if (rtxPipeline.triangleMaterialBuffer && rtxPipeline.triangleMaterialCount > 0) {
        VkDescriptorBufferInfo triangleMaterialInfo = {
            .buffer = rtxPipeline.triangleMaterialBuffer,
            .offset = 0,
            .range = sizeof(uint32_t) * rtxPipeline.triangleMaterialCount
        };

        writes[writeCount++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = rtxPipeline.descriptorSet,
            .dstBinding = 20,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &triangleMaterialInfo
        };
    }

    // Add lighting contribution images (bindings 15, 16, 17)
    VkDescriptorImageInfo lightingImageInfos[3] = {
        { .imageView = directLightView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL },
        { .imageView = indirectLightView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL },
        { .imageView = lightmapView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL }
    };

    for (uint32_t i = 0; i < 3; i++) {
        writes[writeCount++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = rtxPipeline.descriptorSet,
            .dstBinding = 15 + i,  // Bindings 15, 16, 17
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &lightingImageInfos[i]
        };
    }

    // Debug settings UBO (binding 18)
    VkDescriptorBufferInfo debugBufferInfo = {
        .buffer = rtxPipeline.debugSettingsUBO,
        .offset = 0,
        .range = sizeof(DebugSettingsUBO)
    };

    writes[writeCount++] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = rtxPipeline.descriptorSet,
        .dstBinding = 18,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &debugBufferInfo
    };

    if (rtxPipeline.rayQueryBuffer) {
        VkDescriptorBufferInfo queryBufferInfo = {
            .buffer = rtxPipeline.rayQueryBuffer,
            .offset = 0,
            .range = sizeof(rtxShadowQueryGpu_t) * rtxPipeline.rayQueryCapacity
        };

        writes[writeCount++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = rtxPipeline.descriptorSet,
            .dstBinding = 19,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &queryBufferInfo
        };
    }
    
    vkUpdateDescriptorSets(vk.device, writeCount, writes, 0, NULL);
}

void RTX_UploadTriangleMaterials(VkCommandBuffer cmd, const uint32_t *materials, uint32_t count) {
    if (!vk.device) {
        return;
    }

    if (count == 0 || !materials) {
        if (rtxPipeline.triangleMaterialBuffer) {
            vkDestroyBuffer(vk.device, rtxPipeline.triangleMaterialBuffer, NULL);
            rtxPipeline.triangleMaterialBuffer = VK_NULL_HANDLE;
        }
        if (rtxPipeline.triangleMaterialBufferMemory) {
            vkFreeMemory(vk.device, rtxPipeline.triangleMaterialBufferMemory, NULL);
            rtxPipeline.triangleMaterialBufferMemory = VK_NULL_HANDLE;
        }
        rtxPipeline.triangleMaterialCount = 0;
        rtxPipeline.triangleMaterialCapacity = 0;
        return;
    }

    VkDeviceSize bufferSize = sizeof(uint32_t) * (VkDeviceSize)count;

    if (!rtxPipeline.triangleMaterialBuffer ||
        rtxPipeline.triangleMaterialCapacity < count) {
        if (rtxPipeline.triangleMaterialBuffer) {
            vkDestroyBuffer(vk.device, rtxPipeline.triangleMaterialBuffer, NULL);
            rtxPipeline.triangleMaterialBuffer = VK_NULL_HANDLE;
        }
        if (rtxPipeline.triangleMaterialBufferMemory) {
            vkFreeMemory(vk.device, rtxPipeline.triangleMaterialBufferMemory, NULL);
            rtxPipeline.triangleMaterialBufferMemory = VK_NULL_HANDLE;
        }

        VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = bufferSize,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        if (vkCreateBuffer(vk.device, &bufferInfo, NULL, &rtxPipeline.triangleMaterialBuffer) != VK_SUCCESS) {
            rtxPipeline.triangleMaterialBuffer = VK_NULL_HANDLE;
            rtxPipeline.triangleMaterialCount = 0;
            rtxPipeline.triangleMaterialCapacity = 0;
            return;
        }

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(vk.device, rtxPipeline.triangleMaterialBuffer, &memReqs);

        VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = vk_find_memory_type(memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        };

        if (vkAllocateMemory(vk.device, &allocInfo, NULL, &rtxPipeline.triangleMaterialBufferMemory) != VK_SUCCESS) {
            vkDestroyBuffer(vk.device, rtxPipeline.triangleMaterialBuffer, NULL);
            rtxPipeline.triangleMaterialBuffer = VK_NULL_HANDLE;
            rtxPipeline.triangleMaterialBufferMemory = VK_NULL_HANDLE;
            rtxPipeline.triangleMaterialCount = 0;
            rtxPipeline.triangleMaterialCapacity = 0;
            return;
        }

        vkBindBufferMemory(vk.device, rtxPipeline.triangleMaterialBuffer,
                           rtxPipeline.triangleMaterialBufferMemory, 0);
        rtxPipeline.triangleMaterialCapacity = count;
    }

    if (!cmd) {
        return;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo stagingInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vk.device, &stagingInfo, NULL, &stagingBuffer) != VK_SUCCESS) {
        return;
    }

    VkMemoryRequirements stagingReqs;
    vkGetBufferMemoryRequirements(vk.device, stagingBuffer, &stagingReqs);

    VkMemoryAllocateInfo stagingAlloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = stagingReqs.size,
        .memoryTypeIndex = vk_find_memory_type(stagingReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };

    if (vkAllocateMemory(vk.device, &stagingAlloc, NULL, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vk.device, stagingBuffer, NULL);
        return;
    }

    vkBindBufferMemory(vk.device, stagingBuffer, stagingMemory, 0);

    void *mapped = NULL;
    if (vkMapMemory(vk.device, stagingMemory, 0, bufferSize, 0, &mapped) == VK_SUCCESS) {
        Com_Memcpy(mapped, materials, bufferSize);
        vkUnmapMemory(vk.device, stagingMemory);
    } else {
        vkFreeMemory(vk.device, stagingMemory, NULL);
        vkDestroyBuffer(vk.device, stagingBuffer, NULL);
        return;
    }

    VkBufferCopy copyRegion = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = bufferSize
    };

    vkCmdCopyBuffer(cmd, stagingBuffer, rtxPipeline.triangleMaterialBuffer, 1, &copyRegion);

    VkBufferMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = rtxPipeline.triangleMaterialBuffer,
        .offset = 0,
        .size = bufferSize
    };

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 1, &barrier, 0, NULL);

    vkDestroyBuffer(vk.device, stagingBuffer, NULL);
    vkFreeMemory(vk.device, stagingMemory, NULL);

    rtxPipeline.triangleMaterialCount = count;
}

// vk_find_memory_type is already defined in vk.c and declared in vk.h
// No need to redefine it here
