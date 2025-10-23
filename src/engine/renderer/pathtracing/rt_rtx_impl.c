/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Pure Vulkan RTX Hardware Raytracing Implementation
Vulkan Ray Tracing extensions only - no DirectX or OpenGL
===========================================================================
*/

#include "rt_rtx.h"
#include "rt_pathtracer.h"
#include "../core/tr_local.h"
#include "../vulkan/vk.h"
#include <stdint.h>
#include <string.h>

#if defined(_DEBUG)
#define RTX_DEBUG_LOG_CMD(action, buffer, tag) \
    do { \
        if ((buffer) != VK_NULL_HANDLE) { \
            const char *_tag = (tag) ? (tag) : ""; \
            ri.Printf(PRINT_DEVELOPER, "RTX-CMD %s %p %s\n", action, (void*)(buffer), _tag); \
        } \
    } while (0)
#else
#define RTX_DEBUG_LOG_CMD(action, buffer, tag) ((void)0)
#endif

// External RTX state
extern rtxState_t rtx;
extern cvar_t *r_rtx_gi_bounces;
extern cvar_t *r_rtx_debug;

// ============================================================================
// Forward Declarations
// ============================================================================

static VkBuffer RTX_AllocateScratchBuffer(VkDeviceSize size, VkDeviceMemory *memory);
static void RTX_LogCapabilitySummary(void);
static void RTX_DetectGPUVendor(const VkPhysicalDeviceProperties *props, const char **outVendorLabel);
static const char *RTX_VendorLabel(rtxGpuType_t type);
static void RTX_SetNVIDIAArchitecture(const char *deviceName);
static void RTX_SetAMDArchitecture(const char *deviceName);
static void RTX_SetIntelArchitecture(const char *deviceName);
static qboolean RTX_EnsureReadbackBuffer(VkDeviceSize size);
static void RTX_DestroyReadbackBuffer(void);
static qboolean RTX_DownloadColorBuffer(uint32_t width, uint32_t height);

// ============================================================================
// Vulkan Ray Tracing Function Pointers
// ============================================================================

// Define function pointers for RT extensions
static PFN_vkCreateAccelerationStructureKHR qvkCreateAccelerationStructureKHR;
static PFN_vkDestroyAccelerationStructureKHR qvkDestroyAccelerationStructureKHR;
static PFN_vkGetAccelerationStructureBuildSizesKHR qvkGetAccelerationStructureBuildSizesKHR;
static PFN_vkCmdBuildAccelerationStructuresKHR qvkCmdBuildAccelerationStructuresKHR;
static PFN_vkGetAccelerationStructureDeviceAddressKHR qvkGetAccelerationStructureDeviceAddressKHR;
static PFN_vkCmdTraceRaysKHR qvkCmdTraceRaysKHR;
static PFN_vkGetBufferDeviceAddress qvkGetBufferDeviceAddress;

// ============================================================================
// Vulkan Ray Tracing Implementation
// ============================================================================

typedef struct vkrtState_s {
    VkDevice                        device;
    VkPhysicalDevice                physicalDevice;
    VkCommandPool                   commandPool;
    VkCommandBuffer                 commandBuffer;
    
    // Ray tracing pipeline
    VkPipeline                      rtPipeline;
    VkPipelineLayout                pipelineLayout;

    // Debug overlay compute pipeline
    VkPipeline                      debugOverlayPipeline;
    VkPipelineLayout                debugOverlayPipelineLayout;
    VkDescriptorSetLayout           debugOverlaySetLayout;
    VkDescriptorPool                debugOverlayDescriptorPool;
    VkDescriptorSet                 debugOverlayDescriptorSet;
    VkSampler                       debugOverlaySampler;

    // Shader binding table
    VkBuffer                        raygenSBT;
    VkBuffer                        missSBT;
    VkBuffer                        hitSBT;
    VkDeviceMemory                  sbtMemory;
    
    // Acceleration structures
    VkAccelerationStructureKHR      tlas[2];
    VkBuffer                        tlasBuffer[2];
    VkDeviceMemory                  tlasMemory[2];
    int                             activeTLAS;
    
    // BLAS instances
    VkBuffer                        instanceBuffer;
    VkDeviceMemory                  instanceMemory;
    
    // Output image
    VkImage                         rtImage;
    VkImageView                     rtImageView;
    VkDeviceMemory                  rtImageMemory;
    VkBuffer                        readbackBuffer;
    VkDeviceMemory                  readbackMemory;
    void                           *readbackMapped;
    VkDeviceSize                   readbackSize;
    
    // Synchronization
    VkFence                         fence;
    VkSemaphore                     semaphore;
    
    // Ray tracing properties
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties;
    VkPhysicalDeviceProperties    deviceProps;

    // Capability flags
    qboolean                    hasRayTracingPipeline;
    qboolean                    hasAccelerationStructure;
    qboolean                    hasRayQuery;
    qboolean                    hasDeferredHostOps;
    qboolean                    hasRTMaintenance1;
} vkrtState_t;

static vkrtState_t vkrt;
static uint32_t rtOutputWidth;
static uint32_t rtOutputHeight;
static qboolean rtOutputInitialized;

typedef struct rtxBLASGPU_s {
    VkAccelerationStructureKHR as;
    VkBuffer asBuffer;
    VkDeviceMemory asMemory;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexMemory;
    VkBuffer materialBuffer;
    VkDeviceMemory materialMemory;
} rtxBLASGPU_t;

static void RTX_DestroyReadbackBuffer(void) {
    if (vkrt.readbackMapped) {
        vkUnmapMemory(vkrt.device, vkrt.readbackMemory);
        vkrt.readbackMapped = NULL;
    }
    if (vkrt.readbackBuffer) {
        vkDestroyBuffer(vkrt.device, vkrt.readbackBuffer, NULL);
        vkrt.readbackBuffer = VK_NULL_HANDLE;
    }
    if (vkrt.readbackMemory) {
        vkFreeMemory(vkrt.device, vkrt.readbackMemory, NULL);
        vkrt.readbackMemory = VK_NULL_HANDLE;
    }
    vkrt.readbackSize = 0;
}

static qboolean RTX_EnsureReadbackBuffer(VkDeviceSize size) {
    if (vkrt.readbackBuffer && size <= vkrt.readbackSize) {
        return qtrue;
    }

    RTX_DestroyReadbackBuffer();

    if (size == 0) {
        return qtrue;
    }

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vkrt.device, &bufferInfo, NULL, &vkrt.readbackBuffer) != VK_SUCCESS) {
        vkrt.readbackBuffer = VK_NULL_HANDLE;
        return qfalse;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(vkrt.device, vkrt.readbackBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = RTX_FindMemoryType(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };

    if (vkAllocateMemory(vkrt.device, &allocInfo, NULL, &vkrt.readbackMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt.device, vkrt.readbackBuffer, NULL);
        vkrt.readbackBuffer = VK_NULL_HANDLE;
        vkrt.readbackMemory = VK_NULL_HANDLE;
        return qfalse;
    }

    if (vkBindBufferMemory(vkrt.device, vkrt.readbackBuffer, vkrt.readbackMemory, 0) != VK_SUCCESS) {
        RTX_DestroyReadbackBuffer();
        return qfalse;
    }

    if (vkMapMemory(vkrt.device, vkrt.readbackMemory, 0, size, 0, &vkrt.readbackMapped) != VK_SUCCESS) {
        RTX_DestroyReadbackBuffer();
        return qfalse;
    }

    vkrt.readbackSize = size;
    return qtrue;
}

static qboolean RTX_DownloadColorBuffer(uint32_t width, uint32_t height) {
    if (!vkrt.device || vkrt.commandBuffer == VK_NULL_HANDLE || vkrt.rtImage == VK_NULL_HANDLE) {
        return qfalse;
    }

    qboolean copyRequired = (width == (uint32_t)glConfig.vidWidth &&
                             height == (uint32_t)glConfig.vidHeight);

    VkDeviceSize requiredSize = (VkDeviceSize)width * (VkDeviceSize)height * sizeof(float) * 4;

    if (copyRequired) {
        if (!RTX_EnsureReadbackBuffer(requiredSize)) {
            return qfalse;
        }
    }

    vkResetCommandBuffer(vkrt.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    if (vkBeginCommandBuffer(vkrt.commandBuffer, &beginInfo) != VK_SUCCESS) {
        return qfalse;
    }

    if (copyRequired) {
        VkBufferImageCopy copyRegion = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { width, height, 1 }
        };

        vkCmdCopyImageToBuffer(vkrt.commandBuffer,
                               vkrt.rtImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               vkrt.readbackBuffer, 1, &copyRegion);

        VkBufferMemoryBarrier bufferBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vkrt.readbackBuffer,
            .offset = 0,
            .size = requiredSize
        };

        vkCmdPipelineBarrier(vkrt.commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT,
                             0, 0, NULL, 1, &bufferBarrier, 0, NULL);
    }

    VkImageMemoryBarrier imageBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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

    vkCmdPipelineBarrier(vkrt.commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0, 0, NULL, 0, NULL, 1, &imageBarrier);

    if (vkEndCommandBuffer(vkrt.commandBuffer) != VK_SUCCESS) {
        return qfalse;
    }

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &vkrt.commandBuffer
    };

    vkResetFences(vkrt.device, 1, &vkrt.fence);
    if (vkQueueSubmit(vk.queue, 1, &submitInfo, vkrt.fence) != VK_SUCCESS) {
        return qfalse;
    }

    vkWaitForFences(vkrt.device, 1, &vkrt.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(vkrt.device, 1, &vkrt.fence);

    if (copyRequired && vkrt.readbackMapped) {
        RT_ProcessGpuFrame((const float *)vkrt.readbackMapped, (int)width, (int)height);
    }

    return qtrue;
}

static qboolean RTX_EnsureDebugOverlayPipeline(void);
static void RTX_DestroyDebugOverlayPipeline(void);
static qboolean RTX_UpdateDebugOverlayDescriptors(void);

static const char *RTX_VendorLabel(rtxGpuType_t type) {
    switch (type) {
    case RTX_GPU_NVIDIA:
        return "NVIDIA";
    case RTX_GPU_AMD:
        return "AMD";
    case RTX_GPU_INTEL:
        return "Intel";
    default:
        return "Unknown";
    }
}

