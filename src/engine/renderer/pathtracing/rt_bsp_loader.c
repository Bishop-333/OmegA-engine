/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

BSP to RTX Integration
Loads world geometry into RTX acceleration structures
===========================================================================
*/

#include "../core/tr_local.h"
#include "rt_rtx.h"
#include "rt_pathtracer.h"
#include "rt_debug_overlay.h"

// Maximum vertices/indices per BLAS batch
// Reduced for better stability and multiple BLAS creation
#define MAX_BATCH_VERTS 8192
#define MAX_BATCH_INDICES (MAX_BATCH_VERTS * 3)

// Batch accumulator for building BLAS
typedef struct {
    vec3_t vertices[MAX_BATCH_VERTS];
    unsigned int indices[MAX_BATCH_INDICES];
    int numVerts;
    int numIndices;
    int numSurfaces;
} rtxBatchBuilder_t;

static rtxBatchBuilder_t batchBuilder;
static int totalBLASCreated = 0;
static int totalSurfacesProcessed = 0;

/*
================
RTX_FlushBatch

Create BLAS from accumulated batch
================
*/
static void RTX_FlushBatch(void) {
    if (batchBuilder.numVerts > 0 && batchBuilder.numIndices > 0) {
        ri.Printf(PRINT_ALL, "RTX: Flushing batch with %d verts, %d indices, %d surfaces\n",
            batchBuilder.numVerts, batchBuilder.numIndices, batchBuilder.numSurfaces);

        rtxBLAS_t *blas = RTX_CreateBLAS(
            batchBuilder.vertices,
            batchBuilder.numVerts,
            batchBuilder.indices,
            batchBuilder.numIndices,
            qfalse  // static geometry
        );

        if (blas) {
            totalBLASCreated++;
            totalSurfacesProcessed += batchBuilder.numSurfaces;
            ri.Printf(PRINT_ALL, "RTX: Created BLAS %d with %d verts, %d tris, %d surfaces (total surfaces: %d)\n",
                totalBLASCreated, batchBuilder.numVerts, batchBuilder.numIndices / 3, batchBuilder.numSurfaces, totalSurfacesProcessed);
        } else {
            ri.Printf(PRINT_WARNING, "RTX: Failed to create BLAS from batch\n");
        }

        // Reset batch
        batchBuilder.numVerts = 0;
        batchBuilder.numIndices = 0;
        batchBuilder.numSurfaces = 0;
    }
}

/*
================
RTX_AddSurfaceFace

Add a face surface to the current batch
================
*/
static void RTX_AddSurfaceFace(srfSurfaceFace_t *face) {
    if (!face || face->numPoints < 3) {
        return;
    }
    
    // Check if we need to flush
    if (batchBuilder.numVerts + face->numPoints > MAX_BATCH_VERTS ||
        batchBuilder.numIndices + face->numIndices > MAX_BATCH_INDICES) {
        RTX_FlushBatch();
    }
    
    // Add vertices
    int baseVertex = batchBuilder.numVerts;
    for (int i = 0; i < face->numPoints; i++) {
        VectorCopy(face->points[i], batchBuilder.vertices[batchBuilder.numVerts]);
        batchBuilder.numVerts++;
    }
    
    // Add indices - they are stored after the points array
    int *indices = (int *)((byte *)face + face->ofsIndices);
    for (int i = 0; i < face->numIndices; i++) {
        batchBuilder.indices[batchBuilder.numIndices++] = baseVertex + indices[i];
    }
    
    batchBuilder.numSurfaces++;
}

/*
================
RTX_AddSurfaceGrid

Add a grid mesh surface to the current batch
================
*/
static void RTX_AddSurfaceGrid(srfGridMesh_t *grid) {
    if (!grid || grid->width < 2 || grid->height < 2) {
        return;
    }
    
    // Calculate vertex and triangle count
    int numVerts = grid->width * grid->height;
    int numTris = (grid->width - 1) * (grid->height - 1) * 2;
    int numIndices = numTris * 3;
    
    // Check if we need to flush
    if (batchBuilder.numVerts + numVerts > MAX_BATCH_VERTS ||
        batchBuilder.numIndices + numIndices > MAX_BATCH_INDICES) {
        RTX_FlushBatch();
    }
    
    // Add vertices
    int baseVertex = batchBuilder.numVerts;
    for (int i = 0; i < numVerts; i++) {
        VectorCopy(grid->verts[i].xyz, batchBuilder.vertices[batchBuilder.numVerts]);
        batchBuilder.numVerts++;
    }
    
    // Generate indices for the grid
    for (int y = 0; y < grid->height - 1; y++) {
        for (int x = 0; x < grid->width - 1; x++) {
            int v0 = baseVertex + y * grid->width + x;
            int v1 = v0 + 1;
            int v2 = v0 + grid->width;
            int v3 = v2 + 1;
            
            // First triangle
            batchBuilder.indices[batchBuilder.numIndices++] = v0;
            batchBuilder.indices[batchBuilder.numIndices++] = v2;
            batchBuilder.indices[batchBuilder.numIndices++] = v1;
            
            // Second triangle
            batchBuilder.indices[batchBuilder.numIndices++] = v1;
            batchBuilder.indices[batchBuilder.numIndices++] = v2;
            batchBuilder.indices[batchBuilder.numIndices++] = v3;
        }
    }
    
    batchBuilder.numSurfaces++;
}

