/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

RTX Diagnostic Functions
Comprehensive diagnostics for RTX ray tracing issues
===========================================================================
*/

#include "rt_rtx.h"
#include "../core/tr_local.h"

extern rtxState_t rtx;

/*
================
RTX_DiagnosticReport

Generate a comprehensive diagnostic report for RTX
================
*/
void RTX_DiagnosticReport(void) {
    ri.Printf(PRINT_ALL, "\n");
    ri.Printf(PRINT_ALL, "==================================================\n");
    ri.Printf(PRINT_ALL, "         RTX DIAGNOSTIC REPORT\n");
    ri.Printf(PRINT_ALL, "==================================================\n");

    // Check cvars
    ri.Printf(PRINT_ALL, "\n[CVARS]\n");
    extern cvar_t *r_rtx_enabled;
    extern cvar_t *r_rtx_notextures;
    ri.Printf(PRINT_ALL, "  r_rtx_enabled: %s\n", r_rtx_enabled ? r_rtx_enabled->string : "NULL");
    ri.Printf(PRINT_ALL, "  r_rtx_notextures: %s\n", r_rtx_notextures ? r_rtx_notextures->string : "NULL");

    // Check RTX state
    ri.Printf(PRINT_ALL, "\n[RTX STATE]\n");
    ri.Printf(PRINT_ALL, "  rtx.available: %s\n", rtx.available ? "TRUE" : "FALSE");
    ri.Printf(PRINT_ALL, "  rtx.features: 0x%08X\n", rtx.features);
    ri.Printf(PRINT_ALL, "  rtx.frameCount: %u\n", rtx.frameCount);
    ri.Printf(PRINT_ALL, "  rtx.numBLAS: %d\n", rtx.numBLAS);
    ri.Printf(PRINT_ALL, "  rtx.tlas.handle: %s\n", rtx.tlas.handle ? "VALID" : "NULL");
    ri.Printf(PRINT_ALL, "  rtx.tlas.numInstances: %d\n", rtx.tlas.numInstances);
    ri.Printf(PRINT_ALL, "  rtx.denoiser.enabled: %s\n", rtx.denoiser.enabled ? "TRUE" : "FALSE");

    // Check pipeline state
    ri.Printf(PRINT_ALL, "\n[PIPELINES]\n");
    ri.Printf(PRINT_ALL, "  primary.handle: %s\n", rtx.primaryPipeline.handle ? "VALID" : "NULL");
    ri.Printf(PRINT_ALL, "  shadow.handle: %s\n", rtx.shadowPipeline.handle ? "VALID" : "NULL");
    ri.Printf(PRINT_ALL, "  gi.handle: %s\n", rtx.giPipeline.handle ? "VALID" : "NULL");

    // Check shader files
    ri.Printf(PRINT_ALL, "\n[SHADER FILES]\n");
    const char* shaderPaths[] = {
        "baseq3/shaders/rtx/raygen.spv",
        "baseq3/shaders/rtx/closesthit.spv",
        "baseq3/shaders/rtx/miss.spv",
        "baseq3/shaders/rtx/shadow.spv"
    };

    for (int i = 0; i < 4; i++) {
        qboolean found = ri.FS_FileExists(shaderPaths[i]);
        ri.Printf(PRINT_ALL, "  %s: %s\n", shaderPaths[i], found ? "FOUND" : "MISSING!");
    }

    // Check RTX enable state
    ri.Printf(PRINT_ALL, "\n[ENABLE STATE]\n");
    ri.Printf(PRINT_ALL, "  RTX_IsEnabled(): %s\n", RTX_IsEnabled() ? "TRUE" : "FALSE");
    ri.Printf(PRINT_ALL, "  RTX_IsAvailable(): %s\n", RTX_IsAvailable() ? "TRUE" : "FALSE");

    // Diagnosis
    ri.Printf(PRINT_ALL, "\n[DIAGNOSIS]\n");

    if (!r_rtx_enabled || !r_rtx_enabled->integer) {
        ri.Printf(PRINT_ALL, "  PROBLEM: RTX is disabled in cvars\n");
        ri.Printf(PRINT_ALL, "  SOLUTION: set r_rtx_enabled 1; vid_restart\n");
    }
    else if (!rtx.available) {
        ri.Printf(PRINT_ALL, "  PROBLEM: RTX not available (initialization failed)\n");
        ri.Printf(PRINT_ALL, "  SOLUTION: Check console for initialization errors\n");
    }
    else if (!rtx.primaryPipeline.handle) {
        ri.Printf(PRINT_ALL, "  PROBLEM: RT pipeline not created\n");
        ri.Printf(PRINT_ALL, "  SOLUTION: Check if shaders are compiled and present\n");
    }
    else if (!rtx.tlas.handle) {
        ri.Printf(PRINT_ALL, "  PROBLEM: No TLAS (top-level acceleration structure)\n");
        ri.Printf(PRINT_ALL, "  SOLUTION: Load a map to build scene geometry\n");
    }
    else {
        ri.Printf(PRINT_ALL, "  RTX appears to be properly initialized\n");
        ri.Printf(PRINT_ALL, "  If you're not seeing RTX output, check:\n");
        ri.Printf(PRINT_ALL, "  - Are shaders compiled? (compile_rtx_shaders.bat)\n");
        ri.Printf(PRINT_ALL, "  - Is a map loaded?\n");
        ri.Printf(PRINT_ALL, "  - Check console for 'RTX: Recording commands' messages\n");
    }

    ri.Printf(PRINT_ALL, "\n==================================================\n");
}

/*
================
RTX_Cmd_Diagnostic_f

Console command to run diagnostic
================
*/
void RTX_Cmd_Diagnostic_f(void) {
    RTX_DiagnosticReport();
}
