#include <unordered_map>
#include <memory>
#include <cstring>

#include "reader.h"
#include "wasmobj.h"
#include "wasm.h"

struct FunctionType {
    u32 type;
};

struct Function {
    u32 codeOffset;
    u32 size;
    std::string name;
};

struct Data {
    u64 size;
    std::string name;
};

struct Signature {
    std::vector<u32> params;
    std::vector<u32> returns;
};

struct Export {
    std::string name;
    u8 kind;
    u64 index;
};

struct Import {
    std::string module;
    std::string field;
    u8 kind;
    u64 sigIndex;
};

struct ParseContext {
    std::string currentSectionName;

    std::vector<FunctionType> functionTypes;
    std::vector<Function> functions;
    std::vector<Signature> signatures;
    std::vector<Export> exports;
    std::vector<Import> imports;
    std::vector<Data> data;

    u64 numImportedFunctions = 0;
};

static bool readSectionExport(ReadContext& ctx, ParseContext& pctx) {
    auto count = readULEB128(ctx);
    pctx.exports.reserve(count);
    for (u64 i = 0; i < count; ++i) {
        Export ex;
        ex.name = readString(ctx);
        ex.kind = ctx.read<u8>();
        ex.index = readULEB128(ctx);
        pctx.exports.push_back(std::move(ex));
    }

    if (ctx.cur != ctx.end) {
        fprintf(stderr, "Export section ended prematurely %p != %p, off by %lld\n", ctx.cur, ctx.end, ctx.end - ctx.cur);
        return false;
    }

    return true;
}

static bool readSectionImport(ReadContext& ctx, ParseContext& pctx) {
    auto count = readULEB128(ctx);
    pctx.imports.reserve(count);
    for (u64 i = 0; i < count; ++i) {
        auto im = Import();
        im.module = readString(ctx);
        im.field = readString(ctx);
        im.kind = ctx.read<u8>();
        switch (im.kind) {
        case WASM_EXTERNAL_FUNCTION:
            im.sigIndex = readULEB128(ctx);
            pctx.numImportedFunctions++;
            break;
        case WASM_EXTERNAL_TABLE:
            ctx.read<u8>();
            readLimits(ctx);
            break;
        case WASM_EXTERNAL_MEMORY:
            readLimits(ctx);
            break;
        case WASM_EXTERNAL_GLOBAL:
            ctx.read<u8>();
            readULEB128(ctx);
            break;
        case WASM_EXTERNAL_EVENT:
            readULEB128(ctx);
            readULEB128(ctx);
            break;
        }
        pctx.imports.push_back(std::move(im));
    }

    if (ctx.cur != ctx.end) {
        fprintf(stderr, "Import section ended prematurely %p != %p, off by %lld\n", ctx.cur, ctx.end, ctx.end - ctx.cur);
        return false;
    }

    return true;
}

static bool readSectionType(ReadContext &ctx, ParseContext &pctx) {
    auto count = readULEB128(ctx);
    pctx.signatures.reserve(count);
    while (count--) {
        Signature sig;
        auto form = ctx.read<u8>();
        if (form != 96) {
            fprintf(stderr, "invalid signature type %u\n", form);
            return false;
        }

        auto paramCount = readULEB128(ctx);
        while (paramCount--) {
            u32 paramType = ctx.read<u8>();
            sig.params.push_back(paramType);
        }

        auto returnCount = readULEB128(ctx);
        while (returnCount--) {
            u32 paramType = ctx.read<u8>();
            sig.returns.push_back(paramType);
        }

        pctx.signatures.emplace_back(std::move(sig));
    }

    if (ctx.cur != ctx.end) {
        fprintf(stderr, "Type section ended prematurely %p != %p, off by %lld\n", ctx.cur, ctx.end, ctx.end - ctx.cur);
        return false;
    }

    return true;
}

static bool readSectionFunction(ReadContext &ctx, ParseContext &pctx) {
    auto count = readULEB128(ctx);
    pctx.functionTypes.reserve(count);
    pctx.functions.resize(count);

    auto numTypes = pctx.signatures.size();
    while (count--) {
        auto type = readULEB128(ctx);
        if (type >= numTypes) {
            fprintf(stderr, "Invalid function type %llu\n", type);
            return false;
        }
        pctx.functionTypes.push_back({(u32)type});
    }

    if (ctx.cur != ctx.end) {
        fprintf(stderr, "Function section ended prematurely %p != %p, off by %lld\n", ctx.cur, ctx.end, ctx.end - ctx.cur);
        return false;
    }
    return true;
}

