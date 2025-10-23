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
#include <math.h>

/*
================================================================================
Phase 3: Expression System

This file implements the dynamic expression evaluation system for materials,
allowing for time-based and parameter-based animations.
================================================================================
*/

// Expression tables for various waveforms
#define TABLE_SIZE 256

static float sineTable[TABLE_SIZE];
static float triangleTable[TABLE_SIZE];
static float squareTable[TABLE_SIZE];
static float sawtoothTable[TABLE_SIZE];
static float inverseSawtoothTable[TABLE_SIZE];
static qboolean tablesInitialized = qfalse;

/*
================
Material_InitExpressionTables

Initialize lookup tables for expressions
================
*/
static void Material_InitExpressionTables(void) {
    int i;
    float f;
    
    if (tablesInitialized) {
        return;
    }
    
    for (i = 0; i < TABLE_SIZE; i++) {
        f = (float)i / (float)(TABLE_SIZE - 1);
        
        // Sine wave
        sineTable[i] = sin(f * M_PI * 2);
        
        // Triangle wave
        if (f < 0.25f) {
            triangleTable[i] = f * 4.0f;
        } else if (f < 0.75f) {
            triangleTable[i] = 2.0f - (f * 4.0f);
        } else {
            triangleTable[i] = (f * 4.0f) - 4.0f;
        }
        
        // Square wave
        squareTable[i] = (f < 0.5f) ? 1.0f : -1.0f;
        
        // Sawtooth wave
        sawtoothTable[i] = f * 2.0f - 1.0f;
        
        // Inverse sawtooth
        inverseSawtoothTable[i] = 1.0f - (f * 2.0f);
    }
    
    tablesInitialized = qtrue;
}

/*
================
Material_EvaluateExpressions

Evaluate all expressions for a material
================
*/
void Material_EvaluateExpressions(material_t *material, float time) {
    int i;
    float *regs;
    expression_t *exp;
    float result;
    
    if (!material || material->numExpressions == 0) {
        return;
    }
    
    // Ensure tables are initialized
    if (!tablesInitialized) {
        Material_InitExpressionTables();
    }
    
    regs = material->expressionRegisters;
    
    // Set standard registers
    regs[REG_TIME] = time;
    regs[REG_PARM0] = r_materialParm0 ? r_materialParm0->value : 0;
    regs[REG_PARM1] = r_materialParm1 ? r_materialParm1->value : 0;
    regs[REG_PARM2] = r_materialParm2 ? r_materialParm2->value : 0;
    regs[REG_PARM3] = r_materialParm3 ? r_materialParm3->value : 0;
    
    // Evaluate all expressions in order
    for (i = 0; i < material->numExpressions; i++) {
        exp = &material->expressions[i];
        result = 0;
        
        switch (exp->opType) {
        case EXP_OP_ADD:
            result = regs[exp->srcRegister[0]] + regs[exp->srcRegister[1]];
            break;
            
        case EXP_OP_SUBTRACT:
            result = regs[exp->srcRegister[0]] - regs[exp->srcRegister[1]];
            break;
            
        case EXP_OP_MULTIPLY:
            result = regs[exp->srcRegister[0]] * regs[exp->srcRegister[1]];
            break;
            
        case EXP_OP_DIVIDE:
            if (regs[exp->srcRegister[1]] != 0) {
                result = regs[exp->srcRegister[0]] / regs[exp->srcRegister[1]];
            }
            break;
            
        case EXP_OP_MOD:
            if (regs[exp->srcRegister[1]] != 0) {
                result = fmod(regs[exp->srcRegister[0]], regs[exp->srcRegister[1]]);
            }
            break;
            
        case EXP_OP_TABLE:
            result = Material_TableLookup(exp->tableIndex, regs[exp->srcRegister[0]]);
            break;
            
        case EXP_OP_SIN:
            result = sin(regs[exp->srcRegister[0]]);
            break;
            
        case EXP_OP_COS:
            result = cos(regs[exp->srcRegister[0]]);
            break;
            
        case EXP_OP_SQUARE:
            result = regs[exp->srcRegister[0]] * regs[exp->srcRegister[0]];
            break;
            
        case EXP_OP_INVERSE:
            if (regs[exp->srcRegister[0]] != 0) {
                result = 1.0f / regs[exp->srcRegister[0]];
            }
            break;
            
        case EXP_OP_CLAMP:
            result = regs[exp->srcRegister[0]];
            if (result < 0) result = 0;
            if (result > 1) result = 1;
            break;
            
        case EXP_OP_MIN:
            result = (regs[exp->srcRegister[0]] < regs[exp->srcRegister[1]]) ?
                     regs[exp->srcRegister[0]] : regs[exp->srcRegister[1]];
            break;
            
        case EXP_OP_MAX:
            result = (regs[exp->srcRegister[0]] > regs[exp->srcRegister[1]]) ?
                     regs[exp->srcRegister[0]] : regs[exp->srcRegister[1]];
            break;
            
        case EXP_OP_RANDOM:
            result = random();
            break;
            
        default:
            ri.Printf(PRINT_DEVELOPER, "Unknown expression operation %d\n", exp->opType);
            break;
        }
        
        // Store result in destination register
        regs[exp->destRegister] = result;
    }
}

