#pragma once
// Extracts one file from a .7z archive held in memory, using the public-domain 7z decoder of the
// LZMA SDK (third_party/lzma). Portable C++: it does not touch the file system.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../third_party/lzma/7z.h"
#include "../third_party/lzma/7zAlloc.h"
#include "../third_party/lzma/7zCrc.h"

namespace sevenzip {

struct MemoryStream {
    ILookInStream vt; // must stay the first member: the SDK hands back a pointer to it
    const uint8_t *data;
    size_t size;
    size_t pos;
};

inline MemoryStream *self(ILookInStreamPtr p) {
    return reinterpret_cast<MemoryStream *>(const_cast<ILookInStream *>(p));
}
inline SRes look(ILookInStreamPtr p, const void **buf, size_t *size) {
    auto s = self(p);
    size_t avail = s->size - s->pos;
    if (*size > avail)
        *size = avail;
    *buf = s->data + s->pos;
    return SZ_OK;
}
inline SRes skip(ILookInStreamPtr p, size_t offset) {
    auto s = self(p);
    if (offset > s->size - s->pos)
        return SZ_ERROR_INPUT_EOF;
    s->pos += offset;
    return SZ_OK;
}
inline SRes read(ILookInStreamPtr p, void *buf, size_t *size) {
    auto s = self(p);
    size_t n = *size < s->size - s->pos ? *size : s->size - s->pos;
    if (n)
        std::memcpy(buf, s->data + s->pos, n);
    s->pos += n;
    *size = n;
    return SZ_OK;
}
inline SRes seek(ILookInStreamPtr p, Int64 *pos, ESzSeek origin) {
    auto s = self(p);
    Int64 from = origin == SZ_SEEK_SET   ? 0
                 : origin == SZ_SEEK_CUR ? static_cast<Int64>(s->pos)
                                         : static_cast<Int64>(s->size);
    Int64 target = from + *pos;
    if (target < 0 || target > static_cast<Int64>(s->size))
        return SZ_ERROR_PARAM;
    s->pos = static_cast<size_t>(target);
    *pos = target;
    return SZ_OK;
}

inline const char *describe(SRes r) {
    switch (r) {
    case SZ_ERROR_DATA:
        return "dati danneggiati";
    case SZ_ERROR_MEM:
        return "memoria insufficiente";
    case SZ_ERROR_CRC:
        return "CRC non valido";
    case SZ_ERROR_UNSUPPORTED:
        return "metodo di compressione non supportato";
    case SZ_ERROR_NO_ARCHIVE:
        return "non è un archivio 7z";
    case SZ_ERROR_ARCHIVE:
        return "archivio non valido";
    case SZ_ERROR_INPUT_EOF:
        return "archivio troncato";
    default:
        return "errore di estrazione";
    }
}

inline char16_t lowerAscii(char16_t c) {
    return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c + 32) : c;
}

// Finds the first file whose name (last path component, case-insensitive ASCII) equals `wanted`
// and returns its bytes. `foundPath` receives the full path inside the archive.
inline bool extractFile(const std::vector<uint8_t> &archive, const std::u16string &wanted,
                        std::vector<uint8_t> &out, std::u16string &foundPath, std::string &error) {
    static const ISzAlloc alloc = {SzAlloc, SzFree};
    static bool crcReady = false;
    if (!crcReady) {
        CrcGenerateTable();
        crcReady = true;
    }
    MemoryStream stream{};
    stream.vt.Look = look;
    stream.vt.Skip = skip;
    stream.vt.Read = read;
    stream.vt.Seek = seek;
    stream.data = archive.data();
    stream.size = archive.size();
    stream.pos = 0;

    CSzArEx db;
    SzArEx_Init(&db);
    SRes r = SzArEx_Open(&db, &stream.vt, &alloc, &alloc);
    if (r != SZ_OK) {
        error = describe(r);
        SzArEx_Free(&db, &alloc);
        return false;
    }
    std::u16string want;
    for (char16_t c : wanted)
        want += lowerAscii(c);
    bool found = false;
    std::vector<UInt16> name;
    for (UInt32 i = 0; i < db.NumFiles && !found; ++i) {
        if (SzArEx_IsDir(&db, i))
            continue;
        size_t len = SzArEx_GetFileNameUtf16(&db, i, nullptr);
        name.assign(len, 0);
        SzArEx_GetFileNameUtf16(&db, i, name.data());
        std::u16string path(name.begin(), name.end());
        while (!path.empty() && path.back() == 0)
            path.pop_back();
        auto slash = path.find_last_of(u"/\\");
        std::u16string leaf = slash == std::u16string::npos ? path : path.substr(slash + 1);
        std::u16string low;
        for (char16_t c : leaf)
            low += lowerAscii(c);
        if (low != want)
            continue;
        UInt32 blockIndex = 0xFFFFFFFF;
        Byte *buffer = nullptr;
        size_t bufferSize = 0, offset = 0, processed = 0;
        r = SzArEx_Extract(&db, &stream.vt, i, &blockIndex, &buffer, &bufferSize, &offset, &processed, &alloc,
                           &alloc);
        if (r == SZ_OK) {
            out.assign(buffer + offset, buffer + offset + processed);
            foundPath = path;
            found = true;
        } else
            error = describe(r);
        ISzAlloc_Free(&alloc, buffer);
        if (r != SZ_OK)
            break;
    }
    SzArEx_Free(&db, &alloc);
    if (!found && error.empty())
        error = "file non presente nell'archivio";
    return found;
}

} // namespace sevenzip
