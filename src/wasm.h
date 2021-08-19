#pragma once

#include "types.h"

static const char *gWasmObjSignature = "\0asm";
static const uint32_t gWasmVersion = 0x1;

enum {
    WASM_SEC_CUSTOM = 0,
    WASM_SEC_TYPE = 1,
    WASM_SEC_IMPORT = 2,
    WASM_SEC_FUNCTION = 3,
    WASM_SEC_TABLE = 4,
    WASM_SEC_MEMORY = 5,
    WASM_SEC_GLOBAL = 6,
    WASM_SEC_EXPORT = 7,
    WASM_SEC_START = 8,
    WASM_SEC_ELEM = 9,
    WASM_SEC_CODE = 10,
    WASM_SEC_DATA = 11,
    WASM_SEC_DATACOUNT = 12,
    WASM_SEC_EVENT = 13,
    WASM_SEC_MAX
};

enum {
    WASM_EXTERNAL_FUNCTION = 0,
    WASM_EXTERNAL_TABLE,
    WASM_EXTERNAL_MEMORY,
    WASM_EXTERNAL_GLOBAL,
    WASM_EXTERNAL_EVENT,
};

enum {
    WASM_NAMES_FUNCTION = 1,
};

enum WasmSymbolType : unsigned {
    WASM_SYMBOL_TYPE_FUNCTION = 0x0,
    WASM_SYMBOL_TYPE_DATA = 0x1,
    WASM_SYMBOL_TYPE_GLOBAL = 0x2,
    WASM_SYMBOL_TYPE_SECTION = 0x3,
    WASM_SYMBOL_TYPE_EVENT = 0x4,
    WASM_SYMBOL_TYPE_TABLE = 0x5,
};

const unsigned WASM_SYMBOL_BINDING_GLOBAL = 0x0;
const unsigned WASM_SYMBOL_BINDING_WEAK = 0x1;
const unsigned WASM_SYMBOL_BINDING_LOCAL = 0x2;
const unsigned WASM_SYMBOL_VISIBILITY_DEFAULT = 0x0;
const unsigned WASM_SYMBOL_VISIBILITY_HIDDEN = 0x4;
const unsigned WASM_SYMBOL_UNDEFINED = 0x10;
const unsigned WASM_SYMBOL_EXPORTED = 0x20;
const unsigned WASM_SYMBOL_EXPLICIT_NAME = 0x40;
const unsigned WASM_SYMBOL_NO_STRIP = 0x80;
