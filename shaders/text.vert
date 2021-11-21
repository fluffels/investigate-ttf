#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "uniforms.glsl"

layout(location=0) in vec2 inXY;
layout(location=1) in vec2 inUV;

layout(location=0) out vec2 outUV;

void main() {
    // gl_Position = uniforms.ortho * vec4(inXY, 0.f, 1.f);
    gl_Position = vec4(inXY, 0.f, 1.f);
    outUV = inUV;
}
