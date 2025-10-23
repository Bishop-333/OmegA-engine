/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

This file is part of Quake3e-HD.

Quake3e-HD is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
===========================================================================
*/
// tr_taa_pipeline.c - TAA Vulkan pipeline creation

#include "tr_taa.h"
#include "../vulkan/vk.h"

// External TAA state from tr_taa.h
extern taaState_t taaState;

/*
================
R_CreateTAADescriptorSetLayouts

Create descriptor set layouts for TAA
================
*/
qboolean R_CreateTAADescriptorSetLayouts( void ) {
    VkResult result;
    
    // Define descriptor set layout bindings
    VkDescriptorSetLayoutBinding bindings[] = {
        // Current frame color
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = NULL
        },
        // Previous frame color  
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = NULL
        },
        // Motion vectors
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = NULL
        },
        // Depth buffer
        {
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = NULL
        },
        // Output image
        {
            .binding = 4,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = NULL
        },
        // Uniform buffer for TAA parameters
        {
            .binding = 5,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = NULL
        }
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = ARRAY_LEN(bindings),
        .pBindings = bindings
    };
    
    result = vkCreateDescriptorSetLayout( vk.device, &layoutInfo, NULL, &taaState.pipeline.resolveSetLayout );
    if ( result != VK_SUCCESS ) {
        ri.Printf( PRINT_WARNING, "R_CreateTAADescriptorSetLayouts: Failed to create descriptor set layout\n" );
        return qfalse;
    }
    
    return qtrue;
}

/*
================
R_CreateTAAPipelineLayouts

Create pipeline layouts for TAA
================
*/
qboolean R_CreateTAAPipelineLayouts( void ) {
    VkResult result;
    
    if ( !taaState.pipeline.resolveSetLayout ) {
        ri.Printf( PRINT_WARNING, "R_CreateTAAPipelineLayouts: Descriptor set layout not created\n" );
        return qfalse;
    }
    
    // Define push constants for dynamic TAA parameters
    VkPushConstantRange pushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(vec4_t) * 4  // Jitter offset, resolution, feedback factor, etc.
    };
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &taaState.pipeline.resolveSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };
    
    result = vkCreatePipelineLayout( vk.device, &pipelineLayoutInfo, NULL, &taaState.pipeline.resolveLayout );
    if ( result != VK_SUCCESS ) {
        ri.Printf( PRINT_WARNING, "R_CreateTAAPipelineLayouts: Failed to create pipeline layout\n" );
        return qfalse;
    }
    
    return qtrue;
}

