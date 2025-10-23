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

#ifndef TR_MATERIAL_H
#define TR_MATERIAL_H

/*
================================================================================
Phase 3: Material System

This file defines the new material system that replaces the shader system
while maintaining full backward compatibility with Q3 .shader files.
================================================================================
*/

// Forward declarations
typedef struct shader_s shader_t;
typedef struct material_s material_t;
typedef struct materialStage_s materialStage_t;

// Material constants
#define MAX_MATERIAL_STAGES         8
#define MAX_SHADER_STAGE_TEXMODS    4
#define MAX_EXPRESSION_REGISTERS    16
#define MAX_EXPRESSIONS            32
#define MAX_MATERIALS              4096
#define MATERIAL_HASH_SIZE         1024

// Material flags (compatible with shader flags)
#define MATERIAL_SKY               0x00000001
#define MATERIAL_NOMIPMAPS         0x00000002
#define MATERIAL_POLYGONOFFSET     0x00000004
#define MATERIAL_PORTAL            0x00000008
#define MATERIAL_MULTITEXTURE      0x00000010
#define MATERIAL_FOG               0x00000020
#define MATERIAL_NEEDSNORMAL       0x00000040
#define MATERIAL_NEEDSTANGENT      0x00000080
#define MATERIAL_NEEDSCOLOR        0x00000100
#define MATERIAL_AUTOSPRITE        0x00000200
#define MATERIAL_VERTEXLIT         0x00000800
#define MATERIAL_NOSHADOWS         0x00001000
#define MATERIAL_NOSELFSHADOW      0x00002000
#define MATERIAL_FORCESHADOWS      0x00004000
#define MATERIAL_RECEIVESHADOWS    0x00008000
#define MATERIAL_ALPHATEST         0x00010000
#define MATERIAL_TRANSLUCENT       0x00020000
#define MATERIAL_TWOSIDED          0x00040000
#define MATERIAL_ENTITYMERGABLE    0x00080000
#define MATERIAL_PBR               0x00100000  // Uses PBR workflow

// Expression operation types
typedef enum {
    EXP_OP_ADD,
    EXP_OP_SUBTRACT,
    EXP_OP_MULTIPLY,
    EXP_OP_DIVIDE,
    EXP_OP_MOD,
    EXP_OP_TABLE,
    EXP_OP_SIN,
    EXP_OP_COS,
    EXP_OP_SQUARE,
    EXP_OP_INVERSE,
    EXP_OP_CLAMP,
    EXP_OP_MIN,
    EXP_OP_MAX,
    EXP_OP_RANDOM
} expOpType_t;

// Stage lighting types (for future per-pixel lighting)
typedef enum {
    SL_NONE,            // No lighting
    SL_AMBIENT,         // Ambient only
    SL_DIFFUSE,         // Diffuse lighting
    SL_SPECULAR,        // Specular lighting
    SL_BUMP             // Bump/normal mapping
} stageLighting_t;

// Predefined expression registers
enum {
    REG_TIME = 0,
    REG_PARM0,
    REG_PARM1,
    REG_PARM2,
    REG_PARM3,
    REG_PARM4,
    REG_PARM5,
    REG_PARM6,
    REG_PARM7,
    REG_GLOBAL0,
    REG_GLOBAL1,
    REG_GLOBAL2,
    REG_GLOBAL3,
    REG_SCRATCH0,
    REG_SCRATCH1,
    REG_SCRATCH2
};

// Expression structure for dynamic materials
typedef struct expression_s {
    int             destRegister;      // Target register
    expOpType_t     opType;           // Operation type
    int             srcRegister[2];    // Source registers
    float           constantValue;     // Constant operand
    int             tableIndex;        // For table lookups
} expression_t;

// Material stage (enhanced from shaderStage_t)
struct materialStage_s {
    qboolean            active;
    
    // Textures
    image_t*            colorMap;          // Diffuse/color texture
    image_t*            normalMap;         // Normal map
    image_t*            specularMap;       // Specular map
    image_t*            glowMap;           // Emissive map
    
    // Legacy texture bundles for compatibility
    textureBundle_t     bundle[NUM_TEXTURE_BUNDLES];
    
    // Texture coordinate generation
    texCoordGen_t       tcGen;
    vec2_t              tcGenVectors[2];   // For TCGEN_VECTOR
    
    // Texture modifications
    int                 numTexMods;
    texModInfo_t        texMods[MAX_SHADER_STAGE_TEXMODS];
    
    // Color/alpha generation
    colorGen_t          rgbGen;
    waveForm_t          rgbWave;
    alphaGen_t          alphaGen;
    waveForm_t          alphaWave;
    byte                constantColor[4];
    
    // Blend state
    unsigned int        stateBits;         // GLS_* flags
    int                 blendSrc;          // GL blend source
    int                 blendDst;          // GL blend destination
    
    // Lighting mode
    stageLighting_t     lighting;          // Lighting type
    
    // Program selection
    int                 program;           // Shader program to use
    
    // Dynamic expressions
    int                 numExpressions;
    expression_t        *expressions;      // Dynamic value expressions
    
    // Stage-specific flags
    qboolean            isDetail;
};

// Main material structure
struct material_s {
    char                name[MAX_QPATH];
    int                 index;             // Index in tr.materials[]
    int                 sortedIndex;       // For sorting
    
    // Surface properties
    float               sort;               // Sort order
    int                 surfaceFlags;       // SURF_* flags
    int                 contentFlags;       // CONTENTS_* flags
    int                 materialFlags;      // MATERIAL_* flags
    
    // Rendering properties
    cullType_t          cullType;          // Culling mode
    qboolean            polygonOffset;     // Z-fighting prevention
    float               polygonOffsetValue;
    
