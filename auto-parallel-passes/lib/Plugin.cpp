#include "AutoParallel/AllPrivateDetect.h"
#include "AutoParallel/DependencyCheck.h"
#include "AutoParallel/DirectiveInsertion.h"
#include "AutoParallel/ParamGet.h"
#include "AutoParallel/ReductionDetect.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"

using namespace llvm;
using namespace auto_parallel;

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION,
      "AutoParallelPlugin",
      LLVM_VERSION_STRING,
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name, ModulePassManager &MPM,
               ArrayRef<PassBuilder::PipelineElement>) {
              if (Name == "directiveinsertion" || Name == "auto-directive") {
                MPM.addPass(DirectiveInsertion());
                return true;
              }
              if (Name == "paramget" || Name == "auto-paramget") {
                MPM.addPass(ParamGet());
                return true;
              }
              return false;
            });

        PB.registerPipelineParsingCallback(
            [](StringRef Name, FunctionPassManager &FPM,
               ArrayRef<PassBuilder::PipelineElement>) {
              if (Name == "dependencycheck") {
                FPM.addPass(DependencyCheck());
                return true;
              }
              if (Name == "reductiondetect") {
                FPM.addPass(ReductionDetect());
                return true;
              }
              if (Name == "allprivatedetect") {
                FPM.addPass(AllPrivateDetect());
                return true;
              }
              return false;
            });
      }};
}
