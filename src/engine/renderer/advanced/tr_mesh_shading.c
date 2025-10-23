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
// tr_mesh_shading.c - Mesh shading pipeline implementation

#include "tr_mesh_shading.h"
#include "tr_gpu_driven.h"
#include "../vulkan/vk.h"
#include "../core/tr_common_utils.h"
#include <float.h>

// Mesh shading extension function pointers
static PFN_vkCmdDrawMeshTasksNV qvkCmdDrawMeshTasksNV;
static PFN_vkCmdDrawMeshTasksIndirectNV qvkCmdDrawMeshTasksIndirectNV;

// Pipeline cache for different shader combinations
typedef struct meshPipelineKey_s {
    shader_t    *shader;
    uint32_t    stateFlags;
    VkPipeline  pipeline;
} meshPipelineKey_t;

static meshPipelineKey_t meshPipelines[MAX_SHADERS];
static int numMeshPipelines;

/*
================
R_InitMeshShading

Initialize mesh shading pipeline
================
*/
qboolean R_InitMeshShading( void ) {
    VkPhysicalDeviceProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
    };
    
    VkPhysicalDeviceMeshShaderPropertiesNV meshProps = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_NV
    };
    props2.pNext = &meshProps;
    
    vkGetPhysicalDeviceProperties2( vk.physical_device, &props2 );
    
    // Check mesh shader support
    if ( meshProps.maxDrawMeshTasksCount == 0 ) {
        ri.Printf( PRINT_WARNING, "Mesh shading not supported by GPU\n" );
        return qfalse;
    }
    
    // Get extension functions
    qvkCmdDrawMeshTasksNV = (PFN_vkCmdDrawMeshTasksNV)
        vkGetDeviceProcAddr( vk.device, "vkCmdDrawMeshTasksNV" );
    qvkCmdDrawMeshTasksIndirectNV = (PFN_vkCmdDrawMeshTasksIndirectNV)
        vkGetDeviceProcAddr( vk.device, "vkCmdDrawMeshTasksIndirectNV" );
    
    if ( !qvkCmdDrawMeshTasksNV || !qvkCmdDrawMeshTasksIndirectNV ) {
        ri.Printf( PRINT_WARNING, "Failed to get mesh shading functions\n" );
        return qfalse;
    }
    
    // Store mesh shader limits
    gpuContext.meshShading.maxMeshOutputVertices = meshProps.maxMeshOutputVertices;
    gpuContext.meshShading.maxMeshOutputPrimitives = meshProps.maxMeshOutputPrimitives;
    gpuContext.meshShading.maxMeshWorkGroupSize[0] = meshProps.maxMeshWorkGroupSize[0];
    gpuContext.meshShading.maxMeshWorkGroupSize[1] = meshProps.maxMeshWorkGroupSize[1];
    gpuContext.meshShading.maxMeshWorkGroupSize[2] = meshProps.maxMeshWorkGroupSize[2];
    
    // Create mesh shading pipelines
    R_CreateMeshShadingPipelines();
    
    // Allocate meshlet buffer
    R_CreateGPUBuffer( &gpuContext.meshShading.meshletBuffer,
                      &gpuContext.meshShading.meshletMemory,
                      MESHLET_BUFFER_SIZE,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
    
    gpuContext.meshShading.meshlets = ri.Hunk_Alloc( sizeof( gpuMeshlet_t ) * MAX_MESHLETS, h_low );
    
    ri.Printf( PRINT_ALL, "Mesh shading initialized (max vertices: %d, max primitives: %d)\n",
              meshProps.maxMeshOutputVertices, meshProps.maxMeshOutputPrimitives );
    
    return qtrue;
}

