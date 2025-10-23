#version 450

// God Rays (Light Shafts) fragment shader
layout(binding = 0) uniform sampler2D colorTexture;
layout(binding = 1) uniform sampler2D depthTexture;

layout(push_constant) uniform GodRaysParams {
    vec2 lightPos;      // Light position in screen space
    float density;
    float weight;
    float decay;
    float exposure;
    int samples;
} params;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = fragTexCoord;
    vec4 originalColor = texture(colorTexture, uv);
    
    // Calculate ray from pixel to light source
    vec2 deltaTexCoord = (uv - params.lightPos) / float(params.samples) * params.density;
    vec2 currentUV = uv;
    
    vec3 accumColor = vec3(0.0);
    float illuminationDecay = 1.0;
    
    // Raymarch towards light source
    for (int i = 0; i < params.samples; i++) {
        currentUV -= deltaTexCoord;
        
        // Sample color and depth
        vec3 sampleColor = texture(colorTexture, currentUV).rgb;
        float sampleDepth = texture(depthTexture, currentUV).r;
        
        // Use depth to occlude rays
        float occlusion = (sampleDepth < 0.99) ? 0.0 : 1.0;
        sampleColor *= illuminationDecay * params.weight * occlusion;
        
        accumColor += sampleColor;
        illuminationDecay *= params.decay;
    }
    
    // Apply exposure and combine with original
    accumColor *= params.exposure;
    outColor = vec4(originalColor.rgb + accumColor, originalColor.a);
}