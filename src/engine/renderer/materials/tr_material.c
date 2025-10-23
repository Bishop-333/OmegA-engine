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
#include "tr_material_override.h"

/*
================================================================================
Phase 3: Material System Implementation

This file implements the core material parsing and management functionality.
================================================================================
*/

// Global material storage
material_t materials[MAX_MATERIALS];
int numMaterials = 0;
material_t* materialHashTable[MATERIAL_HASH_SIZE];

// Memory pools
static materialMemPool_t expressionPool;
static materialMemPool_t stringPool;

// CVars
cvar_t *r_materialParm0;
cvar_t *r_materialParm1;
cvar_t *r_materialParm2;
cvar_t *r_materialParm3;

/*
================
R_InitMaterialSystem

Initialize the material system
================
*/
void R_InitMaterialSystem(void) {
    ri.Printf(PRINT_ALL, "Initializing material system...\n");
    
    // Clear material array
    Com_Memset(materials, 0, sizeof(materials));
    numMaterials = 0;
    
    // Clear hash table
    Com_Memset(materialHashTable, 0, sizeof(materialHashTable));
    
    // Allocate memory pools
    expressionPool.size = 256 * 1024;  // 256KB for expressions
    expressionPool.base = ri.Hunk_Alloc(expressionPool.size, h_low);
    expressionPool.used = 0;
    
    stringPool.size = 128 * 1024;      // 128KB for strings
    stringPool.base = ri.Hunk_Alloc(stringPool.size, h_low);
    stringPool.used = 0;
    
    // Register CVars
    r_materialParm0 = ri.Cvar_Get("r_materialParm0", "0", 0);
    r_materialParm1 = ri.Cvar_Get("r_materialParm1", "0", 0);
    r_materialParm2 = ri.Cvar_Get("r_materialParm2", "0", 0);
    r_materialParm3 = ri.Cvar_Get("r_materialParm3", "0", 0);
    
    // Register material commands
    Material_RegisterCommands();
    
    // Initialize material override system
    MatOver_Init();
    
    ri.Printf(PRINT_ALL, "Material system initialized\n");
}

/*
================
R_ShutdownMaterialSystem

Cleanup material system
================
*/
void R_ShutdownMaterialSystem(void) {
    // Shutdown material override system
    MatOver_Shutdown();
    
    // Memory is managed by the hunk allocator
    ri.Printf(PRINT_ALL, "Material system shutdown\n");
}

/*
================
Material_HashKey

Generate hash key for material name
================
*/
static int Material_HashKey(const char *name) {
    int hash = 0;
    int c;
    
    while ((c = *name++) != '\0') {
        hash = (hash << 5) - hash + c;
    }
    
    return (hash & (MATERIAL_HASH_SIZE - 1));
}

/*
================
Material_Find

Find material by name
================
*/
material_t* Material_Find(const char *name) {
    int hash;
    material_t *mat;
    
    if (!name || !name[0]) {
        return NULL;
    }
    
    hash = Material_HashKey(name);
    
    for (mat = materialHashTable[hash]; mat; mat = mat->next) {
        if (!Q_stricmp(mat->name, name)) {
            return mat;
        }
    }
    
    return NULL;
}

/*
================
Material_AddToHashTable

Add material to hash table
================
*/
void Material_AddToHashTable(material_t *material) {
    int hash;
    
    if (!material || !material->name[0]) {
        return;
    }
    
    hash = Material_HashKey(material->name);
    material->next = materialHashTable[hash];
    materialHashTable[hash] = material;
}