static bool readSectionCode(ReadContext &ctx, ParseContext &pctx) {
    auto functionCount = readULEB128(ctx);
    if (functionCount != pctx.functionTypes.size()) {
        fprintf(stderr, "Invalid function count %llu != %llu\n", functionCount,
                pctx.functionTypes.size());
        return false;
    }
    for (u64 i = 0; i < functionCount; ++i) {
        auto &function = pctx.functions[i];
        auto &functionType = pctx.functionTypes[i];
        auto &functionSignature = pctx.signatures[functionType.type];
        auto functionStart = ctx.cur;
        auto size = readULEB128(ctx);
        auto functionEnd = ctx.cur + size;

        auto numLocalDecls = readULEB128(ctx);

        while (numLocalDecls--) {
            auto count = readULEB128(ctx);
            auto type = ctx.read<u8>();
        }

        pctx.functions[i].codeOffset = functionStart - ctx.start;
        pctx.functions[i].size = functionEnd - functionStart;

        auto bodySize = functionEnd - ctx.cur;
        ctx.cur = functionEnd;
    }

    if (ctx.cur != ctx.end) {
        fprintf(stderr, "Code section ended prematurely %p != %p, off by %lld\n", ctx.cur, ctx.end, ctx.end - ctx.cur);
        return false;
    }

    return true;
}

static bool readSectionName(ReadContext &ctx, ParseContext &pctx) {
    while (ctx.cur < ctx.end) {
        auto type = ctx.read<u8>();
        auto size = readULEB128(ctx);
        auto subsectionEnd = ctx.cur + size;
        switch (type) {
        case WASM_NAMES_FUNCTION: {
            auto count = readULEB128(ctx);
            while (count--) {
                auto index = readULEB128(ctx);
                auto name = readString(ctx);

                if (type == WASM_NAMES_FUNCTION) {
                    pctx.functions[index].name = std::move(name);
                }
            }
            break;
        }
        }

        ctx.cur = subsectionEnd;
    }
    return true;
}

static bool readLinkingSectionSymtab(ReadContext &ctx, ParseContext &pctx) {
    auto count = readULEB128(ctx);

    while (count--) {
        auto kind = ctx.read<u8>();
        auto flags = readULEB128(ctx);

        auto isDefined = (flags & WASM_SYMBOL_UNDEFINED) == 0;

        switch (kind) {
        case WASM_SYMBOL_TYPE_FUNCTION: {
            auto elemIdx = readULEB128(ctx);
            std::string name;
            if (isDefined) {
                elemIdx -= pctx.numImportedFunctions;
                name = readString(ctx);
                pctx.functions[elemIdx].name = name;
            } else {
                if (flags & WASM_SYMBOL_EXPLICIT_NAME) {
                    name = readString(ctx);
                }
            }

            break;
        }
        case WASM_SYMBOL_TYPE_GLOBAL:
        case WASM_SYMBOL_TYPE_TABLE:
        case WASM_SYMBOL_TYPE_EVENT: {
            auto elemIdx = readULEB128(ctx);
            if (flags & WASM_SYMBOL_EXPLICIT_NAME) {
                auto name = readString(ctx);
            }
            break;
        }
        case WASM_SYMBOL_TYPE_DATA: {
            auto name = readString(ctx);
            if (isDefined) {
                auto idx = readULEB128(ctx);
                auto offset = readULEB128(ctx);
                auto size = readULEB128(ctx);
                pctx.data.push_back({size, name});
            }
            break;
        }
        case WASM_SYMBOL_TYPE_SECTION: {
            auto idx = readULEB128(ctx);
            break;
        }
        default: {
            fprintf(stderr, "Unknown subsection type %u\n", kind);
            return false;
            break;
        }
        }
    }

    return true;
}

