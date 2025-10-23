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

#ifndef VK_SHADER_H
#define VK_SHADER_H

/*
================================================================================
Vulkan Uber-Shader System

This file defines the unified shader pipeline that interprets material data
dynamically, eliminating the need for multiple pipeline permutations.
================================================================================
*/

// Uber-shader configuration passed via push constants
typedef struct uberShaderConfig_s {
    // Feature flags (32 bits)
    uint32_t    features;
    
    // Texture binding info
    uint32_t    textureFlags;
    
    // Lighting configuration
    uint32_t    lightingMode;
    
    // Reserved for future use
    uint32_t    reserved;
} uberShaderConfig_t;

// Feature flag bits
#define UBER_FEATURE_LIGHTMAP           0x00000001
#define UBER_FEATURE_VERTEX_COLOR       0x00000002
#define UBER_FEATURE_ALPHA_TEST         0x00000004
#define UBER_FEATURE_RGBGEN_WAVE        0x00000008
#define UBER_FEATURE_ALPHAGEN_WAVE      0x00000010
#define UBER_FEATURE_TCGEN_ENVIRONMENT  0x00000020
#define UBER_FEATURE_TCMOD_TRANSFORM    0x00000040
#define UBER_FEATURE_TCMOD_TURB         0x00000080
#define UBER_FEATURE_FOG                0x00000100
#define UBER_FEATURE_NORMALMAP          0x00000200
#define UBER_FEATURE_SPECULARMAP        0x00000400
#define UBER_FEATURE_GLOWMAP            0x00000800
#define UBER_FEATURE_DETAIL             0x00001000
#define UBER_FEATURE_MULTITEXTURE       0x00002000
#define UBER_FEATURE_PORTAL             0x00004000
#define UBER_FEATURE_ENVIRONMENT_MAP    0x00008000

// PBR feature flag aliases
#define FEAT_VERTEX_COLOR  UBER_FEATURE_VERTEX_COLOR
#define FEAT_ALPHA_TEST    UBER_FEATURE_ALPHA_TEST
#define FEAT_FOG           UBER_FEATURE_FOG
#define FEAT_NORMAL_MAP    UBER_FEATURE_NORMALMAP
#define FEAT_SPECULAR_MAP  UBER_FEATURE_SPECULARMAP
#define FEAT_ENV_MAP       UBER_FEATURE_ENVIRONMENT_MAP
#define FEAT_DIFFUSE_MAP   0x00010000u
#define FEAT_BLOOM         0x00020000u
#define FEAT_Y_FLIP_POS    0x00040000u
#define FEAT_PBR_SHADING   0x00080000u

// Texture flag bits
#define TEXTURE_FLAG_DIFFUSE            0x00000001
#define TEXTURE_FLAG_NORMAL             0x00000002
#define TEXTURE_FLAG_SPECULAR           0x00000004
#define TEXTURE_FLAG_GLOW               0x00000008
#define TEXTURE_FLAG_DETAIL             0x00000010
#define TEXTURE_FLAG_ENVIRONMENT        0x00000020

// Lighting modes
typedef enum {
    LIGHTING_NONE = 0,
    LIGHTING_IDENTITY = 1,
    LIGHTING_VERTEX = 2,
    LIGHTING_DIFFUSE = 3,
    LIGHTING_SPECULAR = 4,
    LIGHTING_PBR = 5  // Reserved for future
} uberLightingMode_t;

// Uniform buffer for transform matrices (192 bytes)
typedef struct vkTransformUBO_s {
    float               mvpMatrix[16];      // Model-View-Projection
    float               modelMatrix[16];    // Model to World
    float               normalMatrix[16];   // Normal transformation
} vkTransformUBO_t;

// Push constant structure - must match shader layout
typedef struct vkPushConstants_s {
    // Feature/state flags
    uint32_t            features;         // Bitmask of FEAT_* flags
    uint32_t            textureMask;      // Bound texture mask (TEXTURE_FLAG_*)
    uint32_t            transformIndex;   // Dynamic UBO offset index
    uint32_t            _pcPad0;          // Padding for alignment

    // Core material parameters
    float               baseColor[4];     // Fallback/override color
    float               sunColor[4];      // Directional light color (rgb) and intensity (w)
    float               fogColor[4];      // Fog RGB (+A unused)
    float               cameraPos_time[4];// xyz = camera world pos, w = currentTime
    float               materialParams[4];// x=metallic, y=roughness, z=ao, w=emissive intensity
    float               sunDirection[4];  // xyz = sun direction, w = intensity multiplier

    // Scalar parameters
    float               fogParams[2];     // x = density or start, y = end
    float               alphaTestValue;   // Alpha test threshold (0..1)
    float               _pcPad1;          // Padding to 16-byte alignment
} vkPushConstants_t;

