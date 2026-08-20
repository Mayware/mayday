#version 460
#include "../includes/header.glsl"

// In and out have different location namespaces, location means solely just
// slots for linkage, there's also binding / set but that is for descriptor layouts.
layout(location = 0) out uint instanceIndex;
layout(location = 1) out vec2 uv;

// NDC is -1, -1 to 1,1
vec2 positions[] = vec2[](
        // CLockwise, top-left triangle
        vec2(-1., -1.),
        vec2(1., -1.),
        vec2(-1., 1.),
        // Bottom right triangle, clockwise from where we left off
        vec2(-1., 1.),
        vec2(1., -1.),
        vec2(1., 1.)
    );

// Sampling range is 0,0 to 1,1
vec2 uvs[] = vec2[](
        vec2(0, 0),
        vec2(1, 0),
        vec2(0, 1),

        vec2(0, 1),
        vec2(1, 0),
        vec2(1, 1)
    );

void main() {
    // The fragment shader doesn't have access to gl_InstanceIndex
    instanceIndex = gl_InstanceIndex;
    uint vertexIndex = gl_VertexIndex % 6;

    uv = uvs[vertexIndex];
    vec2 position = positions[vertexIndex];
    // W component is explained in the render.cpp matching code
    gl_Position = vec4(position.x * RESOURCE.width + RESOURCE.x, position.y * RESOURCE.height + RESOURCE.y, 0.0, 1.0);
}
