#version 450

layout (triangles) in;
layout (triangle_strip, max_vertices = 18) out;

layout (location = 0) out vec2 frag_tex_coord;
layout (location = 1) flat out int face;

void main(void)
{	
	for ( int i = 0; i < 6; ++i ) {
		for ( int j = 0; j < 3; ++j ) {
			gl_Layer = i;
			gl_Position = vec4( gl_in[j].gl_Position.xy, 0.0, 1.0 );
            frag_tex_coord = gl_in[j].gl_Position.xy;
			face = i;
			EmitVertex();
		}
		EndPrimitive();
	}
}