static void RTX_SetNVIDIAArchitecture(const char *deviceName) {
    rtx.rayTracingTier = 1;
    Q_strncpyz(rtx.gpuArchitecture, "NVIDIA RT", sizeof(rtx.gpuArchitecture));

    if (!deviceName || !deviceName[0]) {
        return;
    }

    if (Q_stristr(deviceName, "RTX 40") ||
        Q_stristr(deviceName, "4090") ||
        Q_stristr(deviceName, "4080") ||
        Q_stristr(deviceName, "4070") ||
        Q_stristr(deviceName, "Ada")) {
        rtx.rayTracingTier = 3;
        Q_strncpyz(rtx.gpuArchitecture, "Ada Lovelace", sizeof(rtx.gpuArchitecture));
        return;
    }

    if (Q_stristr(deviceName, "RTX 30") ||
        Q_stristr(deviceName, "3090") ||
        Q_stristr(deviceName, "3080") ||
        Q_stristr(deviceName, "3070") ||
        Q_stristr(deviceName, "Ampere") ||
        Q_stristr(deviceName, "RTX A") ||
        Q_stristr(deviceName, "A40") ||
        Q_stristr(deviceName, "A5000") ||
        Q_stristr(deviceName, "L40")) {
        rtx.rayTracingTier = 2;
        Q_strncpyz(rtx.gpuArchitecture, "Ampere", sizeof(rtx.gpuArchitecture));
        return;
    }

    if (Q_stristr(deviceName, "RTX 20") ||
        Q_stristr(deviceName, "2080") ||
        Q_stristr(deviceName, "2070") ||
        Q_stristr(deviceName, "2060") ||
        Q_stristr(deviceName, "TITAN RTX") ||
        Q_stristr(deviceName, "Quadro RTX") ||
        Q_stristr(deviceName, "Turing")) {
        rtx.rayTracingTier = 1;
        Q_strncpyz(rtx.gpuArchitecture, "Turing", sizeof(rtx.gpuArchitecture));
        return;
    }
}

static void RTX_SetAMDArchitecture(const char *deviceName) {
    rtx.rayTracingTier = 1;
    Q_strncpyz(rtx.gpuArchitecture, "RDNA", sizeof(rtx.gpuArchitecture));

    if (!deviceName || !deviceName[0]) {
        return;
    }

    if (Q_stristr(deviceName, "7900") ||
        Q_stristr(deviceName, "7800") ||
        Q_stristr(deviceName, "7700")) {
        Q_strncpyz(rtx.gpuArchitecture, "RDNA 3", sizeof(rtx.gpuArchitecture));
        return;
    }

    if (Q_stristr(deviceName, "RX 6") ||
        Q_stristr(deviceName, "6900") ||
        Q_stristr(deviceName, "6800") ||
        Q_stristr(deviceName, "6700") ||
        Q_stristr(deviceName, "6600")) {
        Q_strncpyz(rtx.gpuArchitecture, "RDNA 2", sizeof(rtx.gpuArchitecture));
        return;
    }

    if (Q_stristr(deviceName, "5700") ||
        Q_stristr(deviceName, "5600") ||
        Q_stristr(deviceName, "5500")) {
        Q_strncpyz(rtx.gpuArchitecture, "RDNA 1", sizeof(rtx.gpuArchitecture));
    }
}

static void RTX_SetIntelArchitecture(const char *deviceName) {
    rtx.rayTracingTier = 1;
    Q_strncpyz(rtx.gpuArchitecture, "Xe", sizeof(rtx.gpuArchitecture));

    if (!deviceName || !deviceName[0]) {
        return;
    }

    if (Q_stristr(deviceName, "Arc")) {
        Q_strncpyz(rtx.gpuArchitecture, "Xe-HPG", sizeof(rtx.gpuArchitecture));
    }
}

static void RTX_DetectGPUVendor(const VkPhysicalDeviceProperties *props, const char **outVendorLabel) {
    const char *vendorLabel = "Unknown";

    rtx.gpuType = RTX_GPU_UNKNOWN;
    rtx.rayTracingTier = 0;
    rtx.gpuName[0] = '\0';
    rtx.gpuArchitecture[0] = '\0';

    if (props) {
        Q_strncpyz(rtx.gpuName, props->deviceName, sizeof(rtx.gpuName));

        switch (props->vendorID) {
        case 0x10DE:
            vendorLabel = "NVIDIA";
            rtx.gpuType = RTX_GPU_NVIDIA;
            RTX_SetNVIDIAArchitecture(props->deviceName);
            break;
        case 0x1002:
        case 0x1022:
            vendorLabel = "AMD";
            rtx.gpuType = RTX_GPU_AMD;
            RTX_SetAMDArchitecture(props->deviceName);
            break;
        case 0x8086:
            vendorLabel = "Intel";
            rtx.gpuType = RTX_GPU_INTEL;
            RTX_SetIntelArchitecture(props->deviceName);
            break;
        default:
            vendorLabel = "Unknown";
            break;
        }

        vkrt.deviceProps = *props;
    }

    if (rtx.rayTracingTier <= 0) {
        rtx.rayTracingTier = 1;
    }

    if (!rtx.gpuArchitecture[0]) {
        Q_strncpyz(rtx.gpuArchitecture, "Unknown", sizeof(rtx.gpuArchitecture));
    }

    if (outVendorLabel) {
        *outVendorLabel = vendorLabel;
    }
}

/*
================
RTX_FindMemoryType

Find suitable memory type for allocation
================
*/
static uint32_t RTX_FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(vkrt.physicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    ri.Printf(PRINT_WARNING, "RTX: Failed to find suitable memory type\n");
    return 0;
}

