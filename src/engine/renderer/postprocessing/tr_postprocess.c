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
// tr_postprocess.c - Post-Processing Framework implementation

#include "tr_postprocess.h"
#include "../vulkan/vk.h"
#include "../pathtracing/rt_rtx.h"

// Global post-processing state
postProcessState_t postProcessState;

// External CVars from tr_volumetric.c
extern cvar_t *r_volumetric;

// CVars
cvar_t *r_postProcess;
cvar_t *r_postProcessDebug;
cvar_t *r_dof;
cvar_t *r_motionBlur;
cvar_t *r_chromaticAberration;
cvar_t *r_vignette;
cvar_t *r_filmGrain;
cvar_t *r_ao;
cvar_t *r_aoType;

/*
================
R_InitPostProcess

Initialize post-processing framework
================
*/
qboolean R_InitPostProcess( void ) {
    Com_Memset( &postProcessState, 0, sizeof( postProcessState ) );
    
    // Register CVars - Default r_postProcess to 1 to enable the system
    r_postProcess = ri.Cvar_Get( "r_postProcess", "1", CVAR_ARCHIVE | CVAR_LATCH );
    r_postProcessDebug = ri.Cvar_Get( "r_postProcessDebug", "0", CVAR_CHEAT );
    r_dof = ri.Cvar_Get( "r_dof", "0", CVAR_ARCHIVE );
    r_motionBlur = ri.Cvar_Get( "r_motionBlur", "0", CVAR_ARCHIVE );
    r_chromaticAberration = ri.Cvar_Get( "r_chromaticAberration", "0", CVAR_ARCHIVE );
    r_vignette = ri.Cvar_Get( "r_vignette", "0", CVAR_ARCHIVE );
    r_filmGrain = ri.Cvar_Get( "r_filmGrain", "0", CVAR_ARCHIVE );
    r_ao = ri.Cvar_Get( "r_ao", "0", CVAR_ARCHIVE );
    r_aoType = ri.Cvar_Get( "r_aoType", "0", CVAR_ARCHIVE );
    
    // Initialize default configurations
    
    // Depth of Field
    postProcessState.dofConfig.enabled = r_dof->integer ? qtrue : qfalse;
    postProcessState.dofConfig.focusDistance = 100.0f;
    postProcessState.dofConfig.focusRange = 50.0f;
    postProcessState.dofConfig.nearBlur = 1.0f;
    postProcessState.dofConfig.farBlur = 1.0f;
    postProcessState.dofConfig.bokehSize = 5.0f;
    postProcessState.dofConfig.bokehSamples = 16;
    
    // Motion Blur
    postProcessState.motionBlurConfig.enabled = r_motionBlur->integer ? qtrue : qfalse;
    postProcessState.motionBlurConfig.velocityScale = 1.0f;
    postProcessState.motionBlurConfig.samples = 8;
    postProcessState.motionBlurConfig.maxBlur = 20.0f;
    
    // Ambient Occlusion
    postProcessState.aoConfig.enabled = r_ao->integer ? qtrue : qfalse;
    postProcessState.aoConfig.radius = 0.5f;
    postProcessState.aoConfig.intensity = 1.0f;
    postProcessState.aoConfig.bias = 0.01f;
    postProcessState.aoConfig.samples = 16;
    postProcessState.aoConfig.power = 2.0f;
    
    // Chromatic Aberration
    postProcessState.chromaticConfig.enabled = r_chromaticAberration->integer ? qtrue : qfalse;
    postProcessState.chromaticConfig.strength = 0.5f;
    VectorSet( postProcessState.chromaticConfig.shift, 1.0f, 0.0f, -1.0f );
    
    // Setup pass configurations
    postProcessState.chain.passes[POST_PASS_DEPTH_OF_FIELD].config = &postProcessState.dofConfig;
    postProcessState.chain.passes[POST_PASS_MOTION_BLUR].config = &postProcessState.motionBlurConfig;
    postProcessState.chain.passes[POST_PASS_AMBIENT_OCCLUSION].config = &postProcessState.aoConfig;
    postProcessState.chain.passes[POST_PASS_CHROMATIC_ABERRATION].config = &postProcessState.chromaticConfig;
    
    postProcessState.initialized = qtrue;
    postProcessState.enabled = r_postProcess->integer ? qtrue : qfalse;
    postProcessState.globalIntensity = 1.0f;
    
    ri.Printf( PRINT_ALL, "Post-process init: r_postProcess=%d, r_dof=%d, enabled=%d\n", 
        r_postProcess->integer, r_dof->integer, postProcessState.enabled );
    
    // Build initial post-process chain
    R_BuildPostProcessChain();
    
    // Initialize descriptor resources
    R_InitPostProcessDescriptors();
    
    // Initialize Vulkan pipelines for each effect
    R_InitPostProcessPipelines();
    
    ri.Printf( PRINT_ALL, "Post-processing framework initialized\n" );
    return qtrue;
}

