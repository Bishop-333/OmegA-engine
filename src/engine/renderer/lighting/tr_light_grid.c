/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

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

#include "../core/tr_local.h"
#include "tr_light.h"
#include "tr_light_dynamic.h"

// External references
extern lightSystem_t tr_lightSystem;

// Default grid cell size in world units
static const float LIGHT_GRID_CELL_SIZE = 64.0f;

// Light grid for spatial queries
static lightGrid_t *s_lightGrid = NULL;

// Forward declarations
static void R_AddLightToGridCell(int cellIndex, renderLight_t *light);
void R_DebugBounds(vec3_t mins, vec3_t maxs, vec4_t color);

/*
===============
R_InitLightGrid

Initialize the light grid
===============
*/
static void R_InitLightGrid(lightGrid_t *grid, vec3_t mins, vec3_t maxs, float cellSize) {
    int i, j, k;
    
    if (!grid) {
        return;
    }
    
    VectorCopy(mins, grid->mins);
    VectorCopy(maxs, grid->maxs);
    VectorSet(grid->cellSize, cellSize, cellSize, cellSize);
    
    // Calculate grid dimensions
    for (i = 0; i < 3; i++) {
        grid->gridSize[i] = (int)ceil((grid->maxs[i] - grid->mins[i]) / grid->cellSize[i]);
        if (grid->gridSize[i] < 1) {
            grid->gridSize[i] = 1;
        }
    }
    
    // Allocate grid cells
    int totalCells = grid->gridSize[0] * grid->gridSize[1] * grid->gridSize[2];
    grid->cells = ri.Hunk_Alloc(sizeof(renderLight_t**) * totalCells, h_low);
    
    // Initialize cells to NULL
    for (i = 0; i < totalCells; i++) {
        grid->cells[i] = NULL;
    }
}

/*
===============
R_BuildLightGrid

Build spatial grid for fast light lookups
===============
*/
void R_BuildLightGrid(void) {
    int i, j, k, l;
    vec3_t cellMin, cellMax;
    renderLight_t *light;
    float cellSize;
    
    // Don't rebuild if we already have a grid
    if (s_lightGrid && tr_lightSystem.frameCount > 1) {
        return;
    }
    
    // Allocate grid if needed
    if (!s_lightGrid) {
        s_lightGrid = ri.Hunk_Alloc(sizeof(lightGrid_t), h_low);
    }
    
    // Clear existing grid
    R_ClearLightGrid();
    
    // Get world bounds
    vec3_t worldMins, worldMaxs;
    if (tr.world) {
        VectorCopy(tr.world->nodes[0].mins, worldMins);
        VectorCopy(tr.world->nodes[0].maxs, worldMaxs);
    } else {
        VectorSet(worldMins, -65536, -65536, -65536);
        VectorSet(worldMaxs, 65536, 65536, 65536);
    }
    
    // Initialize grid
    cellSize = LIGHT_GRID_CELL_SIZE;
    R_InitLightGrid(s_lightGrid, worldMins, worldMaxs, cellSize);
    
    // Add lights to grid cells
    for (l = 0; l < tr_lightSystem.numActiveLights; l++) {
        light = tr_lightSystem.activeLights[l];
        
        // Calculate which cells the light affects
        int minCell[3], maxCell[3];
        for (i = 0; i < 3; i++) {
            minCell[i] = (int)floor((light->mins[i] - s_lightGrid->mins[i]) / s_lightGrid->cellSize[i]);
            maxCell[i] = (int)ceil((light->maxs[i] - s_lightGrid->mins[i]) / s_lightGrid->cellSize[i]);
            
            // Clamp to grid bounds
            if (minCell[i] < 0) minCell[i] = 0;
            if (maxCell[i] >= s_lightGrid->gridSize[i]) maxCell[i] = s_lightGrid->gridSize[i] - 1;
        }
        
        // Add light to affected cells
        for (i = minCell[0]; i <= maxCell[0]; i++) {
            for (j = minCell[1]; j <= maxCell[1]; j++) {
                for (k = minCell[2]; k <= maxCell[2]; k++) {
                    int cellIndex = i + j * s_lightGrid->gridSize[0] + 
                                   k * s_lightGrid->gridSize[0] * s_lightGrid->gridSize[1];
                    
                    // Calculate cell bounds
                    cellMin[0] = s_lightGrid->mins[0] + i * s_lightGrid->cellSize[0];
                    cellMin[1] = s_lightGrid->mins[1] + j * s_lightGrid->cellSize[1];
                    cellMin[2] = s_lightGrid->mins[2] + k * s_lightGrid->cellSize[2];
                    cellMax[0] = cellMin[0] + s_lightGrid->cellSize[0];
                    cellMax[1] = cellMin[1] + s_lightGrid->cellSize[1];
                    cellMax[2] = cellMin[2] + s_lightGrid->cellSize[2];
                    
                    // Check if light actually affects this cell
                    qboolean affects = qfalse;
                    if (light->type == RL_DIRECTIONAL) {
                        affects = qtrue; // Directional lights affect everything
                    } else if (light->type == RL_OMNI) {
                        // Check sphere-box intersection
                        vec3_t closest;
                        for (int axis = 0; axis < 3; axis++) {
                            if (light->origin[axis] < cellMin[axis]) {
                                closest[axis] = cellMin[axis];
                            } else if (light->origin[axis] > cellMax[axis]) {
                                closest[axis] = cellMax[axis];
                            } else {
                                closest[axis] = light->origin[axis];
                            }
                        }
                        
                        vec3_t delta;
                        VectorSubtract(closest, light->origin, delta);
                        if (VectorLength(delta) <= light->radius) {
                            affects = qtrue;
                        }
                    } else {
                        // Check AABB overlap for other light types
                        if (!(cellMin[0] > light->maxs[0] || cellMax[0] < light->mins[0] ||
                              cellMin[1] > light->maxs[1] || cellMax[1] < light->mins[1] ||
                              cellMin[2] > light->maxs[2] || cellMax[2] < light->mins[2])) {
                            affects = qtrue;
                        }
                    }
                    
                    if (affects) {
                        // Add light to cell's light list
                        R_AddLightToGridCell(cellIndex, light);
                    }
                }
            }
        }
    }
}

