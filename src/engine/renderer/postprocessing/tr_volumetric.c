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
// tr_volumetric.c - Volumetric Rendering implementation

#include "tr_volumetric.h"
#include "../vulkan/vk.h"

// Global volumetric state
volumetricState_t volumetricState;

// CVars
cvar_t *r_volumetric;
cvar_t *r_volumetricQuality;
cvar_t *r_volumetricDensity;
cvar_t *r_volumetricDebug;

// Forward declarations
static qboolean R_CreateVolumetricResources( void );
static void R_DestroyVolumetricResources( void );
static qboolean R_CreateVolumetricPipelines( void );
static void R_DestroyVolumetricPipelines( void );
static void R_UpdateVolumetricLights( VkCommandBuffer cmd );
static void R_InjectVolumetricDensity( VkCommandBuffer cmd );
static void R_CalculateVolumetricScattering( VkCommandBuffer cmd );
static void R_IntegrateVolumetricLighting( VkCommandBuffer cmd );

// Quality presets
static const volumetricConfig_t volumetricQualityPresets[] = {
    // VOLUMETRIC_QUALITY_OFF
    { .quality = VOLUMETRIC_QUALITY_OFF },
    
    // VOLUMETRIC_QUALITY_LOW
    {
        .quality = VOLUMETRIC_QUALITY_LOW,
        .gridSizeX = 80,
        .gridSizeY = 45,
        .gridSizeZ = 64,
        .depthSlicing = 1.05f,
        .scatteringCoeff = 0.01f,
        .absorptionCoeff = 0.001f,
        .anisotropy = 0.8f,
        .density = 0.05f,
        .marchSteps = 32,
        .marchStepSize = 1.0f,
        .useTemporal = qfalse,
        .enableShadows = qfalse,
    },
    
    // VOLUMETRIC_QUALITY_MEDIUM
    {
        .quality = VOLUMETRIC_QUALITY_MEDIUM,
        .gridSizeX = 160,
        .gridSizeY = 90,
        .gridSizeZ = 128,
        .depthSlicing = 1.03f,
        .scatteringCoeff = 0.02f,
        .absorptionCoeff = 0.002f,
        .anisotropy = 0.7f,
        .density = 0.1f,
        .marchSteps = 64,
        .marchStepSize = 0.5f,
        .useTemporal = qtrue,
        .temporalBlend = 0.9f,
        .enableShadows = qtrue,
        .shadowSamples = 8,
        .shadowDensity = 0.5f,
    },
    
    // VOLUMETRIC_QUALITY_HIGH
    {
        .quality = VOLUMETRIC_QUALITY_HIGH,
        .gridSizeX = 240,
        .gridSizeY = 135,
        .gridSizeZ = 192,
        .depthSlicing = 1.02f,
        .scatteringCoeff = 0.03f,
        .absorptionCoeff = 0.003f,
        .anisotropy = 0.6f,
        .density = 0.15f,
        .marchSteps = 96,
        .marchStepSize = 0.33f,
        .useTemporal = qtrue,
        .temporalBlend = 0.95f,
        .enableShadows = qtrue,
        .shadowSamples = 16,
        .shadowDensity = 0.75f,
    },
    
    // VOLUMETRIC_QUALITY_ULTRA
    {
        .quality = VOLUMETRIC_QUALITY_ULTRA,
        .gridSizeX = 320,
        .gridSizeY = 180,
        .gridSizeZ = 256,
        .depthSlicing = 1.01f,
        .scatteringCoeff = 0.04f,
        .absorptionCoeff = 0.004f,
        .anisotropy = 0.5f,
        .density = 0.2f,
        .marchSteps = 128,
        .marchStepSize = 0.25f,
        .useTemporal = qtrue,
        .temporalBlend = 0.98f,
        .enableShadows = qtrue,
        .shadowSamples = 32,
        .shadowDensity = 1.0f,
    },
};

