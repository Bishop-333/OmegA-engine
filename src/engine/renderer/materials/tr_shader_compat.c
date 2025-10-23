/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

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
#include "tr_material.h"

/*
================================================================================
Phase 3: Shader Compatibility Layer

This file provides backward compatibility between the new material system
and the existing shader system.
================================================================================
*/

/*
================
Material_FromShader

Convert existing shader_t to material_t
================
*/
material_t* Material_FromShader(shader_t *shader) {
    material_t *material;
    int i;
    
    if (!shader) {
        return NULL;
    }
    
    // Check if already converted
    if (shader->material) {
        return shader->material;
    }
    
    if (numMaterials >= MAX_MATERIALS) {
        ri.Printf(PRINT_WARNING, "WARNING: MAX_MATERIALS reached during shader conversion\n");
        return NULL;
    }
    
    // Allocate new material
    material = &materials[numMaterials++];
    Com_Memset(material, 0, sizeof(material_t));
    
    // Copy basic properties
    Q_strncpyz(material->name, shader->name, sizeof(material->name));
    material->index = material - materials;
    material->sort = shader->sort;
    material->surfaceFlags = shader->surfaceFlags;
    material->contentFlags = shader->contentFlags;
    material->cullType = shader->cullType;
    material->polygonOffset = shader->polygonOffset;
    material->portalRange = shader->portalRange;
    
    // Convert flags
    if (shader->noMipMaps) {
        material->materialFlags |= MATERIAL_NOMIPMAPS;
    }
    if (shader->noPicMip) {
        material->materialFlags |= MATERIAL_NOMIPMAPS;
    }
    if (shader->polygonOffset) {
        material->materialFlags |= MATERIAL_POLYGONOFFSET;
    }
    if (shader->entityMergable) {
        material->materialFlags |= MATERIAL_ENTITYMERGABLE;
    }
    if (shader->isSky) {
        material->isSky = qtrue;
        material->sky = shader->sky;
        material->materialFlags |= MATERIAL_SKY;
    }
    if (shader->fogPass) {
        material->isFog = qtrue;
        material->fogParms = shader->fogParms;
        material->materialFlags |= MATERIAL_FOG;
    }
    
    // Convert vertex attributes
    material->vertexAttribs = shader->vertexAttribs;
    if (shader->needsNormal) {
        material->materialFlags |= MATERIAL_NEEDSNORMAL;
    }
    if (shader->needsTangent) {
        material->materialFlags |= MATERIAL_NEEDSTANGENT;
    }
    if (shader->needsColor) {
        material->materialFlags |= MATERIAL_NEEDSCOLOR;
    }
    
    // Convert stages
    material->numStages = shader->numUnfoggedPasses;
    for (i = 0; i < shader->numUnfoggedPasses && i < MAX_MATERIAL_STAGES; i++) {
        if (shader->stages[i]) {
            Material_ConvertStage(&material->stages[i], shader->stages[i]);
        }
    }
    
    // Copy deforms
    material->numDeforms = shader->numDeforms;
    if (shader->numDeforms > 0) {
        Com_Memcpy(material->deforms, shader->deforms, 
                   sizeof(deformStage_t) * shader->numDeforms);
    }
    
    // Set static flag if appropriate
    material->isStaticMaterial = shader->isStaticShader;
    
    // Link back
    material->legacyShader = shader;
    shader->material = material;
    
    // Add to hash table
    Material_AddToHashTable(material);
    
    return material;
}

