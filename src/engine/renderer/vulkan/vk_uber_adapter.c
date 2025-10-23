/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.
===========================================================================
*/

#include "../core/tr_local.h"
#include "vk.h"
#include "vk_shader.h"

// Maximum vertices we can convert in one draw call
#define MAX_UBER_VERTICES 65536

// Vertex conversion buffer
typedef struct {
    VkBuffer        buffer;
    VkDeviceMemory  memory;
    vkVertex_t      *data;         // Mapped memory pointer
    uint32_t        size;          // Buffer size in bytes
    uint32_t        used;          // Current offset for dynamic updates
    qboolean        initialized;
} uberVertexBuffer_t;

static uberVertexBuffer_t vertexBuffer;

// External function declaration
extern uint32_t vk_find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties);

/*
================
VK_CreateUberVertexBuffer

Create a buffer for converted vertex data
================
*/
static qboolean VK_CreateUberVertexBuffer(void) {
    VkBufferCreateInfo bufferInfo;
    VkMemoryRequirements memRequirements;
    VkMemoryAllocateInfo allocInfo;
    VkResult result;

    if (vertexBuffer.initialized) {
        return qtrue;
    }

    // Calculate buffer size
    vertexBuffer.size = MAX_UBER_VERTICES * sizeof(vkVertex_t);

    // Create buffer
    Com_Memset(&bufferInfo, 0, sizeof(bufferInfo));
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = vertexBuffer.size;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    result = vkCreateBuffer(vk.device, &bufferInfo, NULL, &vertexBuffer.buffer);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "VK_CreateUberVertexBuffer: Failed to create buffer\n");
        return qfalse;
    }

    // Get memory requirements
    vkGetBufferMemoryRequirements(vk.device, vertexBuffer.buffer, &memRequirements);

    // Allocate memory (use HOST_VISIBLE for CPU updates)
    Com_Memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vk_find_memory_type(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    result = vkAllocateMemory(vk.device, &allocInfo, NULL, &vertexBuffer.memory);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "VK_CreateUberVertexBuffer: Failed to allocate memory\n");
        vkDestroyBuffer(vk.device, vertexBuffer.buffer, NULL);
        return qfalse;
    }

    // Bind memory to buffer
    result = vkBindBufferMemory(vk.device, vertexBuffer.buffer, vertexBuffer.memory, 0);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "VK_CreateUberVertexBuffer: Failed to bind memory\n");
        vkFreeMemory(vk.device, vertexBuffer.memory, NULL);
        vkDestroyBuffer(vk.device, vertexBuffer.buffer, NULL);
        return qfalse;
    }

    // Map memory for CPU access
    result = vkMapMemory(vk.device, vertexBuffer.memory, 0, vertexBuffer.size, 0, (void**)&vertexBuffer.data);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "VK_CreateUberVertexBuffer: Failed to map memory\n");
        vkFreeMemory(vk.device, vertexBuffer.memory, NULL);
        vkDestroyBuffer(vk.device, vertexBuffer.buffer, NULL);
        return qfalse;
    }

    vertexBuffer.initialized = qtrue;
    vertexBuffer.used = 0;

    ri.Printf(PRINT_DEVELOPER, "Created uber shader vertex buffer (%d KB)\n", vertexBuffer.size / 1024);
    return qtrue;
}

/*
================
VK_DestroyUberVertexBuffer

Clean up vertex buffer
================
*/
void VK_DestroyUberVertexBuffer(void) {
    if (!vertexBuffer.initialized) {
        return;
    }

    if (vertexBuffer.data) {
        vkUnmapMemory(vk.device, vertexBuffer.memory);
        vertexBuffer.data = NULL;
    }

    if (vertexBuffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vk.device, vertexBuffer.memory, NULL);
        vertexBuffer.memory = VK_NULL_HANDLE;
    }

    if (vertexBuffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk.device, vertexBuffer.buffer, NULL);
        vertexBuffer.buffer = VK_NULL_HANDLE;
    }

    vertexBuffer.initialized = qfalse;
}

