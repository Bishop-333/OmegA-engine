#version 450

// Depth of Field fragment shader
layout(binding = 0) uniform sampler2D colorTexture;
layout(binding = 1) uniform sampler2D depthTexture;

layout(push_constant) uniform DOFParams {
    float focusDistance;
    float focusRange;
    float nearBlur;
    float farBlur;
    float bokehSize;
    int bokehSamples;
} params;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Golden angle for better sampling distribution
const float GOLDEN_ANGLE = 2.39996323;

float linearizeDepth(float depth) {
    float n = 0.1; // near plane
    float f = 1000.0; // far plane
    return (2.0 * n) / (f + n - depth * (f - n));
}

vec3 bokehBlur(vec2 uv, float blurAmount) {
    vec3 color = vec3(0.0);
    float totalWeight = 0.0;
    
    for (int i = 0; i < params.bokehSamples; i++) {
        float angle = float(i) * GOLDEN_ANGLE;
        float radius = sqrt(float(i)) / sqrt(float(params.bokehSamples));
        radius *= blurAmount * params.bokehSize * 0.01;
        
        vec2 offset = vec2(cos(angle), sin(angle)) * radius;
        vec2 sampleUV = uv + offset;
        
        if (sampleUV.x >= 0.0 && sampleUV.x <= 1.0 && 
            sampleUV.y >= 0.0 && sampleUV.y <= 1.0) {
            vec3 sampleColor = texture(colorTexture, sampleUV).rgb;
            float weight = 1.0 - radius / (blurAmount * params.bokehSize * 0.01);
            color += sampleColor * weight;
            totalWeight += weight;
        }
    }
    
    return color / max(totalWeight, 0.001);
}

void main() {
    vec2 uv = fragTexCoord;
    float depth = texture(depthTexture, uv).r;
    float linearDepth = linearizeDepth(depth) * 1000.0; // Convert to world units
    
    // Calculate blur amount based on distance from focus point
    float focusStart = params.focusDistance - params.focusRange * 0.5;
    float focusEnd = params.focusDistance + params.focusRange * 0.5;
    
    float blurAmount = 0.0;
    if (linearDepth < focusStart) {
        // Near blur
        blurAmount = (focusStart - linearDepth) / focusStart;
        blurAmount = min(blurAmount * params.nearBlur, 1.0);
    } else if (linearDepth > focusEnd) {
        // Far blur
        blurAmount = (linearDepth - focusEnd) / (1000.0 - focusEnd);
        blurAmount = min(blurAmount * params.farBlur, 1.0);
    }
    
    if (blurAmount > 0.01) {
        outColor = vec4(bokehBlur(uv, blurAmount), 1.0);
    } else {
        outColor = texture(colorTexture, uv);
    }
}