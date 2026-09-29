add_llvm_library(LLVMAutoParallel MODULE
  AllPrivateDetect.cpp
  DependencyCheck.cpp
  DirectiveInsertion.cpp
  ParamGet.cpp
  ReductionDetect.cpp

  DEPENDS
  intrinsics_gen

  PLUGIN_TOOL
  opt
)
