#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding=1) uniform sampler2D colorMap;

layout(location=0) in vec2 inUV;
layout(location=1) in vec4 inRGBA;

layout(location=0) out vec4 outColor;

void main() {
    // float value = texture(colorMap, inUV).r;
    float alpha = 1.f;
    outColor = vec4(inRGBA.rgb, alpha);
    // outColor = vec4(inUV.x, inUV.y, 0, 1);
}
