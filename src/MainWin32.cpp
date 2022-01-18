#include "Vulkan.h"
#include "vulkan/vulkan_core.h"
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
#include "Logging.cpp"
#include "Memory.cpp"
#include "String.cpp"
#include "MathLib.cpp"
#include "FileSystem.cpp"
#include "TTF.cpp"
#include "Vulkan.cpp"
#include <vulkan/vulkan_win32.h>

using std::map;
using std::set;

const int WIDTH = 800;
const int HEIGHT = 800;

// ************************************************************
// * MATH: Definitions for geometric mathematical primitives. *
// ************************************************************

inline int max(int a, int b) {
    return (a > b) ? (a) : (b);
}

// ******
// * UI *
// ******

struct Input {
    bool consolePageDown;
    bool consolePageUp;
    bool consoleNewLine;
    bool consoleToggle;
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
    umm vertexSizeInFloats;
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
        .name = "contours",
    },
    {
        .name = "text",
    },
    {
        .name = "labels",
    },
    {
        .name = "lines",
    },
    {
        .name = "console",
    },
    {
        .name = "background"
    },
    {
        .name = "control_points"
    },
    {
        .name = "icons",
    }
};

PipelineInfo pipelineInfo[] = {
    {
        .name = "text",
        .vertexShaderPath = "shaders/ortho_xy_uv_rgba.vert.spv",
        .fragmentShaderPath = "shaders/text.frag.spv",
        .clockwiseWinding = true,
        .cullBackFaces = false,
        .depthEnabled = false,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    },
    {
        .name = "icons",
        .vertexShaderPath = "shaders/ortho_xy_uv_rgba.vert.spv",
        .fragmentShaderPath = "shaders/text.frag.spv",
        .clockwiseWinding = true,
        .cullBackFaces = false,
        .depthEnabled = false,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    },
    {
        .name = "boxes",
        .vertexShaderPath = "shaders/ortho_xy_uv_rgba.vert.spv",
        .fragmentShaderPath = "shaders/rgba.frag.spv",
        .clockwiseWinding = true,
        .cullBackFaces = false,
        .depthEnabled = false,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    },
    {
        .name = "lines",
        .vertexShaderPath = "shaders/ortho_xy_rgba.vert.spv",
        .fragmentShaderPath = "shaders/lines.frag.spv",
        .depthEnabled = false,
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    },
};

struct BrushInfo {
    const char* name;
    const char* meshName;
    const char* pipelineName;
    vector<UniformInfo> uniforms;
    bool disabled;
};

struct Brush {
    BrushInfo info;
};

BrushInfo brushInfo[] = {
    {
        .name = "background",
        .meshName = "background",
        .pipelineName = "boxes"
    },
    {
        .name = "triangles",
        .meshName = "contours",
        .pipelineName = "stencil"
    },
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
        .name = "icons",
        .meshName = "icons",
        .pipelineName = "icons",
        // TODO(jan): Set up uniforms for textures.
    },
    {
        .name = "control_points",
        .meshName = "control_points",
        .pipelineName = "boxes"
    },
    {
        .name = "labels",
        .meshName = "labels",
        .pipelineName = "text"
    },
    {
        .name = "console",
        .meshName = "console",
        .pipelineName = "boxes"
    },
    {
        .name = "lines",
        .meshName = "lines",
        .pipelineName = "lines",
    },
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
MemoryArena tempArena;

Input input;

RECT windowRect;
f32 windowWidth;
f32 windowHeight;

#define COLOUR_FROM_HEX(name, r, g, b) Vec4 name = { .x = r/255.f, .y = g/255.f, .z = b/255.f, .w = 1.f }

Vulkan vk;
Vec4 base03 = { .x =      0.f, .y =  43/255.f, .z =  54/255.f, .w = 1.f };
Vec4 base01 = { .x = 88/255.f, .y = 110/255.f, .z = 117/255.f, .w = 1.f };
Vec4 white =  { .x =      1.f, .y =       1.f, .z =       1.f, .w = 1.f };
COLOUR_FROM_HEX(base00,  0x62, 0x62, 0x62);
COLOUR_FROM_HEX(base02,  0x07, 0x36, 0x42);
COLOUR_FROM_HEX(magenta, 0xd3, 0x36, 0x82);
COLOUR_FROM_HEX(green,   0x85, 0x99, 0x00);
COLOUR_FROM_HEX(cyan,    0x2a, 0xa1, 0x98);
COLOUR_FROM_HEX(yellow,  0xb5, 0x89, 0x00);