/*
================
R_ShutdownMeshShading

Shutdown mesh shading pipeline
================
*/
void R_ShutdownMeshShading( void ) {
    R_DestroyMeshShadingPipelines();
    
    if ( gpuContext.meshShading.meshletBuffer ) {
        vkDestroyBuffer( vk.device, gpuContext.meshShading.meshletBuffer, NULL );
        vkFreeMemory( vk.device, gpuContext.meshShading.meshletMemory, NULL );
    }
}

/*
================
R_BuildMeshlets

Build meshlets from triangle surface
================
*/
void R_BuildMeshlets( srfTriangles_t *surface ) {
    meshletBuildParams_t params = {
        .maxVertices = MESHLET_MAX_VERTICES,
        .maxPrimitives = MESHLET_MAX_PRIMITIVES,
        .coneWeight = 0.5f,
        .optimizeVertexCache = qtrue
    };
    
    meshShadingSurface_t *meshSurface = ri.Hunk_Alloc( sizeof( meshShadingSurface_t ), h_low );
    
    // Build meshlets from triangle data
    R_BuildMeshletsFromIndices( surface->verts, surface->numVerts, sizeof( drawVert_t ),
                               surface->indexes, surface->numIndexes,
                               &params, meshSurface );
    
    // Optimize meshlets
    R_OptimizeMeshlets( meshSurface );
    
    // Build LODs
    R_BuildMeshletLODs( meshSurface );
    
    // Allocate GPU buffers
    R_AllocateMeshShadingBuffers( meshSurface );
    
    // Upload to GPU
    R_UploadMeshShadingData( meshSurface );
    
    // Store in surface
    // TODO: Add meshShadingData member to srfTriangles_t or use a separate mapping
    // surface->meshShadingData = meshSurface;
}

