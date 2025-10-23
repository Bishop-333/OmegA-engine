#version 450

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec2 inTexCoord2;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inColor;
layout(location = 5) in vec4 inTangent;  // xyz = tangent, w = handedness for bitangent

// Uniform buffer for transform matrices
layout(set = 0, binding = 0) uniform TransformUBO {
    mat4 mvpMatrix;
    mat4 modelMatrix;
    mat4 normalMatrix;
} transforms;

// Push constants for uber shader configuration
layout(push_constant) uniform PushConstants {
    // Uber shader config (16 bytes)
    uint features;
    uint blendMode;
    uint cullMode;
    uint padding0;

    // Material parameters (32 bytes)
    vec4 baseColor;
    vec4 specularColor;
    float specularExponent;
    float alphaTestValue;
    float time;
    float portalRange;

    // Texture mods (32 bytes)
    vec4 tcModParams[2];

    // Wave params (32 bytes)
    vec4 rgbWaveParams;
    vec4 alphaWaveParams;

    // Fog (24 bytes)
    vec4 fogColor;
    vec2 fogParams;
    vec2 fogPadding;

    // Lighting (32 bytes)
    vec4 lightPosition;
    vec4 lightColor;
    float lightRadius;
    float lightIntensity;
    vec2 lightPadding;

    // UBO index (16 bytes)
    uint transformIndex;
    uint padding1;
    uint padding2;
    uint padding3;
} pc;

// Output to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec2 fragTexCoord2;
layout(location = 3) out vec3 fragNormal;
layout(location = 4) out vec4 fragColor;
layout(location = 5) out vec3 fragTangent;
layout(location = 6) out vec3 fragBitangent;
layout(location = 7) out vec3 fragViewDir;

void main() {
    // DEBUG VERSION - Simplified vertex shader

    // Transform position - just use MVP matrix directly
    gl_Position = transforms.mvpMatrix * vec4(inPosition, 1.0);

    // Pass through simple data
    fragWorldPos = inPosition;  // Just pass position as-is
    fragTexCoord = inTexCoord;
    fragTexCoord2 = inTexCoord2;
    fragNormal = inNormal;  // Pass normal without transformation
    fragColor = pc.baseColor;  // Use base color from push constants
    fragTangent = vec3(1.0, 0.0, 0.0);  // Dummy tangent
    fragBitangent = vec3(0.0, 1.0, 0.0);  // Dummy bitangent
    fragViewDir = vec3(0.0, 0.0, 1.0);  // Dummy view direction
}