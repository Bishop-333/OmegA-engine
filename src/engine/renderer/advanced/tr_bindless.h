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
// tr_bindless.h - Bindless resource management system

#ifndef __TR_BINDLESS_H
#define __TR_BINDLESS_H

#include "../core/tr_local.h"
#include "vulkan/vulkan.h"

// Bindless resource limits
#define MAX_BINDLESS_TEXTURES    16384
#define MAX_BINDLESS_SAMPLERS    256
#define MAX_BINDLESS_BUFFERS     8192
#define MAX_BINDLESS_IMAGES      4096

// Invalid resource handle
#define BINDLESS_INVALID_HANDLE  0xFFFFFFFF

// Resource types
typedef enum {
    BINDLESS_RESOURCE_TEXTURE,
    BINDLESS_RESOURCE_SAMPLER,
    BINDLESS_RESOURCE_BUFFER,
    BINDLESS_RESOURCE_IMAGE,
    BINDLESS_RESOURCE_TLAS,     // Top-level acceleration structure for RT
} bindlessResourceType_t;

// Bindless texture descriptor
typedef struct bindlessTexture_s {
    VkImageView         imageView;
    VkSampler          sampler;
    uint32_t           textureID;
    uint32_t           samplerID;
    qboolean           inUse;
    char               name[MAX_QPATH];
    
    // Texture properties
    uint32_t           width;
    uint32_t           height;
    uint32_t           mipLevels;
    VkFormat           format;
    VkImageUsageFlags  usage;
} bindlessTexture_t;

// Bindless buffer descriptor
typedef struct bindlessBuffer_s {
    VkBuffer           buffer;
    VkDeviceSize       offset;
    VkDeviceSize       range;
    uint32_t           bufferID;
    qboolean           inUse;
    
    // Buffer properties
    VkBufferUsageFlags usage;
    VkMemoryPropertyFlags memoryProperties;
} bindlessBuffer_t;

// Bindless sampler descriptor
typedef struct bindlessSampler_s {
    VkSampler          sampler;
    uint32_t           samplerID;
    qboolean           inUse;
    
    // Sampler properties
    VkFilter           minFilter;
    VkFilter           magFilter;
    VkSamplerMipmapMode mipMode;
    VkSamplerAddressMode addressModeU;
    VkSamplerAddressMode addressModeV;
    VkSamplerAddressMode addressModeW;
    float              maxAnisotropy;
} bindlessSampler_t;

// Bindless storage image descriptor
typedef struct bindlessImage_s {
    VkImageView        imageView;
    uint32_t           imageID;
    qboolean           inUse;
    
    // Image properties
    uint32_t           width;
    uint32_t           height;
    VkFormat           format;
    VkImageLayout      layout;
} bindlessImage_t;

// Bindless descriptor pool
typedef struct bindlessPool_s {
    VkDescriptorPool        pool;
    VkDescriptorSetLayout   setLayout;
    VkDescriptorSet         descriptorSet;
    
    // Resource arrays
    bindlessTexture_t      *textures;
    bindlessSampler_t      *samplers;
    bindlessBuffer_t       *buffers;
    bindlessImage_t        *images;
    
    // Resource counts
    uint32_t                numTextures;
    uint32_t                numSamplers;
    uint32_t                numBuffers;
    uint32_t                numImages;
    
    // Free lists
    uint32_t               *freeTextureList;
    uint32_t               *freeSamplerList;
    uint32_t               *freeBufferList;
    uint32_t               *freeImageList;
    
    uint32_t                numFreeTextures;
    uint32_t                numFreeSamplers;
    uint32_t                numFreeBuffers;
    uint32_t                numFreeImages;
    
    // Update tracking
    qboolean                needsUpdate;
    uint32_t               *dirtyTextures;
    uint32_t               *dirtySamplers;
    uint32_t               *dirtyBuffers;
    uint32_t               *dirtyImages;
    uint32_t                numDirtyTextures;
    uint32_t                numDirtySamplers;
    uint32_t                numDirtyBuffers;
    uint32_t                numDirtyImages;
} bindlessPool_t;