/*
================
R_BuildMeshletsFromIndices

Build meshlets from indexed geometry
================
*/
void R_BuildMeshletsFromIndices( const void *vertices, uint32_t vertexCount, uint32_t vertexStride,
                                const uint16_t *indices, uint32_t indexCount,
                                meshletBuildParams_t *params,
                                meshShadingSurface_t *output ) {
    uint32_t maxMeshlets = ( indexCount / 3 + params->maxPrimitives - 1 ) / params->maxPrimitives;
    
    output->meshlets = ri.Hunk_Alloc( sizeof( meshletDesc_t ) * maxMeshlets, h_low );
    output->vertices = ri.Hunk_Alloc( sizeof( meshletVertex_t ) * vertexCount, h_low );
    output->primitives = ri.Hunk_Alloc( sizeof( meshletPrimitive_t ) * indexCount / 3, h_low );
    
    // Convert vertices to meshlet format
    const byte *vertexData = (const byte *)vertices;
    for ( uint32_t i = 0; i < vertexCount; i++ ) {
        const drawVert_t *dv = (const drawVert_t *)( vertexData + i * vertexStride );
        meshletVertex_t *mv = &output->vertices[i];
        
        VectorCopy( dv->xyz, mv->position );
        VectorCopy( dv->normal, mv->normal );
        mv->texCoord[0] = dv->st[0];
        mv->texCoord[1] = dv->st[1];
        // Tangent calculation would go here
        VectorSet4( mv->tangent, 1, 0, 0, 1 );
        // Copy color data - assuming color is byte array
        Com_Memcpy( &mv->color, &dv->color, sizeof(color4ub_t) );
    }
    
    // Build meshlets using greedy algorithm
    uint32_t currentMeshlet = 0;
    uint32_t currentVertex = 0;
    uint32_t currentPrimitive = 0;
    
    uint32_t meshletVertexCount = 0;
    uint32_t meshletPrimitiveCount = 0;
    uint32_t meshletVertices[MESHLET_MAX_VERTICES];
    uint32_t meshletVertexMap[65536];  // Map from global to local vertex index
    
    Com_Memset( meshletVertexMap, 0xff, sizeof( meshletVertexMap ) );
    
    for ( uint32_t i = 0; i < indexCount; i += 3 ) {
        uint32_t tri[3] = { indices[i], indices[i + 1], indices[i + 2] };
        
        // Check if triangle fits in current meshlet
        uint32_t newVertices = 0;
        for ( int j = 0; j < 3; j++ ) {
            if ( meshletVertexMap[tri[j]] == 0xffffffff ) {
                newVertices++;
            }
        }
        
        if ( meshletVertexCount + newVertices > params->maxVertices ||
             meshletPrimitiveCount >= params->maxPrimitives ) {
            // Finalize current meshlet
            meshletDesc_t *meshlet = &output->meshlets[currentMeshlet];
            meshlet->vertexOffset = currentVertex;
            meshlet->vertexCount = meshletVertexCount;
            meshlet->primitiveOffset = currentPrimitive;
            meshlet->primitiveCount = meshletPrimitiveCount;
            
            // Compute culling data
            R_ComputeMeshletCullData( meshlet, output->vertices );
            
            currentMeshlet++;
            currentVertex += meshletVertexCount;
            currentPrimitive += meshletPrimitiveCount;
            
            // Reset for next meshlet
            meshletVertexCount = 0;
            meshletPrimitiveCount = 0;
            Com_Memset( meshletVertexMap, 0xff, sizeof( meshletVertexMap ) );
        }
        
        // Add triangle to current meshlet
        meshletPrimitive_t *prim = &output->primitives[currentPrimitive + meshletPrimitiveCount];
        
        for ( int j = 0; j < 3; j++ ) {
            if ( meshletVertexMap[tri[j]] == 0xffffffff ) {
                meshletVertexMap[tri[j]] = meshletVertexCount;
                meshletVertices[meshletVertexCount] = tri[j];
                meshletVertexCount++;
            }
            prim->indices[j] = meshletVertexMap[tri[j]];
        }
        
        meshletPrimitiveCount++;
    }
    
    // Finalize last meshlet
    if ( meshletPrimitiveCount > 0 ) {
        meshletDesc_t *meshlet = &output->meshlets[currentMeshlet];
        meshlet->vertexOffset = currentVertex;
        meshlet->vertexCount = meshletVertexCount;
        meshlet->primitiveOffset = currentPrimitive;
        meshlet->primitiveCount = meshletPrimitiveCount;
        
        R_ComputeMeshletCullData( meshlet, output->vertices );
        currentMeshlet++;
    }
    
    output->numMeshlets = currentMeshlet;
    output->numVertices = vertexCount;
    output->numPrimitives = indexCount / 3;
}

/*
================
R_ComputeMeshletCullData

Compute culling data for a meshlet
================
*/
void R_ComputeMeshletCullData( meshletDesc_t *meshlet, const meshletVertex_t *vertices ) {
    // Initialize bounds
    VectorSet( meshlet->mins, FLT_MAX, FLT_MAX, FLT_MAX );
    VectorSet( meshlet->maxs, -FLT_MAX, -FLT_MAX, -FLT_MAX );
    VectorClear( meshlet->center );
    
    // Compute bounds and center
    for ( uint32_t i = 0; i < meshlet->vertexCount; i++ ) {
        const meshletVertex_t *v = &vertices[meshlet->vertexOffset + i];
        
        for ( int j = 0; j < 3; j++ ) {
            if ( v->position[j] < meshlet->mins[j] ) {
                meshlet->mins[j] = v->position[j];
            }
            if ( v->position[j] > meshlet->maxs[j] ) {
                meshlet->maxs[j] = v->position[j];
            }
        }
        
        VectorAdd( meshlet->center, v->position, meshlet->center );
    }
    
    VectorScale( meshlet->center, 1.0f / meshlet->vertexCount, meshlet->center );
    
    // Compute bounding sphere radius
    meshlet->radius = 0;
    for ( uint32_t i = 0; i < meshlet->vertexCount; i++ ) {
        const meshletVertex_t *v = &vertices[meshlet->vertexOffset + i];
        float dist = Distance( meshlet->center, v->position );
        if ( dist > meshlet->radius ) {
            meshlet->radius = dist;
        }
    }
    
    // Compute cone for backface culling
    VectorClear( meshlet->coneAxis );
    
    // Average normals to get cone axis
    for ( uint32_t i = 0; i < meshlet->vertexCount; i++ ) {
        const meshletVertex_t *v = &vertices[meshlet->vertexOffset + i];
        VectorAdd( meshlet->coneAxis, v->normal, meshlet->coneAxis );
    }
    
    VectorNormalize( meshlet->coneAxis );
    
    // Compute cone cutoff angle
    meshlet->coneCutoff = 1.0f;
    for ( uint32_t i = 0; i < meshlet->vertexCount; i++ ) {
        const meshletVertex_t *v = &vertices[meshlet->vertexOffset + i];
        float dot = DotProduct( v->normal, meshlet->coneAxis );
        if ( dot < meshlet->coneCutoff ) {
            meshlet->coneCutoff = dot;
        }
    }
}

