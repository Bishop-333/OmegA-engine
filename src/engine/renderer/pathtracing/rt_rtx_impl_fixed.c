/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Pure Vulkan RTX Hardware Raytracing Implementation
Vulkan Ray Tracing extensions only - no DirectX or OpenGL

FIXED VERSION: Asynchronous architecture integrated with main renderer
===========================================================================
*/

#include "rt_rtx.h"
#include "rt_pathtracer.h"
#include "../core/tr_local.h"
#include "../vulkan/vk.h"

// External RTX state
extern rtxState_t rtx;

// Vulkan RTX-specific state structure
typedef struct vkrtState_s {
    // Device handles
    VkDevice                device;
    VkPhysicalDevice        physicalDevice;

    // REMOVED: Command execution objects - now using main renderer's
    // VkCommandPool           commandPool;     // REMOVED
    // VkCommandBuffer         commandBuffer;   // REMOVED
    // VkFence                 fence;           // REMOVED
    // VkSemaphore             semaphore;       // REMOVED

    // Temporary command resources for AS builds only
    VkCommandPool           asBuildCommandPool;
    VkCommandBuffer         asBuildCommandBuffer;
    VkFence                 asBuildFence;

    // Ray tracing properties
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties;

    // Ray tracing resources
    VkAccelerationStructureKHR  tlas;
    VkBuffer                    tlasBuffer;
    VkDeviceMemory              tlasMemory;
    VkBuffer                    instanceBuffer;
    VkDeviceMemory              instanceMemory;

    // Ray tracing pipeline
    VkPipeline              rtPipeline;
    VkPipelineLayout        pipelineLayout;

    // Shader binding table
    VkBuffer                raygenSBT;
    VkBuffer                missSBT;
    VkBuffer                hitSBT;
    VkDeviceMemory          sbtMemory;

    // RT output images
    VkImage                 rtImage;
    VkDeviceMemory          rtImageMemory;
    VkImageView             rtImageView;

    // G-buffer images
    VkImage                 albedoImage;
    VkDeviceMemory          albedoMemory;
    VkImageView             albedoView;
    VkImage                 normalImage;
    VkDeviceMemory          normalMemory;
    VkImageView             normalView;
    VkImage                 motionImage;
    VkDeviceMemory          motionMemory;
    VkImageView             motionView;

    // Depth linearization resources
    VkImage                 depthLinearImage;
    VkDeviceMemory          depthLinearMemory;
    VkImageView             depthLinearView;
    VkPipeline              depthLinearPipeline;
    VkDescriptorSetLayout   depthLinearSetLayout;
    VkPipelineLayout        depthLinearLayout;
    VkDescriptorPool        depthLinearPool;

    // Normal reconstruction resources
    VkPipeline              normalReconPipeline;
    VkDescriptorSetLayout   normalReconSetLayout;
    VkPipelineLayout        normalReconLayout;
    VkDescriptorPool        normalReconPool;

    // Composite resources
    VkPipeline              compositePipeline;
    VkDescriptorSetLayout   compositeSetLayout;
    VkPipelineLayout        compositeLayout;
    VkDescriptorPool        compositePool;

    // Common resources
    VkSampler               computeSampler;

    // Denoiser state (moved to async processing)
    struct {
        qboolean    pendingDenoise;
        VkBuffer    stagingBuffer;
        VkDeviceMemory stagingMemory;
        VkFence     denoiseFence;
    } denoiser;
} vkrtState_t;

// Global Vulkan RTX state
static vkrtState_t vkrt = { 0 };

// Track if RT images have been initialized for proper layout transitions
static qboolean rtImagesInitialized = qfalse;
// Track if depth linear image has been transitioned to GENERAL layout
qboolean depthLinearImageInitialized = qfalse;

// ============================================================================
// Forward declarations for refactored functions
// ============================================================================
static void RTX_CreateASBuildResources(void);
static void RTX_DestroyASBuildResources(void);
static qboolean RTX_BuildAccelerationStructureVK_Sync(void);
static void RTX_ProcessPendingDenoise(void);

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/*
================
RTX_GetDevice

Returns the current Vulkan device handle
================
*/
static VkDevice RTX_GetDevice(void) {
    return vkrt.device ? vkrt.device : vk.device;
}

