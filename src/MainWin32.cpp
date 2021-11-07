#pragma warning (disable: 4267)
#pragma warning (disable: 4996)

#include <Windows.h>
#include <cstdio>

#include "Logging.h"
#include "FileSystem.cpp"
#include "Vulkan.cpp"
#include <vulkan/vulkan_win32.h>

const int WIDTH = 800;
const int HEIGHT = 800;

struct State {
    // bool keyboard[VK_OEM_CLEAR] = {};
} state;

struct PipelineOptions pipelineOptions[] = {
    {
        .name = "text",
        .vertexShaderPath = "shaders/text.vert.spv",
        .fragmentShaderPath = "shaders/text.frag.spv",
        .clockwiseWinding = true,
        .cullBackFaces = false,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    }
};

LRESULT __stdcall
WindowProc(
    HWND    window,
    UINT    message,
    WPARAM  wParam,
    LPARAM  lParam
) {
    switch (message) {
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) PostQuitMessage(0);
            // else state.keyboard[(uint16_t)wParam] = true;
            break;
        // case WM_KEYUP:
            // state.keyboard[(uint16_t)wParam] = false;
            // break;
        default:
            break;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

int __stdcall
WinMain(
    HINSTANCE instance,
    HINSTANCE prevInstance,
    LPSTR commandLine,
    int showCommand
) {
    initLogging();
    INFO("Logging initialized.")

    // Create Window.
    WNDCLASSEX windowClassProperties = {};
    windowClassProperties.cbSize = sizeof(windowClassProperties);
    windowClassProperties.style = CS_HREDRAW | CS_VREDRAW;
    windowClassProperties.lpfnWndProc = (WNDPROC)WindowProc;
    windowClassProperties.hInstance = instance;
    windowClassProperties.lpszClassName = "MainWindowClass";
    ATOM windowClass = RegisterClassEx(&windowClassProperties);
    if (!windowClass) {
        FATAL("could not create window class")
    }
    HWND window = CreateWindowEx(
        0,
        "MainWindowClass",
        "Vk",
        WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        WIDTH,
        HEIGHT,
        nullptr,
        nullptr,
        instance,
        nullptr
    );
    if (window == nullptr) {
        FATAL("could not create window")
    }
    SetWindowPos(
        window,
        HWND_TOP,
        0,
        0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        SWP_FRAMECHANGED
    );
    ShowCursor(FALSE);

    // NOTE(jan): Create Vulkan instance..
    Vulkan vk;
    vk.extensions.emplace_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    createVKInstance(vk);

    // NOTE(jan): Get a surface for Vulkan.
    {
        VkSurfaceKHR surface;

        VkWin32SurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hinstance = instance;
        createInfo.hwnd = window;

        auto result = vkCreateWin32SurfaceKHR(
            vk.handle,
            &createInfo,
            nullptr,
            &surface
        );

        if (result != VK_SUCCESS) {
            throw runtime_error("could not create win32 surface");
        }
        vk.swap.surface = surface;
    }

    // Initialize the rest of Vulkan.
    initVK(vk);

    // Load shaders.
    {
        for (const PipelineOptions& options: pipelineOptions) {
            INFO("%s", options.name);
        }
    }

    // NOTE(jan): Main loop.
    bool done = false;
    while (!done) {
        // NOTE(jan): Pump WIN32 message queue.
        MSG msg;
        BOOL messageAvailable;
        do {
            messageAvailable = PeekMessage(
                &msg,
                (HWND)nullptr,
                0, 0,
                PM_REMOVE
            );
            TranslateMessage(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
            DispatchMessage(&msg);
        } while(messageAvailable);

        // NOTE(jan): Acquire swap image.
        uint32_t swapImageIndex = 0;
        auto result = vkAcquireNextImageKHR(
            vk.device,
            vk.swap.handle,
            std::numeric_limits<uint64_t>::max(),
            vk.swap.imageReady,
            VK_NULL_HANDLE,
            &swapImageIndex
        );
        if ((result == VK_SUBOPTIMAL_KHR) ||
            (result == VK_ERROR_OUT_OF_DATE_KHR)) {
            // TODO(jan): implement resize
            FATAL("could not acquire next image")
        } else if (result != VK_SUCCESS) {
            FATAL("could not acquire next image")
        }

        // NOTE(jan): Start recording commands.
        VkCommandBuffer cmds = {};
        createCommandBuffers(vk.device, vk.cmdPool, 1, &cmds);
        beginFrameCommandBuffer(cmds);

        // NOTE(jan): Clear colour / depth.
        VkClearValue colorClear;
        colorClear.color = {1.f, 0.f, 1.f, 1.f};
        VkClearValue depthClear;
        depthClear.depthStencil = {1.f, 0};
        VkClearValue clears[] = {colorClear, depthClear};

        // NOTE(jan): Render pass.
        VkRenderPassBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.clearValueCount = 2;
        beginInfo.pClearValues = clears;
        beginInfo.framebuffer = vk.swap.framebuffers[swapImageIndex];
        beginInfo.renderArea.extent = vk.swap.extent;
        beginInfo.renderArea.offset = {0, 0};
        beginInfo.renderPass = vk.renderPass;

        vkCmdBeginRenderPass(cmds, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(cmds);
        endCommandBuffer(cmds);

        // Present
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmds;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &vk.swap.imageReady;
        VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &vk.swap.cmdBufferDone;
        vkQueueSubmit(vk.queue, 1, &submitInfo, VK_NULL_HANDLE);
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &vk.swap.handle;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &vk.swap.cmdBufferDone;
        presentInfo.pImageIndices = &swapImageIndex;
        VKCHECK(vkQueuePresentKHR(vk.queue, &presentInfo))
        // PERF(jan): This is potentially slow.
        vkQueueWaitIdle(vk.queue);
    }

    return 0;
}
