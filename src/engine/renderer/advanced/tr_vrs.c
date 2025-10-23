/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

This file is part of Quake3e-HD.

Quake3e-HD is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

Quake3e-HD is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake3e-HD; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
===========================================================================
*/
// tr_vrs.c - Variable Rate Shading implementation

#include "tr_vrs.h"
#include "tr_gpu_driven.h"
#include "../vulkan/vk.h"
#include "../core/tr_common_utils.h"

#ifndef VK_CHECK
#define VK_CHECK( function_call ) { \
    VkResult res = function_call; \
    if ( res < 0 ) { \
        ri.Error( ERR_FATAL, "Vulkan: %s returned error %d", #function_call, res ); \
    } \
}
#endif

// VRS extension function pointers
static PFN_vkCmdSetViewportShadingRatePaletteNV qvkCmdSetViewportShadingRatePaletteNV;
static PFN_vkCmdSetCoarseSampleOrderNV qvkCmdSetCoarseSampleOrderNV;

// CVars
cvar_t *r_variableRateShading;
cvar_t *r_vrsQuality;
cvar_t *r_vrsDebug;
cvar_t *r_vrsDistanceScale;
cvar_t *r_vrsMotionScale;
cvar_t *r_vrsFoveatedScale;

// Global VRS state
static vrsState_t vrsState;

// Forward declarations for internal functions
static void R_CreateMotionVectorBuffer( void );
static void R_DestroyMotionVectorBuffer( void );
static void R_CreateVRSGenerationPipeline( void );
static void R_DestroyVRSGenerationPipeline( void );

// Default quality configurations
static const vrsConfig_t vrsQualityConfigs[] = {
    // VRS_QUALITY_OFF
    {
        .qualityMode = VRS_QUALITY_OFF,
        .sourceMask = VRS_SOURCE_NONE,
    },
    
    // VRS_QUALITY_PERFORMANCE
    {
        .qualityMode = VRS_QUALITY_PERFORMANCE,
        .sourceMask = VRS_SOURCE_DISTANCE | VRS_SOURCE_MOTION,
        .distanceNear = 100.0f,
        .distanceFar = 1000.0f,
        .distanceMaxRate = VRS_RATE_4X4,
        .motionThreshold = 10.0f,
        .motionMaxRate = VRS_RATE_2X2,
        .targetFrameTime = 16.67f,
        .adaptiveStrength = 1.0f,
    },
    
    // VRS_QUALITY_BALANCED
    {
        .qualityMode = VRS_QUALITY_BALANCED,
        .sourceMask = VRS_SOURCE_DISTANCE | VRS_SOURCE_MOTION | VRS_SOURCE_CONTENT,
        .distanceNear = 200.0f,
        .distanceFar = 1500.0f,
        .distanceMaxRate = VRS_RATE_2X2,
        .motionThreshold = 20.0f,
        .motionMaxRate = VRS_RATE_2X1,
        .edgeThreshold = 0.1f,
        .contrastThreshold = 0.2f,
        .targetFrameTime = 16.67f,
        .adaptiveStrength = 0.5f,
    },
    
    // VRS_QUALITY_QUALITY
    {
        .qualityMode = VRS_QUALITY_QUALITY,
        .sourceMask = VRS_SOURCE_DISTANCE | VRS_SOURCE_CONTENT,
        .distanceNear = 500.0f,
        .distanceFar = 2000.0f,
        .distanceMaxRate = VRS_RATE_2X1,
        .edgeThreshold = 0.05f,
        .contrastThreshold = 0.1f,
        .targetFrameTime = 16.67f,
        .adaptiveStrength = 0.25f,
    },
};

