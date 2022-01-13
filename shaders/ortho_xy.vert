#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "uniforms.glsl"

layout(location=0) in vec2 inXY;

void main() {
    gl_Position = uniforms.ortho * vec4(inXY, 0.f, 1.f);
}
