#pragma once

#include <map>
#include <string>
#include <vector>

#include "Logging.cpp"
#include "FileSystem.cpp"
#include "Types.h"

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

};

struct TTFFile {
    u8* data;
    umm length;
    umm position;

    TTFOffsetTable offsetTable;
    std::map<std::string, TTFTableHeader> tableHeaders;
    TTFHeader header;
};

inline void
TTFFileSeek(TTFFile& file, u32 offset) {
    if (offset >= file.length) {
        FATAL("can't seek to %u of TTF file, unexpected EOF", offset);
    }
    file.position = offset;
}

inline TTFTableHeader&
TTFGetTableHeader(TTFFile& file, const char* tableName) {
    if (!file.tableHeaders.contains(tableName)) {
        FATAL("no %s table found in TTF file", tableName);
    }
    return file.tableHeaders.at(tableName);
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
    u16 bytes = TTFReadU32(file);
    return (f64)bytes / (f64)(1 << 16);
}

void
TTFLoadFromPath(const char* path) {
    std::vector<char> contents = readFile(path);
    TTFFile file = {
        .data = (u8*)contents.data(),
        .length = contents.size(),
        .position = 0
    };

    file.offsetTable.scalarType = TTFReadU32(file);
    INFO("sfnt type: %u", file.offsetTable.scalarType);

    file.offsetTable.tableCount = TTFReadU16(file);
    INFO("table count: %u", file.offsetTable.tableCount);

    file.offsetTable.searchRange = TTFReadU16(file);
    INFO("search range: %u", file.offsetTable.searchRange);

    file.offsetTable.entrySelector = TTFReadU16(file);
    INFO("entry selector: %u", file.offsetTable.entrySelector);

    file.offsetTable.rangeShift = TTFReadU16(file);
    INFO("range shift: %u", file.offsetTable.rangeShift);

    for (int tableIndex; tableIndex < file.offsetTable.tableCount; tableIndex++) {
        TTFTableHeader header = {};

        header.tag[0] = (char)TTFReadU8(file);
        header.tag[1] = (char)TTFReadU8(file);
        header.tag[2] = (char)TTFReadU8(file);
        header.tag[3] = (char)TTFReadU8(file);
        INFO("tag: %s", header.tag);

        header.checkSum = TTFReadU32(file);
        INFO("checkSum: %u", header.checkSum);

        header.offset = TTFReadU32(file);
        INFO("offset: %u", header.offset);

        header.length = TTFReadU32(file);
        INFO("length: %u", header.length);

        file.tableHeaders.insert({ header.tag, header });
    }

    // NOTE(jan): head table
    {
        TTFTableHeader headHeader = TTFGetTableHeader(file, "head");
        TTFFileSeek(file, headHeader.offset);

    }
}