/*
================
Material_Parse

Parse material from shader text
================
*/
material_t* Material_Parse(char *name, char **text) {
    material_t  *material;
    char        *token;
    int         stage = -1;
    
    if (numMaterials >= MAX_MATERIALS) {
        ri.Printf(PRINT_WARNING, "WARNING: MAX_MATERIALS reached\n");
        return NULL;
    }
    
    // Allocate new material
    material = &materials[numMaterials++];
    Com_Memset(material, 0, sizeof(material_t));
    
    // Set defaults
    Q_strncpyz(material->name, name, sizeof(material->name));
    material->index = numMaterials - 1;
    material->cullType = CT_FRONT_SIDED;
    material->sort = SS_OPAQUE;
    
    // Initialize expression registers
    material->expressionRegisters[REG_TIME] = 0;
    material->numRegisters = MAX_EXPRESSION_REGISTERS;
    
    // Parse material body
    token = COM_ParseExt(text, qtrue);
    if (token[0] != '{') {
        ri.Printf(PRINT_WARNING, "WARNING: expecting '{', found '%s' in material '%s'\n", 
                  token, name);
        numMaterials--;
        return NULL;
    }
    
    while (1) {
        token = COM_ParseExt(text, qtrue);
        
        if (!token[0]) {
            ri.Printf(PRINT_WARNING, "WARNING: unexpected end of material '%s'\n", name);
            break;
        }
        
        if (token[0] == '}') {
            break;
        }
        
        // Stage definition
        if (token[0] == '{') {
            stage++;
            if (stage >= MAX_MATERIAL_STAGES) {
                ri.Printf(PRINT_WARNING, "WARNING: too many stages in material '%s'\n", name);
                SkipBracedSection(text, 1);
                stage--;
                continue;
            }
            
            Material_ParseStage(material, &material->stages[stage], text);
            material->numStages++;
        }
        // Global material keywords
        else if (!Q_stricmp(token, "cull")) {
            Material_ParseCull(material, text);
        }
        else if (!Q_stricmp(token, "surfaceparm")) {
            Material_ParseSurfaceParm(material, text);
        }
        else if (!Q_stricmp(token, "nomipmaps")) {
            material->materialFlags |= MATERIAL_NOMIPMAPS;
        }
        else if (!Q_stricmp(token, "nopicmip")) {
            material->materialFlags |= MATERIAL_NOMIPMAPS;
        }
        else if (!Q_stricmp(token, "polygonoffset")) {
            material->polygonOffset = qtrue;
            material->materialFlags |= MATERIAL_POLYGONOFFSET;
        }
        else if (!Q_stricmp(token, "sort")) {
            Material_ParseSort(material, text);
        }
        else if (!Q_stricmp(token, "deformvertexes") || !Q_stricmp(token, "deformVertexes")) {
            Material_ParseDeform(material, text);
        }
        else if (!Q_stricmp(token, "skyparms")) {
            Material_ParseSkyParms(material, text);
        }
        else if (!Q_stricmp(token, "fogparms")) {
            Material_ParseFogParms(material, text);
        }
        else if (!Q_stricmp(token, "portal")) {
            material->sort = SS_PORTAL;
            material->portalRange = 256;
            material->materialFlags |= MATERIAL_PORTAL;
        }
        else if (!Q_stricmp(token, "entitymergable")) {
            material->materialFlags |= MATERIAL_ENTITYMERGABLE;
        }
        // New material properties for advanced rendering
        else if (!Q_stricmp(token, "noshadows")) {
            material->materialFlags |= MATERIAL_NOSHADOWS;
            material->castsShadows = qfalse;
        }
        else if (!Q_stricmp(token, "noselfshadow")) {
            material->materialFlags |= MATERIAL_NOSELFSHADOW;
            material->noSelfShadow = qtrue;
        }
        else if (!Q_stricmp(token, "forceshadows")) {
            material->materialFlags |= MATERIAL_FORCESHADOWS;
            material->castsShadows = qtrue;
        }
        else if (!Q_stricmp(token, "receiveshadows")) {
            material->materialFlags |= MATERIAL_RECEIVESHADOWS;
            material->receivesShadows = qtrue;
        }
        else if (!Q_stricmp(token, "twosided")) {
            material->materialFlags |= MATERIAL_TWOSIDED;
            material->cullType = CT_TWO_SIDED;
        }
        else if (!Q_stricmp(token, "translucent")) {
            material->materialFlags |= MATERIAL_TRANSLUCENT;
            material->sort = SS_BLEND0;
        }
        else if (!Q_stricmp(token, "specular")) {
            Material_ParseSpecular(material, text);
        }
        else if (!Q_stricmp(token, "expression")) {
            Material_ParseExpression(material, text);
        }
        else {
            // Unknown keyword - skip
            ri.Printf(PRINT_DEVELOPER, "Unknown material keyword '%s' in '%s'\n", 
                      token, name);
        }
    }
    
    // Post-process material
    Material_Validate(material);
    Material_OptimizeStages(material);
    Material_ComputeVertexAttribs(material);
    
    // Material overrides
    if (r_materialOverride && r_materialOverride->integer) {
        ri.Printf(PRINT_ALL, "^2[MATERIAL] Processing override for: %s\n", material->name);
        materialOverride_t *override = MatOver_Load(material->name);
        if (override) {
            ri.Printf(PRINT_ALL, "^2[MATERIAL] Applying override for: %s\n", material->name);
            MatOver_Apply(material, override);
            // Recompute attributes after override
            Material_ComputeVertexAttribs(material);
        }
    }
    
    // Add to hash table
    Material_AddToHashTable(material);
    
    return material;
}

