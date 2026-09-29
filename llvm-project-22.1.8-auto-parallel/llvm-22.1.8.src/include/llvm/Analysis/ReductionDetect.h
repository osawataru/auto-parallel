


#ifndef LLVM_TRANSFORMS_SCALAR_REDUCTIONDETECT_HEADER
#define LLVM_TRANSFORMS_SCALAR_REDUCTIONDETECT_HEADER

#define DEBUG_TYPE "paramget"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/raw_ostream.h"
//
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/InstIterator.h"

//passes we use
// #include "llvm/Analysis/ScalarEvolution.h"
// #include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include <vector>
#include <string>
#include <unordered_map>
//passes we developed
#include "llvm/Analysis/DependencyCheck.h"
#include "llvm/Analysis/AllPrivateDetect.h"

using namespace llvm;
using namespace std;

namespace llvm {
	class ReductionDetect : public FunctionPass 
	{
	public:
		static char ID;
		ReductionDetect() : FunctionPass(ID) {
			 initializeReductionDetectPass(*PassRegistry::getPassRegistry());
			} 

		void getAnalysisUsage(AnalysisUsage &AU) const override{
			AU.addRequired<LoopInfoWrapperPass>();
			AU.addRequired<DependenceAnalysisWrapperPass>();
      // AU.addRequired<ScalarEvolutionWrapperPass>();
      AU.addRequired<DependencyCheck>();
      //AU.addPreserved<AllPrivateDetect>();
		}
    typedef vector<Instruction*> InstVec;
    typedef map<string,Value*> ValMap;
    typedef map<string,BasicBlock*> BBMap;
    typedef unordered_map<string, ValMap> MapMap;
    LoopInfo *LI;
    DependenceInfo *DI;
    // ScalarEvolution *SE;
    DependencyCheck *DC;
    //
    BBMap *AllPathBBMap;
    MapMap *LoopCounterMap;
    ValMap *CanDepMap;
    //
    ValMap RedCalcMap;
    ValMap ReductionableMap;
    
    bool runOnFunction(Function &F) override;
    void startCheck();
    ValMap &getReductionable() {return ReductionableMap;}
    const ValMap &getReductionable() const {return ReductionableMap;}

    void checkRecursively(Loop* L);
    void checkLoop(Loop* L);

    //Loop counter detection
    //this is required in collectDependencyCandidate to skip the loop counter (targetLoop's loop counter)
    void collectLoopCounter(Loop* L, map<string, ValMap>& LoopCounterMap);
    void traceLoopCounter(Instruction* CondInst, BasicBlock* ExitingBB, Loop* targetLoop, map<string, ValMap>& LoopCounterMap);
    // helpers
    Value* getIncrValue(Value* CheckV, const Loop* targetLoop,const string LoopCounterStr);
    Value* getInitValue(Instruction* LoopCounterLoad,Loop* targetLoop);
    bool isReferToItself(const string phistr, Instruction* CandI, const Loop* L);

    //record the dependency which can be resolved by clauses
    void collectDependencyCandidate(Loop* L, DependenceInfo &DI, string loopCounterName, ValMap &CanDepMap);

    //reduction detect
    void checkReductionCandidate(Instruction* depI, Loop* L);
    bool usesReductionableOperands(Value* checkV, const string reductionName, Value* redMemory, Loop* L, bool &ReferItSelfFlag);
    bool isUsedOnlyForRedCalc(Value* usedV, const string reductionName, Value* redMemory, Loop* L);

    //2024 added
    Function* targetF;
    vector<Value*> AliasVec;
    ValMap UseAllCounterMap;
    unordered_map<string, pair<Value*, Value*>> UsePartialCounterMap;
    const ValMap &getUseAllCounterMap() const {return UseAllCounterMap;}
    const unordered_map<string, pair<Value*, Value*>> &getUsePartialCounterMap() const {return UsePartialCounterMap;}

    void getGEPUse(Value* V, vector<Value*>* vec);
    void getLoopCounterVec(Loop* L, vector<Value*>* LoopCounterVec);
    unsigned getUsedCounterDepth(vector<Value*>* GEPVec, vector<Value*>* LoopCounterVec);
  };
}

//INITIALIZE_PASS_DEPENDENCY(IndVarSimplifyPass)
// static RegisterPass<ReductionDetect> X("reductiondetect", "Detect the reduction variables which accumulate values during parallel execution.", true, false);

// FunctionPass *llvm::createSamePointerReplacePass(){return new SamePointerReplace();}

#endif