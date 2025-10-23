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
Phase 3: Material Optimization

This file implements optimization routines for the material system,
including stage merging, pattern detection, and precomputation.
================================================================================
*/

// Forward declarations
static qboolean Material_StagesCanMerge(materialStage_t *stage1, materialStage_t *stage2);
static void Material_MergeStageInto(materialStage_t *dest, materialStage_t *src);
static void Material_CompactStages(material_t *material);
// ParseWaveForm is declared in tr_material.h and defined in tr_material.c
float COM_ParseFloat(char **text);

/*
================
Material_OptimizeStages

Main optimization function for material stages
================
*/
void Material_OptimizeStages(material_t *material) {
    if (!material) {
        return;
    }
    
    // Merge compatible stages
    Material_MergeStages(material);
    
    // Detect common patterns
    Material_DetectLightmapPattern(material);
    
    // Precompute static values
    Material_PrecomputeStatic(material);
    
    // Select optimal rendering path
    Material_SelectRenderPath(material);
}

/*
================
Material_MergeStages

Merge compatible stages to reduce state changes
================
*/
void Material_MergeStages(material_t *material) {
    int i, j;
    materialStage_t *stage1, *stage2;
    
    if (material->numStages < 2) {
        return;
    }
    
    // Look for stages that can be merged
    for (i = 0; i < material->numStages - 1; i++) {
        stage1 = &material->stages[i];
        
        if (!stage1->active) {
            continue;
        }
        
        for (j = i + 1; j < material->numStages; j++) {
            stage2 = &material->stages[j];
            
            if (!stage2->active) {
                continue;
            }
            
            // Check if stages can be merged
            if (Material_StagesCanMerge(stage1, stage2)) {
                Material_MergeStageInto(stage1, stage2);
                stage2->active = qfalse;
                
                ri.Printf(PRINT_DEVELOPER, "Merged stages %d and %d in material '%s'\n",
                          i, j, material->name);
            }
        }
    }
    
    // Compact stages to remove inactive ones
    Material_CompactStages(material);
}

/*
================
Material_StagesCanMerge

Check if two stages can be merged
================
*/
static qboolean Material_StagesCanMerge(materialStage_t *stage1, materialStage_t *stage2) {
    // Can't merge if either has complex texture coordinate generation
    if (stage1->tcGen != TCGEN_TEXTURE || stage2->tcGen != TCGEN_TEXTURE) {
        if (stage1->tcGen != TCGEN_LIGHTMAP && stage2->tcGen != TCGEN_LIGHTMAP) {
            return qfalse;
        }
    }
    
    // Can't merge if either has texture coordinate modifications
    if (stage1->numTexMods > 0 || stage2->numTexMods > 0) {
        return qfalse;
    }
    
    // Can't merge if both have animated textures
    if (stage1->bundle[0].numImageAnimations > 1 && 
        stage2->bundle[0].numImageAnimations > 1) {
        return qfalse;
    }
    
    // Check for multi-texture blend
    if (stage1->bundle[0].image[0] && stage2->bundle[0].image[0]) {
        // Both stages have textures, check if blend modes are compatible
        if ((stage1->stateBits & GLS_DEPTHMASK_TRUE) &&
            (stage2->stateBits & (GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO))) {
            return qtrue;
        }
    }
    
    return qfalse;
}

/*
================
Material_MergeStageInto

Merge stage2 into stage1
================
*/
static void Material_MergeStageInto(materialStage_t *stage1, materialStage_t *stage2) {
    if (!stage1->bundle[1].image[0]) {
        stage1->bundle[1] = stage2->bundle[0];

        if (stage2->stateBits & GLS_SRCBLEND_BITS) {
            stage1->stateBits |= stage2->stateBits;
        }
    }
}

/*
================
Material_CompactStages

Remove inactive stages and compact the array
================
*/
static void Material_CompactStages(material_t *material) {
    int readIndex, writeIndex;
    materialStage_t tempStage;
    
    writeIndex = 0;
    for (readIndex = 0; readIndex < material->numStages; readIndex++) {
        if (material->stages[readIndex].active) {
            if (readIndex != writeIndex) {
                tempStage = material->stages[readIndex];
                material->stages[writeIndex] = tempStage;
            }
            writeIndex++;
        }
    }
    
    material->numStages = writeIndex;
}

/*
================
Material_DetectLightmapPattern

Detect standard lightmap pattern for optimization
================
*/
void Material_DetectLightmapPattern(material_t *material) {
    materialStage_t *stage0, *stage1;
    
    if (material->numStages >= 2) {
        R_ReportLegacyLightmapUsage("Material_DetectLightmapPattern");
    }
}

