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
// tr_bindless.c - Bindless resource management implementation

#include "tr_bindless.h"
#include "tr_gpu_driven.h"
#include "../vulkan/vk.h"
#include <float.h>

// Forward declarations
static void R_InitBindlessPool( bindlessPool_t *pool, uint32_t maxTextures, uint32_t maxBuffers );
static void R_DestroyBindlessPool( bindlessPool_t *pool );
static void R_InitializeDummyDescriptors( bindlessPool_t *pool );

// Global bindless state
static bindlessState_t bindlessState;

// CVars
cvar_t *r_bindlessDebug;
cvar_t *r_bindlessMaxTextures;
cvar_t *r_bindlessMaxBuffers;

/*
================
R_InitBindlessResources

Initialize bindless resource system
================
*/
qboolean R_InitBindlessResources( void ) {
    Com_Memset( &bindlessState, 0, sizeof( bindlessState ) );
    
    // Register CVars
    r_bindlessDebug = ri.Cvar_Get( "r_bindlessDebug", "0", CVAR_CHEAT );
    r_bindlessMaxTextures = ri.Cvar_Get( "r_bindlessMaxTextures", "4096", CVAR_ARCHIVE );
    r_bindlessMaxBuffers = ri.Cvar_Get( "r_bindlessMaxBuffers", "2048", CVAR_ARCHIVE );
    
    // Check for descriptor indexing support
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2
    };
    
    VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES
    };
    features2.pNext = &indexingFeatures;
    
    vkGetPhysicalDeviceFeatures2( vk.physical_device, &features2 );
    
    if ( !indexingFeatures.descriptorBindingPartiallyBound ||
         !indexingFeatures.runtimeDescriptorArray ||
         !indexingFeatures.descriptorBindingSampledImageUpdateAfterBind ||
         !indexingFeatures.descriptorBindingStorageImageUpdateAfterBind ||
         !indexingFeatures.descriptorBindingStorageBufferUpdateAfterBind ) {
        ri.Printf( PRINT_WARNING, "Bindless resources not fully supported by GPU\n" );
        return qfalse;
    }
    
    bindlessState.supported = qtrue;
    bindlessState.descriptorIndexingSupported = qtrue;
    bindlessState.runtimeDescriptorArraySupported = qtrue;
    bindlessState.updateAfterBindSupported = qtrue;
    
    // Get descriptor limits
    VkPhysicalDeviceProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
    };
    
    VkPhysicalDeviceDescriptorIndexingProperties indexingProps = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES
    };
    props2.pNext = &indexingProps;
    
    vkGetPhysicalDeviceProperties2( vk.physical_device, &props2 );
    
    bindlessState.maxDescriptorSetSampledImages = indexingProps.maxDescriptorSetUpdateAfterBindSampledImages;
    bindlessState.maxDescriptorSetStorageImages = indexingProps.maxDescriptorSetUpdateAfterBindStorageImages;
    bindlessState.maxDescriptorSetStorageBuffers = indexingProps.maxDescriptorSetUpdateAfterBindStorageBuffers;
    bindlessState.maxDescriptorSetSamplers = indexingProps.maxDescriptorSetUpdateAfterBindSamplers;
    
    // Clamp to reasonable values
    uint32_t maxTextures = MIN( r_bindlessMaxTextures->integer, MAX_BINDLESS_TEXTURES );
    uint32_t maxBuffers = MIN( r_bindlessMaxBuffers->integer, MAX_BINDLESS_BUFFERS );
    
    maxTextures = MIN( maxTextures, bindlessState.maxDescriptorSetSampledImages );
    maxBuffers = MIN( maxBuffers, bindlessState.maxDescriptorSetStorageBuffers );
    
    // Initialize resource pool
    R_InitBindlessPool( &bindlessState.pool, maxTextures, maxBuffers );
    
    ri.Printf( PRINT_ALL, "Bindless resources initialized (max textures: %d, max buffers: %d)\n",
              maxTextures, maxBuffers );
    
    return qtrue;
}

/*
================
R_ShutdownBindlessResources

Shutdown bindless resource system
================
*/
void R_ShutdownBindlessResources( void ) {
    if ( !bindlessState.supported ) {
        return;
    }
    
    vkDeviceWaitIdle( vk.device );
    
    R_DestroyBindlessPool( &bindlessState.pool );
    
    Com_Memset( &bindlessState, 0, sizeof( bindlessState ) );
}

