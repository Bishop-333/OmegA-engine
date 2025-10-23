/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Uber Shader Integration
Bridges the existing pipeline system with the uber shader
===========================================================================
*/

#include "../core/tr_local.h"
#include "vk.h"
#include "vk_shader.h"
#include "vk_uber_adapter.h"

// External functions
extern uint32_t vk_find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties);
extern PFN_vkCmdBindDescriptorSets qvkCmdBindDescriptorSets;

// Alignment macro
#ifndef PAD
#define PAD(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#endif

#define MAX_UBER_DESCRIPTOR_SETS 1024

// Cvar to control uber shader usage
static cvar_t *r_useUberShader = NULL;
static cvar_t *r_uberTextureMaps = NULL;
static cvar_t *r_pbrMetallic = NULL;
static cvar_t *r_pbrRoughness = NULL;
static cvar_t *r_pbrAO = NULL;
static cvar_t *r_pbrEmissive = NULL;

// Transform uniform buffer
static VkBuffer transformBuffer = VK_NULL_HANDLE;
static VkDeviceMemory transformMemory = VK_NULL_HANDLE;
static vkTransformUBO_t *transformData = NULL;
static uint32_t transformBufferSize = 0;
static uint32_t transformAlignedSize = 0;  // Aligned size of each transform

// Descriptor pool state per command buffer for uber shader
typedef struct {
    VkDescriptorPool pool;
    uint32_t nextIndex;
    qboolean overflowLogged;
} uberDescriptorState_t;

static uberDescriptorState_t uberDescriptorState[NUM_COMMAND_BUFFERS];

static uberDescriptorState_t *VK_GetActiveUberDescriptorState(void) {
    int cmdIndex = vk.cmd_index;

    if (cmdIndex < 0 || cmdIndex >= NUM_COMMAND_BUFFERS) {
        return NULL;
    }

    return &uberDescriptorState[cmdIndex];
}

static void VK_EnsureUberCvars(void) {
    if (!r_useUberShader) {
        r_useUberShader = ri.Cvar_Get("r_useUberShader", "1", CVAR_ARCHIVE | CVAR_LATCH);
    }
    if (!r_uberTextureMaps) {
        r_uberTextureMaps = ri.Cvar_Get("r_uberTextureMaps", "1", CVAR_ARCHIVE);
    }
    if (!r_pbrMetallic) {
        r_pbrMetallic = ri.Cvar_Get("r_pbrMetallic", "0.0", CVAR_ARCHIVE);
    }
    if (!r_pbrRoughness) {
        r_pbrRoughness = ri.Cvar_Get("r_pbrRoughness", "0.6", CVAR_ARCHIVE);
    }
    if (!r_pbrAO) {
        r_pbrAO = ri.Cvar_Get("r_pbrAO", "1.0", CVAR_ARCHIVE);
    }
    if (!r_pbrEmissive) {
        r_pbrEmissive = ri.Cvar_Get("r_pbrEmissive", "0.0", CVAR_ARCHIVE);
    }
}

static qboolean VK_TexturesEnabled(void) {
    VK_EnsureUberCvars();
    return (r_uberTextureMaps == NULL) ? qtrue : (r_uberTextureMaps->integer != 0);
}


// Default cube map for environment mapping
static VkImage defaultCubeMap = VK_NULL_HANDLE;
static VkImageView defaultCubeMapView = VK_NULL_HANDLE;
static VkDeviceMemory defaultCubeMapMemory = VK_NULL_HANDLE;

// External uber shader descriptor set layout
extern VkDescriptorSetLayout vk_uberDescriptorSetLayout;

// Current stage information supplied by the fixed pipeline iterator
static const shaderStage_t *uberCurrentStage = NULL;

