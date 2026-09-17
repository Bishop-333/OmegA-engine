#version 450

layout(set = 0, binding = 0) uniform sampler2D texture0;
layout(set = 1, binding = 0) uniform sampler2D texture_depth;

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

layout(constant_id = 0) const float gamma = 1.0;
layout(constant_id = 1) const float obScale = 2.0;
layout(constant_id = 2) const float greyscale = 0.0;
layout(constant_id = 3) const int enable_ssao = 0;
layout(constant_id = 4) const float ssao_radius = 24.0;
layout(constant_id = 5) const float ssao_strength = 1.2;
layout(constant_id = 6) const float ssao_bias = 0.08;
layout(constant_id = 7) const int ditherMode = 0; // 0 - disabled, 1 - ordered
layout(constant_id = 8) const int depth_r = 255;
layout(constant_id = 9) const int depth_g = 255;
layout(constant_id = 10) const int depth_b = 255;

const vec3 sRGB = { 0.2126, 0.7152, 0.0722 };

const int bayerSize = 8;
const float bayerMatrix[bayerSize * bayerSize] = {
	0,  32, 8,  40, 2,  34, 10, 42,
	48, 16, 56, 24, 50, 18, 58, 26,
	12, 44, 4,  36, 14, 46, 6,  38,
	60, 28, 52, 20, 62, 30, 54, 22,
	3,  35, 11, 43, 1,  33, 9,  41,
	51, 19, 59, 27, 49, 17, 57, 25,
	15, 47, 7,  39, 13, 45, 5,  37,
	63, 31, 55, 23, 61, 29, 53, 21
};

float threshold() {
	ivec2 coordDenormalized = ivec2(gl_FragCoord.xy);
	ivec2 bayerCoord = coordDenormalized % bayerSize;
	float bayerSample = bayerMatrix[bayerCoord.x + bayerCoord.y * bayerSize];
	float threshold = (bayerSample + 0.5) / float(bayerSize * bayerSize);
	return threshold;
}

vec3 dither(vec3 color) {
	ivec3 depth = ivec3(depth_r, depth_g, depth_b);
	vec3 cDenormalized = color * depth;
	vec3 cLow = floor(cDenormalized);
	vec3 cFractional = cDenormalized - cLow;
	vec3 cDithered = cLow + step(threshold(), cFractional);
	return cDithered / depth;
}

const int SSAO_SAMPLES = 12;
const vec2 ssao_samples[12] = vec2[](
	vec2( 0.25,  0.00), vec2( 0.00,  0.25), vec2(-0.25,  0.00), vec2( 0.00, -0.25),
	vec2( 0.42,  0.42), vec2(-0.42,  0.42), vec2(-0.42, -0.42), vec2( 0.42, -0.42),
	vec2( 0.92,  0.38), vec2(-0.38,  0.92), vec2(-0.92, -0.38), vec2( 0.38, -0.92)
);

// Fonction de hachage rapide pour générer un bruit pseudo-aléatoire
float hash(vec2 p) {
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float computeSSAO(vec2 uv) {
	float d = texture(texture_depth, uv).r;
	if (d <= 0.0001) return 1.0;

	float z = 4.0 / max(d, 0.00001);
	vec3 pos = vec3((uv * 2.0 - 1.0) * z, z);

	vec3 dX = dFdx(pos);
	vec3 dY = dFdy(pos);
	vec3 normal = cross(dY, dX);
	float nLen = length(normal);
	if (nLen < 0.0001) return 1.0;
	normal /= nLen;
	if (normal.z > 0.0) normal = -normal;

	vec2 texSize = vec2(textureSize(texture_depth, 0));
	float radiusPixels = clamp((ssao_radius * texSize.y * 0.75) / max(z, 1.0), 3.0, 64.0);
	vec2 radiusUV = radiusPixels / texSize;

	// Génération d'une rotation aléatoire unique pour chaque pixel
	float randomAngle = hash(gl_FragCoord.xy) * 3.14159265 * 2.0;
	float s = sin(randomAngle);
	float c = cos(randomAngle);
	mat2 rot = mat2(c, -s, s, c);

	float occlusion = 0.0;
	float validSamples = 0.0;

	for (int i = 0; i < SSAO_SAMPLES; i++) {
		// On fait tourner l'échantillon fixe
		vec2 offset = rot * ssao_samples[i];
		vec2 sampleUV = clamp(uv + offset * radiusUV, vec2(0.001), vec2(0.999));
		
		float sD = texture(texture_depth, sampleUV).r;
		if (sD <= 0.0001) continue;

		float sZ = 4.0 / max(sD, 0.00001);
		vec3 sPos = vec3((sampleUV * 2.0 - 1.0) * sZ, sZ);
		vec3 diff = sPos - pos;
		float dist = length(diff);

		float NdotV = dot(normal, diff / max(dist, 0.001));
		if (NdotV > ssao_bias) {
			float rangeCheck = smoothstep(0.0, 1.0, (ssao_radius * 2.0) / (dist + 0.001));
			occlusion += (NdotV - ssao_bias) * rangeCheck;
		}
		validSamples += 1.0;
	}

	if (validSamples < 1.0) return 1.0;
	float factor = (occlusion / validSamples) * ssao_strength;
	return clamp(1.0 - factor, 0.0, 1.0);
}

void main() {
	vec3 base = texture(texture0, frag_tex_coord).rgb;

	if ( enable_ssao == 2 )
	{
		float ao = computeSSAO(frag_tex_coord);
		out_color = vec4(vec3(ao), 1.0);
		return;
	}
	else if ( enable_ssao == 1 )
	{
		float ao = computeSSAO(frag_tex_coord);
		
		// Masque de luminance : on protège les pixels très clairs/émissifs
		// Si la luminosité du pixel s'approche de 1.0, on annule l'effet du SSAO (ao = 1.0)
		float luma = dot(base, sRGB);
		float mask = smoothstep(0.5, 0.9, luma); 
		ao = mix(ao, 1.0, mask);
		
		base *= ao;
	}

	if ( greyscale == 1 )
	{
		base = vec3(dot(base, sRGB));
	}
	else if ( greyscale != 0 )
	{
		vec3 luma = vec3(dot(base, sRGB));
		base = mix(base, luma, greyscale);
	}

	if ( gamma != 1.0 )
	{
		out_color = vec4(pow(base, vec3(gamma)) * obScale, 1);
	}
	else
	{
		out_color = vec4(base * obScale, 1);
	}

	if ( ditherMode == 1 ) {
		out_color.rgb = dither(out_color.rgb);
	}
}
