#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location=0) in vec2 inUV;

layout(location=0) out vec4 outColor;

void main() {
    const float f = inUV.r * inUV.r - inUV.g;
    if (f >= 0) discard;
    outColor = vec4(inUV.r, inUV.g, 0, 1);
}