/*
================
Material_ParseExpression

Parse an expression from material text
================
*/
void Material_ParseExpression(material_t *material, char **text) {
    char *token;
    expression_t *exp;
    
    if (!material || material->numExpressions >= MAX_EXPRESSIONS) {
        ri.Printf(PRINT_WARNING, "WARNING: Max expressions reached in material '%s'\n", 
                  material->name);
        SkipRestOfLine(text);
        return;
    }
    
    exp = &material->expressions[material->numExpressions];
    Com_Memset(exp, 0, sizeof(expression_t));
    
    // Parse destination register
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: Missing destination register in expression for '%s'\n",
                  material->name);
        return;
    }
    exp->destRegister = Material_ParseRegister(token);
    if (exp->destRegister < 0 || exp->destRegister >= MAX_EXPRESSION_REGISTERS) {
        ri.Printf(PRINT_WARNING, "WARNING: Invalid destination register '%s' in material '%s'\n",
                  token, material->name);
        return;
    }
    
    // Parse operation
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: Missing operation in expression for '%s'\n",
                  material->name);
        return;
    }
    exp->opType = Material_ParseOperation(token);
    
    // Parse first operand
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: Missing operand in expression for '%s'\n",
                  material->name);
        return;
    }
    
    // Check if it's a constant or register
    if (token[0] >= '0' && token[0] <= '9') {
        // Constant value - store in scratch register
        exp->constantValue = atof(token);
        exp->srcRegister[0] = REG_SCRATCH0;
        material->expressionRegisters[REG_SCRATCH0] = exp->constantValue;
    } else {
        exp->srcRegister[0] = Material_ParseRegister(token);
    }
    
    // Parse second operand if needed
    if (Material_OperationNeedsSecondOperand(exp->opType)) {
        token = COM_ParseExt(text, qfalse);
        if (!token[0]) {
            ri.Printf(PRINT_WARNING, "WARNING: Missing second operand in expression for '%s'\n",
                      material->name);
            return;
        }
        
        if (token[0] >= '0' && token[0] <= '9') {
            // Constant value - store in scratch register
            exp->constantValue = atof(token);
            exp->srcRegister[1] = REG_SCRATCH1;
            material->expressionRegisters[REG_SCRATCH1] = exp->constantValue;
        } else {
            exp->srcRegister[1] = Material_ParseRegister(token);
        }
    }
    
    // Parse table index for table operations
    if (exp->opType == EXP_OP_TABLE) {
        token = COM_ParseExt(text, qfalse);
        if (!token[0]) {
            exp->tableIndex = 0; // Default to sine table
        } else {
            exp->tableIndex = atoi(token);
        }
    }
    
    material->numExpressions++;
}

