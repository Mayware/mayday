#extension GL_EXT_structured_descriptor_heap : require
// https://wikis.khronos.org/opengl/Core_Language_(GLSL)#Extensions
// https://github.com/google/shaderc/tree/main/glslc#52-include
// https://docs.vulkan.org/glslext/latest/glslext/ext/GLSL_EXT_structured_descriptor_heap.html
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
    Resource resources[];
} resourceHeap;

layout(heap_offset = pushData.samplerHeapOffset) samplerheap SamplerHeap {
    sampler samplers[];
} samplerHeap;

