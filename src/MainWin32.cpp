#include "Vulkan.h"
#include <stdio.h>
#pragma warning (disable: 4267)
#pragma warning (disable: 4996)

#include <Windows.h>
#include <cstdio>
#include <map>
#include <set>

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb/stb_rect_pack.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#include "Types.h"

// ***********
// * LOGGING *
// ***********

struct ConsoleLine {
    // NOTE(jan): Relative to Console::bytesRead
    umm start;
    umm size;
};

#define MAX_SCROLLBACK_LINES 1024
struct LineBuffer {
    ConsoleLine data[MAX_SCROLLBACK_LINES];
    umm first;
    umm next;
    umm count;
    umm max;
};

struct Console {
    void* data;
    umm bytesRead;
    umm top;
    umm bottom;
    umm size;
    LineBuffer lines;
};

void
consolePushLine(Console& console, ConsoleLine& line) {
    LineBuffer& lines = console.lines;
    lines.data[lines.next] = line;
    lines.next = (lines.next + 1) % lines.max;
    lines.first = lines.count < lines.max ? 0 : lines.next + 1;
    lines.count = min(lines.max, lines.count + 1);
}

Console console;
LARGE_INTEGER counterEpoch;
LARGE_INTEGER counterFrequency;
static FILE* logFile;

float getElapsed() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    auto result =
        (t.QuadPart - counterEpoch.QuadPart)
        / (float)counterFrequency.QuadPart;
    return result;
}

void log(const char* level, const char* fileName, int lineNumber, const char* fmt, ...) {
    char* consoleEnd = (char*)console.data + console.bottom;

    ConsoleLine line;
    line.start = console.bottom;

    const char* prefixFmt = "[%s] [%f] [%s:%d] ";

    fprintf(logFile, prefixFmt, level, getElapsed(), fileName, lineNumber);
    int written = sprintf(consoleEnd, prefixFmt, level, getElapsed(), fileName, lineNumber);
    line.size = written;
    console.bytesRead += written;
    console.bottom = (console.bottom + written) % console.size;
    consoleEnd = (char*)console.data + console.bottom;

    va_list args;
    va_start(args, fmt);
    vfprintf(logFile, fmt, args);
    written = vsnprintf(consoleEnd, console.size, fmt, args);
    line.size += written;
    console.bytesRead += written;
    console.bottom = (console.bottom + written) % console.size;
    consoleEnd = (char*)console.data + console.bottom;
    va_end(args);

    fprintf(logFile, "\n");
    written = sprintf(consoleEnd, "\n");
    line.size += written;
    console.bytesRead += written;
    console.bottom = (console.bottom + written) % console.size;
    fflush(logFile);

    consolePushLine(console, line);
}

#define LOG(level, fmt, ...) log(level, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define FATAL(fmt, ...) {\
    LOG("FATAL", fmt, __VA_ARGS__);\
    fclose(logFile);\
    exit(1);\
}
#define INFO(fmt, ...) LOG("INFO", fmt, __VA_ARGS__)
#define WARN(fmt, ...) LOG("WARN", fmt, __VA_ARGS__)
#define ERR(fmt, ...) LOG("ERR", fmt, __VA_ARGS__)

#define CHECK(x, fmt, ...) if (!x) { FATAL(fmt, __VA_ARGS__) }
#define LERROR(x) \
    if (x) {      \
        char buffer[1024]; \
        strerror_s(buffer, x); \
        FATAL("%s", buffer); \
    }

// ***************
// * END LOGGING *
// ***************

#include "Memory.cpp"
#include "String.cpp"
#include "MathLib.cpp"
#include "FileSystem.cpp"
#include "Vulkan.cpp"
#include <vulkan/vulkan_win32.h>

using std::map;
using std::set;

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

struct Uniforms {
    float proj[16];
    float ortho[16];
    Vec4 eye;
    Vec4 rotation;
};

struct Input {
    bool keyboard_keys[KEYBOARD_KEY_COUNT];
};

struct FontInfo {
    const char* name;
    const char* path;
    float size;
};

struct FontInfo fontInfo[] = {
    {
        .name = "default",
        .path = "./fonts/AzeretMono-Medium.ttf",
        .size = 20.f
    },
};