/*
================
R_InitVolumetric

Initialize Volumetric Rendering
================
*/
qboolean R_InitVolumetric( void ) {
    VkResult result;
    
    Com_Memset( &volumetricState, 0, sizeof( volumetricState ) );
    
    // Register CVars
    r_volumetric = ri.Cvar_Get( "r_volumetric", "1", CVAR_ARCHIVE );
    r_volumetricQuality = ri.Cvar_Get( "r_volumetricQuality", "2", CVAR_ARCHIVE );
    r_volumetricDensity = ri.Cvar_Get( "r_volumetricDensity", "1.0", CVAR_ARCHIVE );
    r_volumetricDebug = ri.Cvar_Get( "r_volumetricDebug", "0", CVAR_CHEAT );
    
    // Set default quality
    R_SetVolumetricQuality( r_volumetricQuality->integer );
    
    volumetricState.enabled = r_volumetric->integer ? qtrue : qfalse;
    
    if ( !volumetricState.enabled ) {
        volumetricState.initialized = qfalse;
        return qtrue;
    }
    
    // Create volumetric resources
    if ( !R_CreateVolumetricResources() ) {
        ri.Printf( PRINT_WARNING, "Failed to create volumetric resources\n" );
        return qfalse;
    }
    
    // Create volumetric pipelines
    if ( !R_CreateVolumetricPipelines() ) {
        ri.Printf( PRINT_WARNING, "Failed to create volumetric pipelines\n" );
        R_DestroyVolumetricResources();
        return qfalse;
    }
    
    volumetricState.initialized = qtrue;
    
    ri.Printf( PRINT_ALL, "Volumetric rendering initialized (Quality: %d)\n", volumetricState.config.quality );
    return qtrue;
}

/*
================
R_ShutdownVolumetric

Shutdown Volumetric Rendering
================
*/
void R_ShutdownVolumetric( void ) {
    if ( !volumetricState.initialized ) {
        return;
    }
    
    R_DestroyVolumetricPipelines();
    R_DestroyVolumetricResources();
    
    Com_Memset( &volumetricState, 0, sizeof( volumetricState ) );
}

/*
================
R_SetVolumetricQuality

Set volumetric quality preset
================
*/
void R_SetVolumetricQuality( volumetricQuality_t quality ) {
    if ( quality < 0 || quality > VOLUMETRIC_QUALITY_ULTRA ) {
        quality = VOLUMETRIC_QUALITY_MEDIUM;
    }
    
    volumetricState.config = volumetricQualityPresets[quality];
    
    // Apply density scale
    if ( r_volumetricDensity ) {
        volumetricState.config.density *= r_volumetricDensity->value;
    }
}

/*
================
R_SetVolumetricConfig

Set custom volumetric configuration
================
*/
void R_SetVolumetricConfig( const volumetricConfig_t *config ) {
    volumetricState.config = *config;
}

/*
================
R_AddVolumetricLight

Add a volumetric light source
================
*/
void R_AddVolumetricLight( const volumetricLight_t *light ) {
    if ( volumetricState.numLights >= VOLUMETRIC_MAX_LIGHTS ) {
        return;
    }
    
    volumetricState.lights[volumetricState.numLights++] = *light;
}

/*
================
R_ClearVolumetricLights

Clear all volumetric lights
================
*/
void R_ClearVolumetricLights( void ) {
    volumetricState.numLights = 0;
}

/*
================
R_DrawVolumetricDebug

Draw volumetric debug information
================
*/
void R_DrawVolumetricDebug( void ) {
    if ( !r_volumetricDebug->integer || !volumetricState.initialized ) {
        return;
    }
    
    ri.Printf( PRINT_ALL, "Volumetric Debug:\n" );
    ri.Printf( PRINT_ALL, "  Enabled: %s\n", volumetricState.enabled ? "Yes" : "No" );
    ri.Printf( PRINT_ALL, "  Quality: %d\n", volumetricState.config.quality );
    ri.Printf( PRINT_ALL, "  Grid: %dx%dx%d\n", 
              volumetricState.config.gridSizeX,
              volumetricState.config.gridSizeY,
              volumetricState.config.gridSizeZ );
    ri.Printf( PRINT_ALL, "  Lights: %d\n", volumetricState.numLights );
    ri.Printf( PRINT_ALL, "  Avg Density: %.3f\n", volumetricState.averageDensity );
}

