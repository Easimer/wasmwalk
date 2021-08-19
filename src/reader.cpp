#include "reader.h"

static u64 decodeULEB128(const u8 *p, unsigned *n = nullptr,
                         const u8 *end = nullptr,
                         const char **error = nullptr) {
    const u8 *orig_p = p;
    u64 value = 0;
    unsigned shift = 0;
    if (error)
        *error = nullptr;
    do {
        if (p == end) {
            if (error)
                *error = "malformed uleb128, extends past end";
            if (n)
                *n = (unsigned)(p - orig_p);
            return 0;
        }
        uint64_t slice = *p & 0x7f;
        if ((shift >= 64 && slice != 0) || slice << shift >> shift != slice) {
            if (error)
                *error = "uleb128 too big for uint64";
            if (n)
                *n = (unsigned)(p - orig_p);
            return 0;
        }
        value += slice << shift;
        shift += 7;
    } while (*p++ >= 128);
    if (n)
        *n = (unsigned)(p - orig_p);
    return value;
}

u64 readULEB128(ReadContext &ctx) {
    unsigned Count;
    const char *error = nullptr;
    auto result = decodeULEB128(ctx.cur, &Count, ctx.end, &error);
    if (error != nullptr) {
        fprintf(stderr, "Error in readULEB128: %s\n", error);
        std::abort();
    }
    ctx.cur += Count;
    return result;
}

std::string readString(ReadContext &ctx) {
    auto lenString = readULEB128(ctx);
    if (ctx.cur + lenString > ctx.end) {
        fprintf(stderr, "Error in readString: string is too long\n");
        std::abort();
    }
    auto ret = std::string((const char *)ctx.cur, lenString);
    ctx.cur += lenString;
    return ret;
}

void readLimits(ReadContext &ctx) {
    auto flags = readULEB128(ctx);
    readULEB128(ctx);
    if (flags & 1) {
        readULEB128(ctx);
    }
}