/*
================
R_ShutdownPostProcess

Shutdown post-processing framework
================
*/
void R_ShutdownPostProcess( void ) {
    if ( !postProcessState.initialized ) {
        return;
    }
    
    // Cleanup pipelines
    R_ShutdownPostProcessPipelines();
    
    // Cleanup descriptors
    R_ShutdownPostProcessDescriptors();
    
    Com_Memset( &postProcessState, 0, sizeof( postProcessState ) );
}

/*
================
R_BuildPostProcessChain

Build the post-processing chain based on enabled passes
================
*/
void R_BuildPostProcessChain( void ) {
    postProcessChain_t *chain = &postProcessState.chain;
    uint32_t passIndex = 0;
    
    // Clear chain
    chain->numActivePasses = 0;
    
    // First update enabled state from CVars
    postProcessState.aoConfig.enabled = r_ao->integer ? qtrue : qfalse;
    postProcessState.dofConfig.enabled = r_dof->integer ? qtrue : qfalse;
    postProcessState.motionBlurConfig.enabled = r_motionBlur->integer ? qtrue : qfalse;
    postProcessState.chromaticConfig.enabled = r_chromaticAberration->integer ? qtrue : qfalse;
    
    // Add passes in optimal order
    
    // God rays / volumetric lighting (should be first for proper compositing)
    if ( r_volumetric && r_volumetric->integer ) {
        chain->passOrder[passIndex++] = POST_PASS_GOD_RAYS;
        chain->passes[POST_PASS_GOD_RAYS].enabled = qtrue;
    }
    
    // Pre-tone mapping passes
    if ( postProcessState.aoConfig.enabled ) {
        chain->passOrder[passIndex++] = POST_PASS_AMBIENT_OCCLUSION;
        chain->passes[POST_PASS_AMBIENT_OCCLUSION].enabled = qtrue;
    }
    
    if ( postProcessState.dofConfig.enabled ) {
        chain->passOrder[passIndex++] = POST_PASS_DEPTH_OF_FIELD;
        chain->passes[POST_PASS_DEPTH_OF_FIELD].enabled = qtrue;
    }
    
    if ( postProcessState.motionBlurConfig.enabled ) {
        chain->passOrder[passIndex++] = POST_PASS_MOTION_BLUR;
        chain->passes[POST_PASS_MOTION_BLUR].enabled = qtrue;
    }
    
    // Post-tone mapping passes
    if ( postProcessState.chromaticConfig.enabled ) {
        chain->passOrder[passIndex++] = POST_PASS_CHROMATIC_ABERRATION;
        chain->passes[POST_PASS_CHROMATIC_ABERRATION].enabled = qtrue;
    }
    
    if ( r_vignette && r_vignette->value > 0.0f ) {
        chain->passOrder[passIndex++] = POST_PASS_VIGNETTE;
        chain->passes[POST_PASS_VIGNETTE].enabled = qtrue;
    }
    
    if ( r_filmGrain && r_filmGrain->value > 0.0f ) {
        chain->passOrder[passIndex++] = POST_PASS_FILM_GRAIN;
        chain->passes[POST_PASS_FILM_GRAIN].enabled = qtrue;
    }
    
    chain->numActivePasses = passIndex;
    
    if ( r_postProcessDebug->integer ) {
        ri.Printf( PRINT_ALL, "Post-process chain rebuilt: %d active passes\n", passIndex );
    }
}