/*
================
R_InitVRS

Initialize Variable Rate Shading
================
*/
qboolean R_InitVRS( void ) {
    Com_Memset( &vrsState, 0, sizeof( vrsState ) );
    
    // Register CVars
    r_vrsQuality = ri.Cvar_Get( "r_vrsQuality", "0", CVAR_ARCHIVE );
    r_vrsDebug = ri.Cvar_Get( "r_vrsDebug", "0", CVAR_CHEAT );
    r_vrsDistanceScale = ri.Cvar_Get( "r_vrsDistanceScale", "1.0", CVAR_ARCHIVE );
    r_vrsMotionScale = ri.Cvar_Get( "r_vrsMotionScale", "1.0", CVAR_ARCHIVE );
    r_vrsFoveatedScale = ri.Cvar_Get( "r_vrsFoveatedScale", "1.0", CVAR_ARCHIVE );
    
    // Check for VRS support
    VkPhysicalDeviceProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
    };
    
    VkPhysicalDeviceShadingRateImagePropertiesNV shadingRateProps = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_PROPERTIES_NV
    };
    props2.pNext = &shadingRateProps;
    
    vkGetPhysicalDeviceProperties2( vk.physical_device, &props2 );
    
    // Check if shading rate image is supported
    if ( shadingRateProps.shadingRateTexelSize.width == 0 ) {
        ri.Printf( PRINT_WARNING, "Variable Rate Shading not supported by GPU\n" );
        return qfalse;
    }
    
    // Get extension functions
    qvkCmdSetViewportShadingRatePaletteNV = (PFN_vkCmdSetViewportShadingRatePaletteNV)
        vkGetDeviceProcAddr( vk.device, "vkCmdSetViewportShadingRatePaletteNV" );
    qvkCmdSetCoarseSampleOrderNV = (PFN_vkCmdSetCoarseSampleOrderNV)
        vkGetDeviceProcAddr( vk.device, "vkCmdSetCoarseSampleOrderNV" );
    
    if ( !qvkCmdSetViewportShadingRatePaletteNV ) {
        ri.Printf( PRINT_WARNING, "Failed to get VRS functions\n" );
        return qfalse;
    }
    
    // Store capabilities
    vrsState.supported = qtrue;
    vrsState.imageBasedSupported = qtrue;
    vrsState.tileSize = shadingRateProps.shadingRateTexelSize;
    vrsState.maxShadingRate = shadingRateProps.shadingRateMaxCoarseSamples;
    
    // Calculate shading rate image dimensions
    vrsState.rateImageWidth = ( glConfig.vidWidth + vrsState.tileSize.width - 1 ) / 
                             vrsState.tileSize.width;
    vrsState.rateImageHeight = ( glConfig.vidHeight + vrsState.tileSize.height - 1 ) / 
                              vrsState.tileSize.height;
    vrsState.numTilesX = vrsState.rateImageWidth;
    vrsState.numTilesY = vrsState.rateImageHeight;
    
    // Create shading rate image
    R_CreateShadingRateImage( vrsState.rateImageWidth, vrsState.rateImageHeight );
    
    // Create motion vector buffer
    R_CreateMotionVectorBuffer();
    
    // Create VRS generation pipeline
    R_CreateVRSGenerationPipeline();
    
    // Allocate CPU-side tile data for debugging
    vrsState.tileData = ri.Hunk_Alloc( sizeof( vrsTileData_t ) * 
                                       vrsState.numTilesX * vrsState.numTilesY, h_low );
    
    // Set default quality
    R_SetVRSQuality( VRS_QUALITY_BALANCED );
    
    ri.Printf( PRINT_ALL, "VRS initialized (tile size: %dx%d, image: %dx%d)\n",
              vrsState.tileSize.width, vrsState.tileSize.height,
              vrsState.rateImageWidth, vrsState.rateImageHeight );
    
    return qtrue;
}

/*
================
R_ShutdownVRS

Shutdown Variable Rate Shading
================
*/
void R_ShutdownVRS( void ) {
    if ( !vrsState.supported ) {
        return;
    }
    
    vkDeviceWaitIdle( vk.device );
    
    R_DestroyShadingRateImage();
    R_DestroyMotionVectorBuffer();
    R_DestroyVRSGenerationPipeline();
    
    Com_Memset( &vrsState, 0, sizeof( vrsState ) );
}

