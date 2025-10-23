#version 450

// Vignette fragment shader
layout(binding = 0) uniform sampler2D colorTexture;

layout(push_constant) uniform VignetteParams {
    float intensity;
    float radius;
    float softness;
} params;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = fragTexCoord;
    vec4 color = texture(colorTexture, uv);
    
    // Calculate distance from center
    vec2 center = vec2(0.5);
    float dist = distance(uv, center);
    
    // Apply vignette
    float vignette = smoothstep(params.radius, params.radius - params.softness, dist);
    vignette = mix(1.0, vignette, params.intensity);
    
    outColor = vec4(color.rgb * vignette, color.a);
}