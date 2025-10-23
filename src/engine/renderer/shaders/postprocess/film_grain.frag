#version 450

// Film Grain fragment shader
layout(binding = 0) uniform sampler2D colorTexture;

layout(push_constant) uniform FilmGrainParams {
    float intensity;
    float grainSize;
    float time;
} params;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Simple hash function for noise
float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    vec2 u = f * f * (3.0 - 2.0 * f);
    
    return mix(a, b, u.x) +
           (c - a) * u.y * (1.0 - u.x) +
           (d - b) * u.x * u.y;
}

void main() {
    vec2 uv = fragTexCoord;
    vec4 color = texture(colorTexture, uv);
    
    // Generate film grain noise
    vec2 noiseCoord = uv * params.grainSize + vec2(params.time * 0.1);
    float grain = noise(noiseCoord);
    grain = (grain - 0.5) * params.intensity;
    
    // Apply grain to color
    vec3 finalColor = color.rgb + vec3(grain);
    
    outColor = vec4(finalColor, color.a);
}