/*
================
R_EnablePostPass

Enable or disable a post-processing pass
================
*/
void R_EnablePostPass( postPassType_t type, qboolean enable ) {
    if ( type < 0 || type >= POST_PASS_COUNT ) {
        return;
    }
    
    postProcessState.chain.passes[type].enabled = enable;
    
    // Rebuild chain
    R_BuildPostProcessChain();
}

/*
================
R_SetPostPassIntensity

Set post-processing pass intensity
================
*/
void R_SetPostPassIntensity( postPassType_t type, float intensity ) {
    if ( type < 0 || type >= POST_PASS_COUNT ) {
        return;
    }
    
    postProcessState.chain.passes[type].intensity = intensity;
}

/*
================
R_SetDOFConfig

Set depth of field configuration
================
*/
void R_SetDOFConfig( const dofConfig_t *config ) {
    postProcessState.dofConfig = *config;
    
    // Update pass state
    postProcessState.chain.passes[POST_PASS_DEPTH_OF_FIELD].enabled = config->enabled;
    
    // Rebuild chain if needed
    R_BuildPostProcessChain();
}

/*
================
R_SetMotionBlurConfig

Set motion blur configuration
================
*/
void R_SetMotionBlurConfig( const motionBlurConfig_t *config ) {
    postProcessState.motionBlurConfig = *config;
    
    // Update pass state
    postProcessState.chain.passes[POST_PASS_MOTION_BLUR].enabled = config->enabled;
    
    // Rebuild chain if needed
    R_BuildPostProcessChain();
}

/*
================
R_SetAOConfig

Set ambient occlusion configuration
================
*/
void R_SetAOConfig( const aoConfig_t *config ) {
    postProcessState.aoConfig = *config;
    
    // Update pass state
    postProcessState.chain.passes[POST_PASS_AMBIENT_OCCLUSION].enabled = config->enabled;
    
    // Rebuild chain if needed
    R_BuildPostProcessChain();
}

/*
================
R_AutoFocus

Automatic depth of field focus
================
*/
void R_AutoFocus( const vec3_t viewOrigin, const vec3_t viewDir ) {
    if ( !postProcessState.dofConfig.enabled ) {
        return;
    }
    
    // Trace ray to find focus point
    trace_t trace;
    vec3_t end;
    
    VectorMA( viewOrigin, 10000.0f, viewDir, end );
    
    // Would perform trace here
    // For now, use fixed distance
    postProcessState.dofConfig.focusDistance = 500.0f;
}

/*
================
R_DrawPostProcessDebug

Draw post-processing debug information
================
*/
void R_DrawPostProcessDebug( void ) {
    if ( !r_postProcessDebug->integer || !postProcessState.initialized ) {
        return;
    }
    
    ri.Printf( PRINT_ALL, "Post-Process Debug:\n" );
    ri.Printf( PRINT_ALL, "  Enabled: %s\n", postProcessState.enabled ? "Yes" : "No" );
    ri.Printf( PRINT_ALL, "  Active Passes: %d\n", postProcessState.chain.numActivePasses );
    ri.Printf( PRINT_ALL, "  Frame Time: %.2f ms\n", postProcessState.frameTime );
    
    // List active passes
    for ( uint32_t i = 0; i < postProcessState.chain.numActivePasses; i++ ) {
        postPassType_t type = postProcessState.chain.passOrder[i];
        const char *passNames[] = {
            "Depth of Field",
            "Motion Blur",
            "Chromatic Aberration",
            "Vignette",
            "Film Grain",
            "Lens Flare",
            "God Rays",
            "Ambient Occlusion",
            "Fog",
            "SMAA",
            "FXAA",
            "Sharpen"
        };
        
        ri.Printf( PRINT_ALL, "    %d: %s\n", i + 1, passNames[type] );
    }
}

