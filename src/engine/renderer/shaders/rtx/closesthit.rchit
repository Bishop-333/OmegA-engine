/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

RTX Closest Hit Shader
Handles ray-geometry intersections and material shading
===========================================================================
*/

#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

// Hit attributes from intersection
layout(location = 0) rayPayloadInEXT struct HitInfo {
    vec3 color;
    float distance;
    vec3 normal;
    float roughness;
    vec3 albedo;
    float metallic;
    vec3 worldPos;
    uint materialID;
    vec2 uv;
    uint primitiveID;
    uint instanceID;
    uint hitType;
} hitInfo;

// Intersection attributes
hitAttributeEXT vec2 baryCoords;

// Vertex data
struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 texCoord;
    vec3 tangent;
    vec4 color;
};

// Index buffer reference
layout(buffer_reference, scalar) buffer IndexBuffer {
    uvec3 indices[];
};

// Vertex buffer reference
layout(buffer_reference, scalar) buffer VertexBuffer {
    Vertex vertices[];
};

// Instance data
struct InstanceData {
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint materialIndex;
    uint lightmapIndex;
    mat4 normalMatrix;
    vec4 customData;
};

// Instance buffer
layout(binding = 10, set = 0, scalar) buffer InstanceDataBuffer {
    InstanceData instances[];
} instanceData;

// Material data
struct Material {
    vec4 albedo;
    vec4 specular;
    vec4 emission;
    float roughness;
    float metallic;
    float normalScale;
    float occlusionStrength;
    uint albedoTexture;
    uint normalTexture;
    uint roughnessTexture;
    uint metallicTexture;
    uint emissionTexture;
    uint occlusionTexture;
    uint lightmapTexture;
    uint flags;
};

// Material buffer
layout(binding = 11, set = 0, scalar) buffer MaterialBuffer {
    Material materials[];
} materialData;

// Texture array
layout(binding = 12, set = 0) uniform sampler2D textures[];

// Lightmap array
layout(binding = 13, set = 0) uniform sampler2D lightmaps[];

// Light data
struct Light {
    vec4 position;    // w = type (0=directional, 1=point, 2=spot)
    vec4 direction;   // w = inner cone angle for spot
    vec4 color;       // w = intensity
    vec4 attenuation; // x=constant, y=linear, z=quadratic, w=outer cone angle
};

layout(binding = 14, set = 0, scalar) buffer LightBuffer {
    uint numLights;
    Light lights[];
} lightData;

// Debug settings uniform buffer
layout(binding = 18, set = 0) uniform DebugSettings {
    uint noTextures;     // 1 = disable textures (use grey)
    uint debugMode;      // Various debug visualization modes
    uint reserved1;
    uint reserved2;
} debugSettings;

// Helper functions
vec3 getNormal(vec3 barycentrics, vec3 n0, vec3 n1, vec3 n2) {
    return normalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z);
}

vec2 getTexCoord(vec3 barycentrics, vec2 uv0, vec2 uv1, vec2 uv2) {
    return uv0 * barycentrics.x + uv1 * barycentrics.y + uv2 * barycentrics.z;
}

vec3 getVertexColor(vec3 barycentrics, vec4 c0, vec4 c1, vec4 c2) {
    vec4 color = c0 * barycentrics.x + c1 * barycentrics.y + c2 * barycentrics.z;
    return color.rgb;
}

vec3 applyNormalMap(vec3 normal, vec3 tangent, vec3 normalMapSample, float scale) {
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);
    vec3 mappedNormal = normalMapSample * 2.0 - 1.0;
    mappedNormal.xy *= scale;
    return normalize(TBN * mappedNormal);
}

float computeAttenuation(vec3 lightPos, vec3 worldPos, vec3 attenuation) {
    float distance = length(lightPos - worldPos);
    return 1.0 / (attenuation.x + attenuation.y * distance + attenuation.z * distance * distance);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265359 * denom * denom;
    
    return num / denom;
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

vec3 calculatePBR(vec3 albedo, float metallic, float roughness, vec3 normal, vec3 viewDir, vec3 lightDir, vec3 lightColor) {
    vec3 H = normalize(viewDir + lightDir);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    
    // Cook-Torrance BRDF
    float NDF = distributionGGX(normal, H, roughness);
    float G = geometrySmith(normal, viewDir, lightDir, roughness);
    vec3 F = fresnelSchlick(max(dot(H, viewDir), 0.0), F0);
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(normal, viewDir), 0.0) * max(dot(normal, lightDir), 0.0) + 0.001;
    vec3 specular = numerator / denominator;
    
    float NdotL = max(dot(normal, lightDir), 0.0);
    return (kD * albedo / 3.14159265359 + specular) * lightColor * NdotL;
}