/*
================
RTX_CheckVulkanRTSupport

Check if Vulkan RT extensions are available
================
*/
static qboolean RTX_CheckVulkanRTSupport(void) {
    const char *vendorLabel = "Unknown";
    VkPhysicalDeviceProperties props;
    Com_Memset(&props, 0, sizeof(props));

    if (!vkrt.physicalDevice) {
        return qfalse;
    }

    vkGetPhysicalDeviceProperties(vkrt.physicalDevice, &props);
    RTX_DetectGPUVendor(&props, &vendorLabel);

    rtx.features = RTX_FEATURE_NONE;
    vkrt.hasRayTracingPipeline = qfalse;
    vkrt.hasAccelerationStructure = qfalse;
    vkrt.hasRayQuery = qfalse;
    vkrt.hasDeferredHostOps = qfalse;
    vkrt.hasRTMaintenance1 = qfalse;

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(vkrt.physicalDevice, NULL, &extensionCount, NULL);
    
    if (extensionCount == 0) {
        return qfalse;
    }
    
    VkExtensionProperties *extensions = Z_Malloc(sizeof(VkExtensionProperties) * extensionCount);
    vkEnumerateDeviceExtensionProperties(vkrt.physicalDevice, NULL, &extensionCount, extensions);
    
    qboolean hasRayTracing = qfalse;
    qboolean hasAccelStruct = qfalse;
    qboolean hasRayQuery = qfalse;
    qboolean hasDeferredOps = qfalse;
    qboolean hasMaintenance1 = qfalse;
    
    for (uint32_t i = 0; i < extensionCount; i++) {
        if (!strcmp(extensions[i].extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
            hasRayTracing = qtrue;
        }
        if (!strcmp(extensions[i].extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)) {
            hasAccelStruct = qtrue;
        }
        if (!strcmp(extensions[i].extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME)) {
            hasRayQuery = qtrue;
        }
#ifdef VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
        if (!strcmp(extensions[i].extensionName, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)) {
            hasDeferredOps = qtrue;
        }
#endif
#ifdef VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME
        if (!strcmp(extensions[i].extensionName, VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME)) {
            hasMaintenance1 = qtrue;
        }
#endif
    }
    
    Z_Free(extensions);

    vkrt.hasRayTracingPipeline = hasRayTracing;
    vkrt.hasAccelerationStructure = hasAccelStruct;
    vkrt.hasRayQuery = hasRayQuery;
    vkrt.hasDeferredHostOps = hasDeferredOps;
    vkrt.hasRTMaintenance1 = hasMaintenance1;
    
    if (!hasRayTracing || !hasAccelStruct) {
        ri.Printf(PRINT_WARNING, "RTX: Required Vulkan RT extensions not available\n");
        ri.Printf(PRINT_WARNING, "RTX: Ray Tracing: %s, Accel Struct: %s, Ray Query: %s\n",
                  hasRayTracing ? "YES" : "NO",
                  hasAccelStruct ? "YES" : "NO", 
                  hasRayQuery ? "YES" : "NO");
        return qfalse;
    }
    
    rtx.features |= (RTX_FEATURE_BASIC | RTX_FEATURE_RAY_TRACING);
    if (hasRayQuery) {
        rtx.features |= RTX_FEATURE_RAY_QUERY;
    }

#ifdef USE_OPTIX
    if (rtx.gpuType == RTX_GPU_NVIDIA) {
        rtx.features |= RTX_FEATURE_DENOISER;
    }
#endif
#ifdef USE_DLSS
    if (rtx.gpuType == RTX_GPU_NVIDIA) {
        rtx.features |= RTX_FEATURE_DLSS;
    }
#endif
#ifdef USE_REFLEX
    if (rtx.gpuType == RTX_GPU_NVIDIA) {
        rtx.features |= RTX_FEATURE_REFLEX;
    }
#endif

    ri.Printf(PRINT_ALL, "RTX: GPU detected: %s (%s)\n",
              rtx.gpuName[0] ? rtx.gpuName : "Unknown",
              vendorLabel);
    if (rtx.gpuArchitecture[0]) {
        ri.Printf(PRINT_ALL, "RTX: Architecture: %s (Tier %d)\n", rtx.gpuArchitecture, rtx.rayTracingTier);
    }
    ri.Printf(PRINT_ALL, "RTX: Vulkan RT extensions detected:\n");
    ri.Printf(PRINT_ALL, "  Ray Tracing Pipeline   : %s\n", hasRayTracing ? "YES" : "NO");
    ri.Printf(PRINT_ALL, "  Acceleration Structure : %s\n", hasAccelStruct ? "YES" : "NO");
    ri.Printf(PRINT_ALL, "  Ray Query              : %s\n", hasRayQuery ? "YES" : "NO");
#ifdef VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
    ri.Printf(PRINT_ALL, "  Deferred Host Ops      : %s\n", hasDeferredOps ? "YES" : "NO");
#endif
#ifdef VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME
    ri.Printf(PRINT_ALL, "  RT Maintenance 1       : %s\n", hasMaintenance1 ? "YES" : "NO");
#endif

    return qtrue;
}

static void RTX_LogCapabilitySummary(void) {
    const char *vendor = RTX_VendorLabel(rtx.gpuType);

    ri.Printf(PRINT_ALL, "\n========================================\n");
    ri.Printf(PRINT_ALL, "RTX Capability Summary\n");
    ri.Printf(PRINT_ALL, "========================================\n");
    ri.Printf(PRINT_ALL, "GPU: %s\n", rtx.gpuName[0] ? rtx.gpuName : "Unknown");
    ri.Printf(PRINT_ALL, "Vendor: %s (0x%04X)\n", vendor, vkrt.deviceProps.vendorID);
    if (rtx.gpuArchitecture[0]) {
        ri.Printf(PRINT_ALL, "Architecture: %s\n", rtx.gpuArchitecture);
    }
    ri.Printf(PRINT_ALL, "Ray Tracing Tier: %d\n", rtx.rayTracingTier);

    ri.Printf(PRINT_ALL, "\nExtensions:\n");
    ri.Printf(PRINT_ALL, "  Ray Tracing Pipeline   : %s\n", vkrt.hasRayTracingPipeline ? "YES" : "NO");
    ri.Printf(PRINT_ALL, "  Acceleration Structure : %s\n", vkrt.hasAccelerationStructure ? "YES" : "NO");
    ri.Printf(PRINT_ALL, "  Ray Query              : %s\n", vkrt.hasRayQuery ? "YES" : "NO");
#ifdef VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
    ri.Printf(PRINT_ALL, "  Deferred Host Ops      : %s\n", vkrt.hasDeferredHostOps ? "YES" : "NO");
#endif
#ifdef VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME
    ri.Printf(PRINT_ALL, "  RT Maintenance 1       : %s\n", vkrt.hasRTMaintenance1 ? "YES" : "NO");
#endif

    ri.Printf(PRINT_ALL, "\nFeatures:\n");
    ri.Printf(PRINT_ALL, "  [%c] Ray Tracing\n", (rtx.features & RTX_FEATURE_RAY_TRACING) ? 'x' : ' ');
    ri.Printf(PRINT_ALL, "  [%c] Ray Query\n", (rtx.features & RTX_FEATURE_RAY_QUERY) ? 'x' : ' ');
    ri.Printf(PRINT_ALL, "  [%c] Denoiser\n", (rtx.features & RTX_FEATURE_DENOISER) ? 'x' : ' ');
    ri.Printf(PRINT_ALL, "  [%c] DLSS\n", (rtx.features & RTX_FEATURE_DLSS) ? 'x' : ' ');
    ri.Printf(PRINT_ALL, "  [%c] Reflex\n", (rtx.features & RTX_FEATURE_REFLEX) ? 'x' : ' ');

    if (rtx.shaderGroupHandleSize > 0) {
        ri.Printf(PRINT_ALL, "\nRay Tracing Limits:\n");
        ri.Printf(PRINT_ALL, "  Max Recursion Depth    : %u\n", rtx.maxRayRecursionDepth);
        ri.Printf(PRINT_ALL, "  Shader Handle Size     : %u\n", rtx.shaderGroupHandleSize);
        ri.Printf(PRINT_ALL, "  Handle Alignment       : %u\n", rtx.shaderGroupHandleAlignment);
        ri.Printf(PRINT_ALL, "  Base Alignment         : %u\n", rtx.shaderGroupBaseAlignment);
        ri.Printf(PRINT_ALL, "  Max Primitive Count    : %llu\n", (unsigned long long)rtx.maxPrimitiveCount);
        ri.Printf(PRINT_ALL, "  Max Instance Count     : %llu\n", (unsigned long long)rtx.maxInstanceCount);
        ri.Printf(PRINT_ALL, "  Max Geometry Count     : %llu\n", (unsigned long long)rtx.maxGeometryCount);
    }

    ri.Printf(PRINT_ALL, "========================================\n");
}

/*
================
RTX_InitVulkanRT

Initialize Vulkan Ray Tracing
================
*/
qboolean RTX_InitVulkanRT(void) {
    // Check if we're using Vulkan renderer
    if (!vk.device || !vk.physical_device) {
        ri.Printf(PRINT_WARNING, "RTX: Vulkan renderer not active\n");
        return qfalse;
    }
    
    // Use the existing Vulkan device
    vkrt.device = vk.device;
    vkrt.physicalDevice = vk.physical_device;
    
	// Load RT extension functions
	PFN_vkCreateAccelerationStructureKHR createAccel = (PFN_vkCreateAccelerationStructureKHR)
		vkGetDeviceProcAddr(vkrt.device, "vkCreateAccelerationStructureKHR");
	PFN_vkDestroyAccelerationStructureKHR destroyAccel = (PFN_vkDestroyAccelerationStructureKHR)
		vkGetDeviceProcAddr(vkrt.device, "vkDestroyAccelerationStructureKHR");
	qvkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)
		vkGetDeviceProcAddr(vkrt.device, "vkGetAccelerationStructureBuildSizesKHR");
    qvkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)
        vkGetDeviceProcAddr(vkrt.device, "vkCmdBuildAccelerationStructuresKHR");
    qvkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)
        vkGetDeviceProcAddr(vkrt.device, "vkGetAccelerationStructureDeviceAddressKHR");
    qvkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)
        vkGetDeviceProcAddr(vkrt.device, "vkCmdTraceRaysKHR");
    qvkGetBufferDeviceAddress = (PFN_vkGetBufferDeviceAddress)
        vkGetDeviceProcAddr(vkrt.device, "vkGetBufferDeviceAddress");
    if (!qvkGetBufferDeviceAddress) {
        // Try KHR version
        qvkGetBufferDeviceAddress = (PFN_vkGetBufferDeviceAddress)
            vkGetDeviceProcAddr(vkrt.device, "vkGetBufferDeviceAddressKHR");
    }
    
    // Check for RT support
    if (!RTX_CheckVulkanRTSupport()) {
        return qfalse;
    }
    
    // Verify function pointers loaded
	if (!createAccel || !destroyAccel ||
		!qvkGetAccelerationStructureBuildSizesKHR || !qvkCmdBuildAccelerationStructuresKHR ||
		!qvkGetAccelerationStructureDeviceAddressKHR || !qvkCmdTraceRaysKHR ||
		!qvkGetBufferDeviceAddress) {
		ri.Printf(PRINT_WARNING, "RTX: Failed to load RT extension functions\n");
		return qfalse;
	}

	vk_register_acceleration_structure_dispatch(createAccel, destroyAccel);
    
    // Get RT properties
    vkrt.rtProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    vkrt.asProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    vkrt.rtProperties.pNext = &vkrt.asProperties;
    
    VkPhysicalDeviceProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &vkrt.rtProperties
    };
    
    vkGetPhysicalDeviceProperties2(vkrt.physicalDevice, &props2);
    
    rtx.maxRayRecursionDepth = vkrt.rtProperties.maxRayRecursionDepth;
    rtx.shaderGroupHandleSize = vkrt.rtProperties.shaderGroupHandleSize;
    rtx.shaderGroupHandleAlignment = vkrt.rtProperties.shaderGroupHandleAlignment;
    rtx.shaderGroupBaseAlignment = vkrt.rtProperties.shaderGroupBaseAlignment;
    rtx.maxPrimitiveCount = vkrt.asProperties.maxPrimitiveCount;
    rtx.maxInstanceCount = vkrt.asProperties.maxInstanceCount;
    rtx.maxGeometryCount = vkrt.asProperties.maxGeometryCount;

    RTX_LogCapabilitySummary();
    
    // Create command pool for RT commands
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk.queue_family_index
    };
    
    VkResult result = vkCreateCommandPool(vkrt.device, &poolInfo, NULL, &vkrt.commandPool);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create command pool\n");
        return qfalse;
    }
    
    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vkrt.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    
    result = vkAllocateCommandBuffers(vkrt.device, &allocInfo, &vkrt.commandBuffer);
    if (result == VK_SUCCESS) {
        RTX_DEBUG_LOG_CMD("alloc", vkrt.commandBuffer, "RTX_InitVulkanRT");
        vk_cmd_register("rtx_main", vkrt.commandBuffer, vkrt.commandPool);
    }
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to allocate command buffer\n");
        vkDestroyCommandPool(vkrt.device, vkrt.commandPool, NULL);
        return qfalse;
    }
    
    // Create synchronization objects
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    
    result = vkCreateFence(vkrt.device, &fenceInfo, NULL, &vkrt.fence);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create fence\n");
        RTX_ShutdownVulkanRT();
        return qfalse;
    }
    
    VkSemaphoreCreateInfo semaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    
    result = vkCreateSemaphore(vkrt.device, &semaphoreInfo, NULL, &vkrt.semaphore);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create semaphore\n");
        RTX_ShutdownVulkanRT();
        return qfalse;
    }
    
    ri.Printf(PRINT_ALL, "RTX: Vulkan Ray Tracing initialized successfully\n");
    return qtrue;
}

/*
================
RTX_ShutdownVulkanRT

Cleanup Vulkan RT resources
================
*/
void RTX_ShutdownVulkanRT(void) {
    if (!vkrt.device) {
        return;
    }
    
    // Wait for device to idle
    vkDeviceWaitIdle(vkrt.device);

    RTX_DestroyDebugOverlayPipeline();
    RTX_DestroyReadbackBuffer();

    // Destroy RT resources
    for (int i = 0; i < 2; i++) {
        if (vkrt.tlas[i]) {
            qvkDestroyAccelerationStructureKHR(vkrt.device, vkrt.tlas[i], NULL);
            vkrt.tlas[i] = VK_NULL_HANDLE;
        }
        if (vkrt.tlasBuffer[i]) {
            vkDestroyBuffer(vkrt.device, vkrt.tlasBuffer[i], NULL);
            vkrt.tlasBuffer[i] = VK_NULL_HANDLE;
        }
        if (vkrt.tlasMemory[i]) {
            vkFreeMemory(vkrt.device, vkrt.tlasMemory[i], NULL);
            vkrt.tlasMemory[i] = VK_NULL_HANDLE;
        }
    }
    vkrt.activeTLAS = 0;
    
    if (vkrt.instanceBuffer) {
        vkDestroyBuffer(vkrt.device, vkrt.instanceBuffer, NULL);
    }
    if (vkrt.instanceMemory) {
        vkFreeMemory(vkrt.device, vkrt.instanceMemory, NULL);
    }
    
    // Destroy SBT buffers
    if (vkrt.raygenSBT) {
        vkDestroyBuffer(vkrt.device, vkrt.raygenSBT, NULL);
    }
    if (vkrt.missSBT) {
        vkDestroyBuffer(vkrt.device, vkrt.missSBT, NULL);
    }
    if (vkrt.hitSBT) {
        vkDestroyBuffer(vkrt.device, vkrt.hitSBT, NULL);
    }
    if (vkrt.sbtMemory) {
        vkFreeMemory(vkrt.device, vkrt.sbtMemory, NULL);
    }
    
    if (vkrt.rtPipeline) {
        vkDestroyPipeline(vkrt.device, vkrt.rtPipeline, NULL);
    }
    if (vkrt.pipelineLayout) {
        vkDestroyPipelineLayout(vkrt.device, vkrt.pipelineLayout, NULL);
    }
    
    if (vkrt.semaphore) {
        vkDestroySemaphore(vkrt.device, vkrt.semaphore, NULL);
    }
    if (vkrt.fence) {
        vkDestroyFence(vkrt.device, vkrt.fence, NULL);
    }
    
    if (vkrt.commandBuffer != VK_NULL_HANDLE && vkrt.commandPool != VK_NULL_HANDLE) {
        RTX_DEBUG_LOG_CMD("free", vkrt.commandBuffer, "RTX_Shutdown");
        vk_cmd_unregister(vkrt.commandBuffer);
        vkFreeCommandBuffers(vkrt.device, vkrt.commandPool, 1, &vkrt.commandBuffer);
        vkrt.commandBuffer = VK_NULL_HANDLE;
    }

    if (vkrt.commandPool) {
        vkDestroyCommandPool(vkrt.device, vkrt.commandPool, NULL);
        vkrt.commandPool = VK_NULL_HANDLE;
	}
	
	Com_Memset(&vkrt, 0, sizeof(vkrt));
	ri.Printf(PRINT_ALL, "RTX: Vulkan RT shutdown complete\n");
}