/*
================
R_ExecutePostProcessChain

Execute the entire post-processing chain
================
*/
void R_ExecutePostProcessChain( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage ) {
    // Ensure ping-pong buffers are created now that dimensions are known
    R_EnsurePostProcessBuffers();
    
    // Update enabled state from CVar
    postProcessState.enabled = r_postProcess->integer ? qtrue : qfalse;
    
    // Rebuild chain if CVars have changed
    R_BuildPostProcessChain();
    
    if ( r_postProcessDebug->integer ) {
        ri.Printf( PRINT_ALL, "Post-process: initialized=%d, enabled=%d, numActivePasses=%d\n", 
            postProcessState.initialized, postProcessState.enabled, postProcessState.chain.numActivePasses );
    }
    
    if ( !postProcessState.initialized || !postProcessState.enabled || postProcessState.chain.numActivePasses == 0 ) {
        if ( r_postProcessDebug->integer ) {
            ri.Printf( PRINT_ALL, "Post-process: Skipping - not initialized, enabled or no active passes\n" );
        }
        // If source and dest are the same, nothing to do
        if ( sourceImage == destImage ) {
            return;
        }
        
        // Just copy source to dest if postprocessing is disabled
        VkImageCopy copyRegion = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 }
        };
        vkCmdCopyImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
        return;
    }

    // Composite RTX result if enabled and mode requests it
    extern cvar_t *r_rt_mode;
    if ( RTX_IsEnabled() && RTX_IsAvailable() && r_rt_mode &&
         !Q_stricmp(r_rt_mode->string, "replace") ) {
        // Width/height used to blit the RTX output; dispatch happens before post-process
        int rw = vk.renderWidth ? vk.renderWidth : glConfig.vidWidth;
        int rh = vk.renderHeight ? vk.renderHeight : glConfig.vidHeight;

        VkImage rtImage = RTX_GetRTImage();
        if ( rtImage ) {
            // Transition target to transfer dst and copy RT image over
            VkImageMemoryBarrier barriers[2] = {0};
            barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barriers[0].oldLayout = vk_image_get_layout_or( sourceImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
            barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].image = sourceImage;
            barriers[0].subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

            vkCmdPipelineBarrier( cmd,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                  0, NULL, 0, NULL, 1, barriers );

            vk_image_set_layout( sourceImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );

            if ( vk.color_format == VK_FORMAT_R32G32B32A32_SFLOAT ) {
                VkImageCopy copyRegion = {
                    .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .extent = { (uint32_t)rw, (uint32_t)rh, 1 }
                };
                vkCmdCopyImage( cmd, rtImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                sourceImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
            } else {
                VkImageBlit blitRegion = {
                    .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .srcOffsets = { { 0, 0, 0 }, { (int32_t)rw, (int32_t)rh, 1 } },
                    .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .dstOffsets = { { 0, 0, 0 }, { (int32_t)rw, (int32_t)rh, 1 } }
                };
                vkCmdBlitImage( cmd, rtImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                sourceImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                1, &blitRegion, VK_FILTER_NEAREST );
            }

            // Transition back for shader reads in post chain
            barriers[1] = (VkImageMemoryBarrier){
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = vk_image_get_layout_or( sourceImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ),
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = sourceImage,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            };
            vkCmdPipelineBarrier( cmd,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                  0, NULL, 0, NULL, 1, &barriers[1] );

            vk_image_set_layout( sourceImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
        }
    }

    // Hybrid additive composite: add RT lighting over raster
    if ( RTX_IsEnabled() && RTX_IsAvailable() && r_rt_mode &&
         !Q_stricmp(r_rt_mode->string, "hybrid") ) {
        int rw = vk.renderWidth ? vk.renderWidth : glConfig.vidWidth;
        int rh = vk.renderHeight ? vk.renderHeight : glConfig.vidHeight;
        // Run the composite compute into the albedo proxy, then copy back
        RTX_CompositeHybridAdd( cmd, (uint32_t)rw, (uint32_t)rh, RTX_GetHybridIntensity() );
        // Copy albedo proxy back to color_image to show the result
        VkImageMemoryBarrier barriers[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = RTX_GetRTImage() /* placeholder, will be overridden below */,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = vk_image_get_layout_or( vk.color_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ),
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk.color_image,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            }
        };
        // We can't fetch albedo proxy here directly; do a cautious barrier only for color target
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barriers[1] );
        vk_image_set_layout( vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
        VkImageCopy copyRegion = { .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .extent = { (uint32_t)rw, (uint32_t)rh, 1 } };
        // Copy from RT image (already holds RT color) onto color_image as an approximation for additive blend fallback
        vkCmdCopyImage( cmd, RTX_GetRTImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
        // Restore color to SHADER_READ for subsequent passes
        VkImageMemoryBarrier toShader = barriers[1];
        toShader.oldLayout = vk_image_get_layout_or( vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &toShader );
        vk_image_set_layout( vk.color_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
    }
    
    if ( r_postProcessDebug->integer ) {
        ri.Printf( PRINT_ALL, "Post-process: Executing %d passes\n", postProcessState.chain.numActivePasses );
    }
    
    // Execute each active pass in order
    VkImage currentSource = sourceImage;
    VkImage currentDest = destImage;
    
    for ( uint32_t i = 0; i < postProcessState.chain.numActivePasses; i++ ) {
        postPassType_t passType = postProcessState.chain.passOrder[i];
        
        if ( r_postProcessDebug->integer ) {
            const char *passNames[] = {
                "Depth of Field", "Motion Blur", "Chromatic Aberration",
                "Vignette", "Film Grain", "Lens Flare", "God Rays",
                "Ambient Occlusion", "Fog", "SMAA", "FXAA", "Sharpen"
            };
            ri.Printf( PRINT_ALL, "  Executing pass %d: %s\n", i + 1, 
                passType < POST_PASS_COUNT ? passNames[passType] : "Unknown" );
        }
        
        switch ( passType ) {
            case POST_PASS_GOD_RAYS:
                R_ApplyGodRays( cmd, currentSource, currentDest );
                break;
            case POST_PASS_AMBIENT_OCCLUSION:
                R_ApplyAmbientOcclusion( cmd );
                break;
            case POST_PASS_DEPTH_OF_FIELD:
                R_ApplyDepthOfField( cmd, currentSource, currentDest );
                break;
            case POST_PASS_MOTION_BLUR:
                R_ApplyMotionBlur( cmd, currentSource, currentDest );
                break;
            case POST_PASS_CHROMATIC_ABERRATION:
                R_ApplyChromaticAberration( cmd, currentSource, currentDest );
                break;
            case POST_PASS_VIGNETTE:
                R_ApplyVignette( cmd, currentSource, currentDest );
                break;
            case POST_PASS_FILM_GRAIN:
                R_ApplyFilmGrain( cmd, currentSource, currentDest );
                break;
            default:
                break;
        }
        
        // Swap buffers for next pass if not the last one
        if ( i < postProcessState.chain.numActivePasses - 1 ) {
            VkImage temp = currentSource;
            currentSource = currentDest;
            currentDest = temp;
        }
    }
}

/*
================
R_ApplyGodRays

Apply god rays (volumetric light scattering) effect
================
*/
void R_ApplyGodRays( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage ) {
    // Cannot copy an image to itself
    if ( sourceImage == destImage ) {
        // In-place god rays would require compute shader or intermediate buffer
        return;
    }
    
    if ( !r_volumetric || !r_volumetric->integer ) {
        return;
    }
    
    // God rays implementation
    // This creates light shafts from bright light sources (sun/sky)
    
    // 1. Identify light source position in screen space
    vec3_t sunDirection;
    vec2_t lightScreenPos;
    
    // Get sun direction from world
    if ( tr.sunDirection[0] != 0 || tr.sunDirection[1] != 0 || tr.sunDirection[2] != 0 ) {
        VectorCopy( tr.sunDirection, sunDirection );
    } else {
        // Default sun direction if not set
        VectorSet( sunDirection, 0.3f, 0.3f, 0.9f );
    }
    
    // Project sun position to screen space
    vec3_t sunWorldPos;
    VectorMA( tr.refdef.vieworg, 10000.0f, sunDirection, sunWorldPos );
    
    // Simple screen space radial blur from light source
    // This would normally use a compute shader or fragment shader
    // For now, we'll use a placeholder implementation
    
    // Transition images to correct layouts
    VkImageMemoryBarrier barriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = sourceImage,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = destImage,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        }
    };
    
    vkCmdPipelineBarrier( cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 2, barriers );
    
    // For now, just copy the image (placeholder)
        VkImageBlit blit = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .srcOffsets = { {0,0,0}, { (int32_t)vk.renderWidth, (int32_t)vk.renderHeight, 1 } },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstOffsets = { {0,0,0}, { (int32_t)glConfig.vidWidth, (int32_t)glConfig.vidHeight, 1 } }
        };
        vkCmdBlitImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR );
    
    // Add volumetric light scattering contribution
    if ( volumetricState.enabled ) {
        R_RenderVolumetricFog( cmd );
        R_CompositeVolumetricFog( cmd, destImage );
    }
}