void main() {
    // Get instance data
    InstanceData instance = instanceData.instances[gl_InstanceCustomIndexEXT];
    
    // Get vertex indices
    IndexBuffer indexBuffer = IndexBuffer(instance.indexBufferAddress);
    uvec3 indices = indexBuffer.indices[gl_PrimitiveID];
    
    // Get vertices
    VertexBuffer vertexBuffer = VertexBuffer(instance.vertexBufferAddress);
    Vertex v0 = vertexBuffer.vertices[indices.x];
    Vertex v1 = vertexBuffer.vertices[indices.y];
    Vertex v2 = vertexBuffer.vertices[indices.z];
    
    // Calculate barycentric coordinates
    vec3 barycentrics = vec3(1.0 - baryCoords.x - baryCoords.y, baryCoords.x, baryCoords.y);
    
    // Interpolate vertex attributes
    vec3 position = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
    vec3 normal = getNormal(barycentrics, v0.normal, v1.normal, v2.normal);
    vec2 texCoord = getTexCoord(barycentrics, v0.texCoord, v1.texCoord, v2.texCoord);
    vec3 tangent = normalize(v0.tangent * barycentrics.x + v1.tangent * barycentrics.y + v2.tangent * barycentrics.z);
    vec3 vertexColor = getVertexColor(barycentrics, v0.color, v1.color, v2.color);
    
    // Transform to world space
    vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 worldNormal = normalize(mat3(instance.normalMatrix) * normal);
    vec3 worldTangent = normalize(mat3(instance.normalMatrix) * tangent);
    
    // Get material
    Material mat = materialData.materials[instance.materialIndex];
    
    // Sample textures
    vec3 albedo = mat.albedo.rgb;
    if (mat.albedoTexture != 0 && debugSettings.noTextures == 0) {
        albedo *= texture(textures[mat.albedoTexture - 1], texCoord).rgb;
    } else if (debugSettings.noTextures != 0) {
        // Use default grey color when textures are disabled for debugging
        albedo = vec3(0.5, 0.5, 0.5);
    }
    albedo *= vertexColor;
    
    // Normal mapping
    if (mat.normalTexture != 0 && debugSettings.noTextures == 0) {
        vec3 normalMap = texture(textures[mat.normalTexture - 1], texCoord).rgb;
        worldNormal = applyNormalMap(worldNormal, worldTangent, normalMap, mat.normalScale);
    }
    
    // PBR parameters
    float roughness = mat.roughness;
    if (mat.roughnessTexture != 0 && debugSettings.noTextures == 0) {
        roughness *= texture(textures[mat.roughnessTexture - 1], texCoord).r;
    } else if (debugSettings.noTextures != 0) {
        // Use default roughness when textures are disabled
        roughness = 0.5;
    }
    
    float metallic = mat.metallic;
    if (mat.metallicTexture != 0 && debugSettings.noTextures == 0) {
        metallic *= texture(textures[mat.metallicTexture - 1], texCoord).r;
    } else if (debugSettings.noTextures != 0) {
        // Use default non-metallic when textures are disabled
        metallic = 0.0;
    }
    
    // Ambient occlusion
    float ao = 1.0;
    if (mat.occlusionTexture != 0 && debugSettings.noTextures == 0) {
        ao = texture(textures[mat.occlusionTexture - 1], texCoord).r;
        ao = mix(1.0, ao, mat.occlusionStrength);
    }
    
    // Emission
    vec3 emission = mat.emission.rgb * mat.emission.a;
    if (mat.emissionTexture != 0 && debugSettings.noTextures == 0) {
        emission *= texture(textures[mat.emissionTexture - 1], texCoord).rgb;
    } else if (debugSettings.noTextures != 0) {
        // No emission when textures are disabled
        emission = vec3(0.0);
    }
    
    // Lightmap
    vec3 lightmapColor = vec3(1.0);
    if (instance.lightmapIndex != 0 && mat.lightmapTexture != 0 && debugSettings.noTextures == 0) {
        vec2 lightmapUV = getTexCoord(barycentrics, v0.texCoord, v1.texCoord, v2.texCoord);
        lightmapColor = texture(lightmaps[mat.lightmapTexture - 1], lightmapUV).rgb;
    }
    
    // Calculate view direction
    vec3 viewDir = normalize(-gl_WorldRayDirectionEXT);
    
    // Direct lighting
    vec3 color = vec3(0.0);
    
    for (uint i = 0; i < lightData.numLights && i < 32; i++) {
        Light light = lightData.lights[i];
        vec3 lightDir;
        float attenuation = 1.0;
        
        if (light.position.w == 0.0) {
            // Directional light
            lightDir = normalize(-light.direction.xyz);
        } else if (light.position.w == 1.0) {
            // Point light
            lightDir = normalize(light.position.xyz - worldPos);
            attenuation = computeAttenuation(light.position.xyz, worldPos, light.attenuation.xyz);
        } else if (light.position.w == 2.0) {
            // Spot light
            lightDir = normalize(light.position.xyz - worldPos);
            float theta = dot(lightDir, normalize(-light.direction.xyz));
            float epsilon = light.direction.w - light.attenuation.w; // inner - outer
            float intensity = clamp((theta - light.attenuation.w) / epsilon, 0.0, 1.0);
            attenuation = computeAttenuation(light.position.xyz, worldPos, light.attenuation.xyz) * intensity;
        }
        
        vec3 lightColor = light.color.rgb * light.color.a * attenuation;
        color += calculatePBR(albedo, metallic, roughness, worldNormal, viewDir, lightDir, lightColor);
    }
    
    // Add emission and ambient
    color += emission;
    color += albedo * 0.03 * ao * lightmapColor; // Minimal ambient
    
    // Store hit information
    hitInfo.color = color;
    hitInfo.distance = gl_HitTEXT;
    hitInfo.normal = worldNormal;
    hitInfo.roughness = roughness;
    hitInfo.albedo = albedo;
    hitInfo.metallic = metallic;
    hitInfo.worldPos = worldPos;
    hitInfo.materialID = instance.materialIndex;
    hitInfo.uv = texCoord;
    hitInfo.primitiveID = gl_PrimitiveID;
    hitInfo.instanceID = gl_InstanceCustomIndexEXT;
    hitInfo.hitType = 1; // Hit
}