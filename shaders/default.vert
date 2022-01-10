#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "uniforms.glsl"
#include "quaternion.glsl"

layout(push_constant) uniform PushConstants {
    vec3 color;
} pushConstants;

layout(location=0) in vec3 inPosition;

layout(location=0) out vec3 outColor;

void main() {
    vec4 p = vec4(inPosition, 1.f);
    p -= uniforms.eye;
    p = rotate_vertex_position(uniforms.rotation, p);
    gl_Position = uniforms.proj * p;
    outColor = pushConstants.color;
}
