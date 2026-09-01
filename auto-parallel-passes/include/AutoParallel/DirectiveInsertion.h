#ifndef LLVM_TRANSFORMS_SCALAR_DirectiveInsertion_HEADER
#define LLVM_TRANSFORMS_SCALAR_DirectiveInsertion_HEADER
#define DEBUG_TYPE "paramget"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstIterator.h"

//===for IR Builder===//
#include <map>
#include <vector>
#include <string>
#include <set>
#include <float.h>//for float max, min
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/InstIterator.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

//---for pass---//
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Scalar.h"///
#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/DependenceAnalysis.h"
//- for our analysis pass -//
#include "AutoParallel/DependencyCheck.h"
#include "AutoParallel/AllPrivateDetect.h"
#include "AutoParallel/ReductionDetect.h"


#define FIRST_PRIVATE_DEBUG 0

using namespace llvm;
using namespace std;
namespace {
	
	class DirectiveInsertion : public ModulePass 
	{
	public:
		static char ID;
		DirectiveInsertion() : ModulePass(ID) {} 

		void getAnalysisUsage(AnalysisUsage &AU) const override{
			AU.addRequired<LoopInfoWrapperPass>();
			AU.addRequired<DependenceAnalysisWrapperPass>();
			AU.addRequired<AllPrivateDetect>();
      //		AU.addRequired<ScalarEvolutionWrapperPass>();//added for AllPrivateDetect
			AU.addRequired<ReductionDetect>(); // if you want to debug one pass, comment out this, and use of getAnalysisUsage<>() in source
			//共存できない．ReductionDetectを追加すると，AllPrivateDetectを実行した後，エラーになる．
			AU.addRequired<DependencyCheck>();
			AU.setPreservesAll();
		}
		
		typedef vector<Instruction*> InstVec;
		typedef map<string,Value*> ValMap;
		typedef map<string,BasicBlock*> BBMap;
		typedef unordered_map<string, ValMap> MapMap;

		LoopInfo *LI;
		DependenceInfo *DI;
		
		ValMap AllPrivateMap;
		ValMap PrivateMap;
		ValMap FirstPrivateMap;
		ValMap LastPrivateMap;
		ValMap ReductionableMap;
		//
		ValMap CanDepMap;

		bool runOnModule(Module &M) override;

		//insert directive if thre is no loop carried dependency
		void checkRecursively(Module &M, IRBuilder<> &Builder, DependencyCheck *DC,  Loop* L);
		void insertDirective(Module &M, IRBuilder<> &Builder, DependencyCheck *DC, Loop* L);

		//declaration of our parallelization directive
		void insert_llvm_directive_decl(Module &M, IRBuilder<> &Builder);
		void create_llvm_directive_call(Module &M, IRBuilder<> &Builder,  Loop *L);
		string getPrivateClause(string clauseStr, ValMap checkingMap,  Loop *L);
		string getReductionClause(Loop *L);

		// 2024 added
		ValMap UseAllCounterMap;
    unordered_map<string, pair<Value*, Value*>> UsePartialCounterMap;

		Type* getArrayElemTy(Type* type);
		bool hasStructElemPointerTy(StructType* type);
		bool hasSameAddr(Module &M, Function &checkF, unsigned argNo);
		void getValueUse(Value* V, Function &F, vector<Value*>* vec);
	};
}
char DirectiveInsertion::ID = 0;

//INITIALIZE_PASS_DEPENDENCY(IndVarSimplifyPass)
static RegisterPass<DirectiveInsertion> X("directiveinsertion", "Insert parallelization directive at IR-level.", true, false);


#endif
