#include "demangler.h"

#include <cassert>
#include <cstdio>

using namespace demangler;

static const char *singleCharBuiltins = "vwbcahstijlmxynofdegz";
static const char *doubleCharBuiltins = "DdDeDfDhDiDsDuDaDcDn";

struct Dictionary {
    StringView qualifier = StringView::MakeEmpty();
    List<List<StringView>> entries = {nullptr, nullptr};
};

struct Context {
    StringView str;
    Dictionary dict;
    Arena *arena;
};

static bool IsDigit(char c, uint64_t &value) {
    if ('0' <= c && c <= '9') {
        value = (uint64_t)c - '0';
        return true;
    }

    return false;
}

static StringView ReadLengthPrefixedString(StringView& view) {
    StringView ret;
    uint64_t digit;

    while (IsDigit(view[0], digit)) {
        ret.len *= 10;
        ret.len += digit;
        view += 1;
    }

    ret.start = view.start;
    view += ret.len;

    return ret;
}

static NestedName ParseNestedName(Context &ctx);

static NestedName ParseType(Context& ctx) {
    NestedName nestedName = {false, false, false, REFQ_NONE};
    nestedName.builtIn = StringView::MakeEmpty();
    nestedName.name.head = nestedName.name.tail = nullptr;

    for (int i = 0; singleCharBuiltins[i]; i++) {
        if (ctx.str[0] == singleCharBuiltins[i]) {
            nestedName.builtIn = {1, &singleCharBuiltins[i]};
            ctx.str += 1;
            return nestedName;
        }
    }

    for (int i = 0; doubleCharBuiltins[i]; i += 2) {
        if (ctx.str[0] == doubleCharBuiltins[i + 0] &&
            ctx.str[1] == doubleCharBuiltins[i + 1]) {
            nestedName.builtIn = {2, &doubleCharBuiltins[i + 0]};
            ctx.str += 2;
            break;
            return nestedName;
        }
    }

    // Float-N
    if (ctx.str[0] == 'D' && ctx.str[1] == 'F') {
        uint64_t len = 2;
        auto start = ctx.str.start;
        ctx.str += 2;
        while (ctx.str[0] != '_') {
            len++;
            ctx.str += 1;
        }
        nestedName.builtIn = {len, start};
        return nestedName;
    }

    assert(ctx.str[0] != 'u');
    return nestedName;
}

static NestedName ParseLiteral(Context& ctx) {
    NestedName nestedName = {
        false, false, false, REFQ_NONE};
    nestedName.builtIn = StringView::FromLiteral("literal");
    nestedName.name.head = nestedName.name.tail = nullptr;

    assert(ctx.str[0] == 'L');
    while (ctx.str[0] != 'E') {
        ctx.str += 1;
    }

    assert(ctx.str[0] == 'E');
    ctx.str += 1;

    return nestedName;
}

static TemplateArgs ParseTemplateArgs(Context &ctx) {
    TemplateArgs ret;
    ret.head = ret.tail = nullptr;
    assert(ctx.str[0] == 'I');
    ctx.str += 1;

    while (ctx.str[0] != 'E') {

        auto arg = AppendNewNode(ctx.arena, ret);
        arg->name = {false, false, false, REFQ_NONE};
        arg->name.builtIn = StringView::MakeEmpty();
        arg->name.name.head = arg->name.name.tail = nullptr;
        if (ctx.str[0] == 'N') {
            auto nestedName = ParseNestedName(ctx);
            arg->name = nestedName;
        } else if (ctx.str[0] == 'L') {
            arg->name = ParseLiteral(ctx);
        } else {
            arg->name = ParseType(ctx);
        }
    }

    return ret;
}