/*
================
R_CreateShadingRateImage

Create the shading rate image
================
*/
void R_CreateShadingRateImage( uint32_t width, uint32_t height ) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UINT,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV | 
                VK_IMAGE_USAGE_STORAGE_BIT | 
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    
    VK_CHECK( vkCreateImage( vk.device, &imageInfo, NULL, &vrsState.rateImage ) );
    
    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements( vk.device, vrsState.rateImage, &memReqs );
    
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = vk_find_memory_type( memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
    };
    
    VK_CHECK( vkAllocateMemory( vk.device, &allocInfo, NULL, &vrsState.rateImageMemory ) );
    VK_CHECK( vkBindImageMemory( vk.device, vrsState.rateImage, vrsState.rateImageMemory, 0 ) );
    
    // Create image view
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vrsState.rateImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8_UINT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    VK_CHECK( vkCreateImageView( vk.device, &viewInfo, NULL, &vrsState.rateImageView ) );
}

/*
================
R_DestroyShadingRateImage

Destroy the shading rate image
================
*/
void R_DestroyShadingRateImage( void ) {
    if ( vrsState.rateImageView ) {
        vkDestroyImageView( vk.device, vrsState.rateImageView, NULL );
    }
    
    if ( vrsState.rateImage ) {
        vkDestroyImage( vk.device, vrsState.rateImage, NULL );
    }
    
    if ( vrsState.rateImageMemory ) {
        vkFreeMemory( vk.device, vrsState.rateImageMemory, NULL );
    }
}

/*
================
R_CreateMotionVectorBuffer

Create motion vector buffer for motion-based VRS
================
*/
static void R_CreateMotionVectorBuffer( void ) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R16G16_SFLOAT,
        .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | 
                VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    
    VK_CHECK( vkCreateImage( vk.device, &imageInfo, NULL, &vrsState.motionVectorImage ) );
    
    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements( vk.device, vrsState.motionVectorImage, &memReqs );
    
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = vk_find_memory_type( memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
    };
    
    VK_CHECK( vkAllocateMemory( vk.device, &allocInfo, NULL, &vrsState.motionVectorMemory ) );
    VK_CHECK( vkBindImageMemory( vk.device, vrsState.motionVectorImage, 
                                 vrsState.motionVectorMemory, 0 ) );
    
    // Create image view
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vrsState.motionVectorImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R16G16_SFLOAT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    VK_CHECK( vkCreateImageView( vk.device, &viewInfo, NULL, &vrsState.motionVectorView ) );
}

/*
================
R_DestroyMotionVectorBuffer

Destroy motion vector buffer
================
*/
static void R_DestroyMotionVectorBuffer( void ) {
    if ( vrsState.motionVectorView ) {
        vkDestroyImageView( vk.device, vrsState.motionVectorView, NULL );
    }
    
    if ( vrsState.motionVectorImage ) {
        vkDestroyImage( vk.device, vrsState.motionVectorImage, NULL );
    }
    
    if ( vrsState.motionVectorMemory ) {
        vkFreeMemory( vk.device, vrsState.motionVectorMemory, NULL );
    }
}