/*
================
RTX_GetPhysicalDevice

Returns the current physical device handle
================
*/
static VkPhysicalDevice RTX_GetPhysicalDevice(void) {
    return vkrt.physicalDevice ? vkrt.physicalDevice : vk.physical_device;
}

// ============================================================================
// INITIALIZATION AND SHUTDOWN
// ============================================================================

/*
================
RTX_InitVulkanRT

Initialize Vulkan ray tracing resources
FIXED: No longer creates command buffers or sync objects
================
*/
qboolean RTX_InitVulkanRT(void) {
    VkResult result;

    ri.Printf(PRINT_ALL, "RTX: RTX_InitVulkanRT called\n");

    // Store device handles
    vkrt.device = vk.device;
    vkrt.physicalDevice = vk.physical_device;
    
    if (!vkrt.device) {
        ri.Printf(PRINT_WARNING, "RTX: No Vulkan device available\n");
        return qfalse;
    }

    if (!vkrt.device || !vkrt.physicalDevice) {
        ri.Printf(PRINT_WARNING, "RTX: Vulkan device not initialized\n");
        return qfalse;
    }

    // Get ray tracing properties
    vkrt.rtProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    vkrt.asProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    vkrt.rtProperties.pNext = &vkrt.asProperties;

    VkPhysicalDeviceProperties2 deviceProps2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &vkrt.rtProperties
    };

    vkGetPhysicalDeviceProperties2(vkrt.physicalDevice, &deviceProps2);

    ri.Printf(PRINT_ALL, "RTX: Max ray recursion depth: %d\n", vkrt.rtProperties.maxRayRecursionDepth);
    ri.Printf(PRINT_ALL, "RTX: Shader group handle size: %d\n", vkrt.rtProperties.shaderGroupHandleSize);

    // Create resources for AS builds only (these need to be synchronous)
    RTX_CreateASBuildResources();

    // Create compute sampler for linearization/reconstruction
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .mipLodBias = 0.0f,
        .minLod = 0.0f,
        .maxLod = 0.0f
    };

    result = vkCreateSampler(vkrt.device, &samplerInfo, NULL, &vkrt.computeSampler);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create compute sampler\n");
        RTX_ShutdownVulkanRT();
        return qfalse;
    }

    // Initialize RTX state
    rtx.initialized = qtrue;
    rtx.available = qtrue;
    ri.Printf(PRINT_ALL, "RTX: Set rtx.initialized = true, rtx.available = true\n");

    ri.Printf(PRINT_ALL, "RTX: Vulkan ray tracing initialized (asynchronous mode)\n");

    return qtrue;
}