/*
================
Material_ParseRegister

Parse register name
================
*/
int Material_ParseRegister(const char *token) {
    if (!Q_stricmp(token, "time")) {
        return REG_TIME;
    } else if (!Q_stricmp(token, "parm0")) {
        return REG_PARM0;
    } else if (!Q_stricmp(token, "parm1")) {
        return REG_PARM1;
    } else if (!Q_stricmp(token, "parm2")) {
        return REG_PARM2;
    } else if (!Q_stricmp(token, "parm3")) {
        return REG_PARM3;
    } else if (!Q_stricmp(token, "parm4")) {
        return REG_PARM4;
    } else if (!Q_stricmp(token, "parm5")) {
        return REG_PARM5;
    } else if (!Q_stricmp(token, "parm6")) {
        return REG_PARM6;
    } else if (!Q_stricmp(token, "parm7")) {
        return REG_PARM7;
    } else if (!Q_stricmp(token, "global0")) {
        return REG_GLOBAL0;
    } else if (!Q_stricmp(token, "global1")) {
        return REG_GLOBAL1;
    } else if (!Q_stricmp(token, "global2")) {
        return REG_GLOBAL2;
    } else if (!Q_stricmp(token, "global3")) {
        return REG_GLOBAL3;
    } else if (!Q_stricmp(token, "scratch0")) {
        return REG_SCRATCH0;
    } else if (!Q_stricmp(token, "scratch1")) {
        return REG_SCRATCH1;
    } else if (!Q_stricmp(token, "scratch2")) {
        return REG_SCRATCH2;
    }
    
    // Try to parse as register number
    if (token[0] == 'r' || token[0] == 'R') {
        int reg = atoi(token + 1);
        if (reg >= 0 && reg < MAX_EXPRESSION_REGISTERS) {
            return reg;
        }
    }
    
    // Try to parse as plain number
    int reg = atoi(token);
    if (reg >= 0 && reg < MAX_EXPRESSION_REGISTERS) {
        return reg;
    }
    
    return 0; // Default to time register
}

/*
================
Material_ParseOperation

Parse operation type
================
*/
expOpType_t Material_ParseOperation(const char *token) {
    if (!Q_stricmp(token, "add") || !Q_stricmp(token, "+")) {
        return EXP_OP_ADD;
    } else if (!Q_stricmp(token, "subtract") || !Q_stricmp(token, "sub") || !Q_stricmp(token, "-")) {
        return EXP_OP_SUBTRACT;
    } else if (!Q_stricmp(token, "multiply") || !Q_stricmp(token, "mul") || !Q_stricmp(token, "*")) {
        return EXP_OP_MULTIPLY;
    } else if (!Q_stricmp(token, "divide") || !Q_stricmp(token, "div") || !Q_stricmp(token, "/")) {
        return EXP_OP_DIVIDE;
    } else if (!Q_stricmp(token, "mod") || !Q_stricmp(token, "%")) {
        return EXP_OP_MOD;
    } else if (!Q_stricmp(token, "table")) {
        return EXP_OP_TABLE;
    } else if (!Q_stricmp(token, "sin")) {
        return EXP_OP_SIN;
    } else if (!Q_stricmp(token, "cos")) {
        return EXP_OP_COS;
    } else if (!Q_stricmp(token, "square") || !Q_stricmp(token, "sqr")) {
        return EXP_OP_SQUARE;
    } else if (!Q_stricmp(token, "inverse") || !Q_stricmp(token, "inv")) {
        return EXP_OP_INVERSE;
    } else if (!Q_stricmp(token, "clamp")) {
        return EXP_OP_CLAMP;
    } else if (!Q_stricmp(token, "min")) {
        return EXP_OP_MIN;
    } else if (!Q_stricmp(token, "max")) {
        return EXP_OP_MAX;
    } else if (!Q_stricmp(token, "random") || !Q_stricmp(token, "rand")) {
        return EXP_OP_RANDOM;
    }
    
    ri.Printf(PRINT_WARNING, "WARNING: Unknown expression operation '%s'\n", token);
    return EXP_OP_ADD;
}

/*
================
Material_OperationNeedsSecondOperand

Check if operation needs two operands
================
*/
qboolean Material_OperationNeedsSecondOperand(expOpType_t op) {
    switch (op) {
    case EXP_OP_ADD:
    case EXP_OP_SUBTRACT:
    case EXP_OP_MULTIPLY:
    case EXP_OP_DIVIDE:
    case EXP_OP_MOD:
    case EXP_OP_MIN:
    case EXP_OP_MAX:
        return qtrue;
        
    case EXP_OP_TABLE:
    case EXP_OP_SIN:
    case EXP_OP_COS:
    case EXP_OP_SQUARE:
    case EXP_OP_INVERSE:
    case EXP_OP_CLAMP:
    case EXP_OP_RANDOM:
        return qfalse;
        
    default:
        return qfalse;
    }
}