/*
================
Material_ParseStage

Parse material stage
================
*/
void Material_ParseStage(material_t *material, materialStage_t *stage, char **text) {
    char *token;
    
    // Initialize stage with defaults
    Com_Memset(stage, 0, sizeof(materialStage_t));
    stage->active = qtrue;
    stage->rgbGen = CGEN_IDENTITY;
    stage->alphaGen = AGEN_CONST;
    stage->constantColor[0] = 255;
    stage->constantColor[1] = 255;
    stage->constantColor[2] = 255;
    stage->constantColor[3] = 255;
    stage->stateBits = GLS_DEFAULT;
    stage->tcGen = TCGEN_TEXTURE;
    
    while (1) {
        token = COM_ParseExt(text, qtrue);
        
        if (!token[0]) {
            ri.Printf(PRINT_WARNING, "WARNING: unexpected end of stage in material '%s'\n", 
                      material->name);
            break;
        }
        
        if (token[0] == '}') {
            break;
        }
        
        // Texture maps
        if (!Q_stricmp(token, "map")) {
            Material_ParseMap(material, stage, text);
        }
        else if (!Q_stricmp(token, "clampmap")) {
            Material_ParseMap(material, stage, text);
            stage->bundle[0].isClampMap = qtrue;
        }
        else if (!Q_stricmp(token, "animmap")) {
            Material_ParseAnimMap(material, stage, text);
        }
        else if (!Q_stricmp(token, "normalmap") || !Q_stricmp(token, "bumpmap")) {
            token = COM_ParseExt(text, qfalse);
            stage->normalMap = R_FindImageFile(token, IMGFLAG_NONE | IMGFLAG_NORMALMAP);
            stage->lighting = SL_BUMP;
            material->materialFlags |= MATERIAL_NEEDSNORMAL | MATERIAL_NEEDSTANGENT;
        }
        else if (!Q_stricmp(token, "specularmap")) {
            token = COM_ParseExt(text, qfalse);
            stage->specularMap = R_FindImageFile(token, IMGFLAG_NONE);
            if (stage->lighting == SL_BUMP) {
                stage->lighting = SL_SPECULAR;
            }
        }
        else if (!Q_stricmp(token, "glowmap")) {
            token = COM_ParseExt(text, qfalse);
            stage->glowMap = R_FindImageFile(token, IMGFLAG_NONE);
        }
        // Texture coordinate generation
        else if (!Q_stricmp(token, "tcgen")) {
            Material_ParseTCGen(material, stage, text);
        }
        else if (!Q_stricmp(token, "tcmod")) {
            Material_ParseTCMod(material, stage, text);
        }
        // Color generation
        else if (!Q_stricmp(token, "rgbgen")) {
            Material_ParseRGBGen(material, stage, text);
        }
        else if (!Q_stricmp(token, "alphagen")) {
            Material_ParseAlphaGen(material, stage, text);
        }
        // Blend functions
        else if (!Q_stricmp(token, "blendfunc")) {
            Material_ParseBlendFunc(material, stage, text);
        }
        else if (!Q_stricmp(token, "alphafunc")) {
            Material_ParseAlphaFunc(material, stage, text);
        }
        // Other stage properties
        else if (!Q_stricmp(token, "detail")) {
            stage->isDetail = qtrue;
        }
        else if (!Q_stricmp(token, "alphatest")) {
            token = COM_ParseExt(text, qfalse);
            stage->stateBits |= GLS_ALPHATEST_ENABLE;
            material->materialFlags |= MATERIAL_ALPHATEST;
        }
        else if (!Q_stricmp(token, "depthwrite")) {
            stage->stateBits |= GLS_DEPTHTEST_DISABLE;
        }
        else if (!Q_stricmp(token, "depthfunc")) {
            token = COM_ParseExt(text, qfalse);
            if (!Q_stricmp(token, "equal")) {
                stage->stateBits |= GLS_DEPTHFUNC_EQUAL;
            }
        }
    }
}