// const char* ttfPath = "fonts/AzeretMono-Medium.ttf";
// char testChar = 'B';
const char* ttfPath = "fonts/fa-regular-400.ttf";
u32 testCodepoint = 0xf005;
u32 testIndex = 61444;
VulkanSampler glyphTexture = {};
bool interpolateGlyph = true;
bool debug = true;

// ******************************
// * GEOM: Geometry management. *
// ******************************

void
pushLine(Mesh& mesh, Vec2& start, Vec2& end, Vec4& color) {
    umm baseIndex = mesh.vertexCount;

    mesh.vertices.push_back(start.x);
    mesh.vertices.push_back(start.y);
    mesh.vertices.push_back(color.x);
    mesh.vertices.push_back(color.y);
    mesh.vertices.push_back(color.z);
    mesh.vertices.push_back(color.w);
    mesh.vertexCount++;

    mesh.vertices.push_back(end.x);
    mesh.vertices.push_back(end.y);
    mesh.vertices.push_back(color.x);
    mesh.vertices.push_back(color.y);
    mesh.vertices.push_back(color.z);
    mesh.vertices.push_back(color.w);
    mesh.vertexCount++;

    mesh.indices.push_back(baseIndex);
    mesh.indices.push_back(baseIndex+1);
    mesh.indexCount++;
}

void
pushTriangle(Mesh& mesh, Vec2& p0, Vec2& p1, Vec2& p2) {
    umm baseIndex = mesh.vertexCount;

    mesh.vertices.push_back(p0.x);
    mesh.vertices.push_back(p0.y);
    mesh.vertexCount++;

    mesh.vertices.push_back(p1.x);
    mesh.vertices.push_back(p1.y);
    mesh.vertexCount++;

    mesh.vertices.push_back(p2.x);
    mesh.vertices.push_back(p2.y);
    mesh.vertexCount++;

    mesh.indices.push_back(baseIndex);
    mesh.indices.push_back(baseIndex+1);
    mesh.indices.push_back(baseIndex+2);
    mesh.indexCount += 3;
}

void
pushTriangleWithBarycenter(Mesh& mesh, Vec2& p0, Vec2& p1, Vec2& p2) {
    umm baseIndex = mesh.vertexCount;

    mesh.vertices.push_back(p0.x);
    mesh.vertices.push_back(p0.y);
    mesh.vertices.push_back(0);
    mesh.vertices.push_back(0);
    mesh.vertexCount++;

    mesh.vertices.push_back(p1.x);
    mesh.vertices.push_back(p1.y);
    mesh.vertices.push_back(.5f);
    mesh.vertices.push_back(0);
    mesh.vertexCount++;

    mesh.vertices.push_back(p2.x);
    mesh.vertices.push_back(p2.y);
    mesh.vertices.push_back(1);
    mesh.vertices.push_back(1);
    mesh.vertexCount++;

    mesh.indices.push_back(baseIndex);
    mesh.indices.push_back(baseIndex+1);
    mesh.indices.push_back(baseIndex+2);
    mesh.indexCount += 3;
}

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

AABox
pushText(Mesh& mesh, Font& font, AABox& box, String text, Vec4 color) {
    AABox result = {
        .x0 = box.x0,
        .y1 = box.y1
    };

    umm startVertexIndex = mesh.vertices.size();
    umm lineBreaks = 0;

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

        if (quad.x1 > box.x1) {
            lineBreaks++;
            x = box.x0;
            y += font.info.size;
            stbtt_GetPackedQuad(&cdata, font.bitmapSideLength, font.bitmapSideLength, 0, &x, &y, &quad, 0);
        }

        AABox charBox = {
            .x0 = quad.x0,
            .x1 = quad.x1,
            .y0 = quad.y0,
            .y1 = quad.y1
        };
        result.x0 = min(charBox.x0, result.x0);
        result.x1 = fmax(charBox.x1, result.x1);

        AABox tex = {
            .x0 = quad.s0,
            .x1 = quad.s1,
            .y0 = quad.t0,
            .y1 = quad.t1
        };

        pushAABox(mesh, charBox, tex, color);

        stringIndex++;
    }

    if (lineBreaks > 0) {
        for (umm vertexIndex = startVertexIndex; vertexIndex < mesh.vertices.size(); vertexIndex += mesh.vertexSizeInFloats) {
            mesh.vertices[vertexIndex + 1] -= font.info.size * lineBreaks;
        }
    }

    result.y0 = result.y1 - (lineBreaks + 1) * font.info.size;
    return result;
}

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

