/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "../core/tr_local.h"
#include "vk.h"
#include "vk_shader.h"

/*
================================================================================
Vulkan Uber-Shader Implementation

This file implements the unified shader system that replaces multiple pipeline
permutations with a single flexible uber-shader.
================================================================================
*/

// Global pipelines
vkPipeline_t *vk_uberPipeline = NULL;
vkPipeline_t *vk_postProcessPipeline = NULL;
vkPipeline_t *vk_skyboxPipeline = NULL;

// Shader modules
VkShaderModule vk_uberVertexShader = VK_NULL_HANDLE;
VkShaderModule vk_uberFragmentShader = VK_NULL_HANDLE;

// Uber shader descriptor set layout
VkDescriptorSetLayout vk_uberDescriptorSetLayout = VK_NULL_HANDLE;

// Pipeline cache for different blend states
#define MAX_PIPELINE_CACHE 16
static vkPipeline_t pipelineCache[MAX_PIPELINE_CACHE];
static int numCachedPipelines = 0;

/*
================
VK_CreateUberDescriptorSetLayout

Create descriptor set layout for uber shader
================
*/
static void VK_CreateUberDescriptorSetLayout(void) {
    VkDescriptorSetLayoutBinding bindings[6];
    VkDescriptorSetLayoutCreateInfo layoutInfo;
    VkResult result;

    // Binding 0: Dynamic uniform buffer for transforms
    Com_Memset(&bindings[0], 0, sizeof(bindings[0]));
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = NULL;

    // Binding 1: Diffuse texture
    Com_Memset(&bindings[1], 0, sizeof(bindings[1]));
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = NULL;

    // Binding 2: Lightmap texture
    Com_Memset(&bindings[2], 0, sizeof(bindings[2]));
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].pImmutableSamplers = NULL;

    // Binding 3: Normal map
    Com_Memset(&bindings[3], 0, sizeof(bindings[3]));
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[3].pImmutableSamplers = NULL;

    // Binding 4: Specular map
    Com_Memset(&bindings[4], 0, sizeof(bindings[4]));
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[4].pImmutableSamplers = NULL;

    // Binding 5: Environment map
    Com_Memset(&bindings[5], 0, sizeof(bindings[5]));
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[5].pImmutableSamplers = NULL;

    // Create descriptor set layout
    Com_Memset(&layoutInfo, 0, sizeof(layoutInfo));
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 6;
    layoutInfo.pBindings = bindings;

    result = vkCreateDescriptorSetLayout(vk.device, &layoutInfo, NULL, &vk_uberDescriptorSetLayout);
    if (result != VK_SUCCESS) {
        ri.Error(ERR_FATAL, "Failed to create uber shader descriptor set layout: %d", result);
    }
}

