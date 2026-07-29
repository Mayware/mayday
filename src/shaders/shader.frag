#version 460
layout(location = 0) in vec3 vertex_colour;
layout(location = 0) out vec4 fragment_colour;

void main() {
	fragment_colour = vec4(vertex_colour, 1.0);
}
