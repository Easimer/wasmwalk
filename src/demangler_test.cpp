#include <cstdint>
#include <memory>
#include <cstdio>

#include "demangler.h"

static const char *signatures[] = {
    "_ZN3nms5EventC2ERKS0_",
    "NSt3__214__thread_proxyINS_5tupleIJNS_10unique_ptrINS_15__thread_structENS_14default_deleteIS3_EEEEZN3nms12ModuleThread11StartThreadERNS_7promiseIbEEE3$_0EEEEEPvSE_",
    "main",
    "__GLOBAL_sub_I_entry.cpp",
    "_Z4testIifEvT_T0_",
    "_ZN3nms3asdIfiE1fINS0_IvdEEEEiv",
    "_Z1fI1tI1tI1tI1tI1tI1tI1tI1tI1tIEEEEEEEEEEvv",
    "_ZNSt3__212__hash_tableINS_17__hash_value_typeIN3nms6Engine6StatusENS_12basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEEEENS_22__unordered_map_hasherIS4_SB_NS_4hashIjEENS_8equal_toIS4_EELb1EEENS_21__unordered_map_equalIS4_SB_SG_SE_Lb1EEENS8_ISB_EEE25__emplace_unique_key_argsIS4_JRKNS_4pairIKS4_SA_EEEEENSN_INS_15__hash_iteratorIPNS_11__hash_nodeISB_PvEEEEbEERKT_DpOT0_",
    "_ZNSt3__220__shared_ptr_emplaceIN3nms8ViewWrapENS_9allocatorIS2_EEED2Ev",
    "_ZN1N1TIiiE2mfES0_IddE",
    nullptr
};

int main(int argc, char** argv) { 
    auto mem = std::make_unique<uint8_t[]>(1024 * 1024);
    demangler::Arena arena = {mem.get(), 1024 * 1024, 0};

    for (unsigned i = 0; signatures[i] != nullptr; i++) {
        printf("TESTING '%s'\n\n", signatures[i]);
        auto dm = demangler::demangle(
            demangler::StringView::FromLiteral(signatures[i]), arena);
        demangler::printDemangledName(dm);
        printf("\n");
        arena.FreeAll();
    }
    return 0;
}