static NestedName ParseNestedName(Context &ctx) {
    NestedName nestedName = {
        false, false, false, REFQ_NONE};
    nestedName.builtIn = StringView::MakeEmpty();
    nestedName.name.head = nestedName.name.tail = nullptr;
    assert(ctx.str.NotEmpty() && ctx.str[0] == 'N');
    ctx.str += 1;

    // Read CV-qualifiers
    while (ctx.str[0] == 'r' || ctx.str[0] == 'V' || ctx.str[0] == 'K') {
        switch (ctx.str[0]) {
        case 'r':
            nestedName.qRestrict = true;
            break;
        case 'V':
            nestedName.qVolatile = true;
            break;
        case 'K':
            nestedName.qConst = true;
            break;
        }
        ctx.str += 1;
    }

    // Read ref-qualifier
    if (ctx.str[0] == 'R') {
        nestedName.qRef = REFQ_REF;
        ctx.str += 1;
    } else if (ctx.str[0] == 'O') {
        nestedName.qRef = REFQ_REFREF;
        ctx.str += 1;
    }

    uint64_t digit;
    while (ctx.str[0] != 'E') {

        // Substitution
        if (ctx.str[0] == 'S' && ctx.str[1] != 't') {
            ctx.str += 1;
            if (ctx.str[0] == '_') {
                auto node = AppendNewNode(ctx.arena, nestedName.name);
                assert(ctx.dict.qualifier.NotEmpty());
                *node = ctx.dict.qualifier;
                ctx.str += 1;
                continue;
            }

            uint64_t id = 0, digit;
            while (IsDigit(ctx.str[0], digit)) {
                id *= 10;
                id += digit;
                ctx.str += 1;
            }
            assert(ctx.str[0] == '_');
            ctx.str += 1;

            auto curDictEntry = ctx.dict.entries.head;
            while (id != 0) {
                id--;
                if (curDictEntry == ctx.dict.entries.tail)
                    break;
                curDictEntry = curDictEntry->next;
            }
            
            auto curDictEntryElem = curDictEntry->head;
            while (1) {
                auto node = AppendNewNode(ctx.arena, nestedName.name);
                *node = *curDictEntryElem;

                if (curDictEntryElem == curDictEntry->tail)
                    break;
                curDictEntryElem = curDictEntryElem->next;
            }

            continue;
        }

        if (ctx.str[0] == 'S' && ctx.str[1] == 't') {
            auto node = AppendNewNode(ctx.arena, nestedName.name);
            *node = StringView::FromLiteral("std");
            ctx.str += 2;
            continue;
        }

        if (ctx.str[0] == 'I') {
            // template-args

            auto templateArgs = ParseTemplateArgs(ctx);
            nestedName.templateArgs = templateArgs;

            assert(ctx.str[0] == 'E');
            ctx.str += 1;
            continue;
        }

        if (ctx.str[0] == 'C') {
            if (ctx.str[1] == '1') {
                // complete object ctor
                auto node = AppendNewNode(ctx.arena, nestedName.name);
                *node = StringView::FromLiteral("ctor");
                ctx.str += 2;
                continue;
            }
        } else if (ctx.str[0] == 'D') {
            if (ctx.str[1] == '0') {
                // deleting dtor 
                auto node = AppendNewNode(ctx.arena, nestedName.name);
                *node = StringView::FromLiteral("dtor");
                ctx.str += 2;
                continue;
            } else if (ctx.str[1] == '1') {
                auto node = AppendNewNode(ctx.arena, nestedName.name);
                *node = StringView::FromLiteral("codtor");
                ctx.str += 2;
                continue;
            } else if (ctx.str[1] == '2') {
                auto node = AppendNewNode(ctx.arena, nestedName.name);
                *node = StringView::FromLiteral("bodtor");
                ctx.str += 2;
                continue;
            }
        }
        auto name = ReadLengthPrefixedString(ctx.str);
        auto node = AppendNewNode(ctx.arena, nestedName.name);
        if (ctx.str[0] != 'E') {
            if (ctx.dict.qualifier.Empty()) {
                ctx.dict.qualifier = name;
            } else {
                auto dictEntry = AppendNewNode(ctx.arena, ctx.dict.entries);
                *dictEntry = nestedName.name;
            }
        }
        *node = name;
    }

    assert(ctx.str[0] == 'E');
    ctx.str += 1;

    return nestedName;
}