/*
================
R_InitBindlessPool

Initialize bindless descriptor pool
================
*/
static void R_InitBindlessPool( bindlessPool_t *pool, uint32_t maxTextures, uint32_t maxBuffers ) {
    // Allocate resource arrays
    pool->textures = ri.Hunk_Alloc( sizeof( bindlessTexture_t ) * maxTextures, h_low );
    pool->samplers = ri.Hunk_Alloc( sizeof( bindlessSampler_t ) * MAX_BINDLESS_SAMPLERS, h_low );
    pool->buffers = ri.Hunk_Alloc( sizeof( bindlessBuffer_t ) * maxBuffers, h_low );
    pool->images = ri.Hunk_Alloc( sizeof( bindlessImage_t ) * MAX_BINDLESS_IMAGES, h_low );
    
    // Allocate free lists
    pool->freeTextureList = ri.Hunk_Alloc( sizeof( uint32_t ) * maxTextures, h_low );
    pool->freeSamplerList = ri.Hunk_Alloc( sizeof( uint32_t ) * MAX_BINDLESS_SAMPLERS, h_low );
    pool->freeBufferList = ri.Hunk_Alloc( sizeof( uint32_t ) * maxBuffers, h_low );
    pool->freeImageList = ri.Hunk_Alloc( sizeof( uint32_t ) * MAX_BINDLESS_IMAGES, h_low );
    
    // Initialize free lists
    for ( uint32_t i = 0; i < maxTextures; i++ ) {
        pool->freeTextureList[i] = maxTextures - 1 - i;
    }
    pool->numFreeTextures = maxTextures;
    
    for ( uint32_t i = 0; i < MAX_BINDLESS_SAMPLERS; i++ ) {
        pool->freeSamplerList[i] = MAX_BINDLESS_SAMPLERS - 1 - i;
    }
    pool->numFreeSamplers = MAX_BINDLESS_SAMPLERS;
    
    for ( uint32_t i = 0; i < maxBuffers; i++ ) {
        pool->freeBufferList[i] = maxBuffers - 1 - i;
    }
    pool->numFreeBuffers = maxBuffers;
    
    for ( uint32_t i = 0; i < MAX_BINDLESS_IMAGES; i++ ) {
        pool->freeImageList[i] = MAX_BINDLESS_IMAGES - 1 - i;
    }
    pool->numFreeImages = MAX_BINDLESS_IMAGES;
    
    // Allocate dirty lists
    pool->dirtyTextures = ri.Hunk_Alloc( sizeof( uint32_t ) * maxTextures, h_low );
    pool->dirtySamplers = ri.Hunk_Alloc( sizeof( uint32_t ) * MAX_BINDLESS_SAMPLERS, h_low );
    pool->dirtyBuffers = ri.Hunk_Alloc( sizeof( uint32_t ) * maxBuffers, h_low );
    pool->dirtyImages = ri.Hunk_Alloc( sizeof( uint32_t ) * MAX_BINDLESS_IMAGES, h_low );
    
    // Create descriptor pool
    VkDescriptorPoolSize poolSizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = maxTextures
        },
        {
            .type = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = MAX_BINDLESS_SAMPLERS
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = maxBuffers
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = MAX_BINDLESS_IMAGES
        }
    };
    
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = ARRAY_LEN( poolSizes ),
        .pPoolSizes = poolSizes
    };
    
    VK_CHECK( vkCreateDescriptorPool( vk.device, &poolInfo, NULL, &pool->pool ) );
    
    // Create descriptor set layout
    VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = maxTextures,
            .stageFlags = VK_SHADER_STAGE_ALL
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = MAX_BINDLESS_SAMPLERS,
            .stageFlags = VK_SHADER_STAGE_ALL
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = maxBuffers,
            .stageFlags = VK_SHADER_STAGE_ALL
        },
        {
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = MAX_BINDLESS_IMAGES,
            .stageFlags = VK_SHADER_STAGE_ALL
        }
    };
    
    VkDescriptorBindingFlags bindingFlags[] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | 
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
        
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | 
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
        
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | 
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
        
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | 
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
    };
    
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = ARRAY_LEN( bindingFlags ),
        .pBindingFlags = bindingFlags
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = ARRAY_LEN( bindings ),
        .pBindings = bindings,
        .pNext = &bindingFlagsInfo
    };
    
    VK_CHECK( vkCreateDescriptorSetLayout( vk.device, &layoutInfo, NULL, &pool->setLayout ) );
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &pool->setLayout
    };
    
    VK_CHECK( vkAllocateDescriptorSets( vk.device, &allocInfo, &pool->descriptorSet ) );
    
    // Initialize with dummy descriptors to avoid validation errors
    R_InitializeDummyDescriptors( pool );
}