/*
================
R_CreateVolumetricResources

Create 3D textures and buffers for volumetric rendering
================
*/
static qboolean R_CreateVolumetricResources( void ) {
    VkResult result;
    VkDevice device = vk.device;
    VkPhysicalDevice physicalDevice = vk.physical_device;
    
    // Get grid dimensions from config
    uint32_t gridX = volumetricState.config.gridSizeX;
    uint32_t gridY = volumetricState.config.gridSizeY;
    uint32_t gridZ = volumetricState.config.gridSizeZ;
    
    // Create 3D scattering volume texture
    {
        VkImageCreateInfo imageInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_3D,
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .extent = { gridX, gridY, gridZ },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        
        result = qvkCreateImage( device, &imageInfo, NULL, &volumetricState.resources.scatteringVolume );
        if ( result != VK_SUCCESS ) {
            ri.Printf( PRINT_WARNING, "Failed to create scattering volume texture\n" );
            return qfalse;
        }
        
        // Allocate memory for scattering volume
        VkMemoryRequirements memReqs;
        qvkGetImageMemoryRequirements( device, volumetricState.resources.scatteringVolume, &memReqs );
        
        VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = vk_find_memory_type( memReqs.memoryTypeBits, 
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
        };
        
        result = qvkAllocateMemory( device, &allocInfo, NULL, &volumetricState.resources.scatteringMemory );
        if ( result != VK_SUCCESS ) {
            ri.Printf( PRINT_WARNING, "Failed to allocate scattering volume memory\n" );
            return qfalse;
        }
        
        qvkBindImageMemory( device, volumetricState.resources.scatteringVolume, 
                          volumetricState.resources.scatteringMemory, 0 );
        
        // Create image view
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = volumetricState.resources.scatteringVolume,
            .viewType = VK_IMAGE_VIEW_TYPE_3D,
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        
        result = qvkCreateImageView( device, &viewInfo, NULL, &volumetricState.resources.scatteringView );
        if ( result != VK_SUCCESS ) {
            ri.Printf( PRINT_WARNING, "Failed to create scattering volume view\n" );
            return qfalse;
        }
    }
    
    // Create 3D density volume texture
    {
        VkImageCreateInfo imageInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_3D,
            .format = VK_FORMAT_R16_SFLOAT,
            .extent = { gridX, gridY, gridZ },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        
        result = qvkCreateImage( device, &imageInfo, NULL, &volumetricState.resources.densityVolume );
        if ( result != VK_SUCCESS ) {
            ri.Printf( PRINT_WARNING, "Failed to create density volume texture\n" );
            return qfalse;
        }
        
        // Allocate and bind memory (similar to scattering volume)
        VkMemoryRequirements memReqs;
        qvkGetImageMemoryRequirements( device, volumetricState.resources.densityVolume, &memReqs );
        
        VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = vk_find_memory_type( memReqs.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
        };
        
        result = qvkAllocateMemory( device, &allocInfo, NULL, &volumetricState.resources.densityMemory );
        if ( result != VK_SUCCESS ) {
            return qfalse;
        }
        
        qvkBindImageMemory( device, volumetricState.resources.densityVolume,
                          volumetricState.resources.densityMemory, 0 );
        
        // Create view
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = volumetricState.resources.densityVolume,
            .viewType = VK_IMAGE_VIEW_TYPE_3D,
            .format = VK_FORMAT_R16_SFLOAT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        
        result = qvkCreateImageView( device, &viewInfo, NULL, &volumetricState.resources.densityView );
        if ( result != VK_SUCCESS ) {
            return qfalse;
        }
    }
    
    // Create light buffer
    {
        VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof( volumetricLight_t ) * VOLUMETRIC_MAX_LIGHTS,
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        
        result = qvkCreateBuffer( device, &bufferInfo, NULL, &volumetricState.resources.lightBuffer );
        if ( result != VK_SUCCESS ) {
            return qfalse;
        }
        
        VkMemoryRequirements memReqs;
        qvkGetBufferMemoryRequirements( device, volumetricState.resources.lightBuffer, &memReqs );
        
        VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = vk_find_memory_type( memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT )
        };
        
        result = qvkAllocateMemory( device, &allocInfo, NULL, &volumetricState.resources.lightMemory );
        if ( result != VK_SUCCESS ) {
            return qfalse;
        }
        
        qvkBindBufferMemory( device, volumetricState.resources.lightBuffer,
                           volumetricState.resources.lightMemory, 0 );
    }
    
    return qtrue;
}