/*
===============
R_ClearLightGrid

Clear the light grid
===============
*/
void R_ClearLightGrid(void) {
    int i;
    
    if (!s_lightGrid) {
        return;
    }
    
    // Free all cell light lists
    if (s_lightGrid->cells) {
        int totalCells = s_lightGrid->gridSize[0] * s_lightGrid->gridSize[1] * s_lightGrid->gridSize[2];
        for (i = 0; i < totalCells; i++) {
            if (s_lightGrid->cells[i]) {
                // Light lists are allocated from hunk, so they'll be freed automatically
                s_lightGrid->cells[i] = NULL;
            }
        }
    }
}

/*
===============
R_AddLightToGridCell

Add a light to a grid cell's light list
===============
*/
static void R_AddLightToGridCell(int cellIndex, renderLight_t *light) {
    renderLight_t **cellLights;
    int numLights = 0;
    
    if (!s_lightGrid || !s_lightGrid->cells) {
        return;
    }
    
    // Count existing lights in cell
    cellLights = s_lightGrid->cells[cellIndex];
    if (cellLights) {
        while (cellLights[numLights]) {
            numLights++;
        }
    }
    
    // Allocate new list with room for one more light
    renderLight_t **newList = ri.Hunk_Alloc(sizeof(renderLight_t*) * (numLights + 2), h_low);
    
    // Copy existing lights
    if (cellLights) {
        for (int i = 0; i < numLights; i++) {
            newList[i] = cellLights[i];
        }
    }
    
    // Add new light
    newList[numLights] = light;
    newList[numLights + 1] = NULL; // Null terminate
    
    // Update cell
    s_lightGrid->cells[cellIndex] = newList;
}

/*
===============
R_GetLightsInCell

Get lights affecting a grid cell (already implemented in tr_light_cull.c)
This is an alternative implementation using the grid
===============
*/
renderLight_t** R_GetLightsInGridCell(int x, int y, int z) {
    static renderLight_t *emptyList[1] = { NULL };
    
    if (!s_lightGrid || !s_lightGrid->cells) {
        return emptyList;
    }
    
    // Bounds check
    if (x < 0 || x >= s_lightGrid->gridSize[0] ||
        y < 0 || y >= s_lightGrid->gridSize[1] ||
        z < 0 || z >= s_lightGrid->gridSize[2]) {
        return emptyList;
    }
    
    int cellIndex = x + y * s_lightGrid->gridSize[0] + 
                   z * s_lightGrid->gridSize[0] * s_lightGrid->gridSize[1];
    
    if (s_lightGrid->cells[cellIndex]) {
        return s_lightGrid->cells[cellIndex];
    }
    
    return emptyList;
}