/*
================
R_DestroyBindlessPool

Destroy bindless descriptor pool
================
*/
static void R_DestroyBindlessPool( bindlessPool_t *pool ) {
    if ( pool->pool ) {
        vkDestroyDescriptorPool( vk.device, pool->pool, NULL );
    }
    
    if ( pool->setLayout ) {
        vkDestroyDescriptorSetLayout( vk.device, pool->setLayout, NULL );
    }
}

/*
================
R_InitializeDummyDescriptors

Initialize pool with dummy descriptors
================
*/
static void R_InitializeDummyDescriptors( bindlessPool_t *pool ) {
    // Create dummy texture
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { 1, 1, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    
    VkImage dummyImage;
    VkDeviceMemory dummyMemory;
    VK_CHECK( vkCreateImage( vk.device, &imageInfo, NULL, &dummyImage ) );
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements( vk.device, dummyImage, &memReqs );
    
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = vk_find_memory_type( memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
    };
    
    VK_CHECK( vkAllocateMemory( vk.device, &allocInfo, NULL, &dummyMemory ) );
    VK_CHECK( vkBindImageMemory( vk.device, dummyImage, dummyMemory, 0 ) );
    
    // Create dummy image view
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = dummyImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    VkImageView dummyView;
    VK_CHECK( vkCreateImageView( vk.device, &viewInfo, NULL, &dummyView ) );
    
    // Create dummy sampler
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxAnisotropy = 1.0f
    };
    
    VkSampler dummySampler;
    VK_CHECK( vkCreateSampler( vk.device, &samplerInfo, NULL, &dummySampler ) );
    
    // Store dummy resources
    bindlessState.dummyImage = dummyImage;
    bindlessState.dummyImageView = dummyView;
    bindlessState.dummyImageMemory = dummyMemory;
    bindlessState.dummySampler = dummySampler;
}

/*
================
R_RegisterBindlessTexture

Register a texture for bindless access
================
*/
uint32_t R_RegisterBindlessTexture( VkImageView imageView, VkSampler sampler ) {
    return R_RegisterBindlessTextureWithName( imageView, sampler, "" );
}

/*
================
R_RegisterBindlessTextureWithName

Register a texture with a name for bindless access
================
*/
uint32_t R_RegisterBindlessTextureWithName( VkImageView imageView, VkSampler sampler, const char *name ) {
    bindlessPool_t *pool = &bindlessState.pool;
    
    if ( pool->numFreeTextures == 0 ) {
        ri.Printf( PRINT_WARNING, "R_RegisterBindlessTexture: Out of texture slots\n" );
        return BINDLESS_INVALID_HANDLE;
    }
    
    // Get free slot
    uint32_t slot = pool->freeTextureList[--pool->numFreeTextures];
    bindlessTexture_t *texture = &pool->textures[slot];
    
    // Setup texture descriptor
    texture->imageView = imageView;
    texture->sampler = sampler;
    texture->textureID = slot;
    texture->inUse = qtrue;
    Q_strncpyz( texture->name, name, sizeof( texture->name ) );
    
    // Add to dirty list
    pool->dirtyTextures[pool->numDirtyTextures++] = slot;
    pool->needsUpdate = qtrue;
    
    bindlessState.textureUpdates++;
    
    return slot;
}

/*
================
R_UnregisterBindlessTexture

Unregister a bindless texture
================
*/
void R_UnregisterBindlessTexture( uint32_t handle ) {
    bindlessPool_t *pool = &bindlessState.pool;
    
    if ( handle >= MAX_BINDLESS_TEXTURES || !pool->textures[handle].inUse ) {
        return;
    }
    
    bindlessTexture_t *texture = &pool->textures[handle];
    texture->inUse = qfalse;
    
    // Return to free list
    pool->freeTextureList[pool->numFreeTextures++] = handle;
    
    // Update with dummy descriptor
    texture->imageView = bindlessState.dummyImageView;
    texture->sampler = bindlessState.dummySampler;
    
    // Add to dirty list
    pool->dirtyTextures[pool->numDirtyTextures++] = handle;
    pool->needsUpdate = qtrue;
}