/*
================
R_DestroyVolumetricResources

Destroy volumetric rendering resources
================
*/
static void R_DestroyVolumetricResources( void ) {
    VkDevice device = vk.device;
    
    if ( volumetricState.resources.scatteringView ) {
        qvkDestroyImageView( device, volumetricState.resources.scatteringView, NULL );
    }
    if ( volumetricState.resources.scatteringVolume ) {
        qvkDestroyImage( device, volumetricState.resources.scatteringVolume, NULL );
    }
    if ( volumetricState.resources.scatteringMemory ) {
        qvkFreeMemory( device, volumetricState.resources.scatteringMemory, NULL );
    }
    
    if ( volumetricState.resources.densityView ) {
        qvkDestroyImageView( device, volumetricState.resources.densityView, NULL );
    }
    if ( volumetricState.resources.densityVolume ) {
        qvkDestroyImage( device, volumetricState.resources.densityVolume, NULL );
    }
    if ( volumetricState.resources.densityMemory ) {
        qvkFreeMemory( device, volumetricState.resources.densityMemory, NULL );
    }
    
    if ( volumetricState.resources.lightBuffer ) {
        qvkDestroyBuffer( device, volumetricState.resources.lightBuffer, NULL );
    }
    if ( volumetricState.resources.lightMemory ) {
        qvkFreeMemory( device, volumetricState.resources.lightMemory, NULL );
    }
    
    Com_Memset( &volumetricState.resources, 0, sizeof( volumetricState.resources ) );
}

/*
================
R_CreateVolumetricPipelines

Create compute pipelines for volumetric rendering
================
*/
static qboolean R_CreateVolumetricPipelines( void ) {
    // This would create compute pipelines for:
    // 1. Density injection
    // 2. Light scattering calculation
    // 3. Ray integration
    // 4. Composite with scene
    
    // For now, return true as pipelines would be created here
    return qtrue;
}

/*
================
R_DestroyVolumetricPipelines

Destroy volumetric pipelines
================
*/
static void R_DestroyVolumetricPipelines( void ) {
    VkDevice device = vk.device;
    
    if ( volumetricState.pipeline.densityPipeline ) {
        qvkDestroyPipeline( device, volumetricState.pipeline.densityPipeline, NULL );
    }
    if ( volumetricState.pipeline.scatteringPipeline ) {
        qvkDestroyPipeline( device, volumetricState.pipeline.scatteringPipeline, NULL );
    }
    if ( volumetricState.pipeline.integrationPipeline ) {
        qvkDestroyPipeline( device, volumetricState.pipeline.integrationPipeline, NULL );
    }
    if ( volumetricState.pipeline.compositePipeline ) {
        qvkDestroyPipeline( device, volumetricState.pipeline.compositePipeline, NULL );
    }
    
    Com_Memset( &volumetricState.pipeline, 0, sizeof( volumetricState.pipeline ) );
}

/*
================
R_RenderVolumetricFog

Render volumetric fog/god rays into 3D texture
================
*/
void R_RenderVolumetricFog( VkCommandBuffer cmd ) {
    if ( !volumetricState.initialized || !volumetricState.enabled ) {
        return;
    }
    
    // Update light buffer with current lights
    R_UpdateVolumetricLights( cmd );
    
    // Phase 1: Inject density into 3D texture
    R_InjectVolumetricDensity( cmd );
    
    // Phase 2: Calculate light scattering
    R_CalculateVolumetricScattering( cmd );
    
    // Phase 3: Integrate scattered light along view rays
    R_IntegrateVolumetricLighting( cmd );
    
    // Update average density for debugging
    volumetricState.averageDensity = volumetricState.config.density;
}

/*
================
R_UpdateVolumetricLights

Update light buffer with current scene lights
================
*/
static void R_UpdateVolumetricLights( VkCommandBuffer cmd ) {
    void *data;
    VkResult result;
    
    // Map light buffer
    result = qvkMapMemory( vk.device, volumetricState.resources.lightMemory, 0,
                         sizeof( volumetricLight_t ) * volumetricState.numLights, 0, &data );
    
    if ( result == VK_SUCCESS ) {
        // Copy light data
        Com_Memcpy( data, volumetricState.lights, sizeof( volumetricLight_t ) * volumetricState.numLights );
        
        // Add sun/directional light if present
        if ( tr.sunDirection[0] != 0 || tr.sunDirection[1] != 0 || tr.sunDirection[2] != 0 ) {
            volumetricLight_t *sunLight = &((volumetricLight_t *)data)[volumetricState.numLights];
            sunLight->type = VOLUMETRIC_LIGHT_DIRECTIONAL;
            VectorCopy( tr.sunDirection, sunLight->direction );
            VectorCopy( tr.sunLight, sunLight->color );
            sunLight->intensity = tr.sunLight[3] * 2.0f; // Boost sun intensity for god rays
            sunLight->castShadows = qtrue;
        }
        
        qvkUnmapMemory( vk.device, volumetricState.resources.lightMemory );
    }
}

