set(LLVM_LINK_COMPONENTS
  Analysis
  Core
  DirectiveInsertion
  InstCombine
  IPO
  IRReader
  ScalarOpts
  Support
  Target
  TransformUtils
)

add_llvm_tool(auto-parallel-legacy-driver
  AutoParallelLegacyDriver.cpp
)