/*
================
Material_ParseMap

Parse texture map
================
*/
void Material_ParseMap(material_t *material, materialStage_t *stage, char **text) {
    char *token;
    int flags = IMGFLAG_NONE;
    
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: missing parameter for 'map' in material '%s'\n",
                  material->name);
        return;
    }
    
    // Check for special maps
    if (!Q_stricmp(token, "$lightmap")) {
        R_ReportLegacyLightmapUsage("Material_ParseMap");
        stage->active = qfalse;
        return;
    } else if (!Q_stricmp(token, "$whiteimage")) {
        stage->bundle[0].image[0] = tr.whiteImage;
        stage->colorMap = tr.whiteImage;
        return;
    }
    
    // Handle flags
    if (material->materialFlags & MATERIAL_NOMIPMAPS) {
        flags |= IMGFLAG_NOMIPMAP;
    }
    
    if (stage->bundle[0].isClampMap) {
        flags |= IMGFLAG_CLAMPTOEDGE;
    }
    
    // Load the texture
    stage->bundle[0].image[0] = R_FindImageFile(token, flags);
    stage->colorMap = stage->bundle[0].image[0];
    
    if (!stage->bundle[0].image[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: couldn't find image '%s' in material '%s'\n",
                  token, material->name);
        stage->bundle[0].image[0] = tr.defaultImage;
        stage->colorMap = tr.defaultImage;
    }
}

/*
================
Material_ParseBlendFunc

Parse blend function
================
*/
void Material_ParseBlendFunc(material_t *material, materialStage_t *stage, char **text) {
    char *token;
    int srcBits = 0, dstBits = 0;
    
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: missing parameter for 'blendfunc' in material '%s'\n",
                  material->name);
        return;
    }
    
    // Check for simple blend modes
    if (!Q_stricmp(token, "add")) {
        stage->stateBits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
        return;
    }
    else if (!Q_stricmp(token, "filter")) {
        stage->stateBits = GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO;
        return;
    }
    else if (!Q_stricmp(token, "blend")) {
        stage->stateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
        return;
    }
    
    // Parse source blend mode
    if (!Q_stricmp(token, "GL_ONE")) {
        srcBits = GLS_SRCBLEND_ONE;
    } else if (!Q_stricmp(token, "GL_ZERO")) {
        srcBits = GLS_SRCBLEND_ZERO;
    } else if (!Q_stricmp(token, "GL_SRC_ALPHA")) {
        srcBits = GLS_SRCBLEND_SRC_ALPHA;
    } else if (!Q_stricmp(token, "GL_ONE_MINUS_SRC_ALPHA")) {
        srcBits = GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA;
    } else if (!Q_stricmp(token, "GL_DST_COLOR")) {
        srcBits = GLS_SRCBLEND_DST_COLOR;
    } else if (!Q_stricmp(token, "GL_ONE_MINUS_DST_COLOR")) {
        srcBits = GLS_SRCBLEND_ONE_MINUS_DST_COLOR;
    } else {
        ri.Printf(PRINT_WARNING, "WARNING: unknown blend mode '%s' in material '%s'\n",
                  token, material->name);
        return;
    }
    
    // Parse destination blend mode
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: missing dst blend mode in material '%s'\n",
                  material->name);
        return;
    }
    
    if (!Q_stricmp(token, "GL_ONE")) {
        dstBits = GLS_DSTBLEND_ONE;
    } else if (!Q_stricmp(token, "GL_ZERO")) {
        dstBits = GLS_DSTBLEND_ZERO;
    } else if (!Q_stricmp(token, "GL_SRC_ALPHA")) {
        dstBits = GLS_DSTBLEND_SRC_ALPHA;
    } else if (!Q_stricmp(token, "GL_ONE_MINUS_SRC_ALPHA")) {
        dstBits = GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
    } else if (!Q_stricmp(token, "GL_SRC_COLOR")) {
        dstBits = GLS_DSTBLEND_SRC_COLOR;
    } else if (!Q_stricmp(token, "GL_ONE_MINUS_SRC_COLOR")) {
        dstBits = GLS_DSTBLEND_ONE_MINUS_SRC_COLOR;
    } else {
        ri.Printf(PRINT_WARNING, "WARNING: unknown blend mode '%s' in material '%s'\n",
                  token, material->name);
        return;
    }
    
    stage->stateBits = srcBits | dstBits;
    
    // Clear depth mask for blended stages
    if (stage->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) {
        stage->stateBits |= GLS_DEPTHTEST_DISABLE;
    }
}