/*
================
R_CreateTAAComputePipelines

Create compute pipelines for TAA
================
*/
qboolean R_CreateTAAComputePipelines( void ) {
    VkResult result;
    uint32_t *taaShaderCode = NULL;
    uint32_t taaShaderSize = 0;
    uint32_t *sharpenShaderCode = NULL;
    uint32_t sharpenShaderSize = 0;
    
    if ( !taaState.pipeline.resolveLayout ) {
        ri.Printf( PRINT_WARNING, "R_CreateTAAComputePipelines: Pipeline layout not created\n" );
        return qfalse;
    }
    
    // Load TAA compute shader
    taaShaderCode = R_LoadSPIRV( "shaders/compute/taa_resolve.spv", &taaShaderSize );
    if ( !taaShaderCode ) {
        ri.Printf( PRINT_WARNING, "R_CreateTAAComputePipelines: Failed to load TAA shader\n" );
        // Use fallback or skip TAA
        return qfalse;
    }
    
    // Create TAA compute shader module
    VkShaderModuleCreateInfo shaderModuleInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = taaShaderSize,
        .pCode = taaShaderCode
    };
    
    VkShaderModule taaShaderModule;
    result = vkCreateShaderModule( vk.device, &shaderModuleInfo, NULL, &taaShaderModule );
    ri.Free( taaShaderCode );
    
    if ( result != VK_SUCCESS ) {
        ri.Printf( PRINT_WARNING, "R_CreateTAAComputePipelines: Failed to create TAA shader module\n" );
        return qfalse;
    }
    
    // Create TAA compute pipeline
    VkPipelineShaderStageCreateInfo taaStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = taaShaderModule,
        .pName = "main"
    };
    
    VkComputePipelineCreateInfo taaPipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = taaStageInfo,
        .layout = taaState.pipeline.resolveLayout
    };
    
    result = vkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &taaPipelineInfo, 
                                       NULL, &taaState.pipeline.resolvePipeline );
    
    vkDestroyShaderModule( vk.device, taaShaderModule, NULL );
    
    if ( result != VK_SUCCESS ) {
        ri.Printf( PRINT_WARNING, "R_CreateTAAComputePipelines: Failed to create TAA pipeline\n" );
        return qfalse;
    }
    
    // Load sharpening shader (optional)
    sharpenShaderCode = R_LoadSPIRV( "shaders/compute/taa_sharpen.spv", &sharpenShaderSize );
    if ( sharpenShaderCode ) {
        shaderModuleInfo.codeSize = sharpenShaderSize;
        shaderModuleInfo.pCode = sharpenShaderCode;
        
        VkShaderModule sharpenShaderModule;
        result = vkCreateShaderModule( vk.device, &shaderModuleInfo, NULL, &sharpenShaderModule );
        ri.Free( sharpenShaderCode );
        
        if ( result == VK_SUCCESS ) {
            VkPipelineShaderStageCreateInfo sharpenStageInfo = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = sharpenShaderModule,
                .pName = "main"
            };
            
            VkComputePipelineCreateInfo sharpenPipelineInfo = {
                .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage = sharpenStageInfo,
                .layout = taaState.pipeline.resolveLayout
            };
            
            result = vkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &sharpenPipelineInfo,
                                              NULL, &taaState.pipeline.sharpenPipeline );
            
            vkDestroyShaderModule( vk.device, sharpenShaderModule, NULL );
            
            if ( result != VK_SUCCESS ) {
                ri.Printf( PRINT_WARNING, "R_CreateTAAComputePipelines: Failed to create sharpen pipeline\n" );
                // Continue without sharpening
            }
        }
    }
    
    taaState.initialized = qtrue;
    return qtrue;
}

/*
================
R_AllocateTAADescriptorSets

Allocate descriptor sets for TAA
================
*/
qboolean R_AllocateTAADescriptorSets( void ) {
    VkResult result;
    
    if ( !taaState.pipeline.resolveSetLayout ) {
        ri.Printf( PRINT_WARNING, "R_AllocateTAADescriptorSets: Descriptor set layout not created\n" );
        return qfalse;
    }
    
    // Create descriptor pool
    VkDescriptorPoolSize poolSizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 8  // 4 samplers * 2 sets
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 2  // 1 storage image * 2 sets
        },
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 2  // 1 uniform buffer * 2 sets
        }
    };
    
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = ARRAY_LEN(poolSizes),
        .pPoolSizes = poolSizes,
        .maxSets = 2  // Double buffered
    };
    
    // Create a local descriptor pool for TAA
    static VkDescriptorPool taaDescriptorPool = VK_NULL_HANDLE;
    result = vkCreateDescriptorPool( vk.device, &poolInfo, NULL, &taaDescriptorPool );
    if ( result != VK_SUCCESS ) {
        ri.Printf( PRINT_WARNING, "R_AllocateTAADescriptorSets: Failed to create descriptor pool\n" );
        return qfalse;
    }
    
    // Allocate descriptor sets
    VkDescriptorSetLayout layouts[2] = { taaState.pipeline.resolveSetLayout, taaState.pipeline.resolveSetLayout };
    
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = taaDescriptorPool,
        .descriptorSetCount = 2,
        .pSetLayouts = layouts
    };
    
    // Allocate to the pipeline descriptor sets
    VkDescriptorSet descriptorSets[2] = { taaState.pipeline.resolveDescSet, taaState.pipeline.sharpenDescSet };
    result = vkAllocateDescriptorSets( vk.device, &allocInfo, descriptorSets );
    taaState.pipeline.resolveDescSet = descriptorSets[0];
    taaState.pipeline.sharpenDescSet = descriptorSets[1];
    if ( result != VK_SUCCESS ) {
        ri.Printf( PRINT_WARNING, "R_AllocateTAADescriptorSets: Failed to allocate descriptor sets\n" );
        vkDestroyDescriptorPool( vk.device, taaDescriptorPool, NULL );
        taaDescriptorPool = VK_NULL_HANDLE;
        return qfalse;
    }
    
    return qtrue;
}

