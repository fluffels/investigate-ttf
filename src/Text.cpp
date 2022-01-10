#include "stb/stb_truetype.h"

struct TextVertex {
    Vec2 position;
    Vec2 tex;
};

static stbtt_bakedchar bakedChars[96];

static u32 textLineCount = 0;
static VulkanPipeline textPipeline;
static VulkanBuffer textVertexBuffer;
static VulkanBuffer textIndexBuffer;
static u32 textMaxCharacters = 25 * 100;
static u32 textVertexBufferSize = sizeof(TextVertex) * 4 * textMaxCharacters;
static u32 textIndexBufferSize = sizeof(u32) * 6 * textMaxCharacters;
static u32 textCurrentCharacter = 0;
static u32 textVertexCount = 0;
static u32 textIndexCount = 0;
static TextVertex *textVertices = nullptr;
static TextVertex* textCurrentVertex = nullptr;
static u32* textIndices = nullptr;
static u32* textCurrentIndex = nullptr;
static char* textBuffer = nullptr;

void startText() {
    textCurrentCharacter = 0;
    textLineCount = 0;
    textVertexCount = 0;
    textIndexCount = 0;
    textCurrentVertex = textVertices;
    textCurrentIndex = textIndices;
}

#define println(fmt, ...) {\
    auto count = sprintf_s( \
        textBuffer + textCurrentCharacter,\
        textMaxCharacters - textCurrentCharacter,\
        fmt,\
        __VA_ARGS__\
    ); \
    LERROR(count == -1)\
    textCurrentCharacter += count + 1;\
    textLineCount++;\
}

void initText(
    Vulkan& vk
) {
    textBuffer = (char*)malloc(textMaxCharacters);
    // Load fonts.
    VulkanSampler fontAtlas = {};
    {
        auto fontFile = openFile("fonts/FiraCode-Bold.ttf", "r");
        auto ttfBuffer = new u8[1 << 20];
        fread(ttfBuffer, 1, 1<<20, fontFile);
        const u32 fontWidth = 512;
        const u32 fontHeight = 512;
        u8 bitmap[fontWidth * fontHeight];
        stbtt_BakeFontBitmap(
            ttfBuffer,
            0,
            32.f,
            bitmap,
            fontWidth,
            fontHeight,
            32, 96,
            bakedChars
        );
        delete[] ttfBuffer;
        uploadTexture(
            vk,
            fontWidth,
            fontHeight,
            VK_FORMAT_R8_UNORM,
            bitmap,
            fontWidth * fontHeight,
            fontAtlas
        );
    }
    initVKPipelineNoCull(
        vk,
        "text",
        textPipeline
    );
    updateUniformBuffer(
        vk.device,
        textPipeline.descriptorSet,
        0,
        vk.uniforms.handle
    );
    updateCombinedImageSampler(
        vk.device,
        textPipeline.descriptorSet,
        1,
        &fontAtlas,
        1
    );
    createVertexBuffer(
        vk.device,
        vk.memories,
        vk.queueFamily,
        textVertexBufferSize,
        textVertexBuffer
    );
    textVertices = (TextVertex*)mapMemory(vk.device, textVertexBuffer.memory);
    createIndexBuffer(
        vk.device,
        vk.memories,
        vk.queueFamily,
        textIndexBufferSize,
        textIndexBuffer
    );
    textIndices = (u32*)mapMemory(vk.device, textIndexBuffer.memory);
}

void endText(
    Vulkan& vk,
    VkCommandBuffer cmd
) {
    vkCmdBindPipeline(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        textPipeline.handle
    );
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        textPipeline.layout,
        0, 1, &textPipeline.descriptorSet,
        0, nullptr
    );
    auto c = (char *) textBuffer;
    for (int line = 0; line < textLineCount; line++) {
        float xPos = 16.f;
        float yPos = float(line+1) * 32.f;
        while (*c) {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(bakedChars, 512, 512, *c - 32, &xPos, &yPos, &q, 1);
            u32 baseIndex = textVertexCount;
            {
                textCurrentVertex->tex.x = q.s0;
                textCurrentVertex->tex.y = q.t1;
                textCurrentVertex->position.x = q.x0;
                textCurrentVertex->position.y = q.y1;
                textCurrentVertex++;
            }
            {
                textCurrentVertex->tex.x = q.s1;
                textCurrentVertex->tex.y = q.t1;
                textCurrentVertex->position.x = q.x1;
                textCurrentVertex->position.y = q.y1;
                textCurrentVertex++;
            }
            {
                textCurrentVertex->tex.x = q.s1;
                textCurrentVertex->tex.y = q.t0;
                textCurrentVertex->position.x = q.x1;
                textCurrentVertex->position.y = q.y0;
                textCurrentVertex++;
            }
            {
                textCurrentVertex->tex.x = q.s0;
                textCurrentVertex->tex.y = q.t0;
                textCurrentVertex->position.x = q.x0;
                textCurrentVertex->position.y = q.y0;
                textCurrentVertex++;
            }
            textCurrentIndex[0] = baseIndex + 0;
            textCurrentIndex[1] = baseIndex + 1;
            textCurrentIndex[2] = baseIndex + 2;
            textCurrentIndex[3] = baseIndex + 2;
            textCurrentIndex[4] = baseIndex + 3;
            textCurrentIndex[5] = baseIndex + 0;
            textIndexCount += 6;
            textCurrentIndex += 6;
            textVertexCount += 4;
            c++;
        }
        // Read past the \0 that ended the previous line.
        c++;
    }
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(
        cmd,
        0, 1,
        &textVertexBuffer.handle,
        offsets
    );
    vkCmdBindIndexBuffer(
        cmd,
        textIndexBuffer.handle,
        0,
        VK_INDEX_TYPE_UINT32 //FIXME: should be 16
    );
    vkCmdDrawIndexed(
        cmd,
        textIndexCount,
        1, 0, 0, 0
    );
}