/*
================
Material_ParseCull

Parse culling mode
================
*/
void Material_ParseCull(material_t *material, char **text) {
    char *token;
    
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: missing parameter for 'cull' in material '%s'\n",
                  material->name);
        return;
    }
    
    if (!Q_stricmp(token, "none") || !Q_stricmp(token, "twosided") || 
        !Q_stricmp(token, "disable")) {
        material->cullType = CT_TWO_SIDED;
        material->materialFlags |= MATERIAL_TWOSIDED;
    } else if (!Q_stricmp(token, "back") || !Q_stricmp(token, "backside") || 
               !Q_stricmp(token, "backsided")) {
        material->cullType = CT_BACK_SIDED;
    } else if (!Q_stricmp(token, "front") || !Q_stricmp(token, "frontside") || 
               !Q_stricmp(token, "frontsided")) {
        material->cullType = CT_FRONT_SIDED;
    } else {
        ri.Printf(PRINT_WARNING, "WARNING: unknown cull mode '%s' in material '%s'\n",
                  token, material->name);
    }
}

/*
================
Material_ParseSort

Parse sort order
================
*/
void Material_ParseSort(material_t *material, char **text) {
    char *token;
    
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: missing parameter for 'sort' in material '%s'\n",
                  material->name);
        return;
    }
    
    if (!Q_stricmp(token, "portal")) {
        material->sort = SS_PORTAL;
    } else if (!Q_stricmp(token, "sky")) {
        material->sort = SS_ENVIRONMENT;
    } else if (!Q_stricmp(token, "opaque")) {
        material->sort = SS_OPAQUE;
    } else if (!Q_stricmp(token, "decal")) {
        material->sort = SS_DECAL;
    } else if (!Q_stricmp(token, "seethrough")) {
        material->sort = SS_SEE_THROUGH;
    } else if (!Q_stricmp(token, "banner")) {
        material->sort = SS_BANNER;
    } else if (!Q_stricmp(token, "underwater")) {
        material->sort = SS_UNDERWATER;
    } else if (!Q_stricmp(token, "additive")) {
        material->sort = SS_BLEND1;
    } else if (!Q_stricmp(token, "nearest")) {
        material->sort = SS_NEAREST;
    } else {
        material->sort = atof(token);
    }
}

/*
================
Material_CreateDefault

Create a default material for a texture
================
*/
material_t* Material_CreateDefault(const char *name) {
    material_t *material;
    materialStage_t *stage;

    if (numMaterials >= MAX_MATERIALS) {
        ri.Printf(PRINT_WARNING, "WARNING: MAX_MATERIALS reached\n");
        return NULL;
    }
    
    material = &materials[numMaterials++];
    Com_Memset(material, 0, sizeof(material_t));
    
    Q_strncpyz(material->name, name, sizeof(material->name));
    material->index = numMaterials - 1;
    material->cullType = CT_FRONT_SIDED;
    material->sort = SS_OPAQUE;
    
    // Default diffuse stage
    stage = &material->stages[0];
    stage->active = qtrue;
    stage->bundle[0].image[0] = R_FindImageFile(name, IMGFLAG_NONE);
    if (!stage->bundle[0].image[0]) {
        stage->bundle[0].image[0] = tr.defaultImage;
    }
    stage->colorMap = stage->bundle[0].image[0];
    stage->rgbGen = CGEN_IDENTITY;
    stage->alphaGen = AGEN_CONST;
    stage->constantColor[0] = 255;
    stage->constantColor[1] = 255;
    stage->constantColor[2] = 255;
    stage->constantColor[3] = 255;
    stage->stateBits = GLS_DEFAULT;
    material->numStages = 1;
    
    Material_AddToHashTable(material);

    return material;
}

/*
================
Material_RegisterCommands

Register material console commands
================
*/
void Material_RegisterCommands(void) {
    ri.Cmd_AddCommand("materiallist", Material_ListMaterials_f);
    ri.Cmd_AddCommand("materialinfo", Material_Info_f);
}

