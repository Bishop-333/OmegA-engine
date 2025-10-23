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
// tr_gpu_driven.c - GPU-driven rendering implementation

#include "tr_gpu_driven.h"
#include "../vulkan/vk.h"
#include "../core/tr_common_utils.h"

// External CVars
extern cvar_t *r_lodBias;

// Forward declarations
static void R_UpdateGPUBuffer( VkCommandBuffer commandBuffer, VkBuffer buffer,
                               const void *data, VkDeviceSize size );

// Global GPU-driven context
gpuDrivenContext_t gpuContext;

// CVars
cvar_t *r_gpuDriven;
cvar_t *r_gpuCulling;
cvar_t *r_meshShading;
cvar_t *r_variableRateShading;
cvar_t *r_bindlessTextures;

// Forward declarations
static void R_InitMultiDrawIndirect( void );
static void R_InitGPUCulling( void );
static void R_ShutdownMultiDrawIndirect( void );
static void R_ShutdownGPUCulling( void );
static void R_CreateHiZBuffer( void );
static void R_CreateTimestampQueries( void );
static void R_DestroyHiZBuffer( void );
static void R_DestroyTimestampQueries( void );
static void R_CreateGPUCullingPipeline( multiDrawIndirect_t *mdi );
qboolean R_InitMeshShading( void );
qboolean R_InitVRS( void );
// MatrixCopy is provided by tr_common_utils.h

/*
================
R_InitGPUDriven

Initialize GPU-driven rendering system
================
*/
void R_InitGPUDriven( void ) {
    Com_Memset( &gpuContext, 0, sizeof( gpuContext ) );
    
    // Register CVars
    r_gpuDriven = ri.Cvar_Get( "r_gpuDriven", "1", CVAR_ARCHIVE );
    r_gpuCulling = ri.Cvar_Get( "r_gpuCulling", "1", CVAR_ARCHIVE );
    r_meshShading = ri.Cvar_Get( "r_meshShading", "0", CVAR_ARCHIVE );
    r_variableRateShading = ri.Cvar_Get( "r_variableRateShading", "0", CVAR_ARCHIVE );
    r_bindlessTextures = ri.Cvar_Get( "r_bindlessTextures", "1", CVAR_ARCHIVE );
    
    if ( !r_gpuDriven->integer ) {
        return;
    }
    
    // Initialize multi-draw indirect
    R_InitMultiDrawIndirect();
    
    // Initialize GPU culling
    R_InitGPUCulling();
    
    // Initialize mesh shading if supported
    if ( r_meshShading->integer ) {
        gpuContext.meshShading.supported = R_InitMeshShading();
    }
    
    // Initialize VRS if supported
    if ( r_variableRateShading->integer ) {
        gpuContext.vrs.supported = R_InitVRS();
    }
    
    // Initialize bindless resources if supported
    if ( r_bindlessTextures->integer ) {
        gpuContext.bindless.supported = R_InitBindlessResources();
    }
    
    // Create HiZ buffer for GPU occlusion culling
    R_CreateHiZBuffer();
    
    // Create timestamp query pool
    R_CreateTimestampQueries();
    
    ri.Printf( PRINT_ALL, "GPU-driven rendering initialized\n" );
}

/*
================
R_ShutdownGPUDriven

Shutdown GPU-driven rendering system
================
*/
void R_ShutdownGPUDriven( void ) {
    if ( !r_gpuDriven || !r_gpuDriven->integer ) {
        return;
    }
    
    // Wait for GPU to finish
    vkDeviceWaitIdle( vk.device );
    
    // Shutdown subsystems
    R_ShutdownMultiDrawIndirect();
    R_ShutdownGPUCulling();
    
    if ( gpuContext.meshShading.supported ) {
        R_ShutdownMeshShading();
    }
    
    if ( gpuContext.vrs.supported ) {
        R_ShutdownVRS();
    }
    
    if ( gpuContext.bindless.supported ) {
        R_ShutdownBindlessResources();
    }
    
    // Destroy HiZ buffer
    R_DestroyHiZBuffer();
    
    // Destroy timestamp queries
    R_DestroyTimestampQueries();
    
    Com_Memset( &gpuContext, 0, sizeof( gpuContext ) );
}

