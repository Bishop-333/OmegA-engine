#version 450

void main() 
{
	vec2 position = vec2(2.0 * float(gl_VertexIndex & 2) - 1.0, 4.0 * float(gl_VertexIndex & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}