/*
================
Material_TableLookup

Lookup value in expression table
================
*/
float Material_TableLookup(int tableIndex, float index) {
    int tablePos;
    float frac;
    float value1, value2;
    float *table;
    
    // Ensure tables are initialized
    if (!tablesInitialized) {
        Material_InitExpressionTables();
    }
    
    // Select table
    switch (tableIndex) {
    case 0: // Sine
        table = sineTable;
        break;
    case 1: // Triangle
        table = triangleTable;
        break;
    case 2: // Square
        table = squareTable;
        break;
    case 3: // Sawtooth
        table = sawtoothTable;
        break;
    case 4: // Inverse sawtooth
        table = inverseSawtoothTable;
        break;
    default:
        table = sineTable;
        break;
    }
    
    // Wrap index to [0, 1]
    index = index - floor(index);
    
    // Calculate table position
    index *= (TABLE_SIZE - 1);
    tablePos = (int)index;
    frac = index - tablePos;
    
    // Clamp to table bounds
    if (tablePos < 0) {
        tablePos = 0;
    } else if (tablePos >= TABLE_SIZE - 1) {
        tablePos = TABLE_SIZE - 2;
    }
    
    // Linear interpolation between table entries
    value1 = table[tablePos];
    value2 = table[tablePos + 1];
    
    return value1 + (value2 - value1) * frac;
}

/*
================
Material_ParseAnimMap

Parse animated texture map
================
*/
void Material_ParseAnimMap(material_t *material, materialStage_t *stage, char **text) {
    char *token;
    int maxAnimations;
    float frequency;
    
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: missing parameter for 'animmap' in material '%s'\n",
                  material->name);
        return;
    }
    
    frequency = atof(token);
    if (frequency <= 0) {
        frequency = 1;
    }
    stage->bundle[0].imageAnimationSpeed = frequency;
    
    // Parse animation frames
    maxAnimations = MAX_IMAGE_ANIMATIONS;
    stage->bundle[0].numImageAnimations = 0;
    
    while (1) {
        token = COM_ParseExt(text, qfalse);
        if (!token[0]) {
            break;
        }
        
        if (stage->bundle[0].numImageAnimations >= maxAnimations) {
            ri.Printf(PRINT_WARNING, "WARNING: too many images in animmap for material '%s'\n",
                      material->name);
            break;
        }
        
        stage->bundle[0].image[stage->bundle[0].numImageAnimations] = 
            R_FindImageFile(token, IMGFLAG_NONE);
        
        if (!stage->bundle[0].image[stage->bundle[0].numImageAnimations]) {
            ri.Printf(PRINT_WARNING, "WARNING: couldn't find image '%s' in material '%s'\n",
                      token, material->name);
            stage->bundle[0].image[stage->bundle[0].numImageAnimations] = tr.defaultImage;
        }
        
        stage->bundle[0].numImageAnimations++;
    }
    
    // Set primary texture to first frame
    if (stage->bundle[0].numImageAnimations > 0) {
        stage->colorMap = stage->bundle[0].image[0];
    }
}

/*
================
Material_ParseTCGen

Parse texture coordinate generation
================
*/
void Material_ParseTCGen(material_t *material, materialStage_t *stage, char **text) {
    char *token;
    
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: missing parameter for 'tcgen' in material '%s'\n",
                  material->name);
        return;
    }
    
    if (!Q_stricmp(token, "base") || !Q_stricmp(token, "texture")) {
        stage->tcGen = TCGEN_TEXTURE;
    } else if (!Q_stricmp(token, "lightmap")) {
        R_ReportLegacyLightmapUsage("Material_ParseTCGen");
        stage->tcGen = TCGEN_LIGHTMAP;
    } else if (!Q_stricmp(token, "environment")) {
        stage->tcGen = TCGEN_ENVIRONMENT_MAPPED;
    } else if (!Q_stricmp(token, "vector")) {
        stage->tcGen = TCGEN_VECTOR;
        
        // Parse S vector
        token = COM_ParseExt(text, qfalse);
        stage->tcGenVectors[0][0] = atof(token);
        token = COM_ParseExt(text, qfalse);
        stage->tcGenVectors[0][1] = atof(token);
        token = COM_ParseExt(text, qfalse);
        stage->tcGenVectors[0][2] = atof(token);
        
        // Parse T vector
        token = COM_ParseExt(text, qfalse);
        stage->tcGenVectors[1][0] = atof(token);
        token = COM_ParseExt(text, qfalse);
        stage->tcGenVectors[1][1] = atof(token);
        token = COM_ParseExt(text, qfalse);
        stage->tcGenVectors[1][2] = atof(token);
    } else {
        ri.Printf(PRINT_WARNING, "WARNING: unknown tcgen '%s' in material '%s'\n",
                  token, material->name);
    }
}

