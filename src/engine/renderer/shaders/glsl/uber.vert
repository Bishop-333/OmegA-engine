#version 450

// ---------- Vertex Inputs (match vk_uber.c) ----------
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;   // base / diffuse
layout(location = 2) in vec2 inTexCoord2;  // lightmap
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inColor;      // UNORM8 -> vec4
layout(location = 5) in vec4 inTangent;    // xyz = tangent, w = handedness

// ---------- UBO: set=0,binding=0 ----------
layout(set = 0, binding = 0) uniform TransformUBO {
    mat4 mvpMatrix;
    mat4 modelMatrix;
    mat4 normalMatrix; // inverse-transpose(model) in 4x4 for alignment; we use mat3() slice
} transforms;

// ---------- Feature bits (mirror vk_shader.h) ----------
const uint FEAT_VERTEX_COLOR = 0x00000002u;
const uint FEAT_ALPHA_TEST   = 0x00000004u;
const uint FEAT_FOG          = 0x00000100u;
const uint FEAT_NORMAL_MAP   = 0x00000200u;
const uint FEAT_SPECULAR_MAP = 0x00000400u;
const uint FEAT_ENV_MAP      = 0x00008000u;
const uint FEAT_DIFFUSE_MAP  = 0x00010000u;
const uint FEAT_BLOOM        = 0x00020000u;
const uint FEAT_Y_FLIP_POS   = 0x00040000u;
const uint FEAT_PBR_SHADING  = 0x00080000u;

// ---------- Push Constants ----------
layout(push_constant) uniform PC {
    uint features;        // bitmask of FEAT_*
    uint textureMask;     // texture usage mask (TEXTURE_FLAG_*)
    uint transformIndex;  // dynamic UBO slice index
    uint _pcPad0;

    vec4 baseColor;       // fallback/override color
    vec4 sunColor;        // directional light (rgb) + intensity (w)
    vec4 fogColor;
    vec4 cameraPos_time;  // xyz = camera world pos, w = time
    vec4 materialParams;  // x=metallic, y=roughness, z=ao, w=emissive
    vec4 sunDirection;    // xyz = direction, w = intensity multiplier

    vec2 fogParams;       // density/start, end
    float alphaTestValue;
    float _pcPad1;
} pc;

// ---------- Varyings to fragment ----------
layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec2 vTex0;
layout(location = 2) out vec2 vTex1;
layout(location = 3) out vec3 vNormalWS;
layout(location = 4) out vec4 vColor;
layout(location = 5) out vec3 vTangentWS;
layout(location = 6) out vec3 vBitangentWS;
layout(location = 7) out vec3 vViewDirWS;

void main()
{
    // Positions
    vec4 worldPos = transforms.modelMatrix * vec4(inPosition, 1.0);
    gl_Position = transforms.mvpMatrix * vec4(inPosition, 1.0);

    // Optional NDC Y flip if projection was built GL-style
    if ((pc.features & FEAT_Y_FLIP_POS) != 0u) {
        gl_Position.y = -gl_Position.y;
    }

    // Pass-through texcoords
    vTex0 = inTexCoord;
    vTex1 = inTexCoord2;

    // Transform normal & tangent into world space consistently
    mat3 N = mat3(transforms.normalMatrix);
    vec3 n = normalize(N * inNormal);
    vec3 t = normalize(N * inTangent.xyz);
    vec3 b = normalize(cross(n, t)) * inTangent.w; // recompute after both are transformed

    vNormalWS = n;
    vTangentWS = t;
    vBitangentWS = b;

    // Vertex color or baseColor
    if ((pc.features & FEAT_VERTEX_COLOR) != 0u) {
        vColor = inColor * pc.baseColor;
    } else {
        vColor = pc.baseColor;
    }

    // View direction in world space
    vec3 camPos = pc.cameraPos_time.xyz;
    vec3 V = camPos - worldPos.xyz;
    vViewDirWS = normalize(V);

    // Export world pos
    vWorldPos = worldPos.xyz;
}
