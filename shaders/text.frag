#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding=1) uniform sampler2D colorMap;

layout(location=0) in vec2 inST;

layout(location=0) out vec4 outColor;

void main() {
    float value = texture(colorMap, inST).r;
    outColor = vec4(value);
}