/*
================
VK_InitUberShaderSystem

Initialize the uber-shader system
================
*/
void VK_InitUberShaderSystem(void) {
    vkPipelineState_t defaultState;

    ri.Printf(PRINT_ALL, "Initializing Vulkan uber-shader system...\n");

    // Create uber shader descriptor set layout
    VK_CreateUberDescriptorSetLayout();

    // Load shader modules
    vk_uberVertexShader = VK_LoadShaderModule("shaders/uber.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    vk_uberFragmentShader = VK_LoadShaderModule("shaders/uber.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    if (!vk_uberVertexShader || !vk_uberFragmentShader) {
        ri.Error(ERR_FATAL, "Failed to load uber-shader modules");
    }
    
    // Create main uber-pipeline with default state
    Com_Memset(&defaultState, 0, sizeof(defaultState));
    defaultState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    // Temporarily disable culling to validate geometry is correct
    defaultState.cullMode = VK_CULL_MODE_NONE; // VK_CULL_MODE_BACK_BIT;
    defaultState.depthTestEnable = VK_TRUE;
    defaultState.depthWriteEnable = VK_TRUE;
    // Depth compare op must match reversed depth mode
#ifdef USE_REVERSED_DEPTH
    defaultState.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
#else
    defaultState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
#endif
    defaultState.blendEnable = VK_FALSE;
    defaultState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    defaultState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    defaultState.colorBlendOp = VK_BLEND_OP_ADD;
    defaultState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    defaultState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    defaultState.alphaBlendOp = VK_BLEND_OP_ADD;
    
    vk_uberPipeline = VK_CreateUberPipeline("uber_main", &defaultState);
    
    // Create post-process pipeline
    defaultState.depthTestEnable = VK_FALSE;
    defaultState.depthWriteEnable = VK_FALSE;
    vk_postProcessPipeline = VK_CreateUberPipeline("uber_post", &defaultState);

    // Initialize uber shader integration
    extern void VK_InitUberIntegration(void);
    VK_InitUberIntegration();

    ri.Printf(PRINT_ALL, "Uber-shader system initialized\n");
}

/*
================
VK_ShutdownUberShaderSystem

Shutdown the uber-shader system
================
*/
void VK_ShutdownUberShaderSystem(void) {
    int i;

    // Shutdown the vertex adapter
    extern void VK_ShutdownUberAdapter(void);
    VK_ShutdownUberAdapter();

    // Destroy pipelines
    if (vk_uberPipeline) {
        VK_DestroyPipeline(vk_uberPipeline);
        vk_uberPipeline = NULL;
    }
    
    if (vk_postProcessPipeline) {
        VK_DestroyPipeline(vk_postProcessPipeline);
        vk_postProcessPipeline = NULL;
    }
    
    if (vk_skyboxPipeline) {
        VK_DestroyPipeline(vk_skyboxPipeline);
        vk_skyboxPipeline = NULL;
    }
    
    // Destroy cached pipelines
    for (i = 0; i < numCachedPipelines; i++) {
        if (pipelineCache[i].pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(vk.device, pipelineCache[i].pipeline, NULL);
        }
        if (pipelineCache[i].layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(vk.device, pipelineCache[i].layout, NULL);
        }
    }
    numCachedPipelines = 0;
    
    // Destroy shader modules
    if (vk_uberVertexShader) {
        VK_DestroyShaderModule(vk_uberVertexShader);
        vk_uberVertexShader = VK_NULL_HANDLE;
    }

    if (vk_uberFragmentShader) {
        VK_DestroyShaderModule(vk_uberFragmentShader);
        vk_uberFragmentShader = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (vk_uberDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk.device, vk_uberDescriptorSetLayout, NULL);
        vk_uberDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

/*
================
VK_CreateUberPipeline

Create a pipeline with the uber-shader
================
*/
vkPipeline_t* VK_CreateUberPipeline(const char *name, vkPipelineState_t *state) {
    VkGraphicsPipelineCreateInfo pipelineInfo;
    VkPipelineShaderStageCreateInfo shaderStages[2];
    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly;
    VkPipelineViewportStateCreateInfo viewportState;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineDepthStencilStateCreateInfo depthStencil;
    VkPipelineColorBlendStateCreateInfo colorBlending;
    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    VkPipelineDynamicStateCreateInfo dynamicState;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo;
    VkPushConstantRange pushConstantRange;
    vkPipeline_t *pipeline;
    VkResult result;
    
    // Vertex input binding
    VkVertexInputBindingDescription vertexBinding = {
        .binding = 0,
        .stride = sizeof(vkVertex_t),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    
    // Vertex input attributes (must match shader input locations)
    VkVertexInputAttributeDescription vertexAttribs[] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vkVertex_t, position) },     // inPosition
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vkVertex_t, texCoord0) },        // inTexCoord
        { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vkVertex_t, texCoord1) },        // inTexCoord2
        { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vkVertex_t, normal) },        // inNormal
        { 4, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(vkVertex_t, color) },           // inColor
        { 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vkVertex_t, tangent) }     // inTangent (w = handedness)
    };
    
    // Dynamic states
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE
    };
    
    // Allocate pipeline structure
    if (numCachedPipelines >= MAX_PIPELINE_CACHE) {
        ri.Error(ERR_FATAL, "Pipeline cache overflow");
    }
    pipeline = &pipelineCache[numCachedPipelines++];
    Com_Memset(pipeline, 0, sizeof(vkPipeline_t));
    Q_strncpyz(pipeline->name, name, sizeof(pipeline->name));
    Com_Memcpy(&pipeline->state, state, sizeof(vkPipelineState_t));
    
    // Shader stages
    Com_Memset(shaderStages, 0, sizeof(shaderStages));
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vk_uberVertexShader;
    shaderStages[0].pName = "main";
    
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = vk_uberFragmentShader;
    shaderStages[1].pName = "main";
    
    // Vertex input
    Com_Memset(&vertexInputInfo, 0, sizeof(vertexInputInfo));
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &vertexBinding;
    vertexInputInfo.vertexAttributeDescriptionCount = ARRAY_LEN(vertexAttribs);
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttribs;
    
    // Input assembly
    Com_Memset(&inputAssembly, 0, sizeof(inputAssembly));
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = state->topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Viewport state
    Com_Memset(&viewportState, 0, sizeof(viewportState));
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // Rasterizer
    Com_Memset(&rasterizer, 0, sizeof(rasterizer));
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = state->cullMode;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;  // Match vk_get_mvp_transform() Y flip
    rasterizer.depthBiasEnable = VK_FALSE;
    
    // Multisampling
    Com_Memset(&multisampling, 0, sizeof(multisampling));
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Depth stencil
    Com_Memset(&depthStencil, 0, sizeof(depthStencil));
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = state->depthTestEnable;
    depthStencil.depthWriteEnable = state->depthWriteEnable;
    depthStencil.depthCompareOp = state->depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    // Color blend attachment
    Com_Memset(&colorBlendAttachment, 0, sizeof(colorBlendAttachment));
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | 
                                         VK_COLOR_COMPONENT_G_BIT | 
                                         VK_COLOR_COMPONENT_B_BIT | 
                                         VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = state->blendEnable;
    colorBlendAttachment.srcColorBlendFactor = state->srcColorBlendFactor;
    colorBlendAttachment.dstColorBlendFactor = state->dstColorBlendFactor;
    colorBlendAttachment.colorBlendOp = state->colorBlendOp;
    colorBlendAttachment.srcAlphaBlendFactor = state->srcAlphaBlendFactor;
    colorBlendAttachment.dstAlphaBlendFactor = state->dstAlphaBlendFactor;
    colorBlendAttachment.alphaBlendOp = state->alphaBlendOp;
    
    // Color blending
    Com_Memset(&colorBlending, 0, sizeof(colorBlending));
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Dynamic state
    Com_Memset(&dynamicState, 0, sizeof(dynamicState));
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = ARRAY_LEN(dynamicStates);
    dynamicState.pDynamicStates = dynamicStates;
    
    // Push constant range
    Com_Memset(&pushConstantRange, 0, sizeof(pushConstantRange));
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(vkPushConstants_t);
    
    // Pipeline layout
    VkDescriptorSetLayout descriptorSetLayouts[1];
    uint32_t numDescriptorSetLayouts = 0;

    // Use the uber shader descriptor set layout
    descriptorSetLayouts[numDescriptorSetLayouts++] = vk_uberDescriptorSetLayout;
    
    Com_Memset(&pipelineLayoutInfo, 0, sizeof(pipelineLayoutInfo));
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = numDescriptorSetLayouts;
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    result = vkCreatePipelineLayout(vk.device, &pipelineLayoutInfo, NULL, &pipeline->layout);
    if (result != VK_SUCCESS) {
        ri.Error(ERR_FATAL, "Failed to create pipeline layout: %d", result);
    }
    
    // Create graphics pipeline
    Com_Memset(&pipelineInfo, 0, sizeof(pipelineInfo));
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipeline->layout;
    pipelineInfo.renderPass = vk.render_pass.main;
    pipelineInfo.subpass = 0;
    
    result = vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, 
                                      &pipelineInfo, NULL, &pipeline->pipeline);
    if (result != VK_SUCCESS) {
        ri.Error(ERR_FATAL, "Failed to create graphics pipeline: %d", result);
    }
    
    pipeline->renderPass = vk.render_pass.main;
    
    ri.Printf(PRINT_ALL, "Created uber-pipeline: %s\n", name);
    return pipeline;
}

