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
// tr_postprocess_pipelines.c - Vulkan pipeline creation for post-processing

#include "tr_postprocess.h"
#include "../vulkan/vk.h"

// External Vulkan state
extern Vk_Instance vk;
extern int vkSamples;

// Shader loading
static VkShaderModule R_LoadPostProcessShader( const char *name ) {
    char filename[MAX_QPATH];
    void *data;
    int size;
    VkShaderModule module;
    VkShaderModuleCreateInfo createInfo;
    VkResult result;
    
    Com_sprintf( filename, sizeof(filename), "shaders/postprocess/%s.spv", name );
    
    size = ri.FS_ReadFile( filename, &data );
    if ( size <= 0 || !data ) {
        ri.Printf( PRINT_WARNING, "Failed to load post-process shader: %s\n", filename );
        return VK_NULL_HANDLE;
    }
    
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = NULL;
    createInfo.flags = 0;
    createInfo.codeSize = size;
    createInfo.pCode = (uint32_t *)data;
    
    result = qvkCreateShaderModule( vk.device, &createInfo, NULL, &module );
    ri.FS_FreeFile( data );
    
    if ( result != VK_SUCCESS ) {
        ri.Printf( PRINT_WARNING, "Failed to create shader module for: %s\n", filename );
        return VK_NULL_HANDLE;
    }
    
    return module;
}

// Create descriptor set layout for post-processing
static VkDescriptorSetLayout R_CreatePostProcessDescriptorSetLayout( int numSamplers ) {
    VkDescriptorSetLayoutBinding bindings[4];
    VkDescriptorSetLayoutCreateInfo layoutInfo;
    VkDescriptorSetLayout layout;
    int bindingCount = 0;
    
    // Binding 0: Color texture
    bindings[bindingCount].binding = bindingCount;
    bindings[bindingCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[bindingCount].descriptorCount = 1;
    bindings[bindingCount].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[bindingCount].pImmutableSamplers = NULL;
    bindingCount++;
    
    // Binding 1: Secondary texture (depth, velocity, etc.) if needed
    if ( numSamplers > 1 ) {
        bindings[bindingCount].binding = bindingCount;
        bindings[bindingCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[bindingCount].descriptorCount = 1;
        bindings[bindingCount].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[bindingCount].pImmutableSamplers = NULL;
        bindingCount++;
    }
    
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = NULL;
    layoutInfo.flags = 0;
    layoutInfo.bindingCount = bindingCount;
    layoutInfo.pBindings = bindings;
    
    if ( qvkCreateDescriptorSetLayout( vk.device, &layoutInfo, NULL, &layout ) != VK_SUCCESS ) {
        ri.Error( ERR_FATAL, "Failed to create post-process descriptor set layout" );
    }
    
    return layout;
}

// Create pipeline layout with push constants
static VkPipelineLayout R_CreatePostProcessPipelineLayout( VkDescriptorSetLayout descSetLayout, size_t pushConstantSize ) {
    VkPipelineLayoutCreateInfo layoutInfo;
    VkPushConstantRange pushConstant;
    VkPipelineLayout pipelineLayout;
    
    pushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = pushConstantSize;
    
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = NULL;
    layoutInfo.flags = 0;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descSetLayout;
    layoutInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1 : 0;
    layoutInfo.pPushConstantRanges = pushConstantSize > 0 ? &pushConstant : NULL;
    
    if ( qvkCreatePipelineLayout( vk.device, &layoutInfo, NULL, &pipelineLayout ) != VK_SUCCESS ) {
        ri.Error( ERR_FATAL, "Failed to create post-process pipeline layout" );
    }
    
    return pipelineLayout;
}

// Create a post-processing pipeline
static VkPipeline R_CreatePostProcessPipeline( const char *vertShader, const char *fragShader, 
                                               VkPipelineLayout layout, VkRenderPass renderPass ) {
    VkGraphicsPipelineCreateInfo pipelineInfo;
    VkPipelineShaderStageCreateInfo shaderStages[2];
    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly;
    VkPipelineViewportStateCreateInfo viewportState;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineDepthStencilStateCreateInfo depthStencil;
    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    VkPipelineColorBlendStateCreateInfo colorBlending;
    VkPipelineDynamicStateCreateInfo dynamicState;
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipeline pipeline;
    
    // Load shaders
    VkShaderModule vertModule = R_LoadPostProcessShader( vertShader );
    VkShaderModule fragModule = R_LoadPostProcessShader( fragShader );
    
    if ( !vertModule || !fragModule ) {
        if ( vertModule ) qvkDestroyShaderModule( vk.device, vertModule, NULL );
        if ( fragModule ) qvkDestroyShaderModule( vk.device, fragModule, NULL );
        return VK_NULL_HANDLE;
    }
    
    // Shader stages
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].pNext = NULL;
    shaderStages[0].flags = 0;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";
    shaderStages[0].pSpecializationInfo = NULL;
    
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].pNext = NULL;
    shaderStages[1].flags = 0;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";
    shaderStages[1].pSpecializationInfo = NULL;
    
    // Vertex input (none for fullscreen triangle)
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = NULL;
    vertexInputInfo.flags = 0;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = NULL;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = NULL;
    
    // Input assembly
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.pNext = NULL;
    inputAssembly.flags = 0;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Viewport state (dynamic)
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = NULL;
    viewportState.flags = 0;
    viewportState.viewportCount = 1;
    viewportState.pViewports = NULL;
    viewportState.scissorCount = 1;
    viewportState.pScissors = NULL;
    
    // Rasterizer
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.pNext = NULL;
    rasterizer.flags = 0;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;
    rasterizer.lineWidth = 1.0f;
    
    // Multisampling - must match the render pass sample count
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.pNext = NULL;
    multisampling.flags = 0;
    multisampling.rasterizationSamples = vkSamples ? vkSamples : VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = NULL;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;
    
    // Depth/Stencil state - post-processing doesn't need depth testing
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.pNext = NULL;
    depthStencil.flags = 0;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front.failOp = VK_STENCIL_OP_KEEP;
    depthStencil.front.passOp = VK_STENCIL_OP_KEEP;
    depthStencil.front.depthFailOp = VK_STENCIL_OP_KEEP;
    depthStencil.front.compareOp = VK_COMPARE_OP_ALWAYS;
    depthStencil.front.compareMask = 0;
    depthStencil.front.writeMask = 0;
    depthStencil.front.reference = 0;
    depthStencil.back = depthStencil.front;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    
    // Color blending
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.pNext = NULL;
    colorBlending.flags = 0;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;
    
    // Dynamic state
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.pNext = NULL;
    dynamicState.flags = 0;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    
    // Create pipeline
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = NULL;
    pipelineInfo.flags = 0;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = NULL;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;
    
    if ( qvkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline ) != VK_SUCCESS ) {
        pipeline = VK_NULL_HANDLE;
    }
    
    // Clean up shader modules
    qvkDestroyShaderModule( vk.device, vertModule, NULL );
    qvkDestroyShaderModule( vk.device, fragModule, NULL );
    
    return pipeline;
}

