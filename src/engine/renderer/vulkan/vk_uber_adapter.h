/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

This file is part of Quake III Arena source code.
===========================================================================
*/

#ifndef VK_UBER_ADAPTER_H
#define VK_UBER_ADAPTER_H

// Initialize/shutdown the adapter system
void VK_InitUberAdapter(void);
void VK_ShutdownUberAdapter(void);

// Convert vertices from separate arrays to interleaved format
// Returns offset in buffer where vertices were written
uint32_t VK_ConvertVerticesForUberShader(uint32_t numIndexes, uint32_t *outVertexCount);

// Bind the uber vertex buffer for drawing
void VK_BindUberVertexBuffer(VkCommandBuffer cmd, uint32_t offset);

// Get the vertex buffer handle
VkBuffer VK_GetUberVertexBuffer(void);

// Clean up resources
void VK_DestroyUberVertexBuffer(void);

#endif // VK_UBER_ADAPTER_H