/*
================
R_OptimizeMeshlets

Optimize meshlet vertex ordering for GPU cache
================
*/
void R_OptimizeMeshlets( meshShadingSurface_t *surface ) {
    // Implement vertex cache optimization
    // This would reorder vertices within each meshlet to improve GPU cache utilization
    
    for ( uint32_t i = 0; i < surface->numMeshlets; i++ ) {
        meshletDesc_t *meshlet = &surface->meshlets[i];
        
        // Simple optimization: sort vertices by first use in primitives
        // More sophisticated algorithms like Forsyth's can be implemented
        
        // Track vertex usage
        uint32_t vertexFirstUse[MESHLET_MAX_VERTICES];
        Com_Memset( vertexFirstUse, 0xff, sizeof( vertexFirstUse ) );
        
        for ( uint32_t p = 0; p < meshlet->primitiveCount; p++ ) {
            meshletPrimitive_t *prim = &surface->primitives[meshlet->primitiveOffset + p];
            for ( int v = 0; v < 3; v++ ) {
                if ( vertexFirstUse[prim->indices[v]] == 0xffffffff ) {
                    vertexFirstUse[prim->indices[v]] = p;
                }
            }
        }
        
        // Sort vertices by first use
        // Implementation would go here
    }
}

/*
================
R_BuildMeshletLODs

Build LOD hierarchy for meshlets
================
*/
void R_BuildMeshletLODs( meshShadingSurface_t *surface ) {
    // Simple LOD: group meshlets by distance
    surface->lodCount = 4;
    surface->lodMeshletOffsets = ri.Hunk_Alloc( sizeof( uint32_t ) * surface->lodCount, h_low );
    surface->lodDistances = ri.Hunk_Alloc( sizeof( float ) * surface->lodCount, h_low );
    
    // Setup LOD distances
    surface->lodDistances[0] = 0.0f;
    surface->lodDistances[1] = 100.0f;
    surface->lodDistances[2] = 500.0f;
    surface->lodDistances[3] = 1000.0f;
    
    // For now, use all meshlets at all LODs
    for ( uint32_t i = 0; i < surface->lodCount; i++ ) {
        surface->lodMeshletOffsets[i] = 0;
    }
    
    // More sophisticated LOD generation would simplify meshlets at each level
}