/*
================
R_ApplyAmbientOcclusion

Apply screen-space ambient occlusion
================
*/
void R_ApplyAmbientOcclusion( VkCommandBuffer cmd ) {
    if ( !r_ao || !r_ao->integer ) {
        return;
    }
    
    // SSAO implementation placeholder
    // This would normally sample depth buffer and calculate occlusion
    
    // For now, this is a stub that would:
    // 1. Sample depth buffer in a hemisphere around each pixel
    // 2. Calculate occlusion based on depth differences
    // 3. Apply bilateral blur to reduce noise
    // 4. Composite with main image
    
    // The actual implementation would use compute shaders or
    // a multi-pass fragment shader approach
}

/*
================
R_ApplyDepthOfField

Apply depth of field effect
================
*/
void R_ApplyDepthOfField( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage ) {
    // Cannot copy an image to itself
    if ( sourceImage == destImage ) {
        // In-place DOF would require compute shader or intermediate buffer
        return;
    }
    
    if ( !r_dof || !r_dof->integer ) {
        // Just copy if DOF is disabled
        VkImageCopy copyRegion = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 }
        };
        vkCmdCopyImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
        return;
    }
    
    // DOF implementation would:
    // 1. Read depth buffer to determine focus distance
    // 2. Calculate circle of confusion for each pixel
    // 3. Apply variable blur based on distance from focal plane
    // Placeholder for now
    VkImageCopy copyRegion = {
        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 }
    };
    vkCmdCopyImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
}

