#include "Vulkan.h"
#include "vulkan/vulkan_core.h"
#pragma warning (disable: 4267)
#pragma warning (disable: 4996)

#include <Windows.h>
#include <cstdio>
#include <map>

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb/stb_rect_pack.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#include "MathLib.h"
#include "Logging.h"
#include "FileSystem.cpp"
#include "Vulkan.cpp"
#include <vulkan/vulkan_win32.h>

using std::map;

const int WIDTH = 800;
const int HEIGHT = 800;

enum KEYBOARD_KEYS {
    KEYBOARD_KEY_NONE,
    KEYBOARD_KEY_FORWARD,
    KEYBOARD_KEY_BACKWARD,
    KEYBOARD_KEY_LEFT,
    KEYBOARD_KEY_RIGHT,
    KEYBOARD_KEY_RESET,
    KEYBOARD_KEY_QUIT,
    KEYBOARD_KEY_COUNT
};

// ************************************************************
// * MATH: Definitions for geometric mathematical primitives. *
// ************************************************************

struct AABox {
    f32 x0;
    f32 x1;
    f32 y0;
    f32 y1;
};

// ******************************************************************************************
// * RESOURCE: Definitions for rendering resources (meshes, fonts, textures, pipelines &c). *
// ******************************************************************************************

struct Input {
    bool keyboard_keys[KEYBOARD_KEY_COUNT];
};

struct FontInfo {
    const char* name;
    const char* path;
};

struct FontInfo fontInfo[] = {
    {
        .name = "default",
        .path = "./fonts/AzeretMono-Medium.ttf",
    },
};

struct Font {
    FontInfo info;

    vector<u32> codepointsToLoad;
    vector<u32> failedCodepoints;
    vector<char> ttfFileContents;
    map<u32, stbtt_packedchar> dataForCodepoint;
};

struct MeshInfo {
    const char* name;
};

struct Mesh {
    MeshInfo info;

    vector<f32> vertices;
    vector<u32> indices;
};

enum ResourceType {
    RESOURCE_TYPE_NONE,
    RESOURCE_TYPE_FONT,
    RESOURCE_TYPE_COUNT,
};

struct UniformInfo {
    const char* name;
    ResourceType resourceType;
    const char* resourceName;
};

struct BrushInfo {
    const char* name;
    const char* meshName;
    const char* pipelineName;
    vector<UniformInfo> uniforms;
};

struct Brush {
    BrushInfo info;
};

MeshInfo meshInfo[] = {
    {
        .name = "text",
    },
};

BrushInfo brushInfo[] = {
    {
        .name = "text",
        .meshName = "text",
        .pipelineName = "text",
        .uniforms = {
            {
                .name = "glyphs",
                .resourceType = RESOURCE_TYPE_FONT,
                .resourceName = "default"
            },
        },
    },
};

PipelineInfo pipelineInfo[] = {
    {
        .name = "text",
        .vertexShaderPath = "shaders/text.vert.spv",
        .fragmentShaderPath = "shaders/text.frag.spv",
        .clockwiseWinding = true,
        .cullBackFaces = false,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    }
};

struct Renderer {
    map<const char*, Font> fonts;
    map<const char*, Mesh> meshes;
    map<const char*, VulkanPipeline> pipelines;
    map<const char*, Brush> brushes;
};