/*
================
RTX_ShutdownVulkanRT

Clean up Vulkan ray tracing resources
FIXED: Removed command buffer and sync object cleanup
================
*/
void RTX_ShutdownVulkanRT(void) {
    if (!vkrt.device) {
        return;
    }

    // Wait for device to be idle before cleanup
    vkDeviceWaitIdle(vkrt.device);

    // Clean up AS build resources
    RTX_DestroyASBuildResources();

    // Clean up denoiser resources if any
    if (vkrt.denoiser.stagingBuffer) {
        vkDestroyBuffer(vkrt.device, vkrt.denoiser.stagingBuffer, NULL);
        vkrt.denoiser.stagingBuffer = VK_NULL_HANDLE;
    }
    if (vkrt.denoiser.stagingMemory) {
        vkFreeMemory(vkrt.device, vkrt.denoiser.stagingMemory, NULL);
        vkrt.denoiser.stagingMemory = VK_NULL_HANDLE;
    }
    if (vkrt.denoiser.denoiseFence) {
        vkDestroyFence(vkrt.device, vkrt.denoiser.denoiseFence, NULL);
        vkrt.denoiser.denoiseFence = VK_NULL_HANDLE;
    }

    // Clean up compute sampler
    if (vkrt.computeSampler) {
        vkDestroySampler(vkrt.device, vkrt.computeSampler, NULL);
        vkrt.computeSampler = VK_NULL_HANDLE;
    }

    // Clean up RT images
    if (vkrt.rtImageView) {
        vkDestroyImageView(vkrt.device, vkrt.rtImageView, NULL);
        vkrt.rtImageView = VK_NULL_HANDLE;
    }
    if (vkrt.rtImage) {
        vkDestroyImage(vkrt.device, vkrt.rtImage, NULL);
        vkrt.rtImage = VK_NULL_HANDLE;
    }
    if (vkrt.rtImageMemory) {
        vkFreeMemory(vkrt.device, vkrt.rtImageMemory, NULL);
        vkrt.rtImageMemory = VK_NULL_HANDLE;
    }

    // Clean up G-buffer images
    if (vkrt.albedoView) {
        vkDestroyImageView(vkrt.device, vkrt.albedoView, NULL);
        vkrt.albedoView = VK_NULL_HANDLE;
    }
    if (vkrt.albedoImage) {
        vkDestroyImage(vkrt.device, vkrt.albedoImage, NULL);
        vkrt.albedoImage = VK_NULL_HANDLE;
    }
    if (vkrt.albedoMemory) {
        vkFreeMemory(vkrt.device, vkrt.albedoMemory, NULL);
        vkrt.albedoMemory = VK_NULL_HANDLE;
    }

    if (vkrt.normalView) {
        vkDestroyImageView(vkrt.device, vkrt.normalView, NULL);
        vkrt.normalView = VK_NULL_HANDLE;
    }
    if (vkrt.normalImage) {
        vkDestroyImage(vkrt.device, vkrt.normalImage, NULL);
        vkrt.normalImage = VK_NULL_HANDLE;
    }
    if (vkrt.normalMemory) {
        vkFreeMemory(vkrt.device, vkrt.normalMemory, NULL);
        vkrt.normalMemory = VK_NULL_HANDLE;
    }

    if (vkrt.motionView) {
        vkDestroyImageView(vkrt.device, vkrt.motionView, NULL);
        vkrt.motionView = VK_NULL_HANDLE;
    }
    if (vkrt.motionImage) {
        vkDestroyImage(vkrt.device, vkrt.motionImage, NULL);
        vkrt.motionImage = VK_NULL_HANDLE;
    }
    if (vkrt.motionMemory) {
        vkFreeMemory(vkrt.device, vkrt.motionMemory, NULL);
        vkrt.motionMemory = VK_NULL_HANDLE;
    }

    // Clean up depth linearization resources
    if (vkrt.depthLinearView) {
        vkDestroyImageView(vkrt.device, vkrt.depthLinearView, NULL);
        vkrt.depthLinearView = VK_NULL_HANDLE;
    }
    if (vkrt.depthLinearImage) {
        vkDestroyImage(vkrt.device, vkrt.depthLinearImage, NULL);
        vkrt.depthLinearImage = VK_NULL_HANDLE;
    }
    if (vkrt.depthLinearMemory) {
        vkFreeMemory(vkrt.device, vkrt.depthLinearMemory, NULL);
        vkrt.depthLinearMemory = VK_NULL_HANDLE;
    }

    // Clean up pipelines
    if (vkrt.rtPipeline) {
        vkDestroyPipeline(vkrt.device, vkrt.rtPipeline, NULL);
        vkrt.rtPipeline = VK_NULL_HANDLE;
    }
    if (vkrt.pipelineLayout) {
        vkDestroyPipelineLayout(vkrt.device, vkrt.pipelineLayout, NULL);
        vkrt.pipelineLayout = VK_NULL_HANDLE;
    }

    // Clean up acceleration structures
    if (vkrt.tlas) {
        qvkDestroyAccelerationStructureKHR(vkrt.device, vkrt.tlas, NULL);
        vkrt.tlas = VK_NULL_HANDLE;
    }
    if (vkrt.tlasBuffer) {
        vkDestroyBuffer(vkrt.device, vkrt.tlasBuffer, NULL);
        vkrt.tlasBuffer = VK_NULL_HANDLE;
    }
    if (vkrt.tlasMemory) {
        vkFreeMemory(vkrt.device, vkrt.tlasMemory, NULL);
        vkrt.tlasMemory = VK_NULL_HANDLE;
    }

    // Clean up shader binding table
    if (vkrt.raygenSBT) {
        vkDestroyBuffer(vkrt.device, vkrt.raygenSBT, NULL);
        vkrt.raygenSBT = VK_NULL_HANDLE;
    }
    if (vkrt.missSBT) {
        vkDestroyBuffer(vkrt.device, vkrt.missSBT, NULL);
        vkrt.missSBT = VK_NULL_HANDLE;
    }
    if (vkrt.hitSBT) {
        vkDestroyBuffer(vkrt.device, vkrt.hitSBT, NULL);
        vkrt.hitSBT = VK_NULL_HANDLE;
    }
    if (vkrt.sbtMemory) {
        vkFreeMemory(vkrt.device, vkrt.sbtMemory, NULL);
        vkrt.sbtMemory = VK_NULL_HANDLE;
    }

    // Reset state
    Com_Memset(&vkrt, 0, sizeof(vkrt));
    rtx.initialized = qfalse;
    rtx.available = qfalse;

    ri.Printf(PRINT_ALL, "RTX: Vulkan ray tracing shutdown\n");
}

