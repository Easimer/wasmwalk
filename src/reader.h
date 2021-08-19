#pragma once

#include "types.h"

struct ReadContext {
    u8 const *start;
    u8 const *cur;
    u8 const *end;

    template <typename T> T read() {
        T ret = *(T *)cur;
        cur += sizeof(T);
        return ret;
    }

    template <typename T> bool canRead(unsigned n = 1) {
        return (cur + n * sizeof(T) < end);
    }
};

u64 readULEB128(ReadContext &ctx);
std::string readString(ReadContext &ctx);
void readLimits(ReadContext &ctx);