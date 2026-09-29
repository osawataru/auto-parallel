#ifndef AUTO_PARALLEL_DIRECTIVE_MARKER_H
#define AUTO_PARALLEL_DIRECTIVE_MARKER_H

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"

namespace llvm::auto_parallel {

inline FunctionCallee getOrInsertDirectiveMarker(Module &M) {
  return Intrinsic::getOrInsertDeclaration(&M, Intrinsic::directive);
}

inline CallInst *createDirectiveMarker(IRBuilder<> &Builder, Module &M,
                                       ArrayRef<Metadata *> Clauses) {
  MDNode *Directive = MDNode::get(M.getContext(), Clauses);
  Value *MetadataArg = MetadataAsValue::get(M.getContext(), Directive);
  return Builder.CreateCall(getOrInsertDirectiveMarker(M), {MetadataArg});
}

inline MDNode *getDirectiveMetadata(const Instruction &I) {
  const auto *Call = dyn_cast<CallBase>(&I);
  if (!Call || Call->getIntrinsicID() != Intrinsic::directive ||
      Call->arg_empty())
    return nullptr;
  const auto *MAV = dyn_cast<MetadataAsValue>(Call->getArgOperand(0));
  return MAV ? dyn_cast<MDNode>(MAV->getMetadata()) : nullptr;
}

} // namespace llvm::auto_parallel

#endif