struct Font {
    FontInfo info;
    bool isDirty;
    vector<char> ttfFileContents;

    u32 bitmapSideLength;
    VulkanSampler sampler;

    set<u32> codepointsToLoad;
    set<u32> failedCodepoints;
    map<u32, stbtt_packedchar> dataForCodepoint;
};

struct MeshInfo {
    const char* name;
};

struct Mesh {
    MeshInfo info;

    umm vertexCount;
    vector<f32> vertices;

    umm indexCount;
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

MeshInfo meshInfo[] = {
    {
        .name = "boxes",
    },
    {
        .name = "text",
    },
};

PipelineInfo pipelineInfo[] = {
    {
        .name = "text",
        .vertexShaderPath = "shaders/ortho_xy_uv_rgba.vert.spv",
        .fragmentShaderPath = "shaders/text.frag.spv",
        .clockwiseWinding = true,
        .cullBackFaces = false,
        .depthEnabled = false,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    },
    {
        .name = "boxes",
        .vertexShaderPath = "shaders/ortho_xy_uv_rgba.vert.spv",
        .fragmentShaderPath = "shaders/boxes.frag.spv",
        .clockwiseWinding = true,
        .cullBackFaces = false,
        .depthEnabled = false,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    }
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
    {
        .name = "boxes",
        .meshName = "boxes",
        .pipelineName = "boxes"
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

// ***********
// * GLOBALS *
// ***********

MemoryArena globalArena;

RECT windowRect;
f32 windowWidth;
f32 windowHeight;

Vulkan vk;
Vec4 base03 = { .x =      0.f, .y =  43/255.f, .z =  54/255.f, .w = 1.f };
Vec4 base01 = { .x = 88/255.f, .y = 110/255.f, .z = 117/255.f, .w = 1.f };
Vec4 white =  { .x =      1.f, .y =       1.f, .z =       1.f, .w = 1.f };

// **************************
// * FONT: Font management. *
// **************************

void
packFont(Font& font) {
    INFO("Packing %llu codepoints", font.codepointsToLoad.size());

    font.bitmapSideLength = 512;
    umm bitmapSize = font.bitmapSideLength * font.bitmapSideLength;
    u8* bitmap = new u8[font.bitmapSideLength * font.bitmapSideLength];

    stbtt_pack_context ctxt = {};
    stbtt_PackBegin(&ctxt, bitmap, font.bitmapSideLength, font.bitmapSideLength, 0, 1, NULL);

    for (u32 codepoint: font.codepointsToLoad) {
        if (font.failedCodepoints.contains(codepoint)) continue;

        stbtt_packedchar cdata;
        int result = stbtt_PackFontRange(
            &ctxt,
            (u8*)font.ttfFileContents.data(), 0,
            font.info.size,
            codepoint, 1,
            &cdata
        );
        if (!result) {
            INFO("Could not load codepoint %u", codepoint);
            font.failedCodepoints.insert(codepoint);
        } else {
            font.dataForCodepoint[codepoint] = cdata;
        }
    }

    stbtt_PackEnd(&ctxt);

    if (font.sampler.handle != VK_NULL_HANDLE) {
        destroySampler(vk, font.sampler);
    }

    uploadTexture(vk, font.bitmapSideLength, font.bitmapSideLength, VK_FORMAT_R8_UNORM, bitmap, bitmapSize, font.sampler);
    delete[] bitmap;

    font.isDirty = false;
}

// ***********************************************
// * FRAME: Everything required to draw a frame. *
// ***********************************************

void
pushAABox(Mesh& mesh, AABox& box, AABox& tex, Vec4& color) {
    umm baseIndex = mesh.vertexCount;

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
    mesh.vertices.push_back(color.x);
    mesh.vertices.push_back(color.y);
    mesh.vertices.push_back(color.z);
    mesh.vertices.push_back(color.w);
    mesh.vertexCount++;
    // NOTE(jan): Top-right, v1.
    mesh.vertices.push_back(box.x1);
    mesh.vertices.push_back(box.y0);
    mesh.vertices.push_back(tex.x1);
    mesh.vertices.push_back(tex.y0);
    mesh.vertices.push_back(color.x);
    mesh.vertices.push_back(color.y);
    mesh.vertices.push_back(color.z);
    mesh.vertices.push_back(color.w);
    mesh.vertexCount++;
    // NOTE(jan): Bottom-right, v2.
    mesh.vertices.push_back(box.x1);
    mesh.vertices.push_back(box.y1);
    mesh.vertices.push_back(tex.x1);
    mesh.vertices.push_back(tex.y1);
    mesh.vertices.push_back(color.x);
    mesh.vertices.push_back(color.y);
    mesh.vertices.push_back(color.z);
    mesh.vertices.push_back(color.w);
    mesh.vertexCount++;
    // NOTE(jan): Bottom-left, v3.
    mesh.vertices.push_back(box.x0);
    mesh.vertices.push_back(box.y1);
    mesh.vertices.push_back(tex.x0);
    mesh.vertices.push_back(tex.y1);
    mesh.vertices.push_back(color.x);
    mesh.vertices.push_back(color.y);
    mesh.vertices.push_back(color.z);
    mesh.vertices.push_back(color.w);
    mesh.vertexCount++;

    // NOTE(jan): Top-right triangle.
    mesh.indices.push_back(baseIndex + 0);
    mesh.indices.push_back(baseIndex + 1);
    mesh.indices.push_back(baseIndex + 2);
    mesh.indexCount += 3;

    // NOTE(jan): Bottom-left triangle.
    mesh.indices.push_back(baseIndex + 0);
    mesh.indices.push_back(baseIndex + 2);
    mesh.indices.push_back(baseIndex + 3);
    mesh.indexCount += 3;
}

void pushAABox(Mesh& mesh, AABox& box, Vec4& color) {
    AABox tex = {
        .x0 = 0,
        .x1 = 1,
        .y0 = 0,
        .y1 = 1
    };

    pushAABox(mesh, box, tex, color);
}

void pushText(Mesh& mesh, Font& font, AABox& box, String text, Vec4 color) {
    f32 x = box.x0;
    f32 y = box.y1;

    umm stringIndex = 0;

    while (stringIndex < text.length) {
        char c = text.data[stringIndex];

        // TODO(jan): Better detection of new-lines (unicode).
        if (c == '\n') {
            x = box.x0;
            y += font.info.size;
            stringIndex++;
            continue;
        }
        // TODO(jan): UTF-8 decoding.
        u32 codepoint = (u32)c;

        if (!font.dataForCodepoint.contains(codepoint)) {
            if (!font.failedCodepoints.contains(codepoint)) {
                font.codepointsToLoad.insert(codepoint);
                font.isDirty = true;
            }
            stringIndex++;
            continue;
        }
        stbtt_packedchar cdata = font.dataForCodepoint[codepoint];

        stbtt_aligned_quad quad;
        stbtt_GetPackedQuad(&cdata, font.bitmapSideLength, font.bitmapSideLength, 0, &x, &y, &quad, 0);

        AABox charBox = {
            .x0 = quad.x0,
            .x1 = quad.x1,
            .y0 = quad.y0,
            .y1 = quad.y1
        };

        AABox tex = {
            .x0 = quad.s0,
            .x1 = quad.s1,
            .y0 = quad.t0,
            .y1 = quad.t1
        };

        pushAABox(mesh, charBox, tex, color);

        stringIndex++;
    }
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

    // NOTE(jan): Calculate uniforms (projection matrix &c).
    Uniforms uniforms;

    matrixInit(uniforms.ortho);
    matrixOrtho(windowWidth, windowHeight, uniforms.ortho);

    updateUniforms(vk, &uniforms, sizeof(Uniforms));

    // NOTE(jan): Meshes are cleared and recalculated each frame.
    for (auto& pair: renderer.meshes) {
        Mesh& mesh = pair.second;
        mesh.indexCount = 0;
        mesh.indices.clear();
        mesh.vertexCount = 0;
        mesh.vertices.clear();
    }

    RENDERER_GET(boxes, meshes, "boxes");
    RENDERER_GET(text, meshes, "text");
    RENDERER_GET(font, fonts, "default");

    // NOTE(jan): Building mesh for console.
    AABox backgroundBox = {
        .x0 = 0.f,
        .x1 = windowWidth,
        .y0 = 0.f,
        .y1 = windowHeight / 2.f
    };
    pushAABox(boxes, backgroundBox, base03);

    // NOTE(jan): Building mesh for console prompt.
    const f32 margin = font.info.size / 2.f;
    AABox consoleLineBox = {
        .x0 = margin,
        .y1 = backgroundBox.y1 - margin,
    };
    pushText(text, font, consoleLineBox, stringLiteral("> "), base01);
    consoleLineBox.y1 -= font.info.size;

    // NOTE(jan): Building mesh for console scrollback.
    for (umm lineIndex = console.lines.next - 1; lineIndex > console.lines.first; lineIndex--) {
        if (consoleLineBox.y1 < 0) break;
        ConsoleLine line = console.lines.data[lineIndex];
        String consoleText = {
            .size = line.size,
            // TODO(jan): This is wrong
            .length = line.size,
            .data = (char*)console.data + line.start,
        };
        pushText(text, font, consoleLineBox, consoleText, base01);
        consoleLineBox.y1 -= font.info.size;
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

    std::vector<VulkanMesh> meshesToFree;

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
        if (font.sampler.handle != VK_NULL_HANDLE) {
            updateCombinedImageSampler(
                vk.device, pipeline.descriptorSet, 1, &font.sampler, 1
            );
        }
        updateUniformBuffer(vk.device, pipeline.descriptorSet, 0, vk.uniforms.handle);

        RENDERER_GET(mesh, meshes, brush.info.meshName);
        if ((mesh.indexCount == 0) || (mesh.vertexCount == 0)) continue;

        VulkanMesh& vkMesh = meshesToFree.emplace_back();
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

    // Cleanup.
    for (auto& mesh: meshesToFree) {
        destroyMesh(vk, mesh);
    }
    if (font.isDirty) packFont(font);
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

// Derived from https://github.com/cmuratori/refterm/blob/main/refterm_example_source_buffer.c
Console initConsole(size_t bufferSize) {
    Console result = {};

    SYSTEM_INFO info;
    GetSystemInfo(&info);
    if (!isPowerOfTwo(info.dwAllocationGranularity)) {
        FATAL("system allocation size is not a power of two");
    }

    bufferSize = ((bufferSize / info.dwAllocationGranularity) + 1) * info.dwAllocationGranularity;
    if (bufferSize % info.dwAllocationGranularity != 0) {
        FATAL("invalid buffer size");
    }

    HANDLE section = CreateFileMapping(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        (DWORD)(bufferSize >> 32),
        (DWORD)(bufferSize & 0xffffffff),
        NULL
    );

    void* ringBuffer = nullptr;
    // NOTE(jan): Try to find two consecutive memory areas to map the section twice back-to-back.
    for(size_t offset =   0x40000000;
               offset <  0x400000000;
               offset +=   0x1000000) {
        void *view1 = MapViewOfFileEx(section, FILE_MAP_ALL_ACCESS, 0, 0, bufferSize, (void *)offset);
        void *view2 = MapViewOfFileEx(section, FILE_MAP_ALL_ACCESS, 0, 0, bufferSize, byteOffset(offset, bufferSize));

        if(view1 && view2) {
            ringBuffer = view1;
            break;
        }

        if(view1) UnmapViewOfFile(view1);
        if(view2) UnmapViewOfFile(view2);
    }

    if (!ringBuffer) {
        FATAL("could not allocate ringbuffer");
    }

    result.data = ringBuffer;
    result.size = bufferSize;
    result.top = 0;
    result.bottom = 0;
    result.lines.max = MAX_SCROLLBACK_LINES;
    return result;
}

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
    auto error = fopen_s(&logFile, "LOG", "w");
    if (error) exit(-1);
    console = initConsole(1 * 1024 * 1024);

    QueryPerformanceCounter(&counterEpoch);
    QueryPerformanceFrequency(&counterFrequency);
    INFO("Logging initialized.");

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

    // TODO(jan): Handle resize.
    GetWindowRect(window, &windowRect);
    windowWidth = windowRect.right - windowRect.left;
    windowHeight = windowRect.bottom - windowRect.top;

    // NOTE(jan): Create Vulkan instance..
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
