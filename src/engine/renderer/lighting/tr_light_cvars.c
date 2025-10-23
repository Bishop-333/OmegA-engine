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
// tr_light_cvars.c - Legacy lighting CVars have been retired in favor of the unified pipeline.

#include "../core/tr_local.h"

/*
================
R_InitLightingCVars

Legacy entry point retained for compatibility; all former lighting CVars
have been removed as part of the modernized lighting pipeline.
================
*/
void R_InitLightingCVars( void ) {
    ri.Printf( PRINT_DEVELOPER, "Lighting CVars are managed internally by the unified lighting system.\n" );
}