void RTX_ResetTLASGPU(void) {
	if (!vkrt.device) {
		return;
	}

	vkDeviceWaitIdle(vkrt.device);

	for (int i = 0; i < 2; i++) {
		if (vkrt.tlas[i]) {
			qvkDestroyAccelerationStructureKHR(vkrt.device, vkrt.tlas[i], NULL);
			vkrt.tlas[i] = VK_NULL_HANDLE;
		}
		if (vkrt.tlasBuffer[i]) {
			vkDestroyBuffer(vkrt.device, vkrt.tlasBuffer[i], NULL);
			vkrt.tlasBuffer[i] = VK_NULL_HANDLE;
		}
		if (vkrt.tlasMemory[i]) {
			vkFreeMemory(vkrt.device, vkrt.tlasMemory[i], NULL);
			vkrt.tlasMemory[i] = VK_NULL_HANDLE;
		}
	}

	vkrt.activeTLAS = 0;
}

qboolean RTX_RayQuerySupported(void) {
    return vkrt.hasRayQuery ? qtrue : qfalse;
}

qboolean RTX_DispatchShadowQueries(rtShadowQuery_t *queries, int count) {
    if (!vkrt.hasRayQuery || !queries || count <= 0) {
        return qfalse;
    }

    if (!RTX_RayQueryUpload(queries, count)) {
        return qfalse;
    }

    VkPipeline pipeline = RTX_GetRayQueryPipelineHandle();
    VkPipelineLayout layout = RTX_GetPipelineLayout();
    VkDescriptorSet descriptorSet = RTX_GetDescriptorSet();
    VkBuffer queryBuffer = RTX_RayQueryGetBuffer();

    if (!pipeline || !layout || !descriptorSet || queryBuffer == VK_NULL_HANDLE) {
        return qfalse;
    }

    if (!vkrt.commandBuffer) {
        return qfalse;
    }

    vkResetCommandBuffer(vkrt.commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    if (vkBeginCommandBuffer(vkrt.commandBuffer, &beginInfo) != VK_SUCCESS) {
        return qfalse;
    }

    vkCmdBindPipeline(vkrt.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(vkrt.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            layout, 0, 1, &descriptorSet, 0, NULL);

    uint32_t queryCount = (uint32_t)count;
    vkCmdPushConstants(vkrt.commandBuffer, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(uint32_t), &queryCount);

    uint32_t groupCount = (queryCount + 63) / 64;
    if (groupCount == 0) {
        groupCount = 1;
    }

    vkCmdDispatch(vkrt.commandBuffer, groupCount, 1, 1);

    VkBufferMemoryBarrier bufferBarrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = queryBuffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE
    };

    vkCmdPipelineBarrier(vkrt.commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, NULL, 1, &bufferBarrier, 0, NULL);

    if (vkEndCommandBuffer(vkrt.commandBuffer) != VK_SUCCESS) {
        return qfalse;
    }

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &vkrt.commandBuffer
    };

    vkResetFences(vkrt.device, 1, &vkrt.fence);
    if (vkQueueSubmit(vk.queue, 1, &submitInfo, vkrt.fence) != VK_SUCCESS) {
        return qfalse;
    }

    vkWaitForFences(vkrt.device, 1, &vkrt.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(vkrt.device, 1, &vkrt.fence);

    RTX_RayQueryDownload(queries, count);
    return qtrue;
}

/*
================
RTX_CreateBLASVulkan

Internal function to create Vulkan BLAS
================
*/
static VkAccelerationStructureKHR RTX_CreateBLASVulkan(const VkAccelerationStructureGeometryKHR *geometry,
                                                       const VkAccelerationStructureBuildRangeInfoKHR *range,
                                                       VkBuffer *blasBuffer, VkDeviceMemory *blasMemory) {
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = geometry
    };
    
    // Get required sizes
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };
    
    uint32_t primitiveCount = range->primitiveCount;
    qvkGetAccelerationStructureBuildSizesKHR(vkrt.device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo, &primitiveCount, &sizeInfo);
    
    // Create buffer for AS
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeInfo.accelerationStructureSize,
        .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    };
    
    if (vkCreateBuffer(vkrt.device, &bufferInfo, NULL, blasBuffer) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    // Allocate memory with device address support
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(vkrt.device, *blasBuffer, &memReqs);
    
    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    };
    
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &memoryAllocateFlagsInfo,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = RTX_FindMemoryType(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    
    if (vkAllocateMemory(vkrt.device, &allocInfo, NULL, blasMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt.device, *blasBuffer, NULL);
        return VK_NULL_HANDLE;
    }
    
    vkBindBufferMemory(vkrt.device, *blasBuffer, *blasMemory, 0);
    
    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = *blasBuffer,
        .size = sizeInfo.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
    };
    
    VkAccelerationStructureKHR blas;
    if (qvkCreateAccelerationStructureKHR(vkrt.device, &createInfo, NULL, &blas) != VK_SUCCESS) {
        vkFreeMemory(vkrt.device, *blasMemory, NULL);
        vkDestroyBuffer(vkrt.device, *blasBuffer, NULL);
        return VK_NULL_HANDLE;
    }
    
    // Allocate scratch buffer
    VkBuffer scratchBuffer;
    VkDeviceMemory scratchMemory;
    scratchBuffer = RTX_AllocateScratchBuffer(sizeInfo.buildScratchSize, &scratchMemory);
    
    if (!scratchBuffer) {
        qvkDestroyAccelerationStructureKHR(vkrt.device, blas, NULL);
        vkFreeMemory(vkrt.device, *blasMemory, NULL);
        vkDestroyBuffer(vkrt.device, *blasBuffer, NULL);
        return VK_NULL_HANDLE;
    }
    
    // Build the BLAS
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    
    vkResetCommandBuffer(vkrt.commandBuffer, 0);
    vkBeginCommandBuffer(vkrt.commandBuffer, &beginInfo);
    
    buildInfo.dstAccelerationStructure = blas;
    buildInfo.scratchData.deviceAddress = RTX_GetBufferDeviceAddressVK(scratchBuffer);
    
    const VkAccelerationStructureBuildRangeInfoKHR *rangeInfos[] = { range };
    qvkCmdBuildAccelerationStructuresKHR(vkrt.commandBuffer, 1, &buildInfo, rangeInfos);
    
    // Add memory barrier for AS build
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
    };
    
    vkCmdPipelineBarrier(vkrt.commandBuffer,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        0, 1, &barrier, 0, NULL, 0, NULL);
    
    vkEndCommandBuffer(vkrt.commandBuffer);
    
    // Submit and wait
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &vkrt.commandBuffer
    };
    
    vkResetFences(vkrt.device, 1, &vkrt.fence);
    vkQueueSubmit(vk.queue, 1, &submitInfo, vkrt.fence);
    vkWaitForFences(vkrt.device, 1, &vkrt.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(vkrt.device, 1, &vkrt.fence);
    
    // Clean up scratch buffer
    vkDestroyBuffer(vkrt.device, scratchBuffer, NULL);
    vkFreeMemory(vkrt.device, scratchMemory, NULL);
    
    return blas;
}

