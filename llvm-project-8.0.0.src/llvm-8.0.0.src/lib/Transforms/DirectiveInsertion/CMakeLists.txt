add_llvm_library(LLVMDirectiveInsertion  MODULE
  DirectiveInsertion.cpp

  DEPENDS
  intrinsics_gen
  PLUGIN_TOOL
  opt
  )