/*
================
R_AllocateMeshShadingBuffers

Allocate GPU buffers for mesh shading data
================
*/
void R_AllocateMeshShadingBuffers( meshShadingSurface_t *surface ) {
    VkDeviceSize meshletSize = surface->numMeshlets * sizeof( meshletDesc_t );
    VkDeviceSize vertexSize = surface->numVertices * sizeof( meshletVertex_t );
    VkDeviceSize primitiveSize = surface->numPrimitives * sizeof( meshletPrimitive_t );
    VkDeviceSize totalSize = meshletSize + vertexSize + primitiveSize;
    
    // Create combined buffer for all data
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = totalSize,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    
    VK_CHECK( vkCreateBuffer( vk.device, &bufferInfo, NULL, &surface->meshletBuffer ) );
    
    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements( vk.device, surface->meshletBuffer, &memReqs );
    
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = vk_find_memory_type( memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
    };
    
    VK_CHECK( vkAllocateMemory( vk.device, &allocInfo, NULL, &surface->bufferMemory ) );
    VK_CHECK( vkBindBufferMemory( vk.device, surface->meshletBuffer, surface->bufferMemory, 0 ) );
    
    // Setup buffer offsets
    surface->vertexBuffer = surface->meshletBuffer;
    surface->primitiveBuffer = surface->meshletBuffer;
}

/*
================
R_FreeMeshShadingBuffers

Free GPU buffers for mesh shading data
================
*/
void R_FreeMeshShadingBuffers( meshShadingSurface_t *surface ) {
    if ( surface->meshletBuffer ) {
        vkDestroyBuffer( vk.device, surface->meshletBuffer, NULL );
    }
    
    if ( surface->bufferMemory ) {
        vkFreeMemory( vk.device, surface->bufferMemory, NULL );
    }
}

/*
================
R_UploadMeshShadingData

Upload mesh shading data to GPU
================
*/
void R_UploadMeshShadingData( meshShadingSurface_t *surface ) {
    VkDeviceSize meshletSize = surface->numMeshlets * sizeof( meshletDesc_t );
    VkDeviceSize vertexSize = surface->numVertices * sizeof( meshletVertex_t );
    VkDeviceSize primitiveSize = surface->numPrimitives * sizeof( meshletPrimitive_t );
    
    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkDeviceSize totalSize = meshletSize + vertexSize + primitiveSize;
    
    R_CreateGPUBuffer( &stagingBuffer, &stagingMemory, totalSize,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
    
    // Map and copy data
    void *data;
    vkMapMemory( vk.device, stagingMemory, 0, totalSize, 0, &data );
    
    memcpy( data, surface->meshlets, meshletSize );
    memcpy( (byte *)data + meshletSize, surface->vertices, vertexSize );
    memcpy( (byte *)data + meshletSize + vertexSize, surface->primitives, primitiveSize );
    
    vkUnmapMemory( vk.device, stagingMemory );
    
    // Copy to GPU buffer
    VkCommandBuffer cmd = vk.cmd->command_buffer;
    vk_begin_command_buffer( cmd );
    
    VkBufferCopy copyRegion = { 0, 0, totalSize };
    vkCmdCopyBuffer( cmd, stagingBuffer, surface->meshletBuffer, 1, &copyRegion );
    
    vk_end_command_buffer( cmd );
    
    // Cleanup staging buffer
    vkDestroyBuffer( vk.device, stagingBuffer, NULL );
    vkFreeMemory( vk.device, stagingMemory, NULL );
}

/*
================
R_DrawMeshShading

Draw surface using mesh shading
================
*/
void R_DrawMeshShading( VkCommandBuffer commandBuffer, const drawSurf_t *drawSurf ) {
    // TODO: Get mesh shading data from surface
    meshShadingSurface_t *surface = NULL;
    
    if ( !surface || !qvkCmdDrawMeshTasksNV ) {
        return;
    }
    
    // Get appropriate pipeline
    // Get appropriate pipeline
    uint32_t stateFlags = 0; // TODO: Get state flags
    VkPipeline pipeline = R_GetMeshShadingPipeline( drawSurf->shader, stateFlags );
    vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
    
    // Setup push constants
    meshShaderPushConstants_t pushConstants;
    MatrixCopy( tr.viewParms.projectionMatrix, pushConstants.mvpMatrix );
    MatrixCopy( drawSurf->modelMatrix, pushConstants.modelMatrix );
    MatrixCopy( drawSurf->normalMatrix, pushConstants.normalMatrix );
    VectorCopy4( tr.viewParms.or.origin, pushConstants.viewOrigin );
    pushConstants.meshletOffset = 0;
    pushConstants.meshletCount = surface->numMeshlets;
    pushConstants.materialID = drawSurf->materialID;
    pushConstants.flags = drawSurf->dlightBits;
    
    vkCmdPushConstants( commandBuffer, gpuContext.meshShading.pipelineLayout,
                       VK_SHADER_STAGE_TASK_BIT_NV | VK_SHADER_STAGE_MESH_BIT_NV,
                       0, sizeof( pushConstants ), &pushConstants );
    
    // Bind descriptor sets
    vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            gpuContext.meshShading.pipelineLayout, 0, 1,
                            &gpuContext.meshShading.descriptorSet, 0, NULL );
    
    // Draw mesh tasks
    uint32_t taskCount = ( surface->numMeshlets + MESHLET_LOCAL_SIZE - 1 ) / MESHLET_LOCAL_SIZE;
    qvkCmdDrawMeshTasksNV( commandBuffer, taskCount, 0 );
}

