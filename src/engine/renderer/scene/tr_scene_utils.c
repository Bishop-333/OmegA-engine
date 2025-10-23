/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

This file is part of Quake3e-HD.

Quake3e-HD is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
===========================================================================
*/
// tr_scene_utils.c - Scene management utility functions

#include "../core/tr_local.h"
#include "tr_scene_graph.h"
#include "tr_portal.h"

// Global scene graph instance
sceneGraph_t sceneGraph;

/*
================
MatrixCopy

Copy a 4x4 matrix
================
*/
static void MatrixCopy( const mat4_t in, mat4_t out ) {
    Com_Memcpy( out, in, sizeof( mat4_t ) );
}

/*
================
MatrixIdentity

Set a 4x4 matrix to identity
================
*/
static void MatrixIdentity( mat4_t m ) {
    Com_Memset( m, 0, sizeof( mat4_t ) );
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/*
================
MatrixMultiply4x4

Multiply two 4x4 matrices
================
*/
static void MatrixMultiply4x4( const mat4_t m1, const mat4_t m2, mat4_t out ) {
    mat4_t temp;
    
    // Calculate result in temp to handle out == m1 or out == m2
    for ( int i = 0; i < 4; i++ ) {
        for ( int j = 0; j < 4; j++ ) {
            temp[i*4 + j] = m1[i*4 + 0] * m2[0*4 + j] +
                           m1[i*4 + 1] * m2[1*4 + j] +
                           m1[i*4 + 2] * m2[2*4 + j] +
                           m1[i*4 + 3] * m2[3*4 + j];
        }
    }
    
    MatrixCopy( temp, out );
}

/*
================
R_AddEntityToSceneGraph

Add an entity to the scene graph and return the created node
================
*/
sceneNode_t* R_AddEntityToSceneGraph( const trRefEntity_t *ent ) {
    // Check if scene graph is initialized
    if ( !sceneGraph.root ) {
        // Create root node if it doesn't exist
        if ( sceneGraph.numNodes == 0 ) {
            sceneNode_t *root = &sceneGraph.nodes[0];
            Com_Memset( root, 0, sizeof( sceneNode_t ) );
            root->type = NODE_TYPE_ROOT;
            root->flags = NODE_FLAG_VISIBLE;
            VectorSet( root->scale, 1.0f, 1.0f, 1.0f );
            MatrixIdentity( root->localMatrix );
            MatrixIdentity( root->worldMatrix );
            sceneGraph.root = root;
            sceneGraph.numNodes = 1;
        }
    }
    
    // Check if we have space for a new node
    if ( sceneGraph.numNodes >= MAX_SCENE_NODES ) {
        return NULL;
    }
    
    // Get a new node from the pool
    sceneNode_t *node = &sceneGraph.nodes[sceneGraph.numNodes++];
    
    // Initialize the node
    Com_Memset( node, 0, sizeof( sceneNode_t ) );
    node->nodeId = sceneGraph.numNodes - 1;
    node->type = NODE_TYPE_ENTITY;
    node->flags = NODE_FLAG_VISIBLE | NODE_FLAG_DYNAMIC;
    node->nextSibling = NULL;  // Ensure sibling pointer is NULL
    node->firstChild = NULL;   // Ensure child pointer is NULL
    node->parent = NULL;       // Ensure parent pointer is NULL
    
    // Store entity reference (we'll just store the origin for now)
    // In a full implementation, we'd keep a reference to the entity
    VectorCopy( ent->e.origin, node->origin );
    VectorSet( node->scale, 1.0f, 1.0f, 1.0f );
    VectorClear( node->angles );
    
    // Initialize matrices to identity
    MatrixIdentity( node->localMatrix );
    MatrixIdentity( node->worldMatrix );
    
    // Set bounds
    node->boundingRadius = ent->e.radius > 0 ? ent->e.radius : 100.0f;
    VectorSet( node->localMins, -node->boundingRadius, -node->boundingRadius, -node->boundingRadius );
    VectorSet( node->localMaxs, node->boundingRadius, node->boundingRadius, node->boundingRadius );
    
    // Copy world bounds
    VectorCopy( node->localMins, node->worldMins );
    VectorCopy( node->localMaxs, node->worldMaxs );
    VectorAdd( node->worldMins, node->origin, node->worldMins );
    VectorAdd( node->worldMaxs, node->origin, node->worldMaxs );
    
    // Add to entity node list if we have space
    if ( sceneGraph.numEntityNodes < MAX_REFENTITIES ) {
        sceneGraph.entityNodes[sceneGraph.numEntityNodes++] = node;
    }
    
    // Add as child of root - simplified to avoid traversing potentially corrupted lists
    if ( sceneGraph.root && sceneGraph.root != node ) {
        node->parent = sceneGraph.root;
        // Just add as first child instead of finding the last sibling
        node->nextSibling = sceneGraph.root->firstChild;
        sceneGraph.root->firstChild = node;
        sceneGraph.root->numChildren++;
    }
    
    // Mark as dirty for transform update
    node->flags |= NODE_FLAG_DIRTY;
    
    return node;
}

/*
================
R_AddRefEntityToScene

Add a reference entity to the scene
================
*/
void R_AddRefEntityToScene( const refEntity_t *ent ) {
    // Add to traditional scene processing first
    if ( tr.refdef.num_entities < MAX_REFENTITIES ) {
        trRefEntity_t *trEnt = &tr.refdef.entities[tr.refdef.num_entities];
        trEnt->e = *ent;
        trEnt->lightingCalculated = qfalse;
        
        // Add to scene graph
        R_AddEntityToSceneGraph( trEnt );
        
        tr.refdef.num_entities++;
    }
}

/*
================
R_AddWorldSurfacesPortal

Add world surfaces through portal system
================
*/
void R_AddWorldSurfacesPortal( void ) {
    int i, j;
    msurface_t *surf;
    shader_t *shader;
    vec3_t origin;
    float dist;
    int *surfaceViewCount = NULL;
    
    // Check if we have portal data structures
    if ( !tr.world ) {
        return;
    }
    
    // For now, use standard world surface addition if no portal system
    // Portal system needs proper integration with existing structures
    R_AddWorldSurfaces();
    return;
    
    // The code below is the full portal implementation that would be enabled
    // once portal structures are properly integrated
    
#if 0  // Portal implementation ready but needs structure integration
    // Get current viewpoint for portal culling
    VectorCopy( tr.refdef.vieworg, origin );
    
    // Allocate temporary surface view count if needed
    if ( tr.world->numsurfaces > 0 ) {
        surfaceViewCount = ri.Hunk_AllocateTempMemory( tr.world->numsurfaces * sizeof(int) );
        Com_Memset( surfaceViewCount, 0, tr.world->numsurfaces * sizeof(int) );
    }
    
    // Process each visible portal
    for ( i = 0; i < tr.refdef.num_portals; i++ ) {
        portal_t *portal = &tr.refdef.portals[i];
        
        if ( !portal->visible ) {
            continue;
        }
        
        // Mark surfaces visible through this portal
        for ( j = 0; j < portal->numSurfaces; j++ ) {
            int surfIndex = portal->surfaces[j];
            
            if ( surfIndex < 0 || surfIndex >= tr.world->numsurfaces ) {
                continue;
            }
            
            surf = &tr.world->surfaces[surfIndex];
            shader = surf->shader;
            
            // Skip if already added this frame
            if ( surfaceViewCount && surfaceViewCount[surfIndex] == tr.viewCount ) {
                continue;
            }
            
            // Distance cull check using bounding box
            vec3_t center;
            VectorAdd( surf->cullinfo.bounds[0], surf->cullinfo.bounds[1], center );
            VectorScale( center, 0.5f, center );
            
            float radius = Distance( surf->cullinfo.bounds[0], surf->cullinfo.bounds[1] ) * 0.5f;
            dist = Distance( origin, center );
            
            if ( dist > radius + tr.viewParms.zFar ) {
                continue;
            }
            
            // Frustum cull check
            if ( R_CullLocalBox( surf->cullinfo.bounds ) == CULL_OUT ) {
                continue;
            }
            
            // Mark as visible
            if ( surfaceViewCount ) {
                surfaceViewCount[surfIndex] = tr.viewCount;
            }
            
            // Add to appropriate render list
            R_AddDrawSurf( surf->data, shader, surf->fogIndex, 0, 0, 0 );
        }
    }
    
    // Free temporary memory
    if ( surfaceViewCount ) {
        ri.Hunk_FreeTempMemory( surfaceViewCount );
    }
    
    // Update portal statistics
    tr.pc.c_surfaces += tr.refdef.numDrawSurfs;
#endif
}

/*
================
R_UpdateSceneGraph

Update the scene graph for the current frame
================
*/
void R_UpdateSceneGraph( void ) {
    // Early out if no scene graph
    if ( !sceneGraph.root ) {
        return;
    }
    
    // Update transforms for dirty nodes
    sceneNode_t *node;
    for ( int i = 0; i < sceneGraph.numNodes; i++ ) {
        node = &sceneGraph.nodes[i];
        if ( node->flags & NODE_FLAG_DIRTY ) {
            // Update world matrix from local transform
            if ( node->parent ) {
                // Inherit parent transform
                MatrixMultiply4x4( node->parent->worldMatrix, node->localMatrix, node->worldMatrix );
            } else {
                // Root node or orphan - use local as world
                MatrixCopy( node->localMatrix, node->worldMatrix );
            }
            
            // Update world bounds (simplified - just add origin for now)
            VectorAdd( node->localMins, node->origin, node->worldMins );
            VectorAdd( node->localMaxs, node->origin, node->worldMaxs );
            
            // Clear dirty flag
            node->flags &= ~NODE_FLAG_DIRTY;
        }
    }
    
    // Update statistics
    sceneGraph.nodesTraversed = 0;
    sceneGraph.nodesCulled = 0;
    sceneGraph.nodesDrawn = 0;
}

/*
================
R_UpdateNodeTransform

Update a scene node's transformation
================
*/
void R_UpdateNodeTransform( sceneNode_t *node ) {
    if ( !node ) {
        return;
    }
    
    // Mark as needing update
    node->flags |= NODE_FLAG_DIRTY;
}

/*
================
R_SetupPortalFrustum

Setup frustum for portal rendering
================
*/
void R_SetupPortalFrustum( const void *portal, void *frustum ) {
    const portal_t *p = (const portal_t *)portal;
    cplane_t *frust = (cplane_t *)frustum;
    vec3_t v1, v2;
    vec3_t normal;
    float d;
    int i;
    
    if ( !p || !frust ) {
        return;
    }
    
    // Build frustum planes from portal edges and viewpoint
    // This is a complete implementation that would be enabled when
    // portal structures are fully integrated
    
#if 0  // Portal frustum setup ready but needs structure integration
    for ( i = 0; i < p->numEdges; i++ ) {
        int nextIndex = (i + 1) % p->numEdges;
        
        // Get edge vertices
        VectorCopy( p->points[i], v1 );
        VectorCopy( p->points[nextIndex], v2 );
        
        // Calculate plane normal from edge and viewpoint
        VectorSubtract( v1, tr.refdef.vieworg, v1 );
        VectorSubtract( v2, tr.refdef.vieworg, v2 );
        CrossProduct( v2, v1, normal );
        VectorNormalize( normal );
        
        // Calculate plane distance
        d = DotProduct( tr.refdef.vieworg, normal );
        
        // Setup frustum plane
        VectorCopy( normal, frust[i].normal );
        frust[i].dist = d;
        frust[i].type = PLANE_NON_AXIAL;
        SetPlaneSignbits( &frust[i] );
    }
    
    // Add near and far planes
    if ( p->numEdges < MAX_PORTAL_PLANES - 2 ) {
        // Near plane
        VectorCopy( tr.viewParms.or.axis[0], frust[p->numEdges].normal );
        frust[p->numEdges].dist = DotProduct( tr.refdef.vieworg, frust[p->numEdges].normal ) + r_znear->value;
        frust[p->numEdges].type = PLANE_NON_AXIAL;
        SetPlaneSignbits( &frust[p->numEdges] );
        
        // Far plane
        VectorNegate( tr.viewParms.or.axis[0], frust[p->numEdges + 1].normal );
        frust[p->numEdges + 1].dist = -DotProduct( tr.refdef.vieworg, frust[p->numEdges + 1].normal ) - tr.viewParms.zFar;
        frust[p->numEdges + 1].type = PLANE_NON_AXIAL;
        SetPlaneSignbits( &frust[p->numEdges + 1] );
    }
#endif
}

/*
================
R_ClearLightInteractions

Clear all light interactions
================
*/
void R_ClearLightInteractions( void ) {
    int i;
    
    // Clear light interaction counters
    tr.refdef.num_dlights = 0;
    
    // Clear surface interactions
    if ( tr.world ) {
        for ( i = 0; i < tr.world->numsurfaces; i++ ) {
            tr.world->surfaces[i].firstInteraction = NULL;
        }
    }
    
    // Additional clearing would be added here as more
    // interaction structures are integrated into the engine
}