/*
================
R_CreateVRSGenerationPipeline

Create compute pipeline for VRS generation
================
*/
static void R_CreateVRSGenerationPipeline( void ) {
    // Load compute shader
    size_t codeSize;
    uint32_t *spirvCode = R_LoadSPIRV( "compute/vrs_generate.spv", &codeSize );
    
    if ( !spirvCode ) {
        ri.Printf( PRINT_WARNING, "Failed to load VRS generation shader\n" );
        return;
    }
    
    VkShaderModule shaderModule = R_CreateComputeShader( spirvCode, codeSize );
    
    // Create descriptor set layout
    VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        }
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = ARRAY_LEN( bindings ),
        .pBindings = bindings
    };
    
    VkDescriptorSetLayout descriptorSetLayout;
    VK_CHECK( vkCreateDescriptorSetLayout( vk.device, &layoutInfo, NULL, &descriptorSetLayout ) );
    
    // Create pipeline layout
    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof( vrsConfig_t )
    };
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };
    
    VK_CHECK( vkCreatePipelineLayout( vk.device, &pipelineLayoutInfo, NULL,
                                     &vrsState.generatePipelineLayout ) );
    
    // Create compute pipeline
    vrsState.generatePipeline = R_CreateComputePipeline( shaderModule, 
                                                         vrsState.generatePipelineLayout );
    
    vkDestroyShaderModule( vk.device, shaderModule, NULL );
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorSetLayout
    };
    
    VK_CHECK( vkAllocateDescriptorSets( vk.device, &allocInfo,
                                       &vrsState.generateDescriptorSet ) );
    
    // Update descriptor set
    VkDescriptorImageInfo imageInfos[] = {
        {
            .imageView = vrsState.rateImageView,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL
        },
        {
            .imageView = vk.depth_image_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        },
        {
            .imageView = vrsState.motionVectorView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }
    };
    
    VkDescriptorBufferInfo bufferInfo = {
        .buffer = VK_NULL_HANDLE, // TODO: Create VRS config buffer
        .offset = 0,
        .range = sizeof( vrsConfig_t )
    };
    
    VkWriteDescriptorSet writes[] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vrsState.generateDescriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &imageInfos[0]
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vrsState.generateDescriptorSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &imageInfos[1]
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vrsState.generateDescriptorSet,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &imageInfos[2]
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vrsState.generateDescriptorSet,
            .dstBinding = 3,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bufferInfo
        }
    };
    
    vkUpdateDescriptorSets( vk.device, ARRAY_LEN( writes ), writes, 0, NULL );
}

/*
================
R_DestroyVRSGenerationPipeline

Destroy VRS generation pipeline
================
*/
static void R_DestroyVRSGenerationPipeline( void ) {
    if ( vrsState.generatePipeline ) {
        vkDestroyPipeline( vk.device, vrsState.generatePipeline, NULL );
    }
    
    if ( vrsState.generatePipelineLayout ) {
        vkDestroyPipelineLayout( vk.device, vrsState.generatePipelineLayout, NULL );
    }
}

/*
================
R_SetVRSQuality

Set VRS quality preset
================
*/
void R_SetVRSQuality( vrsQualityMode_t quality ) {
    if ( quality < 0 || quality >= VRS_QUALITY_CUSTOM ) {
        quality = VRS_QUALITY_BALANCED;
    }
    
    vrsState.config = vrsQualityConfigs[quality];
}

/*
================
R_SetVRSConfig

Set custom VRS configuration
================
*/
void R_SetVRSConfig( const vrsConfig_t *config ) {
    vrsState.config = *config;
    vrsState.config.qualityMode = VRS_QUALITY_CUSTOM;
}

/*
================
R_GetVRSConfig

Get current VRS configuration
================
*/
void R_GetVRSConfig( vrsConfig_t *config ) {
    *config = vrsState.config;
}

/*
================
R_BeginVRSFrame

Begin VRS frame processing
================
*/
void R_BeginVRSFrame( void ) {
    if ( !vrsState.supported || !r_variableRateShading->integer ) {
        return;
    }
    
    // Clear statistics
    Com_Memset( &vrsState.stats, 0, sizeof( vrsState.stats ) );
}

/*
================
R_EndVRSFrame

End VRS frame processing
================
*/
void R_EndVRSFrame( void ) {
    if ( !vrsState.supported || !r_variableRateShading->integer ) {
        return;
    }
    
    // Calculate statistics
    uint32_t totalPixels = 0;
    float weightedSum = 0;
    
    for ( int i = 0; i < 11; i++ ) {
        totalPixels += vrsState.stats.pixelsShaded[i];
        
        // Calculate weight based on shading rate
        float weight = 1.0f;
        switch ( i ) {
            case VRS_RATE_1X2:
            case VRS_RATE_2X1:
                weight = 2.0f;
                break;
            case VRS_RATE_2X2:
                weight = 4.0f;
                break;
            case VRS_RATE_2X4:
            case VRS_RATE_4X2:
                weight = 8.0f;
                break;
            case VRS_RATE_4X4:
                weight = 16.0f;
                break;
        }
        
        weightedSum += vrsState.stats.pixelsShaded[i] / weight;
    }
    
    if ( totalPixels > 0 ) {
        vrsState.stats.averageShadingRate = weightedSum / totalPixels;
        vrsState.stats.vrsEfficiency = 1.0f - ( vrsState.stats.pixelsShaded[VRS_RATE_1X1] / 
                                               (float)totalPixels );
    }
}