// ============================================================================
// ACCELERATION STRUCTURE BUILD RESOURCES
// ============================================================================

/*
================
RTX_CreateASBuildResources

Create command pool and buffer specifically for AS builds
These need to be synchronous and separate from the main render loop
================
*/
static void RTX_CreateASBuildResources(void) {
    VkResult result;

    // Create command pool for AS builds
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk.queue_family_index
    };

    result = vkCreateCommandPool(vkrt.device, &poolInfo, NULL, &vkrt.asBuildCommandPool);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create AS build command pool\n");
        return;
    }

    // Allocate command buffer for AS builds
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vkrt.asBuildCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    result = vkAllocateCommandBuffers(vkrt.device, &allocInfo, &vkrt.asBuildCommandBuffer);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to allocate AS build command buffer\n");
        vkDestroyCommandPool(vkrt.device, vkrt.asBuildCommandPool, NULL);
        vkrt.asBuildCommandPool = VK_NULL_HANDLE;
        return;
    }
    vk_cmd_register("rtx_as_build", vkrt.asBuildCommandBuffer, vkrt.asBuildCommandPool);

    // Create fence for AS build synchronization
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = 0  // Not signaled initially
    };

    result = vkCreateFence(vkrt.device, &fenceInfo, NULL, &vkrt.asBuildFence);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create AS build fence\n");
    }
}

/*
================
RTX_DestroyASBuildResources

Destroy AS build specific resources
================
*/
static void RTX_DestroyASBuildResources(void) {
    if (vkrt.asBuildFence) {
        vkDestroyFence(vkrt.device, vkrt.asBuildFence, NULL);
        vkrt.asBuildFence = VK_NULL_HANDLE;
    }

    if (vkrt.asBuildCommandBuffer) {
        vk_cmd_unregister(vkrt.asBuildCommandBuffer);
        vkFreeCommandBuffers(vkrt.device, vkrt.asBuildCommandPool, 1, &vkrt.asBuildCommandBuffer);
        vkrt.asBuildCommandBuffer = VK_NULL_HANDLE;
    }

    if (vkrt.asBuildCommandPool) {
        vkDestroyCommandPool(vkrt.device, vkrt.asBuildCommandPool, NULL);
        vkrt.asBuildCommandPool = VK_NULL_HANDLE;
    }
}

// ============================================================================
// COMPUTE SHADER PASSES (ASYNCHRONOUS)
// ============================================================================

/*
================
RTX_LinearizeDepth

Linearize depth buffer for better ray marching
FIXED: Now accepts command buffer parameter for async recording
================
*/
void RTX_LinearizeDepth(VkCommandBuffer cmd, uint32_t width, uint32_t height, float zNear, float zFar) {
    if (!vkrt.depthLinearPipeline || !vkrt.depthLinearImage) {
        return; // Resources not created yet
    }

    // Validate near/far planes to prevent division by zero
    if (zNear <= 0.0f || zFar <= zNear) {
        ri.Printf(PRINT_WARNING, "RTX: Invalid near/far planes for depth linearization (near=%f, far=%f)\n", zNear, zFar);
        return;
    }

    // Transition depth linear image to GENERAL if not initialized
    if (!depthLinearImageInitialized) {
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vkrt.depthLinearImage,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, NULL, 0, NULL, 1, &barrier);

        depthLinearImageInitialized = qtrue;
    }

    // Bind compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkrt.depthLinearPipeline);

    // Push constants for near/far planes
    struct {
        float zNear;
        float zFar;
        int reserved;
    } pushConstants = {
        .zNear = zNear,
        .zFar = zFar,
        .reserved = 0
    };

    vkCmdPushConstants(cmd, vkrt.depthLinearLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pushConstants), &pushConstants);

    // Bind descriptor sets (assuming they're set up)
    // This would need the actual descriptor set with depth texture and output image
    // vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ...);

    // Dispatch compute shader
    uint32_t groupsX = (width + 7) / 8;  // 8x8 local workgroup size
    uint32_t groupsY = (height + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Memory barrier to ensure compute shader writes are visible to ray tracing
    VkMemoryBarrier memBarrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
    };

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, NULL, 0, NULL);
}

