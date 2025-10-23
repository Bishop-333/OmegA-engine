#version 450

// Motion Blur fragment shader
layout(binding = 0) uniform sampler2D colorTexture;
layout(binding = 1) uniform sampler2D velocityTexture;

layout(push_constant) uniform MotionBlurParams {
    float velocityScale;
    int samples;
    float maxBlur;
} params;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = fragTexCoord;
    vec2 velocity = texture(velocityTexture, uv).xy;
    
    // Scale velocity
    velocity *= params.velocityScale;
    
    // Clamp to maximum blur
    float velocityMag = length(velocity);
    if (velocityMag > params.maxBlur) {
        velocity = normalize(velocity) * params.maxBlur;
    }
    
    // Sample along velocity vector
    vec4 color = vec4(0.0);
    float totalWeight = 0.0;
    
    for (int i = 0; i < params.samples; i++) {
        float t = float(i) / float(params.samples - 1) - 0.5;
        vec2 sampleUV = uv + velocity * t;
        
        if (sampleUV.x >= 0.0 && sampleUV.x <= 1.0 && 
            sampleUV.y >= 0.0 && sampleUV.y <= 1.0) {
            float weight = 1.0 - abs(t * 2.0);
            color += texture(colorTexture, sampleUV) * weight;
            totalWeight += weight;
        }
    }
    
    outColor = color / max(totalWeight, 0.001);
}