// Global bindless state
typedef struct bindlessState_s {
    qboolean                supported;
    qboolean                descriptorIndexingSupported;
    qboolean                runtimeDescriptorArraySupported;
    qboolean                updateAfterBindSupported;
    
    bindlessPool_t          pool;
    
    // Capabilities
    uint32_t                maxDescriptorSetSampledImages;
    uint32_t                maxDescriptorSetStorageImages;
    uint32_t                maxDescriptorSetStorageBuffers;
    uint32_t                maxDescriptorSetSamplers;
    
    // Statistics
    uint32_t                totalTextureMemory;
    uint32_t                totalBufferMemory;
    uint32_t                textureUpdates;
    uint32_t                bufferUpdates;
    
    // Dummy resources for unbound descriptors
    VkImage                 dummyImage;
    VkImageView             dummyImageView;
    VkDeviceMemory          dummyImageMemory;
    VkSampler               dummySampler;
} bindlessState_t;

// Bindless material descriptor (for fast shader access)
typedef struct bindlessMaterial_s {
    uint32_t    diffuseTexture;
    uint32_t    normalTexture;
    uint32_t    specularTexture;
    uint32_t    emissiveTexture;
    
    uint32_t    diffuseSampler;
    uint32_t    normalSampler;
    uint32_t    specularSampler;
    uint32_t    emissiveSampler;
    
    uint32_t    materialBuffer;
    uint32_t    padding[3];
} bindlessMaterial_t;

// API functions
qboolean R_InitBindlessResources( void );
void R_ShutdownBindlessResources( void );

// Texture management
uint32_t R_RegisterBindlessTexture( VkImageView imageView, VkSampler sampler );
uint32_t R_RegisterBindlessTextureWithName( VkImageView imageView, VkSampler sampler, const char *name );
void R_UnregisterBindlessTexture( uint32_t handle );
qboolean R_UpdateBindlessTexture( uint32_t handle, VkImageView imageView );

// Sampler management
uint32_t R_RegisterBindlessSampler( VkSampler sampler );
void R_UnregisterBindlessSampler( uint32_t handle );
uint32_t R_GetDefaultBindlessSampler( void );

// Buffer management
uint32_t R_RegisterBindlessBuffer( VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range );
void R_UnregisterBindlessBuffer( uint32_t handle );
qboolean R_UpdateBindlessBuffer( uint32_t handle, VkDeviceSize offset, VkDeviceSize range );

// Storage image management
uint32_t R_RegisterBindlessImage( VkImageView imageView, VkImageLayout layout );
void R_UnregisterBindlessImage( uint32_t handle );
qboolean R_UpdateBindlessImage( uint32_t handle, VkImageLayout layout );

// Descriptor updates
void R_UpdateBindlessDescriptors( void );
void R_FlushBindlessUpdates( VkCommandBuffer cmd );

// Material management
void R_CreateBindlessMaterial( bindlessMaterial_t *material, shader_t *shader );
uint32_t R_UploadBindlessMaterial( const bindlessMaterial_t *material );

// Utility functions
VkDescriptorSet R_GetBindlessDescriptorSet( void );
VkDescriptorSetLayout R_GetBindlessDescriptorSetLayout( void );
qboolean R_IsBindlessSupported( void );

// Resource queries
qboolean R_GetBindlessTextureInfo( uint32_t handle, bindlessTexture_t *info );
qboolean R_GetBindlessBufferInfo( uint32_t handle, bindlessBuffer_t *info );
uint32_t R_FindBindlessTextureByName( const char *name );

// Statistics
void R_GetBindlessStats( uint32_t *numTextures, uint32_t *numBuffers, 
                         uint32_t *textureMemory, uint32_t *bufferMemory );
void R_PrintBindlessStats( void );

// Debug
void R_ValidateBindlessResources( void );
void R_DumpBindlessResources( const char *filename );

#endif // __TR_BINDLESS_H