/*
================
VK_DestroyPipeline

Destroy a pipeline
================
*/
void VK_DestroyPipeline(vkPipeline_t *pipeline) {
    if (!pipeline) {
        return;
    }
    
    if (pipeline->pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk.device, pipeline->pipeline, NULL);
        pipeline->pipeline = VK_NULL_HANDLE;
    }
    
    if (pipeline->layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk.device, pipeline->layout, NULL);
        pipeline->layout = VK_NULL_HANDLE;
    }
}

/*
================
VK_BindPipeline

Bind a pipeline to the command buffer
================
*/
void VK_BindPipeline(vkPipeline_t *pipeline) {
    if (!pipeline || !pipeline->pipeline) {
        return;
    }
    
    vkCmdBindPipeline(vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
}

/*
================
VK_LoadShaderModule

Load a SPIR-V shader module
================
*/
VkShaderModule VK_LoadShaderModule(const char *filename, VkShaderStageFlagBits stage) {
    VkShaderModuleCreateInfo createInfo;
    VkShaderModule module;
    void *code;
    int codeSize;
    VkResult result;
    
    // Load SPIR-V file
    codeSize = ri.FS_ReadFile(filename, &code);
    if (codeSize <= 0) {
        ri.Printf(PRINT_WARNING, "Failed to load shader: %s\n", filename);
        return VK_NULL_HANDLE;
    }
    
    // Create shader module
    Com_Memset(&createInfo, 0, sizeof(createInfo));
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = codeSize;
    createInfo.pCode = (const uint32_t*)code;
    
    result = vkCreateShaderModule(vk.device, &createInfo, NULL, &module);
    ri.FS_FreeFile(code);
    
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "Failed to create shader module: %s\n", filename);
        return VK_NULL_HANDLE;
    }
    
    return module;
}