/*
================
R_UpdateShadingRateImage

Update the shading rate image based on current frame
================
*/
void R_UpdateShadingRateImage( VkCommandBuffer cmd, const viewParms_t *viewParms ) {
    if ( !vrsState.supported || !r_variableRateShading->integer ) {
        return;
    }
    
    // Transition shading rate image for compute write
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vrsState.rateImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT
    };
    
    vkCmdPipelineBarrier( cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier );
    
    // Bind compute pipeline
    vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vrsState.generatePipeline );
    
    // Bind descriptor set
    vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            vrsState.generatePipelineLayout, 0, 1,
                            &vrsState.generateDescriptorSet, 0, NULL );
    
    // Push constants
    vkCmdPushConstants( cmd, vrsState.generatePipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof( vrsConfig_t ), &vrsState.config );
    
    // Dispatch compute shader
    uint32_t groupX = ( vrsState.rateImageWidth + 7 ) / 8;
    uint32_t groupY = ( vrsState.rateImageHeight + 7 ) / 8;
    vkCmdDispatch( cmd, groupX, groupY, 1 );
    
    // Transition for shading rate image usage
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADING_RATE_IMAGE_READ_BIT_NV;
    
    vkCmdPipelineBarrier( cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV,
                         0, 0, NULL, 0, NULL, 1, &barrier );
}

/*
================
R_SetShadingRate

Set per-draw shading rate
================
*/
void R_SetShadingRate( VkCommandBuffer cmd, vrsRate_t rate ) {
    if ( !vrsState.supported || !r_variableRateShading->integer ) {
        return;
    }
    
    VkShadingRatePaletteEntryNV vkRate = R_VRSRateToVulkan( rate );
    
    // Set viewport shading rate palette
    VkShadingRatePaletteNV palette = {
        .shadingRatePaletteEntryCount = 1,
        .pShadingRatePaletteEntries = &vkRate
    };
    
    if ( qvkCmdSetViewportShadingRatePaletteNV ) {
        qvkCmdSetViewportShadingRatePaletteNV( cmd, 0, 1, &palette );
    }
}

