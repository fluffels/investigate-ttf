#pragma once

#include <map>
#include <string>
#include <urlmon.h>
#include <vcruntime_string.h>
#include <vector>
#include <wtypes.h>

#include "Logging.cpp"
#include "FileSystem.cpp"
#include "MathLib.h"
#include "Memory.cpp"
#include "Types.h"
#include "Memory.h"

enum GLYPH_FLAGS {
    TTF_FLAG_ON_CURVE = 1,
    TTF_FLAG_X_IS_BYTE = 2,
    TTF_FLAG_Y_IS_BYTE = 4,
    TTF_FLAG_REPEAT = 8,
    TTF_FLAG_X_DELTA = 16,
    TTF_FLAG_Y_DELTA = 32,
};

#pragma pack(push, 1)
struct TTFOffsetTable {
    u32 scalarType;
    u16 tableCount;
    u16 searchRange;
    u16 entrySelector;
    u16 rangeShift;
};

struct TTFTableHeader {
    char tag[4];
    u32 checkSum;
    u32 offset;
    u32 length;
};

struct TTFHeader {
    f64 version;
    f64 fontRevision;
    u32 checksumAdjust;
    u32 magicNumber;
    u16 flags;
    u16 unitsPerEm;
    u64 created;
    u64 modified;
    s16 minX;
    s16 minY;
    s16 maxX;
    s16 maxY;
    u16 macStyle;
    u16 lowestRecPPEM;
    s16 fontDirectionHint;
    s16 indexToLocFormat;
    s16 glyphDataFormat;
};
#pragma pack(pop)

struct TTFFile {
    u8* data;
    umm length;
    umm position;

    TTFOffsetTable offsetTable;
    TTFHeader header;
};

inline void
TTFFileSeek(TTFFile& file, u32 offset) {
    if (offset >= file.length) {
        FATAL("can't seek to %u of TTF file, unexpected EOF", offset);
    }
    file.position = offset;
}

inline void
TTFFileAdvance(TTFFile& file, umm offset) {
    TTFFileSeek(file, file.position + offset);
}

inline u8
TTFReadU8(TTFFile& file) {
    if (file.position >= file.length) FATAL("unexpected EOF");
    const u8 result = file.data[file.position++];
    return result;
}

inline u16
TTFReadU16(TTFFile& file) {
    if (file.position >= file.length - 1) FATAL("unexpected EOF");
    u16 result = 0;
    result |= file.data[file.position++] << 8;
    result |= file.data[file.position++] << 0;
    return result;
}

inline s16
TTFReadS16(TTFFile& file) {
    if (file.position >= file.length - 1) FATAL("unexpected EOF");
    s16 result = 0;
    result |= file.data[file.position++] << 8;
    result |= file.data[file.position++] << 0;
    return result;
}

inline u32
TTFReadU32(TTFFile& file) {
    if (file.position >= file.length - 3) FATAL("unexpected EOF");
    u32 result = 0;
    result |= file.data[file.position++] << 24;
    result |= file.data[file.position++] << 16;
    result |= file.data[file.position++] << 8;
    result |= file.data[file.position++] << 0;
    return result;
}

inline f64
TTFReadFixed(TTFFile& file) {
    u32 bytes = TTFReadU32(file);
    return (f64)bytes / (f64)(1 << 16);
}

inline u64
TTFReadTimeStamp(TTFFile& file) {
    u64 result = 0;
    result += TTFReadU32(file) * 0x100000000;
    result += TTFReadU32(file);
    return result;
}

bool
TTFSeekToTable(TTFFile& file, const char tag[4]) {
    umm position = file.position;
    file.position = sizeof(TTFOffsetTable);

    for (int tableIndex = 0; tableIndex < file.offsetTable.tableCount; tableIndex++) {
        TTFTableHeader header = {};

        header.tag[0] = (char)TTFReadU8(file);
        header.tag[1] = (char)TTFReadU8(file);
        header.tag[2] = (char)TTFReadU8(file);
        header.tag[3] = (char)TTFReadU8(file);

        header.checkSum = TTFReadU32(file);
        // INFO("checkSum: %u", header.checkSum);
        header.offset = TTFReadU32(file);
        // INFO("offset: %u", header.offset);
        header.length = TTFReadU32(file);
        // INFO("length: %u", header.length);

        if (strncmp(header.tag, tag, 4) == 0) {
            file.position = header.offset;
            return true;
        }
    }

    ERR("no %.*s table", tag, 4);
    file.position = position;
    return false;
}

