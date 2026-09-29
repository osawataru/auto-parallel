

#ifndef LLVM_ANALYSIS_DEPENDENCYCHECK_HEADER
#define LLVM_ANALYSIS_DEPENDENCYCHECK_HEADER

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
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include <map>
#include <vector>
#include <string>
#include <unordered_map>

using namespace llvm;
using namespace std;


namespace llvm {
	class DependencyCheck : public FunctionPass 
	{
	public:
		static char ID;
		DependencyCheck() : FunctionPass(ID) {
			 initializeDependencyCheckPass(*PassRegistry::getPassRegistry());
			} 

		void getAnalysisUsage(AnalysisUsage &AU) const override{
      AU.setPreservesAll();
			AU.addRequired<LoopInfoWrapperPass>();
			AU.addRequired<DependenceAnalysisWrapperPass>();
		}
    
    typedef vector<Instruction*> InstVec;
    typedef std::map<std::string, llvm::Value *> ValMap;
    typedef std::map<std::string, llvm::BasicBlock *> BBMap;
    typedef unordered_map<string, ValMap> MapMap;
    LoopInfo *LI;
    DependenceInfo *DI;
    BBMap AllPathBBMap;
    MapMap LoopCounterMap;
    ValMap CanDepMap;
    Function *targetF;
    // ConstantInt *incNum;
    // MapMap FullPrivateMaps;

    bool runOnFunction(Function &F) override;

    //Loop counter detection
    //this is required in collectDependencyCandidate to skip the loop counter (targetLoop's loop counter)
    MapMap &getLoopCounterMap(Loop* L);
    void collectLoopCounter(Loop* L);
    void traceLoopCounter(Instruction* CondInst, BasicBlock* ExitingBB, Loop* targetLoop, ValMap &LoopInfoMap, string &loopCounterName);
    // helpers
    Value* getIncrValue(Value* CheckV, const Loop* targetLoop,const string LoopCounterStr);
    Value* getInitValue(Instruction* LoopCounterLoad,Loop* targetLoop);
    bool isReferToItself(const string phistr, Instruction* CandI, const Loop* L, vector<Instruction*>* checkedCandIVec);

    //record the dependency which can be resolved by clauses
    ValMap &getDependencyCandidate(Loop* L);
    void collectDependencyCandidate(Loop* L, string loopCounterName);
    //Memory check : for now, we only check GEP
    bool checkMemoryDependency(Value* StoreMemory, Value* LoadMemory, Loop* L, string loopCounterName, bool &counterUsedFlag);
    //GEP check
    bool checkGEPSubscripts(Value* storeGEPValue, Value* loadGEPValue, Loop* L, string loopCounterName, bool &counterUsedFlag);
    void collectGEPUsedValues(GetElementPtrInst* GEPI, Loop* L,vector<pair<string,Value*>> &usedValVector);
    void traceOp(Value* usedV, Loop* L, vector<pair<string,Value*>> &usedValVector);
    bool isSameConstant(Value* storeV, Value* loadV);

    //record all path to the loop
    BBMap &getAllPathBBMap(Loop* L);
    bool recordAllPath(BasicBlock* checkBB, Loop* L);


    // 2024 added
    vector<Value*> NAVec;
    vector<Value*> AliasVec;
    vector<Value*> &getAliasVec(){return AliasVec;}
    void getNAVec();
    void getNAVec(Value* V);
  };
}

#endif