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
// tr_taa.c - Temporal Anti-Aliasing implementation

#include "tr_taa.h"
#include "../vulkan/vk.h"
#include "../core/command/tr_cmdbuf.h"
#include "../core/tr_common_utils.h"
#include <float.h>

// Forward declarations
static qboolean R_CreateTAAResources( void );
static void R_DestroyTAAResources( void );
static qboolean R_CreateTAAPipelines( void );
static void R_DestroyTAAPipelines( void );

// VK_CHECK macro for error handling
#ifndef VK_CHECK
#define VK_CHECK( function_call ) { \
    VkResult res = function_call; \
    if ( res < 0 ) { \
        ri.Error( ERR_FATAL, "Vulkan: %s returned error %d", #function_call, res ); \
    } \
}
#endif

// Global TAA state
taaState_t taaState;

// CVars
cvar_t *r_taa;
cvar_t *r_taaQuality;
cvar_t *r_taaSharpness;
cvar_t *r_taaDebug;

// Halton sequence for jitter
static const vec2_t haltonSequence[16] = {
    { 0.5f, 0.333333f },
    { 0.25f, 0.666667f },
    { 0.75f, 0.111111f },
    { 0.125f, 0.444444f },
    { 0.625f, 0.777778f },
    { 0.375f, 0.222222f },
    { 0.875f, 0.555556f },
    { 0.0625f, 0.888889f },
    { 0.5625f, 0.037037f },
    { 0.3125f, 0.370370f },
    { 0.8125f, 0.703704f },
    { 0.1875f, 0.148148f },
    { 0.6875f, 0.481481f },
    { 0.4375f, 0.814815f },
    { 0.9375f, 0.259259f },
    { 0.03125f, 0.592593f }
};

// Quality presets
static const taaConfig_t taaQualityPresets[] = {
    // TAA_QUALITY_OFF
    {
        .quality = TAA_QUALITY_OFF,
    },
    
    // TAA_QUALITY_LOW
    {
        .quality = TAA_QUALITY_LOW,
        .jitterPattern = TAA_JITTER_HALTON,
        .feedbackMin = 0.88f,
        .feedbackMax = 0.97f,
        .motionBlending = 0.85f,
        .sharpness = 0.2f,
        .velocityScale = 1.0f,
        .maxVelocity = 16.0f,
        .useMotionVectors = qtrue,
        .useVarianceClipping = qfalse,
        .useYCoCg = qfalse,
        .useCatmullRom = qfalse,
        .useSMAA = qfalse,
        .jitterScale = { 0.25f, 0.25f },
        .jitterSamples = 8,
    },
    
    // TAA_QUALITY_MEDIUM
    {
        .quality = TAA_QUALITY_MEDIUM,
        .jitterPattern = TAA_JITTER_HALTON,
        .feedbackMin = 0.85f,
        .feedbackMax = 0.95f,
        .motionBlending = 0.9f,
        .sharpness = 0.3f,
        .velocityScale = 1.0f,
        .maxVelocity = 24.0f,
        .useMotionVectors = qtrue,
        .useVarianceClipping = qtrue,
        .useYCoCg = qtrue,
        .useCatmullRom = qfalse,
        .useSMAA = qfalse,
        .jitterScale = { 0.35f, 0.35f },
        .jitterSamples = 8,
    },
    
    // TAA_QUALITY_HIGH
    {
        .quality = TAA_QUALITY_HIGH,
        .jitterPattern = TAA_JITTER_HALTON,
        .feedbackMin = 0.8f,
        .feedbackMax = 0.94f,
        .motionBlending = 0.95f,
        .sharpness = 0.4f,
        .velocityScale = 1.0f,
        .maxVelocity = 32.0f,
        .useMotionVectors = qtrue,
        .useVarianceClipping = qtrue,
        .useYCoCg = qtrue,
        .useCatmullRom = qtrue,
        .useSMAA = qtrue,
        .jitterScale = { 0.5f, 0.5f },
        .jitterSamples = 16,
    },
    
    // TAA_QUALITY_ULTRA
    {
        .quality = TAA_QUALITY_ULTRA,
        .jitterPattern = TAA_JITTER_HALTON,
        .feedbackMin = 0.75f,
        .feedbackMax = 0.93f,
        .motionBlending = 0.98f,
        .sharpness = 0.5f,
        .velocityScale = 1.0f,
        .maxVelocity = 32.0f,
        .useMotionVectors = qtrue,
        .useVarianceClipping = qtrue,
        .useYCoCg = qtrue,
        .useCatmullRom = qtrue,
        .useSMAA = qtrue,
        .jitterScale = { 0.5f, 0.5f },
        .jitterSamples = 16,
    },
};