#define RENDERER_GET(var, type, name) \
    if (renderer.type.contains(name) == false) { \
        FATAL("%s contains no entry named '%s'", #type, name); \
    } \
    auto& var = renderer.type.at(name)

#define RENDERER_PUT(var, type, name) \
    if (renderer.type.contains(#name) == true) { \
        FATAL("%s already contains an entry named '%s'", #type, name); \
    } \
    renderer.type.insert({ name, var })

// ***********************************************
// * FRAME: Everything required to draw a frame. *
// ***********************************************

void pushAABox(Mesh& mesh, AABox& box) {
    AABox tex = {
        .x0 = 0,
        .x1 = 1,
        .y0 = 0,
        .y1 = 1
    };

    // NOTE(jan): Assuming that x grows rightward, and y grows downward.
    //            Number vertices of box clockwise starting at the top
    //            left like v0, v1, v2, and v3.
    // NOTE(jan): This is not implemented as triangle strips because boxes are
    //            mostly disjoint.

    // NOTE(jan): Top-left, v0.
    mesh.vertices.push_back(box.x0);
    mesh.vertices.push_back(box.y0);
    mesh.vertices.push_back(tex.x0);
    mesh.vertices.push_back(tex.y0);
    // NOTE(jan): Top-right, v1.
    mesh.vertices.push_back(box.x1);
    mesh.vertices.push_back(box.y0);
    mesh.vertices.push_back(tex.x0);
    mesh.vertices.push_back(tex.y0);
    // NOTE(jan): Bottom-right, v2.
    mesh.vertices.push_back(box.x1);
    mesh.vertices.push_back(box.y1);
    mesh.vertices.push_back(tex.x0);
    mesh.vertices.push_back(tex.y0);
    // NOTE(jan): Bottom-left, v3.
    mesh.vertices.push_back(box.x0);
    mesh.vertices.push_back(box.y1);
    mesh.vertices.push_back(tex.x0);
    mesh.vertices.push_back(tex.y0);

    // NOTE(jan): Top-right triangle.
    mesh.indices.push_back(0);
    mesh.indices.push_back(1);
    mesh.indices.push_back(2);

    // NOTE(jan): Bottom-left triangle.
    mesh.indices.push_back(0);
    mesh.indices.push_back(2);
    mesh.indices.push_back(3);
}

void doFrame(Vulkan& vk, Renderer& renderer, Input& input) {
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
        // TODO(jan): Implement resize.
        FATAL("could not acquire next image")
    } else if (result != VK_SUCCESS) {
        FATAL("could not acquire next image")
    }

    for (auto& pair: renderer.meshes) {
        Mesh& mesh = pair.second;
        mesh.indices.clear();
        mesh.vertices.clear();
    }

    RENDERER_GET(text, meshes, "text");

    AABox testBox = {
        .x0 = -0.5f,
        .x1 =  0.5f,
        .y0 = -0.5f,
        .y1 =  0.5f,
    };
    pushAABox(text, testBox);

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

    for (auto& kv: renderer.brushes) {
        Brush& brush = kv.second;

        RENDERER_GET(pipeline, pipelines, brush.info.pipelineName);
        vkCmdBindPipeline(
            cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle
        );
        vkCmdBindDescriptorSets(
            cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout,
            0, 1, &pipeline.descriptorSet,
            0, nullptr
        );

        RENDERER_GET(mesh, meshes, brush.info.meshName);
        VulkanMesh vkMesh = {};
        uploadMesh(
            vk,
            mesh.vertices.data(), sizeof(mesh.vertices[0]) * mesh.vertices.size(),
            mesh.indices.data(), sizeof(mesh.indices[0]) * mesh.indices.size(),
            vkMesh
        );
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmds, 0, 1, &vkMesh.vBuff.handle, offsets);
        vkCmdBindIndexBuffer(cmds, vkMesh.iBuff.handle, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmds, mesh.indices.size(), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmds);
    endCommandBuffer(cmds);

    // Submit.
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

    // Present.
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

// ************************************************************
// * INIT: Everything required to set up Vulkan pipelines &c. *
// ************************************************************

void init(Vulkan& vk, Renderer& renderer) {
    for (const FontInfo& info: fontInfo) {
        INFO("Loading font '%s'...", info.name);

        Font font = {
            .info = info,
            .ttfFileContents = readFile(info.path),
        };

        RENDERER_PUT(font, fonts, info.name);
    }

    for (const MeshInfo& info: meshInfo) {
        INFO("Creating mesh '%s'...", info.name);

        Mesh mesh = { .info = info, };

        RENDERER_PUT(mesh, meshes, info.name);
    }

    for (const PipelineInfo& info: pipelineInfo) {
        INFO("Creating pipeline '%s'...", info.name);

        VulkanPipeline pipeline = {};
        initVKPipeline(vk, info, pipeline);

        RENDERER_PUT(pipeline, pipelines, info.name);
    }

    for (const BrushInfo& info: brushInfo) {
        INFO("Creating brush '%s'...", info.name);

        Brush brush = {
            .info = info,
        };

        renderer.brushes.insert({ info.name, brush });
    }
}

// **********************************
// * WIN32: Windows specific stuff. *
// **********************************

Input input;

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
            switch (wParam) {
                case VK_ESCAPE: PostQuitMessage(0); break;
                // TODO(jan): Key mapping.
                case 'W': input.keyboard_keys[KEYBOARD_KEY_FORWARD] = true;
            }
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

    // Load shaders, meshes, fonts, textures, and other resources.
    Renderer renderer;
    init(vk, renderer);

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

        doFrame(vk, renderer, input);
    }

    return 0;
}