/*
================
Material_ParseTCMod

Parse texture coordinate modification
================
*/
void Material_ParseTCMod(material_t *material, materialStage_t *stage, char **text) {
    char *token;
    texModInfo_t *tmi;
    
    if (stage->numTexMods >= MAX_SHADER_STAGE_TEXMODS) {
        ri.Printf(PRINT_WARNING, "WARNING: too many tcmods in material '%s'\n", material->name);
        SkipRestOfLine(text);
        return;
    }
    
    tmi = &stage->texMods[stage->numTexMods];
    stage->numTexMods++;
    
    token = COM_ParseExt(text, qfalse);
    if (!token[0]) {
        ri.Printf(PRINT_WARNING, "WARNING: missing parameter for 'tcmod' in material '%s'\n",
                  material->name);
        return;
    }
    
    if (!Q_stricmp(token, "rotate")) {
        token = COM_ParseExt(text, qfalse);
        tmi->type = TMOD_ROTATE;
        tmi->rotateSpeed = atof(token);
    } else if (!Q_stricmp(token, "scale")) {
        token = COM_ParseExt(text, qfalse);
        tmi->scale[0] = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->scale[1] = atof(token);
        tmi->type = TMOD_SCALE;
    } else if (!Q_stricmp(token, "scroll")) {
        token = COM_ParseExt(text, qfalse);
        tmi->scroll[0] = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->scroll[1] = atof(token);
        tmi->type = TMOD_SCROLL;
    } else if (!Q_stricmp(token, "stretch")) {
        token = COM_ParseExt(text, qfalse);
        tmi->wave.func = NameToGenFunc(token);
        
        token = COM_ParseExt(text, qfalse);
        tmi->wave.base = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->wave.amplitude = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->wave.phase = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->wave.frequency = atof(token);
        
        tmi->type = TMOD_STRETCH;
    } else if (!Q_stricmp(token, "transform")) {
        // Parse transformation matrix (2x2 matrix + 2 translation values)
        token = COM_ParseExt(text, qfalse);
        tmi->matrix[0][0] = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->matrix[0][1] = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->matrix[1][0] = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->matrix[1][1] = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->translate[0] = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->translate[1] = atof(token);
        tmi->type = TMOD_TRANSFORM;
    } else if (!Q_stricmp(token, "turb")) {
        token = COM_ParseExt(text, qfalse);
        tmi->wave.base = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->wave.amplitude = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->wave.phase = atof(token);
        token = COM_ParseExt(text, qfalse);
        tmi->wave.frequency = atof(token);
        
        tmi->type = TMOD_TURBULENT;
    } else {
        ri.Printf(PRINT_WARNING, "WARNING: unknown tcmod '%s' in material '%s'\n",
                  token, material->name);
        stage->numTexMods--;
    }
}

/*
================
NameToGenFunc

Convert function name to genFunc_t
================
*/
genFunc_t NameToGenFunc(const char *funcname) {
    if (!Q_stricmp(funcname, "sin")) {
        return GF_SIN;
    } else if (!Q_stricmp(funcname, "square")) {
        return GF_SQUARE;
    } else if (!Q_stricmp(funcname, "triangle")) {
        return GF_TRIANGLE;
    } else if (!Q_stricmp(funcname, "sawtooth")) {
        return GF_SAWTOOTH;
    } else if (!Q_stricmp(funcname, "inversesawtooth")) {
        return GF_INVERSE_SAWTOOTH;
    } else if (!Q_stricmp(funcname, "noise")) {
        return GF_NOISE;
    }
    
    ri.Printf(PRINT_WARNING, "WARNING: invalid function name '%s' for wave\n", funcname);
    return GF_SIN;
}