/*
================
R_InitTAA

Initialize Temporal Anti-Aliasing
================
*/
qboolean R_InitTAA( void ) {
    Com_Memset( &taaState, 0, sizeof( taaState ) );
    
    // Register CVars
    r_taa = ri.Cvar_Get( "r_taa", "1", CVAR_ARCHIVE );
    r_taaQuality = ri.Cvar_Get( "r_taaQuality", "2", CVAR_ARCHIVE );
    r_taaSharpness = ri.Cvar_Get( "r_taaSharpness", "0.5", CVAR_ARCHIVE );
    r_taaDebug = ri.Cvar_Get( "r_taaDebug", "0", CVAR_CHEAT );
    
    // Create TAA resources
    if ( !R_CreateTAAResources() ) {
        ri.Printf( PRINT_WARNING, "Failed to create TAA resources\n" );
        return qfalse;
    }
    
    // Create TAA pipelines
    if ( !R_CreateTAAPipelines() ) {
        ri.Printf( PRINT_WARNING, "Failed to create TAA pipelines\n" );
        R_DestroyTAAResources();
        return qfalse;
    }
    
    // Generate jitter sequence
    R_GenerateJitterSequence( TAA_JITTER_HALTON, 16 );
    
    // Set default quality
    R_SetTAAQuality( TAA_QUALITY_MEDIUM );
    
    taaState.initialized = qtrue;
    taaState.enabled = r_taa->integer ? qtrue : qfalse;
    
    ri.Printf( PRINT_ALL, "TAA initialized\n" );
    
    return qtrue;
}

/*
================
R_ShutdownTAA

Shutdown Temporal Anti-Aliasing
================
*/
void R_ShutdownTAA( void ) {
    if ( !taaState.initialized ) {
        return;
    }
    
    vkDeviceWaitIdle( vk.device );
    
    R_DestroyTAAPipelines();
    R_DestroyTAAResources();
    
    if ( taaState.jitterSequence ) {
        ri.Free( taaState.jitterSequence );
    }
    
    Com_Memset( &taaState, 0, sizeof( taaState ) );
}