// Forward declarations
static void VK_EnsureUberDescriptorSet(void);
static void VK_CreateDefaultCubeMap(void);
static image_t *VK_SelectBundleImage(const textureBundle_t *bundle);
static void ConvertPipelineDefToUberConfig(const Vk_Pipeline_Def *def, uberShaderConfig_t *config);
static uint32_t VK_UpdateUberTexturesForStage(const shaderStage_t *stage, VkDescriptorSet descriptorSet) {
    VkDescriptorImageInfo imageInfos[5];
    VkWriteDescriptorSet descriptorWrites[6];
    VkDescriptorBufferInfo bufferInfo;
    image_t *defaultImage = tr.whiteImage ? tr.whiteImage : tr.defaultImage;
    image_t *diffuse = defaultImage;
    image_t *lightmap = defaultImage;
    image_t *normal = defaultImage;
    image_t *specular = defaultImage;
    VkImageView envView;
    uint32_t textureMask = 0;
    int writeCount = 0;
    qboolean texturesAllowed = VK_TexturesEnabled();

    if (descriptorSet == VK_NULL_HANDLE) {
        return 0;
    }

    if (!defaultImage || defaultImage->view == VK_NULL_HANDLE || transformBuffer == VK_NULL_HANDLE) {
        return 0;
    }

    uint32_t alignedSize = transformAlignedSize;
    if (alignedSize == 0) {
        alignedSize = PAD(sizeof(vkTransformUBO_t), vk.uniform_alignment);
    }

    bufferInfo.buffer = transformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = alignedSize;

    Com_Memset(&descriptorWrites[writeCount], 0, sizeof(descriptorWrites[writeCount]));
    descriptorWrites[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[writeCount].dstSet = descriptorSet;
    descriptorWrites[writeCount].dstBinding = 0;
    descriptorWrites[writeCount].dstArrayElement = 0;
    descriptorWrites[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descriptorWrites[writeCount].descriptorCount = 1;
    descriptorWrites[writeCount].pBufferInfo = &bufferInfo;
    writeCount++;

    if (stage && texturesAllowed) {
        if (stage->numTexBundles > 0) {
            image_t *img = VK_SelectBundleImage(&stage->bundle[0]);
            if (img && img->view != VK_NULL_HANDLE) {
                diffuse = img;
                textureMask |= TEXTURE_FLAG_DIFFUSE;
            }
        }

        if (stage->numTexBundles > 1) {
            image_t *img = VK_SelectBundleImage(&stage->bundle[1]);
            if (img && img->view != VK_NULL_HANDLE) {
                R_ReportLegacyLightmapUsage("VK_UberIntegration bundle1");
            }
        }

        if (stage->numTexBundles > 2) {
            image_t *img = VK_SelectBundleImage(&stage->bundle[2]);
            if (img && img->view != VK_NULL_HANDLE) {
                normal = img;
                specular = img;
                textureMask |= TEXTURE_FLAG_NORMAL;
                textureMask |= TEXTURE_FLAG_SPECULAR;
            }
        }

        if ((stage->tessFlags & TESS_ENV) || stage->bundle[0].tcGen == TCGEN_ENVIRONMENT_MAPPED) {
            textureMask |= TEXTURE_FLAG_ENVIRONMENT;
        }
    }

    envView = (defaultCubeMapView != VK_NULL_HANDLE) ? defaultCubeMapView : defaultImage->view;

    imageInfos[0].sampler = vk.samplers.handle[0];
    imageInfos[0].imageView = diffuse ? diffuse->view : defaultImage->view;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[1].sampler = vk.samplers.handle[0];
    imageInfos[1].imageView = defaultImage->view;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[2].sampler = vk.samplers.handle[0];
    imageInfos[2].imageView = normal ? normal->view : defaultImage->view;
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[3].sampler = vk.samplers.handle[0];
    imageInfos[3].imageView = specular ? specular->view : defaultImage->view;
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[4].sampler = vk.samplers.handle[0];
    imageInfos[4].imageView = envView;
    imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    for (int i = 0; i < 5; i++) {
        if (imageInfos[i].imageView == VK_NULL_HANDLE) {
            imageInfos[i].imageView = defaultImage->view;
        }

        Com_Memset(&descriptorWrites[writeCount], 0, sizeof(descriptorWrites[writeCount]));
        descriptorWrites[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[writeCount].dstSet = descriptorSet;
        descriptorWrites[writeCount].dstBinding = i + 1;
        descriptorWrites[writeCount].dstArrayElement = 0;
        descriptorWrites[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[writeCount].descriptorCount = 1;
        descriptorWrites[writeCount].pImageInfo = &imageInfos[i];
        writeCount++;
    }

    if (writeCount > 0) {
        vkUpdateDescriptorSets(vk.device, writeCount, descriptorWrites, 0, NULL);
    }

    return texturesAllowed ? textureMask : 0;
}

/*
================
VK_UpdateUberTextures

Update texture descriptors for uber shader
================
*/

static image_t *VK_SelectBundleImage(const textureBundle_t *bundle) {
    if (!bundle) {
        return NULL;
    }

    if (bundle->isVideoMap || bundle->isScreenMap) {
        return NULL; // rely on fallback for these special cases
    }

    if (bundle->numImageAnimations <= 1) {
        return bundle->image[0];
    }

    double frame = tess.shaderTime * bundle->imageAnimationSpeed;
    int index = (int)frame;
    if (index < 0) {
        index = 0;
    }
    if (bundle->numImageAnimations > 0) {
        index %= bundle->numImageAnimations;
    }
    return bundle->image[index];
}
static void ConvertPipelineDefToUberConfig(const Vk_Pipeline_Def *def, uberShaderConfig_t *config) {
    uint32_t features = FEAT_DIFFUSE_MAP;

    if (!config) {
        return;
    }

    Com_Memset(config, 0, sizeof(*config));
    config->lightingMode = LIGHTING_NONE;

    if (!def) {
        config->features = features;
        return;
    }

    if (def->allow_discard || (def->state_bits & GLS_ATEST_BITS)) {
        features |= FEAT_ALPHA_TEST;
    }

    if (def->fog_stage != 0) {
        features |= FEAT_FOG;
    }

    if (def->abs_light > 0) {
        config->lightingMode = LIGHTING_DIFFUSE;
    }

    config->features = features;
    config->textureFlags = 0;
    config->reserved = 0;
}
/*
================
VK_UseUberShader

Use uber shader instead of creating a new pipeline
Returns qtrue if uber shader was used
================
*/
qboolean VK_UseUberShader(const Vk_Pipeline_Def *def) {
    vkPushConstants_t pc;
    uberShaderConfig_t config;
    static uint32_t currentTransformIndex = 0;
    uint32_t vertexOffset;
    uint32_t convertedVertexCount;
    uint32_t textureMask = 0;

    if (!vk_uberPipeline || !vk_uberPipeline->pipeline || !transformData || transformAlignedSize == 0) {
        return qfalse;
    }

    if (backEnd.projection2D) {
        return qfalse;
    }

    if (!uberCurrentStage) {
        return qfalse;
    }

    VK_EnsureUberCvars();
    VK_EnsureUberDescriptorSet();

    uberDescriptorState_t *state = VK_GetActiveUberDescriptorState();
    if (!state || state->pool == VK_NULL_HANDLE) {
        return qfalse;
    }

    if (state->nextIndex >= MAX_UBER_DESCRIPTOR_SETS) {
        if (!state->overflowLogged) {
            ri.Printf(PRINT_WARNING, "Uber shader descriptor exhaustion, falling back to classic pipeline\n");
            state->overflowLogged = qtrue;
        }
        return qfalse;
    }

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocInfo;
    Com_Memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = state->pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &vk_uberDescriptorSetLayout;

    VkResult allocResult = vkAllocateDescriptorSets(vk.device, &allocInfo, &descriptorSet);
    if (allocResult != VK_SUCCESS || descriptorSet == VK_NULL_HANDLE) {
        if (!state->overflowLogged) {
            ri.Printf(PRINT_WARNING, "Uber shader descriptor allocation failed (%d)\n", allocResult);
            state->overflowLogged = qtrue;
        }
        return qfalse;
    }

    state->nextIndex++;

    textureMask = VK_UpdateUberTexturesForStage(uberCurrentStage, descriptorSet);

    ConvertPipelineDefToUberConfig(def, &config);

    if (uberCurrentStage) {
        if (uberCurrentStage->numTexBundles > 1) {
            const textureBundle_t *lmBundle = &uberCurrentStage->bundle[1];
            if (lmBundle->image[0] && lmBundle->lightmap != LIGHTMAP_INDEX_NONE) {
                R_ReportLegacyLightmapUsage("VK_Uber stage lightmap");
            }
        }

        if (uberCurrentStage->numTexBundles > 2) {
            const textureBundle_t *extraBundle = &uberCurrentStage->bundle[2];
            if (extraBundle->image[0]) {
                config.features |= FEAT_NORMAL_MAP;
                config.features |= FEAT_SPECULAR_MAP;
            }
        }

        if (uberCurrentStage->tessFlags & TESS_ENV) {
            config.features |= FEAT_ENV_MAP;
        }

        if (uberCurrentStage->tessFlags & TESS_RGBA0) {
            config.features |= FEAT_VERTEX_COLOR;
        }
    }

    if ((textureMask & TEXTURE_FLAG_DIFFUSE) == 0) {
        config.features &= ~FEAT_DIFFUSE_MAP;
    }
    if ((textureMask & TEXTURE_FLAG_NORMAL) == 0) {
        config.features &= ~FEAT_NORMAL_MAP;
    }
    if ((textureMask & TEXTURE_FLAG_SPECULAR) == 0) {
        config.features &= ~FEAT_SPECULAR_MAP;
    }
    if ((textureMask & TEXTURE_FLAG_ENVIRONMENT) == 0) {
        config.features &= ~FEAT_ENV_MAP;
    }

    config.textureFlags = textureMask;

    Com_Memset(&pc, 0, sizeof(pc));
    pc.features = config.features | FEAT_PBR_SHADING;
    pc.textureMask = textureMask;

    uint32_t maxTransforms = transformBufferSize / transformAlignedSize;
    uint32_t transformIndex = currentTransformIndex++ % maxTransforms;
    uint8_t *bufferBase = (uint8_t*)transformData;
    vkTransformUBO_t *transform = (vkTransformUBO_t*)(bufferBase + (transformIndex * transformAlignedSize));

    vk_get_mvp_transform(transform->mvpMatrix);

    {
        const orientationr_t *or = &backEnd.or;
        float modelMatrix[16];
        float normalMatrix[16];
        int axisIdx;

        modelMatrix[0] = or->axis[0][0];
        modelMatrix[1] = or->axis[0][1];
        modelMatrix[2] = or->axis[0][2];
        modelMatrix[3] = 0.0f;

        modelMatrix[4] = or->axis[1][0];
        modelMatrix[5] = or->axis[1][1];
        modelMatrix[6] = or->axis[1][2];
        modelMatrix[7] = 0.0f;

        modelMatrix[8] = or->axis[2][0];
        modelMatrix[9] = or->axis[2][1];
        modelMatrix[10] = or->axis[2][2];
        modelMatrix[11] = 0.0f;

        modelMatrix[12] = or->origin[0];
        modelMatrix[13] = or->origin[1];
        modelMatrix[14] = or->origin[2];
        modelMatrix[15] = 1.0f;

        Com_Memcpy(transform->modelMatrix, modelMatrix, sizeof(modelMatrix));

        Com_Memcpy(normalMatrix, modelMatrix, sizeof(modelMatrix));
        normalMatrix[3] = normalMatrix[7] = normalMatrix[11] = 0.0f;
        normalMatrix[12] = normalMatrix[13] = normalMatrix[14] = 0.0f;
        normalMatrix[15] = 1.0f;

        if (backEnd.currentEntity && backEnd.currentEntity->e.nonNormalizedAxes) {
            for (axisIdx = 0; axisIdx < 3; axisIdx++) {
                float len = VectorLength(or->axis[axisIdx]);
                if (len > 0.0f) {
                    float invLen = 1.0f / len;
                    int base = axisIdx * 4;
                    normalMatrix[base + 0] *= invLen;
                    normalMatrix[base + 1] *= invLen;
                    normalMatrix[base + 2] *= invLen;
                }
            }
        }

        Com_Memcpy(transform->normalMatrix, normalMatrix, sizeof(normalMatrix));
    }

    pc.transformIndex = transformIndex;

    if (def->color.rgb == 0 && def->color.alpha == 0) {
        pc.baseColor[0] = 1.0f;
        pc.baseColor[1] = 1.0f;
        pc.baseColor[2] = 1.0f;
        pc.baseColor[3] = 1.0f;
    } else {
        float colorValue = def->color.rgb / 255.0f;
        pc.baseColor[0] = colorValue;
        pc.baseColor[1] = colorValue;
        pc.baseColor[2] = colorValue;
        pc.baseColor[3] = def->color.alpha / 255.0f;
    }

    VectorCopy(backEnd.refdef.vieworg, pc.cameraPos_time);
    pc.cameraPos_time[3] = backEnd.refdef.floatTime;

    {
        float metallic = r_pbrMetallic ? r_pbrMetallic->value : 0.0f;
        float roughness = r_pbrRoughness ? r_pbrRoughness->value : 0.6f;
        float ao = r_pbrAO ? r_pbrAO->value : 1.0f;
        float emissive = r_pbrEmissive ? r_pbrEmissive->value : 0.0f;

        pc.materialParams[0] = Com_Clamp(0.0f, 1.0f, metallic);
        pc.materialParams[1] = Com_Clamp(0.04f, 1.0f, roughness);
        pc.materialParams[2] = Com_Clamp(0.0f, 2.0f, ao);
        pc.materialParams[3] = Com_Clamp(0.0f, 10.0f, emissive);
    }

    {
        vec3_t sunColor;
        vec3_t sunDir;
        float sunIntensity;
        float dirLen;

        VectorCopy(tr.sunLight, sunColor);
        sunIntensity = VectorLength(sunColor);
        if (sunIntensity <= 0.0001f) {
            sunColor[0] = sunColor[1] = sunColor[2] = 1.0f;
            sunIntensity = 1.0f;
        }
        pc.sunColor[0] = sunColor[0];
        pc.sunColor[1] = sunColor[1];
        pc.sunColor[2] = sunColor[2];
        pc.sunColor[3] = sunIntensity;

        VectorCopy(tr.sunDirection, sunDir);
        dirLen = VectorNormalize(sunDir);
        if (dirLen <= 0.0f) {
            VectorSet(sunDir, 0.0f, 0.0f, -1.0f);
        }
        pc.sunDirection[0] = sunDir[0];
        pc.sunDirection[1] = sunDir[1];
        pc.sunDirection[2] = sunDir[2];
        pc.sunDirection[3] = sunIntensity;
    }

    VectorSet(pc.fogColor, 0.0f, 0.0f, 0.0f);
    pc.fogColor[3] = 0.0f;
    pc.fogParams[0] = 0.0f;
    pc.fogParams[1] = 0.0f;
    if (def->fog_stage) {
        VectorSet(pc.fogColor, 0.5f, 0.5f, 0.5f);
        pc.fogColor[3] = 1.0f;
        pc.fogParams[0] = 0.01f;
        pc.fogParams[1] = 1000.0f;
    }

    pc.alphaTestValue = 0.5f;

    VkPipeline pipeline = vk_uberPipeline->pipeline;
    if (pipeline != vk.cmd->last_pipeline) {
        qvkCmdBindPipeline(vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vk.cmd->last_pipeline = pipeline;
    }

    vk.cmd->uber_shader_active = qtrue;

    vkCmdPushConstants(vk.cmd->command_buffer,
                      vk_uberPipeline->layout,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0,
                      sizeof(vkPushConstants_t),
                      &pc);

    uint32_t uniformOffset = transformIndex * transformAlignedSize;
    qvkCmdBindDescriptorSets(vk.cmd->command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_uberPipeline->layout,
                            0,
                            1,
                            &descriptorSet,
                            1,
                            &uniformOffset);

    vertexOffset = VK_ConvertVerticesForUberShader(tess.numIndexes, &convertedVertexCount);
    if (vertexOffset == 0 || convertedVertexCount == 0) {
        vk.cmd->uber_shader_active = qfalse;
        return qfalse;
    }

    VK_BindUberVertexBuffer(vk.cmd->command_buffer, vertexOffset);
    vk.cmd->uber_vertex_count = convertedVertexCount;
    vk_world.dirty_depth_attachment |= (def->state_bits & GLS_DEPTHMASK_TRUE);

    return qtrue;
}

void VK_SetUberStage(const shaderStage_t *stage, int stageNum) {
    (void)stageNum;
    uberCurrentStage = stage;
}

void VK_ResetUberDescriptors(void) {
    uberDescriptorState_t *state = VK_GetActiveUberDescriptorState();

    if (state && state->pool != VK_NULL_HANDLE) {
        vk_reset_descriptor_pool(state->pool);
    }

    if (state) {
        state->nextIndex = 0;
        state->overflowLogged = qfalse;
    }

    uberCurrentStage = NULL;
}
/*
================
VK_ShouldUseUberShader

Determine if we should use uber shader for this pipeline
================
*/
qboolean VK_ShouldUseUberShader(void) {
    VK_EnsureUberCvars();

    return (r_useUberShader && r_useUberShader->integer &&
            vk_uberPipeline != NULL &&
            vk_uberPipeline->pipeline != VK_NULL_HANDLE);
}

/*
================
VK_InitUberIntegration

Initialize uber shader integration
Called from VK_InitUberShaderSystem
================
*/
/*
================
VK_CreateDefaultCubeMap

Create a simple 1x1 white cube map for default environment mapping
================
*/
static void VK_CreateDefaultCubeMap(void) {
    VkImageCreateInfo imageInfo;
    VkMemoryRequirements memRequirements;
    VkMemoryAllocateInfo allocInfo;
    VkImageViewCreateInfo viewInfo;
    VkResult result;

    if (defaultCubeMapView != VK_NULL_HANDLE) {
        return; // Already created
    }

    // Create a 1x1 cube map image
    Com_Memset(&imageInfo, 0, sizeof(imageInfo));
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = 1;
    imageInfo.extent.height = 1;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6;  // 6 faces for cube map
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    result = vkCreateImage(vk.device, &imageInfo, NULL, &defaultCubeMap);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "Failed to create default cube map image: %d\n", result);
        return;
    }

    // Allocate memory for the image
    vkGetImageMemoryRequirements(vk.device, defaultCubeMap, &memRequirements);

    Com_Memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vk_find_memory_type(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    result = vkAllocateMemory(vk.device, &allocInfo, NULL, &defaultCubeMapMemory);
    if (result != VK_SUCCESS) {
        vkDestroyImage(vk.device, defaultCubeMap, NULL);
        defaultCubeMap = VK_NULL_HANDLE;
        ri.Printf(PRINT_WARNING, "Failed to allocate default cube map memory: %d\n", result);
        return;
    }

    vkBindImageMemory(vk.device, defaultCubeMap, defaultCubeMapMemory, 0);

    // Create cube map view
    Com_Memset(&viewInfo, 0, sizeof(viewInfo));
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = defaultCubeMap;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    result = vkCreateImageView(vk.device, &viewInfo, NULL, &defaultCubeMapView);
    if (result != VK_SUCCESS) {
        vkFreeMemory(vk.device, defaultCubeMapMemory, NULL);
        vkDestroyImage(vk.device, defaultCubeMap, NULL);
        defaultCubeMap = VK_NULL_HANDLE;
        defaultCubeMapMemory = VK_NULL_HANDLE;
        ri.Printf(PRINT_WARNING, "Failed to create default cube map view: %d\n", result);
        return;
    }

    // Transition the image layout to shader read-only optimal
    VkCommandBuffer cmdBuffer = vk_begin_one_time_commands();

    VkImageMemoryBarrier barrier;
    Com_Memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = defaultCubeMap;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );

    vk_end_one_time_commands(cmdBuffer);
}

/*
================
VK_CreateUberDescriptorPool

Create descriptor pool for uber shader
================
*/
static void VK_CreateUberDescriptorPool(uberDescriptorState_t *state) {
    VkDescriptorPoolSize poolSizes[2];
    VkDescriptorPoolCreateInfo poolInfo;
    VkResult result;

    if (!state) {
        return;
    }

    // Dynamic uniform buffer descriptors
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSizes[0].descriptorCount = MAX_UBER_DESCRIPTOR_SETS;

    // Sampler descriptors
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = MAX_UBER_DESCRIPTOR_SETS * 5;  // diffuse, lightmap, normal, specular, environment

    Com_Memset(&poolInfo, 0, sizeof(poolInfo));
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = MAX_UBER_DESCRIPTOR_SETS;

    result = vkCreateDescriptorPool(vk.device, &poolInfo, NULL, &state->pool);
    if (result != VK_SUCCESS) {
        state->pool = VK_NULL_HANDLE;
        ri.Error(ERR_FATAL, "Failed to create uber descriptor pool: %d", result);
    }
}

/*
================
VK_EnsureUberDescriptorSet

Ensure uber descriptor pool is created and required resources initialized
================
*/
static void VK_EnsureUberDescriptorSet(void) {
    image_t *defaultImage = tr.whiteImage ? tr.whiteImage : tr.defaultImage;
    if (!defaultImage || defaultImage->view == VK_NULL_HANDLE) {
        return;
    }

    uberDescriptorState_t *state = VK_GetActiveUberDescriptorState();
    if (!state) {
        return;
    }

    if (state->pool != VK_NULL_HANDLE) {
        return;
    }

    VK_CreateUberDescriptorPool(state);
    VK_CreateDefaultCubeMap();
    state->nextIndex = 0;
    state->overflowLogged = qfalse;
}

void VK_InitUberIntegration(void) {
    VkBufferCreateInfo bufferInfo;
    VkMemoryRequirements memRequirements;
    VkMemoryAllocateInfo allocInfo;
    VkResult result;

    // Enable uber shader now that we have the adapter
    r_useUberShader = ri.Cvar_Get("r_useUberShader", "1", CVAR_ARCHIVE | CVAR_LATCH);

    if (!r_useUberShader->integer) {
        ri.Printf(PRINT_ALL, "Uber shader integration disabled (using traditional pipelines)\n");
        return;
    }

    Com_Memset(uberDescriptorState, 0, sizeof(uberDescriptorState));

    // Initialize the vertex adapter system
    VK_InitUberAdapter();

    // Create uniform buffer for transform matrices
    // Align each transform to minUniformBufferOffsetAlignment
    transformAlignedSize = PAD(sizeof(vkTransformUBO_t), vk.uniform_alignment);

    // Device max uniform buffer is 64KB, calculate how many aligned transforms we can fit
    uint32_t maxTransforms = 65536 / transformAlignedSize;
    if (maxTransforms > 256) maxTransforms = 256;  // Cap at 256

    transformBufferSize = transformAlignedSize * maxTransforms;

    Com_Memset(&bufferInfo, 0, sizeof(bufferInfo));
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = transformBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    result = vkCreateBuffer(vk.device, &bufferInfo, NULL, &transformBuffer);
    if (result != VK_SUCCESS) {
        ri.Error(ERR_FATAL, "Failed to create transform uniform buffer: %d", result);
    }

    // Allocate memory for buffer
    vkGetBufferMemoryRequirements(vk.device, transformBuffer, &memRequirements);

    Com_Memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vk_find_memory_type(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    result = vkAllocateMemory(vk.device, &allocInfo, NULL, &transformMemory);
    if (result != VK_SUCCESS) {
        ri.Error(ERR_FATAL, "Failed to allocate transform buffer memory: %d", result);
    }

    vkBindBufferMemory(vk.device, transformBuffer, transformMemory, 0);

    // Map memory for CPU access
    vkMapMemory(vk.device, transformMemory, 0, transformBufferSize, 0, (void**)&transformData);

    // Defer descriptor set creation until we have valid images
    // They will be created on first use in VK_UseUberShader

    ri.Printf(PRINT_ALL, "Uber shader integration enabled (UBO size: %d KB)\n", transformBufferSize / 1024);
}



























