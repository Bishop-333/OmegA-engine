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
// tr_postprocess_descriptors.c - Descriptor set management for post-processing

#include "tr_postprocess.h"
#include "../vulkan/vk.h"

// External Vulkan state
extern Vk_Instance vk;

// Create descriptor pool for post-processing
VkDescriptorPool R_CreatePostProcessDescriptorPool( void ) {
    VkDescriptorPoolSize poolSizes[2];
    VkDescriptorPoolCreateInfo poolInfo;
    VkDescriptorPool pool;
    
    // Combined image samplers for all post-processing effects
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = POST_PASS_COUNT * 4; // Max 4 textures per effect
    
    // Storage images for compute-based effects if needed
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = POST_PASS_COUNT * 2;
    
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.pNext = NULL;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = POST_PASS_COUNT * 2; // Allow for double buffering
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    
    if ( qvkCreateDescriptorPool( vk.device, &poolInfo, NULL, &pool ) != VK_SUCCESS ) {
        ri.Error( ERR_FATAL, "Failed to create post-process descriptor pool" );
    }
    
    return pool;
}

// Create samplers for post-processing
void R_CreatePostProcessSamplers( void ) {
    VkSamplerCreateInfo samplerInfo;
    
    // Linear sampler for smooth filtering
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.pNext = NULL;
    samplerInfo.flags = 0;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    
    if ( qvkCreateSampler( vk.device, &samplerInfo, NULL, &postProcessState.linearSampler ) != VK_SUCCESS ) {
        ri.Error( ERR_FATAL, "Failed to create linear sampler" );
    }
    
    // Point sampler for pixel-perfect sampling
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    
    if ( qvkCreateSampler( vk.device, &samplerInfo, NULL, &postProcessState.pointSampler ) != VK_SUCCESS ) {
        ri.Error( ERR_FATAL, "Failed to create point sampler" );
    }
}