/*
================
R_CreateTAAResources

Create TAA GPU resources
================
*/
static qboolean R_CreateTAAResources( void ) {
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    uint32_t width = glConfig.vidWidth;
    uint32_t height = glConfig.vidHeight;
    
    // Create history buffers (double-buffered)
    for ( int i = 0; i < 2; i++ ) {
        VkImageCreateInfo imageInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = { width, height, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        
        VK_CHECK( vkCreateImage( vk.device, &imageInfo, NULL, &taaState.resources.historyImage[i] ) );
        
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements( vk.device, taaState.resources.historyImage[i], &memReqs );
        
        VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = vk_find_memory_type( memReqs.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
        };
        
        VK_CHECK( vkAllocateMemory( vk.device, &allocInfo, NULL, 
                                    &taaState.resources.historyMemory[i] ) );
        VK_CHECK( vkBindImageMemory( vk.device, taaState.resources.historyImage[i],
                                     taaState.resources.historyMemory[i], 0 ) );
        
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = taaState.resources.historyImage[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        
        VK_CHECK( vkCreateImageView( vk.device, &viewInfo, NULL,
                                     &taaState.resources.historyView[i] ) );
    }
    
    // Create velocity buffer
    VkImageCreateInfo velocityInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R16G16_SFLOAT,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    
    VK_CHECK( vkCreateImage( vk.device, &velocityInfo, NULL,
                            &taaState.resources.velocityImage ) );
    
    VkMemoryRequirements velocityMemReqs;
    vkGetImageMemoryRequirements( vk.device, taaState.resources.velocityImage, &velocityMemReqs );
    
    VkMemoryAllocateInfo velocityAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = velocityMemReqs.size,
        .memoryTypeIndex = vk_find_memory_type( velocityMemReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
    };
    
    VK_CHECK( vkAllocateMemory( vk.device, &velocityAllocInfo, NULL,
                               &taaState.resources.velocityMemory ) );
    VK_CHECK( vkBindImageMemory( vk.device, taaState.resources.velocityImage,
                                 taaState.resources.velocityMemory, 0 ) );
    
    VkImageViewCreateInfo velocityViewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = taaState.resources.velocityImage,
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
    
    VK_CHECK( vkCreateImageView( vk.device, &velocityViewInfo, NULL,
                                &taaState.resources.velocityView ) );
    
    return qtrue;
}

/*
================
R_DestroyTAAResources

Destroy TAA GPU resources
================
*/
static void R_DestroyTAAResources( void ) {
    // Destroy history buffers
    for ( int i = 0; i < 2; i++ ) {
        if ( taaState.resources.historyView[i] ) {
            vkDestroyImageView( vk.device, taaState.resources.historyView[i], NULL );
        }
        if ( taaState.resources.historyImage[i] ) {
            vkDestroyImage( vk.device, taaState.resources.historyImage[i], NULL );
        }
        if ( taaState.resources.historyMemory[i] ) {
            vkFreeMemory( vk.device, taaState.resources.historyMemory[i], NULL );
        }
    }
    
    // Destroy velocity buffer
    if ( taaState.resources.velocityView ) {
        vkDestroyImageView( vk.device, taaState.resources.velocityView, NULL );
    }
    if ( taaState.resources.velocityImage ) {
        vkDestroyImage( vk.device, taaState.resources.velocityImage, NULL );
    }
    if ( taaState.resources.velocityMemory ) {
        vkFreeMemory( vk.device, taaState.resources.velocityMemory, NULL );
    }
}

/*
================
R_CreateTAAPipelines

Create TAA compute pipelines
================
*/
static qboolean R_CreateTAAPipelines( void ) {
    // Load compute shaders
    size_t velocitySize, resolveSize, sharpenSize;
    uint32_t *velocitySpirv = R_LoadSPIRV( "taa_velocity.comp.spv", &velocitySize );
    uint32_t *resolveSpirv = R_LoadSPIRV( "taa_resolve.comp.spv", &resolveSize );
    uint32_t *sharpenSpirv = R_LoadSPIRV( "taa_sharpen.comp.spv", &sharpenSize );
    
    if ( !velocitySpirv || !resolveSpirv || !sharpenSpirv ) {
        ri.Printf( PRINT_WARNING, "Failed to load TAA shaders\n" );
        return qfalse;
    }
    
    // Create shader modules
    VkShaderModuleCreateInfo moduleInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
    };
    
    VkShaderModule velocityModule, resolveModule, sharpenModule;
    
    moduleInfo.codeSize = velocitySize;
    moduleInfo.pCode = velocitySpirv;
    VK_CHECK( vkCreateShaderModule( vk.device, &moduleInfo, NULL, &velocityModule ) );
    
    moduleInfo.codeSize = resolveSize;
    moduleInfo.pCode = resolveSpirv;
    VK_CHECK( vkCreateShaderModule( vk.device, &moduleInfo, NULL, &resolveModule ) );
    
    moduleInfo.codeSize = sharpenSize;
    moduleInfo.pCode = sharpenSpirv;
    VK_CHECK( vkCreateShaderModule( vk.device, &moduleInfo, NULL, &sharpenModule ) );
    
    // Create descriptor set layouts
    if ( !R_CreateTAADescriptorSetLayouts() ) {
        vkDestroyShaderModule( vk.device, velocityModule, NULL );
        vkDestroyShaderModule( vk.device, resolveModule, NULL );
        vkDestroyShaderModule( vk.device, sharpenModule, NULL );
        return qfalse;
    }
    
    // Create pipeline layouts
    if ( !R_CreateTAAPipelineLayouts() ) {
        vkDestroyShaderModule( vk.device, velocityModule, NULL );
        vkDestroyShaderModule( vk.device, resolveModule, NULL );
        vkDestroyShaderModule( vk.device, sharpenModule, NULL );
        return qfalse;
    }
    
    // Create compute pipelines
    if ( !R_CreateTAAComputePipelines( velocityModule, resolveModule, sharpenModule ) ) {
        vkDestroyShaderModule( vk.device, velocityModule, NULL );
        vkDestroyShaderModule( vk.device, resolveModule, NULL );
        vkDestroyShaderModule( vk.device, sharpenModule, NULL );
        return qfalse;
    }
    
    // Cleanup shader modules
    vkDestroyShaderModule( vk.device, velocityModule, NULL );
    vkDestroyShaderModule( vk.device, resolveModule, NULL );
    vkDestroyShaderModule( vk.device, sharpenModule, NULL );
    
    // Allocate and update descriptor sets
    if ( !R_AllocateTAADescriptorSets() ) {
        return qfalse;
    }
    if ( !R_UpdateTAADescriptorSets() ) {
        return qfalse;
    }
    
    return qtrue;
}

/*
================
R_DestroyTAAPipelines

Destroy TAA pipelines
================
*/
static void R_DestroyTAAPipelines( void ) {
    if ( taaState.pipeline.velocityPipeline ) {
        vkDestroyPipeline( vk.device, taaState.pipeline.velocityPipeline, NULL );
    }
    if ( taaState.pipeline.resolvePipeline ) {
        vkDestroyPipeline( vk.device, taaState.pipeline.resolvePipeline, NULL );
    }
    if ( taaState.pipeline.sharpenPipeline ) {
        vkDestroyPipeline( vk.device, taaState.pipeline.sharpenPipeline, NULL );
    }
    
    if ( taaState.pipeline.velocityLayout ) {
        vkDestroyPipelineLayout( vk.device, taaState.pipeline.velocityLayout, NULL );
    }
    if ( taaState.pipeline.resolveLayout ) {
        vkDestroyPipelineLayout( vk.device, taaState.pipeline.resolveLayout, NULL );
    }
    if ( taaState.pipeline.sharpenLayout ) {
        vkDestroyPipelineLayout( vk.device, taaState.pipeline.sharpenLayout, NULL );
    }
    
    if ( taaState.pipeline.velocitySetLayout ) {
        vkDestroyDescriptorSetLayout( vk.device, taaState.pipeline.velocitySetLayout, NULL );
    }
    if ( taaState.pipeline.resolveSetLayout ) {
        vkDestroyDescriptorSetLayout( vk.device, taaState.pipeline.resolveSetLayout, NULL );
    }
    if ( taaState.pipeline.sharpenSetLayout ) {
        vkDestroyDescriptorSetLayout( vk.device, taaState.pipeline.sharpenSetLayout, NULL );
    }
}

/*
================
R_SetTAAQuality

Set TAA quality preset
================
*/
void R_SetTAAQuality( taaQuality_t quality ) {
    if ( quality < 0 || quality > TAA_QUALITY_ULTRA ) {
        quality = TAA_QUALITY_MEDIUM;
    }
    
    taaState.config = taaQualityPresets[quality];
    
    // Apply custom sharpness if set
    if ( r_taaSharpness ) {
        taaState.config.sharpness = r_taaSharpness->value;
    }
}

/*
================
R_GenerateJitterSequence

Generate subpixel jitter sequence
================
*/
void R_GenerateJitterSequence( taaJitterPattern_t pattern, uint32_t samples ) {
    if ( taaState.jitterSequence ) {
        ri.Free( taaState.jitterSequence );
    }
    
    taaState.jitterSequence = ri.Malloc( sizeof( vec2_t ) * samples );
    taaState.jitterLength = samples;
    
    switch ( pattern ) {
        case TAA_JITTER_HALTON:
            for ( uint32_t i = 0; i < samples && i < 16; i++ ) {
                VectorCopy2( haltonSequence[i], taaState.jitterSequence[i] );
                // Center jitter around 0
                taaState.jitterSequence[i][0] -= 0.5f;
                taaState.jitterSequence[i][1] -= 0.5f;
            }
            break;
            
        case TAA_JITTER_R2:
            // R2 sequence
            {
                float a1 = 1.0f / 1.32471795724474602596f;
                float a2 = 1.0f / 1.32471795724474602596f / 1.32471795724474602596f;
                
                for ( uint32_t i = 0; i < samples; i++ ) {
                    taaState.jitterSequence[i][0] = fmodf( 0.5f + a1 * i, 1.0f ) - 0.5f;
                    taaState.jitterSequence[i][1] = fmodf( 0.5f + a2 * i, 1.0f ) - 0.5f;
                }
            }
            break;
            
        case TAA_JITTER_HAMMERSLEY:
            // Hammersley sequence
            for ( uint32_t i = 0; i < samples; i++ ) {
                taaState.jitterSequence[i][0] = ( i + 0.5f ) / samples - 0.5f;
                
                // Van der Corput sequence for second dimension
                uint32_t bits = i;
                bits = ( bits << 16 ) | ( bits >> 16 );
                bits = ( ( bits & 0x55555555 ) << 1 ) | ( ( bits & 0xAAAAAAAA ) >> 1 );
                bits = ( ( bits & 0x33333333 ) << 2 ) | ( ( bits & 0xCCCCCCCC ) >> 2 );
                bits = ( ( bits & 0x0F0F0F0F ) << 4 ) | ( ( bits & 0xF0F0F0F0 ) >> 4 );
                bits = ( ( bits & 0x00FF00FF ) << 8 ) | ( ( bits & 0xFF00FF00 ) >> 8 );
                
                taaState.jitterSequence[i][1] = bits * 2.3283064365386963e-10f - 0.5f;
            }
            break;
            
        default:
            // No jitter
            for ( uint32_t i = 0; i < samples; i++ ) {
                VectorClear2( taaState.jitterSequence[i] );
            }
            break;
    }
}

/*
================
R_GetTAAJitter

Get jitter offset for current frame
================
*/
void R_GetTAAJitter( uint32_t frameNumber, vec_t *jitter ) {
    if ( !taaState.enabled || !taaState.jitterSequence ) {
        VectorClear2( jitter );
        return;
    }
    
    uint32_t index = frameNumber % taaState.jitterLength;
    VectorCopy2( taaState.jitterSequence[index], jitter );
    
    // Scale by configuration
    jitter[0] *= taaState.config.jitterScale[0];
    jitter[1] *= taaState.config.jitterScale[1];
}

/*
================
R_ApplyTAAJitter

Apply jitter to projection matrix
================
*/
void R_ApplyTAAJitter( mat4_t projMatrix, const vec2_t jitter ) {
    if ( !taaState.enabled ) {
        return;
    }
    
    // Convert pixel jitter to NDC space
    float jitterX = jitter[0] * 2.0f / glConfig.vidWidth;
    float jitterY = jitter[1] * 2.0f / glConfig.vidHeight;
    
    // Apply to projection matrix
    projMatrix[8] += jitterX;
    projMatrix[9] += jitterY;
}

/*
================
R_BeginTAAFrame

Begin TAA frame processing
================
*/
void R_BeginTAAFrame( void ) {
    if ( !taaState.initialized || !taaState.enabled ) {
        return;
    }
    
    // Store previous frame data
    taaState.frameData.prevJitterOffset[0] = taaState.frameData.jitterOffset[0];
    taaState.frameData.prevJitterOffset[1] = taaState.frameData.jitterOffset[1];
    MatrixCopy( taaState.frameData.viewProjMatrix, taaState.frameData.prevViewProjMatrix );
    
    // Get new jitter for this frame
    vec2_t jitter;
    R_GetTAAJitter( taaState.frameData.frameNumber, jitter );
    VectorCopy2( jitter, taaState.frameData.jitterOffset );
    
    // Update frame counter
    taaState.frameData.frameNumber++;
}

/*
================
R_EndTAAFrame

End TAA frame processing
================
*/
void R_EndTAAFrame( void ) {
    if ( !taaState.initialized || !taaState.enabled ) {
        return;
    }
    
    // Swap history buffers
    taaState.resources.currentHistory = 1 - taaState.resources.currentHistory;
    
    // Update statistics
    taaState.convergenceRate = taaState.config.feedbackMax;
}

/*
================
R_UpdateTAAMatrices

Update TAA matrices from view parameters
================
*/
void R_UpdateTAAMatrices( const viewParms_t *viewParms ) {
    if ( !taaState.initialized ) {
        return;
    }
    
    // Store current matrices
    MatrixCopy( viewParms->world.modelMatrix, taaState.frameData.viewMatrix );
    MatrixCopy( viewParms->projectionMatrix, taaState.frameData.projMatrix );
    
    // Calculate view-projection matrix
    MatrixMultiply4x4( taaState.frameData.projMatrix, taaState.frameData.viewMatrix,
                       taaState.frameData.viewProjMatrix );
    
    // Calculate inverse view-projection
    MatrixInverse( taaState.frameData.viewProjMatrix, taaState.frameData.invViewProjMatrix );
    
    // Apply jitter to projection
    if ( taaState.enabled ) {
        R_ApplyTAAJitter( taaState.frameData.projMatrix, taaState.frameData.jitterOffset );
    }
}

/*
================
R_GenerateVelocityBuffer

Generate velocity buffer for motion vectors
================
*/
void R_GenerateVelocityBuffer( VkCommandBuffer cmd ) {
    if ( !taaState.initialized || !taaState.enabled ) {
        return;
    }
    
    // Transition velocity buffer for compute write
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = taaState.resources.velocityImage,
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
    vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                       taaState.pipeline.velocityPipeline );
    
    // Bind descriptor set
    vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            taaState.pipeline.velocityLayout, 0, 1,
                            &taaState.pipeline.velocityDescSet, 0, NULL );
    
    // Push constants
    struct {
        mat4_t currViewProj;
        mat4_t prevViewProj;
        mat4_t invViewProj;
    } pushConstants;
    
    MatrixCopy( taaState.frameData.viewProjMatrix, pushConstants.currViewProj );
    MatrixCopy( taaState.frameData.prevViewProjMatrix, pushConstants.prevViewProj );
    MatrixCopy( taaState.frameData.invViewProjMatrix, pushConstants.invViewProj );
    
    vkCmdPushConstants( cmd, taaState.pipeline.velocityLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof( pushConstants ), &pushConstants );
    
    // Dispatch compute
    uint32_t groupX = ( glConfig.vidWidth + 7 ) / 8;
    uint32_t groupY = ( glConfig.vidHeight + 7 ) / 8;
    vkCmdDispatch( cmd, groupX, groupY, 1 );
    
    // Transition for sampling
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier( cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier );
}