/*
================
R_RegisterBindlessBuffer

Register a buffer for bindless access
================
*/
uint32_t R_RegisterBindlessBuffer( VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range ) {
    bindlessPool_t *pool = &bindlessState.pool;
    
    if ( pool->numFreeBuffers == 0 ) {
        ri.Printf( PRINT_WARNING, "R_RegisterBindlessBuffer: Out of buffer slots\n" );
        return BINDLESS_INVALID_HANDLE;
    }
    
    // Get free slot
    uint32_t slot = pool->freeBufferList[--pool->numFreeBuffers];
    bindlessBuffer_t *buf = &pool->buffers[slot];
    
    // Setup buffer descriptor
    buf->buffer = buffer;
    buf->offset = offset;
    buf->range = range;
    buf->bufferID = slot;
    buf->inUse = qtrue;
    
    // Add to dirty list
    pool->dirtyBuffers[pool->numDirtyBuffers++] = slot;
    pool->needsUpdate = qtrue;
    
    bindlessState.bufferUpdates++;
    bindlessState.totalBufferMemory += range;
    
    return slot;
}

/*
================
R_UnregisterBindlessBuffer

Unregister a bindless buffer
================
*/
void R_UnregisterBindlessBuffer( uint32_t handle ) {
    bindlessPool_t *pool = &bindlessState.pool;
    
    if ( handle >= MAX_BINDLESS_BUFFERS || !pool->buffers[handle].inUse ) {
        return;
    }
    
    bindlessBuffer_t *buf = &pool->buffers[handle];
    
    bindlessState.totalBufferMemory -= buf->range;
    buf->inUse = qfalse;
    
    // Return to free list
    pool->freeBufferList[pool->numFreeBuffers++] = handle;
    
    // Add to dirty list
    pool->dirtyBuffers[pool->numDirtyBuffers++] = handle;
    pool->needsUpdate = qtrue;
}

/*
================
R_RegisterBindlessSampler

Register a sampler for bindless access
================
*/
uint32_t R_RegisterBindlessSampler( VkSampler sampler ) {
    bindlessPool_t *pool = &bindlessState.pool;
    
    if ( pool->numFreeSamplers == 0 ) {
        ri.Printf( PRINT_WARNING, "R_RegisterBindlessSampler: Out of sampler slots\n" );
        return BINDLESS_INVALID_HANDLE;
    }
    
    // Get free slot
    uint32_t slot = pool->freeSamplerList[--pool->numFreeSamplers];
    bindlessSampler_t *samp = &pool->samplers[slot];
    
    // Setup sampler descriptor
    samp->sampler = sampler;
    samp->samplerID = slot;
    samp->inUse = qtrue;
    
    // Add to dirty list
    pool->dirtySamplers[pool->numDirtySamplers++] = slot;
    pool->needsUpdate = qtrue;
    
    return slot;
}

