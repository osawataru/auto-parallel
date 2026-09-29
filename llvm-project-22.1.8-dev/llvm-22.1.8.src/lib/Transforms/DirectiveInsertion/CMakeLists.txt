add_llvm_component_library(LLVMDirectiveInsertion
  DirectiveInsertion.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Analysis
  Core
  ScalarOpts
  Support
  TransformUtils
)
