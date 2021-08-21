#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace demangler {
struct StringView {
    uint64_t len = 0;
    const char *start = nullptr;

    constexpr bool Empty() const noexcept { return start == nullptr; }
    constexpr bool NotEmpty() const noexcept { return start != nullptr; }

    uint64_t Length() const noexcept { return len; }

    template<typename T> static StringView FromString(T const& s) {
        return {s.size(), s.c_str()};
    }

    char operator[](size_t idx) const noexcept {
        if (idx >= len)
            return '\0';
        return start[idx];
    }

    constexpr bool StartsWith(char ch) const noexcept {
        if (len < 1)
            return false;
        return start[0] == ch;
    }

    bool StartsWith(const char *ch) const noexcept {
        if (len == 0)
            return false;
        unsigned i;
        for (i = 0; ch[i] && i < len; i++) {
            if (ch[i] != start[i])
                return false;
        }

        return ch[i] == '\0';
    }

    constexpr bool operator==(std::nullptr_t const&) const noexcept {
        return start == nullptr;
    }

    bool operator==(const char *other) const noexcept {
        return strncmp(start, other, len) == 0;
    }

    StringView operator+(size_t fwd) const noexcept {
        if (len <= fwd) {
            return MakeEmpty();
        }

        return {len - fwd, start + fwd};
    }

    StringView& operator+=(size_t fwd) noexcept {
        if (len <= fwd) {
            len = 0;
            start = nullptr;
        } else {
            len -= fwd;
            start += fwd;
        }

        return *this;
    }

    static StringView FromLiteral(const char *literal) {
        return {strlen(literal), literal};
    }

    constexpr static StringView MakeEmpty() { return {0, nullptr}; }
};

#define EXPLODE_STRV(strv) static_cast<int>(strv.Length()), strv.start

struct Arena {
    void *ptr;
    uint32_t const size;
    uint32_t cursor;

    void *Alloc(size_t siz) {
        if (cursor + siz >= size)
            throw std::exception();
        auto ret = ((uint8_t *)ptr + cursor);
        cursor += siz;
        return ret;
    }

    template <typename T> T *Alloc() { return (T *)Alloc(sizeof(T)); }

    void FreeAll() {
        cursor = 0;
    }
};

template<typename T>
struct Node : T {
    Node *next;
    Node &operator=(T const &other) noexcept {
        *((T *)this) = other;
        return *this;
    }
};

template <typename T> struct List {
    Node<T> *head;
    Node<T> *tail;

    constexpr bool Empty() const noexcept { return head == nullptr; }
    constexpr bool NotEmpty() const noexcept { return head != nullptr; }
};

template <typename T> Node<T> *AppendNode(List<T> &list, Node<T> *node) {
    node->next = nullptr;
    if (list.head == nullptr) {
        list.head = node;
    }
    if (list.tail != nullptr) {
        list.tail->next = node;
    }
    list.tail = node;
    return node;
}

template<typename T> Node<T>* AppendNewNode(Arena *arena, List<T> &list) {
    auto *node = arena->Alloc<Node<T>>();
    return AppendNode(list, node);
}

enum RefQualifier {
    REFQ_NONE,
    REFQ_REF,
    REFQ_REFREF,
};

struct TemplateArg;
struct Encoding;
struct Type;

struct TemplateArgs : List<TemplateArg> {};

struct NestedName {
    bool qRestrict;
    bool qVolatile;
    bool qConst;
    RefQualifier qRef;

    List<StringView> name;
    TemplateArgs templateArgs;
};

struct ParameterPackExpansion {
    Type *type;
};

struct Type {
    enum {
        UNINITIALIZED,
        BUILTIN,
        QUALIFIED,
        TEMPLATE_ARGUMENT,
        PARAMETER_PACK_EXPANSION,
        FUNCTION_POINTER,
    } tag;

    bool qRestrict;
    bool qVolatile;
    bool qConst;

    int levelsOfIndirection;
    RefQualifier refQualifier;

    union {
        StringView builtIn;
        NestedName qualified;
        unsigned templateArgumentIndex;
        ParameterPackExpansion parameterPackExpansion;
    };
};

struct Literal {
    enum {
        UNINITIALIZED,
        INTEGER_LITERAL,
        FLOATING_LITERAL,
        STRING_LITERAL,
        NULLPTR_LITERAL,
        NULL_POINTER,
        // COMPLEX_LITERAL,
        EXTERNAL_NAME,
    } tag;

    union {
        struct {
            StringView type;
            union {
                double valueFloat;
                long long valueInteger;
                unsigned long long valueUInteger;
            };
        } number;
        Encoding *externalName;
    };
};

struct TemplateArg {
    enum {
        UNINITIALIZED,
        NESTED_NAME,
        LITERAL,
        TYPE,
    } tag;

    union {
        Type type;
        NestedName name;
        Literal literal;
    };
};

struct Name {
    enum Kind {
        NAME_NESTED_NAME,
        NAME_UNSCOPED_NAME,
        NAME_UNSCOPED_TEMPLATE_NAME,
        NAME_LOCAL_NAME,
    };

    Kind tag;
    union {
        NestedName nestedName;
    };
};

struct BareFunctionType {
    Type returnType;
    List<Type> argumentTypes;
};

struct Encoding {
    NestedName functionName;
    BareFunctionType bareFunctionType;
};

struct DemangledName {
    StringView cname;
    Encoding encoding;
    StringView vendorSuffix;
};

class Visitor {
  public:
    virtual ~Visitor() = default;

    virtual void Visit(DemangledName const &dm) = 0;
    virtual void Visit(Encoding const &enc) = 0;
};

DemangledName demangle(StringView mangledName, Arena arena);
void printNestedName(NestedName const &nestedName);
void printEncoding(Encoding const &encoding);
void printDemangledName(DemangledName const &dm);
}