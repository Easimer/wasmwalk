#include <cstdio>
#include <algorithm>

#include "wasmobj.h"

enum SymbolKind {
    SYMBOL_FUNCTION = 0,
    SYMBOL_DATA = 1,
    SYMBOL_MAX
};

static void aggregateAndPrint(std::vector<wasmobj::Results> const &allResults, char **argv) {
    struct Symbol {
        const char *path;
        const char *name;
        SymbolKind kind;
        uint64_t size;
    };
    std::vector<Symbol> data;

    for (int idxResult = 0; idxResult < allResults.size(); ++idxResult) {
        auto &results = allResults[idxResult];
        auto *path = argv[idxResult + 1];

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

    printf("symbols:\n");
    for (auto &sym : data) {
        printf("  - path: %s\n", sym.path);
        printf("    name: %s\n", sym.name);
        printf("    kind: %d\n", sym.kind);
        printf("    size: %llu\n", sym.size);
    }
}

static void printResults(std::vector<wasmobj::Results> &allResults, char **argv) {
    for (int idxResult = 0; idxResult < allResults.size(); ++idxResult) {
        auto &results = allResults[idxResult];
        auto *path = argv[idxResult + 1];

        std::sort(results.functions.begin(), results.functions.end(),
                  [](auto &lhs, auto &rhs) { return rhs.size < lhs.size; });
        std::sort(results.data.begin(), results.data.end(),
                  [](auto &lhs, auto &rhs) { return rhs.size < lhs.size; });

        printf("%s:\n", path);
        printf("  functions:\n");
        for (auto &fun : results.functions) {
            printf("    - name: %s\n", fun.name.c_str());
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
        fprintf(stderr, "Usage: %s [-a] [inputfile [inputfile [...]]]\n", argv[0]);
        return 1;
    }

    bool flagAggregate = false;

    for (int idxArg = 1; idxArg < argc; ++idxArg) {
        if (strcmp(argv[idxArg], "-a") == 0) {
            flagAggregate = true;
        }
    }

    std::vector<wasmobj::Results> allResults;
    allResults.reserve(argc);


    for (int idxArg = 1; idxArg < argc; ++idxArg) {
        auto results = wasmobj::Results();
        wasmobj::ProcessFile(results, argv[idxArg]);
        allResults.push_back(std::move(results));
    }

    if (flagAggregate) {
        aggregateAndPrint(allResults, argv);
    } else {
        printResults(allResults, argv);
    }

    return 0;
}