static qboolean RTX_CreateBufferWithData(VkDeviceSize size, VkBufferUsageFlags usage,
                                         VkMemoryPropertyFlags properties, const void *srcData,
                                         VkBuffer *outBuffer, VkDeviceMemory *outMemory) {
    if (vkCreateBuffer == NULL) {
        return qfalse;
    }

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vkrt.device, &bufferInfo, NULL, outBuffer) != VK_SUCCESS) {
        return qfalse;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(vkrt.device, *outBuffer, &memReqs);

    VkMemoryAllocateFlagsInfo allocFlags = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    };

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &allocFlags,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = RTX_FindMemoryType(memReqs.memoryTypeBits, properties)
    };

    if (vkAllocateMemory(vkrt.device, &allocInfo, NULL, outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt.device, *outBuffer, NULL);
        *outBuffer = VK_NULL_HANDLE;
        return qfalse;
    }

    if (vkBindBufferMemory(vkrt.device, *outBuffer, *outMemory, 0) != VK_SUCCESS) {
        vkFreeMemory(vkrt.device, *outMemory, NULL);
        vkDestroyBuffer(vkrt.device, *outBuffer, NULL);
        *outMemory = VK_NULL_HANDLE;
        *outBuffer = VK_NULL_HANDLE;
        return qfalse;
    }

    if (!srcData) {
        return qtrue;
    }

    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        void *mapped = NULL;
        if (vkMapMemory(vkrt.device, *outMemory, 0, size, 0, &mapped) != VK_SUCCESS) {
            vkDestroyBuffer(vkrt.device, *outBuffer, NULL);
            vkFreeMemory(vkrt.device, *outMemory, NULL);
            *outMemory = VK_NULL_HANDLE;
            *outBuffer = VK_NULL_HANDLE;
            return qfalse;
        }

        memcpy(mapped, srcData, size);
        vkUnmapMemory(vkrt.device, *outMemory);
        return qtrue;
    }

    // Device-local path: create staging buffer
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo stagingInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vkrt.device, &stagingInfo, NULL, &stagingBuffer) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt.device, *outBuffer, NULL);
        vkFreeMemory(vkrt.device, *outMemory, NULL);
        *outMemory = VK_NULL_HANDLE;
        *outBuffer = VK_NULL_HANDLE;
        return qfalse;
    }

    VkMemoryRequirements stagingReqs;
    vkGetBufferMemoryRequirements(vkrt.device, stagingBuffer, &stagingReqs);

    VkMemoryAllocateInfo stagingAlloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = stagingReqs.size,
        .memoryTypeIndex = RTX_FindMemoryType(
            stagingReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };

    if (vkAllocateMemory(vkrt.device, &stagingAlloc, NULL, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt.device, stagingBuffer, NULL);
        vkDestroyBuffer(vkrt.device, *outBuffer, NULL);
        vkFreeMemory(vkrt.device, *outMemory, NULL);
        *outMemory = VK_NULL_HANDLE;
        *outBuffer = VK_NULL_HANDLE;
        return qfalse;
    }

    vkBindBufferMemory(vkrt.device, stagingBuffer, stagingMemory, 0);

    void *mapped = NULL;
    if (vkMapMemory(vkrt.device, stagingMemory, 0, size, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt.device, stagingMemory, NULL);
        vkDestroyBuffer(vkrt.device, *outBuffer, NULL);
        vkFreeMemory(vkrt.device, *outMemory, NULL);
        *outMemory = VK_NULL_HANDLE;
        *outBuffer = VK_NULL_HANDLE;
        return qfalse;
    }

    memcpy(mapped, srcData, size);
    vkUnmapMemory(vkrt.device, stagingMemory);

    // Record transfer
    vkResetCommandBuffer(vkrt.commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    if (vkBeginCommandBuffer(vkrt.commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt.device, stagingMemory, NULL);
        vkDestroyBuffer(vkrt.device, *outBuffer, NULL);
        vkFreeMemory(vkrt.device, *outMemory, NULL);
        *outMemory = VK_NULL_HANDLE;
        *outBuffer = VK_NULL_HANDLE;
        return qfalse;
    }

    VkBufferCopy copyRegion = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = size
    };

    vkCmdCopyBuffer(vkrt.commandBuffer, stagingBuffer, *outBuffer, 1, &copyRegion);

    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                         VK_ACCESS_SHADER_READ_BIT
    };

    vkCmdPipelineBarrier(vkrt.commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        0, 1, &barrier, 0, NULL, 0, NULL);

    vkEndCommandBuffer(vkrt.commandBuffer);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &vkrt.commandBuffer
    };

    vkResetFences(vkrt.device, 1, &vkrt.fence);
    vkQueueSubmit(vk.queue, 1, &submitInfo, vkrt.fence);
    vkWaitForFences(vkrt.device, 1, &vkrt.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(vkrt.device, 1, &vkrt.fence);

    vkDestroyBuffer(vkrt.device, stagingBuffer, NULL);
    vkFreeMemory(vkrt.device, stagingMemory, NULL);

    return qtrue;
}

qboolean RTX_BuildBLASGPU(rtxBLAS_t *blas) {
    if (!blas || blas->gpuData) {
        return blas ? qtrue : qfalse;
    }

    if (!vkrt.device || !qvkCreateAccelerationStructureKHR) {
        return qfalse;
    }

    VkDeviceSize vertexSize = sizeof(vec3_t) * (VkDeviceSize)blas->numVertices;
    VkDeviceSize indexSize = sizeof(uint32_t) * (VkDeviceSize)blas->numTriangles * 3;

    VkMemoryPropertyFlags vertexProps = blas->isDynamic
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkBufferUsageFlags vertexUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!(vertexProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        vertexUsage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    if (!RTX_CreateBufferWithData(vertexSize,
                                  vertexUsage,
                                  vertexProps,
                                  blas->vertices,
                                  &vertexBuffer, &vertexMemory)) {
        return qfalse;
    }

    VkMemoryPropertyFlags indexProps = blas->isDynamic
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkBufferUsageFlags indexUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!(indexProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        indexUsage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    if (!RTX_CreateBufferWithData(indexSize,
                                  indexUsage,
                                  indexProps,
                                  blas->indices,
                                  &indexBuffer, &indexMemory)) {
        vkDestroyBuffer(vkrt.device, vertexBuffer, NULL);
        vkFreeMemory(vkrt.device, vertexMemory, NULL);
        return qfalse;
    }

    VkBuffer materialBuffer = VK_NULL_HANDLE;
    VkDeviceMemory materialMemory = VK_NULL_HANDLE;
    if (blas->triangleMaterials && blas->numTriangles > 0) {
        VkDeviceSize materialSize = sizeof(uint32_t) * (VkDeviceSize)blas->numTriangles;
        VkBufferUsageFlags materialUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                           VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        if (!RTX_CreateBufferWithData(materialSize,
                                      materialUsage,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      blas->triangleMaterials,
                                      &materialBuffer, &materialMemory)) {
            vkDestroyBuffer(vkrt.device, indexBuffer, NULL);
            vkFreeMemory(vkrt.device, indexMemory, NULL);
            vkDestroyBuffer(vkrt.device, vertexBuffer, NULL);
            vkFreeMemory(vkrt.device, vertexMemory, NULL);
            return qfalse;
        }
    }

    VkDeviceAddress vertexAddress = RTX_GetBufferDeviceAddressVK(vertexBuffer);
    VkDeviceAddress indexAddress = RTX_GetBufferDeviceAddressVK(indexBuffer);

    if (!vertexAddress || !indexAddress) {
        if (materialBuffer) {
            vkDestroyBuffer(vkrt.device, materialBuffer, NULL);
            vkFreeMemory(vkrt.device, materialMemory, NULL);
        }
        vkDestroyBuffer(vkrt.device, indexBuffer, NULL);
        vkFreeMemory(vkrt.device, indexMemory, NULL);
        vkDestroyBuffer(vkrt.device, vertexBuffer, NULL);
        vkFreeMemory(vkrt.device, vertexMemory, NULL);
        return qfalse;
    }

    VkAccelerationStructureGeometryTrianglesDataKHR triangles = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData.deviceAddress = vertexAddress,
        .vertexStride = sizeof(vec3_t),
        .maxVertex = (uint32_t)blas->numVertices,
        .indexType = VK_INDEX_TYPE_UINT32,
        .indexData.deviceAddress = indexAddress,
        .transformData.deviceAddress = 0
    };

    VkAccelerationStructureGeometryKHR geometry = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry.triangles = triangles,
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR
    };

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {
        .primitiveCount = (uint32_t)blas->numTriangles,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0
    };

    VkBuffer blasBuffer = VK_NULL_HANDLE;
    VkDeviceMemory blasMemory = VK_NULL_HANDLE;
    VkAccelerationStructureKHR asHandle = RTX_CreateBLASVulkan(&geometry, &rangeInfo,
                                                                &blasBuffer, &blasMemory);

    if (asHandle == VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt.device, indexBuffer, NULL);
        vkFreeMemory(vkrt.device, indexMemory, NULL);
        vkDestroyBuffer(vkrt.device, vertexBuffer, NULL);
        vkFreeMemory(vkrt.device, vertexMemory, NULL);
        if (materialBuffer) {
            vkDestroyBuffer(vkrt.device, materialBuffer, NULL);
            vkFreeMemory(vkrt.device, materialMemory, NULL);
        }
        return qfalse;
    }

    rtxBLASGPU_t *gpu = Z_Malloc(sizeof(*gpu));
    gpu->as = asHandle;
    gpu->asBuffer = blasBuffer;
    gpu->asMemory = blasMemory;
    gpu->vertexBuffer = vertexBuffer;
    gpu->vertexMemory = vertexMemory;
    gpu->indexBuffer = indexBuffer;
    gpu->indexMemory = indexMemory;
    gpu->materialBuffer = materialBuffer;
    gpu->materialMemory = materialMemory;

    blas->handle = (void*)(uintptr_t)asHandle;
    blas->gpuData = gpu;

    return qtrue;
}

void RTX_DestroyBLASGPU(rtxBLAS_t *blas) {
    if (!blas || !blas->gpuData) {
        return;
    }

    rtxBLASGPU_t *gpu = (rtxBLASGPU_t *)blas->gpuData;

    if (gpu->as) {
        qvkDestroyAccelerationStructureKHR(vkrt.device, gpu->as, NULL);
    }
    if (gpu->asBuffer) {
        vkDestroyBuffer(vkrt.device, gpu->asBuffer, NULL);
    }
    if (gpu->asMemory) {
        vkFreeMemory(vkrt.device, gpu->asMemory, NULL);
    }
    if (gpu->vertexBuffer) {
        vkDestroyBuffer(vkrt.device, gpu->vertexBuffer, NULL);
    }
    if (gpu->vertexMemory) {
        vkFreeMemory(vkrt.device, gpu->vertexMemory, NULL);
    }
    if (gpu->indexBuffer) {
        vkDestroyBuffer(vkrt.device, gpu->indexBuffer, NULL);
    }
    if (gpu->indexMemory) {
        vkFreeMemory(vkrt.device, gpu->indexMemory, NULL);
    }
    if (gpu->materialBuffer) {
        vkDestroyBuffer(vkrt.device, gpu->materialBuffer, NULL);
    }
    if (gpu->materialMemory) {
        vkFreeMemory(vkrt.device, gpu->materialMemory, NULL);
    }

    Z_Free(gpu);
    blas->gpuData = NULL;
    blas->handle = NULL;
}