/*
================
R_InitMultiDrawIndirect

Initialize multi-draw indirect system
================
*/
static void R_InitMultiDrawIndirect( void ) {
    multiDrawIndirect_t *mdi = &gpuContext.multiDraw;
    
    mdi->maxDraws = MAX_INDIRECT_DRAWS;
    mdi->maxInstances = MAX_INSTANCES;
    
    // Create draw command buffer
    R_CreateGPUBuffer( &mdi->drawBuffer, &mdi->drawMemory,
                      INDIRECT_BUFFER_SIZE,
                      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | 
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
    
    // Create draw count buffer
    R_CreateGPUBuffer( &mdi->countBuffer, &mdi->countMemory,
                      sizeof( uint32_t ),
                      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
    
    // Create instance buffer
    R_CreateGPUBuffer( &mdi->instanceBuffer, &mdi->instanceMemory,
                      INSTANCE_BUFFER_SIZE,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
    
    // Create visibility buffer
    R_CreateGPUBuffer( &mdi->visibilityBuffer, &mdi->visibilityMemory,
                      VISIBILITY_BUFFER_SIZE,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
    
    // Allocate staging memory
    mdi->drawCommands = ri.Hunk_Alloc( sizeof( gpuDrawCommand_t ) * mdi->maxDraws, h_low );
    mdi->instances = ri.Hunk_Alloc( sizeof( gpuInstance_t ) * mdi->maxInstances, h_low );
    mdi->visibilityData = ri.Hunk_Alloc( sizeof( uint32_t ) * mdi->maxDraws, h_low );
    
    // Create GPU culling pipeline (optional - will fail gracefully if shader not found)
    R_CreateGPUCullingPipeline( mdi );
}

/*
================
R_ShutdownMultiDrawIndirect

Shutdown multi-draw indirect system
================
*/
static void R_ShutdownMultiDrawIndirect( void ) {
    multiDrawIndirect_t *mdi = &gpuContext.multiDraw;
    
    if ( mdi->drawBuffer ) {
        vkDestroyBuffer( vk.device, mdi->drawBuffer, NULL );
        vkFreeMemory( vk.device, mdi->drawMemory, NULL );
    }
    
    if ( mdi->countBuffer ) {
        vkDestroyBuffer( vk.device, mdi->countBuffer, NULL );
        vkFreeMemory( vk.device, mdi->countMemory, NULL );
    }
    
    if ( mdi->instanceBuffer ) {
        vkDestroyBuffer( vk.device, mdi->instanceBuffer, NULL );
        vkFreeMemory( vk.device, mdi->instanceMemory, NULL );
    }
    
    if ( mdi->visibilityBuffer ) {
        vkDestroyBuffer( vk.device, mdi->visibilityBuffer, NULL );
        vkFreeMemory( vk.device, mdi->visibilityMemory, NULL );
    }
    
    if ( mdi->cullPipeline ) {
        vkDestroyPipeline( vk.device, mdi->cullPipeline, NULL );
    }
    
    if ( mdi->cullPipelineLayout ) {
        vkDestroyPipelineLayout( vk.device, mdi->cullPipelineLayout, NULL );
    }
}

/*
================
R_BeginMultiDrawIndirect

Begin collecting draws for multi-draw indirect
================
*/
void R_BeginMultiDrawIndirect( void ) {
    multiDrawIndirect_t *mdi = &gpuContext.multiDraw;
    
    mdi->numDraws = 0;
    mdi->numInstances = 0;
    
    // Clear visibility buffer
    Com_Memset( mdi->visibilityData, 0xff, sizeof( uint32_t ) * mdi->maxDraws );
}

/*
================
R_AddDrawIndirect

Add a draw to the multi-draw indirect buffer
================
*/
void R_AddDrawIndirect( const drawSurf_t *drawSurf ) {
    multiDrawIndirect_t *mdi = &gpuContext.multiDraw;
    gpuDrawCommand_t *cmd;
    gpuInstance_t *instance;
    
    if ( mdi->numDraws >= mdi->maxDraws ) {
        ri.Printf( PRINT_WARNING, "R_AddDrawIndirect: MAX_INDIRECT_DRAWS hit\n" );
        return;
    }
    
    if ( mdi->numInstances >= mdi->maxInstances ) {
        ri.Printf( PRINT_WARNING, "R_AddDrawIndirect: MAX_INSTANCES hit\n" );
        return;
    }
    
    // Setup draw command
    cmd = &mdi->drawCommands[mdi->numDraws];
    cmd->cmd.indexCount = drawSurf->numIndexes;
    cmd->cmd.instanceCount = 1;
    cmd->cmd.firstIndex = drawSurf->firstIndex;
    cmd->cmd.vertexOffset = drawSurf->firstVertex;
    cmd->cmd.firstInstance = mdi->numInstances;
    
    cmd->instanceOffset = mdi->numInstances;
    cmd->materialID = drawSurf->materialID;
    cmd->meshID = drawSurf->meshID;
    cmd->visibilityID = mdi->numDraws;
    
    // Setup instance data
    instance = &mdi->instances[mdi->numInstances];
    MatrixCopy( drawSurf->modelMatrix, instance->modelMatrix );
    MatrixCopy( drawSurf->normalMatrix, instance->normalMatrix );
    VectorCopy4( drawSurf->color, instance->color );
    instance->materialID = drawSurf->materialID;
    instance->flags = drawSurf->dlightBits;
    instance->lightMask = drawSurf->lightMask;
    
    mdi->numDraws++;
    mdi->numInstances++;
}

/*
================
R_EndMultiDrawIndirect

Finish collecting draws and upload to GPU
================
*/
void R_EndMultiDrawIndirect( void ) {
    multiDrawIndirect_t *mdi = &gpuContext.multiDraw;
    VkCommandBuffer cmd = vk.cmd->command_buffer;
    
    if ( mdi->numDraws == 0 ) {
        return;
    }
    
    // Upload draw commands
    R_UpdateGPUBuffer( cmd, mdi->drawBuffer, mdi->drawCommands,
                      mdi->numDraws * sizeof( gpuDrawCommand_t ) );
    
    // Upload instance data
    R_UpdateGPUBuffer( cmd, mdi->instanceBuffer, mdi->instances,
                      mdi->numInstances * sizeof( gpuInstance_t ) );
    
    // Upload draw count
    R_UpdateGPUBuffer( cmd, mdi->countBuffer, &mdi->numDraws,
                      sizeof( uint32_t ) );
    
    // Update statistics
    gpuContext.totalDrawCalls = mdi->numDraws;
}

/*
================
R_ExecuteMultiDrawIndirect

Execute multi-draw indirect commands
================
*/
void R_ExecuteMultiDrawIndirect( VkCommandBuffer commandBuffer ) {
    multiDrawIndirect_t *mdi = &gpuContext.multiDraw;
    
    if ( mdi->numDraws == 0 ) {
        return;
    }
    
    // Execute GPU culling if enabled
    if ( r_gpuCulling->integer ) {
        R_ExecuteGPUCulling( commandBuffer );
    }
    
    // Bind instance buffer
    // TODO: Get actual vertex buffer from current draw state
    VkBuffer currentVertexBuffer = VK_NULL_HANDLE; 
    VkBuffer vertexBuffers[] = { currentVertexBuffer, mdi->instanceBuffer };
    VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers( commandBuffer, 0, 2, vertexBuffers, offsets );
    
    // Execute multi-draw indirect
    vkCmdDrawIndexedIndirectCount( commandBuffer,
                                   mdi->drawBuffer, 0,
                                   mdi->countBuffer, 0,
                                   mdi->numDraws,
                                   sizeof( VkDrawIndexedIndirectCommand ) );
}

/*
================
R_InitGPUCulling

Initialize GPU culling system
================
*/
static void R_InitGPUCulling( void ) {
    // GPU culling initialization is handled in compute shader creation
}

/*
================
R_ShutdownGPUCulling

Shutdown GPU culling system
================
*/
static void R_ShutdownGPUCulling( void ) {
    // Cleanup is handled in multi-draw indirect shutdown
}

/*
================
R_SetupGPUCulling

Setup GPU culling parameters for current view
================
*/
void R_SetupGPUCulling( const viewParms_t *viewParms ) {
    gpuCullData_t cullData;
    
    // Setup culling data
    MatrixCopy( viewParms->projectionMatrix, cullData.viewProjMatrix );
    
    // Copy frustum planes
    for ( int i = 0; i < 6; i++ ) {
        VectorCopy( viewParms->frustum[i].normal, cullData.frustumPlanes[i] );
        cullData.frustumPlanes[i][3] = viewParms->frustum[i].dist;
    }
    
    VectorCopy( viewParms->or.origin, cullData.viewOrigin );
    cullData.lodBias = r_lodBias ? r_lodBias->value : 1.0f;
    cullData.drawCount = gpuContext.multiDraw.numDraws;
    cullData.meshletCount = gpuContext.meshShading.numMeshlets;
    cullData.hiZWidth = glConfig.vidWidth;
    cullData.hiZHeight = glConfig.vidHeight;
    
    // Upload to GPU
    // Update cull data buffer (TODO: create buffer)
    VkBuffer cullDataBuffer = VK_NULL_HANDLE;
    R_UpdateGPUBuffer( vk.cmd->command_buffer, cullDataBuffer, &cullData,
                      sizeof( gpuCullData_t ) );
}

/*
================
R_ExecuteGPUCulling

Execute GPU culling compute shader
================
*/
void R_ExecuteGPUCulling( VkCommandBuffer commandBuffer ) {
    multiDrawIndirect_t *mdi = &gpuContext.multiDraw;
    
    if ( !mdi->cullPipeline ) {
        return;
    }
    
    // Bind compute pipeline
    vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                       mdi->cullPipeline );
    
    // Bind descriptor sets
    vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            mdi->cullPipelineLayout, 0, 1,
                            &mdi->cullDescriptorSet, 0, NULL );
    
    // Dispatch compute shader
    uint32_t groupCount = ( mdi->numDraws + 63 ) / 64;
    vkCmdDispatch( commandBuffer, groupCount, 1, 1 );
    
    // Memory barrier
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT
    };
    
    vkCmdPipelineBarrier( commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                         0, 1, &barrier, 0, NULL, 0, NULL );
}

/*
================
R_CreateHiZBuffer

Create hierarchical-Z buffer for GPU occlusion culling
================
*/
static void R_CreateHiZBuffer( void ) {
    uint32_t width = glConfig.vidWidth;
    uint32_t height = glConfig.vidHeight;
    uint32_t mipLevels = 1;
    
    // Calculate mip levels
    while ( width > 1 || height > 1 ) {
        width >>= 1;
        height >>= 1;
        mipLevels++;
    }
    
    gpuContext.hiZMipLevels = mipLevels;
    
    // Create HiZ image (using color format to store depth values)
    R_CreateGPUImage( &gpuContext.hiZBuffer,
                     &gpuContext.hiZBufferMemory,
                     &gpuContext.hiZBufferView,
                     glConfig.vidWidth, glConfig.vidHeight,
                     VK_FORMAT_R32_SFLOAT,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT );
}

/*
================
R_DestroyHiZBuffer

Destroy hierarchical-Z buffer
================
*/
static void R_DestroyHiZBuffer( void ) {
    if ( gpuContext.hiZBuffer ) {
        vkDestroyImageView( vk.device, gpuContext.hiZBufferView, NULL );
        vkDestroyImage( vk.device, gpuContext.hiZBuffer, NULL );
        vkFreeMemory( vk.device, gpuContext.hiZBufferMemory, NULL );
    }
}

/*
================
R_UpdateHiZBuffer

Update hierarchical-Z buffer with depth pyramid
================
*/
void R_UpdateHiZBuffer( VkCommandBuffer commandBuffer ) {
    // Transition HiZ buffer for compute shader write
    R_TransitionImageLayout( commandBuffer, gpuContext.hiZBuffer,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT );
    
    // Bind HiZ generation compute shader
    // TODO: Create HiZ generation pipeline
    VkPipeline hiZGenerationPipeline = VK_NULL_HANDLE;
    vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                       hiZGenerationPipeline );
    
    // Generate HiZ mip chain
    uint32_t width = glConfig.vidWidth;
    uint32_t height = glConfig.vidHeight;
    
    for ( uint32_t mip = 1; mip < gpuContext.hiZMipLevels; mip++ ) {
        width = MAX( 1, width >> 1 );
        height = MAX( 1, height >> 1 );
        
        // Dispatch compute shader for this mip level
        uint32_t groupX = ( width + 7 ) / 8;
        uint32_t groupY = ( height + 7 ) / 8;
        vkCmdDispatch( commandBuffer, groupX, groupY, 1 );
        
        // Barrier between mip levels
        VkMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
        };
        
        vkCmdPipelineBarrier( commandBuffer,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &barrier, 0, NULL, 0, NULL );
    }
    
    // Transition for sampling
    R_TransitionImageLayout( commandBuffer, gpuContext.hiZBuffer,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT );
}

