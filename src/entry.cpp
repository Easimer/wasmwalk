#include <algorithm>
#include <cstdio>
#include <memory>

#include "demangler.h"
#include "wasmobj.h"

enum SymbolKind { SYMBOL_FUNCTION = 0, SYMBOL_DATA = 1, SYMBOL_MAX };

struct Parameters {
    bool flagAggregate = false;
    bool flagDemangle = false;
    bool flagSymbolTree = false;

    unsigned argc = 0;
    char **argv = nullptr;
};

class Demangler {
  public:
    Demangler()
        : mem(std::make_unique<u8[]>(1024 * 1024)), arena{mem.get(),
                                                          1024 * 1024, 0} {}

    void printDemangledName(const char *s) {
        arena.FreeAll();
        try {
            auto dmres = demangler::demangle(
                demangler::StringView::FromLiteral(s), arena);
            demangler::printDemangledName(dmres);
            printf("\n");
        } catch (std::exception const &) {
            printf("%s\n", s);
        }
    }

    void printDemangledName(std::string const &s) {
        arena.FreeAll();
        try {
            fflush(stdout);
            fprintf(stderr, "demangling\n");
            fflush(stderr);
            auto dmres = demangler::demangle(
                demangler::StringView::FromString(s), arena);
            fprintf(stderr, "printing\n");
            fflush(stderr);
            demangler::printDemangledName(dmres);
            printf("\n");
            fflush(stdout);
        } catch (std::exception const &) {
            printf("%s\n", s.c_str());
        }
    }

  private:
    std::unique_ptr<u8[]> mem;
    demangler::Arena arena;
};

static void aggregateAndPrint(std::vector<wasmobj::Results> const &allResults,
                              Parameters const &ctx) {
    struct Symbol {
        const char *path;
        const char *name;
        SymbolKind kind;
        uint64_t size;
    };
    std::vector<Symbol> data;

    for (int idxResult = 0; idxResult < allResults.size(); ++idxResult) {
        auto &results = allResults[idxResult];
        auto *path = ctx.argv[idxResult];

        for (auto &fun : results.functions) {
            data.push_back({path, fun.name.c_str(), SYMBOL_FUNCTION, fun.size});
        }

        for (auto &dat : results.data) {
            data.push_back({path, dat.name.c_str(), SYMBOL_DATA, dat.size});
        }
    }

    std::sort(data.begin(), data.end(),
              [](Symbol const &lhs, Symbol const &rhs) {
                  return rhs.size < lhs.size;
              });

    Demangler D;
    printf("symbols:\n");
    for (auto &sym : data) {
        printf("  - path: %s\n", sym.path);
        printf("    name: ");
        if (ctx.flagDemangle) {
            D.printDemangledName(sym.name);
        } else {
            printf("%s\n", sym.name);
        }
        printf("    kind: %d\n", sym.kind);
        printf("    size: %llu\n", sym.size);
    }
}

static void printResults(std::vector<wasmobj::Results> &allResults,
                         Parameters const &ctx) {
    Demangler D;

    for (int idxResult = 0; idxResult < allResults.size(); ++idxResult) {
        auto &results = allResults[idxResult];
        auto *path = ctx.argv[idxResult];

        std::sort(results.functions.begin(), results.functions.end(),
                  [](auto &lhs, auto &rhs) { return rhs.size < lhs.size; });
        std::sort(results.data.begin(), results.data.end(),
                  [](auto &lhs, auto &rhs) { return rhs.size < lhs.size; });

        printf("%s:\n", path);
        printf("  functions:\n");
        for (auto &fun : results.functions) {
            printf("    - name: ");
            if (ctx.flagDemangle) {
                D.printDemangledName(fun.name);
            } else {
                printf("%s\n", fun.name.c_str());
            }
            printf("      size: %llu\n", fun.size);
        }
        printf("  data:\n");
        for (auto &dat : results.data) {
            printf("    - name: %s\n", dat.name.c_str());
            printf("      size: %llu\n", dat.size);
        }
    }
}

struct NameFragment {
    NameFragment() {}
    NameFragment(char ch) : fragment(1, ch) {}

    std::string fragment;

    size_t totalSize = 0;
    std::vector<NameFragment> children;
};

