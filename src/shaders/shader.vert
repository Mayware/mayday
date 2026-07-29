#version 460
layout(location = 0) out vec3 vertex_colour;

const vec2 positions[3] = vec2[](
	vec2( 0.0, -0.5),
	vec2( 0.5,  0.5),
	vec2(-0.5,  0.5)
);

const vec3 colours[3] = vec3[](
	vec3(1.0, 0.0, 0.0),
	vec3(0.0, 1.0, 0.0),
	vec3(0.0, 0.0, 1.0)
);

void main() {
    // gl_VertexIndex is a global, that is set to whatever position we're currently on
	gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
	vertex_colour = colours[gl_VertexIndex];
}
