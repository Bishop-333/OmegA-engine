/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Ray Generation Shader for RTX Path Tracing
Primary ray generation and dispatch
===========================================================================
*/

#include "common.hlsli"

// Ray payload structure
struct RayPayload {
    float3 color;
    float3 normal;
    float3 albedo;
    float  distance;
    uint   recursionDepth;
    bool   hit;
};

// Shader resources
RaytracingAccelerationStructure g_SceneTLAS : register(t0);
RWTexture2D<float4> g_Output : register(u0);
RWTexture2D<float4> g_Albedo : register(u1);
RWTexture2D<float4> g_Normal : register(u2);
RWTexture2D<float>  g_Depth : register(u3);
RWTexture2D<float2> g_Motion : register(u4);

// Constant buffer
cbuffer GlobalConstants : register(b0) {
    float4x4 g_ViewMatrix;
    float4x4 g_ProjMatrix;
    float4x4 g_InvViewMatrix;
    float4x4 g_InvProjMatrix;
    float4x4 g_PrevViewProjMatrix;
    float3   g_CameraPos;
    float    g_Time;
    float3   g_CameraDir;
    float    g_DeltaTime;
    uint     g_FrameCount;
    uint     g_MaxBounces;
    uint     g_SamplesPerPixel;
    uint     g_RandomSeed;
    float    g_Exposure;
    float    g_GammaCorrection;
    uint     g_EnableDenoiser;
    uint     g_EnableMotionVectors;
};