/*
================
R_ResolveTAA

Resolve TAA with temporal accumulation
================
*/
void R_ResolveTAA( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage ) {
    if ( !taaState.initialized || !taaState.enabled ) {
        // Just copy source to dest if TAA is disabled
        R_CopyImage( cmd, sourceImage, destImage, glConfig.vidWidth, glConfig.vidHeight );
        return;
    }
    
    // Get current and previous history buffers
    uint32_t currHistory = taaState.resources.currentHistory;
    uint32_t prevHistory = 1 - currHistory;
    
    if ( destImage == vk.color_image ) {
        VkImageMemoryBarrier toGeneral = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = vk_image_get_layout_or( vk.color_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ),
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
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

        vkCmdPipelineBarrier( cmd,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              0, 0, NULL, 0, NULL, 1, &toGeneral );

        vk_image_set_layout( vk.color_image, VK_IMAGE_LAYOUT_GENERAL );
    }
    
    // Bind resolve pipeline
    vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                       taaState.pipeline.resolvePipeline );
    
    // Bind descriptor set
    vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            taaState.pipeline.resolveLayout, 0, 1,
                            &taaState.pipeline.resolveDescSet, 0, NULL );
    
    // Push constants
    struct {
        vec4_t jitterInfo;  // xy: current jitter, zw: previous jitter
        vec4_t feedbackParams;  // x: min, y: max, z: motion blend, w: sharpness
        vec4_t colorClampParams;  // variance clipping parameters
    } pushConstants;
    
    pushConstants.jitterInfo[0] = taaState.frameData.jitterOffset[0];
    pushConstants.jitterInfo[1] = taaState.frameData.jitterOffset[1];
    pushConstants.jitterInfo[2] = taaState.frameData.prevJitterOffset[0];
    pushConstants.jitterInfo[3] = taaState.frameData.prevJitterOffset[1];
    
    pushConstants.feedbackParams[0] = taaState.config.feedbackMin;
    pushConstants.feedbackParams[1] = taaState.config.feedbackMax;
    pushConstants.feedbackParams[2] = taaState.config.motionBlending;
    pushConstants.feedbackParams[3] = taaState.config.sharpness;
    
    pushConstants.colorClampParams[0] = taaState.config.useVarianceClipping ? 1.0f : 0.0f;
    pushConstants.colorClampParams[1] = taaState.config.useYCoCg ? 1.0f : 0.0f;
    pushConstants.colorClampParams[2] = taaState.config.useCatmullRom ? 1.0f : 0.0f;
    pushConstants.colorClampParams[3] = 0.0f;
    
    vkCmdPushConstants( cmd, taaState.pipeline.resolveLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof( pushConstants ), &pushConstants );
    
    // Dispatch compute
    uint32_t groupX = ( glConfig.vidWidth + 7 ) / 8;
    uint32_t groupY = ( glConfig.vidHeight + 7 ) / 8;
    vkCmdDispatch( cmd, groupX, groupY, 1 );
    
    // Update history buffer
    R_UpdateTAAHistory( cmd );
}