/*
================
R_ApplyMotionBlur

Apply motion blur effect
================
*/
void R_ApplyMotionBlur( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage ) {
    // Cannot copy an image to itself
    if ( sourceImage == destImage ) {
        // In-place motion blur would require compute shader or intermediate buffer
        return;
    }
    
    if ( !r_motionBlur || !r_motionBlur->integer ) {
        // Just copy if motion blur is disabled
        VkImageCopy copyRegion = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 }
        };
        vkCmdCopyImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
        return;
    }
    
    // Motion blur implementation would:
    // 1. Store previous frame's MVP matrix
    // 2. Calculate per-pixel velocity vectors
    // 3. Blur along velocity vectors
    // For now, just copy the image
    VkImageCopy copyRegion = {
        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 }
    };
    vkCmdCopyImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
}

/*
================
R_ApplyChromaticAberration

Apply chromatic aberration effect
================
*/
void R_ApplyChromaticAberration( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage ) {
    // Cannot copy an image to itself
    if ( sourceImage == destImage ) {
        // In-place chromatic aberration would require compute shader or intermediate buffer
        return;
    }
    
    if ( !r_chromaticAberration || !r_chromaticAberration->integer ) {
        // Just copy if chromatic aberration is disabled
        VkImageCopy copyRegion = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 }
        };
        vkCmdCopyImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
        return;
    }
    
    // Check if pipeline is available
    VkPipeline pipeline = postProcessState.pipelines[POST_PASS_CHROMATIC_ABERRATION];
    if ( pipeline == VK_NULL_HANDLE ) {
        if ( r_postProcessDebug->integer ) {
            ri.Printf( PRINT_WARNING, "Chromatic aberration pipeline not initialized, using fallback\n" );
        }
        // For now, just copy the image as a placeholder
        // The actual shader implementation will be executed when the pipeline is properly initialized
        VkImageCopy copyRegion = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 }
        };
        vkCmdCopyImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
        return;
    }
    
    // The full implementation would render using the chromatic aberration pipeline
    // For now, we're using a simplified copy approach until the pipeline system is fully integrated
    if ( r_postProcessDebug->integer ) {
        ri.Printf( PRINT_ALL, "  Applying chromatic aberration (strength: %.2f)\n", 
            postProcessState.chromaticConfig.strength );
    }
    
    // Placeholder: Copy with notification that effect would be applied
    if ( sourceImage != destImage ) {
        VkImageBlit blit = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .srcOffsets = { {0,0,0}, { (int32_t)vk.renderWidth, (int32_t)vk.renderHeight, 1 } },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstOffsets = { {0,0,0}, { (int32_t)glConfig.vidWidth, (int32_t)glConfig.vidHeight, 1 } }
        };
        vkCmdBlitImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR );
    }
}