/*
===============
R_GetLightsAtPoint

Get lights affecting a world position using the grid
===============
*/
renderLight_t** R_GetLightsAtPoint(vec3_t point) {
    int cellCoord[3];
    int i;
    
    if (!s_lightGrid) {
        R_BuildLightGrid();
        if (!s_lightGrid) {
            static renderLight_t *emptyList[1] = { NULL };
            return emptyList;
        }
    }
    
    // Calculate cell coordinates
    for (i = 0; i < 3; i++) {
        cellCoord[i] = (int)floor((point[i] - s_lightGrid->mins[i]) / s_lightGrid->cellSize[i]);
    }
    
    return R_GetLightsInGridCell(cellCoord[0], cellCoord[1], cellCoord[2]);
}

/*
===============
R_GetNearbyLights

Get lights from neighboring cells for smooth transitions
===============
*/
int R_GetNearbyLights(vec3_t point, renderLight_t **lightList, int maxLights) {
    int cellCoord[3];
    int i, j, k;
    int numLights = 0;
    
    if (!s_lightGrid) {
        return 0;
    }
    
    // Calculate cell coordinates
    for (i = 0; i < 3; i++) {
        cellCoord[i] = (int)floor((point[i] - s_lightGrid->mins[i]) / s_lightGrid->cellSize[i]);
    }
    
    // Check 3x3x3 neighborhood
    for (i = -1; i <= 1 && numLights < maxLights; i++) {
        for (j = -1; j <= 1 && numLights < maxLights; j++) {
            for (k = -1; k <= 1 && numLights < maxLights; k++) {
                renderLight_t **cellLights = R_GetLightsInGridCell(
                    cellCoord[0] + i,
                    cellCoord[1] + j,
                    cellCoord[2] + k
                );
                
                // Add unique lights to list
                for (int l = 0; cellLights[l] && numLights < maxLights; l++) {
                    qboolean found = qfalse;
                    
                    // Check if light is already in list
                    for (int m = 0; m < numLights; m++) {
                        if (lightList[m] == cellLights[l]) {
                            found = qtrue;
                            break;
                        }
                    }
                    
                    if (!found) {
                        lightList[numLights++] = cellLights[l];
                    }
                }
            }
        }
    }
    
    return numLights;
}

/*
===============
R_ComputeSceneLighting

Accumulates lighting at a point using the probe/light system
===============
*/
void R_ComputeSceneLighting(const vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir) {
    const int maxNearby = 32;
    renderLight_t *nearby[32];
    vec3_t directionAccum;
    float directionWeight = 0.0f;
    int numLights;
    int i;

    // Default baseline so scenes without probes still have some light
    VectorSet(ambientLight,
              tr.identityLight * 16.0f,
              tr.identityLight * 16.0f,
              tr.identityLight * 16.0f);
    VectorClear(directedLight);
    VectorClear(lightDir);
    VectorClear(directionAccum);

    // Ensure the spatial grid is ready before sampling
    R_BuildLightGrid();
    numLights = R_GetNearbyLights(point, nearby, maxNearby);

    if (numLights <= 0) {
        float fallback = tr.identityLight * 150.0f;
        VectorSet(ambientLight, fallback, fallback, fallback);
        VectorSet(directedLight, fallback, fallback, fallback);

        if (VectorLengthSquared(tr.sunDirection) > 0.0f) {
            VectorCopy(tr.sunDirection, lightDir);
        } else {
            VectorSet(lightDir, 0.0f, 0.0f, 1.0f);
        }
        return;
    }

    for (i = 0; i < numLights; ++i) {
        renderLight_t *light = nearby[i];
        float brightness = 0.0f;
        float ambientRatio = 0.0f;
        float directRatio = 0.0f;
        vec3_t dirToLight;
        VectorClear(dirToLight);

        if (!light) {
            continue;
        }

        switch (light->type) {
            case RL_DIRECTIONAL: {
                vec3_t dir;
                VectorCopy(light->target, dir);
                if (VectorNormalize(dir) == 0.0f) {
                    VectorCopy(tr.sunDirection, dir);
                    if (VectorNormalize(dir) == 0.0f) {
                        VectorSet(dir, 0.0f, 0.0f, -1.0f);
                    }
                }
                VectorScale(dir, -1.0f, dirToLight);

                brightness = light->intensity > 0.0f ? light->intensity : 0.0f;
                ambientRatio = 0.15f;
                directRatio = 0.85f;
                break;
            }

            case RL_OMNI:
            case RL_PROJ: {
                vec3_t delta;
                float distance;
                float denom;
                float radius;

                VectorSubtract(light->origin, point, delta);
                distance = VectorLength(delta);
                if (distance > 0.0f) {
                    VectorScale(delta, 1.0f / distance, dirToLight);
                } else {
                    VectorSet(dirToLight, 0.0f, 0.0f, 1.0f);
                }

                radius = (light->cutoffDistance > 0.0f) ? light->cutoffDistance : light->radius;
                if (radius > 0.0f && distance > radius) {
                    continue;
                }

                denom = light->constant +
                        light->linear * distance +
                        light->quadratic * distance * distance;
                if (denom <= 0.0001f) {
                    denom = 0.0001f;
                }

                brightness = (light->intensity > 0.0f ? light->intensity : 0.0f) * (1.0f / denom);
                ambientRatio = 0.35f;
                directRatio = 0.65f;
                break;
            }

            case RL_AMBIENT:
                brightness = light->intensity > 0.0f ? light->intensity : 0.0f;
                ambientRatio = 1.0f;
                directRatio = 0.0f;
                break;

            default:
                continue;
        }

        if (brightness <= 0.0f) {
            continue;
        }

        // Convert brightness into 0..255 scale contributions
        {
            float ambientScale = brightness * ambientRatio * tr.identityLightByte;
            float directScale = brightness * directRatio * tr.identityLightByte;

            if (ambientScale > 0.0f) {
                VectorMA(ambientLight, ambientScale, light->color, ambientLight);
            }

            if (directScale > 0.0f) {
                VectorMA(directedLight, directScale, light->color, directedLight);
                VectorMA(directionAccum, directScale, dirToLight, directionAccum);
                directionWeight += directScale;
            }
        }
    }

    if (directionWeight > 0.0f && VectorNormalize2(directionAccum, lightDir) != 0.0f) {
        // normalised above
    } else if (VectorLengthSquared(tr.sunDirection) > 0.0f) {
        VectorCopy(tr.sunDirection, lightDir);
    } else {
        VectorSet(lightDir, 0.0f, 0.0f, 1.0f);
    }

    // Clamp outputs to byte range
    for (i = 0; i < 3; ++i) {
        ambientLight[i] = Com_Clamp(0.0f, (float)tr.identityLightByte, ambientLight[i]);
        directedLight[i] = Com_Clamp(0.0f, (float)tr.identityLightByte, directedLight[i]);
    }
}

