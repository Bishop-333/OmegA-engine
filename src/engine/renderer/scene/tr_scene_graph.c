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
// tr_scene_graph.c - Hierarchical scene graph implementation

#include "../core/tr_local.h"
#include "../core/tr_common_utils.h"
#include "tr_scene_graph.h"
#include "tr_portal.h"

// Global scene graph
sceneGraph_t sceneGraph;

// CVars
cvar_t *r_useSceneGraph;  // Made non-static for external access
static cvar_t *r_spatialTreeType;
static cvar_t *r_drawSceneGraph;
static cvar_t *r_sceneGraphStats;

// Static node ID counter
static int nextNodeId = 1;

/*
================
R_InitSceneGraph

Initialize the scene graph system
================
*/
void R_InitSceneGraph(void) {
    int i;
    
    Com_Memset(&sceneGraph, 0, sizeof(sceneGraph));
    
    // Register CVars
    r_useSceneGraph = ri.Cvar_Get("r_useSceneGraph", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(r_useSceneGraph, "Use hierarchical scene graph");
    
    r_spatialTreeType = ri.Cvar_Get("r_spatialTreeType", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(r_spatialTreeType, "Spatial tree type: 0=Octree, 1=BVH");
    
    r_drawSceneGraph = ri.Cvar_Get("r_drawSceneGraph", "0", CVAR_CHEAT);
    r_sceneGraphStats = ri.Cvar_Get("r_sceneGraphStats", "0", CVAR_CHEAT);
    
    // Initialize node pool
    for (i = 0; i < MAX_SCENE_NODES - 1; i++) {
        sceneGraph.nodes[i].nextSibling = &sceneGraph.nodes[i + 1];
    }
    sceneGraph.freeNodes = &sceneGraph.nodes[0];
    
    // Create root node
    sceneGraph.root = R_CreateSceneNode(NODE_TYPE_ROOT, "root");
    if (sceneGraph.root) {
        MatrixIdentity(sceneGraph.root->localMatrix);
        MatrixIdentity(sceneGraph.root->worldMatrix);
        VectorSet(sceneGraph.root->scale, 1, 1, 1);
    }
    
    // Allocate spatial tree pools
    sceneGraph.spatialTree.octreePoolSize = 4096;
    sceneGraph.spatialTree.octreePool = Z_Malloc(sizeof(octreeNode_t) * sceneGraph.spatialTree.octreePoolSize);
    
    sceneGraph.spatialTree.bvhPoolSize = 4096;
    sceneGraph.spatialTree.bvhPool = Z_Malloc(sizeof(bvhNode_t) * sceneGraph.spatialTree.bvhPoolSize);
    
    // Build initial spatial tree
    R_BuildSpatialTree(r_spatialTreeType->integer ? SPATIAL_BVH : SPATIAL_OCTREE);
    
    ri.Printf(PRINT_ALL, "Scene graph initialized\n");
}

/*
================
R_ShutdownSceneGraph

Cleanup scene graph system
================
*/
void R_ShutdownSceneGraph(void) {
    R_ClearSceneGraph();
    
    if (sceneGraph.spatialTree.octreePool) {
        Z_Free(sceneGraph.spatialTree.octreePool);
        sceneGraph.spatialTree.octreePool = NULL;
    }
    
    if (sceneGraph.spatialTree.bvhPool) {
        Z_Free(sceneGraph.spatialTree.bvhPool);
        sceneGraph.spatialTree.bvhPool = NULL;
    }
    
    Com_Memset(&sceneGraph, 0, sizeof(sceneGraph));
}

/*
================
R_ClearSceneGraph

Clear all nodes except root
================
*/
void R_ClearSceneGraph(void) {
    int i;
    
    // Clear all nodes
    for (i = 0; i < MAX_SCENE_NODES; i++) {
        sceneNode_t *node = &sceneGraph.nodes[i];
        
        if (node->surfaces) {
            Z_Free(node->surfaces);
            node->surfaces = NULL;
        }
        
        if (node->userData) {
            Z_Free(node->userData);
            node->userData = NULL;
        }
        
        Com_Memset(node, 0, sizeof(*node));
    }
    
    // Don't use nextSibling for free list as it conflicts with actual usage
    // Just reset the free nodes pointer
    sceneGraph.freeNodes = NULL;
    
    sceneGraph.numNodes = 0;
    sceneGraph.numModelNodes = 0;
    sceneGraph.numEntityNodes = 0;
    sceneGraph.numLightNodes = 0;
    
    // Recreate root
    sceneGraph.root = R_CreateSceneNode(NODE_TYPE_ROOT, "root");
    
    // Clear spatial tree
    R_ClearSpatialTree();
}

/*
================
R_AllocSceneNode

Allocate a node from the pool
================
*/
sceneNode_t* R_AllocSceneNode(void) {
    sceneNode_t *node;
    
    if (!sceneGraph.freeNodes) {
        ri.Printf(PRINT_WARNING, "R_AllocSceneNode: out of nodes\n");
        return NULL;
    }
    
    node = sceneGraph.freeNodes;
    sceneGraph.freeNodes = node->nextSibling;
    
    Com_Memset(node, 0, sizeof(*node));
    node->nodeId = nextNodeId++;
    
    sceneGraph.numNodes++;
    
    return node;
}

/*
================
R_FreeSceneNode

Return a node to the pool
================
*/
void R_FreeSceneNode(sceneNode_t *node) {
    if (!node || node == sceneGraph.root) {
        return;
    }
    
    // Free allocated data
    if (node->surfaces) {
        Z_Free(node->surfaces);
        node->surfaces = NULL;
    }
    
    if (node->userData) {
        Z_Free(node->userData);
        node->userData = NULL;
    }
    
    // Remove from parent
    if (node->parent) {
        R_RemoveChildNode(node->parent, node);
    }
    
    // Remove from spatial tree
    R_RemoveNodeFromSpatialTree(node);
    
    // Clear node
    Com_Memset(node, 0, sizeof(*node));
    
    // Add to free list
    node->nextSibling = sceneGraph.freeNodes;
    sceneGraph.freeNodes = node;
    
    sceneGraph.numNodes--;
}

/*
================
R_CreateSceneNode

Create a new scene node
================
*/
sceneNode_t* R_CreateSceneNode(nodeType_t type, const char *name) {
    sceneNode_t *node;
    
    node = R_AllocSceneNode();
    if (!node) {
        return NULL;
    }
    
    node->type = type;
    node->flags = NODE_FLAG_VISIBLE;
    
    if (name) {
        Q_strncpyz(node->name, name, sizeof(node->name));
    }
    
    // Initialize transform
    VectorClear(node->origin);
    VectorClear(node->angles);
    VectorSet(node->scale, 1, 1, 1);
    QuatIdentity(node->rotation);
    MatrixIdentity(node->localMatrix);
    MatrixIdentity(node->worldMatrix);
    
    // Initialize bounds
    ClearBounds(node->localMins, node->localMaxs);
    ClearBounds(node->worldMins, node->worldMaxs);
    
    // Set type-specific flags
    switch (type) {
    case NODE_TYPE_MODEL:
        node->flags |= NODE_FLAG_STATIC;
        break;
    case NODE_TYPE_ENTITY:
    case NODE_TYPE_LIGHT:
        node->flags |= NODE_FLAG_DYNAMIC;
        break;
    default:
        break;
    }
    
    return node;
}

/*
================
R_AddChildNode

Add a child to a parent node
================
*/
void R_AddChildNode(sceneNode_t *parent, sceneNode_t *child) {
    if (!parent || !child || child->parent == parent) {
        return;
    }
    
    // Remove from current parent
    if (child->parent) {
        R_RemoveChildNode(child->parent, child);
    }
    
    // Add to new parent
    child->parent = parent;
    child->nextSibling = parent->firstChild;
    parent->firstChild = child;
    parent->numChildren++;
    
    // Mark transforms dirty
    R_MarkNodeDirty(child);
}

/*
================
R_RemoveChildNode

Remove a child from its parent
================
*/
void R_RemoveChildNode(sceneNode_t *parent, sceneNode_t *child) {
    sceneNode_t **link;
    
    if (!parent || !child || child->parent != parent) {
        return;
    }
    
    // Find and remove from sibling list
    for (link = &parent->firstChild; *link; link = &(*link)->nextSibling) {
        if (*link == child) {
            *link = child->nextSibling;
            parent->numChildren--;
            break;
        }
    }
    
    child->parent = NULL;
    child->nextSibling = NULL;
}

/*
================
R_SetNodeTransform

Set node transform from position, angles, and scale
================
*/
void R_SetNodeTransform(sceneNode_t *node, const vec3_t origin, const vec3_t angles, const vec3_t scale) {
    if (!node) {
        return;
    }
    
    VectorCopy(origin, node->origin);
    VectorCopy(angles, node->angles);
    VectorCopy(scale, node->scale);
    
    // Convert angles to quaternion
    QuatFromAngles(angles, node->rotation);
    
    // Build local matrix
    MatrixFromQuat(node->rotation, node->localMatrix);
    // For non-uniform scale, would need to multiply by scale matrix
    // For now just use uniform scale on first component
    MatrixScale(node->localMatrix, scale[0]);
    MatrixSetTranslation(node->localMatrix, origin);
    
    R_MarkNodeDirty(node);
}

/*
================
R_UpdateNodeWorldMatrix

Update world matrix from local matrix and parent
================
*/
void R_UpdateNodeWorldMatrix(sceneNode_t *node) {
    if (!node) {
        return;
    }
    
    if (node->parent && node->parent != sceneGraph.root) {
        // Ensure parent is up to date
        if (node->parent->flags & NODE_FLAG_DIRTY) {
            R_UpdateNodeWorldMatrix(node->parent);
        }
        
        // Multiply with parent's world matrix
        MatrixMultiply4x4(node->parent->worldMatrix, node->localMatrix, node->worldMatrix);
    } else {
        // No parent or root parent - world matrix is local matrix
        MatrixCopy(node->localMatrix, node->worldMatrix);
    }
    
    // Update world bounds
    R_UpdateNodeBounds(node);
    
    // Clear dirty flag
    node->flags &= ~NODE_FLAG_DIRTY;
}

/*
================
R_UpdateNodeBounds

Update world bounds from local bounds and world matrix
================
*/
void R_UpdateNodeBounds(sceneNode_t *node) {
    vec3_t corners[8];
    int i;
    
    if (!node) {
        return;
    }
    
    // Transform local bounds to world space
    for (i = 0; i < 8; i++) {
        vec3_t local, world;
        
        local[0] = (i & 1) ? node->localMaxs[0] : node->localMins[0];
        local[1] = (i & 2) ? node->localMaxs[1] : node->localMins[1];
        local[2] = (i & 4) ? node->localMaxs[2] : node->localMins[2];
        
        MatrixTransformPoint(node->worldMatrix, local, world);
        VectorCopy(world, corners[i]);
    }
    
    // Calculate world AABB
    ClearBounds(node->worldMins, node->worldMaxs);
    for (i = 0; i < 8; i++) {
        AddPointToBounds(corners[i], node->worldMins, node->worldMaxs);
    }
    
    // Calculate bounding radius
    vec3_t center, extent;
    VectorAdd(node->worldMins, node->worldMaxs, center);
    VectorScale(center, 0.5f, center);
    VectorSubtract(node->worldMaxs, center, extent);
    node->boundingRadius = VectorLength(extent);
}

/*
================
R_MarkNodeDirty

Mark node and all children as needing transform update
================
*/
void R_MarkNodeDirty(sceneNode_t *node) {
    sceneNode_t *child;
    
    if (!node) {
        return;
    }
    
    if (!(node->flags & NODE_FLAG_DIRTY)) {
        node->flags |= NODE_FLAG_DIRTY;
        
        // Add to dirty list
        if (sceneGraph.numDirtyNodes < MAX_SCENE_NODES) {
            sceneGraph.dirtyNodes[sceneGraph.numDirtyNodes++] = node->nodeId;
        }
        
        sceneGraph.transformsDirty = qtrue;
    }
    
    // Mark all children dirty
    for (child = node->firstChild; child; child = child->nextSibling) {
        R_MarkNodeDirty(child);
    }
}

/*
================
R_UpdateDirtyNodes

Update all nodes marked as dirty
================
*/
void R_UpdateDirtyNodes(void) {
    int i;
    
    if (!sceneGraph.transformsDirty) {
        return;
    }
    
    for (i = 0; i < sceneGraph.numDirtyNodes; i++) {
        sceneNode_t *node = R_FindNodeById(sceneGraph.dirtyNodes[i]);
        if (node && (node->flags & NODE_FLAG_DIRTY)) {
            R_UpdateNodeWorldMatrix(node);
        }
    }
    
    sceneGraph.numDirtyNodes = 0;
    sceneGraph.transformsDirty = qfalse;
}

/*
================
R_FindNodeById

Find a node by its ID
================
*/
sceneNode_t* R_FindNodeById(int nodeId) {
    int i;
    
    for (i = 0; i < MAX_SCENE_NODES; i++) {
        if (sceneGraph.nodes[i].nodeId == nodeId) {
            return &sceneGraph.nodes[i];
        }
    }
    
    return NULL;
}

/*
================
R_SetNodeModel

Attach a model to a node
================
*/
void R_SetNodeModel(sceneNode_t *node, model_t *model) {
    int i;
    
    if (!node || !model) {
        return;
    }
    
    node->model = model;
    node->type = NODE_TYPE_MODEL;
    
    // Copy model bounds based on model type
    if (model->type == MOD_BRUSH && model->bmodel) {
        VectorCopy(model->bmodel->bounds[0], node->localMins);
        VectorCopy(model->bmodel->bounds[1], node->localMaxs);
        
        // Allocate surface array for brush models
        if (model->bmodel->numSurfaces > 0) {
            node->surfaces = Z_Malloc(sizeof(msurface_t*) * model->bmodel->numSurfaces);
            node->numSurfaces = model->bmodel->numSurfaces;
            
            // Copy surface pointers
            for (i = 0; i < model->bmodel->numSurfaces; i++) {
                node->surfaces[i] = &model->bmodel->firstSurface[i];
            }
        }
    } else {
        // For other model types, use default bounds
        VectorSet(node->localMins, -16, -16, -16);
        VectorSet(node->localMaxs, 16, 16, 16);
    }
    
    // Add to model node list
    if (sceneGraph.numModelNodes < MAX_MODELS) {
        sceneGraph.modelNodes[sceneGraph.numModelNodes++] = node;
    }
    
    R_MarkNodeDirty(node);
}

/*
================
R_BuildSpatialTree

Build spatial acceleration structure
================
*/
void R_BuildSpatialTree(spatialNodeType_t type) {
    vec3_t worldMins, worldMaxs;
    
    sceneGraph.spatialTree.type = type;
    
    // Calculate world bounds
    VectorSet(worldMins, -65536, -65536, -65536);
    VectorSet(worldMaxs, 65536, 65536, 65536);
    
    VectorCopy(worldMins, sceneGraph.spatialTree.worldMins);
    VectorCopy(worldMaxs, sceneGraph.spatialTree.worldMaxs);
    
    if (type == SPATIAL_OCTREE) {
        // Build octree
        vec3_t center;
        float halfSize;
        
        VectorAdd(worldMins, worldMaxs, center);
        VectorScale(center, 0.5f, center);
        
        halfSize = worldMaxs[0] - center[0];
        
        sceneGraph.spatialTree.octreePoolUsed = 0;
        sceneGraph.spatialTree.octreeRoot = R_CreateOctreeNode(center, halfSize, 0);
    } else if (type == SPATIAL_BVH) {
        // Build BVH
        sceneGraph.spatialTree.bvhPoolUsed = 0;
        sceneGraph.spatialTree.bvhRoot = R_CreateBVHNode();
        
        if (sceneGraph.spatialTree.bvhRoot) {
            VectorCopy(worldMins, sceneGraph.spatialTree.bvhRoot->mins);
            VectorCopy(worldMaxs, sceneGraph.spatialTree.bvhRoot->maxs);
        }
    }
}

/*
================
R_CreateOctreeNode

Create an octree node
================
*/
octreeNode_t* R_CreateOctreeNode(const vec3_t center, float halfSize, int depth) {
    octreeNode_t *node;
    
    if (sceneGraph.spatialTree.octreePoolUsed >= sceneGraph.spatialTree.octreePoolSize) {
        ri.Printf(PRINT_WARNING, "R_CreateOctreeNode: pool exhausted\n");
        return NULL;
    }
    
    node = &sceneGraph.spatialTree.octreePool[sceneGraph.spatialTree.octreePoolUsed++];
    Com_Memset(node, 0, sizeof(*node));
    
    VectorCopy(center, node->center);
    node->halfSize = halfSize;
    node->depth = depth;
    
    // Calculate bounds
    VectorSet(node->mins, center[0] - halfSize, center[1] - halfSize, center[2] - halfSize);
    VectorSet(node->maxs, center[0] + halfSize, center[1] + halfSize, center[2] + halfSize);
    
    // Leaf if too small or too deep
    node->isLeaf = (halfSize <= OCTREE_MIN_NODE_SIZE || depth >= OCTREE_MAX_DEPTH);
    
    if (node->isLeaf) {
        node->maxNodes = 32;
        node->nodes = Z_Malloc(sizeof(sceneNode_t*) * node->maxNodes);
    }
    
    sceneGraph.spatialTree.totalNodes++;
    if (node->isLeaf) {
        sceneGraph.spatialTree.leafNodes++;
    }
    
    return node;
}

/*
================
R_CreateBVHNode

Create a BVH node
================
*/
bvhNode_t* R_CreateBVHNode(void) {
    bvhNode_t *node;
    
    if (sceneGraph.spatialTree.bvhPoolUsed >= sceneGraph.spatialTree.bvhPoolSize) {
        ri.Printf(PRINT_WARNING, "R_CreateBVHNode: pool exhausted\n");
        return NULL;
    }
    
    node = &sceneGraph.spatialTree.bvhPool[sceneGraph.spatialTree.bvhPoolUsed++];
    Com_Memset(node, 0, sizeof(*node));
    
    node->isLeaf = qtrue;  // Start as leaf
    node->maxNodes = 32;
    node->nodes = Z_Malloc(sizeof(sceneNode_t*) * node->maxNodes);
    
    sceneGraph.spatialTree.totalNodes++;
    sceneGraph.spatialTree.leafNodes++;
    
    return node;
}

/*
================
R_CullNode

Check if node is culled by frustum
================
*/
qboolean R_CullNode(const sceneNode_t *node, const cplane_t frustum[6]) {
    int i;
    
    if (!node) {
        return qtrue;
    }
    
    // Check against no-cull flag
    if (node->flags & NODE_FLAG_NO_CULL) {
        return qfalse;
    }
    
    // Check bounding sphere first (faster)
    for (i = 0; i < 6; i++) {
        float dist = DotProduct(node->worldMatrix + 12, frustum[i].normal) - frustum[i].dist;
        if (dist < -node->boundingRadius) {
            return qtrue;  // Completely outside
        }
    }
    
    // Check AABB for more accuracy
    return R_CullNodeBounds(node->worldMins, node->worldMaxs, frustum);
}

/*
================
R_CullNodeBounds

Check if AABB is culled by frustum
================
*/
qboolean R_CullNodeBounds(const vec3_t mins, const vec3_t maxs, const cplane_t frustum[6]) {
    int i;
    
    for (i = 0; i < 6; i++) {
        vec3_t corner;
        
        // Find the corner closest to the plane
        corner[0] = (frustum[i].normal[0] < 0) ? maxs[0] : mins[0];
        corner[1] = (frustum[i].normal[1] < 0) ? maxs[1] : mins[1];
        corner[2] = (frustum[i].normal[2] < 0) ? maxs[2] : mins[2];
        
        if (DotProduct(corner, frustum[i].normal) - frustum[i].dist < 0) {
            return qtrue;  // Outside frustum
        }
    }
    
    return qfalse;  // Inside or intersecting
}

/*
================
R_TraverseSceneGraph

Traverse scene graph with callback
================
*/
void R_TraverseSceneGraph(sceneNode_t *node, void (*callback)(sceneNode_t *node, void *data), void *data) {
    sceneNode_t *child;
    
    if (!node || !callback) {
        return;
    }
    
    // Process this node
    callback(node, data);
    sceneGraph.nodesTraversed++;
    
    // Traverse children
    for (child = node->firstChild; child; child = child->nextSibling) {
        R_TraverseSceneGraph(child, callback, data);
    }
}

/*
================
R_CollectVisibleNodes

Collect all visible nodes in frustum
================
*/
void R_CollectVisibleNodes(sceneNode_t *node, const cplane_t frustum[6], sceneNode_t **visibleNodes, int *numVisible) {
    sceneNode_t *child;
    
    if (!node || *numVisible >= MAX_SCENE_NODES) {
        return;
    }
    
    // Check visibility
    if (!(node->flags & NODE_FLAG_VISIBLE)) {
        return;
    }
    
    // Frustum cull
    if (R_CullNode(node, frustum)) {
        node->flags |= NODE_FLAG_CULLED;
        sceneGraph.nodesCulled++;
        return;
    }
    
    node->flags &= ~NODE_FLAG_CULLED;
    
    // Add to visible list
    visibleNodes[(*numVisible)++] = node;
    node->visFrame = tr.frameCount;
    
    // Check children
    for (child = node->firstChild; child; child = child->nextSibling) {
        R_CollectVisibleNodes(child, frustum, visibleNodes, numVisible);
    }
}

/*
================
R_PrintSceneGraphStats

Print scene graph statistics
================
*/
void R_PrintSceneGraphStats(void) {
    ri.Printf(PRINT_ALL, "Scene Graph Statistics:\n");
    ri.Printf(PRINT_ALL, "  Total nodes: %d / %d\n", sceneGraph.numNodes, MAX_SCENE_NODES);
    ri.Printf(PRINT_ALL, "  Model nodes: %d\n", sceneGraph.numModelNodes);
    ri.Printf(PRINT_ALL, "  Entity nodes: %d\n", sceneGraph.numEntityNodes);
    ri.Printf(PRINT_ALL, "  Light nodes: %d\n", sceneGraph.numLightNodes);
    ri.Printf(PRINT_ALL, "  Nodes traversed: %d\n", sceneGraph.nodesTraversed);
    ri.Printf(PRINT_ALL, "  Nodes culled: %d\n", sceneGraph.nodesCulled);
    ri.Printf(PRINT_ALL, "  Nodes drawn: %d\n", sceneGraph.nodesDrawn);
    
    if (sceneGraph.spatialTree.type == SPATIAL_OCTREE) {
        ri.Printf(PRINT_ALL, "  Octree nodes: %d (leaves: %d)\n", 
                 sceneGraph.spatialTree.totalNodes, sceneGraph.spatialTree.leafNodes);
    } else if (sceneGraph.spatialTree.type == SPATIAL_BVH) {
        ri.Printf(PRINT_ALL, "  BVH nodes: %d (leaves: %d)\n",
                 sceneGraph.spatialTree.totalNodes, sceneGraph.spatialTree.leafNodes);
    }
}