/*
================
RTX_AddSurfaceTriangles

Add a triangle soup surface to the current batch
================
*/
static void RTX_AddSurfaceTriangles(srfTriangles_t *tri) {
    if (!tri || tri->numVerts < 3 || tri->numIndexes < 3) {
        return;
    }
    
    // Check if we need to flush
    if (batchBuilder.numVerts + tri->numVerts > MAX_BATCH_VERTS ||
        batchBuilder.numIndices + tri->numIndexes > MAX_BATCH_INDICES) {
        RTX_FlushBatch();
    }
    
    // Add vertices
    int baseVertex = batchBuilder.numVerts;
    for (int i = 0; i < tri->numVerts; i++) {
        VectorCopy(tri->verts[i].xyz, batchBuilder.vertices[batchBuilder.numVerts]);
        batchBuilder.numVerts++;
    }
    
    // Add indices
    for (int i = 0; i < tri->numIndexes; i++) {
        batchBuilder.indices[batchBuilder.numIndices++] = baseVertex + tri->indexes[i];
    }
    
    batchBuilder.numSurfaces++;
}

/*
================
RTX_ProcessWorldSurface

Process a world surface and add it to RTX acceleration structures
================
*/
void RTX_ProcessWorldSurface(msurface_t *surf) {
    static int debugCount = 0;

    if (!surf || !surf->data) {
        return;
    }

    // Skip surfaces that shouldn't be in RTX
    if (surf->shader) {
        if (surf->shader->surfaceFlags & SURF_SKY) {
            return;  // Skip sky surfaces
        }
        if (surf->shader->surfaceFlags & SURF_NODRAW) {
            return;  // Skip nodraw surfaces
        }
    }

    surfaceType_t *type = (surfaceType_t *)surf->data;

    // Debug: Log first few surface types
    if (debugCount < 10) {
        ri.Printf(PRINT_ALL, "RTX: Surface %d type: %d\n", debugCount, *type);
        debugCount++;
    }

    switch (*type) {
        case SF_FACE:
            RTX_AddSurfaceFace((srfSurfaceFace_t *)surf->data);
            break;

        case SF_GRID:
            RTX_AddSurfaceGrid((srfGridMesh_t *)surf->data);
            break;

        case SF_TRIANGLES:
            RTX_AddSurfaceTriangles((srfTriangles_t *)surf->data);
            break;

        case SF_POLY:
            // Polys are usually dynamic, skip for now
            break;

        default:
            // Unsupported surface type
            ri.Printf(PRINT_DEVELOPER, "RTX: Unsupported surface type %d\n", *type);
            break;
    }
}

/*
================
RTX_BeginWorldLoad

Initialize RTX world loading
================
*/
void RTX_BeginWorldLoad(void) {
    // Reset batch builder
    Com_Memset(&batchBuilder, 0, sizeof(batchBuilder));
    totalBLASCreated = 0;
    totalSurfacesProcessed = 0;
    
    ri.Printf(PRINT_ALL, "RTX: Beginning world geometry loading...\n");
}

/*
================
RTX_EndWorldLoad

Finalize RTX world loading
================
*/
void RTX_EndWorldLoad(void) {
    // Flush any remaining surfaces
    RTX_FlushBatch();
    
    // Build the TLAS
    if (totalBLASCreated > 0) {
        RTX_BuildTLAS(&rtx.tlas);

        // Update debug overlay stats
        ri.Printf(PRINT_ALL, "RTX: Calling RTX_UpdateDebugStats with surfaces=%d, BLAS=%d\n",
            totalSurfacesProcessed, totalBLASCreated);
        RTX_UpdateDebugStats(totalSurfacesProcessed, totalBLASCreated);

        ri.Printf(PRINT_ALL, "RTX: World loading complete - %d BLAS created from %d surfaces\n",
            totalBLASCreated, totalSurfacesProcessed);
    } else {
        ri.Printf(PRINT_WARNING, "RTX: No world geometry loaded!\n");
    }

    // Mark completion
    rtx.populationProgress = 100;
}

/*
================
RTX_LoadWorldMap

Main entry point for loading world geometry into RTX
Called from R_LoadWorldMap after BSP is loaded
================
*/
void RTX_LoadWorldMap(void) {
    ri.Printf(PRINT_ALL, "RTX: LoadWorldMap called\n");

    if (!rtx_enable) {
        ri.Printf(PRINT_WARNING, "RTX: rtx_enable cvar is NULL\n");
        return;
    }

    if (!rtx_enable->integer) {
        ri.Printf(PRINT_WARNING, "RTX: Disabled by rtx_enable cvar (value=%d)\n", rtx_enable->integer);
        return;
    }

    if (!tr.world) {
        ri.Printf(PRINT_WARNING, "RTX: No world loaded\n");
        return;
    }

    if (!tr.world->surfaces) {
        ri.Printf(PRINT_WARNING, "RTX: World has no surfaces\n");
        return;
    }

    ri.Printf(PRINT_ALL, "RTX: Beginning world load process\n");
    RTX_BeginWorldLoad();
    
    // Process all world surfaces
    int numSurfaces = tr.world->numsurfaces;
    msurface_t *surfaces = tr.world->surfaces;
    
    ri.Printf(PRINT_ALL, "RTX: Processing %d world surfaces...\n", numSurfaces);

    for (int i = 0; i < numSurfaces; i++) {
        RTX_ProcessWorldSurface(&surfaces[i]);

        // Update progress for loading screen
        if (numSurfaces > 0) {
            rtx.populationProgress = (i * 100) / numSurfaces;
        }

        // Progress update every 1000 surfaces
        if ((i % 1000) == 0 && i > 0) {
            ri.Printf(PRINT_ALL, "RTX: Processed %d/%d surfaces (%d%%), %d surfaces added to batch\n",
                i, numSurfaces, rtx.populationProgress, totalSurfacesProcessed);
        }
    }

    ri.Printf(PRINT_ALL, "RTX: Finished processing all surfaces, total processed: %d\n", totalSurfacesProcessed);
    
    RTX_EndWorldLoad();
}