void renderIcon() {
    MemoryArena tempArena = {};

    TTFFile ttfFile = {};
    TTFGlyph glyph = {};
    bool glyphLoaded = TTFLoadFromPath(ttfPath, &globalArena, ttfFile) &&
                       TTFLoadCodepoint(ttfFile, testCodepoint, interpolateGlyph, &tempArena, &globalArena, glyph);
                       // TTFLoadGlyph(ttfFile, testIndex, true, &tempArena, &globalArena, glyph);
    if (!glyphLoaded) {
        ERR("could not load TTF");
        return;
    }

    const float glyphWidth = glyph.bbox.x1 - glyph.bbox.x0;
    const float glyphHeight = glyph.bbox.y1 - glyph.bbox.y0;
    #define xToStencil(n) ((n) - glyph.bbox.x0)
    #define yToStencil(n) (glyphHeight - glyph.bbox.y0 - (n))
    #define vecToStencil(new, old) Vec2 new = Vec2 { .x = xToStencil(old.x), .y = yToStencil(old.y) }

    // NOTE(jan): Push contour mesh.
    Mesh contourMesh = {};
    VulkanMesh contourVKMesh = {};
    {
        u16 pointIndex = 0;
        u16 contourIndex = 0;
        // NOTE(jan): Should this be outside?
        Vec2 p0 = { .x = 0, .y = 0 };
        while (contourIndex < glyph.contourCount) {
            u16 contourEnd = glyph.contourEnds[contourIndex];
            if (contourEnd - pointIndex + 1 < 2) {
                pointIndex = contourEnd;
                continue;
            }
            Vec2 firstPoint = glyph.points[pointIndex];
            vecToStencil(firstPointStencil, firstPoint);

            umm i1 = pointIndex;
            pointIndex += 2;
            while (pointIndex <= contourEnd) {
                umm i2 = pointIndex;
                pointIndex += 2;

                Vec2 p1 = glyph.points[i1];
                vecToStencil(p1Stencil, p1);
                Vec2 p2 = glyph.points[i2];
                vecToStencil(p2Stencil, p2);
                pushTriangle(contourMesh, p0, p1Stencil, p2Stencil);

                i1 = i2;
            }

            Vec2 lastPoint = glyph.points[i1];
            vecToStencil(lastPointStencil, lastPoint);
            pushTriangle(contourMesh, p0, firstPointStencil, lastPointStencil);

            contourIndex++;
        }

        uploadMesh(
            vk,
            contourMesh.vertices.data(), sizeof(contourMesh.vertices[0]) * contourMesh.vertices.size(),
            contourMesh.indices.data(), sizeof(contourMesh.indices[0]) * contourMesh.indices.size(),
            contourVKMesh
        );
    }

    // NOTE(jan): Push correction mesh.
    Mesh correctionMesh = {};
    VulkanMesh vkCorrectionMesh = {};
    {
        u16 pointIndex = 0;
        u16 contourIndex = 0;
        while (contourIndex < glyph.contourCount) {
            u16 contourEnd = glyph.contourEnds[contourIndex];
            if (contourEnd - pointIndex + 1 < 3) {
                pointIndex = contourEnd;
                continue;
            }
            u16 contourStart = pointIndex;

            while (pointIndex <= contourEnd - 3) {
                umm i0 = pointIndex++;
                umm i1 = pointIndex++;
                umm i2 = pointIndex;

                Vec2 p0 = glyph.points[i0];
                vecToStencil(p0Stencil, p0);
                Vec2 p1 = glyph.points[i1];
                vecToStencil(p1Stencil, p1);
                Vec2 p2 = glyph.points[i2];
                vecToStencil(p2Stencil, p2);
                pushTriangleWithBarycenter(correctionMesh, p0Stencil, p1Stencil, p2Stencil);
            }

            umm i0 = pointIndex++;
            umm i1 = pointIndex++;
            umm i2 = contourStart;
            Vec2 p0 = glyph.points[i0];
            vecToStencil(p0Stencil, p0);
            Vec2 p1 = glyph.points[i1];
            vecToStencil(p1Stencil, p1);
            Vec2 p2 = glyph.points[i2];
            vecToStencil(p2Stencil, p2);
            pushTriangleWithBarycenter(correctionMesh, p0Stencil, p1Stencil, p2Stencil);

            contourIndex++;
        }

        uploadMesh(
            vk,
            correctionMesh.vertices.data(), sizeof(correctionMesh.vertices[0]) * correctionMesh.vertices.size(),
            correctionMesh.indices.data(), sizeof(correctionMesh.indices[0]) * correctionMesh.indices.size(),
            vkCorrectionMesh
        );
    }

    VulkanImage stencilImage = {};
    VkExtent2D stencilExtent = {};
    {
        // NOTE(jan): Render to stencil buffer.
        stencilExtent = {
            .width = (u32)ceilf(glyphWidth),
            .height = (u32)ceilf(glyphHeight),
        };
        VulkanImage colorImage = {};
        createVulkanImage(
            vk.device,
            vk.memories,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_VIEW_TYPE_2D,
            stencilExtent,
            1,
            vk.queueFamily,
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            false,
            0,
            VK_SAMPLE_COUNT_1_BIT, // vk.sampleCountFlagBits,
            colorImage
        );
        createVulkanImage(
            vk.device,
            vk.memories,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_VIEW_TYPE_2D,
            stencilExtent,
            1,
            vk.queueFamily,
            VK_FORMAT_S8_UINT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_STENCIL_BIT,
            false,
            0,
            VK_SAMPLE_COUNT_1_BIT, // vk.sampleCountFlagBits,
            stencilImage
        );

        VkRenderPass renderPass = {};
        {
            VkAttachmentReference attachmentRefs[] = {
                {
                    .attachment = 0,
                    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                },
                {
                    .attachment = 1,
                    .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                }
            };

            VkAttachmentDescription attachmentDescs[] = {
                {
                    .format = VK_FORMAT_R8G8B8A8_SRGB,
                    .samples = VK_SAMPLE_COUNT_1_BIT, // .samples = vk.sampleCountFlagBits,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                },
                {
                    .format = VK_FORMAT_S8_UINT,
                    .samples = VK_SAMPLE_COUNT_1_BIT, // .samples = vk.sampleCountFlagBits,
                    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                },
            };

            VkSubpassDescription subpass = {
                .colorAttachmentCount = 1,
                .pColorAttachments = attachmentRefs,
                .pDepthStencilAttachment = attachmentRefs + 1,
            };

            VkRenderPassCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                .attachmentCount = 2,
                .pAttachments = attachmentDescs,
                .subpassCount = 1,
                .pSubpasses = &subpass,
            };

            auto result = vkCreateRenderPass(vk.device, &info, nullptr, &renderPass);
            VKCHECK(result);
        }

        VkFramebuffer framebuffer = {};
        {
            VkImageView attachments[] = { colorImage.view, stencilImage.view };
            VkFramebufferCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = renderPass,
                .attachmentCount = 2,
                .pAttachments = attachments,
                .width = stencilExtent.width,
                .height = stencilExtent.height,
                .layers = 1,
            };
            auto result = vkCreateFramebuffer(vk.device, &info, nullptr, &framebuffer);
        }

        VkCommandBuffer cmds = {};
        createCommandBuffers(vk.device, vk.cmdPool, 1, &cmds);
        beginFrameCommandBuffer(cmds);

        {
            VkClearValue colorClear;
            colorClear.color = {0.f, 0.f, 0.f, 0.f};
            VkClearValue depthClear;
            depthClear.depthStencil = {1.f, 0};
            VkClearValue clears[] = { colorClear, depthClear };

            VkOffset2D renderOffset = {
                .x = 0,
                .y = 0
            };
            VkRect2D renderArea = {
                .offset = renderOffset,
                .extent = stencilExtent,
            };
            VkRenderPassBeginInfo info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = renderPass,
                .framebuffer = framebuffer,
                .renderArea = renderArea,
                .clearValueCount = 2,
                .pClearValues = clears,
            };
            vkCmdBeginRenderPass(cmds, &info, VK_SUBPASS_CONTENTS_INLINE);
        }

        VulkanPipeline contourPipeline;
        {
            PipelineInfo info = {
                .name = "icon_stencil_contour",
                .vertexShaderPath = "shaders/ortho_xy.vert.spv",
                .fragmentShaderPath = "shaders/white.frag.spv",
                .clockwiseWinding = true,
                .cullBackFaces = false,
                .depthEnabled = false,
                .writeStencilInvert = true,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            };
            initVKPipeline(vk, info, contourPipeline, &renderPass);
        }

        VulkanPipeline correctionPipeline;
        {
            PipelineInfo info = {
                .name = "icon_stencil_correction",
                .vertexShaderPath = "shaders/ortho_xy_barycenter.vert.spv",
                .fragmentShaderPath = "shaders/barycenter.frag.spv",
                .clockwiseWinding = true,
                .cullBackFaces = false,
                .depthEnabled = false,
                .writeStencilInvert = true,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            };
            initVKPipeline(vk, info, correctionPipeline, &renderPass);
        }

        float ortho[16];
        matrixInit(ortho);
        // TODO(jan): matrixOrtho(glyphWidth, glyphHeight, ortho);
        matrixOrtho(windowWidth, windowHeight, ortho);

        // TODO(jan): Destroy.
        VulkanBuffer uniformBuffer;
        createUniformBuffer(vk.device, vk.memories, vk.queueFamily, sizeof(ortho), uniformBuffer);
        updateBuffer(vk, uniformBuffer, ortho, sizeof(ortho));
        updateUniformBuffer(vk.device, contourPipeline.descriptorSet, 0, uniformBuffer.handle);
        updateUniformBuffer(vk.device, correctionPipeline.descriptorSet, 0, uniformBuffer.handle);

        vkCmdBindPipeline(cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, contourPipeline.handle);
        vkCmdBindDescriptorSets(
            cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, contourPipeline.layout,
            0, 1, &contourPipeline.descriptorSet,
            0, nullptr
        );

        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmds, 0, 1, &contourVKMesh.vBuff.handle, offsets);
        vkCmdBindIndexBuffer(cmds, contourVKMesh.iBuff.handle, 0, VK_INDEX_TYPE_UINT32);
        for (int i = 0; i < contourMesh.indices.size() / 3; i++) {
            vkCmdDrawIndexed(cmds, 3, 1, i*3, 0, 0);
        }
        // vkCmdDrawIndexed(cmds, mesh.indices.size(), 1, 0, 0, 0);

        vkCmdBindPipeline(cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, correctionPipeline.handle);
        vkCmdBindDescriptorSets(
            cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, correctionPipeline.layout, 
            0, 1, &correctionPipeline.descriptorSet, 
            0, nullptr
        );

        vkCmdBindVertexBuffers(cmds, 0, 1, &vkCorrectionMesh.vBuff.handle, offsets);
        vkCmdBindIndexBuffer(cmds, vkCorrectionMesh.iBuff.handle, 0, VK_INDEX_TYPE_UINT32);
        for (int i = 0; i < correctionMesh.indices.size() / 3; i++) {
            vkCmdDrawIndexed(cmds, 3, 1, i*3, 0, 0);
        }

        vkCmdEndRenderPass(cmds);
        endCommandBuffer(cmds);

        {
            VkSubmitInfo info = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &cmds,
            };
            vkQueueSubmit(vk.queue, 1, &info, VK_NULL_HANDLE);
        }

        vkQueueWaitIdle(vk.queue);

        // TODO(jan): Destroy meshes / colour image(?).
    }

    // NOTE(jan): Render glyph to texture.
    // VulkanImage glyphTexture = {};
    {
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
        createVulkanImage(
            vk.device,
            vk.memories,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_VIEW_TYPE_2D,
            stencilExtent,
            1,
            vk.queueFamily,
            format,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            false,
            0,
            VK_SAMPLE_COUNT_1_BIT, // vk.sampleCountFlagBits,
            glyphTexture.image
        );
        VkRenderPass renderPass = {};
        {
            VkAttachmentDescription attachmentDescs[] = {
                {
                    .format = format,
                    .samples = VK_SAMPLE_COUNT_1_BIT, // .samples = vk.sampleCountFlagBits,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                },
                {
                    .format = VK_FORMAT_S8_UINT,
                    .samples = VK_SAMPLE_COUNT_1_BIT, // .samples = vk.sampleCountFlagBits,
                    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                },
            };
            VkAttachmentReference attachmentRefs[] = {
                {
                    .attachment = 0,
                    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                },
                {
                    .attachment = 1,
                    .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                },
            };
            VkSubpassDescription subpass = {
                .colorAttachmentCount = 1,
                .pColorAttachments = attachmentRefs,
                .pDepthStencilAttachment = attachmentRefs + 1,
            };
            VkRenderPassCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                .attachmentCount = 2,
                .pAttachments = attachmentDescs,
                .subpassCount = 1,
                .pSubpasses = &subpass
            };
            auto result = vkCreateRenderPass(vk.device, &info, nullptr, &renderPass);
            VKCHECK(result);
        }

        VkFramebuffer framebuffer = {};
        {
            VkImageView attachments[] = { glyphTexture.image.view, stencilImage.view };
            VkFramebufferCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = renderPass,
                .attachmentCount = 2,
                .pAttachments = attachments,
                .width = stencilExtent.width,
                .height = stencilExtent.height,
                .layers = 1,
            };
            auto result = vkCreateFramebuffer(vk.device, &info, nullptr, &framebuffer);
            VKCHECK(result);
        }

        Mesh mesh = {};
        {
            AABox wholeTexture = {
                .x0 = -1,
                .x1 = 1,
                .y0 = -1,
                .y1 = 1
            };
            pushAABox(mesh, wholeTexture, white);
        }
        VulkanMesh vkMesh;
        uploadMesh(
            vk,
            mesh.vertices.data(), sizeof(mesh.vertices[0]) * mesh.vertices.size(),
            mesh.indices.data(), sizeof(mesh.indices[0]) * mesh.indices.size(),
            vkMesh
        );

        VulkanPipeline pipeline;
        {
            PipelineInfo info = {
                .name = "glyph_texture",
                .vertexShaderPath = "shaders/passthrough_xy_uv_rgba.vert.spv",
                .fragmentShaderPath = "shaders/rgba.frag.spv",
                .clockwiseWinding = true,
                .cullBackFaces = false,
                .depthEnabled = false,
                .readStencil = true,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            };
            initVKPipeline(vk, info, pipeline, &renderPass);
        }

        VkCommandBuffer cmds = VK_NULL_HANDLE;
        createCommandBuffers(vk.device, vk.cmdPool, 1, &cmds);
        beginFrameCommandBuffer(cmds);

        {
            VkClearValue clear = {};
            clear.color = {};
            VkOffset2D renderOffset = {
                .x = 0,
                .y = 0
            };
            VkRect2D renderArea = {
                .offset = renderOffset,
                .extent = stencilExtent
            };
            VkRenderPassBeginInfo info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = renderPass,
                .framebuffer = framebuffer,
                .renderArea = renderArea,
                .clearValueCount = 1,
                .pClearValues = &clear,
            };
            vkCmdBeginRenderPass(cmds, &info, VK_SUBPASS_CONTENTS_INLINE);
        }

        vkCmdBindPipeline(cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmds, 0, 1, &vkMesh.vBuff.handle, offsets);
        vkCmdBindIndexBuffer(cmds, vkMesh.iBuff.handle, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmds, mesh.indexCount, 1, 0, 0, 0);

        vkCmdEndRenderPass(cmds);
        endCommandBuffer(cmds);

        {
            VkSubmitInfo info = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &cmds
            };
            auto result = vkQueueSubmit(vk.queue, 1, &info, VK_NULL_HANDLE);
            VKCHECK(result);
        }

        vkDeviceWaitIdle(vk.device);
    }

    createSampler(vk.device, glyphTexture.handle);
    {
        auto cmds = allocateCommandBuffer(vk.device, vk.cmdPoolTransient);
        beginOneOffCommandBuffer(cmds);

        {
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.image = glyphTexture.image.handle;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(
                cmds,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
        }

        endCommandBuffer(cmds);

        {
            VkSubmitInfo info = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &cmds
            };
            auto result = vkQueueSubmit(vk.queue, 1, &info, VK_NULL_HANDLE);
            VKCHECK(result);
        }

        vkDeviceWaitIdle(vk.device);
    }
}

