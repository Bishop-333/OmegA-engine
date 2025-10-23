#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec2 vTex0;
layout(location = 2) in vec2 vTex1;
layout(location = 3) in vec3 vNormalWS;
layout(location = 4) in vec4 vColor;
layout(location = 5) in vec3 vTangentWS;
layout(location = 6) in vec3 vBitangentWS;
layout(location = 7) in vec3 vViewDirWS;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform TransformUBO {
    mat4 mvpMatrix;
    mat4 modelMatrix;
    mat4 normalMatrix;
} transforms;

layout(set = 0, binding = 1) uniform sampler2D uDiffuse;
layout(set = 0, binding = 2) uniform sampler2D uLightmap;
layout(set = 0, binding = 3) uniform sampler2D uNormalMap;
layout(set = 0, binding = 4) uniform sampler2D uSpecularMap;
layout(set = 0, binding = 5) uniform samplerCube uEnvMap;

const uint FEAT_VERTEX_COLOR  = 0x00000002u;
const uint FEAT_ALPHA_TEST    = 0x00000004u;
const uint FEAT_FOG           = 0x00000100u;
const uint FEAT_NORMAL_MAP    = 0x00000200u;
const uint FEAT_SPECULAR_MAP  = 0x00000400u;
const uint FEAT_ENV_MAP       = 0x00008000u;
const uint FEAT_DIFFUSE_MAP   = 0x00010000u;
const uint FEAT_BLOOM         = 0x00020000u;
const uint FEAT_PBR_SHADING   = 0x00080000u;


const uint TEXTURE_FLAG_DIFFUSE     = 0x00000001u;
const uint TEXTURE_FLAG_NORMAL      = 0x00000002u;
const uint TEXTURE_FLAG_SPECULAR    = 0x00000004u;
const uint TEXTURE_FLAG_GLOW        = 0x00000008u;
const uint TEXTURE_FLAG_DETAIL      = 0x00000010u;
const uint TEXTURE_FLAG_ENVIRONMENT = 0x00000020u;

const float PI = 3.14159265359;

layout(push_constant) uniform PC {
    uint features;
    uint textureMask;
    uint transformIndex;
    uint _pcPad0;

    vec4 baseColor;
    vec4 sunColor;
    vec4 fogColor;
    vec4 cameraPos_time;
    vec4 materialParams;
    vec4 sunDirection;

    vec2 fogParams;
    float alphaTestValue;
    float _pcPad1;
} pc;

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-4);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = geometrySchlickGGX(NdotV, roughness);
    float ggx2 = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

void main() {
    bool useDiffuse  = ((pc.features & FEAT_DIFFUSE_MAP) != 0u) && ((pc.textureMask & TEXTURE_FLAG_DIFFUSE) != 0u);
    bool useNormal   = ((pc.features & FEAT_NORMAL_MAP) != 0u) && ((pc.textureMask & TEXTURE_FLAG_NORMAL) != 0u);
    bool useSpecular = ((pc.features & FEAT_SPECULAR_MAP) != 0u) && ((pc.textureMask & TEXTURE_FLAG_SPECULAR) != 0u);
    bool useEnv      = ((pc.features & FEAT_ENV_MAP) != 0u) && ((pc.textureMask & TEXTURE_FLAG_ENVIRONMENT) != 0u);

    vec4 albedoSample = useDiffuse ? texture(uDiffuse, vTex0) : vec4(1.0);
    vec3 albedo = vColor.rgb * albedoSample.rgb;
    float alpha = vColor.a * albedoSample.a;

    if ((pc.features & FEAT_ALPHA_TEST) != 0u && alpha < pc.alphaTestValue) {
        discard;
    }

    vec3 N = normalize(vNormalWS);
    if (useNormal) {
        vec3 nTS = texture(uNormalMap, vTex0).xyz * 2.0 - 1.0;
        mat3 TBN = mat3(normalize(vTangentWS), normalize(vBitangentWS), N);
        N = normalize(TBN * nTS);
    }

    vec3 V = normalize(vViewDirWS);
    if (length(V) < 1e-4) {
        V = normalize(pc.cameraPos_time.xyz - vWorldPos);
    }

    vec3 L = normalize(pc.sunDirection.xyz);
    if (length(L) < 1e-4) {
        L = vec3(0.0, 0.0, -1.0);
    }

    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    float metallic = clamp(pc.materialParams.x, 0.0, 1.0);
    float roughness = clamp(pc.materialParams.y, 0.04, 1.0);
    float ao = max(pc.materialParams.z, 0.0);
    float emissiveStrength = max(pc.materialParams.w, 0.0);

    if (useSpecular) {
        vec3 specSample = texture(uSpecularMap, vTex0).rgb;
        metallic = clamp(specSample.r, 0.0, 1.0);
        roughness = clamp(specSample.g, 0.04, 1.0);
        ao *= specSample.b;
    }

    vec3 lightColor = pc.sunColor.rgb * pc.sunColor.w;
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 numerator = D * G * F;
    float denominator = max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;
    vec3 direct = (diffuse + specular) * lightColor * NdotL;

    vec3 ambientSample = vec3(0.03);
    if (useEnv) {
        vec3 R = reflect(-V, N);
        ambientSample = mix(ambientSample, texture(uEnvMap, R).rgb, 0.8);
    }

    vec3 F_ibl = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 diffuseIBL = ambientSample * albedo;
    vec3 specIBL = ambientSample * F_ibl;
    vec3 ambient = (diffuseIBL * (1.0 - metallic) + specIBL) * ao;

    ambient += emissiveStrength * albedo;

    vec3 color = ambient + direct;
    color = max(color, vec3(0.0));

    if ((pc.features & FEAT_BLOOM) != 0u) {
        float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
        if (brightness > 1.0) {
            color *= 1.1;
        }
    }

    if ((pc.features & FEAT_FOG) != 0u) {
        float dist = length(vWorldPos - pc.cameraPos_time.xyz);
        float fogFactor = 1.0;
        if (pc.fogParams.y > pc.fogParams.x) {
            float start = pc.fogParams.x;
            float endv = pc.fogParams.y;
            fogFactor = clamp((endv - dist) / max(endv - start, 1e-3), 0.0, 1.0);
        } else if (pc.fogParams.x > 0.0) {
            float density = pc.fogParams.x;
            fogFactor = exp(-pow(density * dist, 2.0));
        }
        color = mix(pc.fogColor.rgb, color, fogFactor);
        alpha = mix(1.0, alpha, fogFactor);
    }

    outColor = vec4(color, alpha);
}