/*
================
R_DrawMeshShadingSurface

Draw mesh shading surface with specific transform
================
*/
void R_DrawMeshShadingSurface( VkCommandBuffer commandBuffer, meshShadingSurface_t *surface,
                              const mat4_t modelMatrix ) {
    drawSurf_t drawSurf;
    Com_Memset( &drawSurf, 0, sizeof( drawSurf ) );
    
    // TODO: Store mesh shading data in drawSurf
    // drawSurf.meshShadingData = surface;
    MatrixCopy( modelMatrix, drawSurf.modelMatrix );
    MatrixCopy( modelMatrix, drawSurf.normalMatrix );  // Should calculate proper normal matrix
    drawSurf.shader = tr.defaultShader;
    
    R_DrawMeshShading( commandBuffer, &drawSurf );
}

/*
================
R_GetMeshShadingPipeline

Get or create mesh shading pipeline for shader
================
*/
VkPipeline R_GetMeshShadingPipeline( shader_t *shader, uint32_t stateFlags ) {
    // Search existing pipelines
    for ( int i = 0; i < numMeshPipelines; i++ ) {
        if ( meshPipelines[i].shader == shader &&
             meshPipelines[i].stateFlags == stateFlags ) {
            return meshPipelines[i].pipeline;
        }
    }
    
    // Create new pipeline
    if ( numMeshPipelines >= MAX_SHADERS ) {
        ri.Error( ERR_DROP, "R_GetMeshShadingPipeline: MAX_SHADERS hit" );
        return VK_NULL_HANDLE;
    }
    
    // Pipeline creation would go here
    VkPipeline pipeline = VK_NULL_HANDLE;  // Would create actual pipeline
    
    meshPipelines[numMeshPipelines].shader = shader;
    meshPipelines[numMeshPipelines].stateFlags = stateFlags;
    meshPipelines[numMeshPipelines].pipeline = pipeline;
    numMeshPipelines++;
    
    return pipeline;
}