/*
================
RTX_ReconstructNormals

Reconstruct world-space normals from depth
FIXED: Now accepts command buffer parameter for async recording
================
*/
void RTX_ReconstructNormals(VkCommandBuffer cmd, uint32_t width, uint32_t height) {
    if (!vkrt.normalReconPipeline || !vkrt.normalImage) {
        return; // Resources not created yet
    }

    // Bind compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkrt.normalReconPipeline);

    // Bind descriptor sets
    // vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ...);

    // Dispatch compute shader
    uint32_t groupsX = (width + 7) / 8;  // 8x8 local workgroup size
    uint32_t groupsY = (height + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Memory barrier for subsequent passes
    VkMemoryBarrier memBarrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
    };

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, NULL, 0, NULL);
}

// ============================================================================
// RAY TRACING DISPATCH (ASYNCHRONOUS)
// ============================================================================

/*
================
RTX_DispatchRaysVK

Dispatch ray tracing work
FIXED: Now fully asynchronous, records commands into provided command buffer
================
*/
void RTX_DispatchRaysVK(VkCommandBuffer cmd, const rtxDispatchRays_t *params) {
    if (!vkrt.device || !params) {
        return;
    }

    // Validate parameters
    if (params->width == 0 || params->height == 0) {
        return;
    }

    // Get pipeline and layout (these should be created elsewhere)
    VkPipeline rtPipeline = vkrt.rtPipeline;
    VkPipelineLayout pipelineLayout = vkrt.pipelineLayout;
    VkDescriptorSet descriptorSet = RTX_GetDescriptorSet();

    if (!rtPipeline || !pipelineLayout || !descriptorSet) {
        ri.Printf(PRINT_WARNING, "RTX: Pipeline not properly initialized\n");
        return;
    }

    // AS build must be synchronous for now (during loading or when scene changes)
    // This should ideally be moved out of the main render loop
    if (!vkrt.tlas || rtx.tlas.needsRebuild) {
        RTX_BuildAccelerationStructureVK_Sync();
        rtx.tlas.needsRebuild = qfalse;
    }

    // Get render dimensions
    uint32_t rw = params->width;
    uint32_t rh = params->height;

    // Create or recreate RT images if size changed
    if (!vkrt.rtImage || rtx.renderWidth != rw || rtx.renderHeight != rh) {
        // This should be refactored to happen outside the render loop
        RTX_CreateRTImages(rw, rh);
        rtx.renderWidth = rw;
        rtx.renderHeight = rh;
    }

    // Prepare albedo storage by copying from the current color buffer
    if (vk.color_image && vkrt.albedoImage) {
        VkImageMemoryBarrier barriers[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = vk_image_get_layout_or( vk.color_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ),
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk.color_image,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = rtImagesInitialized ? VK_ACCESS_SHADER_READ_BIT : 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = rtImagesInitialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vkrt.albedoImage,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            }
        };

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 2, barriers);

        vk_image_set_layout( vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );

        VkImageCopy copyRegion = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .extent = { rw, rh, 1 }
        };
        vkCmdCopyImage(cmd, vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      vkrt.albedoImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // Transition back to usable layouts
        barriers[0].oldLayout = vk_image_get_layout_or( vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
        barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0, 0, NULL, 0, NULL, 2, barriers);

        vk_image_set_layout( vk.color_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
    }

    // Linearize depth and reconstruct normals (compute passes)
    RTX_LinearizeDepth(cmd, rw, rh, backEnd.viewParms.zNear, backEnd.viewParms.zFar);
    RTX_ReconstructNormals(cmd, rw, rh);

    // Update descriptor sets with current resources
    extern VkImageView R_GetMotionVectorView(void);
    VkImageView motionViewExternal = R_GetMotionVectorView();

    RTX_UpdateDescriptorSets(vkrt.tlas, vkrt.rtImageView,
                            vkrt.albedoView ? vkrt.albedoView : vkrt.rtImageView,
                            vkrt.normalView ? vkrt.normalView : vkrt.rtImageView,
                            motionViewExternal ? motionViewExternal : (vkrt.motionView ? vkrt.motionView : vkrt.rtImageView),
                            vkrt.rtImageView);

    // Update per-frame UBOs/materials/lights
    RTX_PrepareFrameData(cmd);

    // Transition RT output image to general layout for ray tracing
    if (vkrt.rtImage) {
        VkImageMemoryBarrier imageBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = rtImagesInitialized ? VK_ACCESS_TRANSFER_READ_BIT : 0,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = rtImagesInitialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vkrt.rtImage,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        vkCmdPipelineBarrier(cmd,
            rtImagesInitialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0, 0, NULL, 0, NULL, 1, &imageBarrier);
    }

    // Bind ray tracing pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipeline);

    // Bind descriptor sets
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                           pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

    // Get shader binding table regions
    VkStridedDeviceAddressRegionKHR raygenRegion, missRegion, hitRegion, callableRegion;
    RTX_GetSBTRegions(&raygenRegion, &missRegion, &hitRegion, &callableRegion);

    // Dispatch rays
    static int dispatchCount = 0;
    dispatchCount++;
    if (dispatchCount % 100 == 0) {
        ri.Printf(PRINT_ALL, "RTX: Dispatching rays %dx%d (dispatch #%d)\n", 
                  params->width, params->height, dispatchCount);
    }
    
    qvkCmdTraceRaysKHR(cmd,
                       &raygenRegion, &missRegion, &hitRegion, &callableRegion,
                       params->width, params->height, 1);

    // Transition RT output image for transfer/presentation
    if (vkrt.rtImage) {
        VkImageMemoryBarrier imageBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vkrt.rtImage,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 1, &imageBarrier);
    }

    // Mark images as initialized after first frame
    rtImagesInitialized = qtrue;

    // Request async denoise if enabled (processed later)
    if (rtx_denoise && rtx_denoise->integer && rtx.denoiser.enabled) {
        vkrt.denoiser.pendingDenoise = qtrue;
    }

    if (rtx_debug && rtx_debug->integer) {
        ri.Printf(PRINT_ALL, "RTX: Ray dispatch recorded (%dx%d)\n",
                 params->width, params->height);
    }
}