// Create ping-pong buffers for multi-pass effects
void R_CreatePostProcessPingPongBuffers( void ) {
    VkImageCreateInfo imageInfo;
    VkImageViewCreateInfo viewInfo;
    VkMemoryRequirements memRequirements;
    VkMemoryAllocateInfo allocInfo;
    VkResult result;
    
    // Don't create buffers if dimensions are not set yet
    if ( vk.renderWidth == 0 || vk.renderHeight == 0 ) {
        return;
    }
    
    // Skip if already created
    if ( postProcessState.chain.pingImage != VK_NULL_HANDLE ) {
        return;
    }
    
    // Image creation info
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = NULL;
    imageInfo.flags = 0;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = vk.renderWidth;
    imageInfo.extent.height = vk.renderHeight;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.queueFamilyIndexCount = 0;
    imageInfo.pQueueFamilyIndices = NULL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    // Create ping buffer
    result = qvkCreateImage( vk.device, &imageInfo, NULL, &postProcessState.chain.pingImage );
    if ( result != VK_SUCCESS ) {
        ri.Error( ERR_FATAL, "Failed to create ping buffer image" );
    }
    
    qvkGetImageMemoryRequirements( vk.device, postProcessState.chain.pingImage, &memRequirements );
    
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = NULL;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vk_find_memory_type( memRequirements.memoryTypeBits,
                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
    
    result = qvkAllocateMemory( vk.device, &allocInfo, NULL, &postProcessState.chain.pingMemory );
    if ( result != VK_SUCCESS ) {
        ri.Error( ERR_FATAL, "Failed to allocate ping buffer memory" );
    }
    
    qvkBindImageMemory( vk.device, postProcessState.chain.pingImage, postProcessState.chain.pingMemory, 0 );
    
    // Create pong buffer
    result = qvkCreateImage( vk.device, &imageInfo, NULL, &postProcessState.chain.pongImage );
    if ( result != VK_SUCCESS ) {
        ri.Error( ERR_FATAL, "Failed to create pong buffer image" );
    }
    
    qvkGetImageMemoryRequirements( vk.device, postProcessState.chain.pongImage, &memRequirements );
    
    result = qvkAllocateMemory( vk.device, &allocInfo, NULL, &postProcessState.chain.pongMemory );
    if ( result != VK_SUCCESS ) {
        ri.Error( ERR_FATAL, "Failed to allocate pong buffer memory" );
    }
    
    qvkBindImageMemory( vk.device, postProcessState.chain.pongImage, postProcessState.chain.pongMemory, 0 );
    
    // Create image views
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.pNext = NULL;
    viewInfo.flags = 0;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    // Ping view
    viewInfo.image = postProcessState.chain.pingImage;
    result = qvkCreateImageView( vk.device, &viewInfo, NULL, &postProcessState.chain.pingView );
    if ( result != VK_SUCCESS ) {
        ri.Error( ERR_FATAL, "Failed to create ping buffer view" );
    }
    
    // Pong view
    viewInfo.image = postProcessState.chain.pongImage;
    result = qvkCreateImageView( vk.device, &viewInfo, NULL, &postProcessState.chain.pongView );
    if ( result != VK_SUCCESS ) {
        ri.Error( ERR_FATAL, "Failed to create pong buffer view" );
    }
}

// Allocate descriptor set for a post-processing pass
VkDescriptorSet R_AllocatePostProcessDescriptorSet( VkDescriptorSetLayout layout ) {
    VkDescriptorSetAllocateInfo allocInfo;
    VkDescriptorSet descriptorSet;
    
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext = NULL;
    allocInfo.descriptorPool = postProcessState.descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;
    
    if ( qvkAllocateDescriptorSets( vk.device, &allocInfo, &descriptorSet ) != VK_SUCCESS ) {
        ri.Printf( PRINT_WARNING, "Failed to allocate post-process descriptor set\n" );
        return VK_NULL_HANDLE;
    }
    
    return descriptorSet;
}

// Update descriptor set with textures
void R_UpdatePostProcessDescriptorSet( VkDescriptorSet descriptorSet, 
                                       VkImageView colorView, 
                                       VkImageView secondaryView,
                                       VkSampler sampler ) {
    VkDescriptorImageInfo imageInfos[2];
    VkWriteDescriptorSet writes[2];
    int writeCount = 0;
    
    // Color texture (always present)
    imageInfos[0].sampler = sampler ? sampler : postProcessState.linearSampler;
    imageInfos[0].imageView = colorView;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[writeCount].pNext = NULL;
    writes[writeCount].dstSet = descriptorSet;
    writes[writeCount].dstBinding = 0;
    writes[writeCount].dstArrayElement = 0;
    writes[writeCount].descriptorCount = 1;
    writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[writeCount].pImageInfo = &imageInfos[0];
    writes[writeCount].pBufferInfo = NULL;
    writes[writeCount].pTexelBufferView = NULL;
    writeCount++;
    
    // Secondary texture (depth, velocity, etc.) if provided
    if ( secondaryView ) {
        imageInfos[1].sampler = postProcessState.pointSampler;
        imageInfos[1].imageView = secondaryView;
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        
        writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[writeCount].pNext = NULL;
        writes[writeCount].dstSet = descriptorSet;
        writes[writeCount].dstBinding = 1;
        writes[writeCount].dstArrayElement = 0;
        writes[writeCount].descriptorCount = 1;
        writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[writeCount].pImageInfo = &imageInfos[1];
        writes[writeCount].pBufferInfo = NULL;
        writes[writeCount].pTexelBufferView = NULL;
        writeCount++;
    }
    
    qvkUpdateDescriptorSets( vk.device, writeCount, writes, 0, NULL );
}

// Initialize descriptor resources
void R_InitPostProcessDescriptors( void ) {
    // Create descriptor pool
    postProcessState.descriptorPool = R_CreatePostProcessDescriptorPool();
    
    // Create samplers
    R_CreatePostProcessSamplers();
    
    // Don't create ping-pong buffers yet - dimensions not known
    // They will be created on first use or when dimensions are set
    
    ri.Printf( PRINT_ALL, "Post-processing descriptors initialized\n" );
}

// Ensure ping-pong buffers are created when needed
void R_EnsurePostProcessBuffers( void ) {
    R_CreatePostProcessPingPongBuffers();
}

// Cleanup descriptor resources
void R_ShutdownPostProcessDescriptors( void ) {
    // Destroy ping-pong buffers
    if ( postProcessState.chain.pingView ) {
        qvkDestroyImageView( vk.device, postProcessState.chain.pingView, NULL );
    }
    if ( postProcessState.chain.pingImage ) {
        qvkDestroyImage( vk.device, postProcessState.chain.pingImage, NULL );
    }
    if ( postProcessState.chain.pingMemory ) {
        qvkFreeMemory( vk.device, postProcessState.chain.pingMemory, NULL );
    }
    
    if ( postProcessState.chain.pongView ) {
        qvkDestroyImageView( vk.device, postProcessState.chain.pongView, NULL );
    }
    if ( postProcessState.chain.pongImage ) {
        qvkDestroyImage( vk.device, postProcessState.chain.pongImage, NULL );
    }
    if ( postProcessState.chain.pongMemory ) {
        qvkFreeMemory( vk.device, postProcessState.chain.pongMemory, NULL );
    }
    
    // Destroy samplers
    if ( postProcessState.linearSampler ) {
        qvkDestroySampler( vk.device, postProcessState.linearSampler, NULL );
    }
    if ( postProcessState.pointSampler ) {
        qvkDestroySampler( vk.device, postProcessState.pointSampler, NULL );
    }
    
    // Destroy descriptor pool
    if ( postProcessState.descriptorPool ) {
        qvkDestroyDescriptorPool( vk.device, postProcessState.descriptorPool, NULL );
    }
}

// Get current source image for post-processing
VkImage R_GetPostProcessSourceImage( void ) {
    if ( postProcessState.chain.currentPing ) {
        return postProcessState.chain.pongImage;
    } else {
        return postProcessState.chain.pingImage;
    }
}

// Get current destination image for post-processing
VkImage R_GetPostProcessDestImage( void ) {
    if ( postProcessState.chain.currentPing ) {
        return postProcessState.chain.pingImage;
    } else {
        return postProcessState.chain.pongImage;
    }
}

// Swap ping-pong buffers
void R_SwapPostProcessBuffers( void ) {
    postProcessState.chain.currentPing = !postProcessState.chain.currentPing;
}