/*
================
R_UpdateTAAHistory

Update TAA history buffer
================
*/
void R_UpdateTAAHistory( VkCommandBuffer cmd ) {
    // Copy resolved image to history buffer
    uint32_t currHistory = taaState.resources.currentHistory;
    
    VkImageMemoryBarrier barriers[2] = {
        // Transition history for write
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = taaState.resources.historyImage[currHistory],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT
        },
        // Transition source for read
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vk.color_image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT
        }
    };
    
    vkCmdPipelineBarrier( cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 2, barriers );
    
    // Copy resolved image to history
    VkImageCopy copyRegion = {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .srcOffset = { 0, 0, 0 },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .dstOffset = { 0, 0, 0 },
        .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 }
    };
    
    vkCmdCopyImage( cmd,
                   vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   taaState.resources.historyImage[currHistory], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &copyRegion );
    
    // Transition history back for reading next frame
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier( cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barriers[0] );

    if ( vk.color_image != VK_NULL_HANDLE ) {
        VkImageMemoryBarrier backToShader = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = vk_image_get_layout_or( vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ),
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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

        vkCmdPipelineBarrier( cmd,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              0, 0, NULL, 0, NULL, 1, &backToShader );

        vk_image_set_layout( vk.color_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
    }
}

/*
================
R_ApplyTAASharpening

Apply post-TAA sharpening filter
================
*/
void R_ApplyTAASharpening( VkCommandBuffer cmd, float sharpness ) {
    if ( !taaState.initialized || !taaState.enabled || sharpness <= 0.0f ) {
        return;
    }
    
    if ( vk.color_image != VK_NULL_HANDLE ) {
        VkImageMemoryBarrier toGeneral = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = vk_image_get_layout_or( vk.color_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ),
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
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

        vkCmdPipelineBarrier( cmd,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              0, 0, NULL, 0, NULL, 1, &toGeneral );

        vk_image_set_layout( vk.color_image, VK_IMAGE_LAYOUT_GENERAL );
    }
    
    // Bind sharpening pipeline
    vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                       taaState.pipeline.sharpenPipeline );
    
    // Bind descriptor set
    vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            taaState.pipeline.sharpenLayout, 0, 1,
                            &taaState.pipeline.sharpenDescSet, 0, NULL );
    
    // Push constants
    struct {
        float sharpness;
        float pad[3];
    } pushConstants = {
        .sharpness = sharpness
    };
    
    vkCmdPushConstants( cmd, taaState.pipeline.sharpenLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof( pushConstants ), &pushConstants );
    
    // Dispatch compute
    uint32_t groupX = ( glConfig.vidWidth + 7 ) / 8;
    uint32_t groupY = ( glConfig.vidHeight + 7 ) / 8;
    vkCmdDispatch( cmd, groupX, groupY, 1 );

    if ( vk.color_image != VK_NULL_HANDLE ) {
        VkImageMemoryBarrier backToShader = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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

        vkCmdPipelineBarrier( cmd,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              0, 0, NULL, 0, NULL, 1, &backToShader );

        vk_image_set_layout( vk.color_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
    }
}

