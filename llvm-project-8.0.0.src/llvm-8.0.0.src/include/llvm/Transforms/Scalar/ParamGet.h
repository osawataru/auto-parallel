
/**WORK LIST*****
1.Instruction DELETE
2.Intrinsic REMOVE
3.loop variant TRANSFERRING ::: line 286
4.Builder's CREATIVITY


	errs()<<"\t";
	print(errs()); errs()<<"\n";

********************/
#ifndef LLVM_TRANSFORMS_SCALAR_PARAMGET_HEADER
#define LLVM_TRANSFORMS_SCALAR_PARAMGET_HEADER

#define DEBUG_TYPE "paramget"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

//===for IR Builder===//
#include <map>
#include <vector>
#include <string>
#include <set>
#include <float.h>//for float max, min
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstrTypes.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

//---for pass---//
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Scalar.h"///
#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/DependenceAnalysis.h"


#include "llvm/IR/InstIterator.h"

#define FIRST_PRIVATE_DEBUG 0

using namespace llvm;
using namespace std;
namespace {
	class ParamGet : public ModulePass 
	{
	public:
		static char ID;
		ParamGet() : ModulePass(ID) { initializeParamGetPass(*PassRegistry::getPassRegistry());} 

		void getAnalysisUsage(AnalysisUsage &AU) const override{
			AU.addRequired<ScalarEvolutionWrapperPass>();
			AU.addRequired<LoopInfoWrapperPass>();
			AU.addRequired<DependenceAnalysisWrapperPass>();
		}
		/* StrVmap
		[use]
		OpenMP Runtime Library's function arugument holder.
		*/
		typedef std::map<std::string, Value*> StrVmap;
		typedef std::vector<std::pair<std::string, Value*>> StrVvector;

		/* StrImap
		[use]
		Original Instruction that will be argument of outlined function.
		*/
		typedef std::map<std::string, Instruction*> StrImap;
		typedef std::vector<std::pair<std::string, Instruction*>> StrIvector;

		/* StrBVector
		[use]
		New BB that relates to Captured Original BB.
		*/
		typedef std::vector<std::pair<std::string,BasicBlock*>> StrBVector;
		typedef std::map<std::string, BasicBlock*> StrBmap;

		/* NumImap
		[use]
		Id and Original Instruction related to id.
		*/
		typedef std::map<unsigned int, Instruction*> NumImap;

		/* NumVmap
		[use]
		Id and Built Instruction's Value*.
		Id and used var during calculation of loop condition expr.
		*/
		typedef std::map<unsigned int, Value*> NumVmap;

		/* NumIPairVector
		[use]
		Id and Use of Built Instruction related to id. 

		*/
		typedef std::vector<std::pair<unsigned int,Instruction*>> NumIPairVector;

		/* NumVPairVector
		[use]
		built pointer Instruction that point to new function's argument.
		*/
		typedef std::vector<std::pair<unsigned int,Value*>> NumVPairVector;
		



		class ParamGetCommon
		{
		public:
			enum sched_type {
			  kmp_sch_lower = 32, /**< lower bound for unordered values */
			  kmp_sch_static_chunked = 33,
			  kmp_sch_static = 34, /**< static unspecialized */
			  kmp_sch_dynamic_chunked = 35,
			  kmp_sch_guided_chunked = 36, /**< guided unspecialized */
			  kmp_sch_runtime = 37,
			  kmp_sch_auto = 38, /**< auto */
			  //kmp_sch_trapezoidal = 39 //what is trapezoidal
			};

			//for reduction clause
			enum reduction_operator {
				add,
				mul,
				sub,
				and_bin,
				or_bin,
				xor_bin,
				and_and,
				or_or,
				max,
				min
			};

			BasicBlock* parallel_region_Successor;		
			BasicBlock* static_init_BB;		
			Instruction* targetII;
			Loop* targetLoop;

			StrIvector CapturedInstVector;//vector for ordered Inst
			StrBVector CreatedBB;

			StrVmap FuncArgumentMap;
			StrVmap BuiltValMap;//Vals that are built in this function.
			StrVmap OMPParamMap;
			StrVmap LoopExpr; 
			StrVmap CapturedValMap;
			StrVmap ShareAfterLoopMap;
			StrVmap ShareFirstValMap;