/*
================
VK_DestroyShaderModule

Destroy a shader module
================
*/
void VK_DestroyShaderModule(VkShaderModule module) {
    if (module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vk.device, module, NULL);
    }
}

/*
================
VK_ConfigureUberShader

Configure uber-shader features based on material stage
================
*/
void VK_ConfigureUberShader(const materialStage_t *stage, uberShaderConfig_t *config) {
    Com_Memset(config, 0, sizeof(uberShaderConfig_t));
    
    // Legacy lightmap bundle warning
    if (stage->bundle[1].image[0]) {
        R_ReportLegacyLightmapUsage("VK_ConfigureUberShader bundle1");
    }
    
    if (stage->rgbGen == CGEN_VERTEX) {
        config->features |= FEAT_VERTEX_COLOR;
    }
    
    if (stage->stateBits & GLS_ALPHATEST_ENABLE) {
        config->features |= FEAT_ALPHA_TEST;
    }
    
    if (stage->rgbGen == CGEN_WAVEFORM) {
        // TODO: Add wave support
        // config->features |= FEAT_RGBGEN_WAVE;
    }
    
    if (stage->alphaGen == AGEN_WAVEFORM) {
        // TODO: Add wave support
        // config->features |= FEAT_ALPHAGEN_WAVE;
    }
    
    if (stage->bundle[0].tcGen == TCGEN_ENVIRONMENT_MAPPED) {
        config->features |= FEAT_ENV_MAP;
    }
    
    if (stage->numTexMods > 0) {
        // TODO: Add tcmod support
        // config->features |= FEAT_TCMOD_TRANSFORM;
    }
    
    if (stage->normalMap) {
        config->features |= FEAT_NORMAL_MAP;
    }
    
    if (stage->specularMap) {
        config->features |= FEAT_SPECULAR_MAP;
    }
    
    if (stage->glowMap) {
        // TODO: Add glow map support
        // config->features |= FEAT_GLOWMAP;
    }
    
    if (stage->isDetail) {
        // TODO: Add detail texture support
        // config->features |= FEAT_DETAIL;
    }
    
    // Set texture flags
    if (stage->bundle[0].image[0]) {
        config->textureFlags |= TEXTURE_FLAG_DIFFUSE;
    }
    
    if (stage->bundle[1].image[0]) {
        // Legacy lightmap bundle ignored
    }
    
    if (stage->normalMap) {
        config->textureFlags |= TEXTURE_FLAG_NORMAL;
    }
    
    if (stage->specularMap) {
        config->textureFlags |= TEXTURE_FLAG_SPECULAR;
    }
    
    // Set lighting mode
    config->lightingMode = VK_GetLightingMode(stage);
}

