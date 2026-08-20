#version 460
#include "../includes/header.glsl"

// https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html#input-variables
// Basically, the "qualifiers" of flat, smooth etc only matter on the fragment shader (keyword: completely missing)
layout(location = 0) flat in uint instanceIndex;
// The equation for noperspective: https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf#page=501
layout(location = 1) noperspective in vec2 uv;
// https://docs.vulkan.org/spec/latest/chapters/interfaces.html#interfaces-fragmentoutput
// frag colour means the same: https://docs.vulkan.org/glsl/latest/chapters/spirvmappings.html#_gl_fragcolor
// Writes to the 0th colour attachment
layout(location = 0) out vec4 colour;

void main() {
    colour = texture(sampler2D(RESOURCE.texture, SAMPLER), uv);
}