/*
================
R_UpdateTAADescriptorSets

Update descriptor sets for TAA
================
*/
void R_UpdateTAADescriptorSets( void ) {
    static int currentSet = 0;
    VkWriteDescriptorSet writes[6];
    VkDescriptorImageInfo imageInfos[5];
    VkDescriptorBufferInfo bufferInfo;
    
    if ( !taaState.initialized || !taaState.pipeline.resolveDescSet ) {
        return;
    }
    
    // Toggle between descriptor sets
    currentSet = (currentSet + 1) % 2;
    
    // Setup image descriptors
    // Current frame
    imageInfos[0].sampler = vk.samplers.handle[0];  // Use first available sampler
    imageInfos[0].imageView = vk.color_image_view;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Previous frame
    imageInfos[1].sampler = vk.samplers.handle[0];  // Use first available sampler
    imageInfos[1].imageView = vk.color_image_view;  // Previous frame would be stored separately
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Motion vectors
    imageInfos[2].sampler = vk.samplers.handle[0];  // Use first available sampler
    imageInfos[2].imageView = vk.color_image_view;  // Motion vectors would be separate
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Depth
    imageInfos[3].sampler = vk.samplers.handle[0];  // Use first available sampler
    imageInfos[3].imageView = vk.depth_image_view;
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    
    // Output
    imageInfos[4].sampler = VK_NULL_HANDLE;
    imageInfos[4].imageView = vk.color_image_view;  // Output would be separate
    imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    // Setup buffer descriptor
    bufferInfo.buffer = vk.storage.buffer;  // Use storage buffer
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;
    
    // Write descriptor sets
    for ( int i = 0; i < 5; i++ ) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].pNext = NULL;
        writes[i].dstSet = currentSet == 0 ? taaState.pipeline.resolveDescSet : taaState.pipeline.sharpenDescSet;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;
        
        if ( i < 4 ) {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &imageInfos[i];
            writes[i].pBufferInfo = NULL;
        } else if ( i == 4 ) {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[i].pImageInfo = &imageInfos[i];
            writes[i].pBufferInfo = NULL;
        }
        
        writes[i].pTexelBufferView = NULL;
    }
    
    // Uniform buffer
    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].pNext = NULL;
    writes[5].dstSet = currentSet == 0 ? taaState.pipeline.resolveDescSet : taaState.pipeline.sharpenDescSet;
    writes[5].dstBinding = 5;
    writes[5].dstArrayElement = 0;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[5].pImageInfo = NULL;
    writes[5].pBufferInfo = &bufferInfo;
    writes[5].pTexelBufferView = NULL;
    
    vkUpdateDescriptorSets( vk.device, 6, writes, 0, NULL );
}

/*
================
R_CopyImage

Copy image with format conversion if needed
================
*/
void R_CopyImage( VkCommandBuffer cmd, VkImage src, VkImage dst, uint32_t width, uint32_t height ) {
    VkImageCopy copyRegion = {0};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.extent.width = width;
    copyRegion.extent.height = height;
    copyRegion.extent.depth = 1;
    
    vkCmdCopyImage( cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
}