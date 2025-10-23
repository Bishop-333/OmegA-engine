/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

This file is part of Quake3e-HD.

Quake3e-HD is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

Quake3e-HD is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake3e-HD; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
===========================================================================
*/
// tr_vrs.h - Variable Rate Shading system

#ifndef __TR_VRS_H
#define __TR_VRS_H

#include "../core/tr_local.h"
#include "vulkan/vulkan.h"

// VRS shading rates (NV_shading_rate_image extension)
typedef enum {
    VRS_RATE_1X1 = 0,    // Full resolution
    VRS_RATE_1X2 = 1,    // Half vertical resolution
    VRS_RATE_2X1 = 4,    // Half horizontal resolution  
    VRS_RATE_2X2 = 5,    // Quarter resolution
    VRS_RATE_2X4 = 6,    // 1/8 resolution
    VRS_RATE_4X2 = 9,    // 1/8 resolution
    VRS_RATE_4X4 = 10,   // 1/16 resolution
} vrsRate_t;

// VRS combiner operations
typedef enum {
    VRS_COMBINER_MIN = 0,
    VRS_COMBINER_MAX = 1,
    VRS_COMBINER_SUM = 2,
    VRS_COMBINER_AVERAGE = 3,
    VRS_COMBINER_PASSTHROUGH = 4,
} vrsCombinerOp_t;

// VRS quality modes
typedef enum {
    VRS_QUALITY_OFF = 0,
    VRS_QUALITY_PERFORMANCE,     // Aggressive VRS for maximum performance
    VRS_QUALITY_BALANCED,        // Balanced quality/performance
    VRS_QUALITY_QUALITY,         // Conservative VRS for best quality
    VRS_QUALITY_CUSTOM,          // User-defined settings
    VRS_QUALITY_ADAPTIVE,        // Dynamic based on frame timing
} vrsQualityMode_t;

// VRS heuristic sources
typedef enum {
    VRS_SOURCE_NONE = 0,
    VRS_SOURCE_DISTANCE = ( 1 << 0 ),     // Distance-based shading rate
    VRS_SOURCE_MOTION = ( 1 << 1 ),       // Motion vector based
    VRS_SOURCE_FOVEATED = ( 1 << 2 ),     // Foveated rendering
    VRS_SOURCE_CONTENT = ( 1 << 3 ),      // Content-adaptive
    VRS_SOURCE_LUMINANCE = ( 1 << 4 ),    // Luminance-based
} vrsSource_t;

// Per-tile VRS data
typedef struct vrsTileData_s {
    uint8_t     shadingRate;
    uint8_t     confidence;
    uint16_t    flags;
} vrsTileData_t;

// VRS configuration
typedef struct vrsConfig_s {
    vrsQualityMode_t    qualityMode;
    uint32_t            sourceMask;        // Combination of vrsSource_t
    
    // Distance-based parameters
    float               distanceNear;      // Start fading shading rate
    float               distanceFar;       // Maximum VRS distance
    vrsRate_t           distanceMaxRate;   // Maximum rate at far distance
    
    // Motion-based parameters  
    float               motionThreshold;   // Velocity threshold for VRS
    vrsRate_t           motionMaxRate;     // Maximum rate for moving pixels
    
    // Foveated parameters
    vec2_t              foveaCenter;       // Screen-space fovea center
    float               foveaRadius;       // Fovea radius (full quality)
    float               peripheryRadius;   // Periphery start radius
    vrsRate_t           peripheryMaxRate;  // Maximum rate in periphery
    
    // Content-adaptive parameters
    float               edgeThreshold;     // Edge detection threshold
    float               contrastThreshold; // Contrast threshold
    
    // Adaptive timing
    float               targetFrameTime;   // Target frame time in ms
    float               adaptiveStrength;  // How aggressively to adapt
} vrsConfig_t;