/*
================
R_CreateMeshShadingPipelines

Create default mesh shading pipelines
================
*/
void R_CreateMeshShadingPipelines( void ) {
    // Load task and mesh shaders
    uint32_t taskCodeSize, meshCodeSize;
    uint32_t *taskSpirv = R_LoadSPIRV( "compute/task_shader.spv", &taskCodeSize );
    uint32_t *meshSpirv = R_LoadSPIRV( "compute/mesh_shader.spv", &meshCodeSize );
    
    if ( !taskSpirv || !meshSpirv ) {
        ri.Printf( PRINT_WARNING, "Failed to load mesh shading shaders\n" );
        return;
    }
    
    // Create shader modules
    VkShaderModuleCreateInfo moduleInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
    };
    
    VkShaderModule taskModule, meshModule;
    
    moduleInfo.codeSize = taskCodeSize;
    moduleInfo.pCode = taskSpirv;
    VK_CHECK( vkCreateShaderModule( vk.device, &moduleInfo, NULL, &taskModule ) );
    
    moduleInfo.codeSize = meshCodeSize;
    moduleInfo.pCode = meshSpirv;
    VK_CHECK( vkCreateShaderModule( vk.device, &moduleInfo, NULL, &meshModule ) );
    
    // Create descriptor set layout
    VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_TASK_BIT_NV | VK_SHADER_STAGE_MESH_BIT_NV
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_MESH_BIT_NV
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_MESH_BIT_NV
        }
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = ARRAY_LEN( bindings ),
        .pBindings = bindings
    };
    
    VK_CHECK( vkCreateDescriptorSetLayout( vk.device, &layoutInfo, NULL,
                                          &gpuContext.meshShading.descriptorSetLayout ) );
    
    // Create pipeline layout
    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_TASK_BIT_NV | VK_SHADER_STAGE_MESH_BIT_NV,
        .offset = 0,
        .size = sizeof( meshShaderPushConstants_t )
    };
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &gpuContext.meshShading.descriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };
    
    VK_CHECK( vkCreatePipelineLayout( vk.device, &pipelineLayoutInfo, NULL,
                                     &gpuContext.meshShading.pipelineLayout ) );
    
    // Cleanup shader modules
    vkDestroyShaderModule( vk.device, taskModule, NULL );
    vkDestroyShaderModule( vk.device, meshModule, NULL );
}

/*
================
R_DestroyMeshShadingPipelines

Destroy mesh shading pipelines
================
*/
void R_DestroyMeshShadingPipelines( void ) {
    for ( int i = 0; i < numMeshPipelines; i++ ) {
        if ( meshPipelines[i].pipeline ) {
            vkDestroyPipeline( vk.device, meshPipelines[i].pipeline, NULL );
        }
    }
    
    if ( gpuContext.meshShading.pipelineLayout ) {
        vkDestroyPipelineLayout( vk.device, gpuContext.meshShading.pipelineLayout, NULL );
    }
    
    if ( gpuContext.meshShading.descriptorSetLayout ) {
        vkDestroyDescriptorSetLayout( vk.device, gpuContext.meshShading.descriptorSetLayout, NULL );
    }
    
    numMeshPipelines = 0;
}

/*
================
R_DrawMeshletBounds

Debug visualization of meshlet bounds
================
*/
void R_DrawMeshletBounds( meshShadingSurface_t *surface, const mat4_t modelMatrix ) {
    for ( uint32_t i = 0; i < surface->numMeshlets; i++ ) {
        meshletDesc_t *meshlet = &surface->meshlets[i];
        
        vec3_t worldMins, worldMaxs;
        MatrixTransformPoint( modelMatrix, meshlet->mins, worldMins );
        MatrixTransformPoint( modelMatrix, meshlet->maxs, worldMaxs );
        
        vec4_t color = { 0, 1, 0, 0.5f };
        RB_AddDebugBox( worldMins, worldMaxs, color );
    }
}

/*
================
R_DrawMeshletCones

Debug visualization of meshlet cones
================
*/
void R_DrawMeshletCones( meshShadingSurface_t *surface, const mat4_t modelMatrix ) {
    for ( uint32_t i = 0; i < surface->numMeshlets; i++ ) {
        meshletDesc_t *meshlet = &surface->meshlets[i];
        
        vec3_t worldCenter, worldAxis;
        MatrixTransformPoint( modelMatrix, meshlet->center, worldCenter );
        MatrixTransformNormal( modelMatrix, meshlet->coneAxis, worldAxis );
        
        vec3_t endPoint;
        VectorMA( worldCenter, meshlet->radius, worldAxis, endPoint );
        
        vec4_t color = { 1, 1, 0, 1 };
        RB_AddDebugLine( worldCenter, endPoint, color );
    }
}