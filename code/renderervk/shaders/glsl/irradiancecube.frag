// Generates an irradiance cube from an environment map using convolution

#version 450

layout (location = 0) in vec2 frag_tex_coord;
layout (location = 1) flat in int face;

layout (location = 0) out vec4 out_color;
layout (binding = 0) uniform samplerCube samplerEnv;

#define PI 3.1415926535897932384626433832795

const float deltaPhi = (2.0f * float(PI)) / 180.0f;
const float deltaTheta = (0.5f * float(PI)) / 64.0f;

vec3 irradiance( vec3 N ) {
	vec3 up = vec3(0.0, 1.0, 0.0);
	vec3 right = normalize(cross(up, N));
	up = cross(N, right);

	const float TWO_PI = PI * 2.0;
	const float HALF_PI = PI * 0.5;

	vec3 color = vec3(0.0);
	uint sampleCount = 0u;
	for (float phi = 0.0; phi < TWO_PI; phi += deltaPhi) {
		for (float theta = 0.0; theta < HALF_PI; theta += deltaTheta) {
			vec3 tempVec = cos(phi) * right + sin(phi) * up;
			vec3 sampleVector = cos(theta) * N + sin(theta) * tempVec;
			color += texture(samplerEnv, sampleVector).rgb * cos(theta) * sin(theta);
			sampleCount++;
		}
	}
	
	return PI * color / float( sampleCount );
}

void main()
{

	vec3 normal = normalize( vec3( -frag_tex_coord.x, -frag_tex_coord.y, -1.0 ) );

	if ( face == 0 )
		normal = normalize( vec3( 1.0, -frag_tex_coord.y, -frag_tex_coord.x ) );
	else if ( face == 1 )
		normal = normalize( vec3( -1.0, -frag_tex_coord.y, frag_tex_coord.x ) );
	else if ( face == 2 )
		normal = normalize( vec3( frag_tex_coord.x, 1.0, frag_tex_coord.y ) );
	else if ( face == 3 )
		normal = normalize( vec3( frag_tex_coord.x, -1.0, -frag_tex_coord.y ) );
	else if ( face == 4 )
		normal = normalize( vec3( frag_tex_coord.x, -frag_tex_coord.y, 1.0 ) ); 
		
	
	out_color = vec4( irradiance( normalize( normal ) ), 1.0 );
}
