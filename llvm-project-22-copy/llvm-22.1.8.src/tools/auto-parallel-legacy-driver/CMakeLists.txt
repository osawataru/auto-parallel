set(LLVM_LINK_COMPONENTS
  Analysis
  AutoParallel
  Core
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
