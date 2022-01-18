#pragma once

#include <map>
#include <string>
#include <vector>

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

struct TTFGlyph {
    AABox bbox;
    u16 contourCount;
    u16* contourEnds;
    u16 pointCount;
    Vec2* points;
    bool* isOnCurve;
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
    file.offsetTable.tableCount = TTFReadU16(file);
    file.offsetTable.searchRange = TTFReadU16(file);
    file.offsetTable.entrySelector = TTFReadU16(file);
    file.offsetTable.rangeShift = TTFReadU16(file);

    // NOTE(jan): Parse 'head' table
    TTFSeekToTableOrFail("head")
    file.header.version = TTFReadFixed(file);
    file.header.fontRevision = TTFReadFixed(file);
    file.header.checksumAdjust = TTFReadU32(file);
    file.header.magicNumber = TTFReadU32(file);
    if (file.header.magicNumber != 0x5F0F3CF5) {
        ERR("incorrect magic number");
    }
    file.header.flags = TTFReadU16(file);
    file.header.unitsPerEm = TTFReadU16(file);
    file.header.created = TTFReadTimeStamp(file);
    file.header.modified = TTFReadTimeStamp(file);
    file.header.minX = TTFReadS16(file);
    file.header.minY = TTFReadS16(file);
    file.header.maxX = TTFReadS16(file);
    file.header.maxY = TTFReadS16(file);
    file.header.macStyle = TTFReadU16(file);
    file.header.lowestRecPPEM = TTFReadU16(file);
    file.header.fontDirectionHint = TTFReadS16(file);
    file.header.indexToLocFormat = TTFReadS16(file);
    file.header.glyphDataFormat = TTFReadS16(file);
    return true;
}

