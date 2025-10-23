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
// vk_spirv.c - SPIR-V shader loading

#include "vk.h"

/*
================
R_LoadSPIRV

Load SPIR-V shader bytecode from file
================
*/
uint32_t* R_LoadSPIRV( const char *filename, uint32_t *codeSize ) {
    FILE *file;
    uint32_t *buffer;
    long fileSize;
    size_t bytesRead;
    char fullPath[MAX_OSPATH];
    
    if ( !filename || !codeSize ) {
        ri.Error( ERR_DROP, "R_LoadSPIRV: NULL parameter" );
        return NULL;
    }
    
    // Construct full path to shader file
    const char *searchRoots[] = {
        ri.Cvar_Get( "fs_homepath", "", 0 )->string,
        ri.Cvar_Get( "fs_basepath", "", 0 )->string,
        ""
    };
    const size_t rootCount = sizeof(searchRoots) / sizeof(searchRoots[0]);

    file = NULL;
    for (size_t i = 0; i < rootCount && !file; ++i) {
        if (searchRoots[i] && searchRoots[i][0]) {
            Com_sprintf( fullPath, sizeof(fullPath), "%s/shaders/%s", searchRoots[i], filename );
            file = fopen( fullPath, "rb" );
            if ( !file ) {
                Com_sprintf( fullPath, sizeof(fullPath), "%s/baseq3/shaders/%s", searchRoots[i], filename );
                file = fopen( fullPath, "rb" );
            }
        }
    }

    if ( !file ) {
        Com_sprintf( fullPath, sizeof(fullPath), "shaders/%s", filename );
        file = fopen( fullPath, "rb" );
    }

    if ( !file ) {
        ri.Printf( PRINT_WARNING, "R_LoadSPIRV: Failed to open shader file '%s'\n", filename );
        *codeSize = 0;
        return NULL;
    }
    
    // Get file size
    fseek( file, 0, SEEK_END );
    fileSize = ftell( file );
    fseek( file, 0, SEEK_SET );
    
    // Validate file size
    if ( fileSize <= 0 || fileSize > 1024 * 1024 * 10 ) { // Max 10MB for shader
        ri.Printf( PRINT_WARNING, "R_LoadSPIRV: Invalid file size %ld for '%s'\n", fileSize, filename );
        fclose( file );
        *codeSize = 0;
        return NULL;
    }
    
    // SPIR-V requires 4-byte alignment
    if ( fileSize % 4 != 0 ) {
        ri.Printf( PRINT_WARNING, "R_LoadSPIRV: File '%s' is not 4-byte aligned\n", filename );
        fclose( file );
        *codeSize = 0;
        return NULL;
    }
    
    // Allocate buffer for SPIR-V code
    buffer = (uint32_t*)ri.Malloc( fileSize );
    if ( !buffer ) {
        ri.Error( ERR_DROP, "R_LoadSPIRV: Failed to allocate %ld bytes for shader '%s'", fileSize, filename );
        fclose( file );
        *codeSize = 0;
        return NULL;
    }
    
    // Read the file
    bytesRead = fread( buffer, 1, fileSize, file );
    fclose( file );
    
    if ( bytesRead != (size_t)fileSize ) {
        ri.Printf( PRINT_WARNING, "R_LoadSPIRV: Read %zu bytes, expected %ld for '%s'\n", 
                   bytesRead, fileSize, filename );
        ri.Free( buffer );
        *codeSize = 0;
        return NULL;
    }
    
    // Validate SPIR-V magic number (0x07230203)
    if ( buffer[0] != 0x07230203 ) {
        ri.Printf( PRINT_WARNING, "R_LoadSPIRV: Invalid SPIR-V magic number 0x%08x in '%s'\n", 
                   buffer[0], filename );
        ri.Free( buffer );
        *codeSize = 0;
        return NULL;
    }
    
    // Set output size in bytes
    *codeSize = (uint32_t)fileSize;
    
    ri.Printf( PRINT_DEVELOPER, "R_LoadSPIRV: Loaded shader '%s' (%u bytes)\n", filename, *codeSize );
    
    return buffer;
}