/*
================
R_ClearTAAHistory

Clear TAA history buffers
================
*/
void R_ClearTAAHistory( void ) {
    if ( !taaState.initialized ) {
        return;
    }
    
    VkCommandBuffer cmd = vk.cmd->command_buffer;
    
    // Clear both history buffers
    for ( int i = 0; i < 2; i++ ) {
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = taaState.resources.historyImage[i],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT
        };
        
        vkCmdPipelineBarrier( cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, NULL, 0, NULL, 1, &barrier );
        
        VkClearColorValue clearColor = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };
        
        vkCmdClearColorImage( cmd, taaState.resources.historyImage[i],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clearColor, 1, &range );
        
        // Transition back to shader read
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier( cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, NULL, 0, NULL, 1, &barrier );
    }
    
    // Command buffer is managed externally
    
    // Reset frame counter to restart jitter sequence
    taaState.frameData.frameNumber = 0;
}

/*
================
R_DrawTAADebug

Draw TAA debug visualization
================
*/
void R_DrawTAADebug( void ) {
    if ( !r_taaDebug->integer || !taaState.initialized ) {
        return;
    }
    
    ri.Printf( PRINT_ALL, "TAA Debug:\n" );
    ri.Printf( PRINT_ALL, "  Enabled: %s\n", taaState.enabled ? "Yes" : "No" );
    ri.Printf( PRINT_ALL, "  Quality: %d\n", taaState.config.quality );
    ri.Printf( PRINT_ALL, "  Jitter: %.3f, %.3f\n", 
              taaState.frameData.jitterOffset[0], 
              taaState.frameData.jitterOffset[1] );
    ri.Printf( PRINT_ALL, "  Frame: %d\n", taaState.frameData.frameNumber );
    ri.Printf( PRINT_ALL, "  Convergence: %.2f%%\n", taaState.convergenceRate * 100.0f );
    ri.Printf( PRINT_ALL, "  Average Motion: %.2f\n", taaState.averageMotion );
    ri.Printf( PRINT_ALL, "  Ghosting Pixels: %d\n", taaState.ghostingPixels );
    ri.Printf( PRINT_ALL, "  History Rejections: %d\n", taaState.historyRejections );
}