/*
================
RTX_BuildAccelerationStructureVK

Build TLAS from BLAS instances
================
*/
void RTX_BuildAccelerationStructureVK(void) {
    if (!vkrt.device || rtx.tlas.numInstances == 0) {
        return;
    }
    
    float startTime = ri.Milliseconds();
    
    // Build instance data
    VkAccelerationStructureInstanceKHR *instances = Z_Malloc(
        sizeof(VkAccelerationStructureInstanceKHR) * rtx.tlas.numInstances);

    uint32_t totalTriangleMaterials = 0;
    for (int i = 0; i < rtx.tlas.numInstances; i++) {
        rtxInstance_t *inst = &rtx.tlas.instances[i];
        if (inst->blas) {
            totalTriangleMaterials += inst->blas->numTriangles;
        }
    }

    uint32_t *triangleMaterialAtlas = NULL;
    if (totalTriangleMaterials > 0) {
        triangleMaterialAtlas = Z_Malloc(sizeof(uint32_t) * totalTriangleMaterials);
    }
    uint32_t currentMaterialOffset = 0;
    
    for (int i = 0; i < rtx.tlas.numInstances; i++) {
        rtxInstance_t *inst = &rtx.tlas.instances[i];
        VkAccelerationStructureInstanceKHR *vkInst = &instances[i];

        // Copy transform matrix (3x4 row-major)
        Com_Memcpy(vkInst->transform.matrix, inst->transform, sizeof(float) * 12);

        inst->triangleMaterialOffset = currentMaterialOffset;
        inst->triangleMaterialCount = (inst->blas) ? inst->blas->numTriangles : 0;

        if (inst->triangleMaterialCount > 0 && triangleMaterialAtlas) {
            if (inst->blas && inst->blas->triangleMaterials) {
                Com_Memcpy(triangleMaterialAtlas + currentMaterialOffset,
                           inst->blas->triangleMaterials,
                           sizeof(uint32_t) * inst->triangleMaterialCount);
            } else {
                Com_Memset(triangleMaterialAtlas + currentMaterialOffset, 0,
                           sizeof(uint32_t) * inst->triangleMaterialCount);
            }
        }
        vkInst->instanceCustomIndex = inst->triangleMaterialOffset;
        currentMaterialOffset += inst->triangleMaterialCount;
        vkInst->mask = inst->mask;
        vkInst->instanceShaderBindingTableRecordOffset = inst->shaderOffset;
        vkInst->flags = inst->flags | VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

        // Get BLAS device address
        if (inst->blas) {
            if (!inst->blas->gpuData) {
                RTX_BuildBLASGPU(inst->blas);
            }

            if (inst->blas->gpuData && inst->blas->handle) {
            VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
                .accelerationStructure = (VkAccelerationStructureKHR)inst->blas->handle
            };
            vkInst->accelerationStructureReference = 
                qvkGetAccelerationStructureDeviceAddressKHR(vkrt.device, &addressInfo);
            } else {
                vkInst->accelerationStructureReference = 0;
            }
        } else {
            vkInst->accelerationStructureReference = 0;
        }
    }
    
    // Create or update instance buffer
    size_t instanceDataSize = sizeof(VkAccelerationStructureInstanceKHR) * rtx.tlas.numInstances;
    
    if (!vkrt.instanceBuffer) {
        // Create instance buffer
        VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = instanceDataSize,
            .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT
        };
        
        if (vkCreateBuffer(vkrt.device, &bufferInfo, NULL, &vkrt.instanceBuffer) != VK_SUCCESS) {
            Z_Free(instances);
            return;
        }
        
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(vkrt.device, vkrt.instanceBuffer, &memReqs);
        
        VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
            .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
        };
        
        VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = &memoryAllocateFlagsInfo,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = RTX_FindMemoryType(memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        };
        
        if (vkAllocateMemory(vkrt.device, &allocInfo, NULL, &vkrt.instanceMemory) != VK_SUCCESS) {
            vkDestroyBuffer(vkrt.device, vkrt.instanceBuffer, NULL);
            vkrt.instanceBuffer = VK_NULL_HANDLE;
            Z_Free(instances);
            return;
        }
        
        vkBindBufferMemory(vkrt.device, vkrt.instanceBuffer, vkrt.instanceMemory, 0);
    }
    
    // Upload instance data
    void *data;
    vkMapMemory(vkrt.device, vkrt.instanceMemory, 0, instanceDataSize, 0, &data);
    Com_Memcpy(data, instances, instanceDataSize);
    vkUnmapMemory(vkrt.device, vkrt.instanceMemory);
    
    Z_Free(instances);
    
    // Setup TLAS geometry
    VkDeviceAddress instanceBufferAddress = RTX_GetBufferDeviceAddressVK(vkrt.instanceBuffer);
    
    VkAccelerationStructureGeometryKHR tlasGeometry = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
        .geometry.instances = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .arrayOfPointers = VK_FALSE,
            .data.deviceAddress = instanceBufferAddress
        }
    };
    
    int buildIndex = (vkrt.activeTLAS + 1) & 1;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                 VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &tlasGeometry
    };

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };

    uint32_t instanceCount = rtx.tlas.numInstances;
    qvkGetAccelerationStructureBuildSizesKHR(vkrt.device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo, &instanceCount, &sizeInfo);

    qboolean needsAllocate = (vkrt.tlas[buildIndex] == VK_NULL_HANDLE) ||
                             (sizeInfo.accelerationStructureSize > rtx.tlas.scratchSize);

    if (needsAllocate) {
        if (vkrt.tlas[buildIndex]) {
            qvkDestroyAccelerationStructureKHR(vkrt.device, vkrt.tlas[buildIndex], NULL);
            vkrt.tlas[buildIndex] = VK_NULL_HANDLE;
        }
        if (vkrt.tlasBuffer[buildIndex]) {
            vkDestroyBuffer(vkrt.device, vkrt.tlasBuffer[buildIndex], NULL);
            vkrt.tlasBuffer[buildIndex] = VK_NULL_HANDLE;
        }
        if (vkrt.tlasMemory[buildIndex]) {
            vkFreeMemory(vkrt.device, vkrt.tlasMemory[buildIndex], NULL);
            vkrt.tlasMemory[buildIndex] = VK_NULL_HANDLE;
        }

        VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeInfo.accelerationStructureSize,
            .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        };

        if (vkCreateBuffer(vkrt.device, &bufferInfo, NULL, &vkrt.tlasBuffer[buildIndex]) != VK_SUCCESS) {
            return;
        }

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(vkrt.device, vkrt.tlasBuffer[buildIndex], &memReqs);

        VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
            .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
        };

        VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = &memoryAllocateFlagsInfo,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = RTX_FindMemoryType(memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        };

        if (vkAllocateMemory(vkrt.device, &allocInfo, NULL, &vkrt.tlasMemory[buildIndex]) != VK_SUCCESS) {
            vkDestroyBuffer(vkrt.device, vkrt.tlasBuffer[buildIndex], NULL);
            vkrt.tlasBuffer[buildIndex] = VK_NULL_HANDLE;
            return;
        }

        vkBindBufferMemory(vkrt.device, vkrt.tlasBuffer[buildIndex], vkrt.tlasMemory[buildIndex], 0);

        VkAccelerationStructureCreateInfoKHR createInfo = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = vkrt.tlasBuffer[buildIndex],
            .size = sizeInfo.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
        };

        if (qvkCreateAccelerationStructureKHR(vkrt.device, &createInfo, NULL, &vkrt.tlas[buildIndex]) != VK_SUCCESS) {
            vkFreeMemory(vkrt.device, vkrt.tlasMemory[buildIndex], NULL);
            vkDestroyBuffer(vkrt.device, vkrt.tlasBuffer[buildIndex], NULL);
            vkrt.tlasBuffer[buildIndex] = VK_NULL_HANDLE;
            vkrt.tlasMemory[buildIndex] = VK_NULL_HANDLE;
            return;
        }

        rtx.tlas.scratchSize = sizeInfo.accelerationStructureSize;
    }
    
    // Allocate scratch buffer
    VkBuffer scratchBuffer;
    VkDeviceMemory scratchMemory;
    scratchBuffer = RTX_AllocateScratchBuffer(sizeInfo.buildScratchSize, &scratchMemory);
    
    if (!scratchBuffer) {
        return;
    }
    
    // Build TLAS
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    
    vkResetCommandBuffer(vkrt.commandBuffer, 0);
    vkBeginCommandBuffer(vkrt.commandBuffer, &beginInfo);

    RTX_UploadTriangleMaterials(vkrt.commandBuffer,
                                triangleMaterialAtlas,
                                totalTriangleMaterials);
    
    // Build range info
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {
        .primitiveCount = rtx.tlas.numInstances,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0
    };
    
    buildInfo.dstAccelerationStructure = vkrt.tlas[buildIndex];
    buildInfo.scratchData.deviceAddress = RTX_GetBufferDeviceAddressVK(scratchBuffer);
    
    const VkAccelerationStructureBuildRangeInfoKHR *rangeInfos[] = { &rangeInfo };
    qvkCmdBuildAccelerationStructuresKHR(vkrt.commandBuffer, 1, &buildInfo, rangeInfos);
    
    // Add memory barrier
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
    };
    
    vkCmdPipelineBarrier(vkrt.commandBuffer,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        0, 1, &barrier, 0, NULL, 0, NULL);
    
    vkEndCommandBuffer(vkrt.commandBuffer);
    
    // Submit and wait
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &vkrt.commandBuffer
    };
    
    vkResetFences(vkrt.device, 1, &vkrt.fence);
    vkQueueSubmit(vk.queue, 1, &submitInfo, vkrt.fence);
    vkWaitForFences(vkrt.device, 1, &vkrt.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(vkrt.device, 1, &vkrt.fence);
    
    // Clean up scratch buffer
    vkDestroyBuffer(vkrt.device, scratchBuffer, NULL);
    vkFreeMemory(vkrt.device, scratchMemory, NULL);
    
    if (triangleMaterialAtlas) {
        Z_Free(triangleMaterialAtlas);
    }

    rtx.buildTime = ri.Milliseconds() - startTime;
    vkrt.activeTLAS = buildIndex;

    VkAccelerationStructureKHR activeTLAS = vkrt.tlas[vkrt.activeTLAS];
    rtx.tlas.handle = (void*)(uintptr_t)activeTLAS;
    rtx.tlas.handles[vkrt.activeTLAS] = rtx.tlas.handle;
    rtx.tlas.activeHandle = vkrt.activeTLAS;
    rtx.tlas.needsRebuild = qfalse;
}