/*
================
R_CreateTimestampQueries

Create GPU timestamp query pool
================
*/
static void R_CreateTimestampQueries( void ) {
    gpuContext.timestampCount = 64;
    
    VkQueryPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = gpuContext.timestampCount
    };
    
    VK_CHECK( vkCreateQueryPool( vk.device, &poolInfo, NULL,
                                 &gpuContext.timestampPool ) );
    
    gpuContext.timestamps = ri.Hunk_Alloc( sizeof( uint64_t ) * gpuContext.timestampCount, h_low );
}

/*
================
R_DestroyTimestampQueries

Destroy GPU timestamp query pool
================
*/
static void R_DestroyTimestampQueries( void ) {
    if ( gpuContext.timestampPool ) {
        vkDestroyQueryPool( vk.device, gpuContext.timestampPool, NULL );
    }
}

/*
================
R_CreateGPUBuffer

Helper to create GPU buffer
================
*/
void R_CreateGPUBuffer( VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size,
                       VkBufferUsageFlags usage, VkMemoryPropertyFlags properties ) {
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    
    VK_CHECK( vkCreateBuffer( vk.device, &bufferInfo, NULL, buffer ) );
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements( vk.device, *buffer, &memRequirements );
    
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = vk_find_memory_type( memRequirements.memoryTypeBits, properties )
    };
    
    VK_CHECK( vkAllocateMemory( vk.device, &allocInfo, NULL, memory ) );
    VK_CHECK( vkBindBufferMemory( vk.device, *buffer, *memory, 0 ) );
}

