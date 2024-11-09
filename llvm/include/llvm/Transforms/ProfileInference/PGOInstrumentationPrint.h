#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_PGOINSTRUMENTATIONPRINT_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_PGOINSTRUMENTATIONPRINT_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class PGOInstrumentationPrint : public PassInfoMixin<PGOInstrumentationPrint> {
public:
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_PGOINSTRUMENTATIONPRINT_H