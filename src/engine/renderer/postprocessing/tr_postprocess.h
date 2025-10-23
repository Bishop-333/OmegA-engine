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
// tr_postprocess.h - Post-Processing Framework

#ifndef __TR_POSTPROCESS_H
#define __TR_POSTPROCESS_H

#include "../core/tr_local.h"
#include "tr_taa.h"
#include "tr_ssr.h"
#include "tr_volumetric.h"
#include "tr_hdr.h"
#include "vulkan/vulkan.h"

// Post-process pass types
typedef enum {
    POST_PASS_DEPTH_OF_FIELD = 0,
    POST_PASS_MOTION_BLUR,
    POST_PASS_CHROMATIC_ABERRATION,
    POST_PASS_VIGNETTE,
    POST_PASS_FILM_GRAIN,
    POST_PASS_LENS_FLARE,
    POST_PASS_GOD_RAYS,
    POST_PASS_AMBIENT_OCCLUSION,
    POST_PASS_FOG,
    POST_PASS_SMAA,
    POST_PASS_FXAA,
    POST_PASS_SHARPEN,
    POST_PASS_COUNT
} postPassType_t;

// Post-process pass configuration
typedef struct postPass_s {
    postPassType_t      type;
    qboolean            enabled;
    float               intensity;
    void               *config;
    
    VkPipeline          pipeline;
    VkPipelineLayout    layout;
    VkDescriptorSet     descriptorSet;
} postPass_t;

// Depth of Field configuration
typedef struct dofConfig_s {
    qboolean    enabled;
    float       focusDistance;
    float       focusRange;
    float       nearBlur;
    float       farBlur;
    float       bokehSize;
    uint32_t    bokehSamples;
} dofConfig_t;

// Motion Blur configuration
typedef struct motionBlurConfig_s {
    qboolean    enabled;
    float       velocityScale;
    uint32_t    samples;
    float       maxBlur;
} motionBlurConfig_t;

// Ambient Occlusion configuration
typedef struct aoConfig_s {
    qboolean    enabled;
    float       radius;
    float       intensity;
    float       bias;
    uint32_t    samples;
    float       power;
} aoConfig_t;

// Chromatic Aberration configuration
typedef struct chromaticConfig_s {
    qboolean    enabled;
    float       strength;
    vec3_t      shift;
} chromaticConfig_t;

// Post-processing chain
typedef struct postProcessChain_s {
    postPass_t          passes[POST_PASS_COUNT];
    uint32_t            numActivePasses;
    uint32_t            passOrder[POST_PASS_COUNT];
    
    // Ping-pong buffers
    VkImage             pingImage;
    VkImageView         pingView;
    VkDeviceMemory      pingMemory;
    
    VkImage             pongImage;
    VkImageView         pongView;
    VkDeviceMemory      pongMemory;
    
    qboolean            currentPing;
} postProcessChain_t;

// Post-processing state
typedef struct postProcessState_s {
    qboolean            initialized;
    qboolean            enabled;
    
    postProcessChain_t  chain;
    
    // Pass configurations
    dofConfig_t         dofConfig;
    motionBlurConfig_t  motionBlurConfig;
    aoConfig_t          aoConfig;
    chromaticConfig_t   chromaticConfig;
    
    // Global settings
    float               globalIntensity;
    qboolean            hdrEnabled;
    qboolean            debugMode;
    
    // Resources
    VkDescriptorPool    descriptorPool;
    VkSampler           linearSampler;
    VkSampler           pointSampler;
    
    // Vulkan pipelines and layouts
    VkPipeline          pipelines[POST_PASS_COUNT];
    VkPipelineLayout    pipelineLayouts[POST_PASS_COUNT];
    VkDescriptorSet     descriptorSets[POST_PASS_COUNT];
    
    // Statistics
    float               frameTime;
    uint32_t            passesExecuted;
} postProcessState_t;

// Global post-processing state
extern postProcessState_t postProcessState;

// CVars
extern cvar_t *r_postProcess;
extern cvar_t *r_postProcessDebug;
extern cvar_t *r_dof;
extern cvar_t *r_motionBlur;
extern cvar_t *r_chromaticAberration;
extern cvar_t *r_vignette;
extern cvar_t *r_filmGrain;

// Initialization
qboolean R_InitPostProcess( void );
void R_ShutdownPostProcess( void );

// Chain management
void R_BuildPostProcessChain( void );
void R_ExecutePostProcessChain( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage );

// Pass management
void R_EnablePostPass( postPassType_t type, qboolean enable );
void R_SetPostPassIntensity( postPassType_t type, float intensity );
void R_ConfigurePostPass( postPassType_t type, const void *config );

// Depth of Field
void R_SetDOFConfig( const dofConfig_t *config );
void R_ApplyDepthOfField( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage );
void R_AutoFocus( const vec3_t viewOrigin, const vec3_t viewDir );

// Motion Blur
void R_SetMotionBlurConfig( const motionBlurConfig_t *config );
void R_ApplyMotionBlur( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage );
void R_GenerateVelocityBuffer( VkCommandBuffer cmd );

// Ambient Occlusion
void R_SetAOConfig( const aoConfig_t *config );
void R_ApplyAmbientOcclusion( VkCommandBuffer cmd );
void R_ComputeSSAO( VkCommandBuffer cmd );
void R_ComputeGTAO( VkCommandBuffer cmd );

// Effects
void R_ApplyChromaticAberration( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage );
void R_ApplyVignette( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage );
void R_ApplyFilmGrain( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage );
void R_ApplyLensFlare( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage );
void R_ApplyGodRays( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage );

// Anti-aliasing
void R_ApplySMAA( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage );
void R_ApplyFXAA( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage );

// Sharpening
void R_ApplySharpening( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage, float strength );
void R_ApplyCAS( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage, float sharpness );

// Utility
VkImage R_GetPostProcessSourceImage( void );
VkImage R_GetPostProcessDestImage( void );
void R_SwapPostProcessBuffers( void );

// Debug
void R_DrawPostProcessDebug( void );
void R_ShowPostProcessPasses( void );

// Pipeline management
void R_InitPostProcessPipelines( void );
void R_ShutdownPostProcessPipelines( void );

// Descriptor management
void R_InitPostProcessDescriptors( void );
void R_ShutdownPostProcessDescriptors( void );
void R_EnsurePostProcessBuffers( void );
VkDescriptorSet R_AllocatePostProcessDescriptorSet( VkDescriptorSetLayout layout );
void R_UpdatePostProcessDescriptorSet( VkDescriptorSet descriptorSet, 
                                       VkImageView colorView, 
                                       VkImageView secondaryView,
                                       VkSampler sampler );

#endif // __TR_POSTPROCESS_H