/*
================
R_CreateGPUImage

Helper to create GPU image
================
*/
void R_CreateGPUImage( VkImage *image, VkDeviceMemory *memory, VkImageView *view,
                      uint32_t width, uint32_t height, VkFormat format,
                      VkImageUsageFlags usage, VkImageAspectFlags aspect ) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    
    VK_CHECK( vkCreateImage( vk.device, &imageInfo, NULL, image ) );
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements( vk.device, *image, &memRequirements );
    
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = vk_find_memory_type( memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
    };
    
    VK_CHECK( vkAllocateMemory( vk.device, &allocInfo, NULL, memory ) );
    VK_CHECK( vkBindImageMemory( vk.device, *image, *memory, 0 ) );
    
    // Create image view
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    VK_CHECK( vkCreateImageView( vk.device, &viewInfo, NULL, view ) );
}

/*
================
R_TransitionImageLayout

Helper to transition image layout
================
*/
void R_TransitionImageLayout( VkCommandBuffer commandBuffer, VkImage image,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkImageAspectFlags aspect ) {
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = 0,
            .levelCount = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS
        }
    };
    
    VkPipelineStageFlags srcStage, dstStage;
    
    if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
         newLayout == VK_IMAGE_LAYOUT_GENERAL ) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if ( oldLayout == VK_IMAGE_LAYOUT_GENERAL &&
                newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        ri.Error( ERR_FATAL, "Unsupported image layout transition" );
        return;
    }
    
    vkCmdPipelineBarrier( commandBuffer, srcStage, dstStage, 0,
                         0, NULL, 0, NULL, 1, &barrier );
}