    // Stages
    int                 numStages;
    materialStage_t     stages[MAX_MATERIAL_STAGES];
    
    // Deformations
    int                 numDeforms;
    deformStage_t       deforms[MAX_SHADER_DEFORMS];
    
    // Special effects
    qboolean            isSky;
    skyParms_t          sky;
    qboolean            isFog;
    fogParms_t          fogParms;
    float               portalRange;
    
    // Lighting properties (for future phases)
    qboolean            receivesShadows;
    qboolean            castsShadows;
    qboolean            noSelfShadow;
    float               specularExponent;   // Specular power
    vec3_t              specularColor;      // Specular color
    
    // PBR properties
    vec3_t              baseColor;          // Albedo color
    float               metallic;           // Metallic value
    float               roughness;          // Roughness value
    float               specular;           // Specular intensity
    float               normalStrength;      // Normal map strength
    float               heightScale;         // Parallax height
    qboolean            isPBR;              // Using PBR workflow
    
    // PBR texture maps
    image_t             *normalMap;         // Normal map
    image_t             *specularMap;       // Specular map
    image_t             *roughnessMap;      // Roughness map
    image_t             *metallicMap;       // Metallic map
    image_t             *heightMap;         // Height/displacement map
    qboolean            hasNormalMap;
    qboolean            hasSpecularMap;
    qboolean            hasHeightMap;
    
    // Expression system
    expression_t        expressions[MAX_EXPRESSIONS];
    float               expressionRegisters[MAX_EXPRESSION_REGISTERS];
    int                 numExpressions;
    int                 numRegisters;
    
    // Optimization
    int                 vertexAttribs;      // Required vertex attributes
    qboolean            isStaticMaterial;   // No dynamic content
    
    // Compatibility
    void                (*optimalStageIteratorFunc)(void);  // Legacy
    
    // Linked list
    struct material_s   *next;
    struct material_s   *remappedMaterial;
    
    // Reference to original shader for compatibility
    shader_t            *legacyShader;      // For backward compatibility
    material_t          *convertedMaterial; // Self-reference for converted materials
};

// Memory pools for material system
typedef struct {
    byte    *base;
    int     size;
    int     used;
} materialMemPool_t;

// Function declarations

// Initialization
void R_InitMaterialSystem(void);
void R_ShutdownMaterialSystem(void);

// Material management
material_t* Material_Find(const char *name);
material_t* Material_Parse(char *name, char **text);
material_t* Material_CreateDefault(const char *name);
void Material_AddToHashTable(material_t *material);

// Stage parsing
void Material_ParseStage(material_t *material, materialStage_t *stage, char **text);
void Material_ParseMap(material_t *material, materialStage_t *stage, char **text);
void Material_ParseAnimMap(material_t *material, materialStage_t *stage, char **text);
void Material_ParseBlendFunc(material_t *material, materialStage_t *stage, char **text);
void Material_ParseAlphaFunc(material_t *material, materialStage_t *stage, char **text);
void Material_ParseTCGen(material_t *material, materialStage_t *stage, char **text);
void Material_ParseTCMod(material_t *material, materialStage_t *stage, char **text);
void Material_ParseRGBGen(material_t *material, materialStage_t *stage, char **text);
void Material_ParseAlphaGen(material_t *material, materialStage_t *stage, char **text);

// Material property parsing
void Material_ParseCull(material_t *material, char **text);
void Material_ParseSurfaceParm(material_t *material, char **text);
void Material_ParseSort(material_t *material, char **text);
void Material_ParseDeform(material_t *material, char **text);
void Material_ParseSkyParms(material_t *material, char **text);
void Material_ParseFogParms(material_t *material, char **text);
void Material_ParseSpecular(material_t *material, char **text);

// Helper functions
void ParseWaveForm(char **text, waveForm_t *wave);

// Shader compatibility
material_t* Material_FromShader(shader_t *shader);
shader_t* Material_CreateShaderWrapper(material_t *material);
void Material_ConvertStage(materialStage_t *mStage, shaderStage_t *sStage);
shaderStage_t* Material_CreateStageWrapper(materialStage_t *mStage);
void Material_ExtractBlendFunc(materialStage_t *stage);

// Expression system
void Material_EvaluateExpressions(material_t *material, float time);
void Material_ParseExpression(material_t *material, char **text);
int Material_ParseRegister(const char *token);
expOpType_t Material_ParseOperation(const char *token);
qboolean Material_OperationNeedsSecondOperand(expOpType_t op);
float Material_TableLookup(int tableIndex, float index);

// Optimization
void Material_OptimizeStages(material_t *material);
void Material_MergeStages(material_t *material);
void Material_DetectLightmapPattern(material_t *material);
void Material_PrecomputeStatic(material_t *material);
void Material_SelectRenderPath(material_t *material);
void Material_ComputeVertexAttribs(material_t *material);

// Validation
void Material_Validate(material_t *material);
void Material_ValidateBlendFunc(materialStage_t *stage);
void Material_ValidateTCGen(materialStage_t *stage);
void Material_CreateDefaultStage(material_t *material);

// Debug commands
void Material_RegisterCommands(void);
void Material_ListMaterials_f(void);
void Material_Info_f(void);

// Backend integration
void RB_SetupMaterialStage(materialStage_t *stage);
void RB_IterateMaterialStages(material_t *material);

// Global material system state
extern material_t materials[MAX_MATERIALS];
extern int numMaterials;
extern material_t* materialHashTable[MATERIAL_HASH_SIZE];

// CVars
extern cvar_t *r_materialParm0;
extern cvar_t *r_materialParm1;
extern cvar_t *r_materialParm2;
extern cvar_t *r_materialParm3;

#endif // TR_MATERIAL_H