/*
================
RTX_DispatchRaysVK

Dispatch ray tracing with full pipeline state
================
*/
void RTX_DispatchRaysVK(const rtxDispatchRays_t *params) {
    if (!vkrt.device || !rtx.tlas.numInstances) {
        return;
    }
    
    float startTime = ri.Milliseconds();
    
    // Get pipeline and descriptor set from pipeline system
    VkPipeline rtPipeline = RTX_GetPipeline();
    VkPipelineLayout pipelineLayout = RTX_GetPipelineLayout();
    VkDescriptorSet descriptorSet = RTX_GetDescriptorSet();
    VkAccelerationStructureKHR activeTLAS = vkrt.tlas[vkrt.activeTLAS];

    if (!rtPipeline || !pipelineLayout || !descriptorSet || activeTLAS == VK_NULL_HANDLE) {
        ri.Printf(PRINT_WARNING, "RTX: Pipeline not properly initialized\n");
        return;
    }

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    
    vkResetCommandBuffer(vkrt.commandBuffer, 0);

    VkResult result = vkBeginCommandBuffer(vkrt.commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to begin command buffer\n");
        return;
    }

    // Refresh per-frame uniform data so the shader sees current debug selection
    RTX_PrepareFrameData(vkrt.commandBuffer);

    // Update descriptor sets with current TLAS and output images
    RTX_UpdateDescriptorSets(activeTLAS, vkrt.rtImageView, vkrt.rtImageView,
                            vkrt.rtImageView, vkrt.rtImageView, vkrt.rtImageView);
    
    // Transition RT output image to general layout
    if (vkrt.rtImage) {
        VkImageMemoryBarrier imageBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = rtOutputInitialized ? VK_ACCESS_TRANSFER_READ_BIT : 0,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = rtOutputInitialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
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

        vkCmdPipelineBarrier(vkrt.commandBuffer,
            rtOutputInitialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0, 0, NULL, 0, NULL, 1, &imageBarrier);
    }
    
    // Bind ray tracing pipeline
    vkCmdBindPipeline(vkrt.commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipeline);
    
    // Bind descriptor sets
    vkCmdBindDescriptorSets(vkrt.commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
    
    // Get shader binding table regions
    VkStridedDeviceAddressRegionKHR raygenRegion, missRegion, hitRegion, callableRegion;
    RTX_GetSBTRegions(&raygenRegion, &missRegion, &hitRegion, &callableRegion);
    
    // Dispatch rays
    qvkCmdTraceRaysKHR(vkrt.commandBuffer,
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
        
        vkCmdPipelineBarrier(vkrt.commandBuffer,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 1, &imageBarrier);
    }
    
    vkEndCommandBuffer(vkrt.commandBuffer);
    
    // Submit command buffer
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &vkrt.commandBuffer
    };
    
    vkResetFences(vkrt.device, 1, &vkrt.fence);
    result = vkQueueSubmit(vk.queue, 1, &submitInfo, vkrt.fence);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to submit command buffer\n");
        return;
    }
    
    // Wait for completion
    vkWaitForFences(vkrt.device, 1, &vkrt.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(vkrt.device, 1, &vkrt.fence);

    RTX_DownloadColorBuffer((uint32_t)params->width, (uint32_t)params->height);

    rtx.traceTime = ri.Milliseconds() - startTime;

    rtOutputInitialized = qtrue;
    rtOutputWidth = params->width;
    rtOutputHeight = params->height;
    
    if (r_rtx_debug && r_rtx_debug->integer) {
        ri.Printf(PRINT_ALL, "RTX: Ray dispatch completed in %.2fms (%dx%d)\n", 
                 rtx.traceTime, params->width, params->height);
    }
}

void RTX_WaitForCompletion_Impl(void) {
    if (vkrt.fence != VK_NULL_HANDLE) {
        vkWaitForFences(vkrt.device, 1, &vkrt.fence, VK_TRUE, UINT64_MAX);
    }
}

/*
================
RTX_AllocateScratchBuffer

Allocate scratch buffer for acceleration structure builds
================
*/
static VkBuffer RTX_AllocateScratchBuffer(VkDeviceSize size, VkDeviceMemory *memory) {
    VkBuffer buffer = VK_NULL_HANDLE;
    
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    };
    
    if (vkCreateBuffer(vkrt.device, &bufferInfo, NULL, &buffer) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(vkrt.device, buffer, &memReqs);
    
    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    };
    
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &memoryAllocateFlagsInfo,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = vk_find_memory_type(memReqs.memoryTypeBits, 
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    
    if (vkAllocateMemory(vkrt.device, &allocInfo, NULL, memory) != VK_SUCCESS) {
        vkDestroyBuffer(vkrt.device, buffer, NULL);
        return VK_NULL_HANDLE;
    }
    
    vkBindBufferMemory(vkrt.device, buffer, *memory, 0);
    return buffer;
}

/*
================
RTX_GetBufferDeviceAddress

Get device address of a buffer
================
*/
VkDeviceAddress RTX_GetBufferDeviceAddressVK(VkBuffer buffer) {
    VkBufferDeviceAddressInfo addressInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer
    };
    if (!qvkGetBufferDeviceAddress) {
        ri.Printf(PRINT_WARNING, "RTX: vkGetBufferDeviceAddress not available\n");
        return 0;
    }
    return qvkGetBufferDeviceAddress(vkrt.device, &addressInfo);
}

VkDeviceAddress RTX_GetBufferDeviceAddress(VkBuffer buffer) {
    return RTX_GetBufferDeviceAddressVK(buffer);
}

/*
================
RTX_CreateRTOutputImages

Create output images for ray tracing
================
*/
static qboolean RTX_CreateRTOutputImages(uint32_t width, uint32_t height) {
    // Create main color output image
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    
    if (vkCreateImage(vkrt.device, &imageInfo, NULL, &vkrt.rtImage) != VK_SUCCESS) {
        return qfalse;
    }
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(vkrt.device, vkrt.rtImage, &memReqs);
    
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = vk_find_memory_type(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    
    if (vkAllocateMemory(vkrt.device, &allocInfo, NULL, &vkrt.rtImageMemory) != VK_SUCCESS) {
        vkDestroyImage(vkrt.device, vkrt.rtImage, NULL);
        return qfalse;
    }
    
    vkBindImageMemory(vkrt.device, vkrt.rtImage, vkrt.rtImageMemory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vkrt.rtImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    if (vkCreateImageView(vkrt.device, &viewInfo, NULL, &vkrt.rtImageView) != VK_SUCCESS) {
        vkFreeMemory(vkrt.device, vkrt.rtImageMemory, NULL);
        vkDestroyImage(vkrt.device, vkrt.rtImage, NULL);
        return qfalse;
    }

    // Transition to GENERAL so first dispatch has a defined layout
    VkCommandBuffer setupCmd = vk_begin_one_time_commands();
    if (setupCmd != VK_NULL_HANDLE) {
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vkrt.rtImage,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };

        vkCmdPipelineBarrier(setupCmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0, 0, NULL, 0, NULL, 1, &barrier);

        vk_end_one_time_commands(setupCmd);
    }

    rtOutputInitialized = qfalse;

    return qtrue;
}

void RTX_RecordCommands(VkCommandBuffer cmd) {
    if (!RTX_IsEnabled() || !rtx.available) {
        ri.Printf(PRINT_ALL,
                  "RTX_RecordCommands: abort (enabled=%d available=%d)\n",
                  RTX_IsEnabled() ? 1 : 0,
                  rtx.available ? 1 : 0);
        return;
    }

    if (cmd == VK_NULL_HANDLE) {
        ri.Printf(PRINT_ALL, "RTX_RecordCommands: abort (cmd=NULL)\n");
        return;
    }

    uint32_t width = vk.renderWidth ? vk.renderWidth : (uint32_t)glConfig.vidWidth;
    uint32_t height = vk.renderHeight ? vk.renderHeight : (uint32_t)glConfig.vidHeight;

    if (width == 0 || height == 0) {
        ri.Printf(PRINT_ALL,
                  "RTX_RecordCommands: abort due to zero dimensions (%ux%u)\n",
                  width, height);
        return;
    }

    if (!vkrt.rtImage || rtOutputWidth != width || rtOutputHeight != height) {
        if (!RTX_CreateRTOutputImages(width, height)) {
            ri.Printf(PRINT_WARNING, "RTX: Failed to create ray tracing output image (%ux%u)\n", width, height);
            ri.Printf(PRINT_ALL,
                      "RTX_RecordCommands: abort because RT output image creation failed (%ux%u)\n",
                      width, height);
            return;
        }
        rtOutputWidth = width;
        rtOutputHeight = height;
        rtOutputInitialized = qfalse;
    }

    if (rtx.tlas.needsRebuild) {
        RTX_BuildTLAS(&rtx.tlas);
    }

    rtxDispatchRays_t params = {
        .width = (int)width,
        .height = (int)height,
        .depth = 1,
        .shaderTable = NULL,
        .maxRecursion = r_rtx_gi_bounces ? r_rtx_gi_bounces->integer : 1
    };

    if (params.maxRecursion < 1) {
        params.maxRecursion = 1;
    }

    rtOutputInitialized = qfalse;
    RTX_DispatchRaysVK(&params);

    if (!rtOutputInitialized) {
        ri.Printf(PRINT_WARNING, "RTX: Ray dispatch did not produce output this frame\n");
        return;
    }

    ri.Printf(PRINT_ALL,
              "RTX_RecordCommands: completed ray dispatch for %ux%u\n",
              width, height);

    if (!vkrt.rtImage || vk.color_image == VK_NULL_HANDLE) {
        ri.Printf(PRINT_ALL,
                  "RTX: Skipping framebuffer copy (rtImage=%p, colorImage=%p)\n",
                  (void*)vkrt.rtImage, (void*)vk.color_image);
        return;
    }

    VkImageMemoryBarrier colorBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = vk_image_get_layout_or( vk.color_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ),
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk.color_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &colorBarrier);

    vk_image_set_layout( vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );

    if ( vk.color_format == VK_FORMAT_R32G32B32A32_SFLOAT ) {
        VkImageCopy copyRegion = {
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .extent = { width, height, 1 }
        };

        vkCmdCopyImage(cmd,
                      vkrt.rtImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      1, &copyRegion);
    } else {
        VkImageBlit blitRegion = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .srcOffsets = {
                { 0, 0, 0 },
                { (int32_t)width, (int32_t)height, 1 }
            },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstOffsets = {
                { 0, 0, 0 },
                { (int32_t)width, (int32_t)height, 1 }
            }
        };

        vkCmdBlitImage(cmd,
                       vkrt.rtImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blitRegion,
                       VK_FILTER_NEAREST);
    }

    ri.Printf(PRINT_ALL,
              "RTX: Queued %ux%u ray traced pixels for framebuffer copy (cmd=%p)\n",
              width, height, (void*)cmd);

    colorBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    colorBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    colorBarrier.oldLayout = vk_image_get_layout_or( vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
    colorBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &colorBarrier);

    vk_image_set_layout( vk.color_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

// Denoiser and DLSS implementations are in separate files:
// - rt_rtx_denoiser.c
// - rt_rtx_dlss.c

VkImage RTX_GetRTImage(void) {
    return vkrt.rtImage;
}

VkImageView RTX_GetRTImageView(void) {
    return vkrt.rtImageView;
}

VkBuffer RTX_GetDebugSettingsBuffer(void) {
    return VK_NULL_HANDLE;
}

void RTX_GetLightingContributionViews(VkImageView *directView, VkImageView *indirectView, VkImageView *lightmapView) {
    if (directView) {
        *directView = VK_NULL_HANDLE;
    }
    if (indirectView) {
        *indirectView = VK_NULL_HANDLE;
    }
    if (lightmapView) {
        *lightmapView = VK_NULL_HANDLE;
    }
}

void RTX_CompositeHybridAdd(VkCommandBuffer cmd, uint32_t width, uint32_t height, float intensity) {
    if (!vkrt.rtImage || cmd == VK_NULL_HANDLE) {
        return;
    }

    if (intensity <= 0.0f) {
        return;
    }

    if (!rtOutputInitialized) {
        return;
    }

    VkImage dstImage = vk.color_image;
    if (dstImage == VK_NULL_HANDLE) {
        return;
    }

    VkImageMemoryBarrier barriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vkrt.rtImage,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = vk_image_get_layout_or( dstImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ),
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dstImage,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        }
    };

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 2, barriers);

    vk_image_set_layout( dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );

    if ( vk.color_format == VK_FORMAT_R32G32B32A32_SFLOAT ) {
        VkImageCopy copyRegion = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .extent = { width, height, 1 }
        };

        vkCmdCopyImage(cmd,
                      vkrt.rtImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      1, &copyRegion);
    } else {
        VkImageBlit blitRegion = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .srcOffsets = {
                { 0, 0, 0 },
                { (int32_t)width, (int32_t)height, 1 }
            },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstOffsets = {
                { 0, 0, 0 },
                { (int32_t)width, (int32_t)height, 1 }
            }
        };

        vkCmdBlitImage(cmd,
                       vkrt.rtImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blitRegion,
                       VK_FILTER_NEAREST);
    }

    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].oldLayout = vk_image_get_layout_or( dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
    barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &barriers[1]);

    vk_image_set_layout( dstImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

static qboolean RTX_EnsureDebugOverlayPipeline(void) {
    if (vkrt.debugOverlayPipeline) {
        return qtrue;
    }

    if (!vkrt.device) {
        return qfalse;
    }

    VkDescriptorSetLayoutBinding bindings[8] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 4, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 5, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 6, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 7, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT }
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = ARRAY_LEN(bindings),
        .pBindings = bindings
    };

    if (vkCreateDescriptorSetLayout(vkrt.device, &layoutInfo, NULL, &vkrt.debugOverlaySetLayout) != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create debug overlay descriptor set layout\n");
        return qfalse;
    }

    VkPushConstantRange pcRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(uint32_t) * 4
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &vkrt.debugOverlaySetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcRange
    };

    if (vkCreatePipelineLayout(vkrt.device, &pipelineLayoutInfo, NULL, &vkrt.debugOverlayPipelineLayout) != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create debug overlay pipeline layout\n");
        RTX_DestroyDebugOverlayPipeline();
        return qfalse;
    }

    uint32_t codeSize = 0;
    uint32_t *shaderCode = R_LoadSPIRV("shaders/compute/rtx_debug_overlay.spv", &codeSize);
    if (!shaderCode) {
        ri.Printf(PRINT_WARNING, "RTX: Missing rtx_debug_overlay.spv - run compile_rtx_debug_shader.bat\n");
        return qfalse;
    }

    VkShaderModuleCreateInfo moduleInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = codeSize,
        .pCode = shaderCode
    };

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(vkrt.device, &moduleInfo, NULL, &shaderModule) != VK_SUCCESS) {
        Z_Free(shaderCode);
        ri.Printf(PRINT_WARNING, "RTX: Failed to create debug overlay shader module\n");
        return qfalse;
    }

    VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = vkrt.debugOverlayPipelineLayout,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName = "main"
        }
    };

    if (vkCreateComputePipelines(vkrt.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &vkrt.debugOverlayPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(vkrt.device, shaderModule, NULL);
        Z_Free(shaderCode);
        ri.Printf(PRINT_WARNING, "RTX: Failed to create debug overlay compute pipeline\n");
        RTX_DestroyDebugOverlayPipeline();
        return qfalse;
    }

    vkDestroyShaderModule(vkrt.device, shaderModule, NULL);
    Z_Free(shaderCode);

    VkDescriptorPoolSize poolSizes[2] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = ARRAY_LEN(poolSizes),
        .pPoolSizes = poolSizes
    };

    if (vkCreateDescriptorPool(vkrt.device, &poolInfo, NULL, &vkrt.debugOverlayDescriptorPool) != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create debug overlay descriptor pool\n");
        RTX_DestroyDebugOverlayPipeline();
        return qfalse;
    }

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vkrt.debugOverlayDescriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vkrt.debugOverlaySetLayout
    };

    if (vkAllocateDescriptorSets(vkrt.device, &allocInfo, &vkrt.debugOverlayDescriptorSet) != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to allocate debug overlay descriptor set\n");
        RTX_DestroyDebugOverlayPipeline();
        return qfalse;
    }

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .minLod = 0.0f,
        .maxLod = 0.0f
    };

    if (vkCreateSampler(vkrt.device, &samplerInfo, NULL, &vkrt.debugOverlaySampler) != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "RTX: Failed to create debug overlay sampler\n");
        RTX_DestroyDebugOverlayPipeline();
        return qfalse;
    }

    return qtrue;
}