/*
================
Material_PrecomputeStatic

Precompute static expression values and properties
================
*/
void Material_PrecomputeStatic(material_t *material) {
    int i;
    qboolean isStatic = qtrue;
    materialStage_t *stage;
    
    // Check if material has only static content
    for (i = 0; i < material->numStages; i++) {
        stage = &material->stages[i];
        
        if (!stage->active) {
            continue;
        }
        
        // Check for dynamic color generation
        if (stage->rgbGen != CGEN_IDENTITY && 
            stage->rgbGen != CGEN_CONST &&
            stage->rgbGen != CGEN_IDENTITY_LIGHTING &&
            stage->rgbGen != CGEN_LIGHTING_DIFFUSE) {
            isStatic = qfalse;
            break;
        }
        
        // Check for dynamic alpha generation
        if (stage->alphaGen != AGEN_IDENTITY && 
            stage->alphaGen != AGEN_CONST &&
            stage->alphaGen != AGEN_SKIP) {
            isStatic = qfalse;
            break;
        }
        
        // Check for texture modifications
        if (stage->numTexMods > 0) {
            isStatic = qfalse;
            break;
        }
        
        // Check for animated textures
        if (stage->bundle[0].numImageAnimations > 1) {
            isStatic = qfalse;
            break;
        }
        
        // Check for waveform modifications
        if (stage->rgbWave.func != GF_NONE || stage->alphaWave.func != GF_NONE) {
            isStatic = qfalse;
            break;
        }
    }
    
    // Check for vertex deformations
    if (material->numDeforms > 0) {
        isStatic = qfalse;
    }
    
    // Check for expressions
    if (material->numExpressions > 0) {
        isStatic = qfalse;
    }
    
    material->isStaticMaterial = isStatic;
    
    if (isStatic) {
        ri.Printf(PRINT_DEVELOPER, "Material '%s' marked as static\n", material->name);
    }
    
    // Compute required vertex attributes
    Material_ComputeVertexAttribs(material);
}

/*
================
Material_ComputeVertexAttribs

Compute required vertex attributes for material
================
*/
void Material_ComputeVertexAttribs(material_t *material) {
    int i, j;
    materialStage_t *stage;
    int attribs = 0;
    
    // Always need position
    attribs |= ATTR_POSITION;
    
    // Check each stage for requirements
    for (i = 0; i < material->numStages; i++) {
        stage = &material->stages[i];
        
        if (!stage->active) {
            continue;
        }
        
        // Check texture coordinate requirements
        if (stage->bundle[0].image[0] || stage->colorMap) {
            switch (stage->tcGen) {
            case TCGEN_TEXTURE:
                attribs |= ATTR_TEXCOORD;
                break;
            case TCGEN_LIGHTMAP:
                attribs |= ATTR_TEXCOORD | ATTR_LIGHTCOORD;
                break;
            case TCGEN_ENVIRONMENT_MAPPED:
                attribs |= ATTR_NORMAL;
                break;
            }
        }
        
        // Check color generation
        switch (stage->rgbGen) {
        case CGEN_EXACT_VERTEX:
        case CGEN_VERTEX:
        case CGEN_EXACT_VERTEX_LIT:
        case CGEN_VERTEX_LIT:
        case CGEN_ONE_MINUS_VERTEX:
            attribs |= ATTR_COLOR;
            break;
        case CGEN_LIGHTING_DIFFUSE:
            attribs |= ATTR_NORMAL;
            break;
        }
        
        // Check alpha generation
        switch (stage->alphaGen) {
        case AGEN_VERTEX:
        case AGEN_ONE_MINUS_VERTEX:
            attribs |= ATTR_COLOR;
            break;
        case AGEN_LIGHTING_SPECULAR:
            attribs |= ATTR_NORMAL;
            break;
        }
        
        // Check for normal mapping
        if (stage->normalMap) {
            attribs |= ATTR_NORMAL | ATTR_TANGENT;
        }
        
        // Check lighting mode
        if (stage->lighting == SL_BUMP || stage->lighting == SL_SPECULAR) {
            attribs |= ATTR_NORMAL | ATTR_TANGENT;
        }
    }
    
    // Check for vertex deformations
    for (i = 0; i < material->numDeforms; i++) {
        switch (material->deforms[i].deformation) {
        case DEFORM_WAVE:
        case DEFORM_NORMALS:
        case DEFORM_BULGE:
            attribs |= ATTR_NORMAL;
            break;
        case DEFORM_MOVE:
            attribs |= ATTR_POSITION;
            break;
        }
    }
    
    // Check material flags
    if (material->materialFlags & MATERIAL_NEEDSNORMAL) {
        attribs |= ATTR_NORMAL;
    }
    if (material->materialFlags & MATERIAL_NEEDSTANGENT) {
        attribs |= ATTR_TANGENT;
    }
    if (material->materialFlags & MATERIAL_NEEDSCOLOR) {
        attribs |= ATTR_COLOR;
    }
    
    material->vertexAttribs = attribs;
}