/*
================
VK_ConvertVerticesForUberShader

Convert separate vertex arrays to interleaved format
Expands vertices in index order for non-indexed drawing
Returns the offset in the vertex buffer where data was written
================
*/
uint32_t VK_ConvertVerticesForUberShader(uint32_t numIndexes, uint32_t *outVertexCount) {
    uint32_t k;
    vkVertex_t *dst;
    uint32_t offset;

    // Sanity checks
    // Compile-time check that vkVertex_t is 4-byte aligned
    // Using a compile-time expression that will fail if false
    typedef char vkVertex_alignment_check[(sizeof(vkVertex_t) % 4 == 0) ? 1 : -1];
    if ((numIndexes % 3) != 0) {
        ri.Printf(PRINT_WARNING, "Non-multiple-of-3 index count: %u\n", numIndexes);
    }

    if (!vertexBuffer.initialized) {
        if (!VK_CreateUberVertexBuffer()) {
            return 0;
        }
    }

    // Check if we have enough space
    if (numIndexes > MAX_UBER_VERTICES) {
        ri.Printf(PRINT_WARNING, "VK_ConvertVerticesForUberShader: Too many indexes (%d > %d)\n",
                  numIndexes, MAX_UBER_VERTICES);
        return 0;
    }

    // Check for buffer overflow and wrap if needed
    if (vertexBuffer.used + numIndexes * sizeof(vkVertex_t) > vertexBuffer.size) {
        vertexBuffer.used = 0;  // Wrap to beginning
    }

    offset = vertexBuffer.used;
    dst = (vkVertex_t*)((byte*)vertexBuffer.data + offset);

    // Expand vertices in index order for non-indexed drawing
    for (k = 0; k < numIndexes; k++) {
        // Get the actual vertex index from the index buffer
        const uint32_t i = (uint32_t)tess.indexes[k];
        vkVertex_t *v = &dst[k];

        // Position (required)
        v->position[0] = tess.xyz[i][0];
        v->position[1] = tess.xyz[i][1];
        v->position[2] = tess.xyz[i][2];

        // Texture coordinates (stage 0)
        v->texCoord0[0] = tess.texCoords[0][i][0];
        v->texCoord0[1] = tess.texCoords[0][i][1];

        // Lightmap texture coordinates (stage 1)
        v->texCoord1[0] = tess.texCoords[1][i][0];
        v->texCoord1[1] = tess.texCoords[1][i][1];

        // Normal (tess.normal is vec4_t but we only need xyz)
        v->normal[0] = tess.normal[i][0];
        v->normal[1] = tess.normal[i][1];
        v->normal[2] = tess.normal[i][2];

        // Tangent (for normal mapping)
        // We don't have tangent data in tess, so calculate a default
        // In a proper implementation, tangents would be calculated from the mesh
        v->tangent[0] = 1.0f;
        v->tangent[1] = 0.0f;
        v->tangent[2] = 0.0f;
        v->tangent[3] = 1.0f;  // Handedness for bitangent

        // Color - use first color array (stage 0)
        v->color[0] = tess.svars.colors[0][i].rgba[0];
        v->color[1] = tess.svars.colors[0][i].rgba[1];
        v->color[2] = tess.svars.colors[0][i].rgba[2];
        v->color[3] = tess.svars.colors[0][i].rgba[3];
    }

    // Update buffer usage
    vertexBuffer.used += numIndexes * sizeof(vkVertex_t);

    // Ensure changes are visible to GPU (coherent memory)
    // If using non-coherent memory, we would need vkFlushMappedMemoryRanges here

    *outVertexCount = numIndexes;
    return offset;
}

/*
================
VK_BindUberVertexBuffer

Bind the uber shader vertex buffer at the specified offset
================
*/
void VK_BindUberVertexBuffer(VkCommandBuffer cmd, uint32_t offset) {
    VkDeviceSize vkOffset = (VkDeviceSize)offset;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.buffer, &vkOffset);
}

/*
================
VK_GetUberVertexBuffer

Get the vertex buffer handle
================
*/
VkBuffer VK_GetUberVertexBuffer(void) {
    return vertexBuffer.buffer;
}

/*
================
VK_InitUberAdapter

Initialize the uber shader adapter system
================
*/
void VK_InitUberAdapter(void) {
    Com_Memset(&vertexBuffer, 0, sizeof(vertexBuffer));

    // Pre-create the buffer if uber shader is enabled
    cvar_t *r_useUberShader = ri.Cvar_Get("r_useUberShader", "0", 0);
    if (r_useUberShader && r_useUberShader->integer) {
        VK_CreateUberVertexBuffer();
    }
}

/*
================
VK_ShutdownUberAdapter

Shutdown the uber shader adapter system
================
*/
void VK_ShutdownUberAdapter(void) {
    VK_DestroyUberVertexBuffer();
}