static bool readLinkingSection(ReadContext &ctx, ParseContext &pctx) {
    auto version = readULEB128(ctx);
    if (version != 2) {
        fprintf(stderr, "Unexpected linking metadata version %llu\n", version);
        return false;
    }

    auto origEnd = ctx.end;
    while (ctx.cur < origEnd) {
        ctx.end = origEnd;
        auto type = ctx.read<u8>();
        auto size = readULEB128(ctx);
        ctx.end = ctx.cur + size;
        switch (type) {
        case 8:
            if (!readLinkingSectionSymtab(ctx, pctx)) {
                return false;
            }
            break;
        default:
            ctx.cur += size;
            break;
        }
    }
        return true;
}

static bool readSectionCustom(ReadContext &ctx, ParseContext &pctx) {
    if (pctx.currentSectionName == "name") {
        if (!readSectionName(ctx, pctx)) {
            return false;
        }
    } else if (pctx.currentSectionName == "linking") {
        if (!readLinkingSection(ctx, pctx)) {
            return false;
        }
    }
    return true;
}

static bool readSection(ReadContext &ctx, ParseContext &pctx) {
    auto offset = ctx.cur - ctx.start;
    if (!ctx.canRead<u8>()) {
        fprintf(stderr, "Failed to read section type\n");
        return false;
    }

    auto type = ctx.read<u8>();
    auto size = readULEB128(ctx);

    if (size == 0) {
        fprintf(stderr, "Encountered zero-length section of type %u\n", type);
        return false;
    }

    if (ctx.cur + size > ctx.end) {
        fprintf(stderr, "End of the section is past the end of the file\n");
        return false;
    }

    auto endOfSection = ctx.cur + size;

    bool ret = true;

    if (type == WASM_SEC_CUSTOM) {
        pctx.currentSectionName = readString(ctx);
    }

    ReadContext subctx = {ctx.cur, ctx.cur, endOfSection};
    switch (type) {
    case WASM_SEC_EXPORT:
        ret = readSectionExport(subctx, pctx);
        break;
    case WASM_SEC_IMPORT:
        ret = readSectionImport(subctx, pctx);
        break;
    case WASM_SEC_TYPE:
        ret = readSectionType(subctx, pctx);
        break;
    case WASM_SEC_CODE:
        ret = readSectionCode(subctx, pctx);
        break;
    case WASM_SEC_FUNCTION:
        ret = readSectionFunction(subctx, pctx);
        break;
    case WASM_SEC_CUSTOM: {
        ret = readSectionCustom(subctx, pctx);
        break;
    }
    }

    ctx.cur = endOfSection;

    return ret;
}

static void readFile(ReadContext &ctx, wasmobj::Results &results) {
    size_t rd;

    if (memcmp(ctx.cur, gWasmObjSignature, 4) != 0) {
        fprintf(stderr, "Not a WASM object file\n");
        return;
    }
    ctx.cur += 4;

    if (ctx.cur + 4 >= ctx.end) {
        fprintf(stderr, "Couldn't read WASM object file version\n");
        return;
    }

    if (ctx.read<u32>() != gWasmVersion) {
        fprintf(stderr, "Unsupported WASM object file version\n");
        return;
    }

    ParseContext pctx;
    while (ctx.cur < ctx.end) {
        if (!readSection(ctx, pctx)) {
            return;
        }
    }

    for (auto &fun : pctx.functions) {
        results.functions.push_back({fun.name, fun.size});
    }
    for (auto &dat : pctx.data) {
        results.data.push_back({dat.name, dat.size});
    }
}

bool wasmobj::ProcessFile(Results &outResults, const char *path) {
    size_t rd;
    auto fileInput = fopen(path, "rb");
    if (fileInput == nullptr) {
        fprintf(stderr, "Failed to open %s for reading\n", path);
        return false;
    }

    fseek(fileInput, 0, SEEK_END);
    auto sizInput = ftell(fileInput);

    auto fileBuffer = std::make_unique<u8[]>(sizInput);
    fseek(fileInput, 0, SEEK_SET);
    rd = fread(fileBuffer.get(), sizInput, 1, fileInput);

    if (rd != 1) {
        fprintf(stderr, "Couldn't read contents of %s into memory\n", path);
        fclose(fileInput);
        return false;
    }

    ReadContext ctx = {fileBuffer.get(), fileBuffer.get(),
                   fileBuffer.get() + sizInput};
    readFile(ctx, outResults);

    fclose(fileInput);
    return true;
}