/*
================
R_UpdateGPUBuffer

Helper to update GPU buffer
================
*/
static void R_UpdateGPUBuffer( VkCommandBuffer commandBuffer, VkBuffer buffer,
                              const void *data, VkDeviceSize size ) {
    // Use staging buffer for update
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    
    R_CreateGPUBuffer( &stagingBuffer, &stagingMemory, size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
    
    // Copy data to staging buffer
    void *mapped;
    vkMapMemory( vk.device, stagingMemory, 0, size, 0, &mapped );
    memcpy( mapped, data, size );
    vkUnmapMemory( vk.device, stagingMemory );
    
    // Copy staging buffer to GPU buffer
    VkBufferCopy copyRegion = { 0, 0, size };
    vkCmdCopyBuffer( commandBuffer, stagingBuffer, buffer, 1, &copyRegion );
    
    // Memory barrier
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
    };
    
    vkCmdPipelineBarrier( commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, NULL, 0, NULL );
    
    // Clean up staging buffer
    vkDestroyBuffer( vk.device, stagingBuffer, NULL );
    vkFreeMemory( vk.device, stagingMemory, NULL );
}

/*
================
R_CreateGPUCullingPipeline

Create GPU culling compute pipeline
================
*/
static void R_CreateGPUCullingPipeline( multiDrawIndirect_t *mdi ) {
    // Load compute shader SPIR-V
    size_t codeSize;
    uint32_t *spirvCode = R_LoadSPIRV( "shaders/compute/gpu_culling.spv", &codeSize );
    
    if ( !spirvCode ) {
        ri.Printf( PRINT_WARNING, "Failed to load GPU culling compute shader\n" );
        return;
    }
    
    // Create shader module
    VkShaderModule shaderModule = R_CreateComputeShader( spirvCode, codeSize );
    
    // Create descriptor set layout
    VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 4,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
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
    VK_CHECK( vkCreateDescriptorSetLayout( vk.device, &layoutInfo, NULL,
                                          &descriptorSetLayout ) );
    
    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptorSetLayout
    };
    
    VK_CHECK( vkCreatePipelineLayout( vk.device, &pipelineLayoutInfo, NULL,
                                     &mdi->cullPipelineLayout ) );
    
    // Create compute pipeline
    mdi->cullPipeline = R_CreateComputePipeline( shaderModule, mdi->cullPipelineLayout );
    
    // Cleanup
    vkDestroyShaderModule( vk.device, shaderModule, NULL );
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorSetLayout
    };
    
    VK_CHECK( vkAllocateDescriptorSets( vk.device, &allocInfo,
                                       &mdi->cullDescriptorSet ) );
    
    // Update descriptor set
    VkBuffer cullDataBuffer = VK_NULL_HANDLE; // TODO: Create cull data buffer
    VkDescriptorBufferInfo bufferInfos[] = {
        { mdi->drawBuffer, 0, VK_WHOLE_SIZE },
        { mdi->instanceBuffer, 0, VK_WHOLE_SIZE },
        { mdi->visibilityBuffer, 0, VK_WHOLE_SIZE },
        { cullDataBuffer, 0, sizeof( gpuCullData_t ) }
    };
    
    VkDescriptorImageInfo imageInfo = {
        .sampler = VK_NULL_HANDLE, // TODO: Create HiZ sampler
        .imageView = gpuContext.hiZBufferView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    
    VkWriteDescriptorSet writes[] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mdi->cullDescriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &bufferInfos[0]
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mdi->cullDescriptorSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &bufferInfos[1]
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mdi->cullDescriptorSet,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &bufferInfos[2]
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mdi->cullDescriptorSet,
            .dstBinding = 3,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bufferInfos[3]
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mdi->cullDescriptorSet,
            .dstBinding = 4,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageInfo
        }
    };
    
    vkUpdateDescriptorSets( vk.device, ARRAY_LEN( writes ), writes, 0, NULL );
}