// ***************************
// * FRAME: Drawing a frame. *
// ***************************

void doFrame(Vulkan& vk, Renderer& renderer) {
    f32 frameStart = getElapsed();

    MemoryArena frameArena = {};

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

    RENDERER_GET(background, meshes, "background");
    RENDERER_GET(consoleMesh, meshes, "console");
    RENDERER_GET(controlPoints, meshes, "control_points");
    RENDERER_GET(labels, meshes, "labels");
    RENDERER_GET(icons, meshes, "icons");
    RENDERER_GET(lines, meshes, "lines");
    RENDERER_GET(contours, meshes, "contours");
    RENDERER_GET(text, meshes, "text");
    RENDERER_GET(font, fonts, "default");

    std::vector<VulkanMesh> meshesToFree;

    if (input.consoleToggle) {
        console.show = !console.show;
        input.consoleToggle = false;
    }
    if (input.consoleNewLine) {
        logRaw("> ");
        input.consoleNewLine = false;
    }

    TTFFile ttfFile = {};
    TTFGlyph glyph = {};
    bool glyphLoaded = TTFLoadFromPath(ttfPath, &frameArena, ttfFile) &&
                       TTFLoadCodepoint(ttfFile, testCodepoint, interpolateGlyph, &frameArena, &frameArena, glyph);
                       // TTFLoadGlyph(ttfFile, testIndex, true, &tempArena, &globalArena, glyph);
    if (!glyphLoaded) {
        ERR("could not load TTF");
    } else {
        const float glyphWidth = glyph.bbox.x1 - glyph.bbox.x0;
        const float glyphHeight = glyph.bbox.y1 - glyph.bbox.y0;
        const Vec2 screenOffset = {
            .x = (windowWidth - glyphWidth) / 2.f,
            .y = (windowHeight - glyphHeight) / 2.f,
        };
        #define xToScreen(n) ((n) - glyph.bbox.x0 + screenOffset.x)
        #define yToScreen(n) ((windowHeight - screenOffset.y) - glyph.bbox.y0 - (n))
        AABox centeredBox = {
            .x0 = xToScreen(glyph.bbox.x0),
            .x1 = xToScreen(glyph.bbox.x1),
            .y0 = yToScreen(glyph.bbox.y0),
            .y1 = yToScreen(glyph.bbox.y1),
        };
        pushAABox(background, centeredBox, base03);

        AABox textureCoords = {
            .x0 = 0,
            .x1 = 1,
            .y0 = 1,
            .y1 = 0
        };
        pushAABox(icons, centeredBox, textureCoords, base00);

        // NOTE(jan): Push control points.
        if (debug) {
            #define vecToScreen(new, old) Vec2 new = Vec2 { .x = xToScreen(old.x), .y = yToScreen(old.y) }
            int contourIndex = 0;
            for (int pointIndex = 0; pointIndex < glyph.pointCount; pointIndex++) {
                int contourEnd = glyph.contourEnds[contourIndex];
                if (pointIndex > contourEnd) contourIndex++;

                const Vec2 glyphPoint = glyph.points[pointIndex];
                vecToScreen(screenPoint, glyphPoint);
                AABox pointBox = {
                    .x0 = screenPoint.x - 5,
                    .x1 = screenPoint.x + 5,
                    .y0 = screenPoint.y - 5,
                    .y1 = screenPoint.y + 5,
                };

                if (glyph.isOnCurve[pointIndex]) {
                    pushAABox(controlPoints, pointBox, green);
                } else {
                    pushAABox(controlPoints, pointBox, magenta);
                }

                AABox labelBox = {
                    .x0 = pointBox.x1 + 5,
                    .x1 = windowWidth,
                    .y1 = pointBox.y1 + 5
                };
                char labelBuffer[255];
                int written = snprintf(labelBuffer, 255, "%d/%d", pointIndex, contourIndex);
                String label = {
                    .size = 255,
                    .length = static_cast<umm>(written),
                    .data = labelBuffer,
                };

                pushText(labels, font, labelBox, label, yellow);
            }
        }

        // NOTE(jan): Push lines.
        if (debug) {
            int pointIndex = 0;
            int contourIndex = 0;
            while (contourIndex < glyph.contourCount) {
                u16 contourEnd = glyph.contourEnds[contourIndex];
                Vec2 firstPoint = glyph.points[pointIndex];

                while (pointIndex < contourEnd) {
                    Vec2 point = glyph.points[pointIndex];
                    vecToScreen(screenPoint, point);
                    Vec2 nextPoint = glyph.points[pointIndex+1];
                    vecToScreen(screenNextPoint, nextPoint);
                    pushLine(lines, screenPoint, screenNextPoint, green);
                    pointIndex++;
                }

                Vec2 lastPoint = glyph.points[pointIndex];
                vecToScreen(screenFirstPoint, firstPoint);
                vecToScreen(screenLastPoint, lastPoint);
                pushLine(lines, screenFirstPoint, screenLastPoint, green);

                pointIndex++;
                contourIndex++;
            }
        }
    }

    if (console.show) {
        // NOTE(jan): Building mesh for console.
        AABox backgroundBox = {
            .x0 = 0.f,
            .x1 = windowWidth,
            .y0 = 0.f,
            .y1 = windowHeight / 2.f
        };
        pushAABox(consoleMesh, backgroundBox, base02);

        // NOTE(jan): Building mesh for console prompt.
        const f32 margin = font.info.size / 2.f;
        const f32 console_height = backgroundBox.y1 - backgroundBox.y0 - margin;
        const u32 console_line_height = console_height / font.info.size;

        if (input.consolePageUp && (console.lines.viewOffset < console.lines.count - console_line_height)) {
            console.lines.viewOffset++;
        }
        if (input.consolePageDown && (console.lines.viewOffset > 0)) {
            console.lines.viewOffset--;
        }

        AABox consoleLineBox = {
            .x0 = margin,
            .x1 = backgroundBox.x1,
            .y1 = backgroundBox.y1 - margin,
        };
        AABox promptBox = pushText(text, font, consoleLineBox, stringLiteral("> "), base01);

        f32 cursorAlpha = (1 + sin(frameStart * 10.f)) / 2.f;
        AABox cursorBox = {};
        cursorBox.x0 = promptBox.x1;
        cursorBox.x1 = backgroundBox.x1;
        cursorBox.y1 = promptBox.y1;
        Vec4 cursorColor = {
            .x = base01.x,
            .y = base01.y,
            .z = base01.z,
            .w = cursorAlpha
        };
        cursorBox = pushText(text, font, cursorBox, stringLiteral("_"), cursorColor);

        consoleLineBox.y1 = cursorBox.y0;

        // NOTE(jan): Building mesh for console scrollback.
        umm top = console.lines.next >= 1 + console.lines.viewOffset ? console.lines.next : console.lines.count;
        umm lineIndex = top - 1 - console.lines.viewOffset;
        for (umm i = 0; i < console.lines.count; i++) {
            if (consoleLineBox.y1 < 0) break;
            ConsoleLine line = console.lines.data[lineIndex];
            String consoleText = {
                .size = line.size,
                .length = line.size,
                .data = (char*)console.data + line.start,
            };
            AABox prevLineBox = pushText(text, font, consoleLineBox, consoleText, base01);
            consoleLineBox.y1 = prevLineBox.y0;

            if (lineIndex > 0) {
                lineIndex--;
            } else {
                lineIndex = console.lines.count - 1;
            }
        }
    }

    // NOTE(jan): Update uniforms.
    for (auto kv: renderer.pipelines) {
        auto& pipeline = kv.second;
        updateUniformBuffer(vk.device, pipeline.descriptorSet, 0, vk.uniforms.handle);

        const char* key = kv.first;
        if (strcmp(key, "icons") == 0) {
            updateCombinedImageSampler(
                vk.device, pipeline.descriptorSet, 1, &glyphTexture, 1
            );
        } else if (font.sampler.handle != VK_NULL_HANDLE) {
            updateCombinedImageSampler(
                vk.device, pipeline.descriptorSet, 1, &font.sampler, 1
            );
        }
    }

    // NOTE(jan): Start recording commands.
    VkCommandBuffer cmds = {};
    createCommandBuffers(vk.device, vk.cmdPool, 1, &cmds);
    beginFrameCommandBuffer(cmds);

    // NOTE(jan): Clear colour / depth.
    VkClearValue colorClear;
    colorClear.color.float32[0] = 0.f;
    colorClear.color.float32[1] = 0.f;
    colorClear.color.float32[2] = 0.f;
    colorClear.color.float32[3] = 1.f;
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

    const char* brushOrder[] = {
        "background",
        "icons",
        "control_points",
        "labels",
        "lines",
        "console",
        "text",
    };
    for (auto key: brushOrder) {
        RENDERER_GET(brush, brushes, key);

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

    memoryArenaClear(&frameArena);
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

        Mesh mesh = {
            .info = info,
            // TODO(jan): Calculate this from mesh metadata.
            .vertexSizeInFloats = 8
        };

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

LRESULT __stdcall
WindowProc(
    HWND    window,
    UINT    message,
    WPARAM  wParam,
    LPARAM  lParam
) {
    switch (message) {
        case WM_DESTROY: {
            PostQuitMessage(0);
            break;
        } case WM_KEYDOWN: {
            BOOL repeatFlag = (HIWORD(lParam) & KF_REPEAT) == KF_REPEAT;
            switch (wParam) {
                case VK_ESCAPE: PostQuitMessage(0); break;
                case VK_PRIOR: input.consolePageUp = true; break;
                case VK_NEXT: input.consolePageDown = true; break;
                case VK_RETURN: input.consoleNewLine = true; break;
                case VK_F1: input.consoleToggle = true; break;
                case 'I': {
                    interpolateGlyph = !interpolateGlyph;
                    renderIcon();
                    break;
                }
                case 'D': {
                    debug = !debug;
                    renderIcon();
                    break;
                }
            }
            break;
        } case WM_KEYUP: {
            switch (wParam) {
                case VK_F1: input.consoleToggle = false; break;
                case VK_PRIOR: input.consolePageUp = false; break;
                case VK_NEXT: input.consolePageDown = false; break;
            }
            break;
        } default: {
            return DefWindowProc(window, message, wParam, lParam);
        }
    }
    return 0;
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
    console.show = true;

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

    renderIcon();

    // NOTE(jan): Main loop.
    bool done = false;
    while (!done) {
        // NOTE(jan): Pump WIN32 message queue.
        MSG msg;
        BOOL messageAvailable;
        while(true) {
            messageAvailable = PeekMessage(
                &msg,
                (HWND)nullptr,
                0, 0,
                PM_REMOVE
            );
            if (!messageAvailable) break;
            TranslateMessage(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
            DispatchMessage(&msg);
        }

        doFrame(vk, renderer);
    }

    return 0;
}