/*
================
R_UpdateBindlessDescriptors

Update all dirty bindless descriptors
================
*/
void R_UpdateBindlessDescriptors( void ) {
    bindlessPool_t *pool = &bindlessState.pool;
    
    if ( !pool->needsUpdate ) {
        return;
    }
    
    VkWriteDescriptorSet writes[4];
    uint32_t writeCount = 0;
    
    // Update dirty textures
    if ( pool->numDirtyTextures > 0 ) {
        VkDescriptorImageInfo *imageInfos = ri.Hunk_AllocateTempMemory( 
            sizeof( VkDescriptorImageInfo ) * pool->numDirtyTextures );
        
        for ( uint32_t i = 0; i < pool->numDirtyTextures; i++ ) {
            uint32_t slot = pool->dirtyTextures[i];
            bindlessTexture_t *texture = &pool->textures[slot];
            
            imageInfos[i].sampler = VK_NULL_HANDLE;
            imageInfos[i].imageView = texture->imageView;
            imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        
        writes[writeCount++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = pool->descriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = pool->numDirtyTextures,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = imageInfos
        };
        
        ri.Hunk_FreeTempMemory( imageInfos );
        pool->numDirtyTextures = 0;
    }
    
    // Update dirty samplers
    if ( pool->numDirtySamplers > 0 ) {
        VkDescriptorImageInfo *samplerInfos = ri.Hunk_AllocateTempMemory( 
            sizeof( VkDescriptorImageInfo ) * pool->numDirtySamplers );
        
        for ( uint32_t i = 0; i < pool->numDirtySamplers; i++ ) {
            uint32_t slot = pool->dirtySamplers[i];
            bindlessSampler_t *samp = &pool->samplers[slot];
            
            samplerInfos[i].sampler = samp->sampler;
            samplerInfos[i].imageView = VK_NULL_HANDLE;
            samplerInfos[i].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        
        writes[writeCount++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = pool->descriptorSet,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = pool->numDirtySamplers,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .pImageInfo = samplerInfos
        };
        
        ri.Hunk_FreeTempMemory( samplerInfos );
        pool->numDirtySamplers = 0;
    }
    
    // Update dirty buffers
    if ( pool->numDirtyBuffers > 0 ) {
        VkDescriptorBufferInfo *bufferInfos = ri.Hunk_AllocateTempMemory( 
            sizeof( VkDescriptorBufferInfo ) * pool->numDirtyBuffers );
        
        for ( uint32_t i = 0; i < pool->numDirtyBuffers; i++ ) {
            uint32_t slot = pool->dirtyBuffers[i];
            bindlessBuffer_t *buf = &pool->buffers[slot];
            
            bufferInfos[i].buffer = buf->inUse ? buf->buffer : VK_NULL_HANDLE;
            bufferInfos[i].offset = buf->offset;
            bufferInfos[i].range = buf->range;
        }
        
        writes[writeCount++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = pool->descriptorSet,
            .dstBinding = 2,
            .dstArrayElement = 0,
            .descriptorCount = pool->numDirtyBuffers,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = bufferInfos
        };
        
        ri.Hunk_FreeTempMemory( bufferInfos );
        pool->numDirtyBuffers = 0;
    }
    
    // Perform update
    if ( writeCount > 0 ) {
        vkUpdateDescriptorSets( vk.device, writeCount, writes, 0, NULL );
    }
    
    pool->needsUpdate = qfalse;
}

/*
================
R_GetBindlessDescriptorSet

Get the bindless descriptor set
================
*/
VkDescriptorSet R_GetBindlessDescriptorSet( void ) {
    return bindlessState.pool.descriptorSet;
}

/*
================
R_GetBindlessDescriptorSetLayout

Get the bindless descriptor set layout
================
*/
VkDescriptorSetLayout R_GetBindlessDescriptorSetLayout( void ) {
    return bindlessState.pool.setLayout;
}

/*
================
R_IsBindlessSupported

Check if bindless resources are supported
================
*/
qboolean R_IsBindlessSupported( void ) {
    return bindlessState.supported;
}

/*
================
R_CreateBindlessMaterial

Create bindless material from shader
================
*/
void R_CreateBindlessMaterial( bindlessMaterial_t *material, shader_t *shader ) {
    Com_Memset( material, 0xff, sizeof( *material ) );
    
    // Register shader textures
    for ( int i = 0; i < MAX_SHADER_STAGES && shader->stages[i]; i++ ) {
        shaderStage_t *stage = shader->stages[i];
        
        if ( stage->bundle[0].image[0] ) {
            // Note: image_t has view member directly, sampler needs to be created
            VkSampler sampler = VK_NULL_HANDLE; // TODO: Get sampler from stage
            uint32_t handle = R_RegisterBindlessTexture( 
                stage->bundle[0].image[0]->view,
                sampler );
            
            if ( i == 0 ) {
                material->diffuseTexture = handle;
                material->diffuseSampler = 0;  // Default sampler
            }
        }
    }
}

/*
================
R_PrintBindlessStats

Print bindless resource statistics
================
*/
void R_PrintBindlessStats( void ) {
    if ( !bindlessState.supported ) {
        ri.Printf( PRINT_ALL, "Bindless resources not supported\n" );
        return;
    }
    
    bindlessPool_t *pool = &bindlessState.pool;
    
    ri.Printf( PRINT_ALL, "Bindless Resource Stats:\n" );
    ri.Printf( PRINT_ALL, "  Textures: %d / %d\n", 
              pool->numTextures - pool->numFreeTextures, pool->numTextures );
    ri.Printf( PRINT_ALL, "  Samplers: %d / %d\n",
              pool->numSamplers - pool->numFreeSamplers, pool->numSamplers );
    ri.Printf( PRINT_ALL, "  Buffers: %d / %d\n",
              pool->numBuffers - pool->numFreeBuffers, pool->numBuffers );
    ri.Printf( PRINT_ALL, "  Images: %d / %d\n",
              pool->numImages - pool->numFreeImages, pool->numImages );
    ri.Printf( PRINT_ALL, "  Texture Memory: %d MB\n", bindlessState.totalTextureMemory / ( 1024 * 1024 ) );
    ri.Printf( PRINT_ALL, "  Buffer Memory: %d MB\n", bindlessState.totalBufferMemory / ( 1024 * 1024 ) );
    ri.Printf( PRINT_ALL, "  Updates: %d textures, %d buffers\n",
              bindlessState.textureUpdates, bindlessState.bufferUpdates );
}