// Vertex format for unified pipeline
typedef struct vkVertex_s {
    vec3_t      position;
    vec2_t      texCoord0;
    vec2_t      texCoord1;     // Lightmap coords
    vec3_t      normal;
    vec4_t      tangent;        // xyz = tangent, w = handedness
    byte        color[4];       // RGBA vertex color
} vkVertex_t;

// Pipeline state configuration
typedef struct vkPipelineState_s {
    VkPrimitiveTopology     topology;
    VkCullModeFlags         cullMode;
    VkBool32                depthTestEnable;
    VkBool32                depthWriteEnable;
    VkCompareOp             depthCompareOp;
    VkBool32                blendEnable;
    VkBlendFactor           srcColorBlendFactor;
    VkBlendFactor           dstColorBlendFactor;
    VkBlendOp               colorBlendOp;
    VkBlendFactor           srcAlphaBlendFactor;
    VkBlendFactor           dstAlphaBlendFactor;
    VkBlendOp               alphaBlendOp;
    VkBool32                alphaTestEnable;
    VkCompareOp             alphaTestFunc;
    float                   alphaTestRef;
} vkPipelineState_t;

// Pipeline object
typedef struct vkPipeline_s {
    VkPipeline              pipeline;
    VkPipelineLayout        layout;
    VkRenderPass            renderPass;
    vkPipelineState_t       state;
    char                    name[64];
} vkPipeline_t;

// Shader module management
typedef struct vkShaderModule_s {
    VkShaderModule          module;
    VkShaderStageFlagBits   stage;
    char                    name[64];
} vkShaderModule_t;

// Function declarations
void VK_InitUberShaderSystem(void);
void VK_ShutdownUberShaderSystem(void);
void VK_SetUberStage(const shaderStage_t *stage, int stageNum);
void VK_ResetUberDescriptors(void);

// Pipeline management
vkPipeline_t* VK_CreateUberPipeline(const char *name, vkPipelineState_t *state);
void VK_DestroyPipeline(vkPipeline_t *pipeline);
void VK_BindPipeline(vkPipeline_t *pipeline);

// Shader module loading
VkShaderModule VK_LoadShaderModule(const char *filename, VkShaderStageFlagBits stage);
void VK_DestroyShaderModule(VkShaderModule module);

// Push constant updates
void VK_SetupMaterialPushConstants(const material_t *material, int stageNum, vkPushConstants_t *pc);
void VK_UpdatePushConstants(const vkPushConstants_t *pc);

// State configuration from material
void VK_ConfigureUberShader(const materialStage_t *stage, uberShaderConfig_t *config);
uint32_t VK_GetLightingMode(const materialStage_t *stage);

// Texture coordinate generation
void VK_SetupTexCoordGen(texCoordGen_t tcGen, vec3_t position, vec3_t normal, vec2_t *texCoords);
void VK_ApplyTexMods(const materialStage_t *stage, vec2_t *texCoords, float time);

// Wave form generation
float VK_EvaluateWaveForm(const waveForm_t *wave, float time);
void VK_SetupWaveParams(const waveForm_t *wave, vec4_t params);

// Global pipelines
extern vkPipeline_t *vk_uberPipeline;          // Main uber-shader pipeline
extern vkPipeline_t *vk_postProcessPipeline;   // Post-processing pipeline
extern vkPipeline_t *vk_skyboxPipeline;        // Skybox rendering pipeline

// Shader modules
extern VkShaderModule vk_uberVertexShader;
extern VkShaderModule vk_uberFragmentShader;

// Uber shader integration
qboolean VK_UseUberShader(const Vk_Pipeline_Def *def);
qboolean VK_ShouldUseUberShader(void);

#endif // VK_SHADER_H