			StrVvector LoopInitTraceVector;
			StrVvector LoopCondTraceVector;
			StrVvector LoopIncrTraceVector;

			unsigned int instnum = 0;
			unsigned int bnum = 0;//used for BB name.
			//unsigned int funcnum = 0;//used for omp_outlined number set
			unsigned int valnum = 0;//number of Built Instructions
			const std::string targetIIName = "target.II";
			
			unsigned int CondOperandIntegerSize;

			bool CondLHSisCounter;
			bool CondTrueIsIterate;
			bool LoopCounterIsPHI;

			//for FP to I64
			StrVmap TransformMap;

			//for private clause
			StrVmap PrivateValMap;
			StrVmap FirstPrivateValMap;
			StrVmap LastPrivateValMap;


			typedef std::map<std::string, std::pair<Value*,reduction_operator> > StrVOpmap;
			StrVOpmap ReductionValMap;
			StrVmap ReductionPHIMap;
			StrVmap ReductionRecordMap;
			StrVmap ReductionSrcMap;
			StrVmap ReductionResultMap;


			sched_type myScheduleType = kmp_sch_static;
			unsigned ChunkSize = 1;
			StrBmap DispatchBBMap;

			std::vector<std::string> ThrIndStrVector;
			StrImap ThrIndInstMap;

			void clearAllMember();
			bool searchII( Function &F );
			void searchTargetLoop( LoopInfo* LI );
			void recordTargetRegion();
			void recordNewBB(LLVMContext &TheContext);


			std::string IntToName(unsigned int valnum);
			bool CapturedInstVector_empty(){ return CapturedInstVector.empty(); }

			void insertCapArgument(Instruction* I,	unsigned int &instnum);

			FunctionType* setOMP_outlinedFuncType(IRBuilder<> &Builder);

			Value* buildInstruction(IRBuilder<> &Builder, Instruction* I);

			void removeOriginalIR(Module &M, IRBuilder<> &Builder,
				Loop* targetLoop, BasicBlock* parallel_region_Successor);

			void judgeParameter(Value* argval,Function* ParentF);

			void judgeArgument(Instruction *linstitr);
			

			void buildFuncEntryBB(Module &M, StructType* IdentT, Loop* targetLoop,
					IRBuilder<> &Builder,Function* OutFunc);
			//---used in buildFuncEntryBB---//
			void setLoopExpr(Loop* targetLoop);
			void setLoopInitAndIncr(Instruction* CondInst,Loop* targetLoop);
			void handleInstToFindInitAndIncr(Instruction* CondInst,
				Loop* targetLoop, StrVmap &RegisterMap, std::string name);

			void setPHIInitAndIncr(PHINode* PHIN, Loop* targetLoop, StrVmap &RegisterMap, std::string name);
			void setPhiAsInitIfPhiIsUsedIn(PHINode* PHIN, Value* CheckedValue,	StrVmap &RegisterMap, std::string initstr);

			void setLoadInitAndIncr(Instruction* LoopCounterI, Loop* targetLoop, StrVmap &RegisterMap, std::string name);
			void setLoadAsInitIfLoadedMemoryIsUsedIn(Value* UsingValue, Value* LoadingValue, StrVmap &RegisterMap, std::string initstr);
			void buildInitialValueOfIndexVariable(IRBuilder<> &Builder, Loop* targetLoop);


			void insertForOMPVarAndArgument(Module &M, IRBuilder<> &Builder, Function* OutFunc,Loop* targetLoop);
			void createLocalAllocaForAnyPrivateVariable(IRBuilder<> &Builder, StrVmap& AnyPrivateMap);
			void createLocalAllocaForReductionVariable(IRBuilder<> &Builder);

			void buildAllocaForOMPVar(IRBuilder<> Builder);//actualy this doesn't Build Inst just use Builder func 


			void createAllocaForShareAfterLoop(IRBuilder<> &Builder, Loop* targetLoop);
			void createLoadForShareAfterLoop(IRBuilder<> &Builder, Loop* targetLoop);
			void createStoreForShareAfterLoop(IRBuilder<> &Builder);

			//Value* BuildInstTrace(IRBuilder<> Builder, BinaryOperator* BO);
			//actualy this doesn't Build Inst just use Builder func 

			void createIdentTGlobalVar(Module &M, IRBuilder<> &Builder,
					StructType* IdentT);

			GlobalVariable* createLocationGlobalVar(Module &M, IRBuilder<> &Builder,
					StructType* IdentT, int reserved_1, int flags,
					int reserved_2, int reserved_3, Constant* psource);

			void create_kmpc_global_thread_num_call(Module &M, IRBuilder<> &Bilder);
			//-------------------------------------//

			void insertCreatedBB(Function* OutFunc);

			void buid_static_initBB(Module &M,IRBuilder<> &Builder, Loop* targetLoop);
			//--used in buid_static_initBB--//
			void insertConstantOMPVal(IRBuilder<> &Builder);
			void buildStoreToOMPVar(IRBuilder<> &Builder);


			//--kmpc static--//
			void create_kmpc_for_static_init_4_call(Module &M, IRBuilder<> &Builder);
			void create_kmpc_for_static_init_8_call(Module &M, IRBuilder<> &Builder);
			//--kmpc dynamic--//
			void create_kmpc_dispatch_init_4_call(Module &M, IRBuilder<> &Builder);
			void create_kmpc_dispatch_init_8_call(Module &M, IRBuilder<> &Builder);
			Value* create_kmpc_dispatch_next_4_call(Module &M, IRBuilder<> &Builder);
			Value* create_kmpc_dispatch_next_8_call(Module &M, IRBuilder<> &Builder);
			//--kmpc reduction--//
			Value* create_kmpc_reduce_nowait(Module &M, Function* RedFunc, IRBuilder<> &Builder);
			void create_kmpc_end_reduce_nowait(Module &M, IRBuilder<> &Builder);

			//-------------------------------------//


			void buildPlowerLoad(IRBuilder<> &Builder, string loadInstName);
			void buildCapturedInstVector(Module &M, IRBuilder<> &IRBuilder,
				Function* OutFunc, Loop* targetLoop);
			void initializeUndefinedPrivate(IRBuilder<> &IRBuilder, StrVmap& AnyPrivateMap);
			void replaceCounterWithGEP(IRBuilder<> &Builder, std::string buildstr);
			//---used in buildCapturedInstVector---//
			void buildLocalLoopExitCondition(IRBuilder<> Builder);
			Value* getThreadLocalLoopCounter(IRBuilder<> Builder);
			void buildLocalLoopIncrement(IRBuilder<> Builder,Instruction* I);
			// void buildOriginalLoopVar(IRBuilder<> Builder);
			void build_kmpc_for_static_fini_call(Module &M, IRBuilder<> &Builder, Function* OutFunc);
			//--------------------------------------//

			void create_kmpc_fork_callForOutlinedFunc(Module &M, IRBuilder<> &Builder, Loop* targetLoop);



			void traceLoopExprForUBCalc(IRBuilder<> &Builder,Loop* targetLoop);
			void traceInstForUBCalc(Value* SrcV,StrVvector &TraceVector,
					StrVmap &RegisterMap, std::string initstr, Loop* targetLoop, bool MakeInitialValueZero);
			// void traceInstForUBCalcForIncr(IRBuilder<> &Builder, Value* SrcV,
			// 	StrVvector &TraceVector,Loop* targetLoop);
			
			//----------------
			void buildLoopUB(IRBuilder<> &Builder,Loop* targetLoop);
			Value* castToCreatedKMPCInitSize(IRBuilder<> &Builder, Value* srcV);
			void eraseAllTheUpdatingValueInTracedVector(StrVvector &TraceVector, Loop* targetLoop);
			//----------------
			Value* buildTraceVector(IRBuilder<> &Builder,StrVvector &TraceVector,Loop* targetLoop,StrVmap &RegisterMap, std::string initstr, bool NeedAbsoluteIncrForLoopCounterFlag,bool MakeInitialValueZero);
			Value* buildTracedValue(IRBuilder<> &Builder, StrVmap &RegisterMap, string initstr, Value* buildingV, string buildingName);
			Value* buildTracedInstructionWithOutLoopCarriedValue(IRBuilder<> &Builder, StrVmap &RegisterMap, std::string initstr, bool NeedAbsoluteIncrForLoopCounterFlag, Loop* targetLoop, Instruction* buildTargetI, string buildingName);
			Value* isIncludingLoopCarriedVariable(Instruction* checkingI, Value* carriedV);
			void replaceIncValWithAbsVal(IRBuilder<> &Builder, Instruction* buildTargetI, Value* IncrVal, Value* newOper);		

			//-- set the InitIsBiggerFlag
			void judgeInitIsBigger(IRBuilder<> &Builder,
					bool &InitIsBiggerFlag, bool &IncludeEqualFlag);
			Instruction* getCounterIncrementingInst(Instruction* startI);



			void captureForIntrinsicMetadata(Module &M, IRBuilder<> &Builder, Loop* targetLoop);
			void handleClauses(Module &M, IRBuilder<> &Builder, std::string metastr,
				Loop* targetLoop, Instruction* II);
			void transformScalars(IRBuilder<> &Builder);
			void retransformScalars(IRBuilder<> &Builder);

			//---proto-type for new functionality---//
			void createReductionCollect(Module &M, Function* OutFunc, 
				IRBuilder<> &Builder, Loop* targetLoop);
			void buildReductionFuncBody(Function* RedFunc, IRBuilder<> &Builder, Loop* targetLoop);
			void storeReductionResult(IRBuilder<> &Builder, Loop* targetLoop);
			Value* findInitialValueForPhi(Loop* targetLoop, 
				PHINode* RedPHI, std::string redname);
			Value* getInitialValueForReductionOperand(IRBuilder<> &Builder,
				Loop* targetLoop, Value* redV, ParamGetCommon::reduction_operator redop );
			
			void recordLoadSource(StrVmap &RedLocalMap, Instruction* I);
			Value* getInitialValueForBO(Loop* targetLoop, Value* redV);
			Value* getCmpInstForMaxMin(Loop* targetLoop, Value* redV);
			Value* getSelectInst(Value* judgeV);
			Value* createOtherInstForRedVar(IRBuilder<> &Builder,
				Loop* targetLoop, Value* redV, Value* newRedVal);
			Value* getInstAfterSelect(IRBuilder<> &Builder, Value* SelectV, Value* resultV);
			void createStoreForLastPrivate(IRBuilder<> &Builder);
			void createLoadForLastPrivate(IRBuilder<> &Builder, Loop* targetLoop);

			unsigned int calcSizeOfType(Type* priT);
			unsigned int calcAlignOfType(Type* priT);


			void insertDeclOfKMPC(Module &M, IRBuilder<> &Builder, StructType* IdentT);
			void insert_kmpc_global_thread_num_decl(Module &M, IRBuilder<> &Builder,
					StructType* IdentT);
			//--static--//
			void insert_kmpc_for_static_init_4_decl(Module &M, IRBuilder<> &Builder,
					StructType* IdentT);
			void insert_kmpc_for_static_init_8_decl(Module &M, IRBuilder<> &Builder,
					StructType* IdentT);
			void insert_kmpc_for_static_fini_decl(Module &M, IRBuilder<> &Builder,
					StructType* IdentT);
			//--dynamic--//
			void insert_kmpc_dispatch_init_4_decl(Module &M, IRBuilder<> &Builder,
					StructType* IdentT);
			void insert_kmpc_dispatch_init_8_decl(Module &M, IRBuilder<> &Builder,
					StructType* IdentT);		
			void insert_kmpc_dispatch_next_4_decl(Module &M, IRBuilder<> &Builder,
					StructType* IdentT);
			void insert_kmpc_dispatch_next_8_decl(Module &M, IRBuilder<> &Builder,
					StructType* IdentT);
			// void insert_kmpc_dispatch_fini_4_decl(Module &M, IRBuilder<> &Builder,
			// 		StructType* IdentT);
			// void insert_kmpc_dispatch_fini_8_decl(Module &M, IRBuilder<> &Builder,
			// 		StructType* IdentT);
			//--fork--//
			void insert_kmpc_fork_call_decl(Module &M, IRBuilder<> &Builder,
					StructType* IdentT);
			//--reduction--//
			void insert_kmpc_reduce_nowait_decl(Module &M, IRBuilder<> &Builder,
					StructType* IdentT);
			void insert_kmpc_end_reduce_nowait_decl(Module &M, IRBuilder<> &Builder,
					StructType* IdentT);

			void storeLocalInitialValueForReduction(IRBuilder<> &Builder, Loop* targetLoop);
			void copyInitialValForFirstPrivate(IRBuilder<> &Builder);
			void insertBounds(IRBuilder<> &Builder, Value* InitLoad, Value* CondLoad);

			void buildScheduleFinish(Module &M, IRBuilder<> &Builder,
				Function* OutFunc, Loop* targetLoop);

			void createAllocaForShareInLoop(IRBuilder<> &Builder, Loop* L);
			void buildAllocaForShareInLoop(IRBuilder<> &Builder, string argName, Value* argV);
			void createLoadForShareFirstVal(IRBuilder<> &Builder);

			bool isLoopCounter(Value* checkV);
			//void ThrIndBBBuild(Module &M, IRBuilder<> &Builder,Loop* targetLoop);
			//void TraceAndPushback(Instruction* SrcI,StrVvector &TraceVector);

		};//-----------end of class-------//



		bool runOnModule(Module &M) override;

		Loop* captureInstructionForLoop(Function &F, LLVMContext &TheContext, ParamGetCommon &PGC);

		//void judgeArgument(Instruction* linstitr, StrIvector &CapturedInstVector,
		//		StrImap &FuncArgumentMap);
		//void judgeParameter(Instruction* arginst,StrIvector &CapturedInstVector,
		//		StrImap &FuncArgumentMap);
	};

	void ParamGet::ParamGetCommon::clearAllMember()
	{
		this->parallel_region_Successor = nullptr;	
		this->static_init_BB = nullptr;
		this->targetII = nullptr;
		this->targetLoop = nullptr;
		this->CapturedInstVector.clear();//vector for ordered Inst
		this->FuncArgumentMap.clear();
		this->CreatedBB.clear();
		this->BuiltValMap.clear();//Vals that are built in this function.
		this->OMPParamMap.clear();
		this->LoopExpr.clear();
		this->CapturedValMap.clear();
		this->ShareAfterLoopMap.clear();
		this->ShareFirstValMap.clear();

		this->LoopInitTraceVector.clear();
		this->LoopCondTraceVector.clear();
		this->LoopIncrTraceVector.clear();

		//we count instnum and bnum throughout this pass
		//this is because if count is reset, same name inst maybe appears 
		//and name reference must be wrong
		//But, I think that if number is preserved, there could be limit of progmram size
		// that we can handle by this pass.

		this->instnum = 0;
		this->bnum = 0;//used for BB name.
		this->valnum = 0;//number of Built Instructions
		//this->funcnum = 0;
		//funcnum should keep its value since omp_outlined may be made several time.
		this->CondLHSisCounter=true;

		this->TransformMap.clear();
		this->PrivateValMap.clear();
		this->FirstPrivateValMap.clear();
		this->LastPrivateValMap.clear();
		this->ReductionValMap.clear();
		this->DispatchBBMap.clear();
		this->ThrIndStrVector.clear();
		this->ThrIndInstMap.clear();
		this->ReductionRecordMap.clear();
		this->ReductionSrcMap.clear();
		this->ReductionResultMap.clear();
	}
}
char ParamGet::ID = 0;
INITIALIZE_PASS_BEGIN(ParamGet, "paramget", "Parameter Get Pass", true, false)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(ParamGet, "paramget", "Parameter Get Pass",true, false)

ModulePass *llvm::createParamGetPass(){return new ParamGet();}


//----------------------//












#endif //LLVM_TRANSFORMS_SCALAR_PARAMGET_HEADER