/*
================
R_CreateComputeShader

Create compute shader module
================
*/
VkShaderModule R_CreateComputeShader( const uint32_t *spirvCode, size_t codeSize ) {
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = codeSize,
        .pCode = spirvCode
    };
    
    VkShaderModule shaderModule;
    VK_CHECK( vkCreateShaderModule( vk.device, &createInfo, NULL, &shaderModule ) );
    
    return shaderModule;
}

/*
================
R_CreateComputePipeline

Create compute pipeline
================
*/
VkPipeline R_CreateComputePipeline( VkShaderModule shader, VkPipelineLayout layout ) {
    VkPipelineShaderStageCreateInfo shaderStage = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = shader,
        .pName = "main"
    };
    
    VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = shaderStage,
        .layout = layout
    };
    
    VkPipeline pipeline;
    VK_CHECK( vkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1,
                                       &pipelineInfo, NULL, &pipeline ) );
    
    return pipeline;
}

/*
================
R_DrawGPUStats

Draw GPU-driven rendering statistics
================
*/
void R_DrawGPUStats( void ) {
    if ( !r_speeds->integer ) {
        return;
    }
    
    ri.Printf( PRINT_ALL, "GPU Stats:\n" );
    ri.Printf( PRINT_ALL, "  Total Draws: %d\n", gpuContext.totalDrawCalls );
    ri.Printf( PRINT_ALL, "  Culled Draws: %d\n", gpuContext.culledDrawCalls );
    ri.Printf( PRINT_ALL, "  Total Meshlets: %d\n", gpuContext.totalMeshlets );
    ri.Printf( PRINT_ALL, "  Culled Meshlets: %d\n", gpuContext.culledMeshlets );
    
    if ( gpuContext.meshShading.supported ) {
        ri.Printf( PRINT_ALL, "  Mesh Shading: Enabled\n" );
    }
    
    if ( gpuContext.vrs.supported ) {
        ri.Printf( PRINT_ALL, "  Variable Rate Shading: Enabled\n" );
    }
    
    if ( gpuContext.bindless.supported ) {
        ri.Printf( PRINT_ALL, "  Bindless Resources: %d textures, %d buffers\n",
                  gpuContext.bindless.numTextures,
                  gpuContext.bindless.numBuffers );
    }
}

/*
================
R_DumpGPUTimestamps

Dump GPU timestamp data
================
*/
void R_DumpGPUTimestamps( void ) {
    if ( !gpuContext.timestampPool ) {
        return;
    }
    
    // Get timestamp results
    vkGetQueryPoolResults( vk.device, gpuContext.timestampPool,
                          0, gpuContext.timestampCount,
                          sizeof( uint64_t ) * gpuContext.timestampCount,
                          gpuContext.timestamps, sizeof( uint64_t ),
                          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT );
    
    // Calculate and print timings
    // Get timestamp period from physical device properties
    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties( vk.physical_device, &deviceProps );
    float timestampPeriod = deviceProps.limits.timestampPeriod;
    
    for ( uint32_t i = 0; i < gpuContext.timestampCount - 1; i++ ) {
        if ( gpuContext.timestamps[i] && gpuContext.timestamps[i + 1] ) {
            uint64_t delta = gpuContext.timestamps[i + 1] - gpuContext.timestamps[i];
            float ms = ( delta * timestampPeriod ) / 1000000.0f;
            ri.Printf( PRINT_ALL, "GPU Timer %d-%d: %.3f ms\n", i, i + 1, ms );
        }
    }
}