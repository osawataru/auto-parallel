#ifndef LLVM_TRANSFORMS_AUTOPARALLEL_H
#define LLVM_TRANSFORMS_AUTOPARALLEL_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class AutoParallelDirectivePass
    : public PassInfoMixin<AutoParallelDirectivePass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

class AutoParallelParamGetPass
    : public PassInfoMixin<AutoParallelParamGetPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif
