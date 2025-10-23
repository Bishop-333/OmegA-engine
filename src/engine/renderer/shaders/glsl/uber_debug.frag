#version 450

// Input from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec2 fragTexCoord2;
layout(location = 3) in vec3 fragNormal;
layout(location = 4) in vec4 fragColor;
layout(location = 5) in vec3 fragTangent;
layout(location = 6) in vec3 fragBitangent;
layout(location = 7) in vec3 fragViewDir;

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

// Descriptor set bindings (shifted due to UBO at binding 0)
layout(set = 0, binding = 1) uniform sampler2D diffuseMap;
layout(set = 0, binding = 2) uniform sampler2D lightMap;
layout(set = 0, binding = 3) uniform sampler2D normalMap;
layout(set = 0, binding = 4) uniform sampler2D specularMap;
layout(set = 0, binding = 5) uniform samplerCube environmentMap;

// Output
layout(location = 0) out vec4 outColor;

// Main fragment shader - DEBUG VERSION
void main() {
    // DEBUG: Output solid colors based on position to verify pipeline is working

    // Create a simple gradient based on screen position
    vec3 debugColor = vec3(0.0);

    // Use fragment coordinates to create a pattern
    debugColor.r = fract(fragTexCoord.x * 10.0);
    debugColor.g = fract(fragTexCoord.y * 10.0);
    debugColor.b = 0.5 + 0.5 * sin(pc.time);

    // Also output the base color to see if push constants are working
    if (pc.baseColor.a > 0.0) {
        debugColor = mix(debugColor, pc.baseColor.rgb, 0.5);
    }

    // Make sure we output full opacity
    outColor = vec4(debugColor, 1.0);

    // Add a visible pattern to confirm shader is running
    float pattern = step(0.5, fract(fragTexCoord.x * 20.0)) * step(0.5, fract(fragTexCoord.y * 20.0));
    outColor.rgb = mix(outColor.rgb, vec3(1.0, 1.0, 0.0), pattern * 0.3);
}