bool
TTFLoadGlyph(TTFFile& file, u32 index, bool interpolate, MemoryArena* tempArena, MemoryArena* arena, TTFGlyph& result) {
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
    if (contourCount < 0) {
        ERR("compound glyphs not supported");
        return false;
    }
    if (contourCount == 0) {
        ERR("glyph is empty");
        return false;
    }
    s16 minX = TTFReadS16(file);
    s16 minY = TTFReadS16(file);
    s16 maxX = TTFReadS16(file);
    s16 maxY = TTFReadS16(file);

    u16* contourEnds = (u16*)memoryArenaAllocate(arena, sizeof(u16) * contourCount);
    for (int contourIndex = 0; contourIndex < contourCount; contourIndex++) {
        contourEnds[contourIndex] = TTFReadU16(file);
    }

    u16 instructionLength = TTFReadU16(file);
    TTFFileAdvance(file, instructionLength);

    u16 pointCount = contourEnds[0];
    for (int i = 1; i < contourCount; i++) {
        pointCount = max(pointCount, contourEnds[i]);
    }
    pointCount++;

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

    bool* isOnCurve = (bool*)memoryArenaAllocate(arena, sizeof(bool) * pointCount);
    memset(isOnCurve, 0, sizeof(bool) * pointCount);

    for (int pointIndex = 0; pointIndex < pointCount; pointIndex++) {
        isOnCurve[pointIndex] = (flags[pointIndex] & TTF_FLAG_ON_CURVE) ? true : false;
    }

    Vec2* points = (Vec2*)memoryArenaAllocate(arena, sizeof(Vec2) * pointCount);
    memset(points, 0, sizeof(Vec2) * pointCount);

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
            Vec2* point = points + pointIndex;
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
            Vec2* point = points + pointIndex;
            point->y = y;
            pointIndex++;
        }
    }

    if (!interpolate) {
        result.bbox.x0 = minX;
        result.bbox.x1 = maxX;
        result.bbox.y0 = minY;
        result.bbox.y1 = maxY;
        result.contourCount = contourCount;
        result.contourEnds = contourEnds;
        result.pointCount = pointCount;
        result.points = points;
        result.isOnCurve = isOnCurve;

        file.position = oldPosition;

        return true;
    }

    int totalPointsToAdd = 0;
    for (int pointIndex = 0; pointIndex < pointCount - 1; pointIndex++) {
        int nextPointIndex = pointIndex + 1;

        int pointFlags = flags[pointIndex];
        int nextPointFlags = flags[nextPointIndex];

        bool pointOnCurve = (pointFlags & TTF_FLAG_ON_CURVE) > 0;
        bool nextPointOnCurve = (nextPointFlags & TTF_FLAG_ON_CURVE) > 0;

        if ((!pointOnCurve && !nextPointOnCurve) || (pointOnCurve && nextPointOnCurve)) totalPointsToAdd++;
    }
    int contourStart = 0;
    for (int contourIndex = 0; contourIndex < contourCount; contourIndex++) {
        int contourEnd = contourEnds[contourIndex];

        int pointFlags = flags[contourEnd];
        int nextPointFlags = flags[contourStart];

        bool pointOnCurve = (pointFlags & TTF_FLAG_ON_CURVE) > 0;
        bool nextPointOnCurve = (nextPointFlags & TTF_FLAG_ON_CURVE) > 0;

        if ((!pointOnCurve && !nextPointOnCurve) || (pointOnCurve && nextPointOnCurve)) totalPointsToAdd++;

        contourStart = contourEnd + 1;
    }

    umm newPointCount = pointCount + totalPointsToAdd;

    u16* newContourEnds = (u16*)memoryArenaAllocate(arena, sizeof(u16) * contourCount);
    for (int contourIndex = 0; contourIndex < contourCount; contourIndex++) {
        newContourEnds[contourIndex] = contourEnds[contourIndex];
    }

    bool* newIsOnCurve = (bool*)memoryArenaAllocate(arena, sizeof(bool) * newPointCount);
    memset(newIsOnCurve, 0, sizeof(bool) * newPointCount);

    Vec2* newPoints = (Vec2*)memoryArenaAllocate(arena, sizeof(Vec2) * newPointCount);
    memset(newPoints, 0, sizeof(Vec2) * newPointCount);

    int contourIndex = 0;
    int newPointIndex = 0;
    contourStart = 0;
    for (int contourIndex = 0; contourIndex < contourCount; contourIndex++) {
        int contourEnd = contourEnds[contourIndex];

        for (int pointIndex = contourStart; pointIndex < contourEnd; pointIndex++) {
            int contourEnd = contourEnds[contourIndex];
            if (pointIndex > contourEnd) contourIndex++;

            Vec2 point = points[pointIndex];
            newPoints[newPointIndex] = point;
            newIsOnCurve[newPointIndex] = isOnCurve[pointIndex];
            newPointIndex++;

            int nextPointIndex = pointIndex + 1;
            Vec2 nextPoint = points[nextPointIndex];

            int pointFlags = flags[pointIndex];
            int nextPointFlags = flags[nextPointIndex];

            bool pointOnCurve = (pointFlags & TTF_FLAG_ON_CURVE) > 0;
            bool nextPointOnCurve = (nextPointFlags & TTF_FLAG_ON_CURVE) > 0;

            if (!pointOnCurve && !nextPointOnCurve) {
                Vec2 newPoint = {};
                vectorInterpolate(point, nextPoint, .5f, newPoint);
                newPoints[newPointIndex] = newPoint;
                newIsOnCurve[newPointIndex] = true;
                newPointIndex++;

                for (int i = contourIndex; i < contourCount; i++) newContourEnds[i]++;
            } else if (pointOnCurve && nextPointOnCurve) {
                Vec2 newPoint = {};
                vectorInterpolate(point, nextPoint, .5f, newPoint);
                newPoints[newPointIndex] = newPoint;
                newIsOnCurve[newPointIndex] = false;
                newPointIndex++;

                for (int i = contourIndex; i < contourCount; i++) newContourEnds[i]++;
            }
        }

        Vec2 point = points[contourEnd];
        newPoints[newPointIndex] = point;
        newIsOnCurve[newPointIndex] = isOnCurve[contourEnd];
        newPointIndex++;

        Vec2 nextPoint = points[contourStart];

        int pointFlags = flags[contourEnd];
        int nextPointFlags = flags[contourStart];

        bool pointOnCurve = (pointFlags & TTF_FLAG_ON_CURVE) > 0;
        bool nextPointOnCurve = (nextPointFlags & TTF_FLAG_ON_CURVE) > 0;

        if (!pointOnCurve && !nextPointOnCurve) {
            Vec2 newPoint = {};
            vectorInterpolate(point, nextPoint, .5f, newPoint);
            newPoints[newPointIndex] = newPoint;
            newIsOnCurve[newPointIndex] = true;
            newPointIndex++;

            for (int i = contourIndex; i < contourCount; i++) newContourEnds[i]++;
        } else if (pointOnCurve && nextPointOnCurve) {
            Vec2 newPoint = {};
            vectorInterpolate(point, nextPoint, .5f, newPoint);
            newPoints[newPointIndex] = newPoint;
            newIsOnCurve[newPointIndex] = false;
            newPointIndex++;

            for (int i = contourIndex; i < contourCount; i++) newContourEnds[i]++;
        }

        contourStart = contourEnd + 1;
    }
    // newPoints[newPointIndex] = points[pointCount - 1];
    // newIsOnCurve[newPointIndex] = true;

    result.bbox.x0 = minX;
    result.bbox.x1 = maxX;
    result.bbox.y0 = minY;
    result.bbox.y1 = maxY;
    result.contourCount = contourCount;
    result.contourEnds = newContourEnds;
    result.pointCount = newPointCount;
    result.points = newPoints;
    result.isOnCurve = newIsOnCurve;

    file.position = oldPosition;

    return true;
}