/*
===============
R_DrawLightGrid

Debug visualization of the light grid
===============
*/
void R_DrawLightGrid(void) {
    int i, j, k;
    vec3_t cellMin, cellMax;
    vec4_t color;
    
    if (!s_lightGrid || !r_showLightVolumes || !r_showLightVolumes->integer) {
        return;
    }
    
    // Draw grid cells that contain lights
    for (i = 0; i < s_lightGrid->gridSize[0]; i++) {
        for (j = 0; j < s_lightGrid->gridSize[1]; j++) {
            for (k = 0; k < s_lightGrid->gridSize[2]; k++) {
                int cellIndex = i + j * s_lightGrid->gridSize[0] + 
                               k * s_lightGrid->gridSize[0] * s_lightGrid->gridSize[1];
                
                if (s_lightGrid->cells[cellIndex] && s_lightGrid->cells[cellIndex][0]) {
                    // Calculate cell bounds
                    cellMin[0] = s_lightGrid->mins[0] + i * s_lightGrid->cellSize[0];
                    cellMin[1] = s_lightGrid->mins[1] + j * s_lightGrid->cellSize[1];
                    cellMin[2] = s_lightGrid->mins[2] + k * s_lightGrid->cellSize[2];
                    cellMax[0] = cellMin[0] + s_lightGrid->cellSize[0];
                    cellMax[1] = cellMin[1] + s_lightGrid->cellSize[1];
                    cellMax[2] = cellMin[2] + s_lightGrid->cellSize[2];
                    
                    // Count lights in cell
                    int lightCount = 0;
                    renderLight_t **lights = s_lightGrid->cells[cellIndex];
                    while (lights[lightCount]) {
                        lightCount++;
                    }
                    
                    // Color based on light count
                    if (lightCount == 1) {
                        VectorSet(color, 0, 0, 1); // Blue for 1 light
                    } else if (lightCount == 2) {
                        VectorSet(color, 0, 1, 0); // Green for 2 lights
                    } else if (lightCount == 3) {
                        VectorSet(color, 1, 1, 0); // Yellow for 3 lights
                    } else {
                        VectorSet(color, 1, 0, 0); // Red for 4+ lights
                    }
                    color[3] = 0.2f;
                    
                    // Draw cell bounds
                    R_DebugBounds(cellMin, cellMax, color);
                }
            }
        }
    }
}
