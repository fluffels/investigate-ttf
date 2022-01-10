#include "Render.h"
#include "State.h"

#include "MathLib.cpp"

#include "Text.cpp"

void renderInit(Vulkan& vk, Uniforms& uniforms) {
    auto fov = toRadians(45);
    auto height = vk.swap.extent.height;
    auto width = vk.swap.extent.width;
    auto nearZ = .1f;
    auto farZ = 10.f;
    matrixProjection(width, height, fov, farZ, nearZ, uniforms.proj);

    eventPositionReset(uniforms);

    quaternionInit(uniforms.rotation);

    initText(vk);
}

void renderFrame(Vulkan& vk, char* debugString) {
    recordTextCommandBuffers(vk, textCmds, debugString);
    vector<vector<VkCommandBuffer>> cmdss;
    cmdss.push_back(meshCmds);
    cmdss.push_back(textCmds);
    present(vk, cmdss);
    resetTextCommandBuffers(vk, textCmds);
}