/*
================
Material_SelectRenderPath

Select optimal rendering path based on material properties
================
*/
void Material_SelectRenderPath(material_t *material) {
    // Already set by lightmap detection?
    if (material->optimalStageIteratorFunc) {
        return;
    }
    
    // Single stage materials
    if (material->numStages == 1) {
        if (material->vertexAttribs & ATTR_COLOR) {
            material->optimalStageIteratorFunc = RB_StageIteratorVertexLitTexture;
        } else {
            material->optimalStageIteratorFunc = RB_StageIteratorGeneric;
        }
        return;
    }
    
    // Multi-stage materials
    material->optimalStageIteratorFunc = RB_StageIteratorGeneric;
}

/*
================
Material_Validate

Validate material and fix common issues
================
*/
void Material_Validate(material_t *material) {
    int i;
    materialStage_t *stage;
    
    // Check for empty material
    if (material->numStages == 0) {
        ri.Printf(PRINT_WARNING, "WARNING: material '%s' has no stages\n", material->name);
        Material_CreateDefaultStage(material);
    }
    
    // Validate each stage
    for (i = 0; i < material->numStages; i++) {
        stage = &material->stages[i];
        
        if (!stage->active) {
            continue;
        }
        
        // Ensure at least one texture
        if (!stage->colorMap && !stage->bundle[0].image[0]) {
            ri.Printf(PRINT_WARNING, "WARNING: material '%s' stage %d has no texture\n", 
                      material->name, i);
            stage->bundle[0].image[0] = tr.defaultImage;
            stage->colorMap = tr.defaultImage;
        }
        
        // Check blend function validity
        Material_ValidateBlendFunc(stage);
        
        // Validate texture coordinates
        Material_ValidateTCGen(stage);
    }
    
    // Check sort order
    if (material->sort < SS_BAD || material->sort > SS_MAX) {
        ri.Printf(PRINT_WARNING, "WARNING: material '%s' has invalid sort value %f\n", 
                  material->name, material->sort);
        material->sort = SS_OPAQUE;
    }
    
    // Validate cull type
    if (material->cullType < 0 || material->cullType > CT_TWO_SIDED) {
        material->cullType = CT_FRONT_SIDED;
    }
}