// Initialize DOF pipeline
void R_InitDOFPipeline( postPass_t *pass ) {
    VkDescriptorSetLayout descSetLayout;
    size_t pushConstantSize;
    
    // DOF needs color and depth textures
    descSetLayout = R_CreatePostProcessDescriptorSetLayout( 2 );
    
    // DOF push constants
    // 5 floats (20 bytes) + 1 int (4 bytes) = 24 bytes
    pushConstantSize = 24;
    
    pass->layout = R_CreatePostProcessPipelineLayout( descSetLayout, pushConstantSize );
    pass->pipeline = R_CreatePostProcessPipeline( "dof_vert", "dof_frag", pass->layout, vk.render_pass.main );
    
    if ( !pass->pipeline || !pass->layout ) {
        ri.Printf( PRINT_WARNING, "Failed to create DOF pipeline\n" );
        pass->enabled = qfalse;
        pass->type = POST_PASS_DEPTH_OF_FIELD;
    } else {
        pass->enabled = qtrue;
        pass->type = POST_PASS_DEPTH_OF_FIELD;
    }
}

// Initialize Motion Blur pipeline
void R_InitMotionBlurPipeline( postPass_t *pass ) {
    VkDescriptorSetLayout descSetLayout;
    size_t pushConstantSize;
    
    // Motion blur needs color and velocity textures
    descSetLayout = R_CreatePostProcessDescriptorSetLayout( 2 );
    
    // Motion blur push constants
    // float velocityScale (4 bytes) + int samples (4 bytes) + float maxBlur (4 bytes) = 12 bytes
    pushConstantSize = 12;
    
    pass->layout = R_CreatePostProcessPipelineLayout( descSetLayout, pushConstantSize );
    pass->pipeline = R_CreatePostProcessPipeline( "fullscreen_vert", "motion_blur_frag", pass->layout, vk.render_pass.main );
    
    if ( !pass->pipeline || !pass->layout ) {
        ri.Printf( PRINT_WARNING, "Failed to create Motion Blur pipeline\n" );
        pass->enabled = qfalse;
        pass->type = POST_PASS_MOTION_BLUR;
    } else {
        pass->enabled = qtrue;
        pass->type = POST_PASS_MOTION_BLUR;
    }
}

// Initialize Chromatic Aberration pipeline
void R_InitChromaticAberrationPipeline( postPass_t *pass ) {
    VkDescriptorSetLayout descSetLayout;
    size_t pushConstantSize;
    
    // Chromatic aberration only needs color texture
    descSetLayout = R_CreatePostProcessDescriptorSetLayout( 1 );
    
    // Chromatic aberration push constants
    // float strength (4 bytes) + padding (12 bytes) + vec3 shift (12 bytes) = 28 bytes
    pushConstantSize = 28;
    
    pass->layout = R_CreatePostProcessPipelineLayout( descSetLayout, pushConstantSize );
    pass->pipeline = R_CreatePostProcessPipeline( "fullscreen_vert", "chromatic_aberration_frag", pass->layout, vk.render_pass.main );
    
    if ( !pass->pipeline || !pass->layout ) {
        ri.Printf( PRINT_WARNING, "Failed to create Chromatic Aberration pipeline\n" );
        pass->enabled = qfalse;
        pass->type = POST_PASS_CHROMATIC_ABERRATION;
    } else {
        pass->enabled = qtrue;
        pass->type = POST_PASS_CHROMATIC_ABERRATION;
    }
}

