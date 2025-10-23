#version 450

// Chromatic Aberration fragment shader
layout(binding = 0) uniform sampler2D colorTexture;

layout(push_constant) uniform ChromaticParams {
    float strength;
    vec3 shift;
} params;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = fragTexCoord;
    vec2 center = vec2(0.5);
    vec2 offset = (uv - center) * params.strength;
    
    // Sample each color channel with different offsets
    float r = texture(colorTexture, uv + offset * params.shift.r).r;
    float g = texture(colorTexture, uv + offset * params.shift.g).g;
    float b = texture(colorTexture, uv + offset * params.shift.b).b;
    
    outColor = vec4(r, g, b, 1.0);
}