/*
================
R_ApplyVignette

Apply vignette effect
================
*/
void R_ApplyVignette( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage ) {
    // Cannot copy an image to itself
    if ( sourceImage == destImage ) {
        // In-place vignette would require compute shader or intermediate buffer
        return;
    }
    
    if ( !r_vignette || r_vignette->value <= 0.0f ) {
        // Just copy if vignette is disabled
        VkImageCopy copyRegion = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 }
        };
        vkCmdCopyImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
        return;
    }
    
    // Vignette implementation would:
    // 1. Calculate distance from center for each pixel
    // 2. Darken pixels based on distance
    // 3. Use smooth falloff curve
    // Placeholder for now
    VkImageCopy copyRegion = {
        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 }
    };
    vkCmdCopyImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
}

/*
================
R_ApplyFilmGrain

Apply film grain effect
================
*/
void R_ApplyFilmGrain( VkCommandBuffer cmd, VkImage sourceImage, VkImage destImage ) {
    // Cannot copy an image to itself
    if ( sourceImage == destImage ) {
        // In-place film grain would require compute shader or intermediate buffer
        return;
    }
    
    if ( !r_filmGrain || r_filmGrain->value <= 0.0f ) {
        // Just copy if film grain is disabled
        VkImageCopy copyRegion = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 }
        };
        vkCmdCopyImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
        return;
    }
    
    // Film grain implementation would:
    // 1. Generate noise texture based on time
    // 2. Blend noise with image
    // 3. Vary intensity based on luminance
    // Placeholder for now
    VkImageCopy copyRegion = {
        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .extent = { glConfig.vidWidth, glConfig.vidHeight, 1 }
    };
    vkCmdCopyImage( cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
}
