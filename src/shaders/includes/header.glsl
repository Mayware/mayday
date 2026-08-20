// https://docs.vulkan.org/glslext/latest/glslext/ext/GLSL_EXT_structured_descriptor_heap.html
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_structured_descriptor_heap : require
// gl_InstanceIndex varies across our single draw call. To use a non-uniform (ie. varying) index into an array,
// we need to use this extension. eg. array[nonuniformEXT(gl_InstanceIndex)]
// #extension GL_EXT_nonuniform_qualifier : require
// It's actually no longer needed: https://github.com/KhronosGroup/glslang/blob/main/CHANGES.md#1640-2026-07-14
// https://wikis.khronos.org/opengl/Core_Language_(GLSL)#Extensions
// https://github.com/google/shaderc/tree/main/glslc#52-include
// https://wikis.khronos.org/opengl/Interface_Block_(GLSL)
// https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html

#define RESOURCE resourceHeap.resources[instanceIndex]
#define SAMPLER samplerHeap.samplers[RESOURCE.samplerIndex]

layout(push_constant) uniform PushData {
    uint resourceHeapOffset;
    uint samplerHeapOffset;
} pushData;

struct Resource {
    float x;
    float y;
    float width;
    float height;
    uint samplerIndex;
    texture2D texture;
};

layout(heap_offset = pushData.resourceHeapOffset) resourceheap ResourceHeap {
    /* The GL_EXT_structured_descriptor_heap spec says:
     * Except for the last declared member of a shader storage, resource
     * heap, or sampler heap block (see section “Interface Blocks” and “Heap
     * Blocks”), or a descriptor heap declaration (without set and binding
     * qualifiers), the size of an array must be declared (explicitly sized)
     * before it is indexed with anything other than a constant integral
     * expression.
     * Which should mean that I should not need the 1024 on here, but yet I do
     * lest i get: error: '[' :  array must be redeclared with a size before being indexed with a variable.
     * I think this is an upstream compiler bug, but we only have 1024 max textures anyway, so this is fine
    */
    Resource resources[1024];
} resourceHeap;

layout(heap_offset = pushData.samplerHeapOffset) samplerheap SamplerHeap {
    sampler samplers[1024];
} samplerHeap;

