#include "demangler.h"

#include <cassert>
#include <cstdio>

using namespace demangler;

static const char *singleCharBuiltins = "vwbcahstijlmxynofdegz";
static const char *doubleCharBuiltins = "DdDeDfDhDiDsDuDaDcDn";

struct Dictionary {
    NestedName qualifier = {};
    List<NestedName> entries = {nullptr, nullptr};
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

static bool IsDigit(char c) {
    uint64_t buf;
    return IsDigit(c, buf);
}

static bool IsBase36Digit(char c, uint64_t &value) {
    if ('0' <= c && c <= '9') {
        value = (uint64_t)c - '0';
        return true;
    }

    if ('A' <= c && c <= 'Z') {
        value = 10 + (uint64_t)c - 'A';
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

static NestedName ParseNestedName(Context &ctx, bool isFunction = false);
static Encoding ParseEncoding(Context &ctx);
static void ParseName(Context &ctx, NestedName &nestedName, bool isFunction);
static TemplateArgs ParseTemplateArgs(Context &ctx);
static void ParseAndResolveSubstitution(Context &ctx, NestedName &nestedName);
static BareFunctionType ParseBareFunctionType(Context &ctx);

static Type ParseType(Context& ctx) {
    Type type = {};

    type.levelsOfIndirection = 0;
    type.refQualifier = RefQualifier::REFQ_NONE;

    while (ctx.str[0] == 'P' || ctx.str[0] == 'R' || ctx.str[0] == 'O') {
        switch (ctx.str[0]) {
        case 'P':
            type.levelsOfIndirection++;
            break;
        case 'R':
            type.refQualifier = REFQ_REF;
            break;
        case 'O':
            type.refQualifier = REFQ_REFREF;
            break;
        }
        ctx.str += 1;
    }

    while (ctx.str[0] == 'r' || ctx.str[0] == 'V' || ctx.str[0] == 'K') {
        switch (ctx.str[0]) {
        case 'r':
            type.qRestrict = true;
            break;
        case 'V':
            type.qVolatile = true;
            break;
        case 'K':
            type.qConst = true;
            break;
        }
        ctx.str += 1;
    }

    if (ctx.str.StartsWith('T')) {
        ctx.str += 1;
        type.tag = Type::TEMPLATE_ARGUMENT;
        type.templateArgumentIndex = 0;
        uint64_t digit;
        while (IsDigit(ctx.str[0], digit)) {
            type.templateArgumentIndex *= 10;
            type.templateArgumentIndex += digit;
            ctx.str += 1;
        }
        assert(ctx.str.StartsWith('_'));
        ctx.str += 1;
        return type;
    } else if (ctx.str[0] == 'N') {
        auto nestedName = ParseNestedName(ctx);
        type.tag = Type::QUALIFIED;
        type.qualified = nestedName;
        return type;
    } else if (ctx.str.StartsWith('S')) {
        NestedName nestedName = {};
        ParseAndResolveSubstitution(ctx, nestedName);

        if (ctx.str.StartsWith('I')) {
            nestedName.templateArgs = ParseTemplateArgs(ctx);
            assert(ctx.str.StartsWith('E'));
            ctx.str += 1;
        }

        type.tag = Type::QUALIFIED;
        type.qualified = nestedName;
        return type;
    }

    for (int i = 0; singleCharBuiltins[i]; i++) {
        if (ctx.str[0] == singleCharBuiltins[i]) {
            type.tag = Type::BUILTIN;
            type.builtIn = {1, &singleCharBuiltins[i]};
            ctx.str += 1;
            return type;
        }
    }

    for (int i = 0; doubleCharBuiltins[i]; i += 2) {
        if (ctx.str[0] == doubleCharBuiltins[i + 0] &&
            ctx.str[1] == doubleCharBuiltins[i + 1]) {
            type.tag = Type::BUILTIN;
            type.builtIn = {2, &doubleCharBuiltins[i + 0]};
            ctx.str += 2;
            return type;
        }
    }

    if (ctx.str.StartsWith("Dp")) {
        // Parameter pack expansion
        ctx.str += 2;
        auto *dpType = ctx.arena->Alloc<Type>();
        *dpType = ParseType(ctx);
        type.tag = Type::PARAMETER_PACK_EXPANSION;
        type.parameterPackExpansion = {dpType};
        return type;
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
        type.tag = Type::BUILTIN;
        type.builtIn = {len, start};
        return type;
    }

    if (ctx.str.StartsWith('F')) {
        // Function pointer type
        ctx.str += 1;
        if (ctx.str.StartsWith('Y'))
            ctx.str += 1;
        auto bareFunctionType = ParseBareFunctionType(ctx);

        if (!ctx.str.StartsWith('E'))
            __debugbreak();
        ctx.str += 1;
        type.tag = Type::FUNCTION_POINTER;
        return type;
    }

    __debugbreak();

    assert(ctx.str[0] != 'u');
    return type;
}

static Literal ParseLiteral(Context& ctx) {
    Literal ret = {Literal::INTEGER_LITERAL};

    assert(ctx.str[0] == 'L');
    ctx.str += 1;

    if (ctx.str[0] == '_' && ctx.str[1] == 'Z') {
        ctx.str += 2;
        auto *encoding = ctx.arena->Alloc<Encoding>();
        *encoding = ParseEncoding(ctx);
        ret.tag = Literal::EXTERNAL_NAME;
        ret.externalName = encoding;
    } else {
        auto type = ParseType(ctx);
        switch (type.tag) {
        case Type::BUILTIN:
            assert(type.builtIn.NotEmpty());
            if (type.builtIn == "Dn") {
                ret.tag = Literal::NULLPTR_LITERAL;
            } else if (type.levelsOfIndirection > 0) {
                if (ctx.str[0] == '0') {
                    ret.tag = Literal::NULL_POINTER;
                    ctx.str += 1;
                } else {
                    throw std::exception();
                }
            } else if (type.builtIn == "b") {
                ret.tag = Literal::INTEGER_LITERAL;
                ret.number.type = type.builtIn;
                ret.number.valueUInteger = ((uint32_t)ctx.str[0] - '0');
            } else {
                __debugbreak();
            }
            break;
        default:
            assert(0);
            break;
        }
    }

    while (ctx.str[0] != 'E') {
        ctx.str += 1;
    }

    assert(ctx.str[0] == 'E');
    ctx.str += 1;

    return ret;
}

static void ParseAndResolveSubstitution(Context& ctx, NestedName &nestedName) {
    assert(ctx.str.Length() >= 2);
    assert(ctx.str[0] == 'S');

    nestedName = {};

    ctx.str += 1;

    const char *abbrev = nullptr;
    switch (ctx.str[0]) {
    case 't':
        abbrev = "std";
        break;
    case 'a':
        abbrev = "std::allocator";
        break;
    case 'b':
        abbrev = "std::basic_string";
        break;
    case 's':
        abbrev = "std::basic_string<char, ::std::char_traits<char>, ::std::allocator<char>>";
        break;
    case 'i':
        abbrev = "std::basic_istream<char, std::char_traits<char>>";
        break;
    case 'o':
        abbrev = "std::basic_ostream<char, std::char_traits<char>>";
        break;
    case 'd':
        abbrev = "std::basic_iostream<char, std::char_traits<char>>";
        break;
    }

    if (abbrev != nullptr) {
        auto node = AppendNewNode(ctx.arena, nestedName.name);
        *node = StringView::FromLiteral(abbrev);
        ctx.str += 1;
        return;
    }

    if (ctx.str[0] == '_') {
        assert(ctx.dict.qualifier.name.head != nullptr);
        auto curComponent = ctx.dict.qualifier.name.head;
        while (curComponent != nullptr) {
            auto *node = AppendNewNode(ctx.arena, nestedName.name);
            *node = *curComponent;

            if (curComponent == ctx.dict.qualifier.name.tail)
                break;
            curComponent = curComponent->next;
        }
        ctx.str += 1;
        return;
    }

    uint64_t id = 0, digit;
    while (IsBase36Digit(ctx.str[0], digit)) {
        id *= 36;
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


    auto curComponent = curDictEntry->name.head;
    while (curComponent != nullptr) {
        auto *node = AppendNewNode(ctx.arena, nestedName.name);
        *node = *curComponent;
        if (curComponent == curDictEntry->name.tail)
            break;
        curComponent = curComponent->next;
    }
}

static void ParseTemplateArgs(Context &ctx, TemplateArgs &args) {
    assert(ctx.str[0] == 'I' || ctx.str[0] == 'J');
    ctx.str += 1;

    while (ctx.str[0] != 'E' && ctx.str.NotEmpty()) {

        if (ctx.str[0] == 'N') {
            auto nestedName = ParseNestedName(ctx);
            auto arg = AppendNewNode(ctx.arena, args);
            arg->name = nestedName;
            arg->tag = TemplateArg::NESTED_NAME;
            assert(arg->name.name.NotEmpty());
        } else if (IsDigit(ctx.str[0])) {
            auto arg = AppendNewNode(ctx.arena, args);
            arg->name = {};
            ParseName(ctx, arg->name, false);
            arg->tag = TemplateArg::NESTED_NAME;

            if (ctx.str.StartsWith('I')) {
                arg->name.templateArgs = ParseTemplateArgs(ctx);
                assert(ctx.str[0] == 'E');
                ctx.str += 1;
            }
        } else if (ctx.str[0] == 'L') {
            auto arg = AppendNewNode(ctx.arena, args);
            arg->literal = ParseLiteral(ctx);
            arg->tag = TemplateArg::LITERAL;
        } else if (ctx.str[0] == 'S' && ctx.str[1] != 't') {
            auto arg = AppendNewNode(ctx.arena, args);
            arg->tag = TemplateArg::NESTED_NAME;
            ParseAndResolveSubstitution(ctx, arg->name);
            assert(arg->name.name.NotEmpty());
        } else if (ctx.str[0] == 'J') {
            ParseTemplateArgs(ctx, args);
            assert(ctx.str[0] == 'E');
            ctx.str += 1;
        } else {
            auto arg = AppendNewNode(ctx.arena, args);
            arg->type = ParseType(ctx);
            arg->tag = TemplateArg::TYPE;
        }
    }
}

static TemplateArgs ParseTemplateArgs(Context &ctx) {
    TemplateArgs ret = {};
    ret.head = ret.tail = nullptr;

    ParseTemplateArgs(ctx, ret);

    return ret;
}

static void ParseName(Context& ctx, NestedName& nestedName, bool isFunction) {
    // Substitution
    if (ctx.str[0] == 'S') {
        ParseAndResolveSubstitution(ctx, nestedName);
        return;
    } else if (ctx.str[0] == 'I') {
        // template-args

        auto templateArgs = ParseTemplateArgs(ctx);
        nestedName.templateArgs = templateArgs;

        assert(ctx.str[0] == 'E');
        ctx.str += 1;
        return;
    } else if (ctx.str[0] == 'C') {
        if (ctx.str[1] == '1') {
            // complete object ctor
            auto node = AppendNewNode(ctx.arena, nestedName.name);
            *node = StringView::FromLiteral("ctor");
            ctx.str += 2;
            return;
        } else if (ctx.str[1] == '2') {
            // base object ctor
            auto node = AppendNewNode(ctx.arena, nestedName.name);
            *node = StringView::FromLiteral("boctor");
            ctx.str += 2;
            return;
        } else if (ctx.str[1] == '3') {
            // base object ctor
            auto node = AppendNewNode(ctx.arena, nestedName.name);
            *node = StringView::FromLiteral("coactor");
            ctx.str += 2;
            return;
        }
        ctx.str += 2;
        __debugbreak();
    } else if (ctx.str[0] == 'D') {
        if (ctx.str[1] == '0') {
            // deleting dtor
            auto node = AppendNewNode(ctx.arena, nestedName.name);
            *node = StringView::FromLiteral("dtor");
            ctx.str += 2;
            return;
        } else if (ctx.str[1] == '1') {
            auto node = AppendNewNode(ctx.arena, nestedName.name);
            *node = StringView::FromLiteral("codtor");
            ctx.str += 2;
            return;
        } else if (ctx.str[1] == '2') {
            auto node = AppendNewNode(ctx.arena, nestedName.name);
            *node = StringView::FromLiteral("bodtor");
            ctx.str += 2;
            return;
        }
    } else {
        auto name = ReadLengthPrefixedString(ctx.str);
        auto node = AppendNewNode(ctx.arena, nestedName.name);
        *node = name;
    }

    if (ctx.dict.qualifier.name.head == nullptr) {
        ctx.dict.qualifier = nestedName;
    } else {
        if (!isFunction || IsDigit(ctx.str[0]) || ctx.str[0] == 'I') {
            auto dictEntry = AppendNewNode(ctx.arena, ctx.dict.entries);
            *dictEntry = nestedName;
        }
    }
}

static NestedName ParseNestedName(Context &ctx, bool isFunction) {
    NestedName nestedName = {
        false, false, false, REFQ_NONE};
    nestedName.name.head = nestedName.name.tail = nullptr;
    assert(ctx.str.NotEmpty() && ctx.str[0] == 'N');
    ctx.str += 1;
    auto savedCtx = ctx;

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

    while (!ctx.str.StartsWith('E')) {
        ParseName(ctx, nestedName, isFunction);
    }

    assert(ctx.str[0] == 'E');
    ctx.str += 1;

    return nestedName;
}

static BareFunctionType ParseBareFunctionType(Context& ctx) {
    BareFunctionType ret = {};
    /*
    while (ctx.str[0] == 'N' || ctx.str[0] == 'R' || ctx.str[0] == 'K' ||
           ctx.str[0] == 'P' || ctx.str[0] == 'S') {
        if (ctx.str[0] == 'R') {
            ctx.str += 1;
        } else if (ctx.str[0] == 'K') {
            ctx.str += 1;
        } else if (ctx.str[0] == 'P') {
            ctx.str += 1;
        } else if (ctx.str[0] == 'N') {
            auto nestedName = ParseNestedName(ctx);
            if (ctx.str[0] == 'I') {
                nestedName.templateArgs = ParseTemplateArgs(ctx);
                assert(ctx.str[0] == 'E');
                ctx.str += 1;
            }
            auto *node = AppendNewNode(ctx.arena, ret.argumentTypes);
            *node = nestedName;

        } else if (ctx.str[0] == 'S') {
            NestedName nestedName = {};
            nestedName.name.head = nestedName.name.tail = nullptr;
            ParseAndResolveSubstitution(ctx, nestedName);
            if (ctx.str[0] == 'I') {
                nestedName.templateArgs = ParseTemplateArgs(ctx);
                assert(ctx.str[0] == 'E');
                ctx.str += 1;
            }
            auto *node = AppendNewNode(ctx.arena, ret.argumentTypes);
            *node = nestedName;
        }
    }
    */

    while (ctx.str.NotEmpty() && !ctx.str.StartsWith('E')) {
        auto type = ParseType(ctx);
        assert(type.tag != Type::UNINITIALIZED);
        if (ret.returnType.tag == Type::UNINITIALIZED) {
            ret.returnType = type;
        } else {
            auto *node = AppendNewNode(ctx.arena, ret.argumentTypes);
            *node = type;
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
            ret.functionName = ParseNestedName(ctx, true);
            ret.bareFunctionType = ParseBareFunctionType(ctx);

        } else {
            NestedName nestedName = {false, false, false, REFQ_NONE};
            nestedName.name.head = nestedName.name.tail = nullptr;
            ParseName(ctx, nestedName, true);
            if (ctx.str.StartsWith('I')) {
                nestedName.templateArgs = ParseTemplateArgs(ctx);
                assert(ctx.str.StartsWith('E'));
                ctx.str += 1;
            }
            ret.functionName = nestedName;
            ret.bareFunctionType = ParseBareFunctionType(ctx);
        }
    }
    return ret;
}

DemangledName demangler::demangle(StringView str, Arena arena) {
    DemangledName ret = {};
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

    if (ctx.str.NotEmpty()) {
        if (ctx.str[0] == '.') {
            ctx.str += 1;
            ret.vendorSuffix = ctx.str;
        }
    }

    return ret;
}

const char *resolveBuiltinType(StringView const &s) {
    if (s.Length() == 1) {
        switch (s[0]) {
        case 'v':
            return "void";
        case 'w':
            return "wchar_t";
        case 'b':
            return "bool";
        case 'c':
            return "char";
        case 'i':
            return "int";
        case 'j':
            return "unsigned";
        case 'l':
            return "long";
        case 'f':
            return "float";
        case 'd':
            return "double";
        case 'z':
            return "...";
        }
    } else if (s.Length() == 2) {
        if (s[0] == 'D') {
            switch (s[1]) {
            case 'a':
                return "auto";
            case 'c':
                return "decltype(auto)";
            case 'n':
                return "nullptr_t";
            }
        }
    }

    return "(builtin)";
}

void printType(Type const& type) {
    switch (type.tag) {
    case Type::BUILTIN:
        printf("%s", resolveBuiltinType(type.builtIn));
        break;
    case Type::QUALIFIED:
        printNestedName(type.qualified);
        break;
    case Type::FUNCTION_POINTER:
        printf("(funptr)");
        break;
    }

    for (int i = 0; i < type.levelsOfIndirection; i++) {
        printf("*");
    }
}

void printLiteral(Literal const& literal) {
    switch (literal.tag) {
    case Literal::NULLPTR_LITERAL:
        printf("nullptr");
        break;
    case Literal::NULL_POINTER:
        printf("(void*)0");
        break;
    case Literal::INTEGER_LITERAL:
        if (literal.number.type == "b") {
            printf("%s", literal.number.valueUInteger != 0 ? "true" : "false");
        } else {
            printf("int()");
        }
        break;
    default:
        printf("(literal)");
        break;
    }
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

    auto *cur = nestedName.name.head;
    while (1) {
        printf("::%.*s", (int)cur->Length(), cur->start);

        if (cur == nestedName.name.tail || !cur->next)
            break;
        cur = cur->next;
    }

    if (nestedName.templateArgs.head != nullptr) {
        printf("<");
        auto *curArg = nestedName.templateArgs.head;
        while (1) {
            switch (curArg->tag) {
            case TemplateArg::UNINITIALIZED:
                __debugbreak();
                break;
            case TemplateArg::LITERAL:
                printLiteral(curArg->literal);
                break;
            case TemplateArg::NESTED_NAME:
                printNestedName(curArg->name);
                break;
            case TemplateArg::TYPE:
                printType(curArg->type);
                break;
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
    auto &retType = encoding.bareFunctionType.returnType;
    if (retType.tag != Type::UNINITIALIZED) {
        printType(retType);
        printf(" ");
    } else {
        printf("Ret? ");
    }
    printNestedName(encoding.functionName);
    printf("(");
    auto curArg = encoding.bareFunctionType.argumentTypes.head;
    while (curArg != nullptr) {
        printType(*curArg);
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