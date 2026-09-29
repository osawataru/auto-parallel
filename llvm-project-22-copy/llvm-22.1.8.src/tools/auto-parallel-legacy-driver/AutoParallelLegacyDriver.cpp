#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"

using namespace llvm;

extern "C" ModulePass *createAutoParallelDirectiveInsertionPass();
extern "C" ModulePass *createAutoParallelParamGetPass();

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input LLVM IR>"),
                                          cl::Required);
static cl::opt<std::string> OutputFilename("o", cl::desc("Output LLVM IR"),
                                           cl::value_desc("filename"),
                                           cl::Required);
enum class StageKind { Directive, ParamGet };

static cl::opt<StageKind> Stage(
    "stage", cl::desc("Legacy pass stage"),
    cl::values(clEnumValN(StageKind::Directive, "directive", "Insert directives"),
               clEnumValN(StageKind::ParamGet, "paramget", "Generate OpenMP IR")),
    cl::Required);

int main(int argc, char **argv) {
  InitLLVM Init(argc, argv);
  cl::ParseCommandLineOptions(argc, argv,
                              "LLVM 22 auto-parallel Legacy PM driver\n");

  PassRegistry &Registry = *PassRegistry::getPassRegistry();
  initializeCore(Registry);
  initializeTransformUtils(Registry);
  initializeScalarOpts(Registry);
  initializeInstCombine(Registry);
  initializeAnalysis(Registry);
  initializeIPO(Registry);
  initializeTarget(Registry);

  LLVMContext Context;
  SMDiagnostic Diagnostic;
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Diagnostic, Context);
  if (!M) {
    Diagnostic.print(argv[0], errs());
    return 1;
  }

  legacy::PassManager PM;
  if (Stage == StageKind::Directive)
    PM.add(createAutoParallelDirectiveInsertionPass());
  else if (Stage == StageKind::ParamGet)
    PM.add(createAutoParallelParamGetPass());
  else {
    errs() << "unknown stage\n";
    return 1;
  }

  PM.run(*M);

  std::error_code EC;
  ToolOutputFile Output(OutputFilename, EC, sys::fs::OF_None);
  if (EC) {
    errs() << "cannot open output file '" << OutputFilename << "': "
           << EC.message() << '\n';
    return 1;
  }
  M->print(Output.os(), nullptr);
  Output.keep();
  return 0;
}