#include <queue>

static void collapseNameFragments(NameFragment &top) {
    std::queue<NameFragment *> queue;
    queue.push(&top);

    while (queue.size() > 0) {
        auto *cur = queue.front();
        queue.pop();

        if (cur->children.size() == 1) {
            auto &child = cur->children[0];
            cur->fragment = cur->fragment + child.fragment;
            cur->totalSize = cur->totalSize + child.totalSize;
            cur->children = std::move(child.children);
            queue.push(cur);
        } else {
            for (auto &child : cur->children) {
                queue.push(&child);
            }
        }
    }
}

static size_t propagateSizes(NameFragment &cur) {
    if (cur.children.size() == 0)
        return cur.totalSize;

    size_t sum = 0;
    for (auto &child : cur.children) {
        sum += propagateSizes(child);
    }
    cur.totalSize = sum;
    return sum;
}

static void recursiveFragWalk(NameFragment &cur, size_t level = 0) {
    auto nextLevel = level + 1;
    for (size_t i = 0; i < level*2; i++)
        putchar(' ');
    printf("- frag: %s\n", cur.fragment.c_str());
    for (size_t i = 0; i < level*2; i++)
        putchar(' ');
    printf("  size: %llu\n", cur.totalSize);
    for (size_t i = 0; i < level*2; i++)
        putchar(' ');
    printf("  children:\n");

    for (auto &child : cur.children) {
        recursiveFragWalk(child, nextLevel);
    }
}

static void buildSymbolTreeAndPrint(std::vector<wasmobj::Results> &allResults,
                                    Parameters const &ctx) {
    NameFragment top;

    for (int idxResult = 0; idxResult < allResults.size(); ++idxResult) {
        auto &results = allResults[idxResult];
        auto *path = ctx.argv[idxResult];

        for (auto &fun : results.functions) {
            const auto &name = fun.name;
            auto *curFrag = &top;
            for (size_t i = 0; i < name.size(); i++) {
                auto ch = name[i];
                auto foundChild = false;
                for (auto &child : curFrag->children) {
                    if (child.fragment[0] == ch) {
                        foundChild = true;
                        curFrag = &child;
                        break;
                    }
                }

                if (!foundChild) {
                    curFrag->children.emplace_back(ch);
                    curFrag = &curFrag->children.back();
                }
            }
            curFrag->totalSize += fun.size;
        }
    }

    collapseNameFragments(top);
    top.totalSize = propagateSizes(top);
    recursiveFragWalk(top);
    return;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [-ad] [inputfile [inputfile [...]]]\n",
                argv[0]);
        fprintf(stderr, "  inputfile Path to a WASM object file\n");
        fprintf(stderr, "  -a Aggregate results\n");
        fprintf(stderr, "  -T Symbol tree\n");
        fprintf(stderr, "  -d [EXPERIMENTAL] Try to demangle symbol names\n");
        return 1;
    }

    Parameters ctx = {};

    auto argvInputFiles = std::make_unique<char *[]>(argc);
    ctx.argv = argvInputFiles.get();

    for (int idxArg = 1; idxArg < argc; ++idxArg) {
        if (strcmp(argv[idxArg], "-a") == 0) {
            ctx.flagAggregate = true;
        } else if (strcmp(argv[idxArg], "-d") == 0) {
            ctx.flagDemangle = true;
        } else if (strcmp(argv[idxArg], "-T") == 0) {
            ctx.flagSymbolTree = true;
        } else {
            argvInputFiles[ctx.argc] = argv[idxArg];
            ctx.argc++;
        }
    }

    std::vector<wasmobj::Results> allResults;
    allResults.reserve(ctx.argc);

    for (int idxArg = 0; idxArg < ctx.argc; ++idxArg) {
        auto results = wasmobj::Results();
        wasmobj::ProcessFile(results, ctx.argv[idxArg]);
        allResults.push_back(std::move(results));
    }

    if (ctx.flagAggregate) {
        aggregateAndPrint(allResults, ctx);
    } else if (ctx.flagSymbolTree) {
        buildSymbolTreeAndPrint(allResults, ctx);
    } else {
        printResults(allResults, ctx);
    }

    return 0;
}