static void RTX_DestroyDebugOverlayPipeline(void) {
    if (vkrt.debugOverlaySampler) {
        vkDestroySampler(vkrt.device, vkrt.debugOverlaySampler, NULL);
        vkrt.debugOverlaySampler = VK_NULL_HANDLE;
    }

    if (vkrt.debugOverlayDescriptorPool) {
        vkDestroyDescriptorPool(vkrt.device, vkrt.debugOverlayDescriptorPool, NULL);
        vkrt.debugOverlayDescriptorPool = VK_NULL_HANDLE;
    }

    if (vkrt.debugOverlaySetLayout) {
        vkDestroyDescriptorSetLayout(vkrt.device, vkrt.debugOverlaySetLayout, NULL);
        vkrt.debugOverlaySetLayout = VK_NULL_HANDLE;
    }

    if (vkrt.debugOverlayPipeline) {
        vkDestroyPipeline(vkrt.device, vkrt.debugOverlayPipeline, NULL);
        vkrt.debugOverlayPipeline = VK_NULL_HANDLE;
    }

    if (vkrt.debugOverlayPipelineLayout) {
        vkDestroyPipelineLayout(vkrt.device, vkrt.debugOverlayPipelineLayout, NULL);
        vkrt.debugOverlayPipelineLayout = VK_NULL_HANDLE;
    }
}

static qboolean RTX_UpdateDebugOverlayDescriptors(void) {
    if (!vkrt.debugOverlayDescriptorSet || !vkrt.debugOverlaySampler) {
        return qfalse;
    }

    if (!vkrt.rtImageView || !vk.color_image_view) {
        return qfalse;
    }

    VkImageView rtView = vkrt.rtImageView;
    VkImageView colorView = vk.color_image_view;
    VkImageView depthView = vk.depth_image_view_depth_only ? vk.depth_image_view_depth_only : colorView;

    VkDescriptorImageInfo depthInfo = {
        .sampler = vkrt.debugOverlaySampler,
        .imageView = depthView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    };

    VkDescriptorImageInfo normalInfo = {
        .sampler = vkrt.debugOverlaySampler,
        .imageView = rtView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    };

    VkDescriptorImageInfo motionInfo = normalInfo;
    VkDescriptorImageInfo rtSampleInfo = normalInfo;

    VkDescriptorImageInfo directInfo = normalInfo;
    VkDescriptorImageInfo indirectInfo = normalInfo;
    VkDescriptorImageInfo lightmapInfo = normalInfo;

    VkDescriptorImageInfo overlayImageInfo = {
        .sampler = VK_NULL_HANDLE,
        .imageView = rtView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    };

    VkWriteDescriptorSet writes[8] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vkrt.debugOverlayDescriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &depthInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vkrt.debugOverlayDescriptorSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &normalInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vkrt.debugOverlayDescriptorSet,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &motionInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vkrt.debugOverlayDescriptorSet,
            .dstBinding = 3,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &rtSampleInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vkrt.debugOverlayDescriptorSet,
            .dstBinding = 4,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &overlayImageInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vkrt.debugOverlayDescriptorSet,
            .dstBinding = 5,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &directInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vkrt.debugOverlayDescriptorSet,
            .dstBinding = 6,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &indirectInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vkrt.debugOverlayDescriptorSet,
            .dstBinding = 7,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &lightmapInfo
        }
    };

    vkUpdateDescriptorSets(vkrt.device, ARRAY_LEN(writes), writes, 0, NULL);
    return qtrue;
}

void RTX_ApplyDebugOverlayCompute(VkCommandBuffer cmd, VkImage colorImage) {
    if (!cmd || colorImage == VK_NULL_HANDLE) {
        return;
    }

    if (!rtx.available || !RTX_IsEnabled()) {
        return;
    }

    if (!r_rtx_debug || r_rtx_debug->integer <= 0) {
        return;
    }

    if (!vkrt.rtImage || !rtOutputInitialized) {
        return;
    }

    if (rtOutputWidth == 0 || rtOutputHeight == 0) {
        return;
    }

    (void)RTX_EnsureDebugOverlayPipeline();
    (void)RTX_UpdateDebugOverlayDescriptors();

    VkImageCopy copyRegion = {
        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .extent = { rtOutputWidth, rtOutputHeight, 1 }
    };

    VkImageMemoryBarrier prepareColor = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = vk_image_get_layout_or( colorImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ),
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = colorImage,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &prepareColor);

    vk_image_set_layout( colorImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );

    if ( vk.color_format == VK_FORMAT_R32G32B32A32_SFLOAT ) {
        vkCmdCopyImage(cmd,
                       vkrt.rtImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       colorImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &copyRegion);
    } else {
        VkImageBlit blitRegion = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .srcOffsets = {
                { 0, 0, 0 },
                { (int32_t)rtOutputWidth, (int32_t)rtOutputHeight, 1 }
            },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstOffsets = {
                { 0, 0, 0 },
                { (int32_t)rtOutputWidth, (int32_t)rtOutputHeight, 1 }
            }
        };

        vkCmdBlitImage(cmd,
                       vkrt.rtImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       colorImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blitRegion,
                       VK_FILTER_NEAREST);
    }

    VkImageMemoryBarrier restoreColor = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = vk_image_get_layout_or( colorImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ),
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = colorImage,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &restoreColor);

    vk_image_set_layout( colorImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}