// ============================================================================
// ACCELERATION STRUCTURE BUILDING (SYNCHRONOUS)
// ============================================================================

/*
================
RTX_BuildAccelerationStructureVK_Sync

Build acceleration structures synchronously
This is acceptable during loading but should be avoided during gameplay
================
*/
static qboolean RTX_BuildAccelerationStructureVK_Sync(void) {
    if (!vkrt.asBuildCommandBuffer || !vkrt.asBuildFence) {
        ri.Printf(PRINT_WARNING, "RTX: AS build resources not initialized\n");
        return qfalse;
    }

    // Reset command buffer
    vkResetCommandBuffer(vkrt.asBuildCommandBuffer, 0);

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    VkResult result = vkBeginCommandBuffer(vkrt.asBuildCommandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to begin AS build command buffer\n");
        return qfalse;
    }

    // Record AS build commands here
    // ... (actual AS build implementation) ...

    // End command buffer
    result = vkEndCommandBuffer(vkrt.asBuildCommandBuffer);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to end AS build command buffer\n");
        return qfalse;
    }

    // Submit for execution
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &vkrt.asBuildCommandBuffer
    };

    // Reset fence before submission
    vkResetFences(vkrt.device, 1, &vkrt.asBuildFence);

    result = vkQueueSubmit(vk.queue, 1, &submitInfo, vkrt.asBuildFence);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to submit AS build command buffer\n");
        return qfalse;
    }

    // Wait for completion (synchronous for AS builds)
    result = vkWaitForFences(vkrt.device, 1, &vkrt.asBuildFence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to wait for AS build completion\n");
        return qfalse;
    }

    return qtrue;
}

// ============================================================================
// DENOISER (ASYNCHRONOUS)
// ============================================================================

/*
================
RTX_ProcessPendingDenoise

Process any pending denoise operations asynchronously
This should be called at the end of the frame, after the main submit
================
*/
static void RTX_ProcessPendingDenoise(void) {
    if (!vkrt.denoiser.pendingDenoise) {
        return;
    }

    vkrt.denoiser.pendingDenoise = qfalse;

    // TODO: Implement async denoising
    // This would involve:
    // 1. Checking if previous denoise is complete
    // 2. Starting a new denoise operation on a separate thread
    // 3. Using the denoised result in the next frame

    // For now, denoising is disabled in the async architecture
    // A proper implementation would use a compute shader denoiser
    // or process the denoise on a separate thread with double buffering
}