/*
================
R_SetShadingRateImage

Bind shading rate image for rendering
================
*/
void R_SetShadingRateImage( VkCommandBuffer cmd ) {
    if ( !vrsState.supported || !r_variableRateShading->integer ) {
        return;
    }
    
    // Set shading rate image
    VkViewport viewport = {
        .x = 0,
        .y = 0,
        .width = glConfig.vidWidth,
        .height = glConfig.vidHeight,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    
    vkCmdSetViewport( cmd, 0, 1, &viewport );
    
    // Setup shading rate palette
    VkShadingRatePaletteEntryNV paletteEntries[] = {
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_PIXEL_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_1X2_PIXELS_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X1_PIXELS_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X2_PIXELS_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X4_PIXELS_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X2_PIXELS_NV,
        VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X4_PIXELS_NV,
    };
    
    VkShadingRatePaletteNV palette = {
        .shadingRatePaletteEntryCount = ARRAY_LEN( paletteEntries ),
        .pShadingRatePaletteEntries = paletteEntries
    };
    
    if ( qvkCmdSetViewportShadingRatePaletteNV ) {
        qvkCmdSetViewportShadingRatePaletteNV( cmd, 0, 1, &palette );
    }
}

/*
================
R_CalculateDistanceRate

Calculate shading rate based on distance
================
*/
vrsRate_t R_CalculateDistanceRate( float distance ) {
    if ( distance < vrsState.config.distanceNear ) {
        return VRS_RATE_1X1;
    }
    
    float t = ( distance - vrsState.config.distanceNear ) / 
              ( vrsState.config.distanceFar - vrsState.config.distanceNear );
    t = CLAMP( t, 0.0f, 1.0f );
    t *= r_vrsDistanceScale->value;
    
    // Map to discrete shading rates
    if ( t < 0.25f ) {
        return VRS_RATE_1X1;
    } else if ( t < 0.5f ) {
        return VRS_RATE_2X1;
    } else if ( t < 0.75f ) {
        return VRS_RATE_2X2;
    } else {
        return vrsState.config.distanceMaxRate;
    }
}

/*
================
R_CalculateMotionRate

Calculate shading rate based on motion
================
*/
vrsRate_t R_CalculateMotionRate( const vec2_t motion ) {
    float speed = Vector2Length( motion );
    
    if ( speed < vrsState.config.motionThreshold ) {
        return VRS_RATE_1X1;
    }
    
    float t = speed / ( vrsState.config.motionThreshold * 4.0f );
    t = CLAMP( t, 0.0f, 1.0f );
    t *= r_vrsMotionScale->value;
    
    // Map to discrete shading rates
    if ( t < 0.33f ) {
        return VRS_RATE_1X1;
    } else if ( t < 0.66f ) {
        return VRS_RATE_2X1;
    } else {
        return vrsState.config.motionMaxRate;
    }
}

/*
================
R_VRSRateToVulkan

Convert VRS rate to Vulkan enum
================
*/
VkShadingRatePaletteEntryNV R_VRSRateToVulkan( vrsRate_t rate ) {
    switch ( rate ) {
        case VRS_RATE_1X2:
            return VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_1X2_PIXELS_NV;
        case VRS_RATE_2X1:
            return VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X1_PIXELS_NV;
        case VRS_RATE_2X2:
            return VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X2_PIXELS_NV;
        case VRS_RATE_2X4:
            return VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X4_PIXELS_NV;
        case VRS_RATE_4X2:
            return VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X2_PIXELS_NV;
        case VRS_RATE_4X4:
            return VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X4_PIXELS_NV;
        default:
            return VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_PIXEL_NV;
    }
}

/*
================
R_DrawVRSDebugOverlay

Draw VRS debug visualization
================
*/
void R_DrawVRSDebugOverlay( void ) {
    if ( !r_vrsDebug->integer || !vrsState.supported ) {
        return;
    }
    
    // Draw stats
    ri.Printf( PRINT_ALL, "VRS Stats:\n" );
    ri.Printf( PRINT_ALL, "  Average Rate: %.2f\n", vrsState.stats.averageShadingRate );
    ri.Printf( PRINT_ALL, "  VRS Efficiency: %.1f%%\n", vrsState.stats.vrsEfficiency * 100.0f );
    ri.Printf( PRINT_ALL, "  Tiles Updated: %d\n", vrsState.stats.tilesUpdated );
    
    // Draw rate distribution
    ri.Printf( PRINT_ALL, "  Rate Distribution:\n" );
    ri.Printf( PRINT_ALL, "    1x1: %d\n", vrsState.stats.pixelsShaded[VRS_RATE_1X1] );
    ri.Printf( PRINT_ALL, "    2x1: %d\n", vrsState.stats.pixelsShaded[VRS_RATE_2X1] );
    ri.Printf( PRINT_ALL, "    2x2: %d\n", vrsState.stats.pixelsShaded[VRS_RATE_2X2] );
    ri.Printf( PRINT_ALL, "    4x4: %d\n", vrsState.stats.pixelsShaded[VRS_RATE_4X4] );
}

VkImageView R_GetMotionVectorView( void ) {
    return vrsState.motionVectorView;
}

VkImage R_GetMotionVectorImage( void ) {
    return vrsState.motionVectorImage;
}