/*
================
Material_ListMaterials_f

List all loaded materials
================
*/
void Material_ListMaterials_f(void) {
    int i;
    
    ri.Printf(PRINT_ALL, "--- Materials ---\n");
    ri.Printf(PRINT_ALL, "Num  Name                         Stages  Flags     Sort\n");
    ri.Printf(PRINT_ALL, "---  ---------------------------- ------- --------- ------\n");
    
    for (i = 0; i < numMaterials; i++) {
        material_t *mat = &materials[i];
        ri.Printf(PRINT_ALL, "%3d: %-28s %7d %08x %6.2f\n",
                  i, mat->name, mat->numStages, mat->materialFlags, mat->sort);
    }
    
    ri.Printf(PRINT_ALL, "-------------------\n");
    ri.Printf(PRINT_ALL, "%d materials total\n", numMaterials);
}

/*
================
Material_Info_f

Print detailed info about a material
================
*/
void Material_Info_f(void) {
    const char *name;
    material_t *mat;
    int i;
    
    if (ri.Cmd_Argc() != 2) {
        ri.Printf(PRINT_ALL, "usage: materialinfo <materialname>\n");
        return;
    }
    
    name = ri.Cmd_Argv(1);
    mat = Material_Find(name);
    
    if (!mat) {
        ri.Printf(PRINT_ALL, "Material '%s' not found\n", name);
        return;
    }
    
    ri.Printf(PRINT_ALL, "=== Material: %s ===\n", mat->name);
    ri.Printf(PRINT_ALL, "Index: %d\n", mat->index);
    ri.Printf(PRINT_ALL, "Sort: %.2f\n", mat->sort);
    ri.Printf(PRINT_ALL, "Stages: %d\n", mat->numStages);
    ri.Printf(PRINT_ALL, "Flags: %08x\n", mat->materialFlags);
    ri.Printf(PRINT_ALL, "Cull: %s\n", 
              mat->cullType == CT_TWO_SIDED ? "none" :
              mat->cullType == CT_BACK_SIDED ? "back" : "front");
    
    // Print stage info
    for (i = 0; i < mat->numStages; i++) {
        materialStage_t *stage = &mat->stages[i];
        ri.Printf(PRINT_ALL, "  Stage %d:\n", i);
        ri.Printf(PRINT_ALL, "    Active: %s\n", stage->active ? "yes" : "no");
        ri.Printf(PRINT_ALL, "    RGB Gen: %d\n", stage->rgbGen);
        ri.Printf(PRINT_ALL, "    Alpha Gen: %d\n", stage->alphaGen);
        ri.Printf(PRINT_ALL, "    State Bits: %08x\n", stage->stateBits);
        ri.Printf(PRINT_ALL, "    Lighting: %d\n", stage->lighting);
    }
}

/*
================
Material_ParseAlphaFunc

Parse alpha test function
================
*/
void Material_ParseAlphaFunc(material_t *material, materialStage_t *stage, char **text) {
    char *token;
    
    token = COM_ParseExt(text, qfalse);
    if (!Q_stricmp(token, "GT0")) {
        stage->stateBits |= GLS_ATEST_GT_0;
    } else if (!Q_stricmp(token, "LT128")) {
        stage->stateBits |= GLS_ATEST_LT_80;
    } else if (!Q_stricmp(token, "GE128")) {
        stage->stateBits |= GLS_ATEST_GE_80;
    }
    
    stage->stateBits |= GLS_ALPHATEST_ENABLE;
}