bool
TTFLoadCodepoint(TTFFile& file, u32 codepoint, bool interpolate, MemoryArena* tempArena, MemoryArena* arena, TTFGlyph& result) {
    TTFSeekToTableOrFail("cmap")
    u16 version = TTFReadU16(file);
    u16 subtableCount = TTFReadU16(file);

    s32 unicodeSubtableOffset = -1;
    for (int subtableIndex = 0; subtableIndex < subtableCount; subtableIndex++) {
        u16 platformID = TTFReadU16(file);
        u16 platformSpecificID = TTFReadU16(file);
        u32 offset = TTFReadU32(file);

        if (platformID == 0) {
            unicodeSubtableOffset = offset;
            break;
        }
    }

    if (unicodeSubtableOffset == -1) return false;

    TTFSeekToTableOrFail("cmap")
    TTFFileAdvance(file, unicodeSubtableOffset);
    u16 format = TTFReadU16(file);
    if (format != 4) {
        ERR("only format 4 cmap tables are supported");
        return false;
    }
    u16 length = TTFReadU16(file);
    u16 language = TTFReadU16(file);
    u16 segCount = TTFReadU16(file) / 2;
    u16 searchRange = TTFReadU16(file);
    u16 entrySelector = TTFReadU16(file);
    u16 rangeShift = TTFReadU16(file);

    u16* endCodes = (u16*)memoryArenaAllocate(tempArena, sizeof(u16) * segCount);
    for (int segmentIndex = 0; segmentIndex < segCount; segmentIndex++) endCodes[segmentIndex] = TTFReadU16(file);

    u16 reservedPad = TTFReadU16(file);

    u16* startCodes = (u16*)memoryArenaAllocate(tempArena, sizeof(u16) * segCount);
    for (int segmentIndex = 0; segmentIndex < segCount; segmentIndex++) startCodes[segmentIndex] = TTFReadU16(file);

    u16* idDeltas = (u16*)memoryArenaAllocate(tempArena, sizeof(u16) * segCount);
    for (int segmentIndex = 0; segmentIndex < segCount; segmentIndex++) idDeltas[segmentIndex] = TTFReadU16(file);

    u16* idRangeOffsets = (u16*)memoryArenaAllocate(tempArena, sizeof(u16) * segCount);
    for (int segmentIndex = 0; segmentIndex < segCount; segmentIndex++) idRangeOffsets[segmentIndex] = TTFReadU16(file);

    u16 segmentIndex = 0;
    while (segmentIndex < segCount) {
        u16 endCode = endCodes[segmentIndex];
        if (endCode >= codepoint) break;
        segmentIndex++;
    }
    u16 startCode = startCodes[segmentIndex];
    u16 idRangeOffset = idRangeOffsets[segmentIndex];
    u16 idDelta = idDeltas[segmentIndex];

    umm glyphIndex;
    if (idRangeOffset == 0) {
        glyphIndex = (idDelta + codepoint) % 65536;
    } else {
        u16 startCode = startCodes[segmentIndex];

        umm glyphIndexOffset = idRangeOffset + sizeof(u16) * (codepoint - startCode) - sizeof(u16) * (segCount - segmentIndex);

        TTFFileAdvance(file, glyphIndexOffset);
        glyphIndex = idDelta + TTFReadU16(file);
    }

    return TTFLoadGlyph(file, glyphIndex, interpolate, tempArena, arena, result);
}