/*
================
RTX_BeginFrame

Called at the beginning of each frame
Can be used to process async operations from previous frame
================
*/
void RTX_BeginFrame(void) {
    // Process any pending denoise from last frame
    RTX_ProcessPendingDenoise();

    // Other per-frame initialization
}

/*
================
RTX_EndFrame

Called at the end of each frame
Can be used to kick off async operations for next frame
================
*/
void RTX_EndFrame(void) {
    // Any end-of-frame processing
}

// ============================================================================
// INTEGRATION HELPERS
// ============================================================================

/*
================
RTX_ShouldRenderThisFrame

Determine if RTX should be active this frame
================
*/
qboolean RTX_ShouldRenderThisFrame(void) {
    static int checkCount = 0;
    checkCount++;
    
    if (!rtx.initialized || !rtx.available) {
        if (checkCount % 500 == 1) {
            ri.Printf(PRINT_ALL, "RTX: Not rendering - rtx.initialized=%d, rtx.available=%d\n",
                     rtx.initialized, rtx.available);
        }
        return qfalse;
    }

    if (!RTX_IsEnabled()) {
        if (checkCount % 500 == 1) {
            ri.Printf(PRINT_ALL, "RTX: Not rendering - RTX_IsEnabled() returned false\n");
        }
        return qfalse;
    }

    // Additional conditions can be added here
    return qtrue;
}

/*
================
RTX_RecordCommands

Main entry point for recording RTX commands into the frame's command buffer
This should be called from vk_end_frame or similar
================
*/
void RTX_RecordCommands(VkCommandBuffer cmd) {
    static int frameCount = 0;
    frameCount++;
    
    if (!RTX_ShouldRenderThisFrame()) {
        if (frameCount % 100 == 0) {
            ri.Printf(PRINT_ALL, "RTX: Not rendering frame %d (RTX disabled or not ready)\n", frameCount);
        }
        return;
    }

    if (frameCount % 100 == 0) {
        ri.Printf(PRINT_ALL, "RTX: Recording commands for frame %d\n", frameCount);
    }

    // Set up dispatch parameters
    rtxDispatchRays_t params = {
        .width = vk.renderWidth,
        .height = vk.renderHeight,
        .origin = { backEnd.viewParms.or.origin[0], backEnd.viewParms.or.origin[1], backEnd.viewParms.or.origin[2] },
        .forward = { backEnd.viewParms.or.axis[0][0], backEnd.viewParms.or.axis[0][1], backEnd.viewParms.or.axis[0][2] },
        .right = { backEnd.viewParms.or.axis[1][0], backEnd.viewParms.or.axis[1][1], backEnd.viewParms.or.axis[1][2] },
        .up = { backEnd.viewParms.or.axis[2][0], backEnd.viewParms.or.axis[2][1], backEnd.viewParms.or.axis[2][2] }
    };

    // Record all RTX commands
    RTX_DispatchRaysVK(cmd, &params);
}

// ============================================================================
// PLACEHOLDER IMPLEMENTATIONS
// These functions are referenced but need proper implementation
// ============================================================================

void RTX_CreateRTImages(uint32_t width, uint32_t height) {
    // TODO: Implement RT image creation
}

VkDescriptorSet RTX_GetDescriptorSet(void) {
    // TODO: Implement descriptor set management
    return VK_NULL_HANDLE;
}

void RTX_UpdateDescriptorSets(VkAccelerationStructureKHR tlas, VkImageView rtImage,
                              VkImageView albedo, VkImageView normal,
                              VkImageView motion, VkImageView depth) {
    // TODO: Implement descriptor set updates
}

void RTX_PrepareFrameData(VkCommandBuffer cmd) {
    // TODO: Implement frame data preparation
}

void RTX_GetSBTRegions(VkStridedDeviceAddressRegionKHR *raygen,
                       VkStridedDeviceAddressRegionKHR *miss,
                       VkStridedDeviceAddressRegionKHR *hit,
                       VkStridedDeviceAddressRegionKHR *callable) {
    // TODO: Implement SBT region calculation
}
