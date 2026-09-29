add_llvm_component_library(LLVMAutoParallel
  AllPrivateDetect.cpp
  AutoParallelPasses.cpp
  DependencyCheck.cpp
  DirectiveInsertion.cpp
  ParamGet.cpp
  ReductionDetect.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Analysis
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/Scalar

  DEPENDS
  intrinsics_gen

  COMPONENT_NAME
  AutoParallel

  LINK_COMPONENTS
  Analysis
  Core
  Support
  TransformUtils
)