/*
================
VK_GetLightingMode

Determine lighting mode from material stage
================
*/
uint32_t VK_GetLightingMode(const materialStage_t *stage) {
    if (!stage->active) {
        return LIGHTING_NONE;
    }
    
    if (stage->rgbGen == CGEN_IDENTITY) {
        return LIGHTING_IDENTITY;
    }
    
    if (stage->rgbGen == CGEN_VERTEX || stage->rgbGen == CGEN_EXACT_VERTEX) {
        return LIGHTING_VERTEX;
    }
    
    if (stage->rgbGen == CGEN_LIGHTING_DIFFUSE) {
        return LIGHTING_DIFFUSE;
    }
    
    if (stage->lighting == SL_SPECULAR) {
        return LIGHTING_SPECULAR;
    }
    
    return LIGHTING_NONE;
}

/*
================
VK_SetupMaterialPushConstants

Setup push constants for a material stage
================
*/
void VK_SetupMaterialPushConstants(const material_t *material, int stageNum, vkPushConstants_t *pc) {
    const materialStage_t *stage;
    int i;
    float metallic = 0.0f;
    float roughness = 0.6f;
    float ao = 1.0f;
    float emissive = 0.0f;
    vec3_t sunColor;
    vec3_t sunDir;
    float sunIntensity;

    if (!material || stageNum >= material->numStages) {
        return;
    }

    stage = &material->stages[stageNum];
    Com_Memset(pc, 0, sizeof(vkPushConstants_t));

    uberShaderConfig_t config;
    VK_ConfigureUberShader(stage, &config);
    pc->features = config.features | FEAT_PBR_SHADING;
    pc->textureMask = config.textureFlags;

    for (i = 0; i < 4; i++) {
        pc->baseColor[i] = stage->constantColor[i] / 255.0f;
    }

    if (material->specularExponent > 0.0f) {
        float spec = Com_Clamp(1.0f, 256.0f, material->specularExponent);
        roughness = Com_Clamp(0.04f, 1.0f, 1.0f - (spec / 256.0f));
    }

    pc->materialParams[0] = Com_Clamp(0.0f, 1.0f, metallic);
    pc->materialParams[1] = Com_Clamp(0.04f, 1.0f, roughness);
    pc->materialParams[2] = Com_Clamp(0.0f, 2.0f, ao);
    pc->materialParams[3] = Com_Clamp(0.0f, 10.0f, emissive);

    VectorCopy(tr.sunLight, sunColor);
    sunIntensity = VectorLength(sunColor);
    if (sunIntensity <= 0.0001f) {
        sunColor[0] = sunColor[1] = sunColor[2] = 1.0f;
        sunIntensity = 1.0f;
    }
    pc->sunColor[0] = sunColor[0];
    pc->sunColor[1] = sunColor[1];
    pc->sunColor[2] = sunColor[2];
    pc->sunColor[3] = sunIntensity;

    VectorCopy(tr.sunDirection, sunDir);
    if (VectorNormalize(sunDir) == 0.0f) {
        VectorSet(sunDir, 0.0f, 0.0f, -1.0f);
    }
    pc->sunDirection[0] = sunDir[0];
    pc->sunDirection[1] = sunDir[1];
    pc->sunDirection[2] = sunDir[2];
    pc->sunDirection[3] = sunIntensity;

    pc->alphaTestValue = 0.5f;
    VectorCopy(backEnd.refdef.vieworg, pc->cameraPos_time);
    pc->cameraPos_time[3] = backEnd.refdef.floatTime;

    VectorSet(pc->fogColor, 0.0f, 0.0f, 0.0f);
    pc->fogColor[3] = 0.0f;
    pc->fogParams[0] = 0.0f;
    pc->fogParams[1] = 0.0f;
}


// VK_SetupWaveParams removed - integrated directly into VK_SetupMaterialPushConstants

/*
================
VK_UpdatePushConstants

Update push constants in command buffer
================
*/
void VK_UpdatePushConstants(const vkPushConstants_t *pc) {
    vkCmdPushConstants(vk.cmd->command_buffer, 
                      vk_uberPipeline->layout,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, 
                      sizeof(vkPushConstants_t), 
                      pc);
}