// VRS statistics
typedef struct vrsStats_s {
    uint32_t    pixelsShaded[11];      // Pixels at each shading rate
    float       averageShadingRate;    // Average rate across frame
    float       vrsEfficiency;         // Percentage of pixels with VRS
    uint32_t    tilesUpdated;          // Number of tiles updated this frame
} vrsStats_t;

// VRS state
typedef struct vrsState_s {
    // Hardware capabilities
    qboolean            supported;
    qboolean            perPrimitive;
    qboolean            perViewport;
    qboolean            imageBasedSupported;
    VkExtent2D          tileSize;
    uint32_t            maxShadingRate;
    
    // Shading rate image
    VkImage             rateImage;
    VkImageView         rateImageView;
    VkDeviceMemory      rateImageMemory;
    uint32_t            rateImageWidth;
    uint32_t            rateImageHeight;
    
    // Rate image generation
    VkPipeline          generatePipeline;
    VkPipelineLayout    generatePipelineLayout;
    VkDescriptorSet     generateDescriptorSet;
    
    // Motion vectors for motion-based VRS
    VkImage             motionVectorImage;
    VkImageView         motionVectorView;
    VkDeviceMemory      motionVectorMemory;
    
    // Previous frame for temporal analysis
    VkImage             previousFrameImage;
    VkImageView         previousFrameView;
    VkDeviceMemory      previousFrameMemory;
    
    // Configuration
    vrsConfig_t         config;
    
    // Statistics
    vrsStats_t          stats;
    
    // CPU-side tile data for debugging
    vrsTileData_t      *tileData;
    uint32_t            numTilesX;
    uint32_t            numTilesY;
} vrsState_t;

// API functions
qboolean R_InitVRS( void );
void R_ShutdownVRS( void );

// Configuration
void R_SetVRSQuality( vrsQualityMode_t quality );
void R_SetVRSConfig( const vrsConfig_t *config );
void R_GetVRSConfig( vrsConfig_t *config );

// Per-frame operations
void R_BeginVRSFrame( void );
void R_EndVRSFrame( void );
void R_UpdateShadingRateImage( VkCommandBuffer cmd, const viewParms_t *viewParms );

// Shading rate control
void R_SetShadingRate( VkCommandBuffer cmd, vrsRate_t rate );
void R_SetShadingRateImage( VkCommandBuffer cmd );
void R_SetPerPrimitiveShadingRate( VkCommandBuffer cmd, qboolean enable );

// Motion vector generation
void R_GenerateMotionVectors( VkCommandBuffer cmd );
void R_UpdateMotionVectorHistory( VkCommandBuffer cmd );

// Foveated rendering
void R_SetFoveaCenter( float x, float y );
void R_UpdateFoveatedPattern( VkCommandBuffer cmd );

// Content-adaptive VRS
void R_AnalyzeFrameContent( VkCommandBuffer cmd );
void R_GenerateAdaptiveRateImage( VkCommandBuffer cmd );

// Statistics and debugging
void R_GetVRSStats( vrsStats_t *stats );
void R_DrawVRSDebugOverlay( void );
void R_ExportShadingRateImage( const char *filename );

// Helper functions
vrsRate_t R_CalculateDistanceRate( float distance );
vrsRate_t R_CalculateMotionRate( const vec2_t motion );
vrsRate_t R_CalculateFoveatedRate( float x, float y );
vrsRate_t R_CombineRates( vrsRate_t rate1, vrsRate_t rate2, vrsCombinerOp_t op );

// Vulkan-specific helpers
VkShadingRatePaletteEntryNV R_VRSRateToVulkan( vrsRate_t rate );
void R_CreateShadingRateImage( uint32_t width, uint32_t height );
void R_DestroyShadingRateImage( void );

// Expose motion vector view for consumers (read-only)
VkImageView R_GetMotionVectorView( void );
VkImage     R_GetMotionVectorImage( void );

#endif // __TR_VRS_H