// Initialize Vignette pipeline
void R_InitVignettePipeline( postPass_t *pass ) {
    VkDescriptorSetLayout descSetLayout;
    size_t pushConstantSize;
    
    // Vignette only needs color texture
    descSetLayout = R_CreatePostProcessDescriptorSetLayout( 1 );
    
    // Vignette push constants
    pushConstantSize = sizeof(float) * 3; // intensity, radius, softness
    
    pass->layout = R_CreatePostProcessPipelineLayout( descSetLayout, pushConstantSize );
    pass->pipeline = R_CreatePostProcessPipeline( "fullscreen_vert", "vignette_frag", pass->layout, vk.render_pass.main );
    
    if ( !pass->pipeline || !pass->layout ) {
        ri.Printf( PRINT_WARNING, "Failed to create Vignette pipeline\n" );
        pass->enabled = qfalse;
        pass->type = POST_PASS_VIGNETTE;
    } else {
        pass->enabled = qtrue;
        pass->type = POST_PASS_VIGNETTE;
    }
}

// Initialize Film Grain pipeline
void R_InitFilmGrainPipeline( postPass_t *pass ) {
    VkDescriptorSetLayout descSetLayout;
    size_t pushConstantSize;
    
    // Film grain only needs color texture
    descSetLayout = R_CreatePostProcessDescriptorSetLayout( 1 );
    
    // Film grain push constants
    pushConstantSize = sizeof(float) * 3; // intensity, grainSize, time
    
    pass->layout = R_CreatePostProcessPipelineLayout( descSetLayout, pushConstantSize );
    pass->pipeline = R_CreatePostProcessPipeline( "fullscreen_vert", "film_grain_frag", pass->layout, vk.render_pass.main );
    
    if ( !pass->pipeline || !pass->layout ) {
        ri.Printf( PRINT_WARNING, "Failed to create Film Grain pipeline\n" );
        pass->enabled = qfalse;
        pass->type = POST_PASS_FILM_GRAIN;
    } else {
        pass->enabled = qtrue;
        pass->type = POST_PASS_FILM_GRAIN;
    }
}

// Initialize God Rays pipeline
void R_InitGodRaysPipeline( postPass_t *pass ) {
    VkDescriptorSetLayout descSetLayout;
    size_t pushConstantSize;
    
    // God rays needs color and depth textures
    descSetLayout = R_CreatePostProcessDescriptorSetLayout( 2 );
    
    // God rays push constants
    // vec2 lightPos (8 bytes) + 4 floats (16 bytes) + int samples (4 bytes) = 28 bytes
    pushConstantSize = 28;
    
    pass->layout = R_CreatePostProcessPipelineLayout( descSetLayout, pushConstantSize );
    pass->pipeline = R_CreatePostProcessPipeline( "fullscreen_vert", "god_rays_frag", pass->layout, vk.render_pass.main );
    
    if ( !pass->pipeline || !pass->layout ) {
        ri.Printf( PRINT_WARNING, "Failed to create God Rays pipeline\n" );
        pass->enabled = qfalse;
        pass->type = POST_PASS_GOD_RAYS;
    } else {
        pass->enabled = qtrue;
        pass->type = POST_PASS_GOD_RAYS;
    }
}

// Initialize all post-processing pipelines
void R_InitPostProcessPipelines( void ) {
    if ( !postProcessState.initialized ) {
        return;
    }
    
    // Initialize each effect's pipeline
    R_InitDOFPipeline( &postProcessState.chain.passes[POST_PASS_DEPTH_OF_FIELD] );
    R_InitMotionBlurPipeline( &postProcessState.chain.passes[POST_PASS_MOTION_BLUR] );
    R_InitChromaticAberrationPipeline( &postProcessState.chain.passes[POST_PASS_CHROMATIC_ABERRATION] );
    R_InitVignettePipeline( &postProcessState.chain.passes[POST_PASS_VIGNETTE] );
    R_InitFilmGrainPipeline( &postProcessState.chain.passes[POST_PASS_FILM_GRAIN] );
    R_InitGodRaysPipeline( &postProcessState.chain.passes[POST_PASS_GOD_RAYS] );
    
    ri.Printf( PRINT_ALL, "Post-processing pipelines initialized\n" );
}

// Cleanup pipelines
void R_ShutdownPostProcessPipelines( void ) {
    int i;
    
    for ( i = 0; i < POST_PASS_COUNT; i++ ) {
        postPass_t *pass = &postProcessState.chain.passes[i];
        
        if ( pass->pipeline ) {
            qvkDestroyPipeline( vk.device, pass->pipeline, NULL );
            pass->pipeline = VK_NULL_HANDLE;
        }
        
        if ( pass->layout ) {
            qvkDestroyPipelineLayout( vk.device, pass->layout, NULL );
            pass->layout = VK_NULL_HANDLE;
        }
    }
}