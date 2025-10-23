/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Volumetric Explosions Effect Implementation
===========================================================================
*/

#include "tr_volumetric_explosions.h"

static volumetricExplosion_t explosions[MAX_VOLUMETRIC_EXPLOSIONS];
static int numExplosions = 0;

/*
================
R_InitVolumetricExplosions

Initialize volumetric explosions system
================
*/
void R_InitVolumetricExplosions(void) {
    Com_Memset(explosions, 0, sizeof(explosions));
    numExplosions = 0;
}

/*
================
R_ShutdownVolumetricExplosions

Shutdown volumetric explosions system
================
*/
void R_ShutdownVolumetricExplosions(void) {
    Com_Memset(explosions, 0, sizeof(explosions));
    numExplosions = 0;
}

/*
================
R_AddVolumetricExplosion

Add a new volumetric explosion
================
*/
void R_AddVolumetricExplosion(vec3_t origin, float radius, float intensity, float duration) {
    if (numExplosions >= MAX_VOLUMETRIC_EXPLOSIONS) {
        return;
    }
    
    volumetricExplosion_t *exp = &explosions[numExplosions++];
    VectorCopy(origin, exp->origin);
    exp->radius = radius;
    exp->intensity = intensity;
    exp->duration = duration;
    exp->time = 0.0f;
    exp->active = qtrue;
}

/*
================
R_UpdateVolumetricExplosions

Update all active volumetric explosions
================
*/
void R_UpdateVolumetricExplosions(float deltaTime) {
    int i;
    for (i = 0; i < numExplosions; i++) {
        volumetricExplosion_t *exp = &explosions[i];
        if (!exp->active) continue;
        
        exp->time += deltaTime;
        if (exp->time >= exp->duration) {
            exp->active = qfalse;
        }
    }
    
    // Compact the array
    int writeIdx = 0;
    for (i = 0; i < numExplosions; i++) {
        if (explosions[i].active) {
            if (i != writeIdx) {
                explosions[writeIdx] = explosions[i];
            }
            writeIdx++;
        }
    }
    numExplosions = writeIdx;
}

/*
================
R_RenderVolumetricExplosions

Render all active volumetric explosions
================
*/
void R_RenderVolumetricExplosions(void) {
    // TODO: Implement volumetric explosion rendering
    // This is currently a stub implementation
}