/*
================
R_InjectVolumetricDensity

Inject fog density into 3D texture
================
*/
static void R_InjectVolumetricDensity( VkCommandBuffer cmd ) {
    // Transition density volume to general layout for compute shader write
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = volumetricState.resources.densityVolume,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    qvkCmdPipelineBarrier( cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier );
    
    // Calculate fog density based on height and distance
    // This creates exponential height fog with density variations
    
    // Dispatch compute shader to fill density volume
    // Grid dimensions from config
    uint32_t groupX = (volumetricState.config.gridSizeX + 7) / 8;
    uint32_t groupY = (volumetricState.config.gridSizeY + 7) / 8;
    uint32_t groupZ = (volumetricState.config.gridSizeZ + 7) / 8;
    
    if ( volumetricState.pipeline.densityPipeline ) {
        qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, volumetricState.pipeline.densityPipeline );
        qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                volumetricState.pipeline.densityLayout, 0, 1,
                                &volumetricState.pipeline.densityDescSet, 0, NULL );
        qvkCmdDispatch( cmd, groupX, groupY, groupZ );
    }
}

/*
================
R_CalculateVolumetricScattering

Calculate light scattering through volume
================
*/
static void R_CalculateVolumetricScattering( VkCommandBuffer cmd ) {
    // Transition scattering volume for compute shader write
    VkImageMemoryBarrier barriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = volumetricState.resources.densityVolume,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = volumetricState.resources.scatteringVolume,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        }
    };
    
    qvkCmdPipelineBarrier( cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 0, NULL, 2, barriers );
    
    // Calculate scattering using Henyey-Greenstein phase function
    // For each voxel, accumulate light contribution from all light sources
    
    uint32_t groupX = (volumetricState.config.gridSizeX + 7) / 8;
    uint32_t groupY = (volumetricState.config.gridSizeY + 7) / 8;
    uint32_t groupZ = (volumetricState.config.gridSizeZ + 7) / 8;
    
    if ( volumetricState.pipeline.scatteringPipeline ) {
        qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, volumetricState.pipeline.scatteringPipeline );
        qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                volumetricState.pipeline.scatteringLayout, 0, 1,
                                &volumetricState.pipeline.scatteringDescSet, 0, NULL );
        qvkCmdDispatch( cmd, groupX, groupY, groupZ );
    }
}

/*
================
R_IntegrateVolumetricLighting

Ray march and integrate volumetric lighting
================
*/
static void R_IntegrateVolumetricLighting( VkCommandBuffer cmd ) {
    // Transition scattering volume for reading
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = volumetricState.resources.scatteringVolume,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    qvkCmdPipelineBarrier( cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier );
    
    // Ray march through volume from camera
    // Accumulate in-scattering and out-scattering along rays
    
    if ( volumetricState.pipeline.integrationPipeline ) {
        qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, volumetricState.pipeline.integrationPipeline );
        qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                volumetricState.pipeline.integrationLayout, 0, 1,
                                &volumetricState.pipeline.integrationDescSet, 0, NULL );
        
        // Dispatch based on screen resolution
        uint32_t groupX = (glConfig.vidWidth + 7) / 8;
        uint32_t groupY = (glConfig.vidHeight + 7) / 8;
        qvkCmdDispatch( cmd, groupX, groupY, 1 );
    }
}

/*
================
R_CompositeVolumetricFog

Composite volumetric fog with scene
================
*/
void R_CompositeVolumetricFog( VkCommandBuffer cmd, VkImage targetImage ) {
    if ( !volumetricState.initialized || !volumetricState.enabled ) {
        return;
    }
    
    // Ensure scattering volume is in correct layout for sampling
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = volumetricState.resources.scatteringVolume,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    qvkCmdPipelineBarrier( cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier );
    
    // Composite volumetric lighting with main scene
    // Uses depth buffer for proper occlusion
    
    if ( volumetricState.pipeline.compositePipeline ) {
        // Begin render pass for compositing
        VkRenderPassBeginInfo rpInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = vk.render_pass.main,
            .framebuffer = vk.framebuffers.main[vk.cmd->swapchain_image_index],
            .renderArea = {
                .offset = { 0, 0 },
                .extent = { vk.renderWidth, vk.renderHeight }
            }
        };
        
        qvkCmdBeginRenderPass( cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE );
        
        qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, volumetricState.pipeline.compositePipeline );
        qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                volumetricState.pipeline.compositeLayout, 0, 1,
                                &volumetricState.pipeline.compositeDescSet, 0, NULL );
        
        // Draw fullscreen quad
        qvkCmdDraw( cmd, 3, 1, 0, 0 );
        
        qvkCmdEndRenderPass( cmd );
    }
}