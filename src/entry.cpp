#include <cstdio>
#include <algorithm>
#include <memory>

#include "demangler.h"
#include "wasmobj.h"

enum SymbolKind {
    SYMBOL_FUNCTION = 0,
    SYMBOL_DATA = 1,
    SYMBOL_MAX
};

struct Parameters {
    bool flagAggregate = false;
    bool flagDemangle = false;

    unsigned argc = 0;
    char **argv = nullptr;
};

class Demangler {
  public:
    Demangler()
        : mem(std::make_unique<u8[]>(1024 * 1024)), arena{mem.get(),
                                                          1024 * 1024, 0} {}

    void printDemangledName(const char* s) {
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

    void printDemangledName(std::string const& s) {
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

static void aggregateAndPrint(std::vector<wasmobj::Results> const &allResults, Parameters const &ctx) {
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

static void printResults(std::vector<wasmobj::Results> &allResults, Parameters const &ctx) {
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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [-ad] [inputfile [inputfile [...]]]\n", argv[0]);
        fprintf(stderr, "  inputfile Path to a WASM object file\n");
        fprintf(stderr, "  -a Aggregate results\n");
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
    } else {
        printResults(allResults, ctx);
    }

    return 0;
}
