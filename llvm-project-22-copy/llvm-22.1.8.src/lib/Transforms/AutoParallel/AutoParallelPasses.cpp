#include "llvm/Transforms/AutoParallel.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"

using namespace llvm;

extern "C" ModulePass *createAutoParallelDirectiveInsertionPass();
extern "C" ModulePass *createAutoParallelParamGetPass();

PreservedAnalyses
AutoParallelDirectivePass::run(Module &M, ModuleAnalysisManager &AM) {
  legacy::PassManager PM;
  PM.add(createAutoParallelDirectiveInsertionPass());
  PM.run(M);
  return PreservedAnalyses::none();
}

PreservedAnalyses
AutoParallelParamGetPass::run(Module &M, ModuleAnalysisManager &AM) {
  legacy::PassManager PM;
  PM.add(createAutoParallelParamGetPass());
  PM.run(M);
  return PreservedAnalyses::none();
}
