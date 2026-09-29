


#ifndef LLVM_ANALYSIS_ALLPRIVATEDETECT_HEADER
#define LLVM_ANALYSIS_ALLPRIVATEDETECT_HEADER

#define DEBUG_TYPE "paramget"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
//
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/InstIterator.h"

//passes we use
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include <vector>
#include <string>
#include <unordered_map>
//passes we developed
#include "llvm/Analysis/DependencyCheck.h"

using namespace llvm;
using namespace std;

namespace llvm {
	class AllPrivateDetect : public FunctionPass 
	{
	public:
		static char ID;
		AllPrivateDetect() : FunctionPass(ID) {}

		void getAnalysisUsage(AnalysisUsage &AU) const override{
			AU.addRequired<LoopInfoWrapperPass>();
			AU.addRequired<DependenceAnalysisWrapperPass>();
      AU.addRequired<ScalarEvolutionWrapperPass>();
      AU.addRequired<DependencyCheck>();
      //AU.setPreservesAll();
		}
    
    typedef vector<Instruction*> InstVec;
    typedef map<string,Value*> ValMap;
    typedef map<string,BasicBlock*> BBMap;
    typedef unordered_map<string, ValMap> MapMap;
    LoopInfo *LI;
    DependenceInfo *DI;
    ScalarEvolution *SE;
    DependencyCheck *DC;
    //
    ValMap AllPrivateMap;
    ValMap PrivateMap;
    ValMap LastPrivateMap;
    ValMap FirstPrivateMap;
    //
    BBMap *AllPathBBMap;
    MapMap *LoopCounterMap;
    ValMap *CanDepMap;
    // MapMap FullPrivateMaps;

    bool runOnFunction(Function &F) override;
    void startCheck();

    ValMap &getAllPrivate() {return AllPrivateMap;}
    const ValMap &getAllPrivate() const {return AllPrivateMap;}
    ValMap &getPrivate() {return PrivateMap;}
    const ValMap &getPrivate() const {return PrivateMap;}
    ValMap &getFirstPrivate() {return FirstPrivateMap;}
    const ValMap &getFirstPrivate() const {return FirstPrivateMap;}
    ValMap &getLastPrivate() {return LastPrivateMap;}
    const ValMap &getLastPrivate() const {return LastPrivateMap;}

    void checkRecursively(Loop* L);
    void checkLoop(Loop* L);

    //check each clause candidate
    bool checkLastPrivateCandidate(Loop* L, string loopCounterName);
    bool checkPrivateCandidate(Loop* L, string loopCounterName);
    bool checkFirstPrivateCandidate(Loop* L,  string loopCounterName, ConstantInt* incNum);

    //private detect common
    bool isPrivatizable(Value* CheckValue, Instruction* DependInst, Loop* L,    const string loopCounterName);
    bool checkGEPDependency(Instruction* DependInst, Instruction* CheckI, Loop* L);
    bool checkLoadDependency(const string loopCounterName, Instruction* CheckI);
    bool checkPHIDependency(const string loopCounterName, Instruction* CheckI, Loop* L);
    //
    bool isLoadedAfterStored(Value* checkingMemory, Loop* L);
    void checkBranchRecursively(string checkingMemoryName, Loop* L, string headerName, BasicBlock* BB, bool &LoadAfterStoreFlag);
    bool checkBB(string checkingMemoryName, Loop* L, BasicBlock* BB,bool &LoadAfterStoreFlag);
    void getSuccessorBBs(Instruction* checkI, vector<BasicBlock*> sucBBs);

    //private detect
    bool recordPrivate( string loopCounterName, Loop* L);

    // lastprivate detect
    bool recordLastPrivate( string loopCounterName, Loop* L);
    bool isUsedAfterLoop(Value* checkingMemory,Loop* L);
    bool isIndependentValue(Value* checkV, const string loopCounterName,  Loop* L);

    //firtprivate detect
    ConstantInt* getConstantLoopStep(Value* counterV);
    bool recordFirstPrivate( string loopCounterName,  Loop* L,   ConstantInt* incNum);
    Value* getRootOfMemory(Value* checkingValue);
    bool isAccessingSameArray(const string arrayName, Value* checkingValue);
    void collectDependingLoad(Instruction* storeI, Value* checkingValue,  Loop* L, vector<Value*> &DepLoadVec);

  };
}

#endif
