/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Volumetric Explosions Effect Header
===========================================================================
*/

#ifndef TR_VOLUMETRIC_EXPLOSIONS_H
#define TR_VOLUMETRIC_EXPLOSIONS_H

#include "../core/tr_local.h"

// Volumetric explosion structure
typedef struct volumetricExplosion_s {
    vec3_t      origin;
    float       radius;
    float       intensity;
    float       time;
    float       duration;
    qboolean    active;
} volumetricExplosion_t;

// Maximum number of simultaneous volumetric explosions
#define MAX_VOLUMETRIC_EXPLOSIONS 32

// Function declarations
void R_InitVolumetricExplosions(void);
void R_ShutdownVolumetricExplosions(void);
void R_AddVolumetricExplosion(vec3_t origin, float radius, float intensity, float duration);
void R_UpdateVolumetricExplosions(float deltaTime);
void R_RenderVolumetricExplosions(void);

#endif // TR_VOLUMETRIC_EXPLOSIONS_H