/*
================
Material_ValidateBlendFunc

Validate and fix blend function
================
*/
void Material_ValidateBlendFunc(materialStage_t *stage) {
    unsigned int bits = stage->stateBits;
    
    // Check for invalid blend combinations
    if ((bits & GLS_SRCBLEND_BITS) && !(bits & GLS_DSTBLEND_BITS)) {
        // Source blend without destination blend - add default
        stage->stateBits |= GLS_DSTBLEND_ZERO;
    }
    
    if (!(bits & GLS_SRCBLEND_BITS) && (bits & GLS_DSTBLEND_BITS)) {
        // Destination blend without source blend - add default
        stage->stateBits |= GLS_SRCBLEND_ONE;
    }
    
    // Check for opaque blend that should have depth write
    if ((bits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == 0) {
        stage->stateBits |= GLS_DEPTHMASK_TRUE;
    }
}

/*
================
Material_ValidateTCGen

Validate texture coordinate generation
================
*/
void Material_ValidateTCGen(materialStage_t *stage) {
    // Validate tcGen type
    if (stage->tcGen < TCGEN_BAD || stage->tcGen >= TCGEN_MAX) {
        stage->tcGen = TCGEN_TEXTURE;
    }
    
    // Validate texture modifications
    if (stage->numTexMods < 0 || stage->numTexMods > MAX_SHADER_STAGE_TEXMODS) {
        stage->numTexMods = 0;
    }
}

/*
================
Material_CreateDefaultStage

Create a default stage for an empty material
================
*/
void Material_CreateDefaultStage(material_t *material) {
    materialStage_t *stage;
    
    stage = &material->stages[0];
    Com_Memset(stage, 0, sizeof(materialStage_t));
    
    stage->active = qtrue;
    stage->bundle[0].image[0] = tr.defaultImage;
    stage->colorMap = tr.defaultImage;
    stage->rgbGen = CGEN_IDENTITY;
    stage->alphaGen = AGEN_CONST;
    stage->constantColor[0] = 255;
    stage->constantColor[1] = 255;
    stage->constantColor[2] = 255;
    stage->constantColor[3] = 255;
    stage->stateBits = GLS_DEFAULT;
    stage->tcGen = TCGEN_TEXTURE;
    
    material->numStages = 1;
}

/*
================
Material_ParseRGBGen

Parse RGB generation
================
*/
void Material_ParseRGBGen(material_t *material, materialStage_t *stage, char **text) {
    char *token;
    
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: missing parameter for 'rgbgen' in material '%s'\n",
                  material->name);
        return;
    }
    
    if (!Q_stricmp(token, "identity")) {
        stage->rgbGen = CGEN_IDENTITY;
    } else if (!Q_stricmp(token, "identitylighting")) {
        stage->rgbGen = CGEN_IDENTITY_LIGHTING;
    } else if (!Q_stricmp(token, "const") || !Q_stricmp(token, "constant")) {
        stage->rgbGen = CGEN_CONST;
        stage->constantColor[0] = 255 * COM_ParseFloat(text);
        stage->constantColor[1] = 255 * COM_ParseFloat(text);
        stage->constantColor[2] = 255 * COM_ParseFloat(text);
    } else if (!Q_stricmp(token, "wave")) {
        stage->rgbGen = CGEN_WAVEFORM;
        ParseWaveForm(text, &stage->rgbWave);
    } else if (!Q_stricmp(token, "entity")) {
        stage->rgbGen = CGEN_ENTITY;
    } else if (!Q_stricmp(token, "oneminusentity")) {
        stage->rgbGen = CGEN_ONE_MINUS_ENTITY;
    } else if (!Q_stricmp(token, "vertex")) {
        stage->rgbGen = CGEN_VERTEX;
        material->materialFlags |= MATERIAL_NEEDSCOLOR;
    } else if (!Q_stricmp(token, "oneminusvertex")) {
        stage->rgbGen = CGEN_ONE_MINUS_VERTEX;
        material->materialFlags |= MATERIAL_NEEDSCOLOR;
    } else if (!Q_stricmp(token, "lightingdiffuse")) {
        stage->rgbGen = CGEN_LIGHTING_DIFFUSE;
        material->materialFlags |= MATERIAL_NEEDSNORMAL;
    } else {
        ri.Printf(PRINT_WARNING, "WARNING: unknown rgbgen '%s' in material '%s'\n",
                  token, material->name);
    }
}

/*
================
Material_ParseAlphaGen

Parse alpha generation
================
*/
void Material_ParseAlphaGen(material_t *material, materialStage_t *stage, char **text) {
    char *token;
    
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: missing parameter for 'alphagen' in material '%s'\n",
                  material->name);
        return;
    }
    
    if (!Q_stricmp(token, "identity")) {
        stage->alphaGen = AGEN_IDENTITY;
    } else if (!Q_stricmp(token, "const") || !Q_stricmp(token, "constant")) {
        stage->alphaGen = AGEN_CONST;
        stage->constantColor[3] = 255 * COM_ParseFloat(text);
    } else if (!Q_stricmp(token, "wave")) {
        stage->alphaGen = AGEN_WAVEFORM;
        ParseWaveForm(text, &stage->alphaWave);
    } else if (!Q_stricmp(token, "entity")) {
        stage->alphaGen = AGEN_ENTITY;
    } else if (!Q_stricmp(token, "oneminusentity")) {
        stage->alphaGen = AGEN_ONE_MINUS_ENTITY;
    } else if (!Q_stricmp(token, "vertex")) {
        stage->alphaGen = AGEN_VERTEX;
        material->materialFlags |= MATERIAL_NEEDSCOLOR;
    } else if (!Q_stricmp(token, "oneminusvertex")) {
        stage->alphaGen = AGEN_ONE_MINUS_VERTEX;
        material->materialFlags |= MATERIAL_NEEDSCOLOR;
    } else if (!Q_stricmp(token, "lightingspecular")) {
        stage->alphaGen = AGEN_LIGHTING_SPECULAR;
        material->materialFlags |= MATERIAL_NEEDSNORMAL;
    } else if (!Q_stricmp(token, "portal")) {
        stage->alphaGen = AGEN_PORTAL;
    } else {
        ri.Printf(PRINT_WARNING, "WARNING: unknown alphagen '%s' in material '%s'\n",
                  token, material->name);
    }
}

/*
================
COM_ParseFloat

Parse a float value from string
================
*/
float COM_ParseFloat(char **text) {
    char *token = COM_ParseExt(text, qfalse);
    return atof(token);
}

// ParseWaveForm is now defined in tr_material.c and shared