#define TTFSeekToTableOrFail(tag) if (!TTFSeekToTable(file, tag)) return false;

bool
TTFLoadFromPath(const char* path, MemoryArena* arena, TTFFile& file) {
    std::vector<char> contents = readFile(path);
    file.length = contents.size();
    file.data = (u8*)memoryArenaAllocate(arena, file.length);
    file.position = 0;
    memcpy(file.data, contents.data(), file.length);

    // NOTE(jan): Parse offset table
    file.offsetTable.scalarType = TTFReadU32(file);
    // INFO("sfnt type: %u", file.offsetTable.scalarType);
    file.offsetTable.tableCount = TTFReadU16(file);
    // INFO("table count: %u", file.offsetTable.tableCount);
    file.offsetTable.searchRange = TTFReadU16(file);
    // INFO("search range: %u", file.offsetTable.searchRange);
    file.offsetTable.entrySelector = TTFReadU16(file);
    // INFO("entry selector: %u", file.offsetTable.entrySelector);
    file.offsetTable.rangeShift = TTFReadU16(file);
    // INFO("range shift: %u", file.offsetTable.rangeShift);

    // NOTE(jan): Parse 'head' table
    TTFSeekToTableOrFail("head")

    file.header.version = TTFReadFixed(file);
    INFO("version: %f", file.header.version);
    
    file.header.fontRevision = TTFReadFixed(file);
    INFO("fontRevision: %f", file.header.fontRevision);

    file.header.checksumAdjust = TTFReadU32(file);
    INFO("checksumAdjust: %u", file.header.checksumAdjust);

    file.header.magicNumber = TTFReadU32(file);
    INFO("magicNumber: %x", file.header.magicNumber);
    if (file.header.magicNumber != 0x5F0F3CF5) {
        ERR("incorrect magic number");
    }

    file.header.flags = TTFReadU16(file);
    INFO("flags: %u", file.header.flags);

    file.header.unitsPerEm = TTFReadU16(file);
    INFO("unitsPerEm: %u", file.header.unitsPerEm);

    file.header.created = TTFReadTimeStamp(file);
    INFO("created: %u", file.header.created);

    file.header.modified = TTFReadTimeStamp(file);
    INFO("modified: %u", file.header.modified);

    file.header.minX = TTFReadS16(file);
    INFO("minX: %d", file.header.minX);

    file.header.minY = TTFReadS16(file);
    INFO("minY: %d", file.header.minY);

    file.header.maxX = TTFReadS16(file);
    INFO("maxX: %d", file.header.maxX);

    file.header.maxY = TTFReadS16(file);
    INFO("maxY: %d", file.header.maxY);

    file.header.macStyle = TTFReadU16(file);
    INFO("macStyle: %u", file.header.macStyle);

    file.header.lowestRecPPEM = TTFReadU16(file);
    INFO("lowestRecPPEM: %u", file.header.lowestRecPPEM);

    file.header.fontDirectionHint = TTFReadS16(file);
    INFO("fontDirectionHint: %d", file.header.fontDirectionHint);

    file.header.indexToLocFormat = TTFReadS16(file);
    INFO("indexToLocFormat: %d", file.header.indexToLocFormat);

    file.header.glyphDataFormat = TTFReadS16(file);
    INFO("glyphDataFormat: %d", file.header.glyphDataFormat);

    return true;
}

