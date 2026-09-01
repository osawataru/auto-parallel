#ifndef AUTO_PARALLEL_DIRECTIVE_MARKER_H
#define AUTO_PARALLEL_DIRECTIVE_MARKER_H

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

namespace llvm::auto_parallel {

// Compatibility boundary: change only these helpers if the marker is later
// restored to an LLVM intrinsic or replaced by another representation.
inline constexpr char DirectiveFunctionName[] = "__auto_parallel_directive";
inline constexpr char DirectiveMetadataName[] = "auto_parallel";

inline FunctionCallee getOrInsertDirectiveMarker(Module &M) {
  FunctionType *Ty = FunctionType::get(Type::getVoidTy(M.getContext()), false);
  return M.getOrInsertFunction(DirectiveFunctionName, Ty);
}

inline CallInst *createDirectiveMarker(IRBuilder<> &Builder, Module &M,
                                       ArrayRef<Metadata *> Clauses) {
  CallInst *Call = Builder.CreateCall(getOrInsertDirectiveMarker(M), {});
  Call->setMetadata(DirectiveMetadataName,
                    MDNode::get(M.getContext(), Clauses));
  return Call;
}

inline MDNode *getDirectiveMetadata(const Instruction &I) {
  return I.getMetadata(DirectiveMetadataName);
}

} // namespace llvm::auto_parallel

#endif