/*
================
Material_ParseSurfaceParm

Parse surface parameter
================
*/
void Material_ParseSurfaceParm(material_t *material, char **text) {
    char *token;
    
    token = COM_ParseExt(text, qfalse);
    
    if (!Q_stricmp(token, "nodraw")) {
        material->surfaceFlags |= SURF_NODRAW;
    } else if (!Q_stricmp(token, "structural")) {
        material->contentFlags |= CONTENTS_STRUCTURAL;
    } else if (!Q_stricmp(token, "trans")) {
        material->contentFlags |= CONTENTS_TRANSLUCENT;
    } else if (!Q_stricmp(token, "lava")) {
        material->contentFlags |= CONTENTS_LAVA;
    } else if (!Q_stricmp(token, "slime")) {
        material->contentFlags |= CONTENTS_SLIME;
    } else if (!Q_stricmp(token, "water")) {
        material->contentFlags |= CONTENTS_WATER;
    } else if (!Q_stricmp(token, "fog")) {
        material->contentFlags |= CONTENTS_FOG;
    } else if (!Q_stricmp(token, "sky")) {
        material->surfaceFlags |= SURF_SKY;
        material->isSky = qtrue;
    } else if (!Q_stricmp(token, "slick")) {
        material->surfaceFlags |= SURF_SLICK;
    } else if (!Q_stricmp(token, "noimpact")) {
        material->surfaceFlags |= SURF_NOIMPACT;
    } else if (!Q_stricmp(token, "nomarks")) {
        material->surfaceFlags |= SURF_NOMARKS;
    } else if (!Q_stricmp(token, "ladder")) {
        material->surfaceFlags |= SURF_LADDER;
    } else if (!Q_stricmp(token, "nodamage")) {
        material->surfaceFlags |= SURF_NODAMAGE;
    } else if (!Q_stricmp(token, "metalsteps")) {
        material->surfaceFlags |= SURF_METALSTEPS;
    } else if (!Q_stricmp(token, "flesh")) {
        material->surfaceFlags |= SURF_FLESH;
    } else if (!Q_stricmp(token, "nosteps")) {
        material->surfaceFlags |= SURF_NOSTEPS;
    } else if (!Q_stricmp(token, "nonsolid")) {
        material->surfaceFlags |= SURF_NONSOLID;
    } else if (!Q_stricmp(token, "lightfilter")) {
        material->surfaceFlags |= SURF_LIGHTFILTER;
    } else if (!Q_stricmp(token, "alphashadow")) {
        material->surfaceFlags |= SURF_ALPHASHADOW;
    } else if (!Q_stricmp(token, "nodlight")) {
        material->surfaceFlags |= SURF_NODLIGHT;
    } else if (!Q_stricmp(token, "dust")) {
        material->surfaceFlags |= SURF_DUST;
    }
}

// Forward declaration from tr_expression.c
extern genFunc_t NameToGenFunc(const char *funcname);

/*
================
ParseWaveForm

Parse wave form parameters for material system
================
*/
void ParseWaveForm(char **text, waveForm_t *wave)
{
    char *token;
    
    token = COM_ParseExt(text, qfalse);
    if (token[0] == 0)
    {
        ri.Printf(PRINT_WARNING, "WARNING: missing waveform parm in shader\n");
        return;
    }
    wave->func = NameToGenFunc(token);
    
    // BASE, AMP, PHASE, FREQ
    token = COM_ParseExt(text, qfalse);
    if (token[0] == 0)
    {
        ri.Printf(PRINT_WARNING, "WARNING: missing waveform parm in shader\n");
        return;
    }
    wave->base = atof(token);
    
    token = COM_ParseExt(text, qfalse);
    if (token[0] == 0)
    {
        ri.Printf(PRINT_WARNING, "WARNING: missing waveform parm in shader\n");
        return;
    }
    wave->amplitude = atof(token);
    
    token = COM_ParseExt(text, qfalse);
    if (token[0] == 0)
    {
        ri.Printf(PRINT_WARNING, "WARNING: missing waveform parm in shader\n");
        return;
    }
    wave->phase = atof(token);
    
    token = COM_ParseExt(text, qfalse);
    if (token[0] == 0)
    {
        ri.Printf(PRINT_WARNING, "WARNING: missing waveform parm in shader\n");
        return;
    }
    wave->frequency = atof(token);
}