bool
TTFLoadGlyph(TTFFile& file, u32 index, MemoryArena* tempArena) {
    umm oldPosition = file.position;

    TTFSeekToTableOrFail("loca")
    umm offsetInGlyphTable = 0;
    if (file.header.indexToLocFormat == 1) {
        TTFFileAdvance(file, index * 4);
        offsetInGlyphTable = TTFReadU32(file);
    } else {
        TTFFileAdvance(file, index * 2);
        offsetInGlyphTable = TTFReadU16(file) * 2;
    }

    TTFSeekToTableOrFail("glyf")
    TTFFileAdvance(file, offsetInGlyphTable);
    s16 contourCount = TTFReadS16(file);
    if (contourCount < -1) {
        ERR("invalid contour count");
        return false;
    }
    if (contourCount == -1) {
        ERR("compound glyphs not supported");
        return false;
    }
    if (contourCount == 0) {
        ERR("glyph is empty");
        return false;
    }
    INFO("contourCount: %d", contourCount);
    s16 minX = TTFReadS16(file);
    INFO("minX: %d", minX);
    s16 minY = TTFReadS16(file);
    INFO("minY: %d", minY);
    s16 maxX = TTFReadS16(file);
    INFO("maxX: %d", maxX);
    s16 maxY = TTFReadS16(file);
    INFO("maxY: %d", maxY);

    u16* contourEnds = (u16*)memoryArenaAllocate(tempArena, sizeof(u16) * contourCount);
    for (int contourIndex = 0; contourIndex < contourCount; contourIndex++) {
        contourEnds[contourIndex] = TTFReadU16(file);
    }
    INFO("contourEnds:     %5d %5d %5d %5d", contourEnds[0], contourEnds[1], contourEnds[2], contourEnds[3]);

    u16 instructionLength = TTFReadU16(file);
    TTFFileAdvance(file, instructionLength);

    u16 pointCount = contourEnds[0];
    for (int i = 1; i < contourCount; i++) {
        pointCount = max(pointCount, contourEnds[i]);
    }
    pointCount++;
    INFO("pointCount: %d", pointCount);

    u8* flags = (u8*)memoryArenaAllocate(tempArena, sizeof(u8) * pointCount);
    memset(flags, 0, sizeof(u8) * pointCount);

    umm flagIndex = 0;
    while (flagIndex < pointCount) {
        u8 flag = TTFReadU8(file);
        flags[flagIndex++] = flag;

        u8 repeatCount = flag & TTF_FLAG_REPEAT ? TTFReadU8(file) : 0;
        while (repeatCount > 0) {
            flags[flagIndex++] = flag;
            repeatCount--;
        }
    }
    INFO("flags:           %5d %5d %5d %5d %5d", flags[0], flags[1], flags[2], flags[3], flags[4]);

    Vec2i* points = (Vec2i*)memoryArenaAllocate(tempArena, sizeof(Vec2i) * pointCount);
    memset(points, 0, sizeof(Vec2i) * pointCount);

    {
        int pointIndex = 0;
        s32 x = 0;
        while (pointIndex < pointCount) {
            u8 flag = flags[pointIndex];
            if (flag & TTF_FLAG_X_IS_BYTE) {
                if (flag & TTF_FLAG_X_DELTA) {
                    x += TTFReadU8(file);
                } else {
                    x -= TTFReadU8(file);
                }
            } else {
                if (~flag & TTF_FLAG_X_DELTA) {
                    x += TTFReadS16(file);
                }
            }
            Vec2i* point = points + pointIndex;
            point->x = x;
            pointIndex++;
        }
    }

    {
        int pointIndex = 0;
        s32 y = 0;
        while (pointIndex < pointCount) {
            u8 flag = flags[pointIndex];
            if (flag & TTF_FLAG_Y_IS_BYTE) {
                if (flag & TTF_FLAG_Y_DELTA) {
                    y += TTFReadU8(file);
                } else {
                    y -= TTFReadU8(file);
                }
            } else {
                if (~flag & TTF_FLAG_Y_DELTA) {
                    y += TTFReadS16(file);
                }
            }
            Vec2i* point = points + pointIndex;
            point->y = y;
            pointIndex++;
        }
    }

    INFO("points.x:        %5d %5d %5d %5d %5d", points[0].x, points[1].x, points[2].x, points[3].x, points[4].x);
    for (int i = 0; i < pointCount; i++) {
        INFO("point[%d].x: %d", i, points[i].x);
    }
    INFO("points.y:        %5d %5d %5d %5d %5d", points[0].y, points[1].y, points[2].y, points[3].y, points[4].y);
    for (int i = 0; i < pointCount; i++) {
        INFO("point[%d].y: %d", i, points[i].y);
    }

    file.position = oldPosition;
    return true;
}