// Random number generation
uint WangHash(uint seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

float RandomFloat(inout uint seed) {
    seed = WangHash(seed);
    return float(seed) / 4294967296.0f;
}

float3 RandomInUnitSphere(inout uint seed) {
    float theta = RandomFloat(seed) * 2.0f * 3.14159265f;
    float phi = acos(2.0f * RandomFloat(seed) - 1.0f);
    float r = pow(RandomFloat(seed), 1.0f / 3.0f);
    
    float sinPhi = sin(phi);
    return float3(
        r * sinPhi * cos(theta),
        r * sinPhi * sin(theta),
        r * cos(phi)
    );
}

// Generate camera ray
RayDesc GenerateCameraRay(uint2 pixelCoord, uint2 screenDimensions, inout uint seed) {
    // Add jitter for antialiasing
    float2 jitter = float2(RandomFloat(seed) - 0.5f, RandomFloat(seed) - 0.5f);
    float2 uv = (float2(pixelCoord) + 0.5f + jitter) / float2(screenDimensions);
    
    // Convert to NDC
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // Flip Y axis
    
    // Transform to world space
    float4 clipSpace = float4(ndc, 0.0f, 1.0f);
    float4 viewSpace = mul(g_InvProjMatrix, clipSpace);
    viewSpace /= viewSpace.w;
    
    float4 worldSpace = mul(g_InvViewMatrix, float4(viewSpace.xyz, 1.0f));
    float3 rayDir = normalize(worldSpace.xyz - g_CameraPos);
    
    RayDesc ray;
    ray.Origin = g_CameraPos;
    ray.Direction = rayDir;
    ray.TMin = 0.001f;
    ray.TMax = 10000.0f;
    
    return ray;
}

// Calculate motion vectors
float2 CalculateMotionVector(float3 worldPos, uint2 pixelCoord, uint2 screenDimensions) {
    // Current frame position
    float2 currentUV = float2(pixelCoord) / float2(screenDimensions);
    
    // Previous frame position
    float4 prevClipPos = mul(g_PrevViewProjMatrix, float4(worldPos, 1.0f));
    float2 prevUV = prevClipPos.xy / prevClipPos.w * 0.5f + 0.5f;
    prevUV.y = 1.0f - prevUV.y;
    
    // Motion vector
    return currentUV - prevUV;
}

// Main ray generation shader
[shader("raygeneration")]
void RayGen() {
    uint2 pixelCoord = DispatchRaysIndex().xy;
    uint2 screenDimensions = DispatchRaysDimensions().xy;
    
    // Initialize random seed
    uint seed = g_RandomSeed ^ (pixelCoord.x * 73856093) ^ (pixelCoord.y * 19349663) ^ g_FrameCount;
    
    // Accumulate samples
    float3 totalColor = float3(0, 0, 0);
    float3 totalAlbedo = float3(0, 0, 0);
    float3 totalNormal = float3(0, 0, 0);
    float totalDepth = 0.0f;
    
    for (uint sample = 0; sample < g_SamplesPerPixel; sample++) {
        // Generate primary ray
        RayDesc ray = GenerateCameraRay(pixelCoord, screenDimensions, seed);
        
        // Initialize payload
        RayPayload payload;
        payload.color = float3(0, 0, 0);
        payload.normal = float3(0, 0, 0);
        payload.albedo = float3(0, 0, 0);
        payload.distance = 0.0f;
        payload.recursionDepth = 0;
        payload.hit = false;
        
        // Trace primary ray
        TraceRay(
            g_SceneTLAS,
            RAY_FLAG_NONE,
            0xFF, // Instance mask
            0,    // Hit group index
            0,    // Number of hit groups
            0,    // Miss shader index
            ray,
            payload
        );
        
        // Accumulate results
        totalColor += payload.color;
        totalAlbedo += payload.albedo;
        totalNormal += payload.normal;
        totalDepth += payload.distance;
    }
    
    // Average samples
    totalColor /= float(g_SamplesPerPixel);
    totalAlbedo /= float(g_SamplesPerPixel);
    totalNormal = normalize(totalNormal);
    totalDepth /= float(g_SamplesPerPixel);
    
    // Apply exposure and gamma correction
    totalColor *= g_Exposure;
    totalColor = pow(totalColor, 1.0f / g_GammaCorrection);
    
    // Write outputs
    g_Output[pixelCoord] = float4(totalColor, 1.0f);
    
    if (g_EnableDenoiser) {
        g_Albedo[pixelCoord] = float4(totalAlbedo, 1.0f);
        g_Normal[pixelCoord] = float4(totalNormal * 0.5f + 0.5f, 1.0f);
        g_Depth[pixelCoord] = totalDepth;
    }
    
    if (g_EnableMotionVectors && totalDepth > 0.0f) {
        float3 worldPos = g_CameraPos + normalize(totalNormal) * totalDepth;
        float2 motionVector = CalculateMotionVector(worldPos, pixelCoord, screenDimensions);
        g_Motion[pixelCoord] = motionVector;
    }
}

// Miss shader - sky/environment
[shader("miss")]
void Miss(inout RayPayload payload) {
    // Simple sky gradient
    float3 rayDir = WorldRayDirection();
    float t = 0.5f * (rayDir.y + 1.0f);
    float3 skyColor = lerp(float3(1.0f, 1.0f, 1.0f), float3(0.5f, 0.7f, 1.0f), t);
    
    // Add sun
    float3 sunDir = normalize(float3(0.5f, 0.7f, 0.3f));
    float sunIntensity = pow(max(0.0f, dot(rayDir, sunDir)), 256.0f) * 10.0f;
    skyColor += float3(1.0f, 0.9f, 0.7f) * sunIntensity;
    
    payload.color = skyColor;
    payload.hit = false;
}

// Closest hit shader
[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    // Get hit information
    float3 hitPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    uint instanceID = InstanceID();
    uint primitiveID = PrimitiveIndex();
    float2 barycentrics = attribs.barycentrics;
    
    // Get vertex data
    float3 normal = GetVertexNormal(instanceID, primitiveID, barycentrics);
    float2 texCoord = GetVertexTexCoord(instanceID, primitiveID, barycentrics);
    float3 albedo = GetAlbedo(instanceID, texCoord);
    
    // Store G-buffer data
    payload.normal = normal;
    payload.albedo = albedo;
    payload.distance = RayTCurrent();
    payload.hit = true;

    bool debugMode = (g_SurfaceDebugMode != 0);

    if (!debugMode) {
        if (payload.recursionDepth < g_MaxBounces) {
            // Generate random bounce direction (diffuse)
            uint seed = g_RandomSeed ^ (instanceID * 73856093) ^ (primitiveID * 19349663);
            float3 randomDir = normalize(normal + RandomInUnitSphere(seed));

            // Ensure direction is in hemisphere
            if (dot(randomDir, normal) < 0.0f) {
                randomDir = -randomDir;
            }

            // Create secondary ray
            RayDesc secondaryRay;
            secondaryRay.Origin = hitPos + normal * 0.001f;
            secondaryRay.Direction = randomDir;
            secondaryRay.TMin = 0.001f;
            secondaryRay.TMax = 10000.0f;

            // Trace secondary ray
            RayPayload secondaryPayload;
            secondaryPayload.color = float3(0, 0, 0);
            secondaryPayload.recursionDepth = payload.recursionDepth + 1;
            secondaryPayload.hit = false;

            TraceRay(
                g_SceneTLAS,
                RAY_FLAG_NONE,
                0xFF,
                0, 0, 0,
                secondaryRay,
                secondaryPayload
            );

            // Combine with albedo
            payload.color = albedo * secondaryPayload.color;
        } else {
            // Terminal bounce - just use albedo
            payload.color = albedo * 0.5f; // Ambient term
        }
    }

    float3 directLighting = CalculateDirectLighting(hitPos, normal, albedo);

    if (debugMode) {
        float luminance = saturate(dot(directLighting, float3(0.299f, 0.587f, 0.114f)));
        float3 grayscale = float3(luminance, luminance, luminance);
        float3 normalColor = normal * 0.5f + 0.5f;
        payload.color = saturate(grayscale * 0.6f + normalColor * 0.4f);
        payload.albedo = grayscale;
        payload.normal = normal;
        payload.distance = RayTCurrent();
        return;
    }

    // Add direct lighting
    payload.color += directLighting;
}