#pragma once

#include <string>
#include <vector>

#include "types.h"

namespace wasmobj {
struct Function {
    std::string name;
    u64 size;
};

struct Data {
    std::string name;
    u64 size;
};

struct Results {
    std::vector<Function> functions;
    std::vector<Data> data;
};

bool ProcessFile(Results &outResults, const char *path);
} // namespace wasmobj