/*
================
Material_ParseDeform

Parse vertex deformation
================
*/
void Material_ParseDeform(material_t *material, char **text) {
    char *token;
    deformStage_t *deform;
    
    if (material->numDeforms >= MAX_SHADER_DEFORMS) {
        ri.Printf(PRINT_WARNING, "WARNING: MAX_SHADER_DEFORMS hit in material '%s'\n", material->name);
        return;
    }
    
    deform = &material->deforms[material->numDeforms];
    material->numDeforms++;
    
    token = COM_ParseExt(text, qfalse);
    
    if (!Q_stricmp(token, "wave")) {
        deform->deformation = DEFORM_WAVE;
        token = COM_ParseExt(text, qfalse);
        deform->deformationSpread = atof(token);
        ParseWaveForm(text, &deform->deformationWave);
    } else if (!Q_stricmp(token, "bulge")) {
        deform->deformation = DEFORM_BULGE;
        deform->bulgeWidth = atof(COM_ParseExt(text, qfalse));
        deform->bulgeHeight = atof(COM_ParseExt(text, qfalse));
        deform->bulgeSpeed = atof(COM_ParseExt(text, qfalse));
    } else if (!Q_stricmp(token, "move")) {
        deform->deformation = DEFORM_MOVE;
        deform->moveVector[0] = atof(COM_ParseExt(text, qfalse));
        deform->moveVector[1] = atof(COM_ParseExt(text, qfalse));
        deform->moveVector[2] = atof(COM_ParseExt(text, qfalse));
        ParseWaveForm(text, &deform->deformationWave);
    } else if (!Q_stricmp(token, "autosprite")) {
        deform->deformation = DEFORM_AUTOSPRITE;
        material->materialFlags |= MATERIAL_AUTOSPRITE;
    } else if (!Q_stricmp(token, "autosprite2")) {
        deform->deformation = DEFORM_AUTOSPRITE2;
        material->materialFlags |= MATERIAL_AUTOSPRITE;
    }
}

/*
================
Material_ParseSkyParms

Parse sky parameters
================
*/
void Material_ParseSkyParms(material_t *material, char **text) {
    char *token;
    char pathname[MAX_QPATH];
    int i;
    static const char *suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
    
    // Farbox
    token = COM_ParseExt(text, qfalse);
    if (token[0]) {
        for (i = 0; i < 6; i++) {
            Com_sprintf(pathname, sizeof(pathname), "%s_%s", token, suf[i]);
            material->sky.outerbox[i] = R_FindImageFile(pathname, IMGFLAG_CLAMPTOEDGE);
            if (!material->sky.outerbox[i]) {
                material->sky.outerbox[i] = tr.defaultImage;
            }
        }
    }
    
    // Cloud height
    token = COM_ParseExt(text, qfalse);
    if (token[0]) {
        material->sky.cloudHeight = atof(token);
    }
    
    // Nearbox
    token = COM_ParseExt(text, qfalse);
    if (token[0]) {
        for (i = 0; i < 6; i++) {
            Com_sprintf(pathname, sizeof(pathname), "%s_%s", token, suf[i]);
            material->sky.innerbox[i] = R_FindImageFile(pathname, IMGFLAG_CLAMPTOEDGE);
            if (!material->sky.innerbox[i]) {
                material->sky.innerbox[i] = tr.defaultImage;
            }
        }
    }
    
    material->isSky = qtrue;
    material->materialFlags |= MATERIAL_SKY;
}

/*
================
Material_ParseFogParms

Parse fog parameters
================
*/
void Material_ParseFogParms(material_t *material, char **text) {
    char *token;
    vec3_t color;
    float depthForOpaque;
    
    // Color
    token = COM_ParseExt(text, qfalse);
    color[0] = atof(token);
    
    token = COM_ParseExt(text, qfalse);
    color[1] = atof(token);
    
    token = COM_ParseExt(text, qfalse);
    color[2] = atof(token);
    
    // Distance to opaque
    token = COM_ParseExt(text, qfalse);
    depthForOpaque = atof(token);
    
    // Set fog parameters
    VectorCopy(color, material->fogParms.color);
    material->fogParms.depthForOpaque = depthForOpaque;
    material->isFog = qtrue;
    material->materialFlags |= MATERIAL_FOG;
}

/*
================
Material_ParseSpecular

Parse specular properties
================
*/
void Material_ParseSpecular(material_t *material, char **text) {
    char *token;
    
    // Specular color R
    token = COM_ParseExt(text, qfalse);
    material->specularColor[0] = atof(token);
    
    // Specular color G
    token = COM_ParseExt(text, qfalse);
    material->specularColor[1] = atof(token);
    
    // Specular color B
    token = COM_ParseExt(text, qfalse);
    material->specularColor[2] = atof(token);
    
    // Specular exponent
    token = COM_ParseExt(text, qfalse);
    material->specularExponent = atof(token);
    
    if (material->specularExponent <= 0) {
        material->specularExponent = 32.0f;
    }
}