static BareFunctionType ParseBareFunctionType(Context& ctx) {
    BareFunctionType ret;
    ret.argumentTypes.head = ret.argumentTypes.tail = nullptr;
    while (ctx.str[0] == 'N' || ctx.str[0] == 'R' || ctx.str[0] == 'K' ||
           ctx.str[0] == 'P') {
        if (ctx.str[0] == 'R') {
            ctx.str += 1;
        } else if (ctx.str[0] == 'K') {
            ctx.str += 1;
        } else if (ctx.str[0] == 'P') {
            ctx.str += 1;
        } else if (ctx.str[0] == 'N') {
            auto nestedName = ParseNestedName(ctx);
            auto *node = AppendNewNode(ctx.arena, ret.argumentTypes);
            *node = nestedName;
        }
    }

    return ret;
}

static Encoding ParseEncoding(Context &ctx) {
    Encoding ret = {};
    if (ctx.str.Length() >= 1) {
        if (ctx.str.Length() >= 2 && ctx.str[0] == 'S' &&
            ctx.str[1] == 't') {
            ctx.str += 2;
            uint64_t digit;
            while (IsDigit(ctx.str[0], digit)) {
                auto name = ReadLengthPrefixedString(ctx.str);
            }
        } else if (ctx.str[0] == 'N') {
            unsigned length = 0;
            ret.functionName = ParseNestedName(ctx);
            ret.bareFunctionType = ParseBareFunctionType(ctx);

        } else {
            NestedName nestedName = {false, false, false, REFQ_NONE};
            nestedName.builtIn = StringView::MakeEmpty();
            nestedName.name.head = nestedName.name.tail = nullptr;
            auto node = AppendNewNode(ctx.arena, nestedName.name);
            *node = ReadLengthPrefixedString(ctx.str);
            ret.functionName = nestedName;
            ret.bareFunctionType = ParseBareFunctionType(ctx);
        }
    }
    return ret;
}

DemangledName demangler::demangle(StringView str, Arena arena) {
    DemangledName ret;
    ret.cname = StringView::MakeEmpty();

    if (str.Empty()) {
        std::abort();
    }

    if (str.Length() < 2) {
        ret.cname = str;
        return ret;
    }

    if (str[0] != '_' || str[1] != 'Z') {
        ret.cname = str;
        return ret;
    }

    str += 2;

    Context ctx = {};
    ctx.arena = &arena;
    ctx.str = str;

    ret.encoding = ParseEncoding(ctx);

    if (str.NotEmpty()) {
        if (str[0] == '.') {
            str += 1;
            ret.vendorSuffix = str;
        }
    }

    return ret;
}

void demangler::printNestedName(NestedName const& nestedName) {
    if (nestedName.qConst)
        printf("const ");
    if (nestedName.qVolatile)
        printf("volatile ");
    if (nestedName.qRef == REFQ_REF)
        printf("&");
    if (nestedName.qRef == REFQ_REFREF)
        printf("&&");

    if (nestedName.builtIn.NotEmpty()) {
        printf("_builtin(%.*s)", (int)nestedName.builtIn.Length(),
               nestedName.builtIn.start);
        return;
    }

    auto *cur = nestedName.name.head;
    while (1) {
        printf("::%.*s", (int)cur->Length(), cur->start);

        if (cur == nestedName.name.tail)
            break;
        cur = cur->next;
    }

    if (nestedName.templateArgs.head != nullptr) {
        printf("<");
        auto *curArg = nestedName.templateArgs.head;
        while (1) {
            if (curArg->name.builtIn.NotEmpty()) {
                printf("_builtin(%.*s)", EXPLODE_STRV(curArg->name.builtIn));
            } else {
                printNestedName(curArg->name);
            }
            if (curArg == nestedName.templateArgs.tail)
                break;
            printf(", ");
            curArg = curArg->next;
        }
        printf(">");
    }

}

void demangler::printEncoding(Encoding const &encoding) {
    printNestedName(encoding.functionName);
    printf("(");
    auto curArg = encoding.bareFunctionType.argumentTypes.head;
    while (curArg != nullptr) {
        printNestedName(*curArg);
        if (curArg == encoding.bareFunctionType.argumentTypes.tail)
            break;
        curArg = curArg->next;
    }
    printf(")");
}

void demangler::printDemangledName(DemangledName const &dm) {
    if (dm.cname.NotEmpty()) {
        printf("%.*s()", (int)dm.cname.Length(), dm.cname.start);
    } else {
        printEncoding(dm.encoding);
    }
}