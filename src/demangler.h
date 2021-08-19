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

    constexpr bool operator==(std::nullptr_t const&) const noexcept {
        return start == nullptr;
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

struct TemplateArgs : List<TemplateArg> {};

struct NestedName {
    bool qRestrict;
    bool qVolatile;
    bool qConst;
    RefQualifier qRef;

    StringView builtIn;
    List<StringView> name;
    TemplateArgs templateArgs;
};

struct TemplateArg {
    NestedName name;
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
    List<NestedName> argumentTypes;
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

DemangledName demangle(StringView mangledName, Arena arena);
void printNestedName(NestedName const &nestedName);
void printEncoding(Encoding const &encoding);
void printDemangledName(DemangledName const &dm);
}