/*
================
Material_ConvertStage

Convert shader stage to material stage
================
*/
void Material_ConvertStage(materialStage_t *mStage, shaderStage_t *sStage) {
    int i, b;
    
    if (!mStage || !sStage) {
        return;
    }
    
    // Clear the stage
    Com_Memset(mStage, 0, sizeof(materialStage_t));
    
    mStage->active = sStage->active;
    
    // Copy texture bundles
    Com_Memcpy(mStage->bundle, sStage->bundle, sizeof(textureBundle_t) * NUM_TEXTURE_BUNDLES);
    
    if (sStage->bundle[0].isLightmap || sStage->bundle[0].tcGen == TCGEN_LIGHTMAP) {
        R_ReportLegacyLightmapUsage("MaterialStageFromShaderStage");
        mStage->active = qfalse;
        mStage->lighting = SL_NONE;
        return;
    }
    
    // Set primary texture
    if (sStage->bundle[0].image[0]) {
        mStage->colorMap = sStage->bundle[0].image[0];
    }
    
    // Convert texture coordinate generation
    mStage->tcGen = sStage->bundle[0].tcGen;
    if (mStage->tcGen == TCGEN_VECTOR) {
        for (i = 0; i < 2; i++) {
            for (b = 0; b < 3; b++) {
                mStage->tcGenVectors[i][b] = sStage->bundle[0].tcGenVectors[i][b];
            }
        }
    }
    
    // Copy texture modifications
    mStage->numTexMods = sStage->bundle[0].numTexMods;
    if (mStage->numTexMods > 0) {
        Com_Memcpy(mStage->texMods, sStage->bundle[0].texMods,
                   sizeof(texModInfo_t) * mStage->numTexMods);
    }
    
    // Copy generation functions from bundle
    mStage->rgbGen = sStage->bundle[0].rgbGen;
    mStage->rgbWave = sStage->bundle[0].rgbWave;
    mStage->alphaGen = sStage->bundle[0].alphaGen;
    mStage->alphaWave = sStage->bundle[0].alphaWave;
    Com_Memcpy(mStage->constantColor, &sStage->bundle[0].constantColor, sizeof(color4ub_t));
    
    // Copy state bits
    mStage->stateBits = sStage->stateBits;
    
    // Extract blend functions from state bits
    Material_ExtractBlendFunc(mStage);
    
    // Determine lighting mode (default to diffuse for compatibility)
    mStage->lighting = SL_DIFFUSE;
    
    // Handle detail flag
    mStage->isDetail = sStage->isDetail;
}

/*
================
Material_CreateShaderWrapper

Create shader wrapper for material
================
*/
shader_t* Material_CreateShaderWrapper(material_t *material) {
    shader_t *shader;
    int i;
    
    if (!material) {
        return NULL;
    }
    
    // Check if wrapper already exists
    if (material->legacyShader) {
        return material->legacyShader;
    }
    
    // Allocate shader
    shader = ri.Hunk_Alloc(sizeof(shader_t), h_low);
    Com_Memset(shader, 0, sizeof(shader_t));
    
    // Copy properties from material
    Q_strncpyz(shader->name, material->name, sizeof(shader->name));
    shader->index = tr.numShaders;
    shader->sort = material->sort;
    shader->surfaceFlags = material->surfaceFlags;
    shader->contentFlags = material->contentFlags;
    shader->cullType = material->cullType;
    shader->polygonOffset = material->polygonOffset;
    shader->vertexAttribs = material->vertexAttribs;
    shader->portalRange = material->portalRange;
    
    // Convert flags
    if (material->materialFlags & MATERIAL_NOMIPMAPS) {
        shader->noMipMaps = qtrue;
        shader->noPicMip = qtrue;
    }
    if (material->materialFlags & MATERIAL_ENTITYMERGABLE) {
        shader->entityMergable = qtrue;
    }
    if (material->materialFlags & MATERIAL_SKY) {
        shader->isSky = qtrue;
        shader->sky = material->sky;
    }
    if (material->materialFlags & MATERIAL_FOG) {
        shader->fogPass = qtrue;
        shader->fogParms = material->fogParms;
    }
    if (material->materialFlags & MATERIAL_NEEDSNORMAL) {
        shader->needsNormal = qtrue;
    }
    if (material->materialFlags & MATERIAL_NEEDSTANGENT) {
        shader->needsTangent = qtrue;
    }
    if (material->materialFlags & MATERIAL_NEEDSCOLOR) {
        shader->needsColor = qtrue;
    }
    
    // Link to material
    shader->material = material;
    material->legacyShader = shader;
    
    // Convert stages for compatibility
    shader->numUnfoggedPasses = material->numStages;
    for (i = 0; i < material->numStages && i < MAX_SHADER_STAGES; i++) {
        shader->stages[i] = Material_CreateStageWrapper(&material->stages[i]);
    }
    
    // Copy deforms
    shader->numDeforms = material->numDeforms;
    if (material->numDeforms > 0) {
        Com_Memcpy(shader->deforms, material->deforms,
                   sizeof(deformStage_t) * material->numDeforms);
    }
    
    // Set static flag
    shader->isStaticShader = material->isStaticMaterial;
    
    // Determine optimal stage iterator
    if (material->vertexAttribs & ATTR_COLOR) {
        shader->optimalStageIteratorFunc = RB_StageIteratorVertexLitTexture;
    } else {
        shader->optimalStageIteratorFunc = RB_StageIteratorGeneric;
    }
    
    // Add to shader list if there's room
    if (tr.numShaders < MAX_SHADERS) {
        tr.shaders[tr.numShaders] = shader;
        shader->index = tr.numShaders;
        shader->sortedIndex = tr.numShaders;
        tr.numShaders++;
    }
    
    return shader;
}

/*
================
Material_CreateStageWrapper

Create shader stage wrapper for material stage
================
*/
shaderStage_t* Material_CreateStageWrapper(materialStage_t *mStage) {
    shaderStage_t *sStage;
    int i;
    
    if (!mStage) {
        return NULL;
    }
    
    // Allocate stage
    sStage = ri.Hunk_Alloc(sizeof(shaderStage_t), h_low);
    Com_Memset(sStage, 0, sizeof(shaderStage_t));
    
    sStage->active = mStage->active;
    
    // Copy texture bundles
    Com_Memcpy(sStage->bundle, mStage->bundle, sizeof(textureBundle_t) * NUM_TEXTURE_BUNDLES);
    
    // Ensure primary texture is set
    if (mStage->colorMap && !sStage->bundle[0].image[0]) {
        sStage->bundle[0].image[0] = mStage->colorMap;
    }
    
    // Copy generation functions to bundle
    sStage->bundle[0].rgbGen = mStage->rgbGen;
    sStage->bundle[0].rgbWave = mStage->rgbWave;
    sStage->bundle[0].alphaGen = mStage->alphaGen;
    sStage->bundle[0].alphaWave = mStage->alphaWave;
    Com_Memcpy(&sStage->bundle[0].constantColor, mStage->constantColor, sizeof(color4ub_t));
    
    // Copy state bits
    sStage->stateBits = mStage->stateBits;
    
    // Handle special flags
    sStage->isDetail = mStage->isDetail;
    // Handle fog
    sStage->bundle[0].adjustColorsForFog = ACFF_NONE;
    if (mStage->stateBits & GLS_SRCBLEND_SRC_ALPHA) {
        sStage->bundle[0].adjustColorsForFog = ACFF_MODULATE_ALPHA;
    } else if (mStage->stateBits & GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA) {
        sStage->bundle[0].adjustColorsForFog = ACFF_MODULATE_RGBA;
    }
    
    return sStage;
}

/*
================
Material_ExtractBlendFunc

Extract blend source/dest from state bits
================
*/
void Material_ExtractBlendFunc(materialStage_t *stage) {
    unsigned int bits = stage->stateBits;
    
    // Extract source blend
    switch (bits & GLS_SRCBLEND_BITS) {
    case GLS_SRCBLEND_ZERO:
        stage->blendSrc = GL_ZERO;
        break;
    case GLS_SRCBLEND_ONE:
        stage->blendSrc = GL_ONE;
        break;
    case GLS_SRCBLEND_DST_COLOR:
        stage->blendSrc = GL_DST_COLOR;
        break;
    case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
        stage->blendSrc = GL_ONE_MINUS_DST_COLOR;
        break;
    case GLS_SRCBLEND_SRC_ALPHA:
        stage->blendSrc = GL_SRC_ALPHA;
        break;
    case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
        stage->blendSrc = GL_ONE_MINUS_SRC_ALPHA;
        break;
    case GLS_SRCBLEND_DST_ALPHA:
        stage->blendSrc = GL_DST_ALPHA;
        break;
    case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
        stage->blendSrc = GL_ONE_MINUS_DST_ALPHA;
        break;
    case GLS_SRCBLEND_ALPHA_SATURATE:
        stage->blendSrc = GL_SRC_ALPHA_SATURATE;
        break;
    default:
        stage->blendSrc = GL_ONE;
        break;
    }
    
    // Extract destination blend
    switch (bits & GLS_DSTBLEND_BITS) {
    case GLS_DSTBLEND_ZERO:
        stage->blendDst = GL_ZERO;
        break;
    case GLS_DSTBLEND_ONE:
        stage->blendDst = GL_ONE;
        break;
    case GLS_DSTBLEND_SRC_COLOR:
        stage->blendDst = GL_SRC_COLOR;
        break;
    case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
        stage->blendDst = GL_ONE_MINUS_SRC_COLOR;
        break;
    case GLS_DSTBLEND_SRC_ALPHA:
        stage->blendDst = GL_SRC_ALPHA;
        break;
    case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
        stage->blendDst = GL_ONE_MINUS_SRC_ALPHA;
        break;
    case GLS_DSTBLEND_DST_ALPHA:
        stage->blendDst = GL_DST_ALPHA;
        break;
    case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
        stage->blendDst = GL_ONE_MINUS_DST_ALPHA;
        break;
    default:
        stage->blendDst = GL_ZERO;
        break;
    }
}
