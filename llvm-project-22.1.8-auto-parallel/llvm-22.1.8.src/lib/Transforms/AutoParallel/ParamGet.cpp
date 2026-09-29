
#include "llvm/Transforms/Scalar/ParamGet.h"
using namespace llvm;

bool ParamGet::runOnModule(Module &M)
{
	LLVMContext &TheContext = M.getContext();
	IRBuilder<> Builder(TheContext);

	ParamGetCommon PGC;
	for(auto funcitr = M.begin();funcitr != M.end(); ++funcitr)
	{
		Function& F = *funcitr;

		/*
		1.Find a loop in the function of an argument
		2.Find a loop which has a directive
		3.Record the loop for the parallel IR code generation.
		*/		
		Loop* targetLoop =	captureInstructionForLoop(/*input*/F,TheContext,/*output*/PGC);

		//Parallel function make//
		/*
		1.Find loop counter.(Find initial, last, increment values of the loop counter)
		2.Identify the directive metadata
		3.Build IR code for share variables
		4.Build the function of parallel execution
		5.Declare the libgomp funcions
		6.Build the function body
			1.Build the entry block
			2.Assign the LB and UB for each thread
			3.Insert BB's into the function
			4.Build original instructions
			5.Build the finishing BB
			6.Build fork call at original position
			7.Build store inst for share variables
		7.Remove original IR code
		*/
		if( !(PGC.CapturedInstVector.empty()) )
		{
			//(debug)
			// errs() << "\nRecorded insts:\n";
			// for(auto itr = PGC.CapturedInstVector.begin(); itr != PGC.CapturedInstVector.end(); ++itr){
			// 	Instruction* tempI = itr->second;
			// 	errs()<<"record: ";
			// 	tempI->print(errs());
			// 	errs() << "\n";
			// }
			//---//
	
			//search this func again
			funcitr--;

			//args pointer access make
			/* LoopExpr will be:
			str 	|data
			"init"	|Loop var's initialization // StoreInst
			"cond"	|Loop's exit condition expr// cmp inst
			"incr"	|Loop var's increment 		//Store inst
			*/			
			//Find loop counter
			/*
			1.Find the exiting block (it exits with conditional branch)
			2.Get the initial, last, and increment values of the loop counter
			*/
			PGC.setLoopExpr(targetLoop);
			PGC.traceLoopExprForUBCalc(Builder,targetLoop);

			PGC.captureForIntrinsicMetadata(M, Builder, targetLoop);



			//create alloca if a value in loop is used after loop
			//this function need parallel_region_Successor which is set in setLoopExpr
			PGC.createAllocaForShareAfterLoop(Builder, targetLoop);
			PGC.createAllocaForShareInLoop(Builder, targetLoop);
			errs()<<"---ScalarsToI64---\n";
			PGC.transformScalars(Builder);
			errs()<<"---end ScalarsToI64---\n";

			//-------------Argument must be determined by here-------------------
			Function* OutFunc = 
				Function::Create( PGC.setOMP_outlinedFuncType(Builder),
					Function::InternalLinkage, ".omp_outlined.", &M);
			PGC.OMPParamMap.insert(make_pair(".omp_outlined.", dyn_cast<Value>(OutFunc)));
			//Outlined Function Body
			BasicBlock* entryBB = BasicBlock::Create(TheContext,"",OutFunc);
			Builder.SetInsertPoint(entryBB);
			//ident_t
			llvm::StructType* IdentT;
			if( !( IdentT = StructType::getTypeByName(TheContext, "ident_t")) )
			{
				vector<llvm::Type* > IdentArgs;
				for (int i = 0; i<4; i++)
					IdentArgs.push_back(Builder.getInt32Ty());
				IdentArgs.push_back(PointerType::get(TheContext, 0));
				llvm::ArrayRef<llvm::Type*> IdentRef(IdentArgs);
				IdentT = llvm::StructType::create(TheContext, IdentRef, "ident_t", false);
			}

			//set CondOperandIntegerSize
			Type* loopExitConditionType = PGC.CondLHSisCounter ?
				dyn_cast<Instruction>(PGC.LoopExpr.find("cond")->second)->getOperand(0)->getType():
				dyn_cast<Instruction>(PGC.LoopExpr.find("cond")->second)->getOperand(1)->getType();
			//Lower Bound (for branch)
			if( loopExitConditionType->isPointerTy() ){
				PGC.CondOperandIntegerSize = 64;
			}else if(loopExitConditionType->isIntegerTy()){
				PGC.CondOperandIntegerSize = loopExitConditionType->getIntegerBitWidth();//this is member variable
			}else{
				errs()<<"cond is what type?\n";
			}

			//EntryBBBuild
			PGC.buildFuncEntryBB(/*Input*/M,IdentT,targetLoop,Builder,/*OutPut*/OutFunc);
			errs()<<"buildFuncEntryBB OK\n";
			
			//libgomp library declare			
			PGC.insertDeclOfKMPC(M,Builder,IdentT);
			//(debug)			
			// if( AllocaInst* AI = dyn_cast<AllocaInst>( PGC.OMPParamMap.find("plower")->second ) )
			// {
			// 	errs()<<"AllocaInst cast is OK\n";
			// 	if( dyn_cast<IntegerType>(AI->getType()) )
			// 	{
			// 		errs()<<"IntegerType Cast is OK\n";
			// 		errs()<<"BitWidth is \t"<<dyn_cast<IntegerType>(AI->getType())->getBitWidth()<<"\n";
			// 	}
			// }

			//-------------------------------------------------//
			//Lower Bound (for branch)
			//static_init_BB insert
			BasicBlock* static_init_BB = BasicBlock::Create(TheContext,"",OutFunc);
			Builder.CreateBr(static_init_BB);//br from entryBB to static_init_BB
			Builder.SetInsertPoint(static_init_BB);

			PGC.static_init_BB = static_init_BB;



			//dependency between PGC.build_static_initBB
			auto bbitr = PGC.CreatedBB.begin();
			PGC.DispatchBBMap.insert(make_pair(
				"body",
				bbitr->second
			));
			PGC.DispatchBBMap.insert(make_pair(
				"preheader",
				BasicBlock::Create(TheContext)
			));			
			PGC.DispatchBBMap.insert(make_pair(
				"dispatch",
				PGC.CreatedBB.rbegin()->second
			));	
			PGC.DispatchBBMap.insert(make_pair(
				"dispatch_inc",
				BasicBlock::Create(TheContext)
			));	
			PGC.DispatchBBMap.insert(make_pair(
				"end",
				BasicBlock::Create(TheContext)
			));
			//CreatedBB insert
			// errs() << "Contained BasicBlock insert:\n";
			PGC.insertCreatedBB(OutFunc);
			//not used for canonicalization
			// PGC.DispatchBBMap.insert(make_pair(
			// 	"LBCheck",
			// 	BasicBlock::Create(TheContext)
			// ));


			//Assign the LB and UB for each thread
			//static_init_BB body build
			// "plower_Load"
			// "pupper_Load"
			PGC.buid_static_initBB(M,Builder,targetLoop);
			// errs()<<"buid_static_initBB OK\n";


			//---ThreadIndependen Expr Make---//
			// if(!PGC.ThrIndStrVector.empty())
			// {
			// 	BasicBlock* ThrIndBB = BasicBlock::Create(TheContext,"",OutFunc);
			// 	Builder.CreateBr(ThrIndBB);
			// 	Builder.SetInsertPoint(ThrIndBB);
			// 	errs()<<"ThrIndBuild Start\n";
			// 	PGC.ThrIndBBBuild(M,Builder,targetLoop);
			// }



			errs() << "----------Captured Instruction make start--------------\n";
			Builder.SetInsertPoint(bbitr->second);

			//CapturedInst build
			PGC.buildCapturedInstVector(M,Builder,OutFunc,targetLoop);


			errs()<<"Exiting Make\n";
			PGC.buildScheduleFinish(M,Builder,OutFunc,targetLoop);

			//store lastprivate variables
			if( !PGC.LastPrivateValMap.empty() )
			{
				errs()<<"---LastPrivateStore Start---\n";
				//TheContext
				PGC.createStoreForLastPrivate(Builder);
				// 	reditr end	
				errs()<<"---LastPrivateStore End---\n";				
			}

			//create kmpc_reduce_nowait call
			if( !PGC.ReductionValMap.empty() )
			{
				errs()<<"---Reduction Start---\n";
				//TheContext
				PGC.createReductionCollect(M,OutFunc,Builder,targetLoop);
				// 	reditr end	
				errs()<<"---Reduction End---\n";
			}

			Builder.CreateRetVoid();
			errs()<<"Exiting Make OK\n";

			errs()<<"Create fork call\n";
			//create kmpc_fork_call in BuiltValMap's first inst's BB
			//shareval's load and lastprivate's load is created in this process
			PGC.create_kmpc_fork_callForOutlinedFunc(M,Builder,targetLoop);	
			errs()<<"Create fork call OK\n";


			//create Store to Value used after loop
			errs()<<"Store for shared value after loop\n";
			PGC.createStoreForShareAfterLoop(Builder);
			errs()<<"Store for shared value after loop OK\n";


			errs()<<"RemoveOriginal IR\n";
			PGC.removeOriginalIR(M,Builder,targetLoop,
				PGC.parallel_region_Successor);
			errs()<<"RemoveOriginal IR OK\n";


			//(debug)//
			errs()<<"Gained FuncBody:\n";
			int bbcount = 0;
			for(auto bbitr = OutFunc->begin();bbitr != OutFunc->end(); ++bbitr)
			{
				BasicBlock &B = *bbitr;
				//errs()<< B.getName().str() << "\n";
				errs()<<"BB="<<bbcount++<<"\n";
				for(auto institr = B.begin();institr != B.end(); ++institr)
				{
					Instruction &I = *institr;
					I.print(errs());errs()<<"\n";
				}
			}
			//----//

			PGC.clearAllMember();

			//debug
			errs()<<"Finish parallelizing: ";
			if(F.hasName())
			{
				errs() << F.getName();
			}
			errs()<<"\n";

		}//end of "if( !(CapturedInstVector.empty()) ){"
		// else if( PGC.ReductionRecordMap.empty() )
		// {
		// 	PGC.ReductionRecordMap.clear();
		// }

	}//end of funcitr


	// (debug) show all function
	// for(auto funcitr = M.begin();funcitr != M.end(); ++funcitr)
	// {
	// 	Function& F = *funcitr;
	// 	if(F.hasName())
	// 	{
	// 		errs()<<"---Function: "<<F.getName()<<"---\n";
	// 	}
	// 	for(auto bbitr = F.begin(); bbitr != F.end(); ++bbitr)
	// 	{
	// 		BasicBlock& B = *bbitr;
	// 		if(B.hasName())
	// 		{
	// 			errs()<<"--BasicBlock: "<<B.getName()<<"--\n";
	// 		}
	// 		for(auto institr = B.begin(); institr != B.end(); ++institr)
	// 		{
	// 			Instruction &I = *institr;
	// 			I.print(errs());
	// 			errs()<<"\n";
	// 		}
	// 		errs()<<"\n";
	// 	}
	// 	errs()<<"---end of Function---\n";
	// }


	return false;
}//end of runOnModule


Loop* ParamGet::captureInstructionForLoop( Function &F, LLVMContext &TheContext, ParamGetCommon &PGC)
{
	//targetII and targetLoop is declared in global PGC member
	//Instruction* targetII;
	//Loop* targetLoop;
	bool IIFoundFlag = false;
	//searchII method 
	//-- Intrinsic Instruction is passed to targetII
	IIFoundFlag = PGC.searchII(F);
	//searchII method end
	if(IIFoundFlag)
	{
		Function* ParentF = PGC.targetII->getParent()->getParent();//targetII->getFunction
		// SamePointerReplace* R = &getAnalysis<SamePointerReplace>(*ParentF);
		// R->runOnFunction(*ParentF);
		LoopInfo* LI = &getAnalysis<LoopInfoWrapperPass>(*ParentF).getLoopInfo();

		//searchTargetLoop method start
		//argument
		//LI	
		//targetLoop
		//targetII
		//-- Target Loop is passed to targetLoop
		PGC.searchTargetLoop(LI);

		//searchTargetLoop method end

		//recordTargetRegion method start
		//argument
		//targetII
		//targetLoop
		//-- The record is recorded in CapturedInstVector and CapturedValMap
		//determine the whether the start point is II or Loop
		PGC.recordTargetRegion();
		//debug
		for(auto vecpair : PGC.CapturedInstVector){
			errs()<<"Captured Inst: ";
			vecpair.second->print(errs());
			errs()<<"\n";
		}

		//recordTargetRegion method end

		//recordNewBB start
		//argument
		// targetII
		// targetLoop
		//--create new BBs for new func and record them with theri original name
		PGC.recordNewBB(TheContext);

		//recordNewBB end

		//create for the other exit block
		SmallVector<BasicBlock*, 8> ExitBlocks;
		PGC.targetLoop->getExitBlocks(ExitBlocks);
		vector<BasicBlock*> ExitBBVector;

		string exitBBName;
		for(BasicBlock *ExitBlock : ExitBlocks){
			//---BB recording---//
			//bb name set
			bool alredyRecordedFlag = false;
			if(ExitBlock->hasName()){
				exitBBName = ExitBlock->getName().str();
				for(auto bbpair : PGC.CreatedBB){
					if( bbpair.first == exitBBName ){
						alredyRecordedFlag = true;
						break;
					}
				}//end of bbpair
				if(alredyRecordedFlag){
					continue;//check next ExitBlock
				}
			}else{
				ExitBlock->setName("BB" + PGC.IntToName(PGC.bnum++));	
				exitBBName = ExitBlock->getName().str();
			}
			//create and push_back new BB
			BasicBlock* tempBB = BasicBlock::Create(TheContext);
			tempBB->setName(exitBBName);//we set name to BB, because doing this is useful.
			PGC.CreatedBB.push_back(make_pair(exitBBName, tempBB));
			errs()<<"Exiting block: ";
			tempBB->print(errs());
			errs()<<"\n";
			//-------------------//
		}
	}
	return PGC.targetLoop;
	//---search result output-------------------//
}
bool ParamGet::ParamGetCommon::searchII( Function &F )
{
	bool IIFoundFlag = false;
	for(auto bbitr = F.begin();bbitr != F.end(); ++bbitr)
	{
		if(bbitr != F.end())
		{
			BasicBlock &B = *bbitr;
			for(auto institr = B.begin();institr != B.end(); ++institr)
			{
				Instruction &I = *institr;
				//(debug)
				// errs()<<"I: ";
				// I.print(errs()); errs()<<"\n";

				if(IntrinsicInst* II = dyn_cast<IntrinsicInst>(&I))
				{
					switch (II->getIntrinsicID())
					{
					case Intrinsic::directive:
						targetII = &*institr;
						IIFoundFlag = true;
						break;
					default:
						break;
					}
				}
				if(IIFoundFlag)
				{
					break;
				}
			}
		}
		if(IIFoundFlag)
		{
			//errs()<<"II Found \n";
			break;
		}
	}
	return IIFoundFlag;
}
void ParamGet::ParamGetCommon::searchTargetLoop( LoopInfo* LI )
{
	BasicBlock* ParentBB = targetII->getParent();
	if(( targetLoop = LI->getLoopFor(ParentBB) ))
	{
		//break;
	}else
	{
		BasicBlock* nextBB = ParentBB->getNextNode();
		while( (nextBB) && !(LI->getLoopFor(nextBB)) )
		{
			nextBB = nextBB->getNextNode();
		}
		if(( targetLoop = LI->getLoopFor(nextBB) ))
		{
			//break;
		}else
		{
			errs()<<"### There is no loop ###\n";
		}
	}
}
void ParamGet::ParamGetCommon::recordTargetRegion()
{
	//record insts before the loop
	if( !(targetLoop->contains(targetII)) )
	{
		Function* ParentF = targetII->getParent()->getParent();
		bool IIFoundFlag = false;
		//record from targetII
		judgeArgument(targetII);
		for(auto bbitr = ParentF->begin();bbitr != ParentF->end(); ++bbitr)
		{
			if(bbitr != ParentF->end())
			{
				BasicBlock &B = *bbitr;
				//if reach the loop, break this FOR
				if( targetLoop->contains(&B) )
				{
					break;
				}
				//record inst
				for(auto institr = B.begin();institr != B.end(); ++institr)
				{
					Instruction &I = *institr;

					//(debug)
					// errs()<<"I: ";
					// I.print(errs()); errs()<<"\n";

					if(IIFoundFlag)
					{
						//judge and record
						judgeArgument(&I);

					}else if(IntrinsicInst* II = dyn_cast<IntrinsicInst>(&I))
					{
						switch (II->getIntrinsicID())
						{
						case Intrinsic::directive:
							IIFoundFlag = true;
							break;
						default:
							break;
						}
					}
				}//end of institr
			}
		}//end of bbitr
	}
	//record loop
	for(auto lbbitr = targetLoop->block_begin();
		lbbitr != targetLoop->block_end();++lbbitr)
	{
		BasicBlock* LoopB = *lbbitr;
		for(auto institr = LoopB->begin(); institr != LoopB->end(); ++institr)
		{
			Instruction* I = &*institr;
			judgeArgument(I);
		}
	}	
}
void ParamGet::ParamGetCommon::recordNewBB(LLVMContext &TheContext)
{
	BasicBlock* ParentBB = targetII->getParent();
	while( !(targetLoop->contains(ParentBB)) )
	{
		//record bb until reach the targetLoop
		//get bb name
		if(!ParentBB->hasName()){
			//if no name, this bb is not captured, so create new name for this
			ParentBB->setName("BB" + IntToName(bnum++));
		}
		string originalLoopStartBBName = ParentBB->getName().str();
		//create and push_back new BB
		// errs()<<"CreatedBB push_back:\t"<<str<<"\n";
		BasicBlock* tempBB = BasicBlock::Create(TheContext);
		tempBB->setName(originalLoopStartBBName);//we set name to BB, because doing this is useful.
		CreatedBB.push_back(make_pair(originalLoopStartBBName, tempBB));
		ParentBB = ParentBB->getNextNode();
	}
	for(auto lbbitr = targetLoop->block_begin();
		lbbitr != targetLoop->block_end();++lbbitr)
	{
		BasicBlock* LoopB = *lbbitr;
		//---BB recording---//
		//bb name set
		if(!LoopB->hasName()){
			LoopB->setName("BB" + IntToName(bnum++));
		}
		string BlockName = LoopB->getName().str();

		//create and push_back new BB
		errs()<<"CreatedBB push_back:\t"<<BlockName<<"\n";
		BasicBlock* tempBB = BasicBlock::Create(TheContext);
		tempBB->setName(BlockName);//we set name to BB, because doing this is useful.
		CreatedBB.push_back(make_pair(BlockName, tempBB));
		//-------------------------------------//
	}
}

void ParamGet::ParamGetCommon::insertDeclOfKMPC(Module &M, IRBuilder<> &Builder, StructType* IdentT)
{
	//OpenMP Runtime Library Function Declare


	insert_kmpc_fork_call_decl(M,Builder,IdentT);
	errs()<<"CondOperandIntegerSize in decl: "<<CondOperandIntegerSize<<"\n";
	switch(myScheduleType)
	{
		case ParamGetCommon::sched_type::kmp_sch_static:
		case ParamGetCommon::sched_type::kmp_sch_static_chunked:
			insert_kmpc_global_thread_num_decl(M,Builder,IdentT);
			if( CondOperandIntegerSize < 64 )
			{
				errs()<<"decl kmpc 4\n";
				insert_kmpc_for_static_init_4_decl(M,Builder,IdentT);
			}else
			{
				errs()<<"decl kmpc 8\n";
				insert_kmpc_for_static_init_8_decl(M,Builder,IdentT);
			}
			insert_kmpc_for_static_fini_decl(M,Builder,IdentT);
			break;
		case ParamGetCommon::sched_type::kmp_sch_dynamic_chunked:
		case ParamGetCommon::sched_type::kmp_sch_guided_chunked:
		case ParamGetCommon::sched_type::kmp_sch_auto:
		case ParamGetCommon::sched_type::kmp_sch_runtime:
			if(true)
			{
				if( CondOperandIntegerSize < 64 )
				{
					insert_kmpc_dispatch_init_4_decl(M,Builder,IdentT);
					insert_kmpc_dispatch_next_4_decl(M,Builder,IdentT);
				}else
				{
					insert_kmpc_dispatch_init_8_decl(M,Builder,IdentT);
					insert_kmpc_dispatch_next_8_decl(M,Builder,IdentT);
				}					
			}
			break;
		default:
			errs()<<"this schedule type is what we can't handle for now\n";
	}
	if( !ReductionValMap.empty() )
	{
		insert_kmpc_reduce_nowait_decl(M,Builder,IdentT);
		insert_kmpc_end_reduce_nowait_decl(M,Builder,IdentT);
		// errs()<<"reduce_nowait is emitted\n";
	}

	//debug
	// errs()<<"-- OMPParamMap after declaration --\n";
	// for(auto mappair : OMPParamMap){
	// 	errs()<< mappair.first <<"\n";
	// }
	// errs()<<"----------\n";
}

void ParamGet::ParamGetCommon::insert_kmpc_global_thread_num_decl(Module &M, IRBuilder<> &Builder, StructType* IdentT)
{
	/*
	kmp int32 kmpc global thread num ( ident_t ∗ loc )

	[Parameters]
	loc 	: Source location information
	[Returns]
	The global thread index of the active thread.

	[Descriptiron]
	This function can be called in any context.
	If the runtime has ony been entered at the outermost level 
	from a single (necessarily non-OpenMP ∗ ) thread,
	then the thread number is that which would be 
	returned by omp_get_thread_num() in the outermost active parallel
	construct. (Or zero if there is no active parallel construct,
	since the master thread is necessarily thread zero).
	If multiple non-OpenMP threads all enter an OpenMP construct 
	then this will be a unique thread identifier among 
	all the threads created by the OpenMP runtime (but the value 
	cannote be defined in terms of OpenMP thread ids returned by 
	omp_get_thread_num()).
	Definition at line 98 of file Kmpc_csupport.c.
	*/
	vector<Type*> Args;
	Args.push_back(IdentT->getPointerTo());
	ArrayRef<Type*> ArgsRef(Args);
	FunctionType* kmpc_thrad_numType =
		FunctionType::get(Builder.getInt32Ty(), ArgsRef, false);
	Constant* gotF = M.getOrInsertFunction("__kmpc_global_thread_num", kmpc_thrad_numType);
	OMPParamMap.insert(make_pair("__kmpc_global_thread_num", dyn_cast<Value>(gotF)));
}
void ParamGet::ParamGetCommon::insert_kmpc_for_static_init_4_decl(Module &M, IRBuilder<> &Builder, StructType* IdentT)
{
	/*
	void kmpc for static init 4 ( ident_t ∗ loc, kmp int32 gtid, 
				kmp int32 schedtype, kmp int32 ∗ plastiter, 
				kmp int32 ∗ plower, kmp int32 ∗ pupper, 
				kmp int32 ∗ pstride, kmp int32 incr, kmp int32 chunk )

	[Parameters]
	loc 		|Source code location
	gtid 		|Global thread id of this thread
	schedtype	|Scheduling type
	plastiter	|Pointer to the "last iteration" flag
	plower		|Pointer to the lower bound
	pupper		|Pointer to the upper bound
	pstride		|Pointer to the stride
	incr 		|Loop increment
	chunk 		|The chunk size

	[Description]
	Each of the four functions here are identical apart 
	from the argument types.
	The functions compute the upper and lower bounds and 
	stride to be used for the set of iterations to be executed
	by the current thread from the statically scheduled 
	loop that is described by the initial values of the bounds, stride,
	increment and chunk size.
	Definition at line 739 of file kmp_sched.cpp.

	*/
	vector<Type*> Args;
	Args.push_back(IdentT->getPointerTo()); // loc
	Args.push_back(Builder.getInt32Ty()); // gtid
	Args.push_back(Builder.getInt32Ty()); // schedtype
	
	Args.push_back(Builder.getInt32Ty()->getPointerTo());//plastiter

	Args.push_back(Builder.getInt32Ty()->getPointerTo());//plower
	Args.push_back(Builder.getInt32Ty()->getPointerTo());//pupper
	Args.push_back(Builder.getInt32Ty()->getPointerTo());//pstride
	Args.push_back(Builder.getInt32Ty());// incr
	Args.push_back(Builder.getInt32Ty());// chunk

	ArrayRef<Type*> ArgsRef(Args);
	FunctionType* kmpc_initType =
		FunctionType::get(Builder.getVoidTy(), ArgsRef, false);
	Constant* gotF = M.getOrInsertFunction("__kmpc_for_static_init_4", kmpc_initType);

	//debug
	// errs()<<"__kmpc_for_static_init_4: \n";
	// gotF->print(errs()); errs()<<"\n";

	OMPParamMap.insert(make_pair("__kmpc_for_static_init_4", dyn_cast<Value>(gotF) ));
	/*
	May be we should add this function's ref into some map.
	(for use as operand)
	*/
}
void ParamGet::ParamGetCommon::insert_kmpc_for_static_init_8_decl(Module &M, IRBuilder<> &Builder, StructType* IdentT)
{	
	vector<Type*> Args;
	Args.push_back(IdentT->getPointerTo()); // loc
	Args.push_back(Builder.getInt32Ty()); // gtid
	Args.push_back(Builder.getInt32Ty()); // schedtype
	
	Args.push_back(Builder.getInt32Ty()->getPointerTo());//plastiter

	Args.push_back(Builder.getInt64Ty()->getPointerTo());//plower
	Args.push_back(Builder.getInt64Ty()->getPointerTo());//pupper
	Args.push_back(Builder.getInt64Ty()->getPointerTo());//pstride
	Args.push_back(Builder.getInt64Ty());// incr
	Args.push_back(Builder.getInt64Ty());// chunk
	ArrayRef<Type*> ArgsRef(Args);
	FunctionType* kmpc_initType =
		FunctionType::get(Builder.getVoidTy(), ArgsRef, false);
	Constant* gotF = M.getOrInsertFunction("__kmpc_for_static_init_8", kmpc_initType);
	OMPParamMap.insert(make_pair("__kmpc_for_static_init_8", dyn_cast<Value>(gotF) ));

}
void ParamGet::ParamGetCommon::insert_kmpc_dispatch_init_4_decl(Module &M, IRBuilder<> &Builder, StructType* IdentT)
{	
	vector<Type*> Args;
	Args.push_back(IdentT->getPointerTo()); // loc
	Args.push_back(Builder.getInt32Ty()); // gtid
	Args.push_back(Builder.getInt32Ty()); // schedtype

	Args.push_back(Builder.getInt32Ty());//plower
	Args.push_back(Builder.getInt32Ty());//pupper
	Args.push_back(Builder.getInt32Ty());// Step (or increment if you prefer)
	Args.push_back(Builder.getInt32Ty());// chunk
	ArrayRef<Type*> ArgsRef(Args);
	FunctionType* kmpc_initType =
		FunctionType::get(Builder.getVoidTy(), ArgsRef, false);
	Constant* gotF = M.getOrInsertFunction("__kmpc_dispatch_init_4", kmpc_initType);
	OMPParamMap.insert(make_pair("__kmpc_dispatch_init_4", dyn_cast<Value>(gotF) ));

}
void ParamGet::ParamGetCommon::insert_kmpc_dispatch_init_8_decl(Module &M, IRBuilder<> &Builder, StructType* IdentT)
{	
	vector<Type*> Args;
	Args.push_back(IdentT->getPointerTo()); // loc
	Args.push_back(Builder.getInt32Ty()); // gtid
	Args.push_back(Builder.getInt32Ty()); // schedtype

	Args.push_back(Builder.getInt64Ty());//plower
	Args.push_back(Builder.getInt64Ty());//pupper
	Args.push_back(Builder.getInt64Ty());// Step (or increment if you prefer)
	Args.push_back(Builder.getInt64Ty());// chunk
	ArrayRef<Type*> ArgsRef(Args);
	FunctionType* kmpc_initType =
		FunctionType::get(Builder.getVoidTy(), ArgsRef, false);
	Constant* gotF = M.getOrInsertFunction("__kmpc_dispatch_init_8", kmpc_initType);
	OMPParamMap.insert(make_pair("__kmpc_dispatch_init_8", dyn_cast<Value>(gotF) ));

}
void ParamGet::ParamGetCommon::insert_kmpc_dispatch_next_4_decl(Module &M, IRBuilder<> &Builder, StructType* IdentT)
{	
	vector<Type*> Args;
	Args.push_back(IdentT->getPointerTo()); // loc
	Args.push_back(Builder.getInt32Ty()); // gtid
	
	Args.push_back(Builder.getInt32Ty()->getPointerTo());//plastiter

	Args.push_back(Builder.getInt32Ty()->getPointerTo());//plower
	Args.push_back(Builder.getInt32Ty()->getPointerTo());//pupper
	Args.push_back(Builder.getInt32Ty()->getPointerTo());//pstride
	ArrayRef<Type*> ArgsRef(Args);
	FunctionType* kmpc_initType =
		FunctionType::get(Builder.getInt32Ty(), ArgsRef, false);
	Constant* gotF = M.getOrInsertFunction("__kmpc_dispatch_next_4", kmpc_initType);
	OMPParamMap.insert(make_pair("__kmpc_dispatch_next_4", dyn_cast<Value>(gotF) ));

}
void ParamGet::ParamGetCommon::insert_kmpc_dispatch_next_8_decl(Module &M, IRBuilder<> &Builder, StructType* IdentT)
{	
	vector<Type*> Args;
	Args.push_back(IdentT->getPointerTo()); // loc
	Args.push_back(Builder.getInt32Ty()); // gtid
	
	Args.push_back(Builder.getInt32Ty()->getPointerTo());//plastiter

	Args.push_back(Builder.getInt64Ty()->getPointerTo());//plower
	Args.push_back(Builder.getInt64Ty()->getPointerTo());//pupper
	Args.push_back(Builder.getInt64Ty()->getPointerTo());//pstride
	ArrayRef<Type*> ArgsRef(Args);
	FunctionType* kmpc_initType =
		FunctionType::get(Builder.getInt32Ty(), ArgsRef, false);
	Constant* gotF = M.getOrInsertFunction("__kmpc_dispatch_next_8", kmpc_initType);
	OMPParamMap.insert(make_pair("__kmpc_dispatch_next_8", dyn_cast<Value>(gotF) ));

}
void ParamGet::ParamGetCommon::insert_kmpc_for_static_fini_decl(Module &M, IRBuilder<> &Builder, StructType* IdentT)
{
	/*
	void kmpc for static fini ( ident_t ∗ loc, kmp int32 global tid )

	[Parameters]
	loc 			|Source location
	global_tid 		|Global thread id

	[Description]
	Mark the end of a statically scheduled loop.
	Definition at line 1483 of file kmp_csupport.c.
	*/
	vector<Type*> Args;
	Args.push_back(IdentT->getPointerTo());
	Args.push_back(Builder.getInt32Ty());
	ArrayRef<llvm::Type*> ArgsRef(Args);
	FunctionType* kmpc_finiType =
		llvm::FunctionType::get(Builder.getVoidTy(), ArgsRef, false);
	Constant* gotF = M.getOrInsertFunction("__kmpc_for_static_fini", kmpc_finiType);
	OMPParamMap.insert(make_pair("__kmpc_for_static_fini", dyn_cast<Value>(gotF) ));
}
void ParamGet::ParamGetCommon::insert_kmpc_fork_call_decl(Module &M, IRBuilder<> &Builder, StructType* IdentT)
{
	/*
	void kmpc fork call ( ident_t ∗ loc, kmp int32 argc,
							 kmpc_micro microtask, ... )

	[Parameters]
	loc 		|Source location information
	argc 		|total number of arguments in the ellipsis
	microtask	|pointer to callback routine consisting of 
				|outlined parallel construct
	...			|pointers to shared variables that aren’t global
	*/
	Type* MicroParams[] =
		{ 
			//ref:kmp.h
			//PointerType::getUnqual creates object's pointer
			PointerType::getUnqual(Builder.getInt32Ty()), //*global_tid
			PointerType::getUnqual(Builder.getInt32Ty()) //*bound_tid
		};
	FunctionType* Kmpc_MicroTy =
		llvm::FunctionType::get(Builder.getVoidTy(), MicroParams, true);
	Type* TypeParams[] = 
	{	
		IdentT->getPointerTo(),
		Builder.getInt32Ty(),
		Kmpc_MicroTy->getPointerTo() 
	};
	FunctionType* kmpc_forkTy =
		FunctionType::get(Builder.getVoidTy(), TypeParams, true);
	Constant* gotF = M.getOrInsertFunction("__kmpc_fork_call", kmpc_forkTy);
	OMPParamMap.insert(make_pair("__kmpc_fork_call", dyn_cast<Value>(gotF) ));
}
void ParamGet::ParamGetCommon::insert_kmpc_reduce_nowait_decl(Module &M, IRBuilder<> &Builder, StructType* IdentT)
{	
	vector<Type*> Args;
	Args.push_back(IdentT->getPointerTo()); // loc
	Args.push_back(Builder.getInt32Ty()); // gtid
	
	Args.push_back(Builder.getInt32Ty());//num_vars | number of items (variables) to be reduced

	Args.push_back(Builder.getInt64Ty());//reduce_size | size of data in bytes to be reduced 
										//mostly reduce_size = num_var * 8
	Args.push_back(Builder.getInt8Ty()->getPointerTo());//reduce_data | pointer to data to be reduced

	Type* lhs_data_And_rhs_dataTy[2] =
		{
			PointerType::getUnqual(Builder.getInt8Ty()), //lhs_data pointer
			PointerType::getUnqual(Builder.getInt8Ty()) //rhs_data pointer
		};
	FunctionType* reduce_funcTy =
		llvm::FunctionType::get(Builder.getVoidTy(), lhs_data_And_rhs_dataTy, /*bool isVarArg*/false);
	Args.push_back( reduce_funcTy->getPointerTo() );//reduce_func

	Args.push_back( //lck
		ArrayType::get( Builder.getInt32Ty(), 8 )->getPointerTo()
		//see kmp.h//typedef kmp_int32 kmp_critical_name[8];
	);

	ArrayRef<Type*> ArgsRef(Args);

	//(debug)
	// for(auto tyitr = ArgsRef.begin(); tyitr != ArgsRef.end(); ++tyitr)
	// {
	// 	errs()<<"\treduce_nowait Type:\t";
	// 	(*tyitr)->print(errs()); errs()<<"\n";
	// }

	FunctionType* kmpc_initType =
		FunctionType::get(Builder.getInt32Ty(), ArgsRef, false);
	Constant* gotF = M.getOrInsertFunction("__kmpc_reduce_nowait", kmpc_initType);
	OMPParamMap.insert(make_pair("__kmpc_reduce_nowait", dyn_cast<Value>(gotF) ));
}


void ParamGet::ParamGetCommon::insert_kmpc_end_reduce_nowait_decl(Module &M, IRBuilder<> &Builder, StructType* IdentT)
{
		vector<Type*> Args;
	Args.push_back(IdentT->getPointerTo()); // loc
	Args.push_back(Builder.getInt32Ty()); // gtid
	Args.push_back(	//lck
		ArrayType::get( Builder.getInt32Ty(), 8 )->getPointerTo()
		//see kmp.h//typedef kmp_int32 kmp_critical_name[8];
	);

	ArrayRef<Type*> ArgsRef(Args);

	//(debug)
	// for(auto tyitr = ArgsRef.begin(); tyitr != ArgsRef.end(); ++tyitr)
	// {
	// 	errs()<<"\tend_reduce_nowait Type:\t";
	// 	(*tyitr)->print(errs()); errs()<<"\n";
	// }

	FunctionType* kmpc_initType =
		FunctionType::get(Builder.getVoidTy(), ArgsRef, false);
	Constant* gotF = M.getOrInsertFunction("__kmpc_end_reduce_nowait", kmpc_initType);
	OMPParamMap.insert(make_pair("__kmpc_end_reduce_nowait", dyn_cast<Value>(gotF) ));
}



string ParamGet::ParamGetCommon::IntToName(unsigned int valnum)
{
	string str;
	unsigned int i = valnum;
	do{//args-count starts from 0.
		char c = '0' + i%10;
		str=c+str;
		i/=10;
	}while(i>0);
	return str;
}

void ParamGet::ParamGetCommon::insertCapArgument(Instruction* I, unsigned int &instnum)
{
	string capturingName;
	//I->print(errs());errs()<<"\n";
	
	//specially handle the our parallelization directive
	if(IntrinsicInst* II = dyn_cast<IntrinsicInst>(I)){
		if( II->getIntrinsicID() == Intrinsic::directive ){
			capturingName = targetIIName;//this is not used for now 
			CapturedInstVector.push_back(make_pair(capturingName,I));
			CapturedValMap.insert(make_pair(capturingName,dyn_cast<Value>(I)));
			return;
		}
	}

	//name get
	if(!I->hasName()){
		capturingName = IntToName(instnum);
		if( !I->getType()->isVoidTy() ){
			I->setName(capturingName);			
			capturingName = I->getName().str();
		}
		// else{
		// 	//errs()<<"This inst is VoidTy. We can't set name to this inst.\n";
		// 	//we can't set Name to VoidTy Instruction
		// }
	}else{
		capturingName = I->getName().str();
		//find and set
		if(CapturedValMap.find(capturingName) != CapturedValMap.end()){
			return;
		}
	}
	CapturedInstVector.push_back(make_pair(capturingName,I));
	CapturedValMap.insert(make_pair(capturingName,dyn_cast<Value>(I)));
}


FunctionType* ParamGet::ParamGetCommon::setOMP_outlinedFuncType(IRBuilder<> &Builder)
{
	vector<llvm::Type*> FArgs;
	FArgs.push_back(Builder.getInt32Ty()->getPointerTo());//Outlined func's first arg is not ident_t
	FArgs.push_back(Builder.getInt32Ty()->getPointerTo());
	//parameter is decided in this for loop 
	for(auto valitr = FuncArgumentMap.begin(); valitr != FuncArgumentMap.end(); ++valitr)
	{
		Value* argV = valitr->second;
		FArgs.push_back(argV->getType());

		//---print (debug)---
		// errs() << "Arg type:";
		// argV->getType()->print(errs());
		// errs() << "\t name:" << argV->getName() << "\t Value:";
		// argV->print(errs());
		// errs() << "\n";
		//------
	}
	//---Making a function---//
	ArrayRef<Type*> FArgsRef(FArgs);
	FunctionType* OutFuncType = FunctionType::get(Builder.getVoidTy(), FArgsRef, false);
	return OutFuncType;
}

Value* ParamGet::ParamGetCommon::buildInstruction(IRBuilder<> &Builder, Instruction* I)
{
	//Use this func when we want to built "CapturedInstVector" as func's inst.
	Value* buildval;
	//string str,userstr;
	errs()<<"Build Inst:\t";
	I->print(errs());errs()<<"\n";

	auto valitr = BuiltValMap.begin();
	auto bbitr = CreatedBB.begin();


	switch(I->getOpcode()){
		//Terminator Instructions
		case Instruction::Ret:
			if(dyn_cast<ReturnInst>(I))
			{
				//parallel execution ONLY returns void
				buildval = dyn_cast<Value>(
					Builder.CreateRetVoid()
					);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::Br:
			if(BranchInst* BI = dyn_cast<BranchInst>(I))
			{
				if(BI->isConditional())
				{
					BasicBlock* successors[2];
					Value* oldV = BI->getCondition();
					Value* V;
					//find value
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else
					{
						errs()<<"[ParamGet] buildInstruction: Map not found\n";
						oldV->dump();
					}

					// find two blocks
					for(int i=0;i<2;i++)
					{
						string str = BI->getSuccessor(i)->getName();//block name
						//(debug)
						errs()<<"\tCondBr BB name:\t"<<str<<"\n";
						//related bb search-------
						for(bbitr = CreatedBB.begin(); bbitr != CreatedBB.end(); ++bbitr)
						{
							if( str == bbitr->first )
							{  
								//(debug)
								// errs()<<"Exist\n";
								break;
							}
						}
						successors[i] = bbitr->second;	
						//errs()<<"BB:"<<successors[i]->getName()<<"\n";
						//----------------------------
					}
					buildval = dyn_cast<Value>(
							Builder.CreateCondBr(
									/*Value* Cond*/ V,
									/*BasicBlock* True*/ successors[0],
									/*BasicBlock* False*/ successors[1]
								)
						);
				}else
				{// one block branch
					//Instruction build
					string str = I->getOperand(0)->getName().str();
					for(bbitr = CreatedBB.begin(); bbitr != CreatedBB.end(); ++bbitr)
					{
						if( str == bbitr->first )
						{
							break;
						}
					}
					buildval = dyn_cast<Value>(
						Builder.CreateBr(
								/* Dest BasicBlock* */bbitr->second
							)
						);
				}
				//no map making
			}
			break;
		case Instruction::Switch://ok in beforeBS.ll
			//container inst is not generated
			if(SwitchInst* SI = dyn_cast<SwitchInst>(I))
			{
				Value* oldV = dyn_cast<Value>(SI->getCondition());
				Value* V;
				if( oldV->hasName() &&
					BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
				{
					V = BuiltValMap.find(oldV->getName().str())->second;
				}else
				{
					errs()<<"[ParamGet] buildInstruction: Map not found\n";
					oldV->dump();
				}

				//find default destination
				string str = SI->getDefaultDest()->getName().str();
				for(bbitr = CreatedBB.begin(); bbitr != CreatedBB.end(); ++bbitr)
				{
					if( str == bbitr->first )
					{
						break;
					}
				}

				SwitchInst* newSI = Builder.CreateSwitch(
								/* Value *V */V,
								/* BasicBlock *Dest */bbitr->second,
								/* unsigned NumCases=10 */SI->getNumCases()
							);

				//add other destinations
				for(auto caseitr = SI->case_begin(); caseitr != SI->case_end(); ++caseitr)
				{
					ConstantInt* OnVal = caseitr->getCaseValue();
					str = caseitr->getCaseSuccessor()->getName().str();
					for(bbitr = CreatedBB.begin(); bbitr != CreatedBB.end(); ++bbitr)
					{
						if( str == bbitr->first )
						{
							newSI->addCase(OnVal,bbitr->second);
							break;
						}
					}
				}
				buildval = dyn_cast<Value>(newSI);//this is only for print
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::IndirectBr:
			errs()<<"IndirectBr is not OK\n";	//not debugged
			if(IndirectBrInst* IBI = dyn_cast<IndirectBrInst>(I))
			{
				Value* oldV = dyn_cast<Value>(IBI->getAddress());
				Value* V;
				if( oldV->hasName() &&
					BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
				{
					V = BuiltValMap.find(oldV->getName().str())->second;
				}else
				{
					errs()<<"[ParamGet] buildInstruction: Map not found\n";
					oldV->dump();
				}
				//we assume that address calculation is done in IR code

				IndirectBrInst* newIBI = Builder.CreateIndirectBr(
								/* Value *Addr*/V
							);
				unsigned int destnum = IBI->getNumDestinations();
				for(unsigned int i = 0; i < destnum; i++)
				{
					newIBI->addDestination(IBI->getDestination(i));
					//this is list of possible bb label
				}
				buildval = dyn_cast<Value>(newIBI);//this is only for print
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::Invoke://ok in beforeBS.ll
			if(InvokeInst* II = dyn_cast<InvokeInst>(I))
			{
				Value* Callee = II->getCalledValue();
				if( Callee->hasName() &&
					BuiltValMap.find(Callee->getName().str()) != BuiltValMap.end() )
				{
					Callee = BuiltValMap.find(Callee->getName().str())->second;
				}else
				{
					errs()<<"New callee is same as the original callee\n";
				}

				const unsigned int numsuc = II->getNumSuccessors();
				const unsigned int argnum = II->getNumArgOperands();
				Value* Args[argnum];
				BasicBlock* operands[numsuc];

				//parameter research
				for(unsigned int i=0;i<argnum;i++)
				{
					//argument's related value get
					Value* oldV = I->getOperand(i);

					if( Constant* con = dyn_cast<Constant>(oldV) )
					{
						Args[i] = dyn_cast<Value>(con);
						continue;
					}else if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						Args[i] = BuiltValMap.find(oldV->getName().str())->second;
					}else
					{
						errs()<<"[ParamGet] buildInstruction: Map not found\n";
						oldV->dump();
					}
				}
			 	ArrayRef<Value*> ArgsRef(Args,argnum);

			 	//bb search
				for(unsigned int i=0; i < numsuc; i++)
				{
					string str = II->getSuccessor(i)->getName().str();
					for(bbitr = CreatedBB.begin(); bbitr != CreatedBB.end(); ++bbitr)
					{
						if( str == bbitr->first )
						{
							break;
						}
					}
					if(bbitr != CreatedBB.end())
					{
						operands[i] = bbitr->second;
					}
				}
				buildval = dyn_cast<Value>(
						Builder.CreateInvoke(
								/* Value *Callee */Callee,//this is called function
								/* BasicBlock *NormalDest */operands[0],
								/* BasicBlock *UnwindDest */operands[1],
								/* ArrayRef<Value*> Args */ArgsRef
							)
					);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::Resume:
			errs()<<"Resume is not debugged\n";		//not debugged
			//The ‘resume‘ instruction is a terminator instruction
			// that has no successors.
			if(ResumeInst* RI = dyn_cast<ResumeInst>(I))
			{
				Value* oldV = dyn_cast<Value>(RI->getValue());
				//FIXME: this value may be constant value
				//if constant, we cant find from BuiltValMap
				//so please make constant value like Builder.getInt32(1)
				Value* V;
				if( oldV->hasName() &&
					BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
				{
					V = BuiltValMap.find(oldV->getName().str())->second;
				}else if( dyn_cast<Constant>(oldV) )
				{
					V = oldV;
					errs()<<"New V is same as the original V\n";
				}else
				{
					errs()<<"[ParamGet] buildInstruction: Map not found and oldV is not constant\n";
					oldV->dump();
				}

				buildval = dyn_cast<Value>(
						Builder.CreateResume(
								/* Value *Exn */V
							)
					);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::Unreachable:
			errs()<<"Unreachable is not debugged\n";	//not debugged
			if(dyn_cast<UnreachableInst>(I))
			{
				buildval = dyn_cast<Value>(
						Builder.CreateUnreachable()
					);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::CleanupRet:
		//The ‘cleanupret‘ instruction is a terminator instruction
		// that has an optional successor.
			errs()<<"CleanupRet is not debugged\n";
			errs()<<"############### CleanupRet may cause critical error ##############\n";
			if(CleanupReturnInst* CURI = dyn_cast<CleanupReturnInst>(I))
			{
				Value* oldV = dyn_cast<Value>(CURI->getCleanupPad());
				Value* V;
				if( oldV->hasName() &&
					BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
				{
					V = BuiltValMap.find(oldV->getName().str())->second;
				}else if( dyn_cast<Constant>(oldV) )
				{
					V = oldV;
					errs()<<"New V is same as the original V\n";
				}

				CleanupPadInst* CleanupPad = dyn_cast<CleanupPadInst>(V);
				CleanupReturnInst* newCURI = Builder.CreateCleanupRet(
							/* CleanupPadInst *CleanupPad */CleanupPad
					);
				if( CURI->hasUnwindDest() )
				{
					newCURI->setUnwindDest(CURI->getUnwindDest());
				}
				buildval = dyn_cast<Value>(newCURI);//this is only for print
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::CatchRet:	
		//The ‘catchret‘ instruction is a terminator instruction 
		//that has a single successor.
			errs()<<"CatchRet is not debugged\n";
			if(CatchReturnInst* CRI = dyn_cast<CatchReturnInst>(I))
			{
				Value* oldV = dyn_cast<Value>(CRI->getCatchPad());
				Value* V;
				if( oldV->hasName() &&
					BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
				{
					V = BuiltValMap.find(oldV->getName().str())->second;
				}else if( dyn_cast<Constant>(oldV) )
				{
					V = oldV;
					errs()<<"New V is same as the original V\n";
				}
				CatchPadInst* Catchpad = dyn_cast<CatchPadInst>(V);

				//BB search
				string str = CRI->getSuccessor()->getName().str();
				for(bbitr = CreatedBB.begin(); bbitr != CreatedBB.end(); ++bbitr)
				{
					if( str == bbitr->first )
					{
						break;
					}
				}
				buildval = dyn_cast<Value>(
						Builder.CreateCatchRet(
							/* CatchPadInst *Catchpad */Catchpad,
							/* BasicBlock *BB */bbitr->second
							)
					);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::CatchSwitch:
			errs()<<"CatchSwitch is not debugged\n";
			if(CatchSwitchInst* CSI = dyn_cast<CatchSwitchInst>(I))
			{
				Value* oldV = dyn_cast<Value>(CSI->getParentPad());
				Value* V;
				if( oldV->hasName() &&
					BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
				{
					V = BuiltValMap.find(oldV->getName().str())->second;
				}else if( dyn_cast<Constant>(oldV) )
				{
					V = oldV;
					errs()<<"New V is same as the original V\n";
				}

				//BB search
				string str = CSI->getUnwindDest()->getName().str();
				for(bbitr = CreatedBB.begin(); bbitr != CreatedBB.end(); ++bbitr)
				{
					if( str == bbitr->first )
					{
						break;
					}
				}
				buildval = dyn_cast<Value>(
						Builder.CreateCatchSwitch(
							/* Value *ParentPad */ V,
							/* BasicBlock *UnwindBB */ bbitr->second,
							/* unsigned NumHandlers */ CSI->getNumHandlers()
							)
					);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;

		//Standard binary operators
		case Instruction::Add://ok in test13/before.ll
		case Instruction::FAdd:
		case Instruction::Sub://ok in test13/before.ll
		case Instruction::FSub:
		case Instruction::Mul://ok in test13/before.ll
		case Instruction::FMul:
		case Instruction::UDiv:
		case Instruction::SDiv://ok in test13/before.ll
		case Instruction::FDiv:
		case Instruction::URem:
		case Instruction::SRem:
		case Instruction::FRem:
		//bitwise binary operators
		case Instruction::Shl:
		case Instruction::LShr:
		case Instruction::AShr:
		case Instruction::And:
		case Instruction::Or:
		case Instruction::Xor:
			//FIXME : we Only handle NSWAdd inst.for now
			if(dyn_cast<BinaryOperator>(I)){
				Value* operands[2];
				//name set
				//uselist search
				for(int i=0;i<2;i++)
				{
					Value* oldV = I->getOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}
					operands[i] = V;
				}
				//(debug)
				// errs()<<"\toperands[0]:\t";
				// operands[0]->print(errs()); errs()<<"\n";
				// errs()<<"\toperands[1]:\t";
				// operands[1]->print(errs()); errs()<<"\n";

				buildval = Builder.CreateBinOp(
					/*Instruction::BinaryOps Opc*/Instruction::BinaryOps( I->getOpcode() ),
					/*Value* LHS*/operands[0],
					/*Value* RHS*/operands[1]
				);
				if( BinaryOperator* newBO = dyn_cast<BinaryOperator>(buildval) )
				{
					switch(I->getOpcode())
					{
						case Instruction::SRem:
						case Instruction::URem:
							errs()<<"###### if divide by zero, it may cause critical error #######\n";
							break;
						case Instruction::UDiv:
							errs()<<"###### if divide by zero, it may cause critical error #######\n";
							break;
						case Instruction::SDiv:		
						case Instruction::LShr:
						case Instruction::AShr:
							newBO->setIsExact( I->isExact() );
							break;
						case Instruction::Add:
						case Instruction::Sub:
						case Instruction::Mul:
						case Instruction::Shl:
							newBO->setHasNoUnsignedWrap( I->hasNoUnsignedWrap() );
							newBO->setHasNoSignedWrap( I->hasNoSignedWrap() );
							break;
						case Instruction::FAdd:
						case Instruction::FSub:
						case Instruction::FMul:
						case Instruction::FDiv:
						case Instruction::FRem:
							if( I->isFast() )
							{
								newBO->copyFastMathFlags( I );
								newBO->setFast( I->isFast() );
								newBO->setHasNoNaNs( I->hasNoNaNs() );
								newBO->setHasNoInfs( I->hasNoInfs() );
								newBO->setHasNoSignedZeros( I->hasNoSignedZeros() );
								newBO->setHasAllowReassoc( I->hasAllowReassoc() );
								newBO->setHasAllowReciprocal( I->hasAllowReciprocal() );
								newBO->setHasApproxFunc( I->hasApproxFunc() );
							}				
							break;
						default:
							break;
					}	
				}	
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;

		// Memory operators...
		case Instruction::Alloca:
			if(AllocaInst* AI = dyn_cast<AllocaInst>(I))
			{
				//no uselist search : alloca don't use the other insts
				//Instruction build
				auto AllocaInst =	Builder.CreateAlloca(
									AI->getAllocatedType()
								);
				AllocaInst->setAlignment(AI->getAlignment());
				buildval = dyn_cast<Value>(AllocaInst);
				//val map make			
				BuiltValMap.insert( make_pair(I->getName().str(),buildval) );
				//UseList(this instruction's user list) add.
			}
			break;
		case Instruction::Load:	
			if(LoadInst* LI = dyn_cast<LoadInst>(I))
			{
				//determine the memory we load
				Value* loadMemory = I->getOperand(0);
				//if the memory is built in each thread or shared memory, we use the built one (instruction for each thread, parameter for shared memory)
				if( loadMemory->hasName() &&
					BuiltValMap.find(loadMemory->getName().str()) != BuiltValMap.end() )
				{
					loadMemory = BuiltValMap.find(loadMemory->getName().str())->second;
				}
				//else if it is built as global, we use the original one
				else if( dyn_cast<Constant>(loadMemory) )
				{
					errs()<<"Load the original global memory\n";
				}

				//build the instruction
				buildval = dyn_cast<Value>(
					Builder.CreateAlignedLoad(
						loadMemory,
						LI->getAlignment()
					)
				);
				//insert the built load instruction as the original one
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::Store:
		//FIXME:Aligned Store will be handled//
			if(StoreInst* SI = dyn_cast<StoreInst>(I))
			{
				// bool StoreIsSkippedFlag = false;
				Value* operands[2];
				//no name
				//uselist search
				for(int i=0;i<2;i++)
				{
					Value* oldV = I->getOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						//(debug)
						errs()<<"Store Operand: "<< oldV->getName().str() << "\n";
						oldV->print(errs());
						errs()<<"\n";
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}
					operands[i] = V;
					//debug
					errs()<<"operand["<<i<<"]: ";
					operands[i]->print(errs()); errs()<<"\n";
				}
				// if(StoreIsSkippedFlag)
				// {
				// 	errs()<<"Is this possible to cast pointer and store?\n";
				// 	errs()<<"We can't handle this Store\n";
				// 	I->dump();
				// }else
				{
					//Instruction build
					buildval = dyn_cast<Value>(
							Builder.CreateAlignedStore(
								operands[0],
								operands[1],
								SI->getAlignment()
							)
						);
				}

			}
			//no map making : store don't have name.
			break;
		case Instruction::GetElementPtr://ok in test13/beforeBS.ll
		//FIXME : this case alse handles "no bounds GEP"
			if(auto GEP = dyn_cast<GetElementPtrInst>(I))
			{
				//(debug)
				// errs()<<"SourceElementType:\t";
				// GEP->getSourceElementType()->print(errs()); errs()<<"\n";
				// errs()<<"ResultElementType:\t";
				// GEP->getResultElementType()->print(errs()); errs()<<"\n";
				// errs()<<"PointerOperandType:\t";
				// GEP->getPointerOperandType()->print(errs()); errs()<<"\n";



				Value* GEPPtr = GEP->getPointerOperand();
				if( GEPPtr->hasName() &&
					BuiltValMap.find(GEPPtr->getName().str()) != BuiltValMap.end())
				{
					//found
					GEPPtr = BuiltValMap.find(GEPPtr->getName().str())->second;
				}else
				{
					if( dyn_cast<Constant>( GEPPtr ))
					{
						//we do nothing

						// //this is for private variable
						// if( GEPPtr->hasName() && 
						// 	PrivateValMap.find( GEPPtr->getName().str() ) != PrivateValMap.end() )
						// {
						// 	GEPPtr = PrivateValMap.find( GEPPtr->getName().str() )->second;
						// }else if( ConstantExpr* CE = dyn_cast<ConstantExpr>(GEPPtr) )
						// {
						errs()<<"Operand is ConstantExpr\n";
						// 	GEPPtr->print(errs()); errs()<<"\n";
						// 	if(CE->isGEPWithNoNotionalOverIndexing())
						// 	{
						// 		if( CE->getOperand(0)->hasName() &&
						// 			BuiltValMap.find( CE->getOperand(0)->getName().str() ) != BuiltValMap.end() )
						// 		{	//if global is not private, this section is not processed.
						// 			//--------------------//
						// 			Value* PriPtr = CE->getOperand(0);
						// 			auto valitr = BuiltValMap.find( PriPtr->getName().str() );
						// 			if(valitr != BuiltValMap.end())
						// 			{
						// 				PriPtr = valitr->second;
						// 			}
						// 			vector<Value*> IdxVector;
						// 			for(unsigned i = 1;i < CE->getNumOperands(); i++)
						// 			{
						// 				Value* V = CE->getOperand(i);
						// 				IdxVector.push_back(V);
						// 			}
						// 			ArrayRef<Value*> IdxList(IdxVector);

						// 			Type* GEPTy;
						// 			if(PointerType* PT = dyn_cast<PointerType>( PriPtr->getType() ))
						// 			{
						// 				GEPTy = PT->getElementType();
						// 			}
						// 			GetElementPtrInst* newGEP = dyn_cast<GetElementPtrInst>(
						// 				Builder.CreateGEP(
						// 						/*Type* Ty*/ GEPTy,
						// 						/*Value* Ptr*/ PriPtr,
						// 						/*ArrayRef<Value*> IdxList*/ IdxList
						// 				)
						// 			);
						// 			newGEP->setIsInBounds( 1 );//this is ConstantExpr so inbounds
						// 			GEPPtr = dyn_cast<Value>(newGEP);
						// 		}
						// 	}
						// }
						//-----------------------------------
						// GEPPtr is OK
					}else
					{
						errs()<<"[ParamGet] we can't find GEPPtr\n";
					}
				}
				//(debug
				errs()<<"GEPPtr:\t";
				GEPPtr->print(errs()); errs()<<"\n";


				vector<Value*> IdxVector;
				for(auto valitr = GEP->idx_begin();valitr != GEP->idx_end(); ++valitr)
				{
					Value* oldV = *valitr;
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}

					//(debug)
					errs()<<"old idx :\t";
					oldV->print(errs()); errs()<<"\n";
					errs()<<"new idx :\t";
					V->print(errs()); errs()<<"\n";

					IdxVector.push_back(V);
				}

				ArrayRef<Value*> IdxList(IdxVector);
				GetElementPtrInst* newGEP = dyn_cast<GetElementPtrInst>(
					Builder.CreateGEP(
							/*Type* Ty*/ GEP->getSourceElementType(),
							/*Value* Ptr*/ GEPPtr,
							/*ArrayRef<Value*> IdxList*/ IdxList
					)
				);
				newGEP->setIsInBounds( GEP->isInBounds() );
				buildval = dyn_cast<Value>(newGEP);

				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::Fence:
			errs()<<"Fence is not debugged\n";
			if(FenceInst *FI = dyn_cast<FenceInst>(I))
			{
				buildval = dyn_cast<Value>(
						Builder.CreateFence(
						/* AtomicOrdering Ordering*/ FI->getOrdering(),
						/* SyncScope::ID SSID=SyncScope::System*/ FI->getSyncScopeID()
							)
					);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;

		case Instruction::AtomicCmpXchg:
			errs()<<"AtomicCmpXchg is not debugged\n";
			if(AtomicCmpXchgInst *ACXI = dyn_cast<AtomicCmpXchgInst>(I))
			{
				Value* operands[3];
				for(int i=0;i<3;i++){
					Value* oldV = I->getOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}
					operands[i] = V;
				}


				buildval = dyn_cast<Value>(
						Builder.CreateAtomicCmpXchg(
						/* Value *Ptr */ operands[0],
						/* Value *Cmp */ operands[1],
						/* Value *New */ operands[2],
						/* AtomicOrdering SuccessOrdering */ ACXI->getSuccessOrdering(),
						/* AtomicOrdering FailureOrdering */ ACXI->getFailureOrdering(),
						/* SyncScope::ID SSID=SyncScope::System*/ ACXI->getSyncScopeID()
							)
					);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::AtomicRMW:
			errs()<<"AtomicRMW is not debugged\n";
			if(AtomicRMWInst *ARMWI = dyn_cast<AtomicRMWInst>(I))
			{
				Value* operands[2];
				for(int i=0;i<2;i++)
				{
					Value* oldV = I->getOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}
					operands[i] = V;
					//debug
					errs()<<"operand["<<i<<"]: ";
					operands[i]->print(errs()); errs()<<"\n";
				}


				buildval = dyn_cast<Value>(
						Builder.CreateAtomicRMW(
						/* AtomicRMWInst::BinOP Op */ ARMWI->getOperation(),
						/* Value *Ptr */ operands[0],
						/* Value *Val */ operands[1],
						/* AtomicOrdering Ordering */ ARMWI->getOrdering(),
						/* SyncScope::ID SSID=SyncScope::System*/ ARMWI->getSyncScopeID()
							)
					);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;

		// Cast operators ...
		case Instruction::Trunc://ok in beforeBS.ll
		case Instruction::ZExt:
		case Instruction::SExt://ok in test13/before.ll
		case Instruction::FPToUI:
		case Instruction::FPToSI:
		case Instruction::UIToFP: 
		case Instruction::SIToFP:
		case Instruction::FPTrunc:
		case Instruction::FPExt:
		case Instruction::PtrToInt:
		case Instruction::IntToPtr:
		case Instruction::BitCast://ok in beforeBS.ll
		case Instruction::AddrSpaceCast:
			if(dyn_cast<CastInst>(I))
			{
				//uselist search
				Value* oldV = I->getOperand(0);
				Value* V;
				if( oldV->hasName() &&
					BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
				{
					V = BuiltValMap.find(oldV->getName().str())->second;
				}else if( dyn_cast<Constant>(oldV) )
				{
					V = oldV;
					errs()<<"New V is same as the original V\n";
				}

				Type* DestTy = I->getType();
				//Instruction build
				buildval = Builder.CreateCast(
					/*Instruction::CastOps Op*/Instruction::CastOps( I->getOpcode() ),
					/*Value *V*/V,
					/*Type* DestTy*/ DestTy
				);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		
		/* FIXME: no Build for this Instruction */
		case Instruction::CleanupPad:
		case Instruction::CatchPad:
			errs()<<"FuncletPad is not debugged\n";
			if(FuncletPadInst* FPI = dyn_cast<FuncletPadInst>(I))
			{
				Value* ParentPad = FPI->getParentPad();
				if( ParentPad->hasName() &&
					BuiltValMap.find(ParentPad->getName().str()) != BuiltValMap.end() )
				{
					ParentPad = BuiltValMap.find(ParentPad->getName().str())->second;
				}else if( dyn_cast<Constant>(ParentPad) )
				{
					errs()<<"New V is same as the original V\n";
				}

				unsigned argnum = FPI->getNumArgOperands();
				Value* Args[argnum];
				for(unsigned i=0;i<argnum;i++)
				{
					Value* oldV = FPI->getArgOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}
					Args[i] = V;
				}
			 	ArrayRef<Value*> ArgsRef(Args,argnum);

				buildval = Builder.CreateCleanupPad(
					/* Value* ParentPad*/ParentPad,
					/* ArrayRef<Value*> Args */ArgsRef
					);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;

		// Other operators...
		case Instruction::ICmp://ok in test13/before.ll
			//FIXME : we assume that Icmp doesn't recieve over 2 args.
			if(auto* ICI = dyn_cast<ICmpInst>(I))
			{
				Value* operands[2];

				//uselist search-------------------
				for(int i=0;i<2;i++)
				{
					Value* oldV = I->getOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}
					operands[i] = V;
				}
				//-----------------------------------------------
				buildval = dyn_cast<Value>(
					Builder.CreateICmp(
							/*CmpInst::Predicate P*/ ICI->getPredicate(),
							/*Value* LHS*/ operands[0],
							/*Value* RHS*/ operands[1]
						)
				);			
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::FCmp:
			errs()<<"FCmp is not debugged\n";
			if(FCmpInst* FCI = dyn_cast<FCmpInst>(I))
			{
				Value* operands[2];

				//uselist search-------------------
				for(int i=0;i<2;i++)
				{
					Value* oldV = I->getOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}
					operands[i] = V;
				}
				//(debug)
				// errs()<<"operands[0]: ";
				// operands[0]->print(errs()); errs()<<"\n";
				// errs()<<"operands[1]: ";
				// operands[1]->print(errs()); errs()<<"\n";

				//-----------------------------------------------
				buildval = dyn_cast<Value>(
					Builder.CreateFCmp(
							/*CmpInst::Predicate P*/ FCI->getPredicate() ,
							/*Value* LHS*/ operands[0],
							/*Value* RHS*/ operands[1]
						)
				);			
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::PHI:
			//errs()<<"PHI is not OK\n"; // ok in testprog/1_before.ll
			if(PHINode* PHIN = dyn_cast<PHINode>(I))
			{
				unsigned int incomings = PHIN->getNumIncomingValues();
				PHINode* newPHIN = Builder.CreatePHI(
							/* Type* Ty */ PHIN->getType() ,
							/* unsigned NumReservedValues */ incomings
						);			
				// for(unsigned int i=0; i<incomings; i++)
				// {
				// 	Value* IncomingV = PHIN->getIncomingValue(i);
				// 	auto valitr = BuiltValMap.find(IncomingV->getName().str());
				// 	string IncomingBBStr = PHIN->getIncomingBlock(i)->getName().str();
				// 	auto bbitr = CreatedBB.begin();
				// 	for(; bbitr != CreatedBB.end(); ++bbitr)
				// 	{
				// 		if(IncomingBBStr == bbitr->first)
				// 		{
				// 			break;
				// 		}
				// 	}
				// 	errs()<<"addIncoming:\t";
				// 	valitr->second->print(errs());errs()<<"\n";
				// 	newPHIN->addIncoming(valitr->second,bbitr->second);
				// }
				buildval = dyn_cast<Value>(newPHIN);
				BuiltValMap.insert( make_pair(I->getName().str(),buildval) );
			}
			break;
		case Instruction::Call://ok in test13/before.ll
			if(CallInst* CI = dyn_cast<CallInst>(I))
			{			
				//parallel directives are handled in buildCapturedInstVector

				Value* Callee = CI->getCalledValue();
				//There can be some Callee whose value is detemined by Instruction in Loop
				//So, we search BuiltValMap for such function
				if( Callee->hasName() &&
					BuiltValMap.find(Callee->getName().str()) != BuiltValMap.end() )
				{
					Callee = BuiltValMap.find(Callee->getName().str())->second;
				}else
				{
					errs()<<"New callee is same as the original callee\n";
				}
				//(debug)
				errs()<<"Callee:\t";
				Callee->print(errs());errs()<<"\n";

				const unsigned int argnum = CI->getNumArgOperands();
				Value* Args[argnum];
				//parameter research
				for(unsigned i=0;i<argnum;i++)
				{
					Value* oldV = CI->getArgOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}else if( dyn_cast<MetadataAsValue>(oldV) )
					{
						V = oldV;
						errs()<<"New V is metadata\n";
					}
					Args[i] = V;
				}
				ArrayRef<Value*> ArgsRef(Args,argnum);

				//create inst
				CallInst* newCI = Builder.CreateCall(
						/*Value Callee*/ Callee,
						/*ArrayRef<Value*> Args=None*/ ArgsRef
						);
				buildval = dyn_cast<Value>(newCI);
				newCI->setTailCall( CI->isTailCall() );
				newCI->setAttributes( CI->getAttributes() );
				//this is the most important thing in creating call.
				newCI->setCallingConv( CI->getCallingConv() );
				if( CI->isNoInline() )
				{
					newCI->setIsNoInline();
				}

				if( CI->doesNotAccessMemory() )
				{
					newCI->setDoesNotAccessMemory();
				}else if( CI->onlyReadsMemory() )
				{
					newCI->setOnlyReadsMemory();
				}else if( CI->doesNotReadMemory() )
				{
					newCI->setDoesNotReadMemory();
				}

				if( CI->onlyAccessesArgMemory() )
				{
					newCI->setOnlyAccessesArgMemory();
				}
				if( CI->onlyAccessesInaccessibleMemory() )
				{
					newCI->setOnlyAccessesInaccessibleMemory();
				}
				if( CI->doesNotReturn() )
				{
					newCI->setDoesNotReturn();
				}
				if( CI->doesNotThrow() )
				{
					newCI->setDoesNotThrow();
				}
				if( CI->cannotDuplicate() )
				{
					newCI->setCannotDuplicate();
				}
				if( CI->isConvergent() )
				{
					newCI->setConvergent();
				}else
				{
					newCI->setNotConvergent();
				}


				if( !(Callee->getType()->isVoidTy()) )
				{
					BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
				}
			}
			break;
		case Instruction::Select://ok in beforeBS.ll
			if(dyn_cast<SelectInst>(I))
			{
				Value* operands[3];

				//uselist search
				for(int i=0;i<3;i++)
				{
					Value* oldV = I->getOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}
					operands[i] = V;
				}
				buildval = dyn_cast<Value>(
					Builder.CreateSelect(
							/* Value *C */ operands[0],
							/* Value *True */ operands[1],
							/* Value *False */ operands[2]
						)
				);			
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::UserOp1:
		case Instruction::UserOp2://////////?what should we do?//////////
			errs()<<"##### UserOp is not implemented #######\n";
			break;
		case Instruction::VAArg:
			errs()<<"VAArg is not debugged\n";
			if(VAArgInst* VAAI = dyn_cast<VAArgInst>(I))
			{
				Value* oldV = VAAI->getPointerOperand();
				Value* V;
				if( oldV->hasName() &&
					BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
				{
					V = BuiltValMap.find(oldV->getName().str())->second;
				}else if( dyn_cast<Constant>(oldV) )
				{
					V = oldV;
					errs()<<"New V is same as the original V\n";
				}

				buildval = dyn_cast<Value>(
					Builder.CreateVAArg(
							/* Value *List */ V,
							/* Type *Ty */ VAAI->getType()
						)
				);			
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::ExtractElement:
			errs()<<"ExtractElement is not OK\n";
			if( dyn_cast<ExtractElementInst>(I) )
			{
				Value* operands[2];

				for(int i=0;i<2;i++)
				{
					Value* oldV = I->getOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}
					operands[i] = V;
				}

				buildval = dyn_cast<Value>(
					Builder.CreateExtractElement(
							/* Value *Vec */ operands[0],
							/* Value *Idx */ operands[1]
						)
				);			
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::InsertElement:
			errs()<<"InsertElement is not debugged\n";
			if( dyn_cast<InsertElementInst>(I) )
			{
				Value* operands[3];
				for(int i=0;i<3;i++)
				{
					Value* oldV = I->getOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}
					operands[i] = V;
				}
				buildval = dyn_cast<Value>(
					Builder.CreateInsertElement(
							/* Value *Vec */ operands[0],
							/* Value *NewElt */ operands[1],
							/* Value *Idx */ operands[2]
						)
				);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::ShuffleVector:
			errs()<<"ShuffleVector is not debugged\n";
			if( dyn_cast<ShuffleVectorInst>(I) )
			{
				Value* operands[3];

				for(int i=0;i<3;i++)
				{
					Value* oldV = I->getOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}
					operands[i] = V;
				}

				buildval = dyn_cast<Value>(
					Builder.CreateShuffleVector(
							/* Value *V1 */ operands[0],
							/* Value *V2 */ operands[1],
							/* Value *Mask */ operands[2]
						)
				);			
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::ExtractValue://ok in beforeBS.ll
			if(ExtractValueInst* EVI = dyn_cast<ExtractValueInst>(I))
			{
				Value* oldV = EVI->getAggregateOperand();
				Value* V;
				if( oldV->hasName() &&
					BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
				{
					V = BuiltValMap.find(oldV->getName().str())->second;
				}else if( dyn_cast<Constant>(oldV) )
				{
					V = oldV;
					errs()<<"New V is same as the original V\n";
				}
				buildval = dyn_cast<Value>(
					Builder.CreateExtractValue(
							/* Value *Agg */ V,
							/* ArrayRef<unsigned> Idxs */ EVI->getIndices()
						)
				);			
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::InsertValue:
			if(InsertValueInst* IVI = dyn_cast<InsertValueInst>(I))
			{
				Value* operands[2];

				//this process is equal to getoperand(0), and (1)
				//so we may change this code into getoperand code
				//or we can integrete this create process with similar processes
				for(int i=0;i<2;i++)
				{
					Value* oldV = I->getOperand(i);
					Value* V;
					if( oldV->hasName() &&
						BuiltValMap.find(oldV->getName().str()) != BuiltValMap.end() )
					{
						V = BuiltValMap.find(oldV->getName().str())->second;
					}else if( dyn_cast<Constant>(oldV) )
					{
						V = oldV;
						errs()<<"New V is same as the original V\n";
					}
					operands[i] = V;
				}

				//-----------------------------------------------
				buildval = dyn_cast<Value>(
					Builder.CreateInsertValue(
							/* Value *Agg */ operands[0],
							/* Value *Val */ operands[1],
							/* ArrayRef<unsigned> Idxs */ IVI->getIndices()
						)
				);			
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		case Instruction::LandingPad:
			if(LandingPadInst* LPI = dyn_cast<LandingPadInst>(I))
			{
				LandingPadInst* newLPI = Builder.CreateLandingPad(
							/* Type *Ty */ LPI->getType(),
							/* unsigned NumClauses */ LPI->getNumClauses()
						);

				unsigned int clausenum = LPI->getNumClauses();
				for(unsigned i = 0; i < clausenum; i++){
					//add clause of landing pad
					//getClause returns Constant*
					newLPI->addClause( LPI->getClause(i) );
				}
				newLPI->setCleanup(LPI->isCleanup());

				buildval = dyn_cast<Value>(newLPI);
				BuiltValMap.insert( make_pair( I->getName().str(),buildval ) );
			}
			break;
		default:
			break;
	}//end of switch

	//if Constant Expression is used, we check whether the used global variable is
	//directed as firstprivate or private or lastprivate value.
	//WARNING: we modify original instruction.
	if(Instruction* buildI = dyn_cast<Instruction>(buildval))
	{
		for(unsigned i=0; i<I->getNumOperands(); i++)
		{
			if( dyn_cast<Constant>(I->getOperand(i)) )
			{
				if(I->getOperand(i)->hasName() &&
					BuiltValMap.find( I->getOperand(i)->getName().str() ) != BuiltValMap.end())
				{	//this is for private global variable
					buildI->setOperand(i, BuiltValMap.find( I->getOperand(i)->getName().str() )->second);
				}else if( ConstantExpr* CE = dyn_cast<ConstantExpr>(I->getOperand(i)) )
				{
					I->getOperand(i)->print(errs()); errs()<<"\n";
					switch(CE->getOpcode())
					{
						case Instruction::GetElementPtr:
							if(CE->isGEPWithNoNotionalOverIndexing() )
							{
								if( CE->getOperand(0)->hasName() &&
									BuiltValMap.find( CE->getOperand(0)->getName().str() ) != BuiltValMap.end() )
								{
									Value* GEPPtr = CE->getOperand(0);
									auto valitr = BuiltValMap.find( GEPPtr->getName().str() );
									if(valitr != BuiltValMap.end())
									{	//found
										GEPPtr = valitr->second;
									}
									vector<Value*> IdxVector;
									for(unsigned ii = 1;ii < CE->getNumOperands(); ii++)
									{
										IdxVector.push_back( CE->getOperand(ii) );
									}
									ArrayRef<Value*> IdxList(IdxVector);
									Type* GEPTy;
									if(PointerType* PT = dyn_cast<PointerType>( GEPPtr->getType() ))
									{
										GEPTy = PT->getElementType();
									}

									Builder.SetInsertPoint(buildI);
									GetElementPtrInst* newGEP = dyn_cast<GetElementPtrInst>(
										Builder.CreateGEP(
												/*Type* Ty*/ GEPTy,
												/*Value* Ptr*/ GEPPtr,
												/*ArrayRef<Value*> IdxList*/ IdxList
										)
									);
									newGEP->setIsInBounds(1);//this is ConstantExpr so inbounds
									buildI->setOperand(i, dyn_cast<Value>(newGEP));
									Builder.SetInsertPoint( Builder.GetInsertBlock() );
								}
							}
							break;
						// Cast operators ...
						case Instruction::Trunc:
						case Instruction::ZExt:
						case Instruction::SExt:
						case Instruction::FPToUI:
						case Instruction::FPToSI:
						case Instruction::UIToFP: 
						case Instruction::SIToFP:
						case Instruction::FPTrunc:
						case Instruction::FPExt:
						case Instruction::PtrToInt:
						case Instruction::IntToPtr:
						case Instruction::BitCast:
						case Instruction::AddrSpaceCast:
							if(CE->isCast())
							{
								if( CE->getOperand(0)->hasName() &&
									BuiltValMap.find( CE->getOperand(0)->getName().str() ) != BuiltValMap.end() )
								{
									//global variables that contain scalar or pointer are handled here.
									valitr = BuiltValMap.find(CE->getOperand(0)->getName().str());
									
									Type* DestTy = CE->getType();

									//(debug)
									errs()<<"Cast From: ";
									valitr->second->print(errs()); errs()<<"\n";
									errs()<<"Cast To: ";
									DestTy->print(errs()); errs()<<"\n";
									//This is Load inst, so OK

									Builder.SetInsertPoint(buildI);
									Value* castV = Builder.CreateCast(
										/*Instruction::CastOps Op*/Instruction::CastOps( CE->getOpcode() ),
										/*Value *V*/valitr->second,
										/*Type* DestTy*/ DestTy
									);									
									buildI->setOperand(i, castV);									
									Builder.SetInsertPoint( Builder.GetInsertBlock() );
								}
							}
							break;
						default:
							errs()<<"This CE is not what we can handle\n";
							CE->dump();
					}
				}
			}
		}
	}else if(dyn_cast<Constant>(buildval))
	{
		//do nothing
	}else
	{
		errs()<<"### buildval is not judged whether it contains privatized global ###\n";
		buildval->dump();
	}


	string str = I->getName().str();
	if( !( str[0] >= '0' && str[0] <= '9' ) )
	{
		buildval->setName( str );
	}
	errs()<<"Gained Value:\t";
	buildval->print(errs());errs()<<"\n";

	return buildval;
}

void ParamGet::ParamGetCommon::removeOriginalIR(Module &M, IRBuilder<> &Builder,
	Loop* targetLoop, BasicBlock* parallel_region_Successor)
{
	map<string, BasicBlock*> BBmap;
	BasicBlock* beforeloopBB = CapturedInstVector.begin()->second->getParent();

	vector<Instruction*> DroppedInstVector;
	//erase insts
	//(debug)
	// errs()<<"Inst erase\n";
	//---//

	auto institr=CapturedInstVector.begin();
	//do not erase the first bb of original loop//because we will generated the thread fork call inst in that BB.
	for(;institr!=CapturedInstVector.end();++institr)
	{
		Instruction* I = institr->second;
		I->replaceAllUsesWith(UndefValue::get(I->getType()));
		DroppedInstVector.push_back(I);

		if(I->getOpcode() == Instruction::Br){
			++institr;
			break;
		}
	}
	for(;institr!=CapturedInstVector.end();++institr)
	{
		Instruction* I = institr->second;
		BasicBlock* tempBB = I->getParent();
		if(I->getOpcode() == Instruction::Ret){
			I->setName("");
			I->getParent()->setName("");
			continue;
		}

		auto bbitr = BBmap.find(tempBB->getName().str());
		if(bbitr == BBmap.end()){
			//(debug)
			// errs()<<"BBname:\t"<< tempBB->getName().str() <<"\n";
			BBmap.insert(make_pair(tempBB->getName().str(),tempBB));
		}
		//(debug)
		// errs()<<"Drop\t";
		// I->print(errs()); errs()<<"\n";

		I->replaceAllUsesWith( UndefValue::get(I->getType()) );
		DroppedInstVector.push_back(I);

	}

	for(auto remitr=DroppedInstVector.begin(); remitr!=DroppedInstVector.end(); ++remitr )
	{
		Instruction* I = *remitr;

		//(debug)
		errs()<<"Erase\t";
		I->print(errs()); errs()<<"\n";

		I->eraseFromParent();
	}
	// //collect bbs in loop.
	// for(auto bbitr = targetLoop->block_begin();
	// 	bbitr != targetLoop->block_end();++bbitr)
	// {
	// 	BasicBlock* BB = *bbitr;
	// 	BBVector.push_back(BB);
	// }

	//erase bbs
	//(debug)
	// errs()<<"bb erase\n";
	//---//
	for(auto bbitr = BBmap.begin();
		bbitr != BBmap.end();++bbitr)
	{
		BasicBlock* BB = bbitr->second;
		if(beforeloopBB->getName().str() == bbitr->first)
		{
			//this is where the kmpc_fork_call is called
			continue;
		}
		BB->eraseFromParent();//
	}

	//clear omp_outlined.func's BB name
	for(auto bbitr = CreatedBB.begin();
		bbitr != CreatedBB.end();++bbitr)
	{
		if( bbitr->first[0] >= '0' && bbitr->first[0] <= '9' )
		{
			bbitr->second->setName("");
		}
	}

	//drop names
	for(auto valitr=FuncArgumentMap.begin();valitr!=FuncArgumentMap.end();++valitr)
	{
		//Value* V = valitr->second;
		if( valitr->first[0] >= '0' && valitr->first[0] <= '9' )
		{
			valitr->second->setName("");
		}
	}
	//Builder.SetInsertPoint(beforeloopBB);
	//Builder.CreateBr(parallel_region_Successor);
}

void ParamGet::ParamGetCommon::judgeParameter(Value* argVal, Function* ParentF)
{
	if( dyn_cast<Constant>(argVal) ||
			dyn_cast<GlobalValue>(argVal) ||
			dyn_cast<BasicBlock>(argVal) ||
			dyn_cast<Function>(argVal) 
	){
		return;
	}

	if( dyn_cast<Instruction>(argVal) || dyn_cast<Argument>(argVal) ){
		string argName;
		if(!argVal->hasName()){	
			//name get
			argVal->setName(IntToName(instnum));	
			this->instnum++;
			argName = argVal->getName().str();
		}else{	
			//check CapturedInstVector and FuncArgumentMap
			argName = argVal->getName().str();
			if( CapturedValMap.find(argName) != CapturedValMap.end() ||
					FuncArgumentMap.find(argName) != FuncArgumentMap.end()
			){
				return;
			}
		}
		//setName may be changed if same name is used.
		//so we use val->getName()
		FuncArgumentMap.insert(make_pair( argName, argVal ));
		return;
	}
	//debug
	errs()<<"[ParamGet] we cannot judgeParameter this value: ";
	argVal->print(errs());
	errs()<<"\n";
}

void ParamGet::ParamGetCommon::judgeArgument(Instruction *linstitr)
{
	//inst in loop iteration------
		Instruction& I = *linstitr;
		//(debug)
		// errs()<<"judgeArgument:\t";
		// I.print(errs());errs()<<"\n";

		//---inst name set---//
		if(!( I.hasName() )) 
		{
			//errs()<<"non name I insertCapArgument\n";
			this->insertCapArgument(&I,this->instnum);
			//the name of the method "ArgumentInsert" is so confusing
			this->instnum++;
		}else
		{
			if(CapturedValMap.find( I.getName().str() ) == CapturedValMap.end())
			{	
				//errs()<<"named I insertCapArgument\n";
				this->insertCapArgument(&I,this->instnum);
				//the name of the method "ArgumentInsert" is so confusing
				this->instnum++;

				auto valitr = FuncArgumentMap.find( I.getName().str() );
				if( valitr != FuncArgumentMap.end() )
				{
					FuncArgumentMap.erase( valitr );
					errs()<<"erased from FuncArgumentMap:\t";
					valitr->second->print(errs()); errs()<<"\n";
				}
			}
		}	

		//judge this I's operand		
		Function *ParentF = I.getParent()->getParent();
		switch(I.getOpcode())
		{
			case Instruction::PHI:
				if( PHINode* phiinst = dyn_cast<PHINode>(&I) )
				{
					for(unsigned i = 0; i<phiinst->getNumIncomingValues(); i++)
					{
						// errs()<<"PHI Judging:\t";
						// I.getOperand(i)->print(errs());errs()<<"\n";
						this->judgeParameter( phiinst->getIncomingValue(i),ParentF );
					}
				}
				break;
			default:
				for(unsigned i=0;i<I.getNumOperands();i++)
				{
					// errs()<<"Judging:\t";
					// I.getOperand(i)->print(errs());errs()<<"\n";
					this->judgeParameter( I.getOperand(i),ParentF );

				}
				break;//back to for-loop
		}
		// if(arginst)
		// {
		// 	errs()<<"arginst init: ";
		// 	arginst->print(errs());errs()<<"\n";
		// }
		//---getelement ptr tracker---//
		//---now we have get pointers that used in loop
}





void ParamGet::ParamGetCommon::buildFuncEntryBB(Module &M, StructType* IdentT, Loop* targetLoop,
		IRBuilder<> &Builder,Function* OutFunc)
{
	insertForOMPVarAndArgument(M, Builder,OutFunc,targetLoop);//please judge whether loopvarcopy load is occured in new func


	//insert bounds determined by using InitLoad and CondLoad
	//(debug)
	// errs()<<"insertBounds start\n";
	insertBounds(Builder, 
		OMPParamMap.find("Init_built")->second, 
		OMPParamMap.find("Cond_built")->second
		);
	//(debug)
	// errs()<<"insertBounds end\n";

	//global var for OpenMP Runtime Library
	//  OMPParamMap will be:
	// str 	|data
	// "0"		|var for fini (for Outlined Function0)
	// "1"		|var for init (for Outlined Function0)
	// "2"		|var for fini (for Outlined Function1)
	// "3"		|var for init (for Outlined Function1)
	// :
	
	createIdentTGlobalVar(M,Builder,IdentT);

	//call kmpc_global_thread_num//
	/* OMPParamMap will be updated:
	str 			|data
	"global_tid"		|global thread id of a thread
	*/


}

void ParamGet::ParamGetCommon::setLoopExpr(Loop* targetLoop)
{
	//find loop-exit condition //search from back
	ArrayRef<BasicBlock*> Blocks = targetLoop->getBlocks();
	for(auto bbitr = Blocks.rbegin(); bbitr != Blocks.rend(); ++bbitr)
	{
		BasicBlock* ExitingBlock = *bbitr;
		if( !(targetLoop->isLoopExiting(ExitingBlock)) ){
			continue;
		}
		//search loop-exit BB's teminator
		if( BranchInst* BI = dyn_cast<BranchInst>(ExitingBlock->getTerminator()) )
		{
			if( !(BI->isConditional()) ){
				continue;
			}
			//(debug)
			// errs()<<"ExitBlock:\n";
			// targetLoop->getExitBlock()->print(errs()); errs()<<"\n";
			// errs()<<"LoopLatch:\n";
			// targetLoop->getLoopLatch()->print(errs()); errs()<<"\n";
			// errs()<<"Preheader:\n";
			// targetLoop->getLoopPreheader()->print(errs()); errs()<<"\n";
			// errs()<<"Predecessor:\n";
			// targetLoop->getLoopPredecessor()->print(errs()); errs()<<"\n";
			// errs()<<"ExitingBlock:\n";
			// ExitingBlock->print(errs()); errs()<<"\n";

			//this is first aid.
			//Check whether the conditional branch has edge to inner and outer of the loop
			bool InFlag = false, OutFlag = false;
			unsigned successors = BI->getNumSuccessors();
			for(unsigned i = 0; i < successors; i++){
				BasicBlock* sucBB = BI->getSuccessor(i);
				//(debug)
				// errs()<<"sucBB->str: "<<sucBB->getName().str()<<"\n";
				if( !targetLoop->contains(sucBB) ){
					OutFlag = true;
					parallel_region_Successor = sucBB;
				}else{
					//set CondTrueIsIterate(global member of PGC)
					//true side is i==0
					if(i==0){
						CondTrueIsIterate = true;
					}else{
						CondTrueIsIterate = false;
					}
					InFlag = true;
				}
			}
			if(!(OutFlag && InFlag)){
				continue;
			}

			//determine whether this cond uses the loop counter in evaluation of condition
			//we assume that targeted loop is single-entry and single-exit. 
			if( Instruction* CondInst = dyn_cast<Instruction>(BI->getCondition()) )
			{
				//the right successor isn't contained in parallel execution						
				if(ExitingBlock->hasName())
				{
					//get inital and increment values of the loop counter
					//set LoopExpr("init"), LoopExpr("incr")
					/*
					1.Find the same memory access
					2.Set the initial and increment values
					*/
					setLoopInitAndIncr(CondInst,targetLoop);
					LoopExpr.insert(make_pair("cond",dyn_cast<Value>(CondInst)));//can we set cond after setLoopInitAndIncr?

					//(debug)
					// errs()<<"setLoopInitAndIncr End\n";
					errs()<<"init:\t";
					LoopExpr.find("init")->second->print(errs());errs()<<"\n";	
					errs()<<"cond:\t";
					CondInst->print(errs());errs()<<"\n";
					errs()<<"incr:\t";
					LoopExpr.find("incr")->second->print(errs());errs()<<"\n";

				}//end of ExitingBlock->hasName()
				break;//break the search of loop exiting BB
			}
		}//end of BranchInst BI
	}//end of ExitingBlock
}


void ParamGet::ParamGetCommon::setLoopInitAndIncr(Instruction* CondInst,Loop* targetLoop)
{
	for(unsigned i=0;i<CondInst->getNumOperands();i++)
	{
		if( Instruction* LoopCounterI = dyn_cast<Instruction>(CondInst->getOperand(i)))
		{
			string nonestr = "";

			//(debug)
			errs()<<"Loop counter: ";
			LoopCounterI->print(errs());
			errs()<<"\n";
			switch(LoopCounterI->getOpcode())
			{
				case Instruction::Load:
					LoopCounterIsPHI = false;
					setLoadInitAndIncr(LoopCounterI,targetLoop,LoopExpr,nonestr);
					break;
				case Instruction::PHI:
					LoopCounterIsPHI = true;
					if( PHINode* PHIN = dyn_cast<PHINode>(LoopCounterI) ){
						//(debug)
						// errs()<<"PHIN: ";
						// PHIN->print(errs());
						// errs()<<"\n";
						setPHIInitAndIncr(PHIN,targetLoop,LoopExpr,nonestr);
					}
					break;
				// default:
					// errs()<<"[ParamGet] We can handle only the loop counter represented with store/load or phi.\n";
					// exit(1);
			}
			if( LoopExpr.find("init") != LoopExpr.end() )
			{
				return;
			}
			//check next operand
			setLoopInitAndIncr(LoopCounterI,targetLoop);
		}
	}
}

void ParamGet::ParamGetCommon::setPHIInitAndIncr(PHINode* PHIN, Loop* targetLoop, 
	StrVmap &RegisterMap, string name)
{
	const string initstr = name + "init";
	const string incrstr = name + "incr";
	bool OutsideFlag = false;
	bool InsideFlag = false;
	Value* InsideV;

	//check the phi has incoming values inside and outside of the loop
	for(unsigned i = 0; i<PHIN->getNumIncomingValues(); i++)
	{
		BasicBlock* IncomingBB = PHIN->getIncomingBlock(i);
		if( targetLoop->contains(IncomingBB) )
		{
			InsideFlag = true;
			InsideV = PHIN->getIncomingValue(i);
		}else
		{
			OutsideFlag = true;
		}
	}//end of for(i) loop

	if( OutsideFlag && InsideFlag )
	{
		// //(debug)
		// errs()<<"IncomingV first: ";
		// IncomingV->print(errs());
		// errs()<<"\n";

		//Is this needed? I think we can just record the IncomingV for initial value
		// setPHIInitAndIncrTracing(PHIN,
		// 	IncomingV, RegisterMap, initstr);
		RegisterMap.insert(make_pair(
			initstr,
			PHIN
		));
		RegisterMap.insert(make_pair(
			incrstr,
			InsideV
		));
	}
}

void ParamGet::ParamGetCommon::setLoadInitAndIncr(Instruction* LoopCounterI, Loop* targetLoop, StrVmap &RegisterMap, string name)
{
	const string initstr = name + "init";
	const string incrstr = name + "incr";
	Value* LoadMemory = LoopCounterI->getOperand(0);
	//Find the Store Inst inside loop
	if(LoadMemory->hasName())
	{
		string LoadMemoryStr = LoadMemory->getName().str();
		for(auto uitr = LoadMemory->user_begin(); 
			uitr != LoadMemory->user_end(); ++uitr)
		{
			if( StoreInst* SI = dyn_cast<StoreInst>( *uitr ) )
			{
				Value* StoreMemory = SI->getOperand(1);
				if(targetLoop->contains(SI) && StoreMemory->hasName() && StoreMemory->getName().str() == LoadMemoryStr)
				{
					// //(debug)
					// errs()<<"Load store found: ";
					// SI->print(errs());
					// errs()<<"\n";
					RegisterMap.insert(make_pair(
						initstr,
						dyn_cast<Value>(LoopCounterI)
					));
					RegisterMap.insert(make_pair(
						incrstr,
						SI->getOperand(0)
					));


					// setLoadAsInitIfLoadedMemoryIsUsedIn(SI->getOperand(0), dyn_cast<Value>(LoopCounterI), RegisterMap, initstr);
					// if( RegisterMap.find( initstr ) != RegisterMap.end() )
					// {
					// 	RegisterMap.insert(make_pair(incrstr,SI->getOperand(0)));
					// }
				}
			}
			if( RegisterMap.find(initstr) != RegisterMap.end() )
			{
				// errs()<<"[ParamGet] We can't find store of the loop counter.\n";
				return;
			}
		}		
	}

}


//build init value of reduction variables
//リダクションのその反復における初期値を計算して格納する．
//そうしなければ，ループ下限とか上限で0とかが入ってしまう．
void ParamGet::ParamGetCommon::buildInitialValueOfIndexVariable(IRBuilder<> &Builder, Loop* targetLoop)
{
	StrVmap IndexValMap;
	vector< string > IndexStrVector;
	string loopinitstr = LoopExpr.find("init")->second->getName().str();
	string loopincrstr = LoopExpr.find("incr")->second->getName().str();

	//find index variables
	for(auto institr : CapturedInstVector)
	{
		Instruction* capturedI = institr.second;
		//debug
		errs()<<"InitialValue capturedI: ";
		capturedI->print(errs());
		errs()<<"\n";
		//skip the loop counter
		if(isLoopCounter(dyn_cast<Value>(capturedI))){
			continue;
		}else if(capturedI->hasName())
		{
			string initstr = capturedI->getName().str();
			//skip the induction variables
			//skip reduction (phi but is specified with its scalar)
			if(ReductionValMap.find(initstr) != ReductionValMap.end())
			{
				continue;
			}
			//skip the load from induction variable (memory)
			else if( capturedI->getOpcode() == Instruction::Load ) 
			{
				//if it is reduction variable's memory, skip it
				Value* checkingMemory = capturedI->getOperand(0);
				if( checkingMemory->hasName() && ReductionValMap.find(checkingMemory->getName().str()) != ReductionValMap.end() 
				){
					continue;
				}
			}

			//if it is reduction, skip it
			bool RedPHIExistFlag = false;
			for(auto valitr : ReductionPHIMap)
			{
				Value* RedPHIVal = valitr.second;
				if( RedPHIVal->hasName() )
				{
					string redstr = RedPHIVal->getName().str();
					if( redstr == initstr )
					{
						RedPHIExistFlag = true;
						break;
					}
				}
			}
			if(RedPHIExistFlag)
			{
				continue;
			}

			//Find incr and init for the capturedInst
			if( dyn_cast<LoadInst>(capturedI) || dyn_cast<PHINode>(capturedI) )
			{
				bool InnerLoopIncludesFlag = false;
				for( auto loopitr = targetLoop->begin(); loopitr != targetLoop->end(); ++loopitr )
				{
					Loop* innerLoop = *loopitr;
					if( innerLoop->contains(capturedI) )
					{
						InnerLoopIncludesFlag = true;
						break;
					}
				}

				if( InnerLoopIncludesFlag == false )
				{
					//(debug)
					errs()<<"IndexVariable Check: ";
					capturedI->print(errs());
					errs()<<"\n";
					
					handleInstToFindInitAndIncr(capturedI, targetLoop, IndexValMap, initstr);

					if(IndexValMap.find(initstr + "init") != IndexValMap.end())
					{
						//(debug)
						errs()<<"IndexVariable found: ";
						IndexValMap.find( initstr + "init" )->second->print(errs());
						errs()<<"\n";

						IndexStrVector.push_back(initstr);
					}							

				}		
			}
		}
	}

	//(debug)
	errs()<<"IndexValMap:\n";
	for(auto stritr = IndexStrVector.begin(); stritr != IndexStrVector.end(); ++stritr)
	{
		string str = *stritr + "init";
		errs()<<"name: "<< str <<", val: ";
		IndexValMap.find(str)->second->print(errs());
		errs()<<"\n";
		str = *stritr + "incr";
		errs()<<"name: "<< str <<", val: ";
		IndexValMap.find(str)->second->print(errs());
		errs()<<"\n";
	}

	//trace IVs and build
	for(auto stritr = IndexStrVector.begin(); stritr != IndexStrVector.end(); ++stritr)
	{
		StrVvector ValTraceVector;
		Value* IndexVal;
		string inststr = *stritr;
		string mapstr = inststr + "init";

		//(debug)
		errs()<<"Index Value Tracing Start\n";
		errs()<<"inst name: "<< inststr <<", val: ";
		IndexValMap.find(mapstr)->second->print(errs());
		errs()<<"\n";

		//trace init
		IndexVal = IndexValMap.find(mapstr)->second;
		if(	Instruction* InitInst = dyn_cast<Instruction>(IndexVal) )
		{
			//if loop counter is load
			if(InitInst->getOpcode() == Instruction::Load)
			{
				for(auto capitr = CapturedInstVector.begin(); capitr != CapturedInstVector.end(); ++capitr)
				{
					//find store that stores to init load's memory
					Instruction* CheckingInst = capitr->second;
					if(CheckingInst->getOpcode() == Instruction::Store)
					{
						//check their pointers name
						if( CheckingInst->getOperand(1)->getName().str() == 
								InitInst->getOperand(0)->getName().str() )
						{
							//trace the stored value
							traceInstForUBCalc( 
								CheckingInst->getOperand(0),
								ValTraceVector,
								IndexValMap,inststr,
								targetLoop,
								0
							);
							break;
						}
					}
					//if reach the end, the first value is already in memory,
					//so use the result of its load inst as first value.
					if( ValTraceVector.empty() )
					{
						ValTraceVector.push_back(make_pair(InitInst->getName().str(),InitInst));
						break;
					}
				}	
			}else if(InitInst->getOpcode() == Instruction::PHI)//if loop counter is phi
			{
				PHINode* PHIN = dyn_cast<PHINode>(InitInst);
				for(unsigned i=0; i<PHIN->getNumIncomingValues(); i++)
				{
					//trace phi's incoming value which corresponds to a BB that is outer of loop
					//we assume the targeted loop is single-entry.
					if( !(targetLoop->contains(PHIN->getIncomingBlock(i))) )
					{
						traceInstForUBCalc( 
							PHIN->getIncomingValue(i),
							ValTraceVector,
							IndexValMap,inststr,
							targetLoop,
							0
						);
						break;
					}
				}
			}		
		}else
		{
			errs()<<"[ParamGet] Index Val map's element doesn't have correct name\n";
			errs()<<"inst name: "<< inststr <<", map name: "<< mapstr <<"\nval: ";
			IndexVal->print(errs());
			errs()<<"\n";
		}
		//(debug)
		errs()<<"---Init Trace is over---\n";
		for(auto institr=ValTraceVector.begin(); institr!=ValTraceVector.end(); ++institr)
		{
			institr->second->print(errs());errs()<<"\n";
		}
		errs()<<"---Show is over---\n";

		//build traced vector
		IndexVal = buildTraceVector(Builder, ValTraceVector,targetLoop,
			IndexValMap, inststr+"init", 0, 0);	
		IndexValMap.insert(
			make_pair( inststr+"init_built" ,IndexVal)
		);

		//delete from builtvalmap if it exist in loop
		for(auto valitr =  ValTraceVector.begin(); valitr != ValTraceVector.end(); ++valitr )
		{
			if( Instruction* I = dyn_cast<Instruction>(valitr->second) )
			{
				if(targetLoop->contains(I) && I->hasName())
				{
					BuiltValMap.erase( BuiltValMap.find(I->getName().str()) );
				}
			}
		}


		//(debug)
		errs()<<"built: "<< inststr <<", val: ";
		IndexVal->print(errs());
		errs()<<"\n";


		//trace incr
		ValTraceVector.clear();
		mapstr = inststr + "incr";
		IndexVal = IndexValMap.find(mapstr)->second;
		//trace incr
		//trace the incr of IVs
		if( GetElementPtrInst* GEP = dyn_cast<GetElementPtrInst>(IndexVal) )
		{
			IndexVal = *(GEP->idx_begin());
		}
		traceInstForUBCalc(IndexVal,ValTraceVector,	IndexValMap,inststr+"init",targetLoop,1);
		//(debug)
		errs()<<"---Incr Trace is over---\n";
		for(auto institr=ValTraceVector.begin(); institr!=ValTraceVector.end(); ++institr)
		{
			institr->second->print(errs());errs()<<"\n";
		}
		errs()<<"---Show is over---\n";


		//build traced vector
		IndexVal = buildTraceVector(Builder, ValTraceVector, targetLoop,
			IndexValMap, inststr+"init", 0, 1);	
		IndexValMap.insert(
			make_pair( inststr+"incr_built" ,IndexVal)
		);
		//delete from builtvalmap if it exist in loop
		for(auto valitr =  ValTraceVector.begin(); valitr != ValTraceVector.end(); ++valitr )
		{
			if( Instruction* I = dyn_cast<Instruction>(valitr->second) )
			{
				if(targetLoop->contains(I) && I->hasName())
				{
					BuiltValMap.erase( BuiltValMap.find(I->getName().str()) );
				}
			}
		}
		//(debug)
		errs()<<"built: "<< inststr <<", val: ";
		IndexVal->print(errs());
		errs()<<"\n";
	}


	// //calculate the loop tripcount
	// //UB_calc + LB_calc 
	// //(UB_calc + LB_calc) / Incr_built => TripCount
	// Value* UBCalc = OMPParamMap.find("UB_calc")->second;
	// Value* LBCalc = OMPParamMap.find("LB_calc")->second;
	// Value* IncrBuilt = OMPParamMap.find("Incr_built")->second;
	// Value* TripCount = Builder.CreateSub(
	// 	UBCalc,
	// 	LBCalc
	// 	);
	// TripCount = Builder.CreateSDiv(
	// 	TripCount,
	// 	IncrBuilt
	// 	);
	// OMPParamMap.insert(make_pair(
	// 	"TripCount",TripCount
	// 	));

	//calculate init of IVs for threads
	for(auto stritr = IndexStrVector.begin(); stritr != IndexStrVector.end(); ++stritr)
	{
		// IndexInit = inststr + "init_built"
		// IndexIncr = inststr + "incr_built"
		string inststr = *stritr;
		Value* IndexInit = IndexValMap.find( inststr + "init_built" )->second;
		Value* IndexIncr = IndexValMap.find( inststr + "incr_built" )->second;

		//TripCount * IndexIncr //signed value
		//IndexInit + (TripCount * IndexIncr)
		//(IndexInit + (TripCount * IndexIncr)) / global_tid => ThreadIndexInit
		// Value* InitForThread = Builder.CreateSDiv(
		// 	OMPParamMap.find("plower_Load")->second,
		// 	OMPParamMap.find("Incr_built")->second
		// 	);
		// InitForThread = Builder.CreateSub(
		// 	OMPParamMap.find("plower_Load")->second,
		// 	OMPParamMap.find("Init_built")->second
		// 	);
		Value* InitForThread = Builder.CreateMul(
			OMPParamMap.find("plower_Load")->second,
			IndexIncr
			);
		InitForThread = Builder.CreateAdd(
			InitForThread,
			IndexInit
			);
		///////////////////////////////////////////////
		// please built reverse incr if loop counter decrement loop ?
		// a canonical loop won't need reverse incr, will it?
		//////////////////////////////////////////////
		
		//IndexValMap.insert( inststr + "init_thread", InitForThread);

		//register the init of IVs as original IVs
		//if an IV is phi inst, ReplaceValWith // val corresponding to outside BBs.
		//if an IV is load inst, store to its memory
		Value* IndexVal = IndexValMap.find(inststr + "init")->second;
		if( PHINode* PHIN = dyn_cast<PHINode>(IndexVal) )
		{
			unsigned incomings = PHIN->getNumIncomingValues();
			for(unsigned i=0; i<incomings; i++)
			{
				if( !(targetLoop->contains( PHIN->getIncomingBlock(i) )) )
				{
					Value* IncomingV = PHIN->getIncomingValue(i);
					PHIN->replaceUsesOfWith( IncomingV, InitForThread );
				}
			}
		}else if( LoadInst* LI = dyn_cast<LoadInst>(IndexVal) )
		{
			Value* MemVal = LI->getOperand(0);
			unsigned align = calcAlignOfType( InitForThread->getType() );
			Builder.CreateAlignedStore(
				InitForThread,
				MemVal,
				align
				);
		}else
		{
			//(debug)
			errs()<<"[ParamGet] IndexInit is not phi or load or cast";
			IndexVal->dump();
		}

	}

}

void ParamGet::ParamGetCommon::handleInstToFindInitAndIncr(Instruction* I,
	Loop* targetLoop, StrVmap &RegisterMap, string name)
{
	string initstr = name + "init";
	string incrstr = name + "incr";
	if(I->getOpcode() == Instruction::Load)
	{
		Value* MemVal = I->getOperand(0);
		for(auto useritr = MemVal->user_begin(); 
			useritr != MemVal->user_end(); ++useritr)
		{
			if( StoreInst* SI = dyn_cast<StoreInst>( *useritr ) )
			{
				//we don't check whether SI or I->getOperand(0) hasName
				if(targetLoop->contains(SI) &&
					SI->getOperand(1)->getName().str() == I->getOperand(0)->getName().str())
				{

					//(debug)
					errs()<<"Load store found: ";
					SI->print(errs());
					errs()<<"\n";
					setLoadAsInitIfLoadedMemoryIsUsedIn(SI->getOperand(0),
						dyn_cast<Value>(I), RegisterMap, initstr);
					if( RegisterMap.find( initstr ) != RegisterMap.end() )
					{
						RegisterMap.insert(make_pair(incrstr,SI->getOperand(0)));
					}
				}
			}
			if( RegisterMap.find(initstr) != RegisterMap.end() )
			{
				break;
			}
		}
	}else if(I->getOpcode() == Instruction::PHI)
	{
		if( PHINode* PHIN = dyn_cast<PHINode>(I) )
		{
			//(debug)
			// errs()<<"PHIN: ";
			// PHIN->print(errs());
			// errs()<<"\n";

			string PhiStr = PHIN->getName().str();

			//(debug)
			// errs()<<"PhiStr: "<<PhiStr;
			// errs()<<"\n";
			bool IncludeOutsideLoopBBFlag = false;
			bool IncludeInsideLoopBBFlag = false;

			for(unsigned ii = 0; ii<PHIN->getNumIncomingValues(); ii++)
			{
				BasicBlock* IncomingBB = PHIN->getIncomingBlock(ii);
				if( targetLoop->contains(IncomingBB) )
				{
					IncludeInsideLoopBBFlag = true;
				}else
				{
					IncludeOutsideLoopBBFlag = true;
				}
			}//end of for(ii) loop
			if( IncludeInsideLoopBBFlag && IncludeOutsideLoopBBFlag )
			{
				for(unsigned ii = 0; ii<PHIN->getNumIncomingValues(); ii++)
				{
					Value* IncomingV = PHIN->getIncomingValue(ii);
					if(Instruction* IncomingI = dyn_cast<Instruction>(IncomingV) )
					{
						BasicBlock* IncomingBB = PHIN->getIncomingBlock(ii);
						if( targetLoop->contains(IncomingI) &&
							targetLoop->contains(IncomingBB) )
						{
							// //(debug)
							// errs()<<"PHIN first: ";
							// PHIN->print(errs());
							// errs()<<"\n";

							// //(debug)
							// errs()<<"IncomingV first: ";
							// IncomingV->print(errs());
							// errs()<<"\n";

							setPhiAsInitIfPhiIsUsedIn(PHIN,
								IncomingV, RegisterMap, initstr);
							if( RegisterMap.find(initstr) != RegisterMap.end() )
							{
								RegisterMap.insert(
									make_pair(incrstr,IncomingV)
								);
							}else
							{
								errs()<<"this phi is not used in incr\n";
								//phi_is_used = true;//this is wrong but not doing this, 
								//this function will be infinity loop
							}
							break;
						}
					}
				}//end of for(ii) loop				
			}
		}
	}

}

void ParamGet::ParamGetCommon::setPhiAsInitIfPhiIsUsedIn( PHINode* PHIN, Value* CheckedValue,
	StrVmap &RegisterMap, string initstr)
{
	//trace incr
	if(Instruction* CheckedI = dyn_cast<Instruction>(CheckedValue))
	{
		//(debug)
		// errs()<<"CheckedI inner: ";
		// CheckedI->print(errs());
		// errs()<<"\n";

		for(unsigned i=0; i<CheckedI->getNumOperands(); i++)
		{
			//(debug)
			// errs()<<"Operand inner: ";
			// CheckedI->getOperand(i)->print(errs());
			// errs()<<"\n";


			if( CheckedI->getOperand(i)->hasName() )
			{
				string checkstr = CheckedI->getOperand(i)->getName().str();
				if( PHINode* InterPHIN = dyn_cast<PHINode>(CheckedI) )
				{
					//FIXME: if phi is used in inner loop's phi,
					//wrong incr 
					if( RegisterMap.find( checkstr ) != RegisterMap.end() )
					{
						continue;
					}
					//this is to avoid infiniti reference of phi to phi
					RegisterMap.insert( make_pair(
						PHIN->getName().str(),
						dyn_cast<Value>(PHIN))
					);
					setPhiAsInitIfPhiIsUsedIn( InterPHIN, CheckedI->getOperand(i),
						RegisterMap, PHIN->getName().str() );	

					RegisterMap.erase( PHIN->getName().str() );
					if( RegisterMap.find( initstr ) != RegisterMap.end() )
					{
						RegisterMap.erase( RegisterMap.find(initstr) );
						RegisterMap.insert(make_pair(initstr,dyn_cast<Value>(PHIN)));	
					}				
				}else if( checkstr == PHIN->getName().str() )
				{
					if( dyn_cast<BinaryOperator>(CheckedI) ||
						dyn_cast<GetElementPtrInst>(CheckedI) )
					{
						// errs()<<"PHI is Used\n";
						RegisterMap.insert(make_pair(initstr,dyn_cast<Value>(PHIN)));
					}
				}else
				{
					setPhiAsInitIfPhiIsUsedIn( PHIN, CheckedI->getOperand(i),
						RegisterMap, initstr );
				}
			}

			if( RegisterMap.find(initstr) != RegisterMap.end() )
			{
				return;
			}
		}
	}else
	{
		//errs()<<"This Operand is not Instruction\n";
		return;
	}
}
void ParamGet::ParamGetCommon::setLoadAsInitIfLoadedMemoryIsUsedIn(Value* UsingValue, 
	Value* LoadingValue, StrVmap &RegisterMap, string initstr)
{
	Value* LoadedMemory = dyn_cast<Instruction>(LoadingValue)->getOperand(0);
	if( Instruction* UsingI = dyn_cast<Instruction>(UsingValue) )
	{
		for(unsigned i=0; i<UsingI->getNumOperands(); i++)
		{
			Value* CheckOp = UsingI->getOperand(i);
			if( CheckOp->hasName() )
			{
				if( LoadInst* LI = dyn_cast<LoadInst>(CheckOp) )
				{
					if( LI->getOperand(0)->hasName() && 
						LI->getOperand(0)->getName().str() == LoadedMemory->getName().str() )
					{
						errs()<<"Load is Used\n";
						RegisterMap.insert(make_pair(initstr,LoadingValue));
					}
				}else
				{
					setLoadAsInitIfLoadedMemoryIsUsedIn( CheckOp, LoadingValue, RegisterMap, initstr );
					if( RegisterMap.find(initstr) != RegisterMap.end() )
					{
						return;
					}
				}
			}
		}
	}else
	{
		errs()<<"This Operand is not Instruction\n";
		return;
	}
}


void ParamGet::ParamGetCommon::insertForOMPVarAndArgument(Module &M, IRBuilder<> &Builder, Function* OutFunc,
		Loop* targetLoop)
{
	//alloca inst for loop argument var
	buildAllocaForOMPVar(Builder);	
	// //debug
	errs()<<"(currently we skip this debug)-- Show all element in OMPParamMap ---\n";
	// for(auto ompitr : OMPParamMap)
	// {
	// 	errs()<<ompitr.first<<": ";
	// 	ompitr.second->print(errs());errs()<<"\n";
	// }
	//alloca for first private
	if(!FirstPrivateValMap.empty())
	{
		errs()<<"---create alloca of firstprivate in local memory---\n";
		createLocalAllocaForAnyPrivateVariable(Builder,FirstPrivateValMap);
	}
	//alloca for last private var
	if(!LastPrivateValMap.empty())
	{
		errs()<<"---create alloca of lastprivate in local memory---\n";
		createLocalAllocaForAnyPrivateVariable(Builder,LastPrivateValMap);
	}
	//alloca for private var
	if(!PrivateValMap.empty())
	{
		errs()<<"---create alloca of lastprivate in local memory---\n";
		createLocalAllocaForAnyPrivateVariable(Builder,PrivateValMap);
		//----if want to generate optimized code, challenge below----//
			//check whether private is stored or not
			/*This is not smart
			To-Be: If it is scalar or pointer, and it was stored, create phi.
					Phi recieve first value of it's memory's Stored Val.
					And revieve updated value when predecessor is loop iteration.
			*/
			//create alloca for val stored in subloop of the targeted loop
			// for(auto loopitr = targetLoop->begin(); loopitr != targetLoop->end(); ++loopitr)
			// {
			// 	Loop* subLoop = *loopitr;	
			// 	for(auto lbbitr = subLoop->block_begin(); lbbitr != subLoop->block_end();++lbbitr)
			// 	{
			// 		BasicBlock* subB = *lbbitr;
			// 		for(auto institr = subB->begin(); institr != subB->end(); ++institr)
			// 		{
			// 			Instruction* subI = &*institr;
			// 			if(StoreInst* SI = dyn_cast<StoreInst>(subI))
			// 			{
			// 				if(SI->getOperand(1)->hasName())
			// 				{
			// 					string pristr = SI->getOperand(1)->getName().str();
			// 					if( PrivateValMap.find( pristr ) != PrivateValMap.end() &&
			// 						BuiltValMap.find( pristr ) == BuiltValMap.end() )
			// 					{
			// 						Type* ElemTy = SI->getOperand(0)->getType();
			// 						align = calcAlignOfType(ElemTy);
			// 						auto prialloca = Builder.CreateAlloca( ElemTy );
			// 						prialloca->setAlignment( align );
			// 						prialloca->setName( pristr );
			// 						buildval = dyn_cast<Value>(prialloca);

			// 						BuiltValMap.insert(make_pair(pristr,buildval));
			// 						errs()<<"inserted to BuiltValMap:\tname:"<<pristr<<": val\t";
			// 						buildval->print(errs()); errs()<<"\n";
			// 					}	
			// 				}
			// 			}
			// 		}
			// 	}
			// }//end of subLoop itr

			//create alloca for composite type
			// for(auto valitr = PrivateValMap.begin(); valitr != PrivateValMap.end(); ++valitr)
			// {
			// 	string str = valitr->first;
			// 	Value* priV = valitr->second;
			// 	Type* priT = priV->getType();
			// 	if( !(dyn_cast<PointerType>(priT)) )
			// 	{
			// 		continue;
			// 	}			
			// 	priT = priV->getType()->getPointerElementType();
			// 	switch(priT->getTypeID())
			// 	{
			// 		//errs()<<"CompositeType: "<<str<<"\n";
			// 		case Type::TypeID::StructTyID:
			// 		case Type::TypeID::ArrayTyID:
			// 		case Type::TypeID::VectorTyID:
			// 			if( AllocaInst* AI = dyn_cast<AllocaInst>(priV) )
			// 			{
			// 				align = AI->getAlignment();
			// 			}else if( GlobalVariable* GV = dyn_cast<GlobalVariable>(priV) )
			// 			{
			// 				align = GV->getAlignment();
			// 			}else //Argument is handled here
			// 			{
			// 				//set the biggest align of elements in Structure
			// 				//this may be wrong. 
			// 				//How to know correct align?
			// 				align = calcAlignOfType(priT);
			// 			}	
			// 			if(align)// allocate is only for pointers
			// 			{
			// 				// if(dyn_cast<Argument>(priV))
			// 				// {
			// 				// 	priT = priT->getPointerElementType();
			// 				// }
			// 				auto prialloca = Builder.CreateAlloca( priT );
			// 				prialloca->setAlignment( align );
			// 				prialloca->setName( str );
			// 				buildval = dyn_cast<Value>(prialloca);

			// 				BuiltValMap.insert(make_pair(str,buildval));
			// 				errs()<<"inserted to BuiltValMap:\tname:"<<str<<": val\t";
			// 				buildval->print(errs()); errs()<<"\n";
			// 			}
			// 			break;
			// 		default:
			// 			break;
			// 	}
			// }
		//-----------------------
	}

	//alloca for reduction var
	if(!ReductionValMap.empty())
	{
		errs()<<"---create alloca of reduction in local memory---\n";
		createLocalAllocaForReductionVariable(Builder);
	}


	errs()<<"---insert argument to BuiltValMap---\n";

	auto fargitr = OutFunc->arg_begin()+2;
	//auto alloitr = ArgPtr.begin()+2;//arg 0 is ident_t, arg 1 is number of FuncArgs
	for(auto valitr : FuncArgumentMap) 
	{
		//Value* buildval;
		Value* fargV = dyn_cast<Value>(&*fargitr);//func argument's value

		//Instruction* oldInst = argitr->second;

		//str is captured inst name
		string argName = valitr.first;
		if( ReductionValMap.find(argName) != ReductionValMap.end() )
		{
			argName = argName + "_Arg";
			//we use local Reduction Var for inner loop calc,
			//and use Arg Reduction Var for reduce_nowait
		}
		else if( FirstPrivateValMap.find(argName) != FirstPrivateValMap.end() )
		{
			if( !dyn_cast<PointerType>( FirstPrivateValMap.find(argName)->second->getType() ) &&
				dyn_cast<PointerType>( fargV->getType() ) &&
				LastPrivateValMap.find(argName) != LastPrivateValMap.end() )
			{
				switch( FirstPrivateValMap.find(argName)->second->getType()->getTypeID() )
				{	
					case Type::TypeID::StructTyID:			
					case Type::TypeID::ArrayTyID:
					case Type::TypeID::VectorTyID:
					case Type::TypeID::X86_FP80TyID:
					case Type::TypeID::FP128TyID:
					case Type::TypeID::PPC_FP128TyID:
						argName = argName + ".FirstPrivateArgument";
						break;
					default:
						argName = argName + ".LastPrivateArgument";	
						break;
				}
			}else
			{
				argName = argName + ".FirstPrivateArgument";
			}
		}else if( LastPrivateValMap.find(argName) != LastPrivateValMap.end() )
		{
			argName = argName + ".LastPrivateArgument";			
		}
		BuiltValMap.insert(make_pair(
			argName,
			fargV
		));//use the load of FuncArgs as operand

		errs()<<"inserted to BuiltValMap:\tname:"<<argName<<": val\t";
		BuiltValMap.find(argName)->second->print(errs());
		errs()<<"\n";
		if( !(argName[0] >= '0' && argName[0] <= '9') )
		{
			BuiltValMap.find(argName)->second->setName(argName);
		}
		//++alloitr;
		fargitr++;

		// //(debug)
		// errs()<<"name:\t"<<valitr->first<<"\n";
		// errs()<<"arg val:\t";
		// fargV->print(errs()); errs()<<"\n";
	}

	errs()<<"---retransformScalars---\n";
	retransformScalars(Builder);

	errs()<<"---load for ShareFirstVal---\n";
	createLoadForShareFirstVal(Builder);

	if(!ReductionValMap.empty())
	{
		//insert reference for ReductionRecordMap
		for(auto reditr = ReductionRecordMap.begin(); reditr != ReductionRecordMap.end(); ++reditr)
		{
			string str = reditr->first + "_Arg";
			if(BuiltValMap.find(str) == BuiltValMap.end())
			{
				string builtstr = reditr->second->getName().str();
				if( BuiltValMap.find(builtstr) != BuiltValMap.end() )
				{
					BuiltValMap.insert(make_pair(
						str,
						BuiltValMap.find(builtstr)->second
					));
				}else
				{
					errs()<<"[ParamGet] Reduction record map element does not have reference in new function\n";
				}
			}

		}
		errs()<<"---reduction initialize---\n";
		storeLocalInitialValueForReduction(Builder, targetLoop);
	}

	if(!FirstPrivateValMap.empty()) 
	{
		errs()<<"---firstprivate memcpy---\n";
		copyInitialValForFirstPrivate(Builder);
	}

	// errs()<<"---private pointer load---\n";
	// for(auto valitr = PrivateValMap.begin(); valitr != PrivateValMap.end(); ++valitr)
	// {
	// 	Value* OrigPriV = valitr->second;
	// 	if( dyn_cast<AllocaInst>(OrigPriV) || dyn_cast<GlobalVariable>(OrigPriV) )
	// 	{
	// 		continue;
	// 	}else if( dyn_cast<PointerType>(OrigPriV->getType()) )
	// 	{
	// 		unsigned align = 0;
	// 		Value* buildval;
	// 		align = calcAlignOfType( OrigPriV->getType() );
	// 		buildval = Builder.CreateAlignedLoad(
	// 			BuiltValMap.find(valitr->first)->second,
	// 			align
	// 		);
	// 		BuiltValMap.find()	
	// 	}
	// }

	errs()<<"---buildLoopUpperBound begin---\n";
	buildLoopUB(Builder,targetLoop);
	errs()<<"---buildLoopUpperBound end---\n";
	return;
}


void ParamGet::ParamGetCommon::createLocalAllocaForReductionVariable(IRBuilder<> &Builder)
{
	for(auto reditr : ReductionValMap)
	{
		Type* RedVarTy = reditr.second.first->getType();//This is RedVar before Create RedVar_Local
		if( RedVarTy->isPointerTy() )//this is not for phi, for Load
		{
			RedVarTy = dyn_cast<PointerType>(RedVarTy)->getElementType();
		}
		//(debug)
		// errs()<<"reduction variable: ";
		// reditr.second.first->print(errs()); errs()<<"\n";

		//set align
		unsigned align = 4; //RedVarTy->isIntegerTy(32)//this may miss the error of the types that we don't thought of
		if( RedVarTy->isIntegerTy(64) || RedVarTy->isPointerTy() || RedVarTy->isDoubleTy() )
		{
			align = 8;
		}//we don't handle PointerTy for now
		else if(RedVarTy->isIntegerTy(16))
		{
			align = 2;
		}

		//create AllocaInst
		AllocaInst* ValueAlloca = Builder.CreateAlloca( RedVarTy );
		ValueAlloca->setAlignment(align);
		Value* buildval = dyn_cast<Value>(ValueAlloca);

		//set name of local reduction variable
		string reductionName = reditr.first + "_Local";
		BuiltValMap.insert(make_pair(reductionName ,buildval));	
		//if the RedVar doesn't use a phi inst, use the original name
		if( ReductionPHIMap.find(reditr.first) == ReductionPHIMap.end() ){
			reductionName = reditr.first;
		}
		buildval->setName(reductionName);		

		//replace use of RedVar with RedVar_Local
		//To store the result of each thred calculation to the local memory
		if( reditr.second.first->getType()->isPointerTy() )//this is not phi
		{
			BuiltValMap.insert(make_pair(reditr.first ,buildval));	
		}

		//(debug)
		errs()<<"inserted to BuiltValMap:\tname:"<<reditr.first<<": val\t";
		buildval->print(errs()); errs()<<"\n";
	}

	//create .omp.reduction.red_list alloca
	auto red_listAlloca =
		Builder.CreateAlloca( ArrayType::get(Builder.getInt8Ty()->getPointerTo(), ReductionValMap.size()) );
	red_listAlloca->setAlignment(8);

	Value* buildval = dyn_cast<Value>(red_listAlloca);
	buildval->setName( ".omp.reduction.red_list" );
	BuiltValMap.insert(make_pair( ".omp.reduction.red_list" ,buildval));
}

void ParamGet::ParamGetCommon::createLocalAllocaForAnyPrivateVariable(IRBuilder<> &Builder, StrVmap& AnyPrivateMap)
{
	//create alloca for composite type
	//to use memcpy
	for(auto valitr : AnyPrivateMap)
	{
		string priName = valitr.first;
		Value* priV = valitr.second;
		//if firstprivate is scalar, we need not to create alloca in local memory
		if( !(dyn_cast<PointerType>(priV->getType())) )
		{
			continue;
		}			

		Type* priT = priV->getType()->getPointerElementType();
		unsigned align = 0;
		Value* buildval;
		if(	priT->getTypeID() == Type::TypeID::StructTyID ||
				priT->getTypeID() == Type::TypeID::ArrayTyID ||
				priT->getTypeID() == Type::TypeID::VectorTyID )
		{
			//set alignment of these structured type variable
			if( AllocaInst* AI = dyn_cast<AllocaInst>(priV) )
			{
				align = AI->getAlignment();
			}else if( GlobalVariable* GV = dyn_cast<GlobalVariable>(priV) )
			{
				align = GV->getAlignment();
			}else //Argument is handled here
			{
				//this may be wrong. 
				//set the biggest align of elements in Structure
				//How to know correct align?
				align = calcAlignOfType(priT);
			}	
			if(align)// allocate is only for pointers
			{
				//build alloca inst
				auto prialloca = Builder.CreateAlloca( priT );
				prialloca->setAlignment( align );
				prialloca->setName( priName );

				//record the built inst
				buildval = dyn_cast<Value>(prialloca);
				BuiltValMap.insert(make_pair(priName,buildval));
				errs()<<"inserted to BuiltValMap: "<<priName<<": val: ";
				buildval->print(errs()); errs()<<"\n";
			}else{
				errs()<<"[ParamGet] we can't determine align of this structured firstprivate variable: ";
				priV->print(errs());
			}
		}else{
			//if alloca or global, fargTy is alloca of priV, so alloca priElemTy
			if( dyn_cast<AllocaInst>(priV) || dyn_cast<GlobalVariable>(priV) )
			{
				Type* AllocatedType = priT;						
				errs()<<"we create new alloca for this value\n";
				unsigned align = calcAlignOfType(AllocatedType);
				AllocaInst* AI = Builder.CreateAlloca(AllocatedType);
				AI->setAlignment(align);//this pointer type is 64bits
				if(Value* buildval = dyn_cast<Value>(AI))
				{
					buildval->setName( priName );
					BuiltValMap.insert(make_pair(priName,buildval));

					errs()<<"inserted to BuiltValMap:\tname:"<< priName <<": val\t";
					buildval->print(errs());
					errs()<<"\n";
				}		
			}
		}
	}
}







Value* ParamGet::ParamGetCommon::getInitialValueForReductionOperand(
	IRBuilder<> &Builder, Loop* targetLoop, 
	Value* redV, ParamGetCommon::reduction_operator redop )
{	
	Value* InitialV;
	Type* RedVarTy = redV->getType();
	if( RedVarTy->isPointerTy() )//this is not for phi, for Load
	{
		RedVarTy = dyn_cast<PointerType>(RedVarTy)->getElementType();
	}
	
	switch( redop )
	{
		case reduction_operator::add:
		case reduction_operator::sub:
		case reduction_operator::or_bin:
		case reduction_operator::xor_bin:	
		case reduction_operator::or_or:
			if( RedVarTy->isFloatTy() || RedVarTy->isDoubleTy() )
			{
				InitialV = dyn_cast<Value>( ConstantFP::get(RedVarTy, (double)0) );
			}else if( RedVarTy->isVectorTy() )
			{
				ConstantAggregateZero* zeroinitializer = ConstantAggregateZero::get(RedVarTy);
				InitialV = dyn_cast<Value>( zeroinitializer );
				// errs()<<"zeroinitializer_Local: ";
				// zeroinitializer->print(errs()); errs()<<"\n";
				// errs()<<"RedVarTy_Local: ";
				// RedVarTy->print(errs()); errs()<<"\n";
			}else
			{
				//(debug)
				errs()<<"redV: ";
				redV->print(errs()); 
				errs()<<"\n";
				errs()<<"RedVarTy: ";
				RedVarTy->print(errs()); 
				errs()<<"\n";
				InitialV = Builder.getIntN(RedVarTy->getIntegerBitWidth(),0);
			}
			break;
		case reduction_operator::mul:
		case reduction_operator::and_and:
			if( RedVarTy->isFloatTy() || RedVarTy->isDoubleTy() )
			{
				InitialV = dyn_cast<Value>( ConstantFP::get(RedVarTy, (double)1) );

			}else
			{
				InitialV = Builder.getIntN(RedVarTy->getIntegerBitWidth(),1);
			}	
			break;
		case reduction_operator::and_bin:
			if( RedVarTy->isFloatTy() || RedVarTy->isDoubleTy() )
			{
				InitialV = ConstantFP::get(RedVarTy, (double)~0);
			}else
			{
				InitialV = Builder.getIntN(RedVarTy->getIntegerBitWidth(),~0);
			}	
			break;
		case reduction_operator::max:
			if(true)
			{
				CmpInst *RedCmp;
				Value* CmpV = getCmpInstForMaxMin(targetLoop, redV);	
				if((RedCmp = dyn_cast<CmpInst>(CmpV)))
				{
					//just want to assign RedCmp
				}else
				{
					errs()<<"RedCmp can not be calculated\n";
					redV->dump();
				}
				// if max, store least value		
				long long least = 0;//this is least if RedCmp is unsigned
				if(RedVarTy->isFloatTy() || RedVarTy->isDoubleTy())
				{
					//floating point type is always signed type
					double fleast;
					if(RedVarTy->isFloatTy())
					{
						//32 bit
						//least = FLT_MIN;
						fleast = -FLT_MAX;
					}else if(RedVarTy->isDoubleTy())
					{
						//64 bit
						//least = DBL_MIN;
						fleast = -DBL_MAX;
					}

					InitialV = ConstantFP::get(RedVarTy, fleast);
				}else
				{
					if( RedCmp->isSigned() )
					{	
						least = 1;
						least = (long long)(least<<( RedVarTy->getIntegerBitWidth()-1 ));
						// 0001 -> 1000 = -8
						// errs()<<"### bitwidth ->" << RedVarTy->getIntegerBitWidth() <<"\n";
						// errs()<<"Type->";
						// RedVarTy->print(errs()); errs()<<"\n";
						// errs()<<"######## least->"<<least<<"\n";
					}
					// errs()<<"######## least->"<<least<<"\n";
					InitialV = Builder.getIntN(RedVarTy->getIntegerBitWidth(), least);
				}
			}
			break;
		case reduction_operator::min:
			if(true)
			{
				CmpInst *RedCmp;
				Value* CmpV = getCmpInstForMaxMin(targetLoop, redV);
				if((RedCmp = dyn_cast<CmpInst>(CmpV))){
					//just want to assign RedCmp
				}else{
					errs()<<"RedCmp is not calculated\n";
					redV->dump();
				}
				//if min, store largest value				
				long long largest = -1;//this is largest if RedCmp is unsigned
				if(RedVarTy->isFloatTy() || RedVarTy->isDoubleTy())
				{
					double flargest;
					//floating point type is always signed type
					if(RedVarTy->isFloatTy())
					{
						//32 bit
						//largest = FLT_MAX
						flargest = FLT_MAX;
					}else if(RedVarTy->isDoubleTy())
					{
						//64 bit
						//largest = DBL_MAX;
						flargest = DBL_MAX;
					}

					InitialV = ConstantFP::get(RedVarTy, flargest);
				}else
				{
					if( RedCmp->isSigned() )
					{
						largest = 1;
						largest = (long long)0 ^ (largest<<( RedVarTy->getIntegerBitWidth()-1 ));
						// 0001 -> 1000 
						// 0000 ^ 1000 = 0111 = 7
					}
					InitialV = Builder.getIntN(RedVarTy->getIntegerBitWidth(), largest);						
				}	
			}
			break;
		default:
			break;
	}
	return InitialV;
}

void ParamGet::ParamGetCommon::storeLocalInitialValueForReduction(IRBuilder<> &Builder, Loop* targetLoop)
{
	for(auto reditr : ReductionValMap)
	{
		Type* RedVarTy = reditr.second.first->getType();//This is RedVar before Create RedVar_Local
		//this is not for phi, for Load
		if( RedVarTy->isPointerTy() ){
			RedVarTy = dyn_cast<PointerType>(RedVarTy)->getElementType();
		}
		//set align
		unsigned align = 4;
		if( RedVarTy->isIntegerTy(64) || RedVarTy->isPointerTy() || RedVarTy->isDoubleTy() ){
			align = 8;
		}//we don't handle PointerTy for now
		else if(RedVarTy->isIntegerTy(16)){
			align = 2;
		}

		string reductionName = reditr.first;
		Value* DstVal = BuiltValMap.find( reductionName + "_Local" )->second;
		Value* initialV = 	getInitialValueForReductionOperand(
			Builder, targetLoop, reditr.second.first, reditr.second.second
		);

		//replace phi's incoming value (if RedVar uses phi)
		if( ReductionPHIMap.find( reductionName ) != ReductionPHIMap.end() )
		{
			Value* phiV = reditr.second.first;
			//underneath is wrong: ReductionPHIMap records original initial value 
			//ReductionPHIMap.find( reductionName )->second;
			errs()<< reductionName << ": found in ReductionPHIMap\n";
			phiV->print(errs());
			errs()<<"\n";

			if( PHINode* PHIN = dyn_cast<PHINode>(phiV) )
			{
				//we replace the original phi's incoming Value
				//this may cause critical error				
				errs()<< reductionName << ": is PHI\n";
				unsigned incomings = PHIN->getNumIncomingValues();
				for(unsigned i=0; i<incomings; i++)
				{
					if( !(targetLoop->contains( PHIN->getIncomingBlock(i) )) )
					{
						errs()<<"*** modifing original PHI inst ***\n";
						PHIN->print(errs()); errs()<<"\n: after :\n";
						PHIN->setIncomingValue(i, initialV);
						PHIN->print(errs());
						errs()<<"\n******\n";
					}
				}
			}
		}else //Store/Load reduction for GEP elem
		{
			errs()<< reductionName << ": NOT found in ReductionPHIMap\n";
			//create StoreInst for RedVar_Local and replace phi's incoming value (if RedVar uses phi)
			//create StoreInst for RedVar_Local
			Builder.CreateAlignedStore(
				initialV,
				DstVal,
				align
			);
		}
	}//end of for reditr
}


void ParamGet::ParamGetCommon::copyInitialValForFirstPrivate(IRBuilder<> &Builder)
{
	for(auto valitr = FirstPrivateValMap.begin(); valitr != FirstPrivateValMap.end(); ++valitr)
	{
		Value* OrigPriV = valitr->second;
		Type* OrigPriTy = OrigPriV->getType();
		unsigned align = 0;
		string str = valitr->first;
		string argstr = str+".FirstPrivateArgument";

		//if first private is scalar, use casted value
		if( !dyn_cast<PointerType>(OrigPriV->getType()) )
		{
			//If scalar value, use ".FirstPrivateArgument" as original val.
			//cast i64 to val modify BuiltValMap.(".FirstPrivateArgument")->second, so we can reference it.
			Value* buildval = BuiltValMap.find( argstr )->second;
			BuiltValMap.insert(make_pair(str,buildval));
			//(debug)
			errs()<<"BuiltValMap:\tname:"<<str<<": val\t";
			BuiltValMap.find(str)->second->print(errs()); errs()<<"\n";		
			continue;			
		}

		OrigPriTy = OrigPriV->getType()->getPointerElementType();
		switch(OrigPriTy->getTypeID())
		{

			//if composite type, create memcpy
			case Type::TypeID::StructTyID:
			case Type::TypeID::ArrayTyID:
			case Type::TypeID::VectorTyID:
				//align get
				if( AllocaInst* AI = dyn_cast<AllocaInst>(OrigPriV) )
				{
					align = AI->getAlignment();
				}else if( GlobalVariable* GV = dyn_cast<GlobalVariable>(OrigPriV) )
				{
					align = GV->getAlignment();
				}else //Argument is handled here
				{
					//set the biggest align of elements in Structure
					//this may be wrong. 
					//How to know correct align?
					align = calcAlignOfType(OrigPriTy);
				}
				if(align)// allocate is only for pointers
				{
					//size calculate
					errs()<<"showing "<< argstr <<"\n";
					unsigned int TypeSize = calcSizeOfType( OrigPriTy );

					if( (OrigPriTy->getTypeID() == Type::TypeID::StructTyID) && (TypeSize % align != 0))
					{
						//set Size to match alignment
						TypeSize += align - (TypeSize % align);
					}						
					//(debug)
					// errs()<<"Type:\t";
					// priTy->print(errs()); errs()<<"\n";
					// errs()<<"TypeSize: "<<TypeSize<<"\n";
					// errs()<<"str->val: ";
					// BuiltValMap.find( str )->second->print(errs()); errs()<<"\n";
					// errs()<<"str.arg->val: ";
					// BuiltValMap.find( argstr )->second->print(errs()); errs()<<"\n";


					// Value* LoadVal = dyn_cast<Value>(Builder.CreateAlignedLoad(
					// 	BuiltValMap.find( argstr )->second,
					// 	calcAlignOfType( BuiltValMap.find( argstr )->second->getType() )
					// ));

					errs()<<"str "<<str<<"\n";
					Builder.CreateMemCpy(
						/*Value *Dst*/BuiltValMap.find(str)->second,
						/*unsigned DstAlign*/align,
						/*Value *Src*/BuiltValMap.find( argstr )->second,
						/*unsigned SrcAlign*/align,
						/*uing64_t Size*/TypeSize
					);
				}
				break;
			// case Type::TypeID::X86_FP80TyID:
			// case Type::TypeID::FP128TyID:
			// case Type::TypeID::PPC_FP128TyID:
			// 		break;
			// case Type::TypeID::PointerTyID:
			// 	//pointer to the pointer is passed with alloca containing the pointer.
			// 	//so we load from the argument.
			// 	if( true )
			// 	{
			// 		Value* buildval;
			// 		align = calcAlignOfType( valitr->second->getType() );
			// 		buildval = dyn_cast<Value>(Builder.CreateAlignedLoad( 
			// 			BuiltValMap.find( argstr )->second,
			// 			align
			// 		));
			// 		buildval->setName( str );						
			// 		BuiltValMap.insert(make_pair(str,buildval));
			// 		errs()<<"BuiltValMap:\tname:"<<str<<": val\t";
			// 		BuiltValMap.find(str)->second->print(errs()); errs()<<"\n";
			// 	}
			// 	break;
			default:
				//In this process, the value that will be handled with phi is handled.
				//the loaded value is used as phi's first value.
				//also the Argument which is not pointer of composite or scalar is handled here
				//we load and use it.	

				//firstprivate that is not composite nor scalar nor farg is alloca nor global
				//it's pointer defined in orginal function

				if( dyn_cast<AllocaInst>( BuiltValMap.find(str)->second ) )
				{
					//DstVal is alloca created for outlined func
					Value* DstVal = BuiltValMap.find(str)->second;

					//(debug)
					errs()<<"BuiltValMap:\tname:"<<str<<": val\t";
					BuiltValMap.find(str)->second->print(errs()); errs()<<"\n";

					Value* LoadVal = dyn_cast<Value>(Builder.CreateAlignedLoad(
						BuiltValMap.find(argstr)->second,//this value's corresponding argument of outlined func 
						align
					));

					//(debug)
					errs()<<"LoadVal: ";
					LoadVal->print(errs()); errs()<<"\n";
	
					Builder.CreateAlignedStore(
						LoadVal,
						DstVal,
						align
					);
				}else
				{
					Value* buildval;
					align = calcAlignOfType( valitr->second->getType() );
					errs()<<"str:"<<str<<"\n";

					//1.load from BuiltValMap( name + ".FirstPrivateArgument" )
					Value* loadVal;
					if( BuiltValMap.find(argstr) != BuiltValMap.end() )
					{	
						loadVal = BuiltValMap.find( argstr )->second;
					}else if( BuiltValMap.find(str+".LastPrivateArgument") != BuiltValMap.end() )
					{
						loadVal = BuiltValMap.find(str+".LastPrivateArgument")->second;
					}else
					{
						errs()<<"value is not found\n";
						OrigPriV->dump();
					}
					buildval = dyn_cast<Value>(Builder.CreateAlignedLoad( 
						loadVal,
						align
					));
					//2.Use 1. as initial value of ReplacementMap
					buildval->setName( str );
					BuiltValMap.insert(make_pair(str,buildval));
					//if alloca is already created, alloca is referenced,
					//else scalar is referenced.

					errs()<<"BuiltValMap:\tname:"<<str<<": val\t";
					BuiltValMap.find(str)->second->print(errs()); errs()<<"\n";

					//create reference for phi
					//BuiltValMap.insert(make_pair(str+".phi",buildval));
				}
				break;
		}//end of switch


		// //Eliminate Scalar (this process is only for Argument's Scalar Variable)
		// if( !(dyn_cast<PointerType>(priTy)) )
		// {
		// 	continue;
		// }

		// priTy = priV->getType()->getPointerElementType();		

	}//end of for
}

void ParamGet::ParamGetCommon::buildAllocaForOMPVar(IRBuilder<> Builder)
{
	/* OMPParamMap will be updated:
	str 
		"loopvar"	|
		"Init_built"	|	
		"Cond_built"	|
		"Incr_built"	|
		"UB_calc"		|
		"loopvar_copy"	|
		"plower"		|
		"pupper"		|
		"pstride"		|
		"plastiter"	|

	*/
	// //---alloca builder------------------//
	string str;
	AllocaInst* alloca;
	unsigned align;
	if( this->CondOperandIntegerSize < 32 ){
		align = 2;
	}else if(this->CondOperandIntegerSize < 64){
		align = 4;	
	}else{
		align = 8;
	}


	alloca = Builder.CreateAlloca( Builder.getIntNTy(CondOperandIntegerSize) );
	alloca->setAlignment(align);
	str = "plower";
	alloca->setName(str);
	OMPParamMap.insert(make_pair(str,dyn_cast<Value>(alloca)));

	//Upper Bound (for branch)
	alloca = Builder.CreateAlloca( Builder.getIntNTy(CondOperandIntegerSize) );
	alloca->setAlignment(align);
	str = "pupper";
	alloca->setName(str);
	OMPParamMap.insert(make_pair(str,dyn_cast<Value>(alloca)));

	//Pointer to the stride
	alloca = Builder.CreateAlloca( Builder.getIntNTy(CondOperandIntegerSize) );
	alloca->setAlignment(align);
	str = "pstride";
	alloca->setName(str);
	OMPParamMap.insert(make_pair(str,dyn_cast<Value>(alloca)));

	//pointer to the "last iteration" flag
	alloca = Builder.CreateAlloca( Builder.getInt32Ty() );
	alloca->setAlignment(4);
	str = "plastiter";
	alloca->setName(str);
	OMPParamMap.insert(make_pair(str,dyn_cast<Value>(alloca)));
	//---alloca builder end------------------//

}


void ParamGet::ParamGetCommon::insertBounds(IRBuilder<> &Builder, Value* InitLoad,
	Value* CondLoad)
{
	if(CmpInst* CI = dyn_cast<CmpInst>(LoopExpr.find("cond")->second))
	{
		//check whether cond uses init after building incr
		//--analyze whether cond uses init after incr--
		string CondStr = CI->getName().str();
		string IncrStr = dyn_cast<Value>(LoopExpr.find("incr")->second)->getName().str();
		bool CondUsesInitAfterIncr = false;	

		// errs()<<"CondStr: "<< CondStr <<"\nIncrStr: "<< IncrStr <<"\n";

		if( dyn_cast<PHINode>(LoopExpr.find("init")->second) )	
		{
			unsigned operands = CI->getNumOperands();
			for(unsigned i=0; i<operands; i++)
			{
				Value* OpeVal = CI->getOperand(i);
				if( OpeVal->hasName() && 
					OpeVal->getName().str() == LoopExpr.find("init")->second->getName().str() )
				{
					for(auto institr = CapturedInstVector.begin(); 
						institr != CapturedInstVector.end(); ++institr)
					{
						Instruction* CheckedI = institr->second;
						// errs()<<"CheckedI: ";
						// CheckedI->print(errs()); errs()<<"\n";
						if(CheckedI->hasName())
						{
							if( CheckedI->getName().str() == CondStr )
							{
								break;
							}else if( CheckedI->getName().str() == IncrStr )
							{
								CondUsesInitAfterIncr = true;
								break;
							}
						}
					}
					break;
				}
			}	

		}
		//---------------------------------//
		//we believe that Incr and Cond is recorded in order of the program process
		//-------
		//Restrictly, we should check cond's predecessir BB name
		//and if reach the entry BB, CondUsesInitAfterIncr = false
		//else if reach the incr, CondUsesInitAfterIncr = true
		//But it is so confusing. So I believe in Loop.begin begins in order that
		//instructions are processed.
		//-------


		Value* UBCalc;
		Value* LBCalc;
		//determine the UB and LB
		bool InitIsBiggerFlag;
		bool IncludeEqualFlag;
		judgeInitIsBigger(Builder, InitIsBiggerFlag, IncludeEqualFlag);
		if( CondLoad->getType()->isPointerTy() )// for the loop whose counter is pointer type
		{
			Value* CondAddr = Builder.CreatePtrToInt(
				/*Value* V*/CondLoad,
				/*Type* DestTy*/Builder.getIntNTy(CondOperandIntegerSize)
				);
			Value* InitAddr = Builder.CreatePtrToInt(
				InitLoad,
				Builder.getIntNTy(CondOperandIntegerSize)
				);

			//Calc Sub of UB
			if(InitIsBiggerFlag)
			{
				UBCalc = Builder.CreateSub(InitAddr,CondAddr);
				OMPParamMap.insert(make_pair("Ptr_init",CondLoad));
			}else
			{
				UBCalc = Builder.CreateSub(CondAddr,InitAddr);
				OMPParamMap.insert(make_pair("Ptr_init",InitLoad));
			}
			errs()<<"Ptr_init: ";
			OMPParamMap.find("Ptr_init")->second->print(errs());
			errs()<<"\n";

			if( PointerType* PT = dyn_cast<PointerType>(CondLoad->getType()) )
			{
				if( PT->getElementType()->isIntegerTy(32) )
				{
					UBCalc = Builder.CreateAShr(
						UBCalc,
						Builder.getIntN(CondOperandIntegerSize,2)
						);
				}else if( PT->getElementType()->isIntegerTy(64) )
				{
					UBCalc = Builder.CreateAShr(
						UBCalc,
						Builder.getIntN(CondOperandIntegerSize,3)
						);
				}else
				{
					errs()<<"this pointer type is not what we can handle\n";
				}
			}
			//(debug)
			errs()<<"Ptr UB_calc: ";
			UBCalc->print(errs());
			errs()<<"\n";
		}else
		{
			//not used for canonicalization
			// if(CondUsesInitAfterIncr == false)
			// {
			// 	//bigger side is upper bounds
			// 	//we don't modify InitLoad
			// 	//we add 1 to CondLoad					
			// 	if( (IncludeEqualFlag == false && CondTrueIsIterate == true) ||
			// 		(IncludeEqualFlag == true && CondTrueIsIterate == false) )
			// 	{
			// 		if(InitIsBiggerFlag)
			// 		{
			// 			CondLoad = Builder.CreateAdd( 
			// 				CondLoad, 
			// 				dyn_cast<Value>( Builder.getIntN(CondOperandIntegerSize,1) ) 
			// 			);
			// 		}else
			// 		{
			// 			//bigger side is upper bounds
			// 			CondLoad = Builder.CreateAdd( 
			// 				CondLoad, 
			// 				dyn_cast<Value>( Builder.getIntN(CondOperandIntegerSize,-1) ) 
			// 			);						
			// 		}			
			// 	}	
			// }			
			if(InitIsBiggerFlag)
			{
				UBCalc = InitLoad;
				LBCalc = CondLoad;
			}else
			{
				UBCalc = CondLoad;
				LBCalc = InitLoad;
			}

			if(CondUsesInitAfterIncr == false)
			{
				//bigger side is upper bounds
				//we don't modify InitLoad
				//we add 1 to CondLoad					
				if( (IncludeEqualFlag == false && CondTrueIsIterate == true) ||
					(IncludeEqualFlag == true && CondTrueIsIterate == false) ||
					(CI->getPredicate() == CmpInst::Predicate::ICMP_EQ) ||
					(CI->getPredicate() == CmpInst::Predicate::ICMP_NE) )
				{
					UBCalc = Builder.CreateAdd(
						UBCalc,
						Builder.getIntN( UBCalc->getType()->getIntegerBitWidth(), -1)
						);	
				}
			}
			//canonicalization
			UBCalc = Builder.CreateSub(
				UBCalc,
				LBCalc
				);
			UBCalc = Builder.CreateSDiv(
				UBCalc,
				OMPParamMap.find("Incr_built")->second
				);
			//(debug)
			errs()<<"UB_calc: ";
			UBCalc->print(errs());
			errs()<<"\n";
			errs()<<"LB_calc: ";
			LBCalc->print(errs());
			errs()<<"\n";
		}

		LBCalc = Builder.getIntN( UBCalc->getType()->getIntegerBitWidth(), 0);
		OMPParamMap.insert(make_pair("UB_calc",UBCalc));
		OMPParamMap.insert(make_pair("LB_calc",LBCalc));



		OMPParamMap.insert(make_pair("UB_canonical",UBCalc));
		OMPParamMap.insert(make_pair("LB_canonical",LBCalc));

		//(debug)
		errs()<<"UB_canonical: ";
		UBCalc->print(errs());
		errs()<<"\n";
		errs()<<"LB_canonical: ";
		LBCalc->print(errs());
		errs()<<"\n";


	}else
	{
		errs()<<"[ParamGet] cond is not Inst.\n";
		errs()<<"\tWe can't determine UB and LB when cond is not Instruction\n";
	}




}


//######################################################################################################################################################################


void ParamGet::ParamGetCommon::createAllocaForShareAfterLoop(IRBuilder<> &Builder, Loop* targetLoop)
{
	//if I is used after loop, we should return the value of it.
	//1.check if I is used after loop
	//2.create alloca for it
	//3.load and use it as original

	//in end of loop
	//1.create store to alloca
	BasicBlock* LoopPreheaderBB = CapturedInstVector.begin()->second->getParent();
	Function* OriginalFunc = LoopPreheaderBB->getParent();
	BasicBlock* FirstBB = &*(OriginalFunc->begin());
	Instruction* FirstInst = &*(FirstBB->begin());	


	//we share the result of reduction after loop
	//if phi's operand of inner loop is used, we map that value with reduction_Arg//FuncArgumentMap
	//if load's stored value is used, we map that with reduction memory //ReductionRecordMap
	for(auto redpair : ReductionValMap){
		Value* redV = redpair.second.first; 
		//get reduction shared memory value
		string redName = redpair.first;
		Value* redShareMemory = nullptr;
		if( ReductionRecordMap.find(redName) != ReductionRecordMap.end() ){
			redShareMemory = ReductionRecordMap.find(redName)->second;
		}else if( FuncArgumentMap.find( redName ) != FuncArgumentMap.end() ){
			redShareMemory = FuncArgumentMap.find(redName)->second;
		}
		//debug
		else{
			errs()<<"[ParamGet] we can't find alloca of this reduction: ";
			redV->print(errs());
			errs()<<"\n";
			exit(1);
		}

		//get reduction's result value name
		Value* resultV = nullptr;
		if(PHINode* PHIN = dyn_cast<PHINode>(redV)){
			unsigned incomings = PHIN->getNumIncomingValues();
			for(unsigned i=0; i<incomings ; i++){
				BasicBlock* incomingBB = PHIN->getIncomingBlock(i);
				if(targetLoop->contains(incomingBB)){
					resultV = PHIN->getIncomingValue(i);
					//get name of resultV
					if(!resultV->hasName()){
						resultV->setName("reduction.used.after.loop");
					}
					string resultName = resultV->getName();
					ReductionResultMap.insert(make_pair(resultName, redShareMemory));
				}
			}
		}else{
			for(Value* redUser : redV->users()){
				if(StoreInst* SI = dyn_cast<StoreInst>(redUser)){
					if(targetLoop->contains(SI)){
						resultV = SI->getOperand(0);
						//get name of resultV
						if(!resultV->hasName()){
							resultV->setName("reduction.used.after.loop");
						}
						string resultName = resultV->getName();
						ReductionResultMap.insert(make_pair(resultName, redShareMemory));
					}
				}
			}
		}
	}


	for(auto instpair : CapturedInstVector)
	{	
		Instruction* I = instpair.second;
		string capturedName = I->getName().str();
		
		//exception handle
		if( ReductionValMap.find( capturedName ) != ReductionValMap.end() ||
				LastPrivateValMap.find( capturedName ) != LastPrivateValMap.end() 
		){
			continue;
		}

		//check whether the user of I is out of loop
		for(Value* IUser : I->users()){
			//check if used after loop
			//I is defined in loop, so the use which is outside of loop appeares after the loop
			if(Instruction* userI = dyn_cast<Instruction>(IUser)){
				if( !targetLoop->contains(userI) )
				{
					Value* allocaV = nullptr;
					//if it is reduction's result
					if(I->hasName() && ReductionResultMap.find(I->getName().str()) != ReductionResultMap.end()){
						allocaV = ReductionResultMap.find(I->getName().str())->second;
						//record AllocaInst
						ShareAfterLoopMap.insert(make_pair(capturedName,allocaV));
						//debug
						errs()<<"Alloca to share a value after loop: "<<capturedName<<": ";
						allocaV->print(errs());
						errs()<<"\n";
					}else{
						//CHANGE: we don't make load for values used after loop except for reduction resutls.
						//it can make this pass inconvinient, but can reduce the use of un-important value
						//--
						// string allocaName = capturedName+".alloca";
						// unsigned align = calcAlignOfType(I->getType());
						// //create alloca for it
						// Builder.SetInsertPoint( FirstInst );
						// AllocaInst* AI = Builder.CreateAlloca( I->getType() );
						// AI->setAlignment( align );
						// allocaV = dyn_cast<Value>(AI);
						// //register alloca to FuncArgument
						// FuncArgumentMap.insert(make_pair(allocaName,allocaV));
						// //but we do not erase I from FuncArgumentMap
						// //may be this cause speed down
						userI->replaceUsesOfWith(I,UndefValue::get(I->getType()));
					}
				}
			}
		} 
	}

}
void ParamGet::ParamGetCommon::createLoadForShareAfterLoop(IRBuilder<> &Builder, Loop* targetLoop)
{
	//in end of loop
	//1.create store to alloca
	//--


	Value* LoadVal;
	Value* AllocaVal;//This AllocaVal hold Alloca in Original Func
	unsigned align;
	BasicBlock* LoopPreheaderBB = CapturedInstVector.begin()->second->getParent();	
	for(auto instpair : CapturedInstVector){	
		Instruction* I = instpair.second;
		bool LoadFlag = false;
		//set LoadFlag
		for(Value* IUser : I->users()){
			if(Instruction* userI = dyn_cast<Instruction>(IUser)){
				//check if the I is used after loop. I is defined in loop, so the use which is outside of loop appeares after the loop
				if(!targetLoop->contains(userI)){
					LoadFlag = true;
					//(debug)
					errs()<<"Load after parallel target userI: ";
					userI->print(errs());
					errs()<<"\n";
					//if userI is phi, and predecessor BB is in loop, replace the BB
					if( PHINode* PHIN = dyn_cast<PHINode>(userI) ){
						for(unsigned i=0; i<PHIN->getNumIncomingValues(); i++){
							if(	PHIN->getIncomingValue(i)->hasName() &&
									PHIN->getIncomingValue(i)->getName().str() == I->getName().str()
							){
								if(targetLoop->contains(PHIN->getIncomingBlock(i))){
									PHIN->setIncomingBlock(i, LoopPreheaderBB);
									//debug
									errs()<<"Share after loop is phi inst, its incoming BB is replaced: ";
									userI->print(errs()); errs()<<"\n";
								}
							}
						}
					}
				}
			}
		}
		if(LoadFlag)
		{
			string shareName = I->getName().str();
			//load and use it
			if( FuncArgumentMap.find( shareName ) != FuncArgumentMap.end() )
			{
				AllocaVal = FuncArgumentMap.find( shareName )->second;
				errs()<<"alloca found as func argument: ";
			}else if( ReductionRecordMap.find(shareName) != ReductionRecordMap.end() )
			{
				AllocaVal = ReductionRecordMap.find(shareName)->second;
				errs()<<"alloca found as reduction record: ";
			}else if( ShareAfterLoopMap.find(shareName) != ShareAfterLoopMap.end() )
			{
				AllocaVal = ShareAfterLoopMap.find( shareName )->second;	
				errs()<<"alloca found as share after loop: ";
			}else if( LastPrivateValMap.find(shareName) != LastPrivateValMap.end() )
			{
				AllocaVal = FuncArgumentMap.find( shareName )->second;
				//(debug)
				errs()<<"lastprivate AllocaVal is: ";
				AllocaVal->print(errs()); errs()<<"\n";
				if( !dyn_cast<AllocaInst>(AllocaVal) && !dyn_cast<GlobalVariable>(AllocaVal) )
				{
					errs()<<"it is alloca or global, we skip load\n";
					//alloca and global is stored in outlined func
					continue;
				}else if( dyn_cast<PointerType>(AllocaVal->getType()) )
				{
					bool continueflag = false;
					switch( AllocaVal->getType()->getPointerElementType()->getTypeID() )
					{						
						case Type::TypeID::StructTyID:
						case Type::TypeID::ArrayTyID:
						case Type::TypeID::VectorTyID:
							continueflag = true;
							errs()<<"it is class, structure or vector. so we skip load\n";
							//class, structure, and vector is copied with memcpy in outlined func
							break;
						default:
							break;
					}
					if(continueflag)
					{
						continue;
					}
				}
			}else
			{
				errs()<<"we didn't record I's alloca\n";
				I->dump();
			}
			AllocaVal->print(errs());
			errs()<<"\n";

			align = calcAlignOfType(I->getType());
			Builder.SetInsertPoint(LoopPreheaderBB->getTerminator());
			LoadVal = dyn_cast<Value>(Builder.CreateAlignedLoad(
				AllocaVal,
				align
			));
			I->replaceAllUsesWith(LoadVal);
		}
	}
	Builder.SetInsertPoint(LoopPreheaderBB->getTerminator());
}

void ParamGet::ParamGetCommon::createAllocaForShareInLoop(IRBuilder<> &Builder, Loop* L)
{
	Instruction &firstI = CapturedInstVector.begin()->second->getParent()->getParent()->front().front();
	//alloca for vector type
	for(auto mappair : FuncArgumentMap){
		//check whether vector type or not
		string argName = mappair.first;
		Value* argV = mappair.second;
		Type* checkTy = argV->getType();
		if( checkTy->isVectorTy() || checkTy->isArrayTy() || checkTy->isStructTy() )
		{
			Builder.SetInsertPoint( &firstI );
			buildAllocaForShareInLoop(Builder,argName,argV);
		}
		// else if(checkTy->isPointerTy())
		// {
		// 	for(auto users : argV->users()){
		// 		if(GetElementPtrInst* userGEP = dyn_cast<GetElementPtrInst>(users)){
		// 			if(L->contains(userGEP)){
		// 				if(LoadInst* LI = dyn_cast<LoadInst>(argV))
		// 				{
		// 					//register pointer of this argument to FuncArgument
		// 					FuncArgumentMap.find( argName )->second = LI->getOperand(0);
		// 					//record AllocaInst to load in the task of threads
		// 					ShareFirstValMap.insert(make_pair(argName, LI->getOperand(0)));
		// 				}else{
		// 					Builder.SetInsertPoint( &firstI );
		// 					buildAllocaForShareInLoop(Builder,argName,argV);
		// 				}
		// 				break;
		// 			}
		// 		}
		// 	}//end of users
		// }
	}//end of mappair
}

void ParamGet::ParamGetCommon::buildAllocaForShareInLoop(IRBuilder<> &Builder, string argName, Value* argV)
{
	Type* checkTy = argV->getType();
	//if vector type, create alloca for it
	unsigned align = calcAlignOfType(checkTy);
	AllocaInst* AI = Builder.CreateAlloca( checkTy );
	AI->setAlignment( align );

	//register alloca to FuncArgument
	FuncArgumentMap.find( argName )->second = AI;
	//record AllocaInst to load in the task of threads
	ShareFirstValMap.insert(make_pair(argName, AI));

	//store the specified value to the created alloca (to pass the pointer of the original variable)
	Builder.SetInsertPoint( dyn_cast<Instruction>(this->targetII) );
	Builder.CreateAlignedStore(
		argV,
		AI,
		align
	);
	//load is created in insertForOMPVarAndArgument
}


void ParamGet::ParamGetCommon::createLoadForShareFirstVal(IRBuilder<> &Builder)
{
	for(auto valitr = ShareFirstValMap.begin(); valitr != ShareFirstValMap.end(); ++valitr)
	{
		unsigned align;
		//calc align // this is temporary implement, please make this process better
		if( PointerType* PT = dyn_cast<PointerType>( valitr->second->getType() ) )
		{
			align = calcAlignOfType( PT->getElementType() );
		}
		//create load
		Value* builtval = dyn_cast<Value>(Builder.CreateAlignedLoad(
			BuiltValMap.find(valitr->first)->second,
			align
		));
		//insert map
		BuiltValMap.find(valitr->first)->second = builtval;

	}	
}



void ParamGet::ParamGetCommon::createStoreForShareAfterLoop(IRBuilder<> &Builder)
{
	for(auto sharepair : ShareAfterLoopMap){
		string shareName = sharepair.first;
		if( ReductionResultMap.find(shareName) != ReductionResultMap.end() ){
			continue;
		}
		Value* AllocaVal = BuiltValMap.find( sharepair.first+".alloca" )->second;
		Value* SrcVal = BuiltValMap.find( sharepair.first )->second;
		//(debug)
		errs()<<"Shared AI: ";
		AllocaVal->print(errs()); errs()<<"\n";
		errs()<<"Shared Val: ";
		SrcVal->print(errs()); errs()<<"\n";

		//store is created at the last of the basic block 
		//which contains the instruction that defines the shared variable defined
		Builder.SetInsertPoint( dyn_cast<Instruction>(SrcVal)->getParent()->getTerminator() );
		Builder.CreateAlignedStore(
			SrcVal,
			AllocaVal,
			calcAlignOfType( SrcVal->getType() )			
		);
		//the difference of using lastprivate is
		//1.lastprivate reflect only the result of last iteration,
		// while sharing reflect the result of all iteration
		// (so, shared value must not have data dependency)
	}
}


void ParamGet::ParamGetCommon::createIdentTGlobalVar(Module &M, IRBuilder<> &Builder,
	 StructType* IdentT)
{
	/*
	FIXME:for now, we can handle ONLY a parallel loop with no barrier.
	we had better calculate global var's ident_t value.
	*/
	// unsigned int glovarnum = 0;
	/*//---str---//
	String describing the source location.
	The string is composed of semi-colon separated fields
	which describe the source file, the function and a pair
	of line numbers that delimit the construct
	*/		
	//---ident_t global (global constant)---//
	StringRef StrRef(";unknown;unknown;0;0;;\00");
	Constant* psource = dyn_cast<Constant>(
			Builder.CreateGlobalStringPtr(StrRef,"str")
		);

	OMPParamMap.insert(
		make_pair(
			IntToName(514),
			createLocationGlobalVar(M,Builder,IdentT,0,514,0,0,psource)
		)
	);

	OMPParamMap.insert(
		make_pair(
			IntToName(2),
			createLocationGlobalVar(M,Builder,IdentT,0,2,0,0,psource)
		)
	);	

	OMPParamMap.insert(
		make_pair(
			IntToName(18),
			createLocationGlobalVar(M,Builder,IdentT,0,18,0,0,psource)
		)
	);



	//create lck
	ConstantAggregateZero* initval = ConstantAggregateZero::get( ArrayType::get(Builder.getInt32Ty(),8) );
	GlobalVariable* IdentVar = new GlobalVariable(
		/*Module &M*/ M,
		/*Type* Ty*/ ArrayType::get( Builder.getInt32Ty(), 8 ),
		/*bool constant*/ false,
		/*LinkageTypes Link*/ GlobalValue::LinkageTypes::CommonLinkage,
				//http://llvm.org/docs/doxygen/classllvm_1_1GlobalValue.html#aedfa75f0c85c4aa85b257f066fbea57c
		/*Constant* Initializer*/ initval,
		/*const Twine &Name=""*/".gomp_critical_user_.reduction.var"
		);
	OMPParamMap.insert(make_pair(
		"lck",
		IdentVar
	));
}


GlobalVariable* ParamGet::ParamGetCommon::createLocationGlobalVar(Module &M, IRBuilder<> &Builder,
					StructType* IdentT,	int reserved_1, int flags,
					int reserved_2, int reserved_3, Constant* psource)
{		
	vector<Constant*> Body;
	Body.push_back(Builder.getInt32(reserved_1));
	Body.push_back(Builder.getInt32(flags));
	Body.push_back(Builder.getInt32(reserved_2));
	Body.push_back(Builder.getInt32(reserved_3));
	Body.push_back(psource);
	ArrayRef<Constant*> GVRef(Body);

	Constant* initval = ConstantStruct::get(IdentT,GVRef);
	GlobalVariable* IdentVar = new GlobalVariable(
		/*Module &M*/ M,
		/*Type* Ty*/ IdentT,
		/*bool constant*/ true,
		/*LinkageTypes Link*/ GlobalValue::LinkageTypes::PrivateLinkage,
		/*Constant* Initializer*/ initval
		);
	return IdentVar;
	//---[ident_t definition]-------------------------------
	// /*! above
	// Values for bit flags used in the ident_t to describe the fields.
	// */
	// /*! Use trampoline for internal microtasks */
	// #define KMP_IDENT_IMB 0x01
	// /*! Use c-style ident structure */
	// #define KMP_IDENT_KMPC 0x02
	// /* 0x04 is no longer used */
	// /*! Entry point generated by auto-parallelization */
	// #define KMP_IDENT_AUTOPAR 0x08
	// /*! Compiler generates atomic reduction option for kmpc_reduce* */
	// #define KMP_IDENT_ATOMIC_REDUCE 0x10
	// /*! To mark a 'barrier' directive in user code */
	// #define KMP_IDENT_BARRIER_EXPL 0x20
	// /*! To Mark implicit barriers. */
	// #define KMP_IDENT_BARRIER_IMPL 0x0040
	// #define KMP_IDENT_BARRIER_IMPL_MASK 0x01C0
	// #define KMP_IDENT_BARRIER_IMPL_FOR 0x0040
	// #define KMP_IDENT_BARRIER_IMPL_SECTIONS 0x00C0
	// #define KMP_IDENT_BARRIER_IMPL_SINGLE 0x0140
	// #define KMP_IDENT_BARRIER_IMPL_WORKSHARE 0x01C0
	// !
	//  * The ident structure that describes a source location.
	// typedef struct ident {
	//   kmp_int32 reserved_1; /**<  might be used in Fortran; see above  */
	//   kmp_int32 flags; /**<  also f.flags; KMP_IDENT_xxx flags; KMP_IDENT_KMPC
	//                       identifies this union member  */
	//   kmp_int32 reserved_2; /**<  not really used in Fortran any more; see above */
	// #if USE_ITT_BUILD
	// /*  but currently used for storing region-specific ITT */
	// /*  contextual information. */
	// #endif /* USE_ITT_BUILD */
	//   kmp_int32 reserved_3; /**< source[4] in Fortran, do not use for C++  */
	//   char const *psource; /**< String describing the source location.
	//                        The string is composed of semi-colon separated fields
	//                        which describe the source file, the function and a pair
	//                        of line numbers that delimit the construct. */
	// } ident_t;
	// /*!*/
	//------------------------------------------------------------------------
}

void ParamGet::ParamGetCommon::create_kmpc_global_thread_num_call(Module &M, IRBuilder<> &Builder)
{
	//string str;
	Function* F = dyn_cast<Function>( OMPParamMap.find("__kmpc_global_thread_num")->second ); //M.getFunction("__kmpc_global_thread_num");
	/*
	auto gvitr = GVVector.begin();
	for(;gvitr!=GVVector.end();++gvitr){
		if(Constant* temp = dyn_cast<Constant>(gvitr->second)){
			if(User* U = dyn_cast<User>(temp->getOperand(0)) ){
				if(U->getOperand(1) == Builder.getInt32(2)){
					break;
				}
			}				
		}
	}
	if(gvitr == GVVector.end()){
		errs()<<"Global Variable is not found\n";
		return;
	}
	*/
	auto ompitr = OMPParamMap.find("2");
	ArrayRef<Value*> Args(ompitr->second);
	Value* buildval;
	buildval = dyn_cast<Value>(
		Builder.CreateCall(
		/*FunctionType* FTy*/ F->getFunctionType(),
		/*Value* Callee*/ dyn_cast<Value>(F),
		/*ArrayRef<Value* > Args*/ Args
		)
	);

	//global-tid store
	OMPParamMap.insert(make_pair("global_tid",buildval));
}


void ParamGet::ParamGetCommon::insertCreatedBB(Function* OutFunc)
{
	for(auto bbpair : CreatedBB){
		errs()<<"Insert BB: "<<bbpair.first<<"\n";
		BasicBlock* nextbb = bbpair.second;
		nextbb->insertInto(OutFunc);
	}
}


void ParamGet::ParamGetCommon::buid_static_initBB(Module &M, IRBuilder<> &Builder,
		Loop* targetLoop)
{
	/* OMPParamMap will be updated:
	str 	|data
	sched 	|FIXME: foModule &M,r now, this is not calculated value.
	incr 	|new incr is always 1
	chunk 	|FIXME: for now, this is not calculated value.
	*/
	insertConstantOMPVal(Builder);

	/* OMPParamMap will be updated:
	str 		|data
	"plower"		|Pointer to the lower bound
	"UB_calc"
	"pupper"		|Pointer to the upper bound
	"pstride"
	"plastiter"
	*/	
	buildStoreToOMPVar(Builder);

	//kmpc_globa_thread_num is not needed?
	//create_kmpc_global_thread_num_call(M,Builder);
	OMPParamMap.insert(make_pair(
		"global_tid",
		dyn_cast<Value>(Builder.CreateAlignedLoad(
			dyn_cast<Value>( &*(Builder.GetInsertBlock()->getParent()->arg_begin()) ),//Argument[0] is global_tid
			4 // global_tid is always i32 type, So this constant Align(4) is OK.
		))
	));


	BasicBlock* EndBB;
	BasicBlock* BodyBB;
	Value* CondValue;


	switch(myScheduleType)
	{
		case kmp_sch_static:
		case kmp_sch_static_chunked:
			{
				//create kmpc_for_static_init call inst
				errs()<<"CondOperandIntegerSize in call: "<<CondOperandIntegerSize<<"\n";
				if(CondOperandIntegerSize < 64){
					errs()<<"call kmpc 4\n";
					create_kmpc_for_static_init_4_call(M,Builder);
				}else{
					errs()<<"call kmpc 8\n";
					create_kmpc_for_static_init_8_call(M,Builder);
				}
				//new Loop var's store should appear after kmpc_init



				//select and store UB minimum of {UB_calc, pupper_Load}
				Value* UBLoad = dyn_cast<Value>(
					Builder.CreateAlignedLoad(
						OMPParamMap.find("pupper")->second,
						dyn_cast<AllocaInst>( 
							OMPParamMap.find("pupper")->second
							)->getAlignment()
						)
					);
				//OMPParamMap.insert(make_pair("pupper_Load",UBLoad));

				//CreateBr to LoopBody
				Value* ICmpUB = Builder.CreateICmp(
					CmpInst::Predicate::ICMP_SGT,
					UBLoad,
					OMPParamMap.find("UB_calc")->second
				);
				UBLoad = Builder.CreateSelect(
					/*Value* C*/ICmpUB,
					/*Value* True*/OMPParamMap.find("UB_calc")->second,
					/*Value* False*/UBLoad
				);
				OMPParamMap.insert(make_pair("pupper_Load",UBLoad));
				// OMPParamMap.find("pupper_Load")->second = UBLoad;

				//calculate Loop counter's UB for canonicalization

				//(debug)
				// errs()<<"UBLoad: ";
				// UBLoad->print(errs());	
				// errs()<<"\n";
				// errs()<<"Incr_original: ";
				// OMPParamMap.find("Incr_original")->second->print(errs());	
				// errs()<<"\n";

				UBLoad = Builder.CreateMul(
					UBLoad,
					OMPParamMap.find("Incr_original")->second
					);
				UBLoad = Builder.CreateAdd(
					UBLoad,
					OMPParamMap.find("Init_built")->second
					);
				OMPParamMap.insert(make_pair("pupper_of_loop_counter",UBLoad));


				Builder.CreateAlignedStore(
					UBLoad,
					OMPParamMap.find("pupper")->second,
					dyn_cast<AllocaInst>( OMPParamMap.find("pupper")->second )->getAlignment()
				); 	//Store is for the case if pupper_Load is higher than UB_calc






				//Load LB
				Value* LBLoad = dyn_cast<Value>(
					Builder.CreateAlignedLoad(
						OMPParamMap.find("plower")->second,
						dyn_cast<AllocaInst>( 
							OMPParamMap.find("plower")->second
							)->getAlignment()
						)
					);
				OMPParamMap.insert(make_pair("plower_Load",LBLoad));
				//calculate Loop counter's LB for canonicalization
				LBLoad = Builder.CreateMul(
					LBLoad,
					OMPParamMap.find("Incr_original")->second
					);

				if(Instruction* IncrI = dyn_cast<Instruction>( LoopExpr.find("incr")->second ))
				{
					//get loop counter's binary operator
					Instruction* IncrementingI = getCounterIncrementingInst(IncrI);
					switch( IncrementingI->getOpcode() )
					{
						case Instruction::Add:
						case Instruction::FAdd:
						case Instruction::GetElementPtr:			
							LBLoad = Builder.CreateAdd(
								LBLoad,
								OMPParamMap.find("Init_built")->second
								);
							break;
						case Instruction::Sub:
						case Instruction::FSub:
							if(CondLHSisCounter)
							{
								LBLoad = Builder.CreateSub(
									LBLoad,
									OMPParamMap.find("Init_built")->second
									);
							}else
							{
								errs()<<"[ParamGet] if the incr is a sub inst, loop counter should be at LHS\n";
								exit(1);
							}
						break;
						default:
							errs()<<"[ParamGet] original incr is not add or sub or GEP\n";
							exit(1);
							break;
					}
				}
				//to the different bitwidth loop counter, we should set the initial lower bound to the loop counter's bitwidth.
				Type* dstType = LoopExpr.find("init")->second->getType();
				unsigned counterWidth = dstType->getIntegerBitWidth();
				Value* castedLBLoad = LBLoad;
				if(counterWidth < CondOperandIntegerSize){
					castedLBLoad = Builder.CreateTrunc(LBLoad, dstType);
				}else if(counterWidth > CondOperandIntegerSize){
					castedLBLoad = Builder.CreateSExt(LBLoad, dstType);
				}
				// OMPParamMap.insert(make_pair("plower_Load_casted",castedLBLoad));

				if( dyn_cast<LoadInst>(LoopExpr.find("init")->second) )
				{
					Builder.CreateAlignedStore(
						castedLBLoad,
						OMPParamMap.find("plower")->second,
						dyn_cast<AllocaInst>( OMPParamMap.find("plower")->second )->getAlignment()
					); 	//Store is for the case if pupper_Load is higher than UB_calc					
				}
				OMPParamMap.insert(make_pair("plower_of_loop_counter",castedLBLoad));

				//if "newLB < origLB" is true, it's invalid. So branch to end
				if(myScheduleType == kmp_sch_static)
				{
					DispatchBBMap.erase( DispatchBBMap.find("preheader") );
					DispatchBBMap.insert( make_pair("preheader", Builder.GetInsertBlock() ));
					EndBB = DispatchBBMap.find("dispatch")->second;
					BodyBB = DispatchBBMap.find("body")->second;
					// Builder.CreateCondBr(
					// CondValue,
					// /*BasicBlock* True*/,DispatchBBMap.find("dispatch")->second,
					// /*BasicBlock* False*/DispatchBBMap.find("body")->second
					// );
				}else //chunked
				{
					Builder.CreateCondBr(
					CondValue,
					/*BasicBlock* True*/DispatchBBMap.find("end")->second,
					/*BasicBlock* False*/DispatchBBMap.find("preheader")->second
					);			

					BasicBlock* preBB = Builder.GetInsertBlock();
					DispatchBBMap.find("preheader")->second->insertInto(
						Builder.GetInsertBlock()->getParent()
					);
					Builder.SetInsertPoint(DispatchBBMap.find("preheader")->second);		

					//update LB and UB for stride
					PHINode* UBphi = Builder.CreatePHI(
						OMPParamMap.find("pupper_Load")->second->getType(),
						/*unsigned NumReservedValues*/2
					);
					UBphi->addIncoming(OMPParamMap.find("pupper_Load")->second,preBB);
					OMPParamMap.insert(make_pair(
						"phi_for_UB_stride",
						dyn_cast<Value>(UBphi)
					));
					PHINode* LBphi = Builder.CreatePHI(
						OMPParamMap.find("plower_Load")->second->getType(),
						/*unsigned NumReservedValues*/2
					);
					LBphi->addIncoming(OMPParamMap.find("plower_Load")->second,preBB);
					OMPParamMap.insert(make_pair(
						"phi_for_LB_stride",
						dyn_cast<Value>(LBphi)
					));

					//cond br to body
					CondValue = Builder.CreateICmp(
						CmpInst::Predicate::ICMP_SGT,
						dyn_cast<Value>(LBphi),
						dyn_cast<Value>(UBphi)
					);

					EndBB = DispatchBBMap.find("dispatch_inc")->second;
					BodyBB = DispatchBBMap.find("body")->second;
					// Builder.CreateCondBr(
					// 	CondValue,
					// 	DispatchBBMap.find("dispatch_inc")->second,
					// 	DispatchBBMap.find("body")->second
					// );
				}
			}
			break;
		case kmp_sch_dynamic_chunked:
		case kmp_sch_guided_chunked:
		case kmp_sch_auto:
		case kmp_sch_runtime:
			if(true)
			{
				//create kmpc_dispatch call inst

				Value* NextExist;
				if(CondOperandIntegerSize < 64)
				{
					create_kmpc_dispatch_init_4_call(M,Builder);
					NextExist = create_kmpc_dispatch_next_4_call(M,Builder);
				}else
				{
					create_kmpc_dispatch_init_8_call(M,Builder);
					NextExist = create_kmpc_dispatch_next_8_call(M,Builder);
				}	
				CondValue = Builder.CreateICmp(
					CmpInst::Predicate::ICMP_EQ,// ==
					NextExist,
					Builder.getInt32(0)//dispatch_next's return value is i32 type
				);
				Builder.CreateCondBr(
					CondValue,
					/*BasicBlock* True*/DispatchBBMap.find("end")->second,
					/*BasicBlock* False*/DispatchBBMap.find("preheader")->second
				);	



				DispatchBBMap.find("preheader")->second->insertInto(
					Builder.GetInsertBlock()->getParent()
				);
				Builder.SetInsertPoint(DispatchBBMap.find("preheader")->second);


				//cmp br create
				//new Loop var's store should appear after kmpc_init
				Value* LBLoad;
				LBLoad = dyn_cast<Value>(
					Builder.CreateAlignedLoad(
						OMPParamMap.find("plower")->second,
						dyn_cast<AllocaInst>( 
							OMPParamMap.find("plower")->second
							)->getAlignment()
						)
					);
				OMPParamMap.insert(make_pair("plower_Load",LBLoad));
				//we don't use plower load after this process


				//calculate Loop counter's LB for canonicalization
				LBLoad = Builder.CreateMul(
					LBLoad,
					OMPParamMap.find("Incr_original")->second
					);
				if(Instruction* IncrI = dyn_cast<Instruction>( LoopExpr.find("incr")->second ))
				{
					//get loop counter's binary operator
					Instruction* IncrementingI = getCounterIncrementingInst(IncrI);
					switch( IncrementingI->getOpcode() )
					{
						case Instruction::Add:
						case Instruction::FAdd:
						case Instruction::GetElementPtr:			
							LBLoad = Builder.CreateAdd(
								LBLoad,
								OMPParamMap.find("Init_built")->second
								);
							break;
						case Instruction::Sub:
						case Instruction::FSub:
							if(CondLHSisCounter)
							{
								LBLoad = Builder.CreateSub(
									LBLoad,
									OMPParamMap.find("Init_built")->second
									);
							}else
							{
								errs()<<"[ParamGet] if the incr is a sub inst, loop counter should be at LHS\n";
							}
						break;
						default:
							errs()<<"[ParamGet] original incr is not add or sub or GEP\n";
							break;
					}
				}	
				OMPParamMap.insert(make_pair("plower_of_loop_counter",LBLoad));




				Value* UBLoad = dyn_cast<Value>(
					Builder.CreateAlignedLoad(
						OMPParamMap.find("pupper")->second,
						dyn_cast<AllocaInst>( 
							OMPParamMap.find("pupper")->second
							)->getAlignment()
						)
					);
				OMPParamMap.insert(make_pair("pupper_Load",UBLoad));


				//calculate Loop counter's UB for canonicalization
				UBLoad = Builder.CreateMul(
					UBLoad,
					OMPParamMap.find("Incr_original")->second
					);
				UBLoad = Builder.CreateAdd(
					UBLoad,
					OMPParamMap.find("Init_built")->second
					);
				OMPParamMap.insert(make_pair("pupper_of_loop_counter",UBLoad));




				EndBB = DispatchBBMap.find("dispatch")->second;
				BodyBB = DispatchBBMap.find("body")->second;

				// Builder.CreateCondBr(
				// 	CondValue,
				// 	/*BasicBlock* True*/DispatchBBMap.find("dispatch")->second,
				// 	/*BasicBlock* False*/DispatchBBMap.find("body")->second
				// );
			}	
			break;
		default:
			errs()<<"this schedule type is not what we can handle for now\n";
	}



	// buildInitialValueOfIndexVariable(Builder,targetLoop);


	CondValue = Builder.CreateICmp(
		CmpInst::Predicate::ICMP_SGT,
		OMPParamMap.find("plower_Load")->second,
		OMPParamMap.find("pupper_Load")->second
	);
	Builder.CreateCondBr(
		CondValue,
		/*BasicBlock* True*/EndBB,
		/*BasicBlock* False*/BodyBB
	);
}

void ParamGet::ParamGetCommon::buildPlowerLoad(IRBuilder<> &Builder, string loadInstName)
{	
	Value* plowerLoad;
	if( OMPParamMap.find("plower_Load_in_Loop") == OMPParamMap.end() ){
		Value* plowerV = OMPParamMap.find("plower")->second;
		plowerLoad = dyn_cast<Value>(
			Builder.CreateAlignedLoad(plowerV, 
				dyn_cast<AllocaInst>(plowerV)->getAlignment())
		);
		OMPParamMap.insert(make_pair("plower_Load_in_Loop",plowerLoad));
		//(debug)
		errs()<<"first encounter of plower_Load_in_Loop: ";
		plowerLoad->print(errs());
		errs()<<"\n";
	}else{
		plowerLoad = OMPParamMap.find("plower_Load_in_Loop")->second;
	}
	if( BuiltValMap.find(loadInstName) != BuiltValMap.end() ){
		BuiltValMap.find(loadInstName)->second = plowerLoad;
	}else{
		BuiltValMap.insert(make_pair(loadInstName, plowerLoad));
	}
	//debug
	errs()<<"inserted: ";
	BuiltValMap.find(loadInstName)->second->print(errs());
	errs()<<"\n";
}


void ParamGet::ParamGetCommon::buildCapturedInstVector(Module &M, IRBuilder<> &Builder, Function* OutFunc, Loop* targetLoop)
{
	auto bbitr = CreatedBB.begin();

	errs()<<"--- initialize private as undef ---\n";
	initializeUndefinedPrivate(Builder,PrivateValMap);

	errs()<<"--- initialize lastprivate as undef ---\n";
	initializeUndefinedPrivate(Builder,LastPrivateValMap);

	errs()<<"--- force to use local reduction in each thread ---\n";
	//replace alloca for reduction
	for(auto reditr : ReductionValMap)
	{
		string reductionName = reditr.first;
		//(debug)
		// errs()<<"name: "<< reductionName <<", val: ";
		// redV->print(errs()); 
		// errs()<<"\n";

		//FIXME: this is first aid for SNPD article
		//if redV is Alloca or global or Argument, reduction is used.
		//store destination change
		string localName = reductionName+"_Local";
		if( BuiltValMap.find(localName) != BuiltValMap.end() &&
				BuiltValMap.find(reductionName) != BuiltValMap.end() )
		//please test: red is global or argument
		// Value* redV = reditr.second.first;
		// if( dyn_cast<AllocaInst>(redV) ||
		// 	dyn_cast<GlobalVariable>(redV) || dyn_cast<Argument>(redV) )
		{
			Value* localV = BuiltValMap.find(reductionName+"_Local")->second;
			BuiltValMap.erase( BuiltValMap.find(reductionName) );
			BuiltValMap.insert(make_pair(
				reductionName,
				localV
				));	
			errs()<<"inserted to BuiltValMap: ";
			BuiltValMap.find(reductionName)->second->print(errs()); 
			errs()<<"\n";		
		}
	}
	errs()<<"-------------------\n";


	//erase increment store to the elder loop counter.
	//if erase CapturedInstVector's element in iteration, it makes a lot of problem.
	//Creation of new increment starts when we meet the LoopExpr("incr")




	//build start
	bool targetIISkippedFlag = false;
	//we assume that init is defined before loop. so the insts in LoopInitTraceVector won't appear.
	// auto inititr = LoopInitTraceVector.begin();
	auto conditr = LoopCondTraceVector.begin();
	auto incritr = LoopIncrTraceVector.begin();
	for(auto institr = CapturedInstVector.begin(); institr != CapturedInstVector.end(); ++institr)
	{
		string curbbStr = bbitr->first;
		Instruction* CheckI = institr->second;
		errs()<<"Build : ";
		CheckI->print(errs());
		errs()<<"\n";

		//skip loop counter's store
		if( LoadInst* InitLoad = dyn_cast<LoadInst>(LoopExpr.find("init")->second) )
		//loop counter is load
		{
			if(StoreInst* SI = dyn_cast<StoreInst>(CheckI))
			{
				if(SI->getOperand(0)->getName().str() == LoopExpr.find("incr")->second->getName().str())
				{
					continue;
					//EraseFlag = true;
				}else if( SI->getOperand(1)->getName().str() == InitLoad->getOperand(0)->getName().str() )
				{
					// errs()<<"StoreInst will not be built: this StoreInst is storing loopcounter.\n";
					// errs()<<"Loop counter must be stored only once in Latch part.\n";
					continue;
					//EraseFlag = true;
				}
			}
		}
		string inststr = CheckI->getName().str();		

		//skip original cond and incr //this is not beautiful way
		Instruction* I = institr->second;
		//(debug)
		// errs()<<"I is:\t";
		// I->print(errs()); errs()<<"\n";
		if(inststr == LoopExpr.find("cond")->second->getName().str())//cond
		{
			// "plower_Load_in_Loop"
			errs()<<"--- build local loop exit condition ---\n";
			buildLocalLoopExitCondition(Builder);
			// errs()<<"buildLocalLoopExitCondition OK\n";
			continue;
		}else if(inststr == LoopExpr.find("incr")->second->getName().str())//incr
		{
			errs()<<"--- build local loop increment ---\n";
			buildLocalLoopIncrement(Builder,I);
			errs()<<"buildLocalLoopIncrement OK\n";
			continue;
		}else if( isLoopCounter(dyn_cast<Value>(I)) )//init
		{
			errs()<<"--- build local loop counter ---\n";
			if(PHINode* PHIN = dyn_cast<PHINode>(I)){
				Value* newInitVal = buildInstruction(Builder,I);
				//debug
				errs()<<"Thread local loop counter: "<<inststr<<": ";
				newInitVal->print(errs());
				errs()<<"\n";
				OMPParamMap.insert(make_pair("plower_Load_in_Loop",newInitVal));
			}else if(dyn_cast<LoadInst>(I)){
				buildPlowerLoad(Builder, I->getName().str());
			}else{
				errs()<<"loopcounter is not Load or PHI\n";
				I->dump();
			}
			//(debug)
			errs()<<"insert OMPParamMap as plower_Load_in_Loop: ";
			OMPParamMap.find("plower_Load_in_Loop")->second->print(errs()); errs()<<"\n";
			// errs()<<"--- built  ---\n";
			continue;
		}

		//skip cond or incr trace vector ----------------
		if( conditr != LoopCondTraceVector.end() && conditr->second->hasName() && inststr == conditr->first 
		){
			errs()<<"Cond trace inst is skipped: ";
			institr->second->print(errs());
			errs()<<"\n";
			conditr++;
			continue;
		}else if( incritr != LoopIncrTraceVector.end() && incritr->second->hasName() && inststr == incritr->first 
		){
			Value* incrElemV = incritr->second;
			bool buildFlag = isLoopCounter(incrElemV);
			incritr++;
			if(!buildFlag){
				errs()<<"Incr trace inst is skipped: ";
				incrElemV->print(errs());
				errs()<<"\n";
				incritr++;
				continue;
			}
		}
		//---------------
		// if(EraseFlag)
		// {
		// 	institr--;
		// }
		//------------------------------------------------------






		//before use original loop counter in new loop, we calculate Original_LoopCounter
		// if( LoadInst* LI = dyn_cast<LoadInst>(LoopExpr.find("init")->second) )//init
		// {
		// 	string initstr;
		// 	initstr = LI->getOperand(0)->getName().str();
		// 	if(dyn_cast<LoadInst>(I))
		// 	{
		// 		//if build target Inst is load from init ptr, and new init load is not in same BB
		// 		//we create load. Else we can skip load
		// 		if( I->getOperand(0)->getName().str() == initstr )
		// 		{
		// 			//This could be wrong. Please check it work with every Loop
		// 			//we just skip LoadInst that is used Only by Incr
		// 			if( I->user_empty() )
		// 			{
		// 				errs()<<"User is empty, we skip this\n";
		// 				continue;
		// 			}
		// 			//----------------
		// 			buildPlowerLoad(Builder, I->getName().str());
		// 			continue;
		// 			// if( dyn_cast<LoadInst>(LoopExpr.find("init")->second) ){
		// 			// 	if( OMPParamMap.find("plower_Load_in_Loop") == OMPParamMap.end() ){
		// 			// 		Value* plowerV = OMPParamMap.find("plower")->second;
		// 			// 		Value* LBLoad = dyn_cast<Value>(
		// 			// 			Builder.CreateAlignedLoad(plowerV, 
		// 			// 				dyn_cast<AllocaInst>(plowerV)->getAlignment())
		// 			// 		);
		// 			// 		OMPParamMap.insert(make_pair("plower_Load_in_Loop",LBLoad));
		// 			// 		//(debug)
		// 			// 		errs()<<"plower_Load_in_Loop first: ";
		// 			// 	}
		// 			// }
		// 			// //--------------
		// 			// if( OMPParamMap.find("plower_Load_in_Loop") != OMPParamMap.end() ){
		// 			// 	Value* loadValue = OMPParamMap.find("plower_Load_in_Loop")->second;
		// 			// 	string loadName = I->getName().str();
		// 			// 	if( BuiltValMap.find(loadName) == BuiltValMap.end() ){
		// 			// 		BuiltValMap.insert(make_pair( loadName, loadValue	));
		// 			// 	}else{
		// 			// 		BuiltValMap.find(loadName)->second = loadValue;
		// 			// 	}
		// 			// 	//(debug)				
		// 			// 	errs()<<"plower_Load_in_Loop others: ";
		// 			// }
					
		// 			// errs()<<"inserted: ";
		// 			// BuiltValMap.find(I->getName().str())->second->print(errs());
		// 			// // errs()<<"\n";
		// 			// continue;
		// 		}
		// 	}
		// }

		//for ptr loop counter's use
		if( OMPParamMap.find("Ptr_init") != OMPParamMap.end() &&
			OMPParamMap.find("Ptr_in_Loop") == OMPParamMap.end() )
		{
			//check whether an inst uses init as an operand
			unsigned openum = I->getNumOperands();
			for(unsigned i=0; i<openum; i++)
			{
				Value* OpeVal = I->getOperand(i);
				string initstr = LoopExpr.find("init")->second->getName().str();
				//checking ope name is same to init name
				if( OpeVal->hasName() && OpeVal->getName().str() == initstr )
				{
					if( BuiltValMap.find(initstr) != BuiltValMap.end() )
					{
						replaceCounterWithGEP(Builder, initstr);
						//(debug)
						errs()<<"Ptr_in_Loop: ";
						OMPParamMap.find("Ptr_in_Loop")->second->print(errs());
						errs()<<"\n";
					}
					break;	
				}
			}
		}



		//--for private replace--//
		//private var
		if( !(PrivateValMap.empty()) || !(FirstPrivateValMap.empty()) || !(LastPrivateValMap.empty()) )
		{
			//future work
			//check GEP index
			//if Store's and Load's pointer operand points same GEP Index 
			//replace value
			if(I->getOpcode() == Instruction::Store)
			{
				Value* DstVal = I->getOperand(1);
				string str = DstVal->getName().str();
				errs()<<"DstName: "<< DstVal->getName().str() <<"\n";
				if( ConstantExpr* CE = dyn_cast<ConstantExpr>(DstVal) )
				{
					errs()<<"Sorry we can't handle ConstantExpr of Store\t";
					CE->print(errs()); errs()<<"\n";
				}
				if(DstVal->hasName() && 
					( PrivateValMap.find(str) != PrivateValMap.end() ||
					 FirstPrivateValMap.find(str) != FirstPrivateValMap.end()  ||
					 LastPrivateValMap.find(str) != LastPrivateValMap.end() ) )
				{
					//bool ScalarOrPointerFlag = false;
					//Type* SrcType = I->getOperand(0)->getType();

					// if( dyn_cast<PointerType>(SrcType) )
					// {
					// 	errs()<<"Src is pointer\n";
					// 	ScalarOrPointerFlag = true;						
					// }else
					// {
					// 	//(debug)
					// 	errs()<<"ElemTy:\t";
					// 	SrcType->print(errs()); errs()<<"\n";

					// 	if( !( SrcType->isStructTy() ||
					// 			SrcType->isArrayTy() ||
					// 			SrcType->isVectorTy() ) )
					// 	{
					// 		errs()<<"Src is pointer\n";
					// 		ScalarOrPointerFlag = true;
					// 	}
					// }
					//--------------
					// if( BuiltValMap.find( str+".FirstPrivate.phi" ) != BuiltValMap.end() )
					// {
					// 	errs()<<"------------SKIPPED with phi-------------\n";
					// 	I->print(errs()); errs()<<"\n";
					// 	errs()<<"-------------------------\n";
					// 	Value* replaceVal;
					// 	if( I->getOperand(0)->hasName() )
					// 	{
					// 		replaceVal = BuiltValMap.find( I->getOperand(0)->getName().str() )->second;
					// 	}else
					// 	{
					// 		//Constant
					// 		replaceVal = I->getOperand(0);
					// 		if( !(dyn_cast<Constant>(replaceVal)) )
					// 		{
					// 			errs()<<"We assume that no-name operand is constant val\n";
					// 			replaceVal->dump();
					// 		}
					// 	}
					// 	BuiltValMap.find( DstVal->getName().str() )->second = replaceVal;

					// 	//(debug)
					// 	errs()<<"replaced with: ";
					// 	BuiltValMap.find( DstVal->getName().str() )->second->print(errs()); errs()<<"\n";
					// 	continue;
					// }else 
					if( dyn_cast<AllocaInst>( BuiltValMap.find(str)->second ) )
					{
						//this private is used in 
						//so create store or load as the original region
					}else
					{
						errs()<<"------------SKIPPED with scalar-------------\n";
						errs()<<"####[ParamGet] Why is this happening? ###\n";
						I->print(errs()); errs()<<"\n";
						errs()<<"-------------------------\n";
						Value* SrcVal;
						if( I->getOperand(0)->hasName() )
						{
							SrcVal = BuiltValMap.find( I->getOperand(0)->getName() )->second;
						}else
						{
							SrcVal = I->getOperand(0);
							//this might be wrong
						}
						BuiltValMap.find(str)->second = SrcVal;
						errs()<<"replaced with: ";
						BuiltValMap.find( DstVal->getName().str() )->second->print(errs()); errs()<<"\n";

						continue;
					}

				}	
			}else if( I->getOpcode() == Instruction::Load )
			{
				Value* SrcVal = I->getOperand(0);
				string str = SrcVal->getName().str();
				if( ConstantExpr* CE = dyn_cast<ConstantExpr>(SrcVal) )
				{
					errs()<<"This load is using CE\t";
					CE->print(errs()); errs()<<"\n";
					switch(CE->getOpcode())
					{
						case Instruction::Trunc:
						case Instruction::ZExt:
						case Instruction::SExt:
						case Instruction::FPToUI:
						case Instruction::FPToSI:
						case Instruction::UIToFP: 
						case Instruction::SIToFP:
						case Instruction::FPTrunc:
						case Instruction::FPExt:
						case Instruction::PtrToInt:
						case Instruction::IntToPtr:
						case Instruction::BitCast:
						case Instruction::AddrSpaceCast:
							if(CE->isCast())
							{
								if( CE->getOperand(0)->hasName() &&
									BuiltValMap.find( CE->getOperand(0)->getName().str() ) != BuiltValMap.end() )
								{
									string cestr = CE->getOperand(0)->getName().str();
									//global variables that contain scalar or pointer are handled here.
									auto valitr = BuiltValMap.find(cestr);
									
									//(debug)
									errs()<<"Cast From: ";
									valitr->second->print(errs()); errs()<<"\n";

									// if(BuiltValMap.find(cestr+".FirstPrivate.phi") != BuiltValMap.end())
									// {
									// 	errs()<<"Casted var is loaded val for phi\n";
									// 	Value* ceV = Builder.CreateCast(
									// 		/*Instruction::CastOps Op*/Instruction::CastOps( CE->getOpcode() ),
									// 		/*Value *V*/valitr->second,
									// 		/*Type* DestTy*/ I->getType()
									// 	);				
									// 	BuiltValMap.insert(make_pair(
									// 		I->getName().str(),
									// 		ceV
									// 	));
									// }

									//continue is done at "I is already built"
								}
							}
							break;
						default:
							errs()<<"This CE is not what we can handle for LOAD\n";
							CE->dump();
					}
				}
				if(SrcVal->hasName() && 
					( PrivateValMap.find(str) != PrivateValMap.end() ||
					 FirstPrivateValMap.find(str) != FirstPrivateValMap.end() ||
					 LastPrivateValMap.find(str) != LastPrivateValMap.end() ) )
				{
					//bool ScalarOrPointerFlag = false;
					//Type* DstType = I->getType();
					// if( dyn_cast<PointerType>(DstType) )
					// {
					// 	ScalarOrPointerFlag = true;
					// }else
					// {
					// 	if( !( DstType->isStructTy() ||
					// 			DstType->isArrayTy() ||
					// 			DstType->isVectorTy() ) )
					// 	{
					// 		ScalarOrPointerFlag = true;
					// 	}
					// }
					// if( ScalarOrPointerFlag )//only replace scalar variable and pointer
					// {
					//------------------
					// if( BuiltValMap.find( str+".FirstPrivate.phi" ) != BuiltValMap.end() )
					// {
					// 	errs()<<"------------SKIPPED with phi-------------\n";
					// 	I->print(errs()); errs()<<"\n";
					// 	errs()<<"inserted to BuiltValMap:\tname:"<<I->getName().str()<<": val\t";
					// 	BuiltValMap.find( SrcVal->getName().str() )->second->print(errs()); errs()<<"\n";
					// 	errs()<<"-------------------------\n";

					// 	// BuiltValMap.find( 
					// 	// 	I->getName().str() 
					// 	// )->second = BuiltValMap.find( SrcVal->getName().str() )->second;
					// 	BuiltValMap.insert(make_pair(
					// 		I->getName().str(),
					// 		BuiltValMap.find( SrcVal->getName().str() )->second
					// 	));
					// 	continue;
					// }else 
					if( dyn_cast<AllocaInst>( BuiltValMap.find(str)->second ) )
					{
						//this private is used in 
						//so create store or load as the original region
					}else // same as above
					{
						errs()<<"------------SKIPPED with scalar-------------\n";
						I->print(errs()); errs()<<"\n";
						errs()<<"inserted to BuiltValMap:\tname:"<<I->getName().str()<<": val\t";
						BuiltValMap.find( SrcVal->getName().str() )->second->print(errs()); errs()<<"\n";
						errs()<<"-------------------------\n";
						BuiltValMap.insert(make_pair(
							I->getName().str(),
							BuiltValMap.find( SrcVal->getName().str() )->second
						));

						continue;
					}
				}
			}
			// 	else if( I->getOpcode() == Instruction::GetElementPtr )
			// 	{
			// 		Value* SrcVal = I->getOperand(0);
			// 		if(SrcVal->hasName() && 
			// 			( PrivateValMap.find(SrcVal->getName().str()) != PrivateValMap.end() ||
			// 			 FirstPrivateValMap.find(SrcVal->getName().str()) != FirstPrivateValMap.end() ) )
			// 		{
			// 			bool ScalarOrPointerFlag = false;
			// 			Type* DstType = SrcVal->getType()->getPointerElementType();
			// 			if( dyn_cast<PointerType>(DstType) )
			// 			{
			// 				Type* ElementType = DstType->getPointerElementType();
			// 				if( !( ElementType->isStructTy() ||
			// 						ElementType->isArrayTy() ||
			// 						ElementType->isVectorTy() ) )
			// 				{
			// 					ScalarOrPointerFlag = true;
			// 				}
			// 			}else
			// 			{
			// 				ScalarOrPointerFlag = true;
			// 			}
			// 			if( ScalarOrPointerFlag )//only replace scalar variable and pointer
			// 			{
			// 				errs()<<"------------Store will be skipped-------------\n";
			// 				I->print(errs()); errs()<<"\n";
			// 				errs()<<"-------------------------\n";
			// 			}		
			// 		}
			// 	}
		}
		//----------------//

		//--for firstprivate replace--//
		if( I->hasName() )
		{
			string inststr = I->getName().str();

			//if firstprivate, skip building.
			if( FirstPrivateValMap.find( inststr ) != FirstPrivateValMap.end() )
			{
				errs()<<"FirstPrivate is already Built:\t";
				I->print(errs()); errs()<<"\t";
				errs()<<"In BuiltValMap, it is phi:\t";
				BuiltValMap.find( inststr )->second->print(errs()); errs()<<"\n";
				continue;		
			}else if( PrivateValMap.find( inststr ) != PrivateValMap.end() ||
				LastPrivateValMap.find( inststr ) != LastPrivateValMap.end() )
			{
				if( BuiltValMap.find(inststr) != BuiltValMap.end() ){
					errs()<<"Erase undef initial value for private/lastprivate\n";
					errs()<<"Erase from BuiltValMap: ";
					BuiltValMap.find( inststr )->second->print(errs()); 
					errs()<<"\n";
					//if lastprivate or private, replace undef initial value with new inst to be build
					BuiltValMap.erase( BuiltValMap.find(inststr) );
				}
			}else if( BuiltValMap.find( inststr ) != BuiltValMap.end() )
			{
				//else if there are already inst, skip it.
				errs()<<"I is already Built:\t";
				I->print(errs()); errs()<<"\t";
				errs()<<"In BuiltValMap:\t";
				BuiltValMap.find( inststr )->second->print(errs()); errs()<<"\n";
				continue;
			}
		}

		switch(I->getOpcode())
		{
			case Instruction::Ret:
				if(dyn_cast<ReturnInst>(I))
				{
					build_kmpc_for_static_fini_call(M,Builder,OutFunc);	
					buildInstruction(Builder, I);
					//next bb set
					if(bbitr != CreatedBB.end())
					{
						errs()<<"RetFrom:\t"<<bbitr->first<<"\n";
						BasicBlock* nextbb = bbitr->second;
						Builder.SetInsertPoint(nextbb);
						bbitr++;		
					}
				}
				break;
			case Instruction::Br:
			case Instruction::Switch:
			case Instruction::IndirectBr:
			case Instruction::Invoke:
			case Instruction::Resume:
			case Instruction::Unreachable:
			case Instruction::CleanupRet:
			case Instruction::CatchRet:
			case Instruction::CatchSwitch:
				if(I->isTerminator())
				{	

					buildInstruction(Builder, I);
					//next bb set
					bbitr++;		
					if(bbitr != CreatedBB.end())
					{							
						//(debug)
						errs()<<"change Insert point to the BB: :\t"<<bbitr->first<<"\n";
						BasicBlock* nextbb = bbitr->second;
						Builder.SetInsertPoint(nextbb);
						//loopvarcopyloadFLAG = false;
					}
			
				}
				break;
			case Instruction::Call://ok in test13/before.ll
				if(dyn_cast<CallInst>(I))
				{			
					if(IntrinsicInst* II = dyn_cast<IntrinsicInst>(I))
					{
						if( (II->getIntrinsicID() == Intrinsic::directive) && (targetIISkippedFlag == false) )
						{
							targetIISkippedFlag=true;
							errs()<<"Captured Inst is directive\n";
							break;
						}
					}
					buildInstruction(Builder, I);
				}
				break;
			case Instruction::Load:
				buildInstruction(Builder, I);
				break;
			default:
				buildInstruction(Builder, I);
				break;
		}

		if(!ReductionValMap.empty())
		{
			// errs()<<"ReductionValMap is not empty\n";
			for(auto reditr = ReductionValMap.begin(); reditr != ReductionValMap.end(); ++reditr)
			{
				//(debug)
				// errs()<<"reditr start\n";
				if( PHINode* PHIN = dyn_cast<PHINode>( reditr->second.first ) 	)
				{
					//(debug)
					// errs()<<"PHIN cast OK\n";

					//1.check if PHIN's Incoming Value is built Inst I
					for(unsigned i = 0; i<PHIN->getNumIncomingValues(); i++)
					{
						if( Instruction* IncomingI = dyn_cast<Instruction>(PHIN->getIncomingValue(i)) )
						{
							string IncomingStr = IncomingI->getName().str();
							if( I->getName().str() == IncomingStr )
							{
								//2.if it is so, create Store to RedVar_Local	
								ReductionSrcMap.insert(make_pair(
									reditr->first,
									BuiltValMap.find( I->getName().str() )->second
								));
								// errs()<<"Creating Store\n";
								// unsigned align = dyn_cast<AllocaInst>(
								// 	 BuiltValMap.find(reditr->first + "_Local")->second 
								// )->getAlignment();
								// Value* StoreVal = BuiltValMap.find( I->getName().str() )->second;
								// Builder.CreateAlignedStore( 
								// 	StoreVal,
								// 	BuiltValMap.find(reditr->first + "_Local")->second,
								// 	align
								// );
							}				 
						}
					}//end of for(i)
				}else if( dyn_cast<BinaryOperator>( reditr->second.first ) 	)
				{
					//1.check if BO is built Inst I
					string RedStr = reditr->first;
					if( I->getName().str() == RedStr )
					{
						//(debug)
						errs()<<"---BinaryOperator RedInst found\n";
						errs()<<"builtInst: ";
						I->print(errs()); errs()<<"\n";
						errs()<<"----------------\n";

						//2.if it is so, create Store to RedVar_Local	
						ReductionSrcMap.insert(make_pair(
							reditr->first,
							BuiltValMap.find( I->getName().str() )->second
						));
						// errs()<<"Creating Store\n";
						// unsigned align = dyn_cast<AllocaInst>(
						// 	 BuiltValMap.find(reditr->first + "_Local")->second 
						// )->getAlignment();
						// Value* StoreVal = BuiltValMap.find( I->getName().str() )->second;
						// Builder.CreateAlignedStore( 
						// 	StoreVal,
						// 	BuiltValMap.find(reditr->first + "_Local")->second,
						// 	align
						// );
					}	
				}
			}
		}//end of ReductionValMap.empty
	}

	//make PHINode after CapturedInstVector iteration
	//1.if PHINode does not need to include any IncomingValue,
	// we don't make in buildInstruction and make in here.
	//2.if PHINode need more than one IncomingValue,
	// we make one existing IncomingValue, dorp it and make all in here.
	errs()<<"----- add phi's incomings -----\n";
	for(auto vecpair : CapturedInstVector){
		if(PHINode* PHIN = dyn_cast<PHINode>(vecpair.second)){
			errs()<<"PHIN found:\t";
			PHIN->print(errs()); errs()<<"\n";

			PHINode* newPHIN;
			bool PHINodeIsInit = false;
			if(PHIN->getName().str() == LoopExpr.find("init")->second->getName().str())
			{
				PHINodeIsInit = true;
				newPHIN = dyn_cast<PHINode>( OMPParamMap.find("plower_Load_in_Loop")->second );
			}else{
				newPHIN = dyn_cast<PHINode>(BuiltValMap.find( PHIN->getName().str() )->second);	
			}
			errs()<<"PHIN's new inst was found:\t";
			newPHIN->print(errs()); errs()<<"\n";

			unsigned incomings = PHIN->getNumIncomingValues();
			for(unsigned i=0; i<incomings; i++)
			{
				//Incoming BasicBlock search
				BasicBlock* OldBB = PHIN->getIncomingBlock(i);
				BasicBlock* IncomingBB;
				auto bbitr = CreatedBB.begin();
				for(; bbitr != CreatedBB.end(); ++bbitr){
					if(OldBB->getName().str() == bbitr->first){
						IncomingBB = bbitr->second;
						break;
					}
				}
				//then IncomingBB wasn't found
				if(bbitr == CreatedBB.end()){
					//errs()<<"we assume that IncomingBB is from PHI's predecessior if it is nullptr\n";
					IncomingBB = newPHIN->getParent()->getPrevNode();
				}

				//Incoming Value search
				errs()<<"Add Incoming search: ";
				Value* IncomingV = PHIN->getIncomingValue(i);
				if(PHINodeIsInit){
					errs()<<"for loop counter:: ";
					if(targetLoop->contains(OldBB)){
						//iteration's phi value
						errs()<<"this IncomingValue is replaced with plower_Load_in_Loop\n";
						IncomingV = OMPParamMap.find("new_incr")->second;
					}else{
						//entry phi value
						errs()<<"this IncomingValue is replaced with plower_of_loop_counter\n";
						if(myScheduleType == sched_type::kmp_sch_static_chunked){
							IncomingV = OMPParamMap.find("phi_for_LB_stride")->second;
							IncomingBB = DispatchBBMap.find("preheader")->second;
						}else{
							IncomingV = OMPParamMap.find("plower_of_loop_counter")->second;
						}
					}
				}else if( IncomingV->hasName() ){
					errs()<<"IncomingV Name: "<< IncomingV->getName().str() << "\n";
					IncomingV = BuiltValMap.find(IncomingV->getName().str())->second;
				}//else if IncomingV has no name, it is Constant Value
				errs()<<"IncomingV: ";
				IncomingV->print(errs()); errs()<<"\n";
				newPHIN->addIncoming(IncomingV, IncomingBB);
			}
		}else
		{
			// errs()<<"Sorry we can't find built PHINode\n";
		}
	}//end of institr


}//end of buildCapturedInstVector

void ParamGet::ParamGetCommon::initializeUndefinedPrivate(IRBuilder<> &IRBuilder, StrVmap& AnyPrivateMap)
{

	//initialize private variables to undefined
	//DO NOT use this function for firstprivate 
	for(auto valitr : AnyPrivateMap)
	{
		Value* priV = valitr.second;
		string privateName = valitr.first;
		Type* privateType = priV->getType();
		//we don't create undef for PHI node (lastprivate can be phi)
		if( dyn_cast<PHINode>(priV) ){
			continue;
		}
		if( dyn_cast<AllocaInst>(priV) )
		{
			privateType = privateType->getPointerElementType();
		}
		if(BuiltValMap.find(privateName) == BuiltValMap.end())
		{
			BuiltValMap.insert(make_pair(
				privateName,
				UndefValue::get(privateType)
			));
		}
		errs()<<"name: "<< privateName <<", val: ";
		BuiltValMap.find(privateName)->second->print(errs()); 
		errs()<<"\n";
	}
}

void ParamGet::ParamGetCommon::replaceCounterWithGEP(IRBuilder<> &Builder,
		string buildstr)
{
	Value* InitOri = OMPParamMap.find("Ptr_init")->second;
	//for the counter of ptr type, build GEP and insert to BuiltValMap
	//get base ptr
	//get current loop counter
	Value* CurLB = OMPParamMap.find("plower_Load_in_Loop")->second;
	//create GEP inst
	Value* GEPVal = Builder.CreateInBoundsGEP(
		InitOri->getType()->getPointerElementType(),
		InitOri,
		CurLB
		);

	if( BuiltValMap.find(buildstr) != BuiltValMap.end() )
	{
		BuiltValMap.erase( BuiltValMap.find(buildstr) );
	}
	BuiltValMap.insert(make_pair(
		buildstr, GEPVal
	));
	OMPParamMap.insert(make_pair(
		"Ptr_in_Loop",
		GEPVal
		));
	
	errs()<<"pointer change: name:"<< buildstr <<"\nval: ";
	BuiltValMap.find(buildstr)->second->print(errs()); 
	errs()<<"\n";
	//institr->first is equal to LoopExpr.find("init")->second->getName().str()
	//errs()<<"\n";
}

//####################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################

void ParamGet::ParamGetCommon::buildLocalLoopExitCondition(IRBuilder<> Builder)
{
	Instruction* InitInst = dyn_cast<Instruction>(LoopExpr.find("init")->second);
	bool CondUsesInitAfterIncr = false;
	Instruction* CondVal = dyn_cast<Instruction>(LoopExpr.find("cond")->second);
	Value* LoopCountVal = getThreadLocalLoopCounter(Builder);
	if(LoopCountVal == nullptr)
	{
		//(debug)
		errs()<<"[ParamGet] we can't find thread local loop counter\n";
		return;
	}

	//terminator branch check
	Value* buildval;
	Value* UBValue = OMPParamMap.find("pupper_of_loop_counter")->second;
	if(myScheduleType == sched_type::kmp_sch_static_chunked){
		Value* pupperMemory = OMPParamMap.find("pupper")->second;
		UBValue = dyn_cast<Value>(
			Builder.CreateAlignedLoad(
				pupperMemory,
				dyn_cast<AllocaInst>( pupperMemory )->getAlignment()
		));
		OMPParamMap.insert(make_pair("pupper_Load_in_Loop", UBValue));
	}

	if( CmpInst* CI = dyn_cast<CmpInst>( LoopExpr.find("cond")->second ) ){
		CmpInst::Predicate newPred = CI->getPredicate();
		switch( newPred )
		{
			case CmpInst::Predicate::ICMP_EQ:
			case CmpInst::Predicate::ICMP_NE:
				if(	ConstantInt* ConstI = 
						dyn_cast<ConstantInt>(OMPParamMap.find("Incr_original")->second) )
				{
					//(debug)
					errs()<<"ConstI: ";
					ConstI->print(errs());
					errs()<<"\n";
					const APInt &IncrAPI = ConstI->getValue();
					if( Instruction* IncrI = 	dyn_cast<Instruction>(LoopExpr.find("incr")->second) )
					{
						Instruction* IncrementingI = getCounterIncrementingInst(IncrI);
						unsigned incrOpcode = IncrementingI->getOpcode();
						if( incrOpcode == Instruction::Add ||
							incrOpcode == Instruction::FAdd ||
							incrOpcode == Instruction::GetElementPtr 
						){
							if(IncrAPI.isNegative()){
								if(newPred == CmpInst::Predicate::ICMP_EQ){
									newPred = CmpInst::Predicate::ICMP_SLT;
								}else{
									newPred = CmpInst::Predicate::ICMP_SGT;
								}
							}else{
								if(newPred == CmpInst::Predicate::ICMP_EQ){
									newPred = CmpInst::Predicate::ICMP_SGT;
								}else{
									newPred = CmpInst::Predicate::ICMP_SLT;
								}				
							}
						}else if( incrOpcode == Instruction::Sub ||
											incrOpcode == Instruction::FSub 
						){
							if(IncrAPI.isNegative()){
								if(newPred == CmpInst::Predicate::ICMP_EQ){
									newPred = CmpInst::Predicate::ICMP_SGT;
								}else{
									newPred = CmpInst::Predicate::ICMP_SLT;
								}			
							}else{
								////////////not checked yet////////////
								if(newPred == CmpInst::Predicate::ICMP_EQ){
									newPred = CmpInst::Predicate::ICMP_SLT;
								}else{
									newPred = CmpInst::Predicate::ICMP_SGT;
								}
							}
						}else{
							errs()<<"[ParamGet] cond's Predicate is not Add or Sub or GEP,\n";
							errs()<<"\tSorry we can't determine whether InitLoad is UB or CondLoad is UB\n";
						}
					}
				}
				break;
			default:
				if(CondTrueIsIterate && CondUsesInitAfterIncr){
					newPred = CmpInst::Predicate::ICMP_SLT;
				}else if(CondTrueIsIterate && !CondUsesInitAfterIncr){
					newPred = CmpInst::Predicate::ICMP_SLE;
				}else if(!CondTrueIsIterate && CondUsesInitAfterIncr){
					newPred = CmpInst::Predicate::ICMP_SGE;
				}else if(!CondTrueIsIterate && !CondUsesInitAfterIncr){
					newPred = CmpInst::Predicate::ICMP_SGT;
				}
				break;
		}
		

		buildval = dyn_cast<Value>(
			Builder.CreateICmp(
				newPred,
				LoopCountVal,
				UBValue
				)
			);	
		//this is for storeReductionResult
		DispatchBBMap.insert(make_pair(
			"exit",
			Builder.GetInsertBlock() 
			));
	}
	BuiltValMap.insert( 
		make_pair(LoopExpr.find("cond")->second->getName().str(),buildval) 
		);
	errs()<<"built Local Loop Exit Condition: name:"<<LoopExpr.find("cond")->second->getName().str()<<": ";
	buildval->print(errs()); errs()<<"\n";
}	

Value* ParamGet::ParamGetCommon::getThreadLocalLoopCounter(IRBuilder<> Builder)
{
	Instruction* InitInst = dyn_cast<Instruction>(LoopExpr.find("init")->second);
	bool CondUsesInitAfterIncr = false;
	Instruction* CondVal = dyn_cast<Instruction>(LoopExpr.find("cond")->second);

	Value* LoopCounterV = CondLHSisCounter ?
		/*CondLHSisCounter is true: */CondVal->getOperand(0) :
		/*CondLHSisCounter is false: */CondVal->getOperand(1);
	errs()<<"Original loop counter, we replace this with thread local one: ";
	LoopCounterV->print(errs()); errs()<<"\n";

	//get thread local loop counter
	//if it is already built, use the built one
	if(LoopCounterV->hasName() && BuiltValMap.find(LoopCounterV->getName().str()) != BuiltValMap.end()){
		LoopCounterV = BuiltValMap.find(LoopCounterV->getName().str())->second;
		return LoopCounterV;
	}
	//else if it is Load loop counter, we load it.
	else if(dyn_cast<LoadInst>(InitInst)){
		buildPlowerLoad(Builder, InitInst->getName().str());
		LoopCounterV = OMPParamMap.find("plower_Load_in_Loop")->second;
		return LoopCounterV;
	}
	//else if it is phi loop counter, we use the phi
	else if(dyn_cast<PHINode>(InitInst)){
		errs()<<"In PHI buildLocalLoopExitCondition\n";
		Instruction* CondInst = dyn_cast<Instruction>(LoopExpr.find("cond")->second);
		string CondStr = CondLHSisCounter ?
			CondInst->getOperand(0)->getName().str() :
			CondInst->getOperand(1)->getName().str();
		
		//---We changed the method. This change might cause error for other IR codes---//
		//if "new_incr" is built, then use this--//
		if( OMPParamMap.find("new_incr") != OMPParamMap.end() ){
			if(CondStr == LoopExpr.find("init")->second->getName().str()){
				CondUsesInitAfterIncr = true;
				LoopCounterV = OMPParamMap.find("plower_Load_in_Loop")->second;
			}else{
				LoopCounterV = OMPParamMap.find("new_incr")->second;
			}
		}else if( OMPParamMap.find("plower_Load_in_Loop") != OMPParamMap.end() ){
			LoopCounterV = OMPParamMap.find("plower_Load_in_Loop")->second;
			//BuiltValMap.find( OMPParamMap.find("init")->second->getName().str() )->second;
		}else{
			errs()<<"sorry we haven't prepare for Cond which doesn't use LoopCounter or Incremented LoopCounter\n";
			return nullptr;
		}
		errs()<<"Thread local loop counter: ";
		LoopCounterV->print(errs()); errs()<<"\n";
		return LoopCounterV;
		//--new method end--//
		// if( CondStr == LoopExpr.find("incr")->second->getName().str() )
		// {
		// 	LoopCounterV = OMPParamMap.find("new_incr")->second;
		// }else if( CondStr == LoopExpr.find("init")->second->getName().str())
		// {
		// 	LoopCounterV = OMPParamMap.find("plower_Load_in_Loop")->second;
		// 		//BuiltValMap.find( OMPParamMap.find("init")->second->getName().str() )->second;
		// }else
		// {
		// 	errs()<<"sorry we haven't prepare for Cond which doesn't use LoopCounter or Incremented LoopCounter\n";
		// }
		//-------------------------------//
	}
	errs()<<"[ParamGet] getThreadLocalLoopCounter: we only handle phi or load of loop counter\n";
	return nullptr;
}

void ParamGet::ParamGetCommon::buildLocalLoopIncrement(IRBuilder<> Builder,Instruction* I)
{
	Instruction* InitInst = dyn_cast<Instruction>( LoopExpr.find("init")->second );
	Value* LoopCountVal;

	buildPlowerLoad(Builder, InitInst->getName().str());
	LoopCountVal = OMPParamMap.find("plower_Load_in_Loop")->second;

	Value* operand = buildInstruction(Builder,I);
	//Increment
	// 		case Instruction::Add:
	// 		case Instruction::FAdd:
	// 		case Instruction::GetElementPtr:
	// 				operand = dyn_cast<Value>(Builder.CreateAdd( 
	// 					LoopCountVal,
	// 					OMPParamMap.find("Incr_original")->second 
	// 					)); 
	// 			break;
	// 		case Instruction::Sub:
	// 		case Instruction::FSub:
	// 			if(CondLHSisCounter)
	// 			{
	// 				operand = dyn_cast<Value>(Builder.CreateSub( 
	// 					LoopCountVal,
	// 					OMPParamMap.find("Incr_original")->second 
	// 					)); 
	// 			}else
	// 			{
	// 				errs()<<"[ParamGet] if the incr is a sub inst, loop counter should be at LHS\n";
	// 			}
	// 		break;
	// 		default:
	// 			errs()<<"[ParamGet] original incr is not add or sub or GEP\n";
	// 			break;
	// 	}
	// }



	if( dyn_cast<LoadInst>(InitInst) )
	{
		Builder.CreateAlignedStore(
				operand,
				OMPParamMap.find("plower")->second,
				dyn_cast<AllocaInst>( 
					OMPParamMap.find("plower")->second
					)->getAlignment()
			);
		//Condition Judge
		// BuiltValMap.insert( 
		// 	make_pair(LoopExpr.find("cond")->second->getName().str(),buildval) 
		// 	);		

	//for now, OMPParamMap("LoopCounter") is not depend whether LoopExpr("init") is Load or PHI
	}else if(dyn_cast<PHINode>(InitInst))
	{
		errs()<<"new_incr is Created:\t";
		operand->print(errs());	errs()<<"\n";

		//BuiltValMap.insert(make_pair( OMPParamMap.find("incr")->second->getName().str(),operand ));
		OMPParamMap.insert(make_pair("new_incr",operand));
	}else
	{
		//(debug)
		errs()<<"we can't build Preheader, InitInst is not LoadInst or PHINode\n";
		operand->dump();
	}

	//this is first aid, it may be wrong
	//we register new_incr as incr in builtvalmap
	if( BuiltValMap.find(I->getName().str()) != BuiltValMap.end() )
	{
		BuiltValMap.erase( BuiltValMap.find(I->getName().str()) );
	}
	BuiltValMap.insert(make_pair(I->getName().str(), operand));
}


void ParamGet::ParamGetCommon::build_kmpc_for_static_fini_call(Module &M, IRBuilder<> &Builder, 
	Function* OutFunc)
{

	string str;
	//M.getFunction("__kmpc_for_static_fini");
	Function* F = dyn_cast<Function>( OMPParamMap.find("__kmpc_for_static_fini")->second );

	vector<Value*> Args;

	auto ompitr = OMPParamMap.find("514");
	Args.push_back(ompitr->second);
	ompitr = OMPParamMap.find("global_tid");
	Args.push_back(ompitr->second);		

	ArrayRef<Value*> ArgsRef(Args);
	Builder.CreateCall(
		/*FunctionType* FTy*/ F->getFunctionType(),
		/*Value* Callee*/ dyn_cast<Value>(F),
		/*ArrayRef<Value* > Args*/ ArgsRef
		);	

}
void ParamGet::ParamGetCommon::insertConstantOMPVal(IRBuilder<> &Builder)
{
	/*OMPParamMap will be USED:
	str 		|data
	"sched"		|Pointer to the lower bound
	"incr"
	"chunk"		|Pointer to the upper bound
	*/

	//function that calculate loop argument
	/* Anyway, This method build ALL the loop argument (used by init func)
	Args.push_back( dyn_cast<Value>(Builder.getInt32(34)) );//shcedule type
	*/
	OMPParamMap.insert(
		make_pair( "sched", dyn_cast<Value>(Builder.getInt32( myScheduleType )) )//this is always int32 type
		);
	unsigned bitwidth = CondOperandIntegerSize;
	// incr 		|Loop increment	
	OMPParamMap.insert(make_pair(
		"incr", dyn_cast<Value>(Builder.getIntN(bitwidth, 1)) 
		));
	// chunk 		|The chunk size
	OMPParamMap.insert(
		make_pair("chunk", dyn_cast<Value>(Builder.getIntN(bitwidth, ChunkSize )) )
	 );

}

void ParamGet::ParamGetCommon::buildStoreToOMPVar(IRBuilder<> &Builder)
{
	/*OMPParamMap will be USED:
	str 		|data
	"plower"		|Pointer to the lower bound
	"UB_calc"
	"pupper"		|Pointer to the upper bound
	"pstride"
	"plastiter"
	*/	
	//plower
	unsigned bitwidth = CondOperandIntegerSize;

	Builder.CreateAlignedStore(
			OMPParamMap.find("LB_canonical")->second,
			OMPParamMap.find("plower")->second,
			dyn_cast<AllocaInst>( 
				OMPParamMap.find("plower")->second
				)->getAlignment()
		);

	// //pupper 
	// buildval = dyn_cast<Value>(
	// 		Builder.CreateAlignedLoad(
	// 			OMPParamMap.find("UB_calc")->second,
	// 			dyn_cast<AllocaInst>( 
	// 				OMPParamMap.find("UB_calc")->second
	// 				)->getAlignment()
	// 		)
	// 	);
	Builder.CreateAlignedStore(
		OMPParamMap.find("UB_canonical")->second,
		OMPParamMap.find("pupper")->second,
		dyn_cast<AllocaInst>( 
			OMPParamMap.find("pupper")->second
			)->getAlignment()
		);

	//pstride
	Builder.CreateAlignedStore(
		dyn_cast<Value>(Builder.getIntN(bitwidth,1)),
		OMPParamMap.find("pstride")->second,
		dyn_cast<AllocaInst>( 
			OMPParamMap.find("pstride")->second
			)->getAlignment()
		);

	//plastiter
	Builder.CreateAlignedStore(
		dyn_cast<Value>(Builder.getInt32(0)), //this always int32 type 
		OMPParamMap.find("plastiter")->second,
		dyn_cast<AllocaInst>( 
			OMPParamMap.find("plastiter")->second
			)->getAlignment()
		);
}


//###################################################################################################################################################################### kmpc create

void ParamGet::ParamGetCommon::create_kmpc_for_static_init_4_call(Module &M, IRBuilder<> &Builder)
{
	string str;
	//function that calculate loop argument

	Function* F = dyn_cast<Function>( OMPParamMap.find("__kmpc_for_static_init_4")->second );
	vector<Value*> Args;

	auto ompitr = OMPParamMap.find("514");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("global_tid");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("sched");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("plastiter");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("plower");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("pupper");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("pstride");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("incr");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("chunk");
	Args.push_back(ompitr->second);

	//loop argment set
	ArrayRef<Value*> ArgsRef(Args);
	Builder.CreateCall(
		/*FunctionType* FTy*/ F->getFunctionType(),
		/*Value* Callee*/ dyn_cast<Value>(F),
		/*ArrayRef<Value* > Args*/ ArgsRef
		);	
}	
void ParamGet::ParamGetCommon::create_kmpc_for_static_init_8_call(Module &M, IRBuilder<> &Builder)
{
	//init_8_call is same work as init_4_call	string str;
	//function that calculate loop argument

	Function* F = dyn_cast<Function>( OMPParamMap.find("__kmpc_for_static_init_8")->second );
	//	M.getFunction("__kmpc_for_static_init_8");// actually this name is the only difference.
	vector<Value*> Args;
	// F->print(errs());
	// errs()<<"error: \n";

	auto ompitr = OMPParamMap.find("514");
	// errs()<<"error22: \n";
	Args.push_back(ompitr->second);
	// errs()<<"514: ";
	// ompitr->second->print(errs()); errs()<<"\n";

	ompitr = OMPParamMap.find("global_tid");
	Args.push_back(ompitr->second);
	// errs()<<"global_tid: ";
	// ompitr->second->print(errs()); errs()<<"\n";

	ompitr = OMPParamMap.find("sched");
	Args.push_back(ompitr->second);
	// errs()<<"sched: ";
	// ompitr->second->print(errs()); errs()<<"\n";

	ompitr = OMPParamMap.find("plastiter");
	Args.push_back(ompitr->second);
	// errs()<<"plastiter: ";
	// ompitr->second->print(errs()); errs()<<"\n";

	ompitr = OMPParamMap.find("plower");
	Args.push_back(ompitr->second);
	// errs()<<"plower: ";
	// ompitr->second->print(errs()); errs()<<"\n";

	ompitr = OMPParamMap.find("pupper");
	Args.push_back(ompitr->second);
	// errs()<<"pupper: ";
	// ompitr->second->print(errs()); errs()<<"\n";

	ompitr = OMPParamMap.find("pstride");
	Args.push_back(ompitr->second);
	// errs()<<"pstride: ";
	// ompitr->second->print(errs()); errs()<<"\n";

	ompitr = OMPParamMap.find("incr");
	Args.push_back(ompitr->second);
	// errs()<<"incr: ";
	// ompitr->second->print(errs()); errs()<<"\n";

	ompitr = OMPParamMap.find("chunk");
	Args.push_back(ompitr->second);
	// errs()<<"chunk: ";
	// ompitr->second->print(errs()); errs()<<"\n";

	//loop argment set
	ArrayRef<Value*> ArgsRef(Args);
	Builder.CreateCall(
		/*FunctionType* FTy*/ F->getFunctionType(),
		/*Value* Callee*/ dyn_cast<Value>(F),
		/*ArrayRef<Value* > Args*/ ArgsRef
		);	
}	
void ParamGet::ParamGetCommon::create_kmpc_dispatch_init_4_call(Module &M, IRBuilder<> &Builder)
{
	string str;
	//function that calculate loop argument

	Function* F = dyn_cast<Function>( OMPParamMap.find("__kmpc_dispatch_init_4")->second );
	//M.getFunction("__kmpc_dispatch_init_4");
	vector<Value*> Args;

	auto ompitr = OMPParamMap.find("2");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("global_tid");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("sched");
	Args.push_back(ompitr->second);

	Args.push_back( Builder.getInt32(0) );//Lower bound

	ompitr = OMPParamMap.find("UB_calc");
	Args.push_back(ompitr->second);//Upper bound

	ompitr = OMPParamMap.find("incr");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("chunk");
	Args.push_back(ompitr->second);

	//loop argment set
	ArrayRef<Value*> ArgsRef(Args);
	Builder.CreateCall(
		/*FunctionType* FTy*/ F->getFunctionType(),
		/*Value* Callee*/ dyn_cast<Value>(F),
		/*ArrayRef<Value* > Args*/ ArgsRef
		);	
}	
void ParamGet::ParamGetCommon::create_kmpc_dispatch_init_8_call(Module &M, IRBuilder<> &Builder)
{
	string str;
	//function that calculate loop argument

	Function* F = dyn_cast<Function>( OMPParamMap.find("__kmpc_dispatch_init_8")->second );
	//M.getFunction("__kmpc_dispatch_init_8");
	vector<Value*> Args;

	auto ompitr = OMPParamMap.find("2");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("global_tid");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("sched");
	Args.push_back(ompitr->second);

	Args.push_back( Builder.getInt64(0) );//Lower bound

	ompitr = OMPParamMap.find("UB_calc");
	Args.push_back(ompitr->second);//Upper bound

	ompitr = OMPParamMap.find("incr");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("chunk");
	Args.push_back(ompitr->second);

	//loop argment set
	ArrayRef<Value*> ArgsRef(Args);
	Builder.CreateCall(
		/*FunctionType* FTy*/ F->getFunctionType(),
		/*Value* Callee*/ dyn_cast<Value>(F),
		/*ArrayRef<Value* > Args*/ ArgsRef
		);	
}	
Value* ParamGet::ParamGetCommon::create_kmpc_dispatch_next_4_call(Module &M, IRBuilder<> &Builder)
{
	string str;
	//function that calculate loop argument

	Function* F = dyn_cast<Function>( OMPParamMap.find("__kmpc_dispatch_next_4")->second );
	//M.getFunction("__kmpc_dispatch_next_4");
	vector<Value*> Args;

	auto ompitr = OMPParamMap.find("2");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("global_tid");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("plastiter");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("plower");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("pupper");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("pstride");
	Args.push_back(ompitr->second);

	//loop argment set
	ArrayRef<Value*> ArgsRef(Args);
	Value* RetVal = dyn_cast<Value>(
		Builder.CreateCall(
			/*FunctionType* FTy*/ F->getFunctionType(),
			/*Value* Callee*/ dyn_cast<Value>(F),
			/*ArrayRef<Value* > Args*/ ArgsRef
		)
	);
	return RetVal;
}	
Value* ParamGet::ParamGetCommon::create_kmpc_dispatch_next_8_call(Module &M, IRBuilder<> &Builder)
{
	string str;
	//function that calculate loop argument

	Function* F = dyn_cast<Function>( OMPParamMap.find("__kmpc_dispatch_next_8")->second ); 
	//M.getFunction("__kmpc_dispatch_next_8");
	vector<Value*> Args;

	auto ompitr = OMPParamMap.find("2");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("global_tid");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("plastiter");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("plower");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("pupper");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("pstride");
	Args.push_back(ompitr->second);

	//loop argment set
	ArrayRef<Value*> ArgsRef(Args);
	Value* RetVal = dyn_cast<Value>(
		Builder.CreateCall(
			/*FunctionType* FTy*/ F->getFunctionType(),
			/*Value* Callee*/ dyn_cast<Value>(F),
			/*ArrayRef<Value* > Args*/ ArgsRef
		)
	);
	return RetVal;
}	
Value* ParamGet::ParamGetCommon::create_kmpc_reduce_nowait(Module &M, Function* RedFunc,
	IRBuilder<> &Builder)
{
	string str;
	//function that calculate loop argument

	Function* F = dyn_cast<Function>( OMPParamMap.find("__kmpc_reduce_nowait")->second );
	//M.getFunction("__kmpc_reduce_nowait");
	vector<Value*> Args;

	auto ompitr = OMPParamMap.find("18");
	Args.push_back(ompitr->second);
	//(debug)
	// errs()<<"18::\t";
	// ompitr->second->print(errs()); errs()<<"\n";

	ompitr = OMPParamMap.find("global_tid");
	Args.push_back(ompitr->second);

	//num_vars
	Args.push_back( Builder.getInt32( ReductionValMap.size() ) );

	//reduce_size
	Args.push_back( Builder.getInt64( ReductionValMap.size() << 3 ) );

	//reduce_data
	ompitr = OMPParamMap.find(".omp.reduction.reduction_func_BitCast");
	Args.push_back(ompitr->second);
	//(debug)
	// errs()<<"BC:\t";
	// ompitr->second->print(errs()); errs()<<"\n";

	//reduce_func
	Args.push_back( dyn_cast<Value>(RedFunc) );

	//lck
	ompitr = OMPParamMap.find("lck");
	Args.push_back(ompitr->second);

	//(debug)
	// for(auto valitr = Args.begin(); valitr != Args.end(); ++valitr)
	// {
	// 	Value* dbgV = *(valitr);
	// 	errs()<<"reduce_nowait Args:\t";
	// 	dbgV->print(errs()); errs()<<"\n";
	// }
	// errs()<<"reduce_nowait FuncTy:\t";
	// F->getFunctionType()->print(errs()); errs()<<"\n";


	//loop argment set
	ArrayRef<Value*> ArgsRef(Args);
	Value* RetVal = dyn_cast<Value>(
		Builder.CreateCall(
			/*FunctionType* FTy*/ F->getFunctionType(),
			/*Value* Callee*/ dyn_cast<Value>(F),
			/*ArrayRef<Value* > Args*/ ArgsRef
		)
	);


	//(debug)
	// errs()<<"reduce_nowait RetVal:\t";
	// RetVal->print(errs()); errs()<<"\n";

	return RetVal;
}	
void ParamGet::ParamGetCommon::create_kmpc_end_reduce_nowait(Module &M, IRBuilder<> &Builder)
{
	string str;
	//function that calculate loop argument

	Function* F = dyn_cast<Function>( OMPParamMap.find("__kmpc_end_reduce_nowait")->second );
	//M.getFunction("__kmpc_end_reduce_nowait");
	vector<Value*> Args;

	auto ompitr = OMPParamMap.find("18");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("global_tid");
	Args.push_back(ompitr->second);

	ompitr = OMPParamMap.find("lck");
	Args.push_back(ompitr->second);

	//loop argment set
	ArrayRef<Value*> ArgsRef(Args);
	Builder.CreateCall(
		/*FunctionType* FTy*/ F->getFunctionType(),
		/*Value* Callee*/ dyn_cast<Value>(F),
		/*ArrayRef<Value* > Args*/ ArgsRef
	);
}

void ParamGet::ParamGetCommon::create_kmpc_fork_callForOutlinedFunc(Module &M, IRBuilder<> &Builder,
	Loop* targetLoop)
{	
	vector<Value*> Args;
	auto OMPitr = OMPParamMap.find("2");
	Args.push_back(OMPitr->second);

	Args.push_back(dyn_cast<Value>( Builder.getInt32(FuncArgumentMap.size()) ));// this is always int32

	Type* MicroParams[] =
		{ 
			//ref:kmp.h
			//PointerType::getUnqual creates object's pointer
			PointerType::getUnqual(Builder.getInt32Ty()), //*global_tid
			PointerType::getUnqual(Builder.getInt32Ty()), //*bound_tid
		};
	FunctionType* Kmpc_MicroTy =
		llvm::FunctionType::get(Builder.getVoidTy(), MicroParams, true);

	Function* outfunc = dyn_cast<Function>( OMPParamMap.find(".omp_outlined.")->second ); 
	//M.getFunction(".omp_outlined.");
	
	//FunctionType* outfuncptr = outfunc->getFunctionType();
	//outfuncptr->print(errs());errs()<<"\n";

	Value* buildval = dyn_cast<Value>(
		Builder.CreateBitCast(
				dyn_cast<Value>(outfunc),
				dyn_cast<Type>(PointerType::getUnqual( Kmpc_MicroTy )) 
			)
		);

	Args.push_back(buildval);
	//Args.push_back(buildval);

	for(auto valitr = FuncArgumentMap.begin(); valitr != FuncArgumentMap.end(); ++valitr)
	{
		Value* argV = valitr->second;
		Args.push_back(argV);
		// if( argV->hasName() )
		// {
		// 	if( valitr->first[0] >= '0' && valitr->first[0] <= '9' )
		// 	{
		// 		argV->setName("");
		// 	}
		// }
	}
	

	Function* fork = dyn_cast<Function>( OMPParamMap.find("__kmpc_fork_call")->second );
	//M.getFunction("__kmpc_fork_call");
	ArrayRef<Value*> ArgsRef(Args);

	//Instruction* capfirst = CapturedInstVector.begin()->second;//this is Intirisic
	Builder.SetInsertPoint(targetII);
	//		errs()<<"Now we don't Create fork_call\n";// Ctrl+/
	Builder.CreateCall(
		/*FunctionType* FTy*/ 	fork->getFunctionType(),
		/*Value* Callee */ 		dyn_cast<Value>(fork),
		/*ArrayRef<Value* > Args*/ ArgsRef
		);	

	//create load 
	createLoadForShareAfterLoop(Builder, targetLoop);
	//createLoadForLastPrivate(Builder, targetLoop);


	//---not related to fork_call---//
	Builder.CreateBr(parallel_region_Successor);
	Function* ParentFn = Builder.GetInsertBlock()->getParent();
	if( ParentFn->hasPersonalityFn() )
	{
		outfunc->setPersonalityFn( ParentFn->getPersonalityFn() );
		//function should be personality function
		//if exception handling instruction is used.
	}

	//if predecessor has phi and it has operand refering BB within loop,
	//replace the IncomingBB with the current building BB.
	BasicBlock* replaceBB = Builder.GetInsertBlock();
	for(auto institr = parallel_region_Successor->begin(); 
		institr != parallel_region_Successor->end(); ++institr)
	{
		//get phi, else end
		if(PHINode* PHIN = dyn_cast<PHINode>( &*institr ))
		{
			unsigned incomings = PHIN->getNumIncomingValues();
			for(unsigned i = 0; i < incomings; i++)
			{
				//if a phi's BB is contained by loop, replace it.
				if(targetLoop->contains( PHIN->getIncomingBlock(i) ))
				{
					PHIN->setIncomingBlock(i, replaceBB);
				}
			}
		}else
		{
			break;		
		}
	}
}


//###################################################################################################################################################################### trace


void ParamGet::ParamGetCommon::traceLoopExprForUBCalc(IRBuilder<> &Builder,Loop* targetLoop)
{
	/* LoopExpr willbe
	str 	|data
	init	|Loop var's initialization // LoadInst or PHINode
	cond	|Loop's exit condition expr// cmp inst
	incr	|Loop var's increment 		//StoreInst or BinaryOperation
	*/
	//trace init
	if(Instruction* InitInst = dyn_cast<Instruction>(LoopExpr.find("init")->second))
	{
		//if loop counter is load
		if(InitInst->getOpcode() == Instruction::Load)
		{
			for(auto vecpair: CapturedInstVector)
			{
				//find store that stores to init load's memory
				Instruction* CheckingInst = vecpair.second;
				if(CheckingInst->getOpcode() == Instruction::Store){
					//check whether their memory name are same.
					if( CheckingInst->getOperand(1)->getName().str() == InitInst->getOperand(0)->getName().str() ){
						//if same, trace the stored value (this trace record is to build initial value of loop counter)
						traceInstForUBCalc(CheckingInst->getOperand(0),LoopInitTraceVector,LoopExpr,"init",targetLoop,0);
						break;
					}
				}
				//if reach the exit-cond, loop counter's first value is already in memory
				//so use this load as first value.
				if(CheckingInst->getName().str() == LoopExpr.find("cond")->second->getName().str()){
					LoopInitTraceVector.push_back(make_pair(InitInst->getName().str(),InitInst));
					break;
				}
			}
		}else if(PHINode* initPHIN = dyn_cast<PHINode>(InitInst))
		{
			for(unsigned i=0; i<initPHIN->getNumIncomingValues(); i++){
				//trace phi's incoming value which corresponds to a BB that is outer of loop. we assume that the targeted loop is single-entry.
				if( !targetLoop->contains(initPHIN->getIncomingBlock(i)) ){
					traceInstForUBCalc(initPHIN->getIncomingValue(i),LoopInitTraceVector,LoopExpr,"init",targetLoop,0);
					break;
				}
			}
		}		

		//(debug)
		errs()<<"--- Loop counter's initial value ---\n";
		for(auto institr=LoopInitTraceVector.begin(); institr!=LoopInitTraceVector.end(); ++institr)
		{
			institr->second->print(errs());errs()<<"\n";
		}
	}




	//--cond trace--//
	CmpInst* CondInst  = dyn_cast<CmpInst>( LoopExpr.find("cond")->second );
	CondLHSisCounter = true;//global(class's member)
	//we assume CondInst's Op0 is Counter's Loaded Value
	//Fist, Trace RHS of cond
	traceInstForUBCalc(CondInst->getOperand(1), LoopCondTraceVector, LoopExpr,"init",targetLoop,0 );

	//Second, check whether Instructions which exist in Vector use init.
	//if use, trace LHS
	for(auto condpair : LoopCondTraceVector)
	{
		Value* condV = condpair.second;
		if(isLoopCounter(condV)){
			CondLHSisCounter = false;
			LoopCondTraceVector.clear();
			errs()<<"tracing: ";
			CondInst->getOperand(0)->print(errs());
			errs()<<"\n";
			traceInstForUBCalc(CondInst->getOperand(0),this->LoopCondTraceVector,LoopExpr,"init",targetLoop,0);
			break;
		}
	}
	//(debug)
	//trace is over, traced Instructins is LoopExitCondition
	errs()<<"--- Loop counter's cond ---:\n";
	for(auto institr = LoopCondTraceVector.begin(); institr != LoopCondTraceVector.end(); ++institr)
	{
		institr->second->print(errs());errs()<<"\n";
	}


	//--incr trace--//
	//ONLY incr's traceInstForUBCalc should replace LoopCounter with 0
	Value* IncrVal = LoopExpr.find("incr")->second;
	if( GetElementPtrInst* GEP = dyn_cast<GetElementPtrInst>(IncrVal) )
	{
		IncrVal = *(GEP->idx_begin());
	}
	traceInstForUBCalc(IncrVal,LoopIncrTraceVector,
		LoopExpr,"init",targetLoop,1);

	//(debug)
	errs()<<"--- Loop counter's incr trace ---\n";
	for(auto institr = LoopIncrTraceVector.begin(); institr != LoopIncrTraceVector.end(); ++institr)
	{
		institr->second->print(errs());errs()<<"\n";
	}
}


void ParamGet::ParamGetCommon::traceInstForUBCalc(Value* SrcV,StrVvector &TraceVector,StrVmap &RegisterMap, string initstr, Loop* targetLoop, bool MakeInitialValueZero)
{
	if(SrcV->hasName())
	{
		auto valitr = FuncArgumentMap.find( SrcV->getName().str() );
		if(valitr != FuncArgumentMap.end())
		{
			//this is same process in this func's last
			string str = SrcV->getName().str();
			TraceVector.push_back( make_pair(str,SrcV) );

			//(debug)//
			auto last = TraceVector.rbegin();
			errs()<<"Inst in UB calculation: ";
			last->second->print(errs());
			errs()<<"\n";
			//---//
			return;
		}
		//skip init for Incr
		if(MakeInitialValueZero)
		{
			if(RegisterMap.find(initstr)->second->getName().str() == SrcV->getName().str())
			{
				return;
			}
		}
	}

	if(Instruction* SrcI = dyn_cast<Instruction>(SrcV))
	{
		switch(SrcI->getOpcode())
		{

			//Standard binary operators
			case Instruction::Add:
			case Instruction::FAdd:
			case Instruction::Sub:
			case Instruction::FSub:
			case Instruction::Mul:
			case Instruction::FMul:
			case Instruction::UDiv:
			case Instruction::SDiv:
			case Instruction::FDiv:
			case Instruction::URem:
			case Instruction::SRem:
			case Instruction::FRem:
			//bitwise binary operators
			case Instruction::Shl:
			case Instruction::LShr:
			case Instruction::AShr:
			case Instruction::And:
			case Instruction::Or:
			case Instruction::Xor:
			// Cast operators ...
			case Instruction::Trunc:
			case Instruction::ZExt:
			case Instruction::SExt:
			case Instruction::FPToUI:
			case Instruction::FPToSI:
			case Instruction::UIToFP: 
			case Instruction::SIToFP:
			case Instruction::FPTrunc:
			case Instruction::FPExt:
			case Instruction::PtrToInt:
			case Instruction::IntToPtr:
			case Instruction::BitCast:
			case Instruction::AddrSpaceCast:
			//Memory operators...
			case Instruction::GetElementPtr:
			//Other operators...
			case Instruction::Call:
				if( dyn_cast<BinaryOperator>(SrcI) || 
						dyn_cast<CastInst>(SrcI) || 
						dyn_cast<GetElementPtrInst>(SrcI) ||
						dyn_cast<CallInst>(SrcI) )
				{
					unsigned int openum = SrcI->getNumOperands();
					for(unsigned int i=0;i<openum;i++)
					{
						Value* OperVal = SrcI->getOperand(i);
						//(debug)
						// errs()<<"####first Operand is set to:";
						// OperVal->print(errs()); errs()<<"\n";
						//--------------------------------//
						//what causes inifinity loop?????????????????????????
						while(PHINode* PHIN = dyn_cast<PHINode>(OperVal)) // phi is one of the end of trace.
						{
							if( PHIN->hasName() && MakeInitialValueZero && 
								RegisterMap.find(initstr)->second->getName().str() == PHIN->getName().str() )
							{
								break;
							}
							//we assume that targetloop is single entry.
							//If there is any problem:
							//Idea1. make phinode before the parallel execution.
							//Idea2. do something.
							for(unsigned ii = 0; ii<PHIN->getNumIncomingValues(); ii++)
							{
								BasicBlock* IncomingBB = PHIN->getIncomingBlock(ii);
								if( IncomingBB->hasName() )
								{
									if( !(targetLoop->contains(IncomingBB)) )
									{
										OperVal = PHIN->getIncomingValue(ii);
										SrcI->setOperand(i,OperVal);
										//(debug)
										// errs()<<"####second phi Operand is set to:";
										// OperVal->print(errs()); errs()<<"\n";
										break;
									}
								}
							}
							if( dyn_cast<PHINode>(OperVal) )
							{
								//now, phi's first val is not found
								//we research in sub loop of this
								for(auto loopitr = targetLoop->begin(); loopitr != targetLoop->end(); ++loopitr)
								{
									Loop* subLoop = *loopitr;					
									for(unsigned ii=0; ii<PHIN->getNumIncomingValues(); ii++)
									{
										BasicBlock* IncomingBB = PHIN->getIncomingBlock(ii);
										if( IncomingBB->hasName() )//we check name, prepare for the targetLoop that 
																	//doesn't include all of incoming BasicBlock
										{
											if( !(subLoop->contains(IncomingBB)) )
											{	
												OperVal = PHIN->getIncomingValue(ii);
												SrcI->setOperand(i,OperVal);
												// //(debug)
												// errs()<<"####third phi Operand is set to:";
												// OperVal->print(errs()); errs()<<"\n";
												break;
											}			
										}
									}
								}//end of subLoop itr
							}

						}//end of while--------------------------------//
						if( Instruction* opI = dyn_cast<Instruction>(OperVal) )
						{
							//(debug)
							// errs()<<"####fourth trace inst is set to:";
							// OperVal->print(errs()); errs()<<"\n";
							traceInstForUBCalc(opI,TraceVector,
								RegisterMap,initstr,targetLoop, MakeInitialValueZero);
						}
					}//end of operand itr
				}
				break;
			// Memory operators...
			case Instruction::Load:
				if( Instruction* OperI = dyn_cast<Instruction>(SrcI->getOperand(0)) )
				{
					if( targetLoop->contains( OperI ) )
					{
						traceInstForUBCalc( OperI,TraceVector,
							RegisterMap,initstr,targetLoop, MakeInitialValueZero );
						//this is for correspondness with the case that init is phi.
						//If phi, init's instruction is not recorded.
					}					
				}
				break;
			// Other operators...
			case Instruction::PHI:
				if( PHINode* PHIN = dyn_cast<PHINode>(SrcI) )//PHI is one of the end of trace
				{
					//we should get one of IncomingValues which is initial value of loop-counter
					for(unsigned i=0; i<PHIN->getNumIncomingValues(); i++)
					{
						Value* OperVal = PHIN->getIncomingValue(i);
						BasicBlock* IncomingBB = PHIN->getIncomingBlock(i);
						if( IncomingBB->hasName() )//we check name, prepare for the targetLoop that 
													//doesn't include all of incoming BasicBlock
						{						
							if(Instruction* IncomingI = dyn_cast<Instruction>( OperVal ))
							{
								if( !(targetLoop->contains(IncomingBB)) )
								{		
									traceInstForUBCalc(IncomingI,TraceVector,
										RegisterMap,initstr,targetLoop,MakeInitialValueZero);
									return;//we don't record PHI in trace vector
								}else if( dyn_cast<PHINode>( OperVal ) )
								{
									while(PHINode* PHIN = dyn_cast<PHINode>(OperVal)) // phi is one of the end of trace.
									{
										if( PHIN->hasName() && MakeInitialValueZero && 
											RegisterMap.find(initstr)->second->getName().str() == PHIN->getName().str() )
										{
											break;
										}
										//we assume that targetloop is single entry.
										//If there is any problem:
										//Idea1. make phinode before the parallel execution.
										//Idea2. do something.
										for(unsigned ii = 0; ii<PHIN->getNumIncomingValues(); ii++)
										{
											BasicBlock* IncomingBB = PHIN->getIncomingBlock(ii);
											if( IncomingBB->hasName() )
											{
												if( !(targetLoop->contains(IncomingBB)) )
												{
													OperVal = PHIN->getIncomingValue(ii);
													SrcI->setOperand(i,OperVal);
													//(debug)
													// errs()<<"####second phi phi is set to:";
													// OperVal->print(errs()); errs()<<"\n";
													break;
												}
											}
										}
										if( dyn_cast<PHINode>(OperVal) )
										{
											//now, phi's first val is not found
											//we research in sub loop of this
											for(auto loopitr = targetLoop->begin(); loopitr != targetLoop->end(); ++loopitr)
											{
												Loop* subLoop = *loopitr;					
												for(unsigned ii=0; ii<PHIN->getNumIncomingValues(); ii++)
												{
													BasicBlock* IncomingBB = PHIN->getIncomingBlock(ii);
													if( IncomingBB->hasName() )//we check name, prepare for the targetLoop that 
																				//doesn't include all of incoming BasicBlock
													{
														if( !(subLoop->contains(IncomingBB)) )
														{	
															OperVal = PHIN->getIncomingValue(ii);
															SrcI->setOperand(i,OperVal);
															//(debug)
															// errs()<<"####third phi phi is set to:";
															// OperVal->print(errs()); errs()<<"\n";
															break;
														}			
													}
												}
											}//end of subLoop itr
										}

									}//end of while--------------------------------////end of while--------------------------------//
									if( Instruction* opI = dyn_cast<Instruction>(OperVal) )
									{
										// errs()<<"####fourth phi phi trace is set to:";
										// OperVal->print(errs()); errs()<<"\n";
										traceInstForUBCalc(opI,TraceVector,
											RegisterMap,initstr,targetLoop,MakeInitialValueZero);
										return;
									}
								}
							}
						}
					}
					//now, phi's first val is not found
					//we research in sub loop of this
					for(auto loopitr = targetLoop->begin(); loopitr != targetLoop->end(); ++loopitr)
					{
						Loop* subLoop = *loopitr;					
						for(unsigned i=0; i<PHIN->getNumIncomingValues(); i++)
						{
							BasicBlock* IncomingBB = PHIN->getIncomingBlock(i);
							if( IncomingBB->hasName() )//we check name, prepare for the targetLoop that 
														//doesn't include all of incoming BasicBlock
							{
								if( !(subLoop->contains(IncomingBB)) )
								{								
									if(Instruction* IncomingI = dyn_cast<Instruction>(PHIN->getIncomingValue(i)))
									{
										traceInstForUBCalc(IncomingI,TraceVector,
											RegisterMap,initstr,targetLoop,MakeInitialValueZero);	
										return;//we don't record PHI in trace vector
									}
								}			
							}
						}
					}
					errs()<<"phi instruction's first val is not found\n";
					PHIN->dump();

				}
				break;
			default:
				errs()<<"Trace etc: we can't handle it.";
				SrcI->print(errs()); errs()<<"\n";
				return;
		}
	}


	string traceName;
	if(SrcV->hasName())
	{
		traceName = SrcV->getName().str();
	}else{			
		traceName = IntToName(this->instnum++);
		if(!SrcV->getType()->isVoidTy()){
			SrcV->setName(traceName);
			traceName = SrcV->getName().str();
		}
	}
	TraceVector.push_back( make_pair(traceName,SrcV) );

	//(debug)//
	auto last = TraceVector.rbegin();
	errs()<<"Traced Inst:";
	last->second->print(errs());errs()<<"\n";
	//---//
	return;
}


//###################################################################################################################################################################### trace



void ParamGet::ParamGetCommon::buildLoopUB(IRBuilder<> &Builder,Loop* targetLoop)
{
	/* Storing OMPParamMap 
		str				|data
		// "loopvar"	|

		"Init_built"	|	
		"Cond_built"	|
		"Incr_built"	|
		"UB_calc"		|
		"loopvar_copy"	|
		
		//"plower"		|
		//"pupper"		|
		//"pstride"		|
		//"plastiter"	|
	*/
	//Originals' load get

	Value* InitLoad;
	Value* CondLoad;
	Value* IncrLoad;
	//bool CondLHSisCounter;

	//Init_original set
	errs()<<"---InitLoad is Store or PHI or else---\n";
	InitLoad = buildTraceVector(Builder,this->LoopInitTraceVector, targetLoop,
		LoopExpr, "init", 0, 0);	
	//(debug)
	errs()<<"Built InitLoad--> ";
	InitLoad->print(errs());
	errs()<<"\n";

	//Cond_original set
	errs()<<"---CondLoad is ICmp or else---\n";
	CondLoad = buildTraceVector(Builder,this->LoopCondTraceVector, targetLoop,
		LoopExpr, "init", 0, 0);	
	//(debug)
	errs()<<"Built CondLoad--> ";
	CondLoad->print(errs());
	errs()<<"\n";



	//Incr_original set
	errs()<<"---IncrLoad is BinaryOperator or GetElementptr's Idx---\n";
	IncrLoad = buildTraceVector(Builder,this->LoopIncrTraceVector,targetLoop,
		LoopExpr, "init", 1, 1);
	//(debug)
	errs()<<"Built IncrLoad--> ";
	IncrLoad->print(errs());
	errs()<<"\n";
	
	//cast from if any loop counter variables is not same to new UB and LB bitwidth
	InitLoad = castToCreatedKMPCInitSize(Builder,InitLoad);
	OMPParamMap.insert(make_pair("Init_built",InitLoad));
	CondLoad = castToCreatedKMPCInitSize(Builder,CondLoad);
	OMPParamMap.insert(make_pair("Cond_built",CondLoad));
	IncrLoad = castToCreatedKMPCInitSize(Builder,IncrLoad);
	OMPParamMap.insert(make_pair("Incr_built",IncrLoad));
	//Incr_original
	Value* IncrOriginalV = OMPParamMap.find("Incr_original")->second;
	IncrOriginalV = castToCreatedKMPCInitSize(Builder,IncrOriginalV);
	OMPParamMap.find("Incr_original")->second = IncrOriginalV;

	//(debug)
	errs()<<"Thread local Init,Cond,Incr, and original incr:\n";
	InitLoad->print(errs());
	errs()<<"\n";
	CondLoad->print(errs());
	errs()<<"\n";
	IncrLoad->print(errs());
	errs()<<"\n";
	IncrOriginalV->print(errs());
	errs()<<"\n";

	//delete all the built inst from the record of BuiltValMap
	//instructions in init does not update, so we have not to check the deletion
	// eraseAllTheUpdatingValueInTracedVector(LoopInitTraceVector,targetLoop);
	eraseAllTheUpdatingValueInTracedVector(LoopCondTraceVector,targetLoop);
	eraseAllTheUpdatingValueInTracedVector(LoopIncrTraceVector,targetLoop);
}

Value* ParamGet::ParamGetCommon::castToCreatedKMPCInitSize(IRBuilder<> &Builder, Value* srcV)
{
	Type* dstType = Type::getIntNTy(Builder.getContext(), CondOperandIntegerSize);
	unsigned counterWidth = 0;
	//InitLoad	
	counterWidth = srcV->getType()->getIntegerBitWidth();
	if(counterWidth < CondOperandIntegerSize){
		srcV = Builder.CreateSExt(srcV, dstType);
	}else if(counterWidth > CondOperandIntegerSize){
		srcV = Builder.CreateTrunc(srcV, dstType);
	}
	return srcV;
}


void ParamGet::ParamGetCommon::eraseAllTheUpdatingValueInTracedVector(StrVvector &TraceVector, Loop* targetLoop)
{
	for(auto vecpair : TraceVector){
		if(Instruction* vecI = dyn_cast<Instruction>(vecpair.second)){
			if(targetLoop->contains(vecI)){
				errs()<<"Erase traced inst defined out of loop: "<<vecpair.first<<": ";
				vecpair.second->print(errs());
				errs()<<"\n";
				if(BuiltValMap.find(vecpair.first) != BuiltValMap.end()){
					BuiltValMap.erase( BuiltValMap.find(vecpair.first) );
				}
			}
		}else{
			errs()<<"This inst is not Erased from map: "<<vecpair.first<<": ";
			vecpair.second->print(errs());
			errs()<<"\n";
		}
	}
}



//###################################################################################################################################################################### build trace

Value* ParamGet::ParamGetCommon::buildTraceVector(IRBuilder<> &Builder,
		StrVvector &TraceVector, Loop* targetLoop, 
		StrVmap &RegisterMap, string initstr, bool NeedAbsoluteIncrForLoopCounterFlag,
		bool MakeInitialValueZero
		)
{
	Value* returnV = nullptr;
	//bool Ignored = false;
	for(auto vecpair : TraceVector){
		Value* buildingV = vecpair.second;
		string buildingName = vecpair.first;
		errs()<<"building a inst in a vector: original name: "<<buildingName<<": ";
		buildingV->print(errs());
		errs()<<"\n";
		
		//if we are building incr and it is BinaryOperator or CastInst, we should do something
		//else we built the value like others
		if(MakeInitialValueZero){
			Instruction* buildTargetI = dyn_cast<Instruction>(vecpair.second);
			returnV = buildTracedInstructionWithOutLoopCarriedValue(Builder,RegisterMap,initstr,NeedAbsoluteIncrForLoopCounterFlag,targetLoop,buildTargetI,buildingName);
		}else{
		 	returnV = buildTracedValue(Builder,RegisterMap,initstr,buildingV,buildingName);
		}

	}//end of vecpair
	return returnV;
}

Value* ParamGet::ParamGetCommon::buildTracedValue(IRBuilder<> &Builder, StrVmap &RegisterMap, string initstr, Value* buildingV, string buildingName)
{
	// string buildingName = buildingV->getName().str();//this name can't be done for const value
	Value* returnV = nullptr;
	//if it is passed as argument, we use the argument
	//this may only be used for building init of loop carried variable
	if(!buildingName.empty() && FuncArgumentMap.find( buildingName ) != FuncArgumentMap.end()){
		//if the buildingV is specified in some clauses, its name are different from the original one. So we use the thread local name to find the value in BuiltValMap.
		if(ReductionValMap.find(buildingName) != ReductionValMap.end()){
			buildingName = buildingName + "_Arg";
			//we use local Reduction Var for inner loop calc,
			//and use Arg Reduction Var for reduce_nowait
		}else if(FirstPrivateValMap.find(buildingName) != FirstPrivateValMap.end()){
			buildingName = buildingName + ".FirstPrivateArgument";
		}else if(LastPrivateValMap.find(buildingName) != LastPrivateValMap.end()){
			buildingName = buildingName + ".LastPrivateArgument";
		}
		returnV = BuiltValMap.find( buildingName )->second;
		
		//(debug)
		errs()<<"-> It is passed as function argument (shared variable): \n";
		errs()<<"thread local name: "<< buildingName <<": ";
		returnV->print(errs()); 
		errs()<<"\n";
		return returnV;
	}	
	//if it is already built, we use that local value
	else if(!buildingName.empty() &&  BuiltValMap.find(buildingName) != BuiltValMap.end() )
	{
		returnV = BuiltValMap.find(buildingName)->second;
		errs()<<"-> Same name value was found in BuiltValMap: ";
		returnV->print(errs()); errs()<<"\n";
		return returnV;
	}
	//if it is constant, we use it as it is.
	else if(dyn_cast<Constant>(buildingV)){
		BuiltValMap.insert(make_pair(buildingName,buildingV));
		returnV = buildingV;
		//(debug)
		errs()<<"-> it is constant: "<< buildingName <<": ";
		buildingV->print(errs()); errs()<<"\n";
		return returnV;
	}
	//else if it is defined in loop and not yet built,
	//-- instruction handle --//
	else if(Instruction* buildingI = dyn_cast<Instruction>(buildingV))
	{	
		Instruction* loopCarriedInst = dyn_cast<Instruction>(RegisterMap.find(initstr)->second);
		string loopCarriedName = loopCarriedInst->getName().str();
		unsigned loopCarriedOpcode = loopCarriedInst->getOpcode();
		unsigned buildingOpcode = buildingI->getOpcode();
		if(loopCarriedOpcode == buildingOpcode && loopCarriedOpcode == Instruction::Load){
			Value* loopCarriedMemory = loopCarriedInst->getOperand(0);
			Value* buildingMemory = buildingI->getOperand(0);
			//if it is loading same memory, we use the inst which is already built
			if( buildingMemory->hasName() && buildingMemory->getName() == loopCarriedMemory->getName() ){
				//we use the built one
				// this initial pointer set is for constant init value
				Value* localV = nullptr;
				if( OMPParamMap.find("Init_built") != OMPParamMap.end() ){
					localV = OMPParamMap.find("Init_built")->second;
				}
				else if(BuiltValMap.find(loopCarriedName) != BuiltValMap.end()){
					localV = BuiltValMap.find(loopCarriedName)->second;
				}
				else{
					localV = buildInstruction(Builder, buildingI);
				}
				BuiltValMap.insert(make_pair(buildingName, localV));
				returnV = localV;

				//debug
				errs()<<"-> Load inst is replaced with the inst that are already built: ";
				errs()<<buildingName<<": ";
				returnV->print(errs()); 
				errs()<<"\n";
				return returnV;
			}
		}
		returnV = buildInstruction(Builder, buildingI);
	}
	//-- instruction handle END --//
	errs()<<"[ParamGet] We can't build this value: ";
	returnV->print(errs());
	errs()<<"\n";
	return returnV;
}


Value* ParamGet::ParamGetCommon::buildTracedInstructionWithOutLoopCarriedValue(IRBuilder<> &Builder, StrVmap &RegisterMap, string initstr, bool NeedAbsoluteIncrForLoopCounterFlag, Loop* targetLoop, Instruction* buildingI, string buildingName)
{
	// string buildingName = buildingI->getName().str();//this inst can't handle const value

	//if loop carried inst is pointer, we do something
	if(GetElementPtrInst* GEP = dyn_cast<GetElementPtrInst>(RegisterMap.find(initstr)->second)){
		Value* buildingIncrV;
		Value* IndexVal = *(GEP->idx_begin());
		if(IndexVal->hasName() && buildingName == IndexVal->getName().str()){
			if(BuiltValMap.find(buildingName) != BuiltValMap.end()){
				buildingIncrV = BuiltValMap.find(buildingName)->second;
			}else{
				buildingIncrV = buildInstruction(Builder, buildingI);
			}
			//GEP only accepts indice whiche are integer type
			Value* ConstZeroVal = Builder.getIntN( buildingIncrV->getType()->getIntegerBitWidth(), 0);
			replaceIncValWithAbsVal(Builder,
				/* increment binary operation */buildingI,
				/* increment value */buildingIncrV,
				/* constant 0 used for creating absolute val */ConstZeroVal);

			if(BuiltValMap.find(buildingName) != BuiltValMap.end()){
				return BuiltValMap.find(buildingName)->second;
			}else{
				errs()<<"[ParamGet] Building Error of pointer type increment\n";buildingIncrV->print(errs());
				errs()<<"\n";
			}
		}
	}
	//detect loop carring side
	//operand number is necessary for the case of replacement
	Value* initV = RegisterMap.find(initstr)->second;
	errs()<<"loopCarriedName: "<<initV->getName().str()<<"\n";
	Value* loopCarriedV = isIncludingLoopCarriedVariable(buildingI,initV);
	if(loopCarriedV != nullptr){					
		//get constant number of 0
		Value* ConstZeroVal = nullptr;
		Type* opTy = loopCarriedV->getType();
		if(opTy->isIntegerTy()){
			ConstZeroVal = Builder.getIntN(opTy->getIntegerBitWidth(), 0);
		}else if( opTy->isDoubleTy() || opTy->isFloatTy() ){
			ConstZeroVal = ConstantFP::get(opTy, (double)0);
		}
		//debug
		if(ConstZeroVal == nullptr){
			errs()<<"[ParamGet] We can't prepare 0 for this type: ";
			loopCarriedV->print(errs());
			errs()<<"\n";
		}

		if(dyn_cast<BinaryOperator>(buildingI)){
			if( NeedAbsoluteIncrForLoopCounterFlag ){
					//if incr is negative value, or incremented with sub inst, we obtain absolute value. 
					//buildingI is inserted to BuiltValMap with absolute incr val.
					//we replace the value using the operands that is not the loop counter (which means it's increment value)
					Value* incV;
					for(Value* opV : buildingI->operands()){
						if(opV->hasName() && opV->getName() != loopCarriedV->getName()){
							incV = BuiltValMap.find(opV->getName().str())->second;
							break;
						}else if(!opV->hasName() && dyn_cast<ConstantInt>(opV)){
							incV = opV;
						}else{
							errs()<<"[ParamGet] we can't get increment value for this inst: ";
							buildingI->print(errs());
							errs()<<"\n";
						}
					}
					//debug
					errs()<<"replace incr val with abs val for this BO: ";
					buildingI->print(errs());
					errs()<<"\n";
					errs()<<"incrementing value: ";
					incV->print(errs());
					errs()<<"\n";
					replaceIncValWithAbsVal(Builder,buildingI,incV,ConstZeroVal);	
			}
		}else{
			Value* replaceVal = ConstZeroVal;
			Instruction* cloneI = buildingI->clone();
			cloneI->replaceUsesOfWith(loopCarriedV, replaceVal);
			cloneI->setName(buildingName);
			buildInstruction(Builder, cloneI);

			//erase clone (builtval will be left)
			cloneI->replaceAllUsesWith( UndefValue::get(cloneI->getType()) );
			cloneI->deleteValue();
		}		
		if(BuiltValMap.find(buildingName) != BuiltValMap.end()){
			return BuiltValMap.find(buildingName)->second;
		}else{
			errs()<<"[ParamGet] Error building incr vector of loop carried variables: ";
			buildingI->print(errs());			
			errs()<<"\n";
		}
	}
	errs()<<"Loop carried variable not found in this inst's operand. We just build it like other insts: ";
	buildingI->print(errs());
	errs()<<"\n";
	
	return buildTracedValue(Builder,RegisterMap,initstr,buildingI,buildingName);
}



Value* ParamGet::ParamGetCommon::isIncludingLoopCarriedVariable(Instruction* checkingI, Value* carriedV)
{
	string loopCarriedName = carriedV->getName().str();
	for(Value* opV : checkingI->operands()){
		if(opV->hasName() && opV->getName().str() == loopCarriedName){
			errs()<<"The checking inst includes this loop counter: ";
			opV->print(errs());
			errs()<<"\n";
			return opV;
		}else if(Instruction* opI = dyn_cast<Instruction>(opV)){
			if(Instruction* carriedI = dyn_cast<Instruction>(carriedV)){
				if(	opI->getOpcode() == Instruction::Load && 
						carriedI->getOpcode() == Instruction::Load
				){
					Value* LoadMemory =  opI->getOperand(0);
					Value* loopCarriedMemory = carriedI->getOperand(0);
					if(	LoadMemory->hasName() && loopCarriedMemory->hasName() && 		
							LoadMemory->getName() == loopCarriedMemory->getName()
					){
						errs()<<"The checking load inst loads the loop counter: ";
						opV->print(errs());
						errs()<<"\n";
						return opV;
					}
				}
			}
			else if(opI->getOpcode() == Instruction::PHI){
				continue;
			}
			else if(dyn_cast<BinaryOperator>(opV)){
				errs()<<"Other binary operator found. We don't trace this inst because we assume that loop counter have already been replaced at the time this inst was built: ";
				opV->print(errs());
				errs()<<"\n";
				continue;
			}
			//
			if(isIncludingLoopCarriedVariable(opI, carriedV) != nullptr){
				return opV;
			}
		}
	}
	errs()<<"Loop carried variable not found in this instruction.\n";
	return nullptr;
}


void ParamGet::ParamGetCommon::replaceIncValWithAbsVal(IRBuilder<> &Builder, 
		Instruction* buildTargetI, Value* IncrVal, Value* ConstZeroVal)
{		
	bool IncrIsGEPFlag=false;
	if(dyn_cast<GetElementPtrInst>(LoopExpr.find("incr")->second)){
		IncrIsGEPFlag = true;
	}

	if(CmpInst* CI = dyn_cast<CmpInst>(LoopExpr.find("cond")->second))
	{
		OMPParamMap.insert(make_pair(
			"Incr_original", IncrVal
			));
		Value* AbsVal = IncrVal;
		unsigned buildOpcode = buildTargetI->getOpcode();
		//this swich set Absolute of Incr
		switch(CI->getPredicate())
		{
			case CmpInst::Predicate::ICMP_UGT:// >
			case CmpInst::Predicate::ICMP_SGT:// >
			case CmpInst::Predicate::ICMP_UGE:// >=
			case CmpInst::Predicate::ICMP_SGE:// >=
				//if add or ptr, below cases will be changed from the original incrementation
				if(buildOpcode == Instruction::Add || buildOpcode == Instruction::FAdd ||IncrIsGEPFlag)
				{
					if(	(CondLHSisCounter && CondTrueIsIterate) || // i > N 
							(!CondLHSisCounter && !CondTrueIsIterate) // !(N > i)
					){
						AbsVal = Builder.CreateNSWSub(ConstZeroVal,IncrVal);
					}
				}else if(buildOpcode == Instruction::Sub || buildOpcode == Instruction::FSub)
				{
					if(	(CondLHSisCounter && !CondTrueIsIterate) || // !(i > N)
							(!CondLHSisCounter && CondTrueIsIterate) // N > i
					){
						AbsVal = Builder.CreateNSWSub(ConstZeroVal,IncrVal);
					}
				}
				break;
			case CmpInst::Predicate::ICMP_ULT:// <
			case CmpInst::Predicate::ICMP_SLT:// <
			case CmpInst::Predicate::ICMP_ULE:// <=
			case CmpInst::Predicate::ICMP_SLE:// <=
				//if add or ptr, below cases will be changed from the original incrementation
				if(buildOpcode == Instruction::Add || buildOpcode == Instruction::FAdd ||IncrIsGEPFlag)
				{
					if(	(CondLHSisCounter && !CondTrueIsIterate) || // !(i < N)
							(!CondLHSisCounter && CondTrueIsIterate) // N < i
					){
						AbsVal = Builder.CreateNSWSub(ConstZeroVal,IncrVal);
					}
				}else if(buildOpcode == Instruction::Sub || buildOpcode == Instruction::FSub)
				{
					if(	(CondLHSisCounter && CondTrueIsIterate) || // i < N 
							(!CondLHSisCounter && !CondTrueIsIterate) // !(N < i)
					){
						AbsVal = Builder.CreateNSWSub(ConstZeroVal,IncrVal);
					}
				}
				break;
			case CmpInst::Predicate::ICMP_EQ:
			case CmpInst::Predicate::ICMP_NE:
				if( dyn_cast<ConstantInt>( IncrVal ) )
				{
					const APInt &incrAPI = dyn_cast<ConstantInt>( IncrVal )->getValue();
					if(incrAPI.isNegative()){
						AbsVal = dyn_cast<Value>(Builder.getInt(0 - incrAPI));
					}
				}else 
				{
					errs()<<"[ParamGet] cond's Predicate is EQ, POINTER\n";
					errs()<<"\tEQ is Only allowed if UB and LB (CondAddr and InitAddr) can cast to ConstantInt\n";
				}
				break;
			default:
				break;
		}
		if( BuiltValMap.find( buildTargetI->getName().str() ) == BuiltValMap.end() )
		{
			BuiltValMap.insert(make_pair(buildTargetI->getName().str(), AbsVal));
		}else{
			BuiltValMap.find(buildTargetI->getName().str())->second = AbsVal;
		}
		//AbsVal is used but Incr_absolute is not used
		// OMPParamMap.insert(make_pair(
		// 	"Incr_absolute", AbsVal
		// 	));
		errs()<<"Incr_absolute created :"<<buildTargetI->getName().str()<<": val\t";
		BuiltValMap.find( buildTargetI->getName().str() )->second->print(errs()); errs()<<"\n";	
		//--------------------
	}
}



//###################################################################################################################################################################### set InitIsBiggerFlag

void ParamGet::ParamGetCommon::judgeInitIsBigger(IRBuilder<> &Builder, 
		bool &InitIsBiggerFlag, bool &IncludeEqualFlag)
{
	InitIsBiggerFlag=true;
	IncludeEqualFlag=true;
	// //(debug)
	// errs()<<"InitLoad: ";
	// InitLoad->print(errs());
	// errs()<<"\n";
	// errs()<<"CondLoad: ";
	// CondLoad->print(errs());
	// errs()<<"\n";
	// errs()<<"IncrLoad: ";
	// IncrLoad->print(errs());
	// errs()<<"\n";

	if(CmpInst* CI = dyn_cast<CmpInst>(LoopExpr.find("cond")->second))
	{
		//determine the UB and LB
		switch(CI->getPredicate())
		{
			case CmpInst::Predicate::ICMP_UGT:// >
			case CmpInst::Predicate::ICMP_SGT:// >
				IncludeEqualFlag=false;
				//fall through
			case CmpInst::Predicate::ICMP_UGE:// >=
			case CmpInst::Predicate::ICMP_SGE:// >=		
				if(	(CondLHSisCounter && !CondTrueIsIterate) ||
						(!CondLHSisCounter && CondTrueIsIterate)
				){
					//smaller side is upper bounds
					InitIsBiggerFlag=false;
				}
				break;
			case CmpInst::Predicate::ICMP_ULT:// <
			case CmpInst::Predicate::ICMP_SLT:// <
				IncludeEqualFlag=false;	
				//fall through
			case CmpInst::Predicate::ICMP_ULE:// <=
			case CmpInst::Predicate::ICMP_SLE:// <=		
				if(	(!CondLHSisCounter && !CondTrueIsIterate) ||
						(CondLHSisCounter && CondTrueIsIterate)
				){
					//smaller side is upper bounds
					InitIsBiggerFlag=false;
				}
				break;
			case CmpInst::Predicate::ICMP_EQ:// ==
			case CmpInst::Predicate::ICMP_NE:// !=
				IncludeEqualFlag=false;
				if(ConstantInt* ConstI = dyn_cast<ConstantInt>(OMPParamMap.find("Incr_original")->second)){
					const APInt &IncrAPI = ConstI->getValue();
					if( Instruction* IncrI = dyn_cast<Instruction>(LoopExpr.find("incr")->second) )
					{
						//get loop counter's binary operator
						Instruction* IncrementingI = getCounterIncrementingInst(IncrI);
						unsigned incrOpcode = IncrementingI->getOpcode();
						if(	incrOpcode == Instruction::Add ||
								incrOpcode == Instruction::FAdd ||
								incrOpcode == Instruction::GetElementPtr
						){
							if(!IncrAPI.isNegative()){
								//In increment, this program adds negative value
								//that means, loop counter decrements
								//So we assume that InitLoad is UB	
								//New cond's predicate will be sge or uge
								// UBCalc = CondLoad;
								// LBCalc = InitLoad;
								InitIsBiggerFlag=false;
							}
						}else if(	incrOpcode == Instruction::Sub ||
											incrOpcode == Instruction::FSub 
						){
							if(IncrAPI.isNegative()){
								//In increment, this program subs negative value
								//that means, loop counter increments
								//So we assume that InitLoad is LB	
								//New cond's predicate will be sge or uge
								// UBCalc = CondLoad;
								// LBCalc = InitLoad;
								InitIsBiggerFlag=false;
							}
						}else{
							errs()<<"[ParamGet] cond's Predicate is not Add or Sub,\n";
							errs()<<"\tSorry we can't determine whether InitLoad is UB or CondLoad is UB\n";
						}
					}
				}else
				{
					errs()<<"[ParamGet] cond's Predicate is EQ or NE,\n";
					errs()<<"\tThese predicate are Only allowed if the original incr can cast to ConstantInt\n";
				}
				break;
			default:
				break;
		}
		// errs()<<"LoopCounter is int\n";
		// errs()<<"\tSubVal is \t";
		// SubVal->print(errs());errs()<<"\n";
		// errs()<<"\tUB-LB result is \t";
		// result->print(errs());errs()<<"\n";
	}

	return;
}	

Instruction* ParamGet::ParamGetCommon::getCounterIncrementingInst(Instruction* startI)
{
	errs()<<"search counter incrementing BO: ";
	startI->print(errs());
	errs()<<"\n";

	for(Value* opV : startI->operands()){
		if(isLoopCounter(opV)){
			return startI;
		}
		//
		if(Instruction* opI = dyn_cast<Instruction>(opV)){
			Value* returnedV = getCounterIncrementingInst(opI);
			if(!dyn_cast<BinaryOperator>(returnedV)){
				return opI;
			}
		}
	}
	return nullptr;
}

//######################################################################################################################################################################



void ParamGet::ParamGetCommon::captureForIntrinsicMetadata(Module &M, IRBuilder<> &Builder, Loop* targetLoop)
{
	for(auto institr = CapturedInstVector.begin(); institr != CapturedInstVector.end(); ++institr)
	{	
		Instruction* I = institr->second;
		// I->print(errs());errs()<<"\n";

		if(IntrinsicInst* II = dyn_cast<IntrinsicInst>(I))
		{
			errs()<<"---Capture intrinsic---\n";
			switch (II->getIntrinsicID())
			{
				case Intrinsic::directive:
					errs()<<"detected metadata: ";
					for(auto argitr = II->arg_begin();argitr != II->arg_end();++argitr)
					{
						//get Argument of Intrinsic Function
						Value* arg = argitr->get();
						arg->print(errs());errs()<<"\n";

						//get Metadata Node
						MDNode* MDN = dyn_cast<MDNode>(dyn_cast<MetadataAsValue>(arg)->getMetadata());

						//iterate Metadata
						for(auto mditr = MDN->op_begin(); mditr != MDN->op_end(); ++mditr)
						{
							Metadata* MD = mditr->get();
							MD->print(errs());errs()<<"\n";

							//handle String Metadata
							if( MDString* MDS = dyn_cast<MDString>(MD) )
							{
								string metastr = MDS->getString();	
								//(debug)
								errs()<<"handling: " << metastr << "\n";
								handleClauses(M,Builder,metastr,targetLoop,II);
							}
						}
				     /**/
					}//end of argitr
					break;
				default:
					break;
			}//end of switch
			return;
		}//end of IntrinsicInst IF
	}

			// }//end of institr
	// 	}//end of bbitr
	// }//end of funcitr
}
void ParamGet::ParamGetCommon::handleClauses(Module &M, IRBuilder<> &Builder,
											string metastr, Loop* targetLoop, Instruction* II)
{
	//firstprivate and lastprivate must be handled before private
	//because find("private") return true both of them
	//handle firstprivate clause

	size_t start = metastr.find("firstprivate");
	if( start != string::npos ) //then private clause exists.
	{
		vector<string>	FirstPrivateStrVector;
		//(debug)
		errs()<<"private clause start:"<<start<<"\n";

		//erase "private("
		metastr.erase(
			metastr.begin()+start,
			metastr.begin()+start+string("firstprivate(").length()
		);

		string varstr;
		for(char ch: metastr)
		{
			if( (ch == ' ') )
			{
				continue;
			}
			if( (ch == ',')|(ch == ')') )
			{
				if(!varstr.empty())
				{
					FirstPrivateStrVector.push_back(varstr);
				}
				varstr.clear();
			}else
			{
				varstr += ch;
			}
		}
		//string after ')' will be private var name
		if(!varstr.empty())
		{
			FirstPrivateStrVector.push_back(varstr);
		}

		// //(debug)
		for(auto stritr = FirstPrivateStrVector.begin(); 
			stritr != FirstPrivateStrVector.end(); ++stritr)
		{
			errs()<<"varitr:";
			errs()<< *stritr <<"\n";
		}

		//insert private var to PrivateValMap
		//and erase from FuncArgumentMap
		for(auto stritr = FirstPrivateStrVector.begin(); 
			stritr != FirstPrivateStrVector.end(); ++stritr)
		{
			auto valitr = FuncArgumentMap.find( *stritr );
			if(valitr != FuncArgumentMap.end())
			{
				FirstPrivateValMap.insert(make_pair(
					valitr->first,
					valitr->second
					)
				);
			}else
			{
				errs()<<"sorry we can't find firstprivate var:"<<*stritr<<"\n";
				//insert dummy
				GlobalVariable* GV = M.getGlobalVariable(*stritr);
				if(GV)
				{
					errs()<<"OK: this Value is Global\n";
					Value* GVArg = dyn_cast<Value>(GV);
					FirstPrivateValMap.insert(make_pair(
						*stritr,
						GVArg
						)
					);
					FuncArgumentMap.insert(make_pair(
						*stritr,
						GVArg
					));
				}else
				{
					bool foundflag = false;
					Value* priV;
					if( CapturedValMap.find( *stritr ) != CapturedValMap.end() )
					{
						priV = CapturedValMap.find( *stritr )->second;
						foundflag = true;
					}else
					{
						Function* CallerFunc = II->getParent()->getParent();
						for(auto fargitr = CallerFunc->arg_begin(); fargitr != CallerFunc->arg_end(); ++fargitr)
						{
							Value* fargV = dyn_cast<Value>(&*fargitr);//func argument's value
							if( fargV->hasName() && fargV->getName().str() == *stritr )
							{
								priV = fargV;
								foundflag = true;
							}
						}
					}
					if(foundflag)
					{
						FirstPrivateValMap.insert(make_pair(
							*stritr,
							priV
							)
						);
						FuncArgumentMap.insert(make_pair(
							*stritr,
							priV
						));
					}else
					{
						errs()<<"####### directed Value name:"<< *stritr <<" is not found ########\n";
					}
				}										
			}
			if(true)
			{
				Value* priV = FuncArgumentMap.find(*stritr)->second;
				Type* priTy = priV->getType();
				//(debug)
				errs()<<"This firstprivate value is not shared by default\n";
				errs()<<"\t";
				priV->print(errs()); errs()<<"\n";

				//this section create alloca in caller func of outlined func
				/*
				1.if the value is pointer to scalar, we don't create alloca
				2.if the value is pointer to pointer, we create alloca
				3.the difference between 1. and 2, is solved at insertForOMPVarAndArgument function
				*/
				if( dyn_cast<Argument>(priV) && dyn_cast<PointerType>(priTy) )
					//we specially handle Argument
					//this is because Argument is actually a load of alloca
				{
					switch(priTy->getPointerElementType()->getTypeID())
					{
						//if composite type, we pass as it is.
						case Type::TypeID::StructTyID:
						case Type::TypeID::ArrayTyID:
						case Type::TypeID::VectorTyID:
							break;
						default:
							if(true){
								errs()<<"we create new alloca for this value\n";
								//create alloca in original func.
								unsigned align = calcAlignOfType(priTy);
								Function* CallerFunc = II->getParent()->getParent();
								BasicBlock* firstBB = &*(CallerFunc->begin());
								Instruction* firstI = &*(firstBB->begin());
								Builder.SetInsertPoint(firstI);
								AllocaInst* AI = Builder.CreateAlloca(priTy);
								AI->setAlignment(align);//this pointer type is 64bits 
								if(Value* AllocaVal = dyn_cast<Value>(AI))
								{
									//store first value to alloca
									Builder.SetInsertPoint(II);
									Builder.CreateAlignedStore(
										priV,
										AllocaVal,
										align
									);
									//Function argument is replaced with alloca of the firstprivate variable
									FuncArgumentMap.find(*stritr)->second = AllocaVal;
								}							
							}
							break;
					}

				}
			}	


			//(debug)
			// errs()<<"This value is not alloca or global.\n";
			// errs()<<"\t";
			// FirstPriArg->print(errs()); errs()<<"\n";

			//(debug)
			errs()<<"FirstPrivate: "<<*stritr<<"\t as Argument: ";
			FuncArgumentMap.find(*stritr)->second->print(errs());errs()<<"\n";

		}
	}


	//handle lastprivate clause
	start = metastr.find("lastprivate");//incomplete
	if( start != string::npos ) //then private clause exists.
	{
		vector<string>	LastPrivateStrVector;
		//(debug)
		errs()<<"private clause start:"<<start<<"\n";

		//erase "private("
		metastr.erase(
			metastr.begin()+start,
			metastr.begin()+start+string("lastprivate(").length()
		);

		string varstr;
		for(char ch: metastr)
		{
			if( (ch == ' ') )
			{
				continue;
			}
			if( (ch == ',')|(ch == ')') )
			{
				if(!varstr.empty())
				{
					LastPrivateStrVector.push_back(varstr);
				}
				varstr.clear();
			}else
			{
				varstr += ch;
			}
		}
		//string after ')' will be private var name
		if(!varstr.empty())
		{
			LastPrivateStrVector.push_back(varstr);
		}

		// //(debug)
		for(auto stritr = LastPrivateStrVector.begin(); 
			stritr != LastPrivateStrVector.end(); ++stritr)
		{
			errs()<<"varitr:";
			errs()<< *stritr <<"\n";
		}

		//insert private var to PrivateValMap
		//and erase from FuncArgumentMap
		StrVmap AllocaMap;
		for(auto stritr = LastPrivateStrVector.begin(); 
			stritr != LastPrivateStrVector.end(); ++stritr)
		{
			if(FirstPrivateValMap.find(*stritr) != FirstPrivateValMap.end())
			{
				LastPrivateValMap.insert(make_pair(
					*stritr,
					FirstPrivateValMap.find(*stritr)->second
					)
				);

				//(debug)
				errs()<<"LastPrivate:"<< *stritr;
				FirstPrivateValMap.find(*stritr)->second->print(errs());errs()<<"\n";
			}else if(FuncArgumentMap.find( *stritr ) != FuncArgumentMap.end())
			{
				auto valitr = FuncArgumentMap.find( *stritr );
				//if lastprivate is found in argument, we assume that 
				//this variable is defined before the loop, so this is pointer and used 
				//with load instruction
				LastPrivateValMap.insert(make_pair(
					valitr->first,
					valitr->second
					)
				);

				//(debug)
				errs()<<"LastPrivate:"<<valitr->first;
				(valitr->second)->print(errs());errs()<<"\n";
				if( !dyn_cast<PointerType>( valitr->second->getType() ) )
				{
					errs()<<"Lastprivate which appears in arugment must be pointer.\n";
					valitr->second->dump();
				}
			}else
			{
				//insert dummy
				GlobalVariable* GV = M.getGlobalVariable(*stritr);
				if(GV)
				{
					errs()<<"OK: this Value is Global\n";
					Value* GVArg = dyn_cast<Value>(GV);
					LastPrivateValMap.insert(make_pair(
						*stritr,
						GVArg
						)
					);
					FuncArgumentMap.insert(make_pair(
						*stritr,
						GVArg
					));
				}else
				{
					bool foundflag = false;
					Value* priV;
					if( CapturedValMap.find( *stritr ) != CapturedValMap.end() )
					{
						errs()<<"this is defined in loop, so we create alloca to return result\n";
						Value* priV = CapturedValMap.find(*stritr)->second;
						LastPrivateValMap.insert(make_pair(
							*stritr,
							priV
							)
						);
						Function* CallerFunc = II->getParent()->getParent();
						BasicBlock* firstBB = &*(CallerFunc->begin());
						Instruction* firstI = &*(firstBB->begin());
						Builder.SetInsertPoint(firstI);

						Type* priTy = priV->getType();
						AllocaInst* AI = Builder.CreateAlloca(priTy);
						AI->setAlignment( calcAlignOfType(priTy) );
						AI->setName(*stritr + ".LastReturnAddress");

						LastPrivateValMap.insert(make_pair(
							*stritr,
							priV
							)
						);
						FuncArgumentMap.insert(make_pair(
							*stritr,
							dyn_cast<Value>(AI)
						));	
						foundflag = true;

					}else
					{
						Function* CallerFunc = II->getParent()->getParent();
						for(auto fargitr = CallerFunc->arg_begin(); fargitr != CallerFunc->arg_end(); ++fargitr)
						{
							Value* fargV = dyn_cast<Value>(&*fargitr);//func argument's value
							if( fargV->hasName() && fargV->getName().str() == *stritr )
							{
								priV = fargV;
								LastPrivateValMap.insert(make_pair(
									*stritr,
									priV
									)
								);
								FuncArgumentMap.insert(make_pair(
									*stritr,
									priV
								));
								foundflag = true;
								break;
							}
						}
					}
					if( foundflag == false )
					{
						errs()<<"this is not defined in loop, so we search from function's alloca\n";
						errs()<<"and we check whether the the storing value to this var is defined in loop or not \n";
						errs()<<"if in loop, we create lastprivate alloca for the storing value and return the lastprivate value to the allca\n";
						bool LoopReachFlag = false;
						Instruction* directedI;
						Function* CallerFunc = II->getParent()->getParent();
						//find directed value
						for(auto bbitr = CallerFunc->begin(); bbitr != CallerFunc->end(); ++bbitr )
						{
							BasicBlock* BB = &(*bbitr);
							for(auto institr = BB->begin(); institr != BB->end(); ++institr)
							{
								Instruction* I = &(*institr);
								//I->print(errs()); errs()<<"\n";
								if( I->hasName() )
								{
									if( I->getName().str() == *stritr )
									{
										directedI = I;
										foundflag = true;
										break;
									}
									if(targetLoop->contains(I))
									{
										LoopReachFlag = true;
										break;
									}
								}
							}
							if(LoopReachFlag || foundflag)
							{
								break;
							}
						}

						if(LoopReachFlag)
						{
							errs()<<"####### directed Value name:"<< *stritr <<" is not found ########\n";	
						}else if(foundflag)
						{
							//(debug)
							errs()<<"we found the directed var: ";
							directedI->print(errs()); errs()<<"\n";
							for(auto usritr = directedI->user_begin(); usritr != directedI->user_end(); ++usritr )
							{
								if(StoreInst* SI = dyn_cast<StoreInst>(*usritr))
								{
									priV = SI->getOperand(0);
									//we search the Store whose src is defined in loop
									if(priV->hasName() &&
										CapturedValMap.find(priV->getName().str()) != CapturedValMap.end() )
									{
										errs()<<"we create alloca for this storing value: ";
										priV->print(errs()); errs()<<"\n";

										Function* CallerFunc = II->getParent()->getParent();
										BasicBlock* firstBB = &*(CallerFunc->begin());
										Instruction* firstI = &*(firstBB->begin());
										Builder.SetInsertPoint(firstI);

										Type* priTy = priV->getType();
										AllocaInst* AI = Builder.CreateAlloca(priTy);
										AI->setAlignment( calcAlignOfType(priTy) );
										AI->setName(*stritr + ".LastReturnAddress");//this is only appear in IR

										//*stritr is modified here
										*stritr = *stritr + ".LastReturnValue";
										priV->setName(*stritr);
										LastPrivateValMap.insert(make_pair(
											*stritr,
											priV
											)
										);
										FuncArgumentMap.insert(make_pair(
											*stritr,
											dyn_cast<Value>(AI)
										));
										break;
									}
								}
							}
							if( (*stritr).empty() )
							{
								errs()<<"####### directed Value name:"<< *stritr <<" is not Stored ########\n";
							}
						}
					}
				}										
			}

			//this section judge whether create alloca for the detected var or not
			if( LastPrivateValMap.find(*stritr) != LastPrivateValMap.end() )
			{
				Value* priV = FuncArgumentMap.find(*stritr)->second;
				Type* priTy = priV->getType();
				//(debug)
				errs()<<"This lastprivate value is not shared by default\n";
				errs()<<"\t";
				priV->print(errs()); errs()<<"\n";

				//this section create alloca in caller func of outlined func
				/*
				1.if the value is pointer to scalar, we don't create alloca
				2.if the value is pointer to pointer, we create alloca
				3.the difference between 1. and 2, is solved at insertForOMPVarAndArgument function
				*/
				if( dyn_cast<Argument>(priV) && dyn_cast<PointerType>(priTy) )
					//we specially handle Argument
					//this is because Argument is actually a load of alloca
				{
					switch(priTy->getPointerElementType()->getTypeID())
					{
						//if composite type, we pass as it is.
						case Type::TypeID::StructTyID:
						case Type::TypeID::ArrayTyID:
						case Type::TypeID::VectorTyID:
							break;
						default:
							if(true){
								errs()<<"we create new alloca for this value\n";
								//create alloca in original func.
								unsigned align = calcAlignOfType(priTy);
								Function* CallerFunc = II->getParent()->getParent();
								BasicBlock* firstBB = &*(CallerFunc->begin());
								Instruction* firstI = &*(firstBB->begin());
								Builder.SetInsertPoint(firstI);
								AllocaInst* AI = Builder.CreateAlloca(priTy);
								AI->setAlignment(align);//this pointer type is 64bits 
								if(Value* AllocaVal = dyn_cast<Value>(AI))
								{
									//store first value to alloca
									Builder.SetInsertPoint(II);
									Builder.CreateAlignedStore(
										priV,
										AllocaVal,
										align
									);
									//Function argument is replaced with alloca of the firstprivate variable
									FuncArgumentMap.find(*stritr)->second = AllocaVal;
								}							
							}
							break;
					}

				}

				// Value* priV = LastPrivateValMap.find(*stritr)->second;
				// Type* priTy = priV->getType();
				// //(debug)
				// errs()<<"This lastprivate value: "<< *stritr <<"\n";
				// errs()<<"\t";
				// priV->print(errs()); errs()<<"\n";

				// //this section create alloca in caller func of outlined func
				// //we create alloca only for variables who are not allocated in original func
				// if( !dyn_cast<AllocaInst>(priV) && !dyn_cast<GlobalVariable>(priV) )
				// {
				// 	bool allocaflag = true;
				// 	// if( dyn_cast<PointerType>(priV->getType()) )
				// 	// {
				// 	// 	//if 
				// 	// 	allocaflag = false;			
				// 	// }
				// 	//scalar and pointer which doesn't point composite type is handled here
				// 	if(allocaflag)
				// 	{
				// 		errs()<<"we create new alloca for this value\n";
				// 		//create alloca in original func.
				// 		unsigned align = calcAlignOfType(priTy);
				// 		Function* CallerFunc = II->getParent()->getParent();
				// 		BasicBlock* firstBB = &*(CallerFunc->begin());
				// 		Instruction* firstI = &*(firstBB->begin());
				// 		Builder.SetInsertPoint(firstI);
				// 		AllocaInst* AI = Builder.CreateAlloca(priTy);
				// 		AI->setAlignment(align);//this pointer type is 64bits 
				// 		if(Value* AllocaVal = dyn_cast<Value>(AI))
				// 		{
				// 			if( FirstPrivateValMap.find(*stritr) != FirstPrivateValMap.end() )
				// 			{
				// 				Builder.SetInsertPoint(II);
				// 				Builder.CreateAlignedStore(
				// 					FirstPrivateValMap.find(*stritr)->second,
				// 					AllocaVal,
				// 					align
				// 				);
				// 			}
				// 			//Function argument is replaced with alloca of the firstprivate variable
				// 			FuncArgumentMap.find(*stritr)->second = AllocaVal;
				// 		}
				// 	}
				// }
			}
		}							
	}


	//handle private clause
	start = metastr.find("private");
	if( start != string::npos ) //then private clause exists.
	{
		vector<string>	PrivateStrVector;
		//(debug)
		errs()<<"private clause start:"<<start<<"\n";

		//erase "private("
		metastr.erase(
			metastr.begin()+start,
			metastr.begin()+start+string("private(").length()
		);

		string varstr;
		for(char ch: metastr)
		{
			if( (ch == ' ') )
			{
				continue;
			}
			if( (ch == ',')|(ch == ')') )
			{
				if(!varstr.empty())
				{
					PrivateStrVector.push_back(varstr);
				}
				varstr.clear();
			}else
			{
				varstr += ch;
			}
		}
		//string after ')' will be private var name
		if(!varstr.empty())
		{
			PrivateStrVector.push_back(varstr);
		}

		// //(debug)
		for(auto stritr = PrivateStrVector.begin(); 
			stritr != PrivateStrVector.end(); ++stritr)
		{
			errs()<<"varitr:";
			errs()<< *stritr <<"\n";
		}

		//insert private var to PrivateValMap
		//and erase from FuncArgumentMap
		for(auto stritr = PrivateStrVector.begin(); 
			stritr != PrivateStrVector.end(); ++stritr)
		{
			auto valitr = FuncArgumentMap.find( *stritr );
			if( (FirstPrivateValMap.find(*stritr) != FirstPrivateValMap.end()) ||
			  (LastPrivateValMap.find(*stritr) != LastPrivateValMap.end()) )
			{
				continue;
			}
			if(valitr != FuncArgumentMap.end())
			{
				PrivateValMap.insert(make_pair(
					valitr->first,
					valitr->second
					)
				);	
				FuncArgumentMap.erase(valitr);

				//(debug)
				errs()<<"Private:"<<valitr->first;
				(valitr->second)->print(errs());errs()<<"\n";
			}else
			{
				errs()<<"sorry we can't find private var:"<<*stritr<<"\n";
				//insert dummy
				GlobalVariable* GV = M.getGlobalVariable(*stritr);
				if(GV)
				{
					errs()<<"OK: this Value is Global\n";
					Value* GVArg = dyn_cast<Value>(GV);
					PrivateValMap.insert(make_pair(
						*stritr,
						GVArg
						)
					);
				}
			}
			//we need not to create alloca for private
		}
	}


	//handle schedule clause
	start = metastr.find("schedule");
	if( start != string::npos )
	{
		vector<string>	ScheduleStrVector;
		//(debug)
		errs()<<"schedule clause start:"<<start<<"\n";

		//erase "schedule("
		metastr.erase(
			metastr.begin()+start,
			metastr.begin()+start+string("schedule(").length()
		);

		string varstr;
		for(char ch: metastr)
		{
			if( (ch == ' ') )
			{
				continue;
			}
			if( (ch == ',')|(ch == ')') )
			{
				if(!varstr.empty())
				{
					ScheduleStrVector.push_back(varstr);
				}
				varstr.clear();
			}else
			{
				varstr += ch;
			}
		}
		//string after ')' will be schedule parameter
		if(!varstr.empty())
		{
			ScheduleStrVector.push_back(varstr);
		}

		// //(debug)
		for(auto stritr = ScheduleStrVector.begin(); 
			stritr != ScheduleStrVector.end(); ++stritr)
		{
			errs()<<"varitr:";
			errs()<< *stritr <<"\n";
		}

		//set ScheduleType
		for(auto stritr = ScheduleStrVector.begin(); 
			stritr != ScheduleStrVector.end(); ++stritr)
		{
			string str = *stritr;
			if( str == "static" )
			{
				myScheduleType = kmp_sch_static;
			}else if(str == "dynamic")
			{
				myScheduleType = kmp_sch_dynamic_chunked;
			}else if(str == "guided")
			{
				myScheduleType = kmp_sch_guided_chunked;											
			}else if(str == "auto")
			{
				myScheduleType = kmp_sch_auto;											
			}else if(str == "runtime")
			{
				myScheduleType = kmp_sch_runtime;											
			}

			if( ( '0' <= str[0] ) && ( str[0] <= '9' ) )
			{
				ChunkSize = str[0] - '0';
				for(unsigned i=1;i<str.size();i++)
				{
					ChunkSize = ChunkSize*10 + str[i]-'0'; 
				}
				if(myScheduleType == kmp_sch_static)
				{
					myScheduleType = kmp_sch_static_chunked;
				}
			}
		}
		//(debug)
		errs()<<"Schedule clause was processed\n";
		errs()<<"myScheduleType is "<<myScheduleType<<"\n";
		errs()<<"ChunkSize is "<<ChunkSize<<"\n";
	}

	//handle reduction clause
	start = metastr.find("reduction");
	if( start != string::npos )
	{
		vector<string>	ReductionStrVector;
		metastr.erase(
			metastr.begin()+start,
			metastr.begin()+start+string("reduction(").length()
		);

		string varstr;
		for(char ch: metastr)
		{
			if( !varstr.empty() && ((ch == ' ')||(ch == ',')||(ch == ')')||(ch == ':')) )
			{
				ReductionStrVector.push_back(varstr);
				varstr.clear();
			}else
			{
				varstr += ch;
			}
		}
		// //string after ')' will be Reduction parameter
		// if(!varstr.empty())
		// {
		// 	ReductionStrVector.push_back(varstr);
		// }

	  //(debug)
		for(auto stritr = ReductionStrVector.begin(); 
			stritr != ReductionStrVector.end(); ++stritr)
		{
			errs()<<"reduction parameters: ";
			errs()<< *stritr <<"\n";
		}



		//set rediction operator and reduction variable
		reduction_operator redop;
		StrVmap AllocaToInitMap;
		for(auto stritr = ReductionStrVector.begin(); 
			stritr != ReductionStrVector.end(); ++stritr)
		{
			string str = *stritr;
			//initial Value is 0
			if( (str == "+") ){
				redop = reduction_operator::add;
			}
			else if(str == "-"){
				redop = reduction_operator::sub;
			}
			else if(str == "|"){
				redop = reduction_operator::or_bin;
			}
			else if	(str == "^"){
				redop = reduction_operator::xor_bin;
			}
			else if(str == "||"){
				redop = reduction_operator::or_or;
			}
			//initial Value is 1
			else if(str == "*"){
				redop = reduction_operator::mul;
			}
			else if(str == "&&"){
				redop = reduction_operator::and_and;
			}
			//initial Value is ~0
			else if( str == "&" ){
				redop = reduction_operator::and_bin;
			}
			//initial value is least
			else if( str == "max" )
			{
				redop = reduction_operator::max;
			}
			//initial value is largest
			else if( str == "min" )
			{
				redop = reduction_operator::min;
			}
			//set reduction variable
			else
			{
				auto valitr = FuncArgumentMap.find( *stritr );
				if(valitr != FuncArgumentMap.end())
				{
					//(debug)
					errs()<<"Reduction: "<<valitr->first;
					(valitr->second)->print(errs());errs()<<"\n";

					auto mapitr = ReductionValMap.find(valitr->first);
					if(mapitr == ReductionValMap.end())
					{
						errs()<<"Set the argument that you specified to reduction, to the reduction value\n";
						ReductionValMap.insert(make_pair(
							valitr->first,
							make_pair(valitr->second,redop)
							)
						);	
					}
					//FuncArgument is needed
					//because the reduction result is stored to it.
					//debug
					errs()<<"Each thread's result is stored to: ";
					ReductionValMap.find(valitr->first)->second.first->print(errs()); 
					errs()<<"\n";
					// errs()<<"Each thread calculate the reduction using this operand: "<<ReductionValMap.find(valitr->first)->second.second<<"\n";

				}else
				{
					//(debug)
					// errs()<<"sorry we can't find reduction var:"<<*stritr<<"\n";
					errs()<<"Create new Alloca at loop's parent function\n";
					
					//find ReductionVar's Inst
					Value* RedVal;
					if(CapturedValMap.find(*stritr) != CapturedValMap.end())
					{
						RedVal = CapturedValMap.find(*stritr)->second;
					}else
					{
						errs()<<"Reduction Inst does not exist in this loop.\n";
						RedVal->dump();
						exit(1);
					}

					//(debug)
					errs()<<"You specified this variable as reduction: ";
					RedVal->print(errs()); errs()<<"\n";

					ReductionValMap.insert(make_pair(
						*stritr,
						make_pair(RedVal,redop)
						)
					);
				}
			}
		}

		//create ReductionRecordMap
		//The element of this map is the alloca of
		{
			StrVmap RedLocalMap;
			Function* ParentF = targetII->getParent()->getParent();
			for(Argument& arg : ParentF->args())
			{
				if(Value* argV = dyn_cast<Value>(&arg))
				{
					for(User* user : argV->users())
					{
						//record arguments which are used in reduction after loop 
						if( Instruction* I = dyn_cast<Instruction>(user) )
						{
							if( !(targetLoop->contains(I)) )
							{
								recordLoadSource(RedLocalMap,I);
							}
						}
					}
				}
			}
			for(auto reditr : ReductionValMap)
			{
				//check whether a targetII's parent has RedVar_Arg
				Value* redV = reditr.second.first;
				for(User* user : redV->users())
				{
					if( Instruction* I = dyn_cast<Instruction>(user) )
					{
						if( I->getOpcode() == Instruction::Store && 
							 	targetLoop->contains(I) &&
								I->getOperand(1)->hasName() )
						{
							Value* storeMemory = I->getOperand(1);
							string memoryName = storeMemory->getName().str();
							if( RedLocalMap.find(memoryName) != RedLocalMap.end() )
							{
								ReductionRecordMap.insert(make_pair( 
									reditr.first, 
									storeMemory
									));
							}		
						}
					}
				}
			}
		}// end of RedLocalMap and ParentF

		//(debug)
		for(auto reditr : ReductionRecordMap)
		{
			errs()<<"red record: name: "<<reditr.first<<" val:";
			reditr.second->print(errs()); errs()<<"\n";
		}



		//create alloca for phi and BO reduction, and record their Inst for AllocaToInit
		for(auto reditr = ReductionValMap.begin(); reditr != ReductionValMap.end(); ++reditr)
		{
			string redname = reditr->first;
			Value* RedVal = reditr->second.first;
			//if ReductionRecordMap has element named as same of redname,
			// use caller's RedVar_Local as RedVar_Arg,
			// transfer store of caller's RedVar_Local to targetII's location and
			if( ReductionRecordMap.find(redname) != ReductionRecordMap.end() )
			{
				//(debug)
				errs()<<"RedRecMap: "<< redname <<"\n";
				//use caller's RedVar_Local
				Value* RedVarArg = ReductionRecordMap.find(redname)->second;

				//trasfer store to targetII's location
				if( Instruction* I = dyn_cast<Instruction>(RedVarArg) )
				{
					//(debug)
					errs()<<"I: "<< redname <<"\n";
					for(auto useritr = I->user_begin(); useritr != I->user_end(); ++useritr)
					{
						//we assume that RedVar_Local is stored only once
						if( StoreInst* SI = dyn_cast<StoreInst>((*useritr)) )
						{
							Value* OpeVal = SI->getOperand(1);
							if(( OpeVal->hasName() &&
								 (OpeVal->getName().str() == (redname+"_Local")) ))
							{
								//transfer
								SI->moveBefore(targetII);
							}
						}
					}
				}
				//insert it
				if( FuncArgumentMap.find( RedVarArg->getName().str() ) == FuncArgumentMap.end() )
				{
					//(debug)
					errs()<<"RedRecMap FuncArg: "<< redname <<"\n";

					//do not erase
					// auto argitr = FuncArgumentMap.find( RedVarArg->getName().str() );
					//(debug)
					// errs()<<"erase from FuncArgumentMap:\n";
					// argitr->second->print(errs()); errs()<<"\n";
					// FuncArgumentMap.erase(argitr);
					FuncArgumentMap.insert(make_pair(
						redname,
						RedVarArg
					));
				}

			}else if( FuncArgumentMap.find( redname ) == FuncArgumentMap.end() )
			{
				//(debug)
				errs()<<"No FuncArgMap: "<< redname <<"\n";

				BasicBlock* FirstBB = &*(II->getParent()->getParent()->begin());
				Builder.SetInsertPoint(FirstBB,FirstBB->begin());
				//Builder has no Insert point, So we just Set, not preserve anything

				//create alloca to return result of reduction
				//if alloca is directed, this pass process that RedVar in ll.9861~9903.
				//So, only the phi or BO RedVar will be prepared the alloca.
				AllocaInst* AI = Builder.CreateAlloca( RedVal->getType() );
				Type* RedValTy = AI->getAllocatedType();
				AI->setName( redname + "_Arg" );
				unsigned align = calcAlignOfType(RedValTy);
				AI->setAlignment(align);

				Value* RedValArg = dyn_cast<Value>(AI);
				FuncArgumentMap.insert(make_pair(
					redname,
					RedValArg
				));
				AllocaToInitMap.insert(make_pair(
					redname,
					RedValArg												
				));
				//(debug)
				errs()<<"Allocated:\t";
				AI->print(errs()); errs()<<"\n";
			}

			//we assume that RedInst is always PHI
			// {
			//  1.Insert [phi's name] and [IncomingValue's name] to stringMap
			// set<string> RedVarStrSet;
			// if(PHINode* PHIN = dyn_cast<PHINode>(RedVal))
			// {
			// 	RedVarStrSet.insert(RedInst->getName().str());
			// 	for(unsigned i=0;i<PHIN->getNumIncomingValues();i++)
			// 	{
			// 		Value* IncomingV = PHIN->getIncomingValue(i);
			// 		if( IncomingV->hasName() )
			// 		{
			// 			RedVarStrSet.insert( IncomingV->getName().str() );
			// 		}
			// 	}
			// }
		}

		//Store initial value to RedVar_Arg
		for(auto valitr = AllocaToInitMap.begin(); 
			valitr != AllocaToInitMap.end(); ++valitr)
		{
			unsigned align = 4;
			Type* RedVarTy = dyn_cast<PointerType>( 
				valitr->second->getType() 
			)->getElementType();
			if( RedVarTy->isIntegerTy(64) || RedVarTy->isPointerTy() || RedVarTy->isDoubleTy() )
			{
				align = 8;
			}//we don't handle PointerTy for now
			else if(RedVarTy->isIntegerTy(16))
			{
				align = 2;
			}

			//Search initial value of this inst
			//if RedVar is alloca, there is already stored initial value.
			//This is because AllocaToInitMap does not include the directed alloca inst.

			Value* redV = ReductionValMap.find( valitr->first )->second.first;
			Value* InitialV;
			if( PHINode* PHIN = dyn_cast<PHINode>(redV) )
			{
				InitialV = findInitialValueForPhi(targetLoop, PHIN, valitr->first);
				//(debug)
				// errs()<<"### red phi initial: \n";
				// InitialV->print(errs()); errs()<<"\n";
			}else if( dyn_cast<BinaryOperator>(redV) )
			{
				//if BO, chech its user
				InitialV = getInitialValueForBO(targetLoop,redV);
			}

			//(debug)
			errs()<<"Reduction InitialV Store:\t";
			
			// 2023/12/19編集
			// コメントアウト↓
			// print()でエラー
			// InitialV->print(errs());
			errs()<<"read ParamGet.cpp:line9322";
			errs()<<"\n";

			//store the InitialV
			Builder.SetInsertPoint( targetII );
			Builder.CreateAlignedStore(
				InitialV,
				valitr->second,
				align
			);

		}//end of AllocaToInitMap itr
	}
}
Value* ParamGet::ParamGetCommon::findInitialValueForPhi(Loop* targetLoop, PHINode* RedPHI,
	string redname)
{
	Value* InitialV = dyn_cast<Value>(RedPHI);

	//if this phi is defined outside loop, this is initial value
	if( !targetLoop->contains( dyn_cast<Instruction>(InitialV) ) )
	{
		return InitialV;
	}

	//if phi, Store the Incoming Value
	// which correscpond to the BB that outside loop.
	unsigned incomings = RedPHI->getNumIncomingValues();
	for(unsigned i=0; i<incomings; i++)
	{
		//get Incoming Value which is not from inside loop
		if( !(targetLoop->contains( RedPHI->getIncomingBlock(i) )) )
		{
			InitialV = RedPHI->getIncomingValue(i);

			//(debug)
			errs()<<"InitialV found:\t";
			InitialV->print(errs());
			errs()<<"\n";

			ReductionPHIMap.insert(make_pair(
				redname,
				InitialV
				));
			return InitialV;
		}
	}

	// //(debug)
	// errs()<<"InitialV NOT found:\t";
	// errs()<<"\n";

	//if the RedVal is in the loop, 
	// we assume that targetLoop block_begin is sorted by block's order of appearance
	// so we set incoming val of firstly appears as InitialV 
	for(auto bitr = targetLoop->block_begin(); bitr != targetLoop->block_end(); ++bitr)
	{
		BasicBlock* BB = *bitr;
		if(BB->hasName())
		{			
			unsigned incomings = RedPHI->getNumIncomingValues();
			for(unsigned i=0; i<incomings; i++)
			{
				//get Incoming Value which is not from inside loop
				if( RedPHI->getIncomingBlock(i)->hasName() && 
					RedPHI->getIncomingBlock(i)->getName().str() == BB->getName().str())
				{
					InitialV = RedPHI->getIncomingValue(i);
					if( PHINode* InitialPHI = dyn_cast<PHINode>(InitialV) )
					{
						InitialV = findInitialValueForPhi(targetLoop, InitialPHI, redname);
					}
					ReductionPHIMap.insert(make_pair(
						redname,
						InitialV
						));
					return InitialV;
				}
			}
		}
	}

	//(debug)
	errs()<<"InitialV NOT found:\t";
	InitialV->print(errs());
	errs()<<"\n";
	return InitialV;
}
void ParamGet::ParamGetCommon::recordLoadSource(StrVmap &RedLocalMap, Instruction* I)
{
	if(LoadInst* LI = dyn_cast<LoadInst>(I))
	{
		Value* SrcOp = LI->getOperand(0);

		// errs()<<"recordLoadSource SrcOp: ";
		// SrcOp->print(errs()); errs()<<"\n";

		if(SrcOp->hasName())
		{
			RedLocalMap.insert(make_pair(
				SrcOp->getName().str(),
				SrcOp
				));				
		}
		return;		
	}else if( dyn_cast<PHINode>(I) )
	{
		//this is to avoid infinitial recursive call
		return;
	}
	else
	{
		unsigned operands = I->getNumOperands();
		for(unsigned i = 0; i < operands; i++)
		{
			// errs()<<"recordLoadSource checkI: ";
			// I->getOperand(i)->print(errs()); errs()<<"\n";

			if(Instruction* checkI = dyn_cast<Instruction>(I->getOperand(i)))
			{
				recordLoadSource(RedLocalMap, checkI);
			}
		}			
	}
	return;
}
Value* ParamGet::ParamGetCommon::getInitialValueForBO(Loop* targetLoop, Value* redV)
{
	Value* initialV;
	for(auto useritr = redV->user_begin(); useritr != redV->user_end(); ++useritr)
	{
		//if phi which is containd by the loop, Store the Incoming Value
		// which correscpond to the BB that targetLoop does not contains.

		if( PHINode* PHIN = dyn_cast<PHINode>((*useritr)) )
		{
			if( targetLoop->contains(PHIN) )
			{
				unsigned incomings = PHIN->getNumIncomingValues();
				for(unsigned i=0; i<incomings; i++)
				{
					//get Incoming Value which is not from inside loop
					if( !(targetLoop->contains( PHIN->getIncomingBlock(i) )) )
					{
						initialV = PHIN->getIncomingValue(i);
						ReductionPHIMap.insert(make_pair(
							redV->getName().str(),
							dyn_cast<Value>(PHIN)
							));
						break;
					}
				}
				break;							
			} 
		}else if( CastInst* CI = dyn_cast<CastInst>((*useritr)) )
		{
			if( targetLoop->contains(CI) )
			{
				initialV = getInitialValueForBO(targetLoop,(*useritr));
				break;
			}
		}
	}
	//(debug)
	errs()<<"### red BO initial: \n";
	initialV->print(errs()); errs()<<"\n";
	return initialV;
}
Value* ParamGet::ParamGetCommon::getCmpInstForMaxMin(Loop* targetLoop, Value* redV)
{
	Value* IncomingV;
	if( PHINode* PHIN = dyn_cast<PHINode>(redV) )
	{
		for(unsigned i=0; i<PHIN->getNumOperands(); i++)
		{
			if( targetLoop->contains(PHIN->getIncomingBlock(i)) )
			{
				IncomingV = PHIN->getIncomingValue(i);
				break;
			}
		}
		if( !dyn_cast<SelectInst>(IncomingV) )
		{
			IncomingV = getSelectInst(IncomingV);
			//after this, the SelectInst is written in IncomingV
		}
		//get condition
		if( SelectInst* SI = dyn_cast<SelectInst>(IncomingV) )
		{
			if( dyn_cast<CmpInst>(SI->getCondition()) )
			{
				return SI->getCondition();
			}											
		}else//error 
		{
			errs()<<"What determines the phi? error in createReductionCollect\n";
			IncomingV->dump();
		}
	}else //we check it is stored
	{
		for(auto useritr = redV->user_begin(); useritr != redV->user_end(); ++useritr)
		{
			if( StoreInst* StoreI = dyn_cast<StoreInst>((*useritr)) )
			{
				if( targetLoop->contains(StoreI) )
				{
					IncomingV = StoreI->getOperand(0);
					//get condition

					if( (!dyn_cast<SelectInst>(IncomingV)) )
					{
						IncomingV = getSelectInst(IncomingV);
					}
					
					if( SelectInst* SI = dyn_cast<SelectInst>(IncomingV) )
					{
						if( dyn_cast<CmpInst>(SI->getCondition()) )
						{
							return SI->getCondition();
						}							
					}else//error 
					{
						errs()<<"What determines the store? error in createReductionCollect\n";
						IncomingV->dump();
					}
				}
			}
		}
	}
	//error value return
	errs()<<"### error value return ###\n";
	IncomingV->dump();
	return IncomingV;
}
Value* ParamGet::ParamGetCommon::getSelectInst(Value* judgeV)
{
	if(Instruction* ptrI = dyn_cast<Instruction>(judgeV))
	{
		for(unsigned i=0; i<ptrI->getNumOperands(); i++)
		{
			if(dyn_cast<Instruction>( ptrI->getOperand(i) ))
			{
				judgeV = ptrI->getOperand(i);
				//debug
				errs()<<"getSelectInst: judgeV-> ";
				judgeV->print(errs()); errs()<<"\n";
				if(dyn_cast<SelectInst>( judgeV ))
				{
					return judgeV;
				}else
				{
					Value* tempV = getSelectInst(judgeV);
					if(dyn_cast<SelectInst>( tempV ))
					{
						return tempV;
					}
				}
			}
		}
	}
	//error value return
	errs()<<"### error value return ###\n";
	judgeV->dump();
	return judgeV;	
}
void ParamGet::ParamGetCommon::transformScalars(IRBuilder<> &Builder)
{
	//set insert point
	Instruction* InsertPointInst;
	for(auto institr : CapturedInstVector)
	{
		Instruction* I = institr.second;
		if(IntrinsicInst* II = dyn_cast<IntrinsicInst>(I))
		{
			if(II->getIntrinsicID() == Intrinsic::directive)
			{
				Builder.SetInsertPoint(I);
				InsertPointInst = I;
				// errs()<<"Insert point is found\n";
				// II->print(errs()); errs()<<"\n";
				break;
			}
		}
	}

	//(debug)
	errs()<<"-- transform scalars to i64 --\n";

	//cast to i64 for scalars
	for(auto mappair : FuncArgumentMap){
		//debug
		errs()<<"val: ";
		mappair.second->print(errs()); 
		errs()<<"\n";

		Value* argVal = mappair.second;
		Type* argTy = argVal->getType();
		unsigned typeId = argTy->getTypeID();
		Value* buildval;
		bool transformflag = false;
		if(	typeId == Type::TypeID::FloatTyID ){
			// buildval = Builder.CreateFPExt(
			// 	/*Value+ V*/argVal,
			// 	/*Type* DestTy*/Builder.getDoubleTy()
			// );
			buildval = Builder.CreateBitCast(
				/*Value+ V*/argVal,
				/*Type* DestTy*/Builder.getInt32Ty()
			);
			transformflag = true;
		}
		if(typeId == Type::TypeID::DoubleTyID){
			buildval = Builder.CreateBitCast(
				/*Value+ V*/argVal,
				/*Type* DestTy*/Builder.getInt64Ty()
			);
			transformflag = true;
		}
		else if(
			typeId == Type::TypeID::X86_FP80TyID ||
			typeId == Type::TypeID::FP128TyID ||
			typeId == Type::TypeID::PPC_FP128TyID
		){
			//this must be passed through alloca
			//and create load in outlined func to use as original variable
			//we create load if CapValMap.find(str) is scalar and 128bit type
			// errs()<<"=== FuncArgument CHeck ===\n";
			if( LoadInst* LI = dyn_cast<LoadInst>(argVal) )
			{
				// errs()<<"=== FuncArgument CHeck 22===\n";
				//if 128bit type is loaded, pass its loading memory
				buildval = LI->getOperand(0);
			}else 
			{
				// errs()<<"=== FuncArgument CHeck 33===\n";
				//if there is not memory, create alloca
				//and use it
				//set inert point to function's first instruction
				Builder.SetInsertPoint( &*((&*Builder.GetInsertBlock()->getParent()->begin())->begin()) );
				AllocaInst* AI = Builder.CreateAlloca( argTy );
				AI->setAlignment( calcAlignOfType(argTy) );

				//search insert point again
				Builder.SetInsertPoint( InsertPointInst );

				//create store
				Builder.CreateAlignedStore( 
					argVal,
					AI,
					AI->getAlignment()
				);
				buildval = dyn_cast<Value>(AI);
			}
			// errs()<<"=== FuncArgument CHeck 44===\n";
			transformflag = true;			
		}

		if(transformflag){
			//pair to old Inst. Not to new inst
			if( argVal->hasName() ){
				//str is captured inst name
				string str = argVal->getName().str();
				errs()<<"name :" << str <<"\n";
				TransformMap.insert(make_pair( str, mappair.second ));

				//replace the FuncArgumentMap elem
				string sameName = mappair.first;
				FuncArgumentMap.find(sameName)->second = buildval;
				// FuncArgumentMap.erase( FuncArgumentMap.find(sameName) );
				// FuncArgumentMap.insert(make_pair(
				// 	sameName,
				// 	buildval
				// ));
				// //mappair.second = buildval;
				errs()<<"transformScalars: ";
				FuncArgumentMap.find(sameName)->second->print(errs()); errs()<<"\n";
			}
			else{
				errs()<<"[ParamGet] Value has no name\n";
			}
		}
	}

	//debug
	// BasicBlock* DebugBB = InsertPointInst->getParent();
	// errs()<<"transformed BB::\n";
	// DebugBB->print(errs()); errs()<<"\n";

}
void ParamGet::ParamGetCommon::retransformScalars(IRBuilder<> &Builder)
{
	//cast to i64 for scalars
	for(auto valitr : TransformMap)
	{
		//str is original inst name
		string str = valitr.first;
		if( ReductionValMap.find(str) != ReductionValMap.end() )
		//this is not good, which changes str.
		{
			str = str + "_Arg";
			//we use local Reduction Var for inner loop calc,
			//and use Arg Reduction Var for reduce_nowait
		}else if( FirstPrivateValMap.find(str) != FirstPrivateValMap.end() )
		{
			str = str + ".FirstPrivateArgument";
		}else if( LastPrivateValMap.find(str) != LastPrivateValMap.end() )
		{
			str = str + ".LastPrivateArgument";
		}

		Value* argVal = BuiltValMap.find( str )->second;
		Type* OrigTy = valitr.second->getType();
		Value* buildval;
		bool transformflag = false;


		//(debug)
		// errs()<<"retransform: "<<str<<"\n";
		// errs()<<"Argument is: ";
		// argVal->print(errs()); errs()<<"\n";


		switch( OrigTy->getTypeID() )
		{
			case Type::TypeID::IntegerTyID:
				if( OrigTy->getIntegerBitWidth() < 64 )
				{
					buildval = Builder.CreateTrunc(
						/*Value* V*/argVal,
						/*Type* DestTy*/OrigTy
					);
					transformflag = true;
				}
				break;
			case Type::TypeID::FloatTyID:
				if(true)//this nest is for use variable in case
				{
					// buildval = Builder.CreateTrunc(
					// 	/*Value+ V*/argVal,
					// 	/*Type* DestTy*/Builder.getInt32Ty()
					// );		
					buildval = Builder.CreateBitCast(argVal,OrigTy);	
					transformflag = true;
				}	
				break;
			case Type::TypeID::DoubleTyID:
				buildval = Builder.CreateBitCast(
					/*Value+ V*/argVal,
					/*Type* DestTy*/OrigTy
				);
				transformflag = true;
				break;
			case Type::TypeID::X86_FP80TyID:
			case Type::TypeID::FP128TyID:
			case Type::TypeID::PPC_FP128TyID:
				//create load for 128bit scalar passed by alloca
				buildval = dyn_cast<Value>(	Builder.CreateAlignedLoad(
					argVal,
					calcAlignOfType(OrigTy)
				));
				transformflag = true;
				//BuiltValMap.find(str)->second = buildval;
				// errs()<<"replaced BuiltValMap:\tname:"<< str <<": val\t";
				// buildval->print(errs()); errs()<<"\n";
				break;
			default:
				errs()<<"this may be scalar directed in lastprivate\n";
				argVal->print(errs()); errs()<<"\n";
				break;
		}
		if(transformflag)
		{
			str = valitr.first;
			//pair to old Inst. Not to new inst
			if( BuiltValMap.find(str) == BuiltValMap.end() )
			{
				BuiltValMap.insert(make_pair(str,buildval));
			}else
			{
				BuiltValMap.find(str)->second = buildval;
			}			
			errs()<<"inserted to BuiltValMap:\tname:"<<str<<": val\t";
			BuiltValMap.find(str)->second->print(errs()); errs()<<"\n";
		}

	}
}

void ParamGet::ParamGetCommon::createReductionCollect(Module &M, Function* OutFunc, 
	IRBuilder<> &Builder, Loop* targetLoop)
{
	LLVMContext &TheContext = M.getContext();
	Value* RedList = BuiltValMap.find(".omp.reduction.red_list")->second;
		
	// 1. bitcast "[num_var x i8*]* .omp.reduction.red_list" (pointer of reduce_data)"
	// 	to ReductionValMap.reditr->second->getType()->getPointerTo()
	Value* BC = Builder.CreateBitCast( 
		RedList,
		BuiltValMap.find(ReductionValMap.begin()->first + "_Local")->second->getType()->getPointerTo()
	);
	// 2. Store ReductionValMap.reditr->second to 1.
	Builder.CreateAlignedStore(
		BuiltValMap.find(ReductionValMap.begin()->first + "_Local")->second,
		BC,
		8 // ReductionValMap's val is Pointer so Store is always 8
	);


	// reditr start //Bitcast "RedList" to RedVarTy, and Store RedVar to RedList's BitCast
	unsigned redcounter = 1;
	for(auto reditr = ReductionValMap.begin(); 
			reditr != ReductionValMap.end(); ++reditr)
	{
		if(reditr == ReductionValMap.begin())//this is not good
		{//we want to use "reditr = ReductionValMap.begin() + 1", but it's not allowed
			continue;
		}
		// 3. getelementptr omp.reduction.red_list, 0, reditr-index
		vector<Value*> Idx;
		Idx.push_back(Builder.getInt64(0));
		Idx.push_back(Builder.getInt64(redcounter++));
		ArrayRef<Value*> IdxList(Idx);
		Value* GEPVal = Builder.CreateInBoundsGEP(
			RedList,
			IdxList
		);
		// 4. bitcast 3. to ReductionValMap.reditr->second->getType()->getPointerTo()
		BC = Builder.CreateBitCast(
			GEPVal,
			BuiltValMap.find(reditr->first + "_Local")->second->getType()->getPointerTo()
		);
		// 5. Store ReductionValMap.reditr->second to 4.
		Builder.CreateAlignedStore(
			BuiltValMap.find(reditr->first + "_Local")->second,
			BC,
			8// ReductionValMap's val is Pointer, So Store is always 8 alignment
		);
	}
	// reditr end

	// 6. bitcast omp.reduction.red_list to i8*
	BC = Builder.CreateBitCast(
		RedList,
		Builder.getInt8Ty()->getPointerTo()
	);
	OMPParamMap.insert(make_pair(
		".omp.reduction.reduction_func_BitCast",
		BC
	));

	// 7. call kmpc_reduce_nowait
	//make FunctionType
	vector<Type*> FArgs;
	FArgs.push_back(Builder.getInt8Ty()->getPointerTo());//Outlined func's first arg is not ident_t
	FArgs.push_back(Builder.getInt8Ty()->getPointerTo());
	ArrayRef<Type*> FArgsRef(FArgs);
	FunctionType* RedFuncType = FunctionType::get(Builder.getVoidTy(), FArgsRef, false);
	//create Function
	Function* RedFunc = Function::Create( 
		RedFuncType,
		Function::InternalLinkage, ".omp.reduction.reduction_func", &M
	);
	//build RedFunc's body
	errs()<<"Reduction's called func build\n";
	buildReductionFuncBody(RedFunc, Builder, targetLoop);
	errs()<<"Reduction's called func build end\n";
	//create call kmpc_reduc_nowait
	Value* RetVal = create_kmpc_reduce_nowait(M, RedFunc, Builder);


	// 8. switch kmpc_reduce_nowait's result
	// //what does this return value mean?
	BasicBlock* case1BB = BasicBlock::Create(TheContext,"",OutFunc);
	BasicBlock* case2BB = BasicBlock::Create(TheContext,"",OutFunc);
	BasicBlock* endBB = BasicBlock::Create(TheContext,"",OutFunc);
	SwitchInst* SI = Builder.CreateSwitch(
		/*Value* V*/RetVal,
		/*BasicBlock* Dest*/endBB,
		/*unsigned NumCases=10*/2
	);
	SI->addCase( Builder.getInt32(1), case1BB );//kmpc_reduce_nowait's return value is int32
	SI->addCase( Builder.getInt32(2), case2BB );

	// case 1:
	Builder.SetInsertPoint(case1BB);
	// 	reditr start
	for(auto reditr : ReductionValMap)
	{
		string reductionName = reditr.first;
		Type* RedVarTy = dyn_cast<PointerType>( 
			BuiltValMap.find(reductionName + "_Local")->second->getType() 
		)->getElementType();
		unsigned align = calcAlignOfType(RedVarTy);
		unsigned bitwidth = align << 3;

		// if( RedVarTy->isIntegerTy(64) || RedVarTy->isPointerTy() || RedVarTy->isDoubleTy() )
		// {
		// 	align = 8;
		// 	if(RedVarTy->isIntegerTy(64))
		// 	{
		// 		bitwidth = 64;
		// 	}
		// }//we don't handle PointerTy for now
		// else if(RedVarTy->isIntegerTy(16))
		// {
		// 	align = 2;
		// 	bitwidth = 16;
		// }
		switch(reditr.second.second)
		{					
			// 	if RedOP is not && or ||		
			case ParamGetCommon::reduction_operator::add:
			case ParamGetCommon::reduction_operator::mul:
			case ParamGetCommon::reduction_operator::sub:
			case ParamGetCommon::reduction_operator::and_bin:
			case ParamGetCommon::reduction_operator::or_bin:
			case ParamGetCommon::reduction_operator::xor_bin:	
				if(true)
				{		
					Value* LoadValues[2];	
					//(debug)
					errs()<<"reduction var name: "<< reductionName <<"\n";
					if( BuiltValMap.find( reductionName + "_Arg" ) == BuiltValMap.end() )
					{
						errs()<<"BuiltValMap doesn't have _Arg of this.\n";
					}else
					{
						errs()<<"_Arg: ";
						BuiltValMap.find( reductionName + "_Arg" )->second->print(errs());
						errs()<<"\n";
					}					
					if( BuiltValMap.find( reductionName + "_Local" ) == BuiltValMap.end() )
					{
						errs()<<"BuiltValMap doesn't have _Local of this.\n";
					}else
					{
						errs()<<"_Local: ";
						BuiltValMap.find( reductionName + "_Local" )->second->print(errs());
						errs()<<"\n";
					}
					//----------------

					//0. Load "RedVar in Arg" and "RedVar in body"
					LoadValues[0] = dyn_cast<Value>(Builder.CreateAlignedLoad(
						BuiltValMap.find( reductionName + "_Arg" )->second,
						align
					));
					LoadValues[1] = dyn_cast<Value>(Builder.CreateAlignedLoad(
						BuiltValMap.find( reductionName + "_Local" )->second,
						align
					));
					//1. [RedOP] "RedVar in Arg", "RedVar in body" 
					Value* SrcVal;
					switch(reditr.second.second)
					{					
						case ParamGetCommon::reduction_operator::add:
						case ParamGetCommon::reduction_operator::sub:
							if( RedVarTy->isFloatTy() || RedVarTy->isDoubleTy() )
							{
								SrcVal = Builder.CreateFAdd(
									LoadValues[1],
									LoadValues[0]
								);
							}else
							{
								SrcVal = Builder.CreateNSWAdd(
									LoadValues[1],
									LoadValues[0]									
								);
							}
							break;
						case ParamGetCommon::reduction_operator::mul:
							if( RedVarTy->isFloatTy() || RedVarTy->isDoubleTy() )
							{
								SrcVal = Builder.CreateFMul(
									LoadValues[1],
									LoadValues[0]
								);
							}else
							{
								SrcVal = Builder.CreateNSWMul(
									LoadValues[1],
									LoadValues[0]									
								);
							}						
							break;
						case ParamGetCommon::reduction_operator::and_bin:
							SrcVal = Builder.CreateAnd(
								LoadValues[1],
								LoadValues[0]									
							);
							break;
						case ParamGetCommon::reduction_operator::or_bin:
							SrcVal = Builder.CreateOr(
								LoadValues[1],
								LoadValues[0]									
							);
							break;
						case ParamGetCommon::reduction_operator::xor_bin:	
							SrcVal = Builder.CreateXor(
								LoadValues[1],
								LoadValues[0]									
							);
							break;
						default:
							break;
					}
					//2. Store 1. to "RedVar in Arg"
					Builder.CreateAlignedStore(
						SrcVal,
						BuiltValMap.find( reductionName + "_Arg" )->second,
						align
					);
				}
				break;
			// 		if &&
			case ParamGetCommon::reduction_operator::and_and:
				if(true)
				{
					Value* LoadValues[2];		
					//0.Load "RedVar in Arg"
					LoadValues[0] = dyn_cast<Value>(Builder.CreateAlignedLoad(
						BuiltValMap.find( reductionName + "_Arg" )->second,
						align
					));
					//1.ICmp ne 0., 0
					Value* CmpVal1;
					if( RedVarTy->isDoubleTy() || RedVarTy->isFloatTy() )
					{
						CmpVal1 = Builder.CreateFCmp(
							CmpInst::Predicate::ICMP_NE,
							LoadValues[0],
							dyn_cast<Value>( ConstantFP::get(RedVarTy, (double)0) )
						);
					}else
					{
						CmpVal1 = Builder.CreateICmp(
							CmpInst::Predicate::ICMP_NE,
							LoadValues[0],
							Builder.getIntN(RedVarTy->getIntegerBitWidth(),0)
						);	
					}

					//2.Load "RedVar in body"
					LoadValues[1] = dyn_cast<Value>(Builder.CreateAlignedLoad(
						BuiltValMap.find( reductionName + "_Local" )->second,
						align
					));

					//(debug)
					// errs()<<"LoadValues[1]:\t";
					// LoadValues[1]->print(errs()); errs()<<"\n";


					//3.ICmp ne 3., 0
					Value* CmpVal2;
					if( RedVarTy->isDoubleTy() || RedVarTy->isFloatTy() )
					{
						CmpVal2 = Builder.CreateFCmp(
							CmpInst::Predicate::ICMP_NE,
							LoadValues[1],
							dyn_cast<Value>( ConstantFP::get(RedVarTy, (double)0) )
						);
					}else
					{
						CmpVal2 = Builder.CreateICmp(
							CmpInst::Predicate::ICMP_NE,
							LoadValues[1],
							Builder.getIntN(bitwidth,0)
						);	
					}
					//4.And 1., 3.	
					Value* OpVal = Builder.CreateAnd(
						CmpVal1,
						CmpVal2
					); 
			
					//-----------or_or do same thing -----
					//5.zext 4.(or 2.) to i32
					Value* SrcVal = Builder.CreateZExt(
						OpVal,
						Builder.getIntNTy(bitwidth)
					);
					//6.Store 5. to "RedVar in Arg"
					Builder.CreateAlignedStore(
						SrcVal,
						BuiltValMap.find( reductionName + "_Arg" )->second,
						align									
					);
					//--------------------------------
				}
				break;

			// 		if ||
			case ParamGetCommon::reduction_operator::or_or:
				if(true)
				{					
					//0.Load "RedVar in Arg" and "RedVar in body"	
					Value* LoadValues[2];		
					LoadValues[0] = dyn_cast<Value>(Builder.CreateAlignedLoad(
						BuiltValMap.find( reductionName + "_Arg" )->second,
						align
					));
					LoadValues[1] = dyn_cast<Value>(Builder.CreateAlignedLoad(
						BuiltValMap.find( reductionName + "_Local" )->second,
						align
					));
					//1.or "RedVar in Arg", "RedVar in body" 
					Value* OpVal = Builder.CreateOr(
						LoadValues[0],
						LoadValues[1]
					);
					//2.ICmp ne 2., 0	
					Value* CmpVal;
					if( RedVarTy->isDoubleTy() || RedVarTy->isFloatTy() )
					{
						CmpVal = Builder.CreateFCmp(
							CmpInst::Predicate::ICMP_NE,
							OpVal,
							dyn_cast<Value>( ConstantFP::get(RedVarTy, (double)0) )
						);
					}else
					{
						CmpVal = Builder.CreateICmp(
							CmpInst::Predicate::ICMP_NE,
							OpVal,
							Builder.getIntN(bitwidth,0)
						);	
					}
					//--------and_and do same thing -----
					//5.zext 4.(or 2.) to i32
					Value* SrcVal = Builder.CreateZExt(
						CmpVal,
						Builder.getIntNTy(bitwidth)
					);
					//6.Store 5. to "RedVar in Arg"
					Builder.CreateAlignedStore(
						SrcVal,
						BuiltValMap.find( reductionName + "_Arg" )->second,
						align									
					);
					//--------------------------------
				}
				break;
			//	if max,min
			case ParamGetCommon::reduction_operator::max:
			case ParamGetCommon::reduction_operator::min:
				if(true)
				{
					//0.Load "RedVar in Arg" and "RedVar in body"	
					Value* LoadValues[2];		
					LoadValues[0] = dyn_cast<Value>(Builder.CreateAlignedLoad(
						BuiltValMap.find( reductionName + "_Arg" )->second,
						align
					));
					LoadValues[1] = dyn_cast<Value>(Builder.CreateAlignedLoad(
						BuiltValMap.find( reductionName + "_Local" )->second,
						align
					));
					//1.[fcmp or icmp] [ogt, sgt, ugt] "RedVar in Arg", "RedVar in body"
					//determine the predicate
					Value* redV = reditr.second.first;
					CmpInst *RedCmp;
					Value* IncomingV = getCmpInstForMaxMin(targetLoop, redV);
					if((RedCmp = dyn_cast<CmpInst>(IncomingV)))
					{
						//just want to assign RedCmp
					}else
					{
						errs()<<"RedCmp is not calculated\n";
						redV->dump();
					}

					//now, RedCmp is original compare inst of this reduction
					Value* newRedCmp;
					if( RedCmp->isFPPredicate() )
					{
						newRedCmp = Builder.CreateFCmp(
							/*CmpInst::Predicate P*/RedCmp->getPredicate(),
							/*Value *LHS*/LoadValues[0],
							/*Value *RHS*/LoadValues[1]
						);
					}else if( RedCmp->isIntPredicate() )
					{
						newRedCmp = Builder.CreateICmp(
							/*CmpInst::Predicate P*/RedCmp->getPredicate(),
							/*Value *LHS*/LoadValues[0],
							/*Value *RHS*/LoadValues[1]
						);
					}else//error
					{
						errs()<<"CmpInst's predicate is not what we can handle\n";
						RedCmp->dump();
					}

					//2.select 1., if true "RedVar in Arg", else "RedVar in body"
					Value* newSelectVal = Builder.CreateSelect(
						newRedCmp,
						LoadValues[0],
						LoadValues[1]
					);
					string str = to_string(instnum++);
					newSelectVal->setName( str );
					BuiltValMap.insert(make_pair(
						str,
						newSelectVal
					));//necessary to use this select in creating other inst for RedVar

					Value* newRedVal = createOtherInstForRedVar(Builder, targetLoop, redV, newSelectVal);

					//3.Store 2. to "RedVar in Arg" 
					Builder.CreateAlignedStore(
						newRedVal,
						BuiltValMap.find( reductionName + "_Arg" )->second,
						align
					);
				}	
			default:
				break;
		}
	}
	// 	reditr end

	// 	3. call kmpc_end_reduce_nowait
	create_kmpc_end_reduce_nowait(M, Builder);	



	// 	4. br to end
	Builder.CreateBr(endBB);

	// case 2:
	Builder.SetInsertPoint(case2BB);
	// 	reditr start
	for(auto reditr : ReductionValMap)
	{
		string reductionName = reditr.first;
		Type* RedVarTy = dyn_cast<PointerType>( 
			BuiltValMap.find(reductionName + "_Local")->second->getType() 
		)->getElementType();
		unsigned align = calcAlignOfType(RedVarTy);
		unsigned bitwidth = calcSizeOfType(RedVarTy);


		//(debug)
		errs() << "reduction var name: " << reductionName <<"\n";
		errs() << "reditr->second.first: ";
		reditr.second.first->print(errs()); errs()<< "\n";
		errs() << "align: " << align <<"\n";
		errs() << "bitwidth: " << bitwidth <<"\n";

		// if( RedVarTy->isIntegerTy(64) || RedVarTy->isPointerTy() || RedVarTy->isDoubleTy() )
		// {
		// 	align = 8;
		// 	if(RedVarTy->isIntegerTy(64) || RedVarTy->isDoubleTy())
		// 	{
		// 		bitwidth = 64;
		// 	}
		// }//we don't handle PointerTy for now
		// else if(RedVarTy->isIntegerTy(16))
		// {
		// 	align = 2;
		// 	bitwidth = 16;
		// }


		// 	if reditr.second is (not Int) or (operator is *,&&,||)
		if( RedVarTy->isDoubleTy() || RedVarTy->isFloatTy() || RedVarTy->isIntegerTy(16) ||
			reditr.second.second == ParamGetCommon::reduction_operator::mul ||
			reditr.second.second == ParamGetCommon::reduction_operator::and_and ||
			reditr.second.second == ParamGetCommon::reduction_operator::or_or ||
			reditr.second.second == ParamGetCommon::reduction_operator::max ||
			reditr.second.second == ParamGetCommon::reduction_operator::min )
		{
			// 		1. if reditr.second->getType() is not Int,
			// 			bit cast "RedVar in Arg" to i32*
			Value* RedargVal = BuiltValMap.find( reductionName + "_Arg" )->second ;
			Value* LoadValues[2];
			if( RedVarTy->isDoubleTy() || RedVarTy->isFloatTy() )
			{
				RedargVal = Builder.CreateBitCast(
					RedargVal,
					Builder.getIntNTy(bitwidth<<3)->getPointerTo()
				);
			}
			// 		2. load atomicly (monotonic) from 1.
			LoadInst* LI = Builder.CreateAlignedLoad(
				RedargVal,
				align
			);
			LI->setAtomic( AtomicOrdering::Monotonic );	//llvm/Support/AtomicOrdering.h
			LoadValues[1] = dyn_cast<Value>( LI );

			// 		3. load from "RedVar in body"
			LoadValues[0] = dyn_cast<Value>(Builder.CreateAlignedLoad(
				BuiltValMap.find( reductionName + "_Local" )->second,
				align
			));
			// 		if RedOp is &&
			if( reditr.second.second == ParamGetCommon::reduction_operator::and_and )
			{
				// 			3'. ICmp ne 3., 0
				Value* CmpVal;
				if( RedVarTy->isDoubleTy() || RedVarTy->isFloatTy() )
				{
					CmpVal = Builder.CreateFCmp(
						CmpInst::Predicate::ICMP_NE,
						LoadValues[0],
						dyn_cast<Value>( ConstantFP::get(RedVarTy,(double)0) )
					);
				}else
				{
					CmpVal = Builder.CreateICmp(
						CmpInst::Predicate::ICMP_NE,
						LoadValues[0],
						Builder.getIntN(RedVarTy->getIntegerBitWidth(),0)
					);	
				}
				LoadValues[0] = CmpVal;
			}

			// 		BB: atomic_cont
			BasicBlock* atomic_contBB = BasicBlock::Create(TheContext,"",OutFunc,endBB);
			BasicBlock* entryBB = Builder.GetInsertBlock();
			Builder.CreateBr(atomic_contBB);
			Builder.SetInsertPoint(atomic_contBB);

			// 		4. phi {i16 or i32} [2., case 2], [9., atomic_cont]
			PHINode* PHIN;
			PHIN = Builder.CreatePHI( 
				Builder.getIntNTy( bitwidth<<3 ),
				2
			);
			PHIN->addIncoming( LoadValues[1], entryBB );


			LoadValues[1] = dyn_cast<Value>(PHIN);
			// 		4'. if reditr.second->getType() is not Int,
			// 			bitcast 4. to reditr.second->getType()


			errs()<<"LV[0]: ";
			LoadValues[0]->print(errs()); errs()<<"\n";
			errs()<<"LV[1]: ";
			LoadValues[1]->print(errs()); errs()<<"\n";
			errs()<<"RAV: ";
			RedargVal->print(errs()); errs()<<"\n";
			errs()<<"RVT: ";
			RedVarTy->print(errs()); errs()<<"\n";

			if( (RedVarTy->isDoubleTy() || RedVarTy->isFloatTy()) )
			{
				LoadValues[1] = Builder.CreateBitCast(
					LoadValues[1],
					RedVarTy
				);
			}
			Value* RedPHIVal = dyn_cast<Value>(PHIN);
				//we don't have to use RedPHIVal, just use LoadValues[1]
				//but it could be a complicated code, so use RedPHIVal

			// 		if RedOp is * or not Int
			// 			6. RedOp 3., 4.
			Value* ResultVal;
			switch(reditr.second.second)
			{
				case ParamGetCommon::reduction_operator::add:
				case ParamGetCommon::reduction_operator::sub:
					if( RedVarTy->isFloatTy() || RedVarTy->isDoubleTy() )
					{
						ResultVal = Builder.CreateFAdd(
							LoadValues[0],
							LoadValues[1]
						);
					}else
					{
						ResultVal = Builder.CreateNSWAdd(
							LoadValues[0],
							LoadValues[1]									
						);
					}		
					break;
				case ParamGetCommon::reduction_operator::mul:
					if( RedVarTy->isFloatTy() || RedVarTy->isDoubleTy() )
					{
						ResultVal = Builder.CreateFMul(
							LoadValues[0],
							LoadValues[1]
						);
					}else
					{
						ResultVal = Builder.CreateNSWMul(
							LoadValues[0],
							LoadValues[1]									
						);
					}						
					break;
				case ParamGetCommon::reduction_operator::and_bin:
					ResultVal = Builder.CreateAnd(
						LoadValues[0],
						LoadValues[1]									
					);
					break;
				case ParamGetCommon::reduction_operator::or_bin:
					ResultVal = Builder.CreateOr(
						LoadValues[0],
						LoadValues[1]									
					);
					break;
				case ParamGetCommon::reduction_operator::xor_bin:	
					ResultVal = Builder.CreateXor(
						LoadValues[0],
						LoadValues[1]									
					);
					break;
				
				case ParamGetCommon::reduction_operator::and_and:
					if(true)
					{
						// 			5. ICmp ne 4., 0
						Value* CmpVal;
						if( RedVarTy->isDoubleTy() || RedVarTy->isFloatTy() )
						{
							CmpVal = Builder.CreateFCmp(
								CmpInst::Predicate::ICMP_NE,
								LoadValues[1],
								dyn_cast<Value>( ConstantFP::get(RedVarTy, (double)0) )
							);
						}else
						{
							CmpVal = Builder.CreateICmp(
								CmpInst::Predicate::ICMP_NE,
								LoadValues[1],
								Builder.getIntN(RedVarTy->getIntegerBitWidth(),0)
							);	
						}
						// 			6. RedOp 5., 3'
						Value* SrcVal = Builder.CreateAnd(
							CmpVal,
							LoadValues[0]
						);
						// 			6', zext 6. to i32
						ResultVal = Builder.CreateZExt(
							SrcVal,
							Builder.getIntNTy(bitwidth)
						);
					}
					break;
				case ParamGetCommon::reduction_operator::or_or:
					if(true)
					{
						// 			5. RedOp 3., 4
						Value* SrcVal = Builder.CreateOr(
							LoadValues[0],
							LoadValues[1]
						);
						// 			6. ICmp ne 5., 0
						Value* CmpVal;
						if( RedVarTy->isDoubleTy() || RedVarTy->isFloatTy() )
						{
							CmpVal = Builder.CreateFCmp(
								CmpInst::Predicate::ICMP_NE,
								SrcVal,
								dyn_cast<Value>( ConstantFP::get(RedVarTy, (double)0) )
							);
						}else
						{
							CmpVal = Builder.CreateICmp(
								CmpInst::Predicate::ICMP_NE,
								SrcVal,
								Builder.getIntN(RedVarTy->getIntegerBitWidth(),0)
							);	
						}
						// 			6'. zext 6. to i32
						ResultVal = Builder.CreateZExt(
							CmpVal,
							Builder.getIntNTy(bitwidth)
						);
					}
					break;
				case ParamGetCommon::reduction_operator::max:
				case ParamGetCommon::reduction_operator::min:
					if(true)
					{
						Value* redV = reditr.second.first;
						CmpInst *RedCmp;
						Value* IncomingV = getCmpInstForMaxMin(targetLoop, redV);
						if((RedCmp = dyn_cast<CmpInst>(IncomingV)))
						{
							//just want to assign the RedCmp
						}else
						{
							errs()<<"RedCmp is not calculated\n";
							redV->dump();
						}		

						Value* newRedCmp;//Its predicate would be swapped predicate of RedCmp
						if( RedCmp->isFPPredicate() )
						{
							newRedCmp = Builder.CreateFCmp(
								/*CmpInst::Predicate P*/RedCmp->getSwappedPredicate(),
								/*Value *LHS*/LoadValues[0],
								/*Value *RHS*/LoadValues[1]
							);
						}else if( RedCmp->isIntPredicate() )
						{
							newRedCmp = Builder.CreateICmp(
								/*CmpInst::Predicate P*/RedCmp->getSwappedPredicate(),
								/*Value *LHS*/LoadValues[0],
								/*Value *RHS*/LoadValues[1]
							);
						}else//error
						{
							errs()<<"CmpInst's predicate is not what we can handle\n";
							RedCmp->dump();
						}				

						//2.select 1., if true "RedVar in Arg", else "RedVar in body"
						Value* newSelectVal = Builder.CreateSelect(
							newRedCmp,
							LoadValues[0],
							LoadValues[1]
						);
						string str = to_string(instnum++);
						newSelectVal->setName( str );
						BuiltValMap.insert(make_pair(
							str,
							newSelectVal
						));//necessary to use this select in creating other inst for RedVar

						//generate same instruction used to create RedVar(redV) after SelectInst
						ResultVal = createOtherInstForRedVar(Builder, targetLoop, redV, newSelectVal);


					}
					break;
				default:
					break;
			}

			// 		else if RedOp is ||
	


			// 		7. if reditr.second->getType() is not Int,
			// 			bitcast 6. to i32
			if( RedVarTy->isDoubleTy() || RedVarTy->isFloatTy() )
			{
				ResultVal = Builder.CreateBitCast(
					ResultVal,
					Builder.getIntNTy(bitwidth<<3)
				);
			}
			//(debug)
			errs()<<"RedargVal:\t";
			RedargVal->print(errs()); errs()<<"\n";
			errs()<<"RedPHIVal:\t";
			RedPHIVal->print(errs()); errs()<<"\n";
			errs()<<"ResultVal:\t";
			ResultVal->print(errs()); errs()<<"\n";

			// 		8. cmpxchg 1., 4., 6. monotonic monotonic
			// 			[cmpxchg]
			// 			It loads a value in memory and compares ito to a given value.
			// 			If they are equal, it tries to store a new value into the memory.
			// 			Operand1: an address to operate on
			// 			Operand2: a value to compare to the value currently be at that address
			// 			Operand3: a new value to place at that address if the compared values are ewual
			Value* Agg = dyn_cast<Value>(
				Builder.CreateAtomicCmpXchg(
					/*Value* Ptr*/RedargVal,
					/*Value* Cmp*/RedPHIVal,
					/*Value* New*/ResultVal,
					/*AtomicOrdering SuccessOrdering*/AtomicOrdering::Monotonic,
					/*AtomicOrdering FailureOrdering*/AtomicOrdering::Monotonic
					/*SyncScope::ID SSID=SyncScope::System*/
				)
			);

			// 		9. extractvalue { {i16 or i32}, i1 } 8., 0
			vector<unsigned> Idx;
			Idx.push_back(0);
			ArrayRef<unsigned> Idxs1(Idx);
			Value* ExtVal = Builder.CreateExtractValue(
				/*Value* Agg*/Agg,
				/*ArrayRef<unsigned> Idxs*/Idxs1
			);
			PHIN->addIncoming(ExtVal, atomic_contBB);

			// 		10. extractvalue { {i16 or i32}, i1 } 8., 1
			//vector<unsigned> Idx;
			Idx.clear();
			Idx.push_back(1);
			ArrayRef<unsigned> Idxs2(Idx);
			ExtVal = Builder.CreateExtractValue(
				/*Value* Agg*/Agg,
				/*ArrayRef<unsigned> Idxs*/Idxs2
			);
			// 		11. if 10. is true, atomic_exit, else atomic_cont
			BasicBlock* atomic_exitBB;
			if( ReductionValMap.rbegin()->first == reductionName )
			{
				atomic_exitBB = endBB;
			}else
			{
				atomic_exitBB = BasicBlock::Create(TheContext,"",OutFunc,endBB);
			}	
			Builder.CreateCondBr(
				ExtVal,
				atomic_exitBB,
				atomic_contBB
			);
			Builder.SetInsertPoint(atomic_exitBB);
			// 			if there is no reditr, atomic_exit is endBB
			
		}else
		{	// 	if reditr.second is Int and (operator is +,-,&,|,^)
			// 		1. load "RedVar in body"
			Value* LoadVal = Builder.CreateAlignedLoad(
				BuiltValMap.find(reductionName+"_Local")->second,
				align
			);
			Value* argVal = BuiltValMap.find(reditr.first + "_Arg")->second;
			if( RedVarTy->isVectorTy() )
			{
				argVal = Builder.CreateAlignedLoad(
					argVal,
					align
				);
			}

			//special process for array type 
			if( RedVarTy->isArrayTy() || RedVarTy->isVectorTy() )
			{			
				// Instruction::BinaryOps Opc;
				switch(reditr.second.second)
				{						
					//if RedOp is {+,-}
					case ParamGetCommon::reduction_operator::add:
					case ParamGetCommon::reduction_operator::sub:
						// Opc = Instruction::BinaryOps::Add;
						Builder.CreateNSWAdd(
							argVal,
							LoadVal
							);
						break;
					//else (Red Op is {&,|,^})
					case ParamGetCommon::reduction_operator::and_bin:
						// Opc = Instruction::BinaryOps::And;
						Builder.CreateAnd(
							argVal,
							LoadVal
							);
						break;							
					case ParamGetCommon::reduction_operator::or_bin:
						// Opc = Instruction::BinaryOps::Or;
						Builder.CreateOr(
							argVal,
							LoadVal
							);
						break;			
					case ParamGetCommon::reduction_operator::xor_bin:
						// Opc = Instruction::BinaryOps::Xor;
						Builder.CreateXor(
							argVal,
							LoadVal
							);
						break;						
					default:
						break;
				}
				// Builder.CreateBinOp(
				// 	Opc,
				// 	argVal,
				// 	LoadVal
				// );
			}else
			{
				switch(reditr.second.second)
				{						
					//if RedOp is {+,-}
					case ParamGetCommon::reduction_operator::add:
					case ParamGetCommon::reduction_operator::sub:
						// 2. atomicrmw add "RedVar in Arg", 1.
						Builder.CreateAtomicRMW(
							/*AtomicRMWInst::BinOp Op*/AtomicRMWInst::BinOp::Add,
							/*Value* Ptr*/argVal,
							/*Value* Val*/LoadVal,
							/*AtomicOrdering Ordering*/AtomicOrdering::Monotonic
							/*SyncScope::ID SSID=SyncScope::System*/
						);
						break;
					//else (Red Op is {&,|,^})
					case ParamGetCommon::reduction_operator::and_bin:
						Builder.CreateAtomicRMW(
							/*AtomicRMWInst::BinOp Op*/AtomicRMWInst::BinOp::And,
							/*Value* Ptr*/argVal,
							/*Value* Val*/LoadVal,
							/*AtomicOrdering Ordering*/AtomicOrdering::Monotonic
							/*SyncScope::ID SSID=SyncScope::System*/
						);
						break;							
					case ParamGetCommon::reduction_operator::or_bin:
						Builder.CreateAtomicRMW(
							/*AtomicRMWInst::BinOp Op*/AtomicRMWInst::BinOp::Or,
							/*Value* Ptr*/argVal,
							/*Value* Val*/LoadVal,
							/*AtomicOrdering Ordering*/AtomicOrdering::Monotonic
							/*SyncScope::ID SSID=SyncScope::System*/
						);
						break;							
					case ParamGetCommon::reduction_operator::xor_bin:
						//2. atomicrmw {and,or,xor} "RedVar in Arg", 1.
						Builder.CreateAtomicRMW(
							/*AtomicRMWInst::BinOp Op*/AtomicRMWInst::BinOp::Xor,
							/*Value* Ptr*/argVal,
							/*Value* Val*/LoadVal,
							/*AtomicOrdering Ordering*/AtomicOrdering::Monotonic
							/*SyncScope::ID SSID=SyncScope::System*/
						);
						break;
					default:
						break;
				}			
			}

	

			// 		3.if there is not reditr, create br to endBB
			// 			else do not create br
			if( ReductionValMap.rbegin()->first == reductionName )
			{
				Builder.CreateBr(endBB);
				Builder.SetInsertPoint(endBB);
			}
		}
	}
			

}

void ParamGet::ParamGetCommon::buildReductionFuncBody(Function* RedFunc, 
		IRBuilder<> &Builder, Loop* targetLoop)
{
	BasicBlock* OriginalInsertBlock = Builder.GetInsertBlock();
	auto OriginalInsertPoint = Builder.GetInsertPoint();


	//(debug)
	errs()<<"Original Insert Point is:\t";
	if(OriginalInsertPoint == OriginalInsertBlock->end())
	{
		errs()<<"end of BB\n";
	}else
	{
		(*OriginalInsertPoint).print(errs()); errs()<<"\n";
	}
	


	// //omp.reduction.reduction_func // called from  kmpc_reduce_nowait
	LLVMContext &TheContext = RedFunc->getContext();
	map<string, pair<Value*,Value*>> ArgLoadValsMap;
	Value* argVal[2] = {
		dyn_cast<Value>( &*(RedFunc->arg_begin()) ),
		dyn_cast<Value>( &*(RedFunc->arg_begin()+1) )
	};
	Value* LoadValues[2];

	Builder.SetInsertPoint(
		BasicBlock::Create(TheContext,"",RedFunc)
	);

	// 1. bitcast Arg[1] to reditr->second->getType()->getPointerTo()
	Value* BCSrc = Builder.CreateBitCast(
		dyn_cast<Value>( argVal[1] ), //Arg[1]
		BuiltValMap.find( ReductionValMap.begin()->first + "_Local")->second->getType()->getPointerTo()
	);
	// 2. load from 1. //atomic
	LoadValues[0] = dyn_cast<Value>(
		Builder.CreateAlignedLoad(
			BCSrc,
			8// Pointer Alignment is always 8
		)
	);
	// 3. bitcast Arg[0] to reditr->second->getType()->getPointerTo()
	BCSrc = Builder.CreateBitCast(
		dyn_cast<Value>( argVal[0] ), //Arg[0]
		BuiltValMap.find( ReductionValMap.begin()->first + "_Local")->second->getType()->getPointerTo()
	);
	// 4. load from 3.
	LoadValues[1] = dyn_cast<Value>(
		Builder.CreateAlignedLoad(
			BCSrc,
			8// Pointer Alignment is always 8
		)
	);
	ArgLoadValsMap.insert(make_pair(
		ReductionValMap.begin()->first,
		make_pair(
			LoadValues[0],
			LoadValues[1]
		)
	));

	// reditr start
	unsigned redcounter = 1;
	for(auto reditr : ReductionValMap)
	{
		if(reditr == *ReductionValMap.begin())
		{//we want to use "reditr = PGC.ReductionValMap.begin() + 1", but it's not allowed
			continue;
		}

		Type* BitCastDstTy = BuiltValMap.find(reditr.first + "_Local")->second->getType()->getPointerTo();

		// 5. getelementptr Arg[1], Idx = reditr-count * 8
		Value* GEPVal = Builder.CreateInBoundsGEP(
			Builder.getInt8Ty(), //RedFunc's Arg is Int8TyPtr, So GEP of it is Int8Ty
			argVal[1],
			Builder.getInt64( redcounter*8 )
		); 
		// 6. bitcast 5. to reditr.second->getType()->getPointerTo()
		BCSrc = Builder.CreateBitCast(
			dyn_cast<Value>( GEPVal ),
			BitCastDstTy
		);
		// 7. load from 6.
		LoadValues[0] = dyn_cast<Value>(
			Builder.CreateAlignedLoad(
				BCSrc,
				8// Pointer Alignment is always 8
			)
		);


		// 8. getelementptr Arg[0], Idx = reditr-count * 8
		GEPVal = Builder.CreateInBoundsGEP(
			Builder.getInt8Ty(), //RedFunc's Arg is Int8TyPtr, So GEP of it is Int8Ty
			argVal[0],
			Builder.getInt64( redcounter*8 )
		); 
		// 9. bitcast 7. to reditr->second->getType()->getPointerTo()
		BCSrc = Builder.CreateBitCast(
			dyn_cast<Value>( GEPVal ),
			BitCastDstTy
		);
		// 10. load from 9.
		LoadValues[1] = dyn_cast<Value>(
			Builder.CreateAlignedLoad(
				BCSrc,
				8// Pointer Alignment is always 8
			)
		);

		ArgLoadValsMap.insert(make_pair(
			reditr.first,
			make_pair(
				LoadValues[0],
				LoadValues[1]
			)
		));
		redcounter++;
	}
	// reditr end

	// reditr start
	for(auto reditr : ReductionValMap)
	{
		unsigned align = 4;
		unsigned bitwidth = 32; // used in and_and , or_or
		string reductionName = reditr.first;
		Type* RedVarTy = dyn_cast<PointerType>( 
			BuiltValMap.find(reductionName + "_Local")->second->getType() 
		)->getElementType();

		if(!RedVarTy)
		{
			errs()<<"Sorry we can handle ONLY memory access ReductionVar\n";
		}
		if( RedVarTy->isIntegerTy(64) || RedVarTy->isPointerTy() || RedVarTy->isDoubleTy() )
		{
			align = 8;
			if(RedVarTy->isIntegerTy(64))
			{
				bitwidth = 64;
			}
		}//we don't handle PointerTy for now
		else if(RedVarTy->isIntegerTy(16))
		{
			align = 2;
			bitwidth = 16;
		}


		switch( reditr.second.second )
		{
			// if RedOp is not && or ||
			case reduction_operator::add:
			case reduction_operator::mul:
			case reduction_operator::sub:
			case reduction_operator::and_bin:
			case reduction_operator::or_bin:
			case reduction_operator::xor_bin:	
				if( ArgLoadValsMap.find(reductionName) != ArgLoadValsMap.end() )
				{

					// 	11. load from 4.(or 10.)
					LoadValues[0] = Builder.CreateAlignedLoad(
						ArgLoadValsMap.find(reductionName)->second.second,
						align
					);			
					// 	12. load from 2.(or 7.)
					LoadValues[1] = Builder.CreateAlignedLoad(
						ArgLoadValsMap.find(reductionName)->second.first,
						align
					);			
					// 	13. Reduction-operator 12., 11.
					Value* SrcVal;
					switch(reditr.second.second)
					{
						case reduction_operator::add:
						case reduction_operator::sub:
							if( RedVarTy->isFloatTy() || RedVarTy->isDoubleTy() )
							{
								SrcVal = Builder.CreateFAdd(
									LoadValues[1],
									LoadValues[0]
								);
							}else
							{
								SrcVal = Builder.CreateNSWAdd(
									LoadValues[1],
									LoadValues[0]									
								);
							}
							break;
						case reduction_operator::mul:
							if( RedVarTy->isFloatTy() || RedVarTy->isDoubleTy() )
							{
								SrcVal = Builder.CreateFMul(
									LoadValues[1],
									LoadValues[0]
								);
							}else
							{
								SrcVal = Builder.CreateNSWMul(
									LoadValues[1],
									LoadValues[0]									
								);
							}						
							break;
						case reduction_operator::and_bin:
							SrcVal = Builder.CreateAnd(
								LoadValues[1],
								LoadValues[0]									
							);
							break;
						case reduction_operator::or_bin:
							SrcVal = Builder.CreateOr(
								LoadValues[1],
								LoadValues[0]									
							);
							break;
						case reduction_operator::xor_bin:	
							SrcVal = Builder.CreateXor(
								LoadValues[1],
								LoadValues[0]									
							);
							break;
						default:
							break;
					}

					// 	14. Store 13. to 4.(or 10.)
					Builder.CreateAlignedStore(
						SrcVal,
						ArgLoadValsMap.find(reductionName)->second.second,
						align
					);
				}
				break;
			//else
			case reduction_operator::and_and:
			case reduction_operator::or_or:
				if( ArgLoadValsMap.find(reductionName) != ArgLoadValsMap.end() )
				{
					// 	11. Load from 4.(or 10.)
					LoadValues[0] = Builder.CreateAlignedLoad(
						ArgLoadValsMap.find(reductionName)->second.second,
						align
					);						
					// 	12. ICmp eq 11., 0
					Value* CmpVal;
					if( RedVarTy->isDoubleTy() || RedVarTy->isFloatTy() )
					{
						CmpVal = Builder.CreateFCmp(
							CmpInst::Predicate::ICMP_EQ,
							LoadValues[0],
							dyn_cast<Value>( ConstantFP::get(RedVarTy, (double)0) )
						);
					}else
					{
						CmpVal = Builder.CreateICmp(
							CmpInst::Predicate::ICMP_EQ,
							LoadValues[0],
							Builder.getIntN(RedVarTy->getIntegerBitWidth(),0)
						);	
					}
					BasicBlock* prevBB = Builder.GetInsertBlock();
					BasicBlock* rhsBB = BasicBlock::Create(TheContext,"",RedFunc);
					BasicBlock* endBB = BasicBlock::Create(TheContext,"",RedFunc);
					// 	13. br 12., true:l.end, false:l.rhs
					Builder.CreateCondBr(
						CmpVal,
						endBB,
						rhsBB
					);

					// 	l.rhs:
					Builder.SetInsertPoint(rhsBB	);
					// 	14. Load from 2.(or 7.)
					LoadValues[1] = Builder.CreateAlignedLoad(
						ArgLoadValsMap.find(reductionName)->second.first,
						align
					);			
					// 	15. ICmp ne 14., 0
					if( RedVarTy->isDoubleTy() || RedVarTy->isFloatTy() )
					{
						CmpVal = Builder.CreateFCmp(
							CmpInst::Predicate::ICMP_NE,
							LoadValues[1],
							dyn_cast<Value>( ConstantFP::get(RedVarTy, (double)0) )
						);
					}else
					{
						CmpVal = Builder.CreateICmp(
							CmpInst::Predicate::ICMP_NE,
							LoadValues[1],
							Builder.getIntN(RedVarTy->getIntegerBitWidth(),0)
						);	
					}
					// 	16. zext 15. to {i16,32,64}
					Value* ZExtVal = Builder.CreateZExt(
						CmpVal,
						Builder.getIntNTy( bitwidth )
					);
					

					Builder.CreateBr(endBB);

					// 	l.end
					Builder.SetInsertPoint(endBB	);
					// 	if RedOp is &&
					// 		17. phi i32 [0, entry], [16., l.rhs]
					// 	else 
					// 		17. phi i32 [1, entry], [16., l.rhs]
					PHINode* PHIN = Builder.CreatePHI(Builder.getIntNTy(bitwidth), 2);
					switch( reditr.second.second )
					{
						case reduction_operator::and_and:
							PHIN->addIncoming( Builder.getIntN(bitwidth,0), prevBB );
							PHIN->addIncoming( ZExtVal, rhsBB );
							break;
						case reduction_operator::or_or:
							PHIN->addIncoming( Builder.getIntN(bitwidth,1), prevBB );
							PHIN->addIncoming( ZExtVal, rhsBB );
							break;
						default:
							break;
					}

					// 	18. Store 17. to 4.(or 10.)
					Builder.CreateAlignedStore(
						dyn_cast<Value>(PHIN),
						ArgLoadValsMap.find(reductionName)->second.second,
						align
					);
				}
				break;
			
			//max, min
			case reduction_operator::max:
			case reduction_operator::min:
				if(true)
				{
					//0.Load "RedVar in Arg" and "RedVar in body"	
					Value* LoadValues[2];		
					LoadValues[0] = dyn_cast<Value>(Builder.CreateAlignedLoad(
						ArgLoadValsMap.find( reductionName )->second.second,
						align
					));	
					LoadValues[1] = dyn_cast<Value>(Builder.CreateAlignedLoad(
						ArgLoadValsMap.find( reductionName )->second.first,
						align
					));
					//1.[fcmp or icmp] [ogt, sgt, ugt] "RedVar in Arg", "RedVar in body"
					//determine the predicate
					Value* redV = reditr.second.first;
					CmpInst *RedCmp;
					Value* IncomingV = getCmpInstForMaxMin(targetLoop, redV);
					if((RedCmp = dyn_cast<CmpInst>(IncomingV)))
					{
						//just want to assign RedCmp
					}else
					{
						errs()<<"RedCmp is not calculated\n";
						redV->dump();
					}

					//now, RedCmp is original compare inst of this reduction
					Value* newRedCmp;
					if( RedCmp->isFPPredicate() )
					{
						newRedCmp = Builder.CreateFCmp(
							/*CmpInst::Predicate P*/RedCmp->getPredicate(),
							/*Value *LHS*/LoadValues[0],
							/*Value *RHS*/LoadValues[1]
						);
					}else if( RedCmp->isIntPredicate() )
					{
						newRedCmp = Builder.CreateICmp(
							/*CmpInst::Predicate P*/RedCmp->getPredicate(),
							/*Value *LHS*/LoadValues[0],
							/*Value *RHS*/LoadValues[1]
						);
					}else//error
					{
						errs()<<"CmpInst's predicate is not what we can handle\n";
						RedCmp->dump();
					}

					//2.select 1., if true "RedVar in Arg", else "RedVar in body"
					Value* newSelectVal = Builder.CreateSelect(
						newRedCmp,
						LoadValues[0],
						LoadValues[1]
					);
					string str = to_string(instnum++);
					newSelectVal->setName( str );
					BuiltValMap.insert(make_pair(
						str,
						newSelectVal
					));//necessary to use this select in creating other inst for RedVar

					//2.1. generate same instruction used to create RedVar(redV) after SelectInst
					Value* newRedVal = createOtherInstForRedVar(Builder, targetLoop, redV, newSelectVal);

					//3.Store 2. to "RedVar in Arg" 
					Builder.CreateAlignedStore(
						newRedVal,
						ArgLoadValsMap.find(reductionName)->second.second,
						align
					);	
				}

			default:
				break;
		}
	}
	// reditr end
	// 15. ret void
	Builder.CreateRetVoid();
	Builder.SetInsertPoint(OriginalInsertBlock, OriginalInsertPoint);

}

Value* ParamGet::ParamGetCommon::createOtherInstForRedVar(IRBuilder<> &Builder,
		Loop* targetLoop, Value* redV, Value* newSelectVal)
{
	Value* IncomingV;
	if(PHINode* PHIN = dyn_cast<PHINode>(redV))
	{
		for(unsigned i=0; i<PHIN->getNumOperands(); i++)
		{
			if(targetLoop->contains( PHIN->getIncomingBlock(i) ))
			{
				IncomingV = PHIN->getIncomingValue(i);
				break;
			}
		}
		return getInstAfterSelect(Builder, newSelectVal, IncomingV);

	}else //we check it is stored
	{
		for(auto useritr = redV->user_begin(); useritr != redV->user_end(); ++useritr)
		{
			if( StoreInst* StoreI = dyn_cast<StoreInst>((*useritr)) )
			{
				if( targetLoop->contains(StoreI) )
				{
					IncomingV = StoreI->getOperand(0);
					return getInstAfterSelect(Builder, newSelectVal, IncomingV);
				}
			}
		}
	}	
	errs()<<"### error value return ###\n";
	IncomingV->dump();
	return IncomingV;
}
Value* ParamGet::ParamGetCommon::getInstAfterSelect(IRBuilder<> &Builder, Value* SelectV, Value* resultV)
{
	if( dyn_cast<SelectInst>(resultV) )
	{
		return SelectV;
	}
	if(Instruction* ptrI = dyn_cast<Instruction>(resultV))
	{
		for(unsigned i=0; i<ptrI->getNumOperands(); i++)
		{
			if(dyn_cast<Instruction>( ptrI->getOperand(i) ))
			{
				resultV = ptrI->getOperand(i);
				//debug
				errs()<<"getSelectInst: resultV-> ";
				resultV->print(errs()); errs()<<"\n";
				resultV = getInstAfterSelect(Builder, SelectV, resultV);
				ptrI->setOperand(i, resultV);
			}
		}
	  	return buildInstruction(Builder, ptrI);
	}
	errs()<<"### error value return ###\n";
	resultV->dump();
	return resultV;
}


void ParamGet::ParamGetCommon::createStoreForLastPrivate(IRBuilder<> &Builder)
{	
	BasicBlock* initBB = this->static_init_BB;
	BasicBlock* loopexitBB = dyn_cast<Instruction>(
			BuiltValMap.find( LoopExpr.find("cond")->second->getName().str() )->second
		)->getParent();
	Function* OutFunc = Builder.GetInsertBlock()->getParent();
	BasicBlock* lastprivate_store_BB = BasicBlock::Create(Builder.getContext(), "", OutFunc);
	BasicBlock* outlined_exit_BB = BasicBlock::Create(Builder.getContext(), "", OutFunc);
	Value* plastiterVal = OMPParamMap.find("plastiter")->second;

	StrVmap LastPrivateStoreMap;

	// //phi must be at the begining of  basic block
	// Builder.SetInsertPoint( &*(Builder.GetInsertBlock()->begin()) );

	//create phi for lastprivate value
	for(auto valitr = LastPrivateValMap.begin(); valitr != LastPrivateValMap.end(); ++valitr)
	{
		string str = valitr->first;
		Value* SrcVal = BuiltValMap.find(str)->second;
		Builder.SetInsertPoint( Builder.GetInsertBlock()->getFirstNonPHI() );
		//debug
		errs()<<"lastprivate src before: ";
		SrcVal->print(errs()); errs()<<"\n";

		Value* priV = LastPrivateValMap.find(str)->second;
		errs()<<"private var : ";
		priV->print(errs()); errs()<<"\n";

		Value* InitialV = UndefValue::get(SrcVal->getType());
		if( FirstPrivateValMap.find(str) != FirstPrivateValMap.end() )//this is not tested
		{
			InitialV = FirstPrivateValMap.find(str+".InitialValue")->second;
		}

		Type* judgeTy = priV->getType();
		if( dyn_cast<PointerType>(priV->getType()) )
		{
			judgeTy = priV->getType()->getPointerElementType();
		}
		switch( judgeTy->getTypeID() )
		{
			case Type::TypeID::StructTyID:
			case Type::TypeID::ArrayTyID:
			case Type::TypeID::VectorTyID:
				if(true)//this process is same to "else" process 
				{
					PHINode* PHIN = Builder.CreatePHI( SrcVal->getType(), 2 );
					PHIN->addIncoming(SrcVal, loopexitBB);
					//this value is taken if lastiteration is not executed.
					PHIN->addIncoming(InitialV, initBB);
					SrcVal = dyn_cast<Value>(PHIN);
				}
				break;
			default:	
				if( !dyn_cast<AllocaInst>(priV) && !dyn_cast<GlobalVariable>(priV) && !dyn_cast<PointerType>(judgeTy) )
				{			
					//if scalar, alloca or global, we pass them with phi
					PHINode* PHIN = Builder.CreatePHI( SrcVal->getType(), 2 );
					PHIN->addIncoming(SrcVal, loopexitBB);
					//this value is taken if lastiteration is not executed.
					PHIN->addIncoming(InitialV, initBB);
					SrcVal = dyn_cast<Value>(PHIN);
				}else
				{					
					SrcVal = dyn_cast<Value>(Builder.CreateAlignedLoad(
						SrcVal,
						calcAlignOfType(priV->getType())
					));	
				}
				break;
		}

	


		//debug
		errs()<<"lastprivate src after: ";
		SrcVal->print(errs()); errs()<<"\n";

		//record to store the phi
		LastPrivateStoreMap.insert(make_pair(str, SrcVal));
	}
	Builder.SetInsertPoint( Builder.GetInsertBlock() );

	//condition branch to lastprivate_store_BB
	Value* LoadVal = dyn_cast<Value>(Builder.CreateAlignedLoad(
		plastiterVal,
		dyn_cast<AllocaInst>( plastiterVal )->getAlignment()
	));

	//(debug)
	errs()<<"lastiteration flag Load: ";
	LoadVal->print(errs()); errs()<<"\n";
	errs()<<"getIntegerBitWidth: "<<LoadVal->getType()->getIntegerBitWidth()<<"\n";

	Value* ICmpVal = Builder.CreateICmp(
		CmpInst::Predicate::ICMP_EQ,
		LoadVal,
		Builder.getIntN(LoadVal->getType()->getIntegerBitWidth(), 0)
	);
	Builder.CreateCondBr(
		/*cond*/ICmpVal,
		/*true branch*/outlined_exit_BB,
		/*false branch*/lastprivate_store_BB
	);

	//create lastprivate_store_BB
	Builder.SetInsertPoint(lastprivate_store_BB);
	for(auto valitr = LastPrivateValMap.begin(); valitr != LastPrivateValMap.end(); ++valitr)
	{
		string str = valitr->first;
		Value* SrcVal = LastPrivateStoreMap.find(str)->second;
		Value* DstVal;
		if( BuiltValMap.find(str+".LastPrivateArgument") != BuiltValMap.end() )
		{
			//(debug)
			errs()<<"Dst is from lastprivate\n";
			DstVal = BuiltValMap.find(str+".LastPrivateArgument")->second;
		}else if( BuiltValMap.find(str+".FirstPrivateArgument") != BuiltValMap.end()  )
		{
			//(debug)
			errs()<<"Dst is from firstprivate\n";
			DstVal = BuiltValMap.find(str+".FirstPrivateArgument")->second;
		}else if( dyn_cast<GlobalVariable>( valitr->second ) )
		{
			//(debug)
			errs()<<"Dst is from global variable\n";
			DstVal = valitr->second;
		}else
		{
			//(debug)
			errs()<<"Dst is from what?\n";
			DstVal = BuiltValMap.find(str)->second;
		}
		//debug
		errs()<<"lastprivate str: "<<str<<"\n";
		errs()<<"lastprivate store src: ";
		SrcVal->print(errs()); errs()<<"\n";
		errs()<<"lastprivate store dst: ";
		DstVal->print(errs()); errs()<<"\n";

		//memcpy or store
		unsigned align = calcAlignOfType(SrcVal->getType());
		Value* OrigPriV;
		if( FuncArgumentMap.find(str) != FuncArgumentMap.end() )
		{
			OrigPriV = FuncArgumentMap.find(str)->second;
		}else if( dyn_cast<GlobalVariable>( valitr->second )  )
		{
			OrigPriV = valitr->second;
		}
		Type* OrigPriTy = OrigPriV->getType()->getPointerElementType();

		switch(OrigPriTy->getTypeID())
		{
			case Type::TypeID::StructTyID:
			case Type::TypeID::ArrayTyID:
			case Type::TypeID::VectorTyID:
				//align get
				if( AllocaInst* AI = dyn_cast<AllocaInst>(OrigPriV) )
				{
					align = AI->getAlignment();
				}else if( GlobalVariable* GV = dyn_cast<GlobalVariable>(OrigPriV) )
				{
					align = GV->getAlignment();
				}else //Argument is handled here
				{
					//set the biggest align of elements in Structure
					//this may be wrong. 
					//How to know correct align?
					align = calcAlignOfType(OrigPriTy);
				}
				if(align)// allocate is only for pointers
				{
					//size calculate
					unsigned int TypeSize = calcSizeOfType( OrigPriTy );

					//if typesize is not aligned to align, add some value
					if( (OrigPriTy->getTypeID() == Type::TypeID::StructTyID) && (TypeSize % align != 0))
					{
						//set Size to match alignment
						TypeSize += align - (TypeSize % align);
					}
					
					//(debug)
					// errs()<<"Type:\t";
					// priTy->print(errs()); errs()<<"\n";
					// errs()<<"TypeSize: "<<TypeSize<<"\n";
					// errs()<<"str->val: ";
					// SrcVal->print(errs()); errs()<<"\n";
					// errs()<<"str.arg->val: ";
					// DstVal->print(errs()); errs()<<"\n";
					//************************************** create bitcast or search bitcast

					errs()<<"str "<<str<<"\n";
					Builder.CreateMemCpy(
						/*Value *Dst*/DstVal,
						/*unsigned DstAlign*/align,
						/*Value *Src*/SrcVal,
						/*unsigned SrcAlign*/align,
						/*uing64_t Size*/TypeSize
					);
				}
				break;
			default:
				Builder.CreateAlignedStore(
					SrcVal,
					DstVal,
					align
				);
				break;

		}
	}

	//create br to outlined_exit_BB
	Builder.CreateBr(outlined_exit_BB);
	Builder.SetInsertPoint(outlined_exit_BB);
}

//this function is not used
//the load process is handled in createLoadForShareAfterLoop function
// void ParamGet::ParamGetCommon::createLoadForLastPrivate(IRBuilder<> &Builder, Loop* targetLoop)
// {
// 	//phi's incoming BasicBlock is already modified in createLoadForShareAfterLoop

// 	//unsigned align;
// 	//in end of loop
// 	//1.create store to alloca
// 	BasicBlock* LoopPreheaderBB = CapturedInstVector.begin()->second->getParent();	
// 	Builder.SetInsertPoint(LoopPreheaderBB->getTerminator());

// 	Value* LoadVal;
// 	Value* LoadedVal;
// 	for(auto valitr = LastPrivateValMap.begin(); valitr != LastPrivateValMap.end(); ++valitr)
// 	{
// 		string str = valitr->first;
// 		LoadedVal = FuncArgumentMap.find(str)->second;

// 		//check pointer element depth
// 		//if depth is same, there is no need to load,
// 		//this is because outlined func already stored the lastiteration value. 
// 		// unsigned fargcounter = 0;
// 		// unsigned fpricounter = 0;
// 		// Type* fargTy = LoadedVal->getType();
// 		// Type* fpriTy = valitr->second->getType();
// 		// while( dyn_cast<PointerType>(fargTy) )
// 		// {
// 		// 	fargcounter++;
// 		// 	fargTy = fargTy->getPointerElementType();
// 		// }
// 		// while( dyn_cast<PointerType>(fpriTy) )
// 		// {
// 		// 	fpricounter++;
// 		// 	fpriTy = fpriTy->getPointerElementType();
// 		// }
// 		// if(fargcounter == fpricounter)
// 		// {
// 		// 	continue;
// 		// }
// 		LoadVal = dyn_cast<Value>(Builder.CreateAlignedLoad(
// 			LoadedVal,
// 			calcAlignOfType( valitr->second->getType() )
// 		));

// 		//LastPrivate could be global or argument, so it is OK if it's not Alloca
// 		//errs()<<"LastPrivate is not alloca\n";
// 		//valitr->second->dump();


// 		//replace use
// 		valitr->second->replaceAllUsesWith(LoadVal);
// 	}
// }


unsigned int ParamGet::ParamGetCommon::calcSizeOfType(Type* priT)
{
	unsigned int TypeSize = 0;
	switch(priT->getTypeID())
	{
		case Type::TypeID::HalfTyID:
			TypeSize = priT->getPrimitiveSizeInBits()>>3;//this may be wrong
			if(TypeSize == 0)
			{
				errs()<<"getPrimitiveSizeInBits is not OK\n";
				TypeSize = 2;//this may be wrong
			}
			break;
		case Type::TypeID::FloatTyID:
			TypeSize = priT->getPrimitiveSizeInBits()>>3;//this may be wrong
			if(TypeSize == 0)
			{
				errs()<<"getPrimitiveSizeInBits is not OK\n";
				TypeSize = 4;//this may be wrong
			}
			break;
		case Type::TypeID::DoubleTyID:
			TypeSize = priT->getPrimitiveSizeInBits()>>3;//this may be wrong
			if(TypeSize == 0)
			{
				errs()<<"getPrimitiveSizeInBits is not OK\n";
				TypeSize = 8;//this may be wrong
			}
			break;
		case Type::TypeID::IntegerTyID:
			TypeSize = priT->getPrimitiveSizeInBits()>>3;//this may be wrong
			if(TypeSize == 0)
			{
				errs()<<"getPrimitiveSizeInBits is not OK\n";
				TypeSize = priT->getIntegerBitWidth() >> 3;
			}
			break;
		case Type::TypeID::PointerTyID:
			if( PointerType* PT = dyn_cast<PointerType>(priT) )
			{
				unsigned AddressSpace = PT->getAddressSpace();
				if( AddressSpace == 0 )
				{
					TypeSize = 8;//64bit, if 32bit environment, it is 4
				}else
				{
					errs()<<"**** AddressSpace = "<<AddressSpace<<" ****\n";
					TypeSize = AddressSpace;
				}
			}
			break;
		case Type::TypeID::StructTyID:
			if( StructType* ST = dyn_cast<StructType>(priT)  )
			{
				for(unsigned i = 0; i < ST->getNumElements(); i++)
				{
					Type* ElemTy = ST->getElementType(i);
					// if( dyn_cast<StructType>(ElemTy) )
					// {
					// 	errs()<<"###### Struct Type Alignment is added to TypeSize ######\n";
					// 	TypeSize += calcAlignOfType( ElemTy );
					// }
					TypeSize += calcSizeOfType( ElemTy );
				}
			}
			break;
		case Type::TypeID::ArrayTyID:
		case Type::TypeID::VectorTyID:
			if( SequentialType* ST = dyn_cast<SequentialType>(priT)  )
			{
				TypeSize = calcSizeOfType( ST->getElementType() );
				TypeSize = TypeSize * ST->getNumElements();
			}
			break;
		case Type::TypeID::X86_FP80TyID:
		case Type::TypeID::FP128TyID:
		case Type::TypeID::PPC_FP128TyID:
			//TypeSize = priT->getPrimitiveSizeInBits()>>3;//this may be wrong
			if(TypeSize == 0)
			{
				errs()<<"getPrimitiveSizeInBits is not OK\n";
				TypeSize = 16;//FP is 128bit, 128 >> 3 = 16
			}
			break;
		default:
			if(true)
			{
				errs()<<"size of non base type\n";
				//TypeSize = calcAlignOfType(priT);
				for(unsigned i=0;i<priT->getNumContainedTypes();i++)
				{
					TypeSize += calcSizeOfType( priT->getContainedType(i) );
				}
				break;
			}

	}

	//(debug)
	//errs()<<"TypeSize: "<<TypeSize<<"\t type: ";
	//priT->print(errs()); errs()<<"\n";

	return TypeSize;
}

unsigned int ParamGet::ParamGetCommon::calcAlignOfType(Type* priT)
{
	unsigned int TypeAlign = 0;
	switch(priT->getTypeID())
	{
		case Type::TypeID::HalfTyID:
			TypeAlign = 4;//this may be wrong
			break;
		case Type::TypeID::FloatTyID:
			TypeAlign = 4;
			break;
		case Type::TypeID::DoubleTyID:
			TypeAlign = 8;
			break;
		case Type::TypeID::IntegerTyID:
			TypeAlign = priT->getIntegerBitWidth() >> 3;
			break;
		case Type::TypeID::PointerTyID:
			TypeAlign = 8;//64bit, if 32bit environment, it is 4
			break;
		case Type::TypeID::StructTyID:
			if(true)
			{
				//calculate the biggest of element in this type
				for(unsigned i=0;i<priT->getNumContainedTypes();i++)
				{
					unsigned int tempAlign = calcAlignOfType( priT->getContainedType(i) );
					if( TypeAlign < tempAlign )
					{
						TypeAlign = tempAlign;
					}
				}
			}			
			// if(StructType* ST = dyn_cast<StructType>(priT))
			// {
			// 	//calculate the biggest of element in Structure
			// 	for(unsigned i=0; i<ST->getNumElements(); i++)
			// 	{
			// 		Type* ElemTy = ST->getElementType(i);
			// 		unsigned int tempAlign = calcAlignOfType(ElemTy);
			// 		if( TypeAlign < tempAlign )
			// 		{
			// 			TypeAlign = tempAlign;
			// 		}
			// 	}
			// }
			break;
		case Type::TypeID::ArrayTyID:
			TypeAlign = 16;
			break;
		case Type::TypeID::VectorTyID:
			TypeAlign = 16;
			break;			
		case Type::TypeID::X86_FP80TyID:
		case Type::TypeID::FP128TyID:
		case Type::TypeID::PPC_FP128TyID:
			TypeAlign = 16;//FP is 128bit, 128 >> 3 = 16
			break;
		default:	
			errs()<<"align of non base type\n";
			if(true)
			{
				//calculate the biggest of element in this type
				for(unsigned i=0;i<priT->getNumContainedTypes();i++)
				{
					unsigned int tempAlign = calcAlignOfType( priT->getContainedType(i) );
					if( TypeAlign < tempAlign )
					{
						TypeAlign = tempAlign;
					}
				}
			}	
			break;
	}
	return TypeAlign;
}

void ParamGet::ParamGetCommon::buildScheduleFinish(Module &M, IRBuilder<> &Builder,
	Function* OutFunc, Loop* targetLoop)
{

	// SmallVector<BasicBlock*, 8> ExitingBlocks;
	// //get loop-exit block
	// targetLoop->getExitingBlocks(ExitingBlocks);

	switch(myScheduleType)
	{
		case ParamGetCommon::sched_type::kmp_sch_static:
			if(true)
			{
				errs()<<"store Reduction Result start\n";
				//this is for phi reduction variable
				storeReductionResult(Builder, targetLoop);
				build_kmpc_for_static_fini_call(M,Builder,OutFunc);				
			}
			break;
		case ParamGetCommon::sched_type::kmp_sch_static_chunked:
			if(myScheduleType == ParamGetCommon::sched_type::kmp_sch_static_chunked)
			{
				// 1.load LB and UB
				Value* LBLoad = dyn_cast<Value>(Builder.CreateAlignedLoad(
					OMPParamMap.find("plower")->second,
					dyn_cast<AllocaInst>( OMPParamMap.find("plower")->second )->getAlignment()
				));
				Value* UBLoad = OMPParamMap.find("pupper_Load_in_Loop")->second;

				// 2.add BB that is for dispatch inc
				BasicBlock* dispatch_inc_BB = DispatchBBMap.find("dispatch_inc")->second;
				DispatchBBMap.find("dispatch_inc")->second->insertInto(
					Builder.GetInsertBlock()->getParent()
				);
				Builder.CreateBr(dispatch_inc_BB);//br from entryBB to static_init_BB
				Builder.SetInsertPoint(dispatch_inc_BB);

				// 3.create LBPHI
				// 	[ phi_for_LB_stride, DipatchBB("preheader") ],
				// 	[ 1.LB, 1.BB ]
				PHINode* LBPHI = Builder.CreatePHI(
					LBLoad->getType(),
					2
				);
				LBPHI->addIncoming( 
					OMPParamMap.find("phi_for_LB_stride")->second,
					DispatchBBMap.find("preheader")->second
				);
				LBPHI->addIncoming( 
					LBLoad,
					DispatchBBMap.find("dispatch")->second
				);

				// 4.create UBPHI 
				// 	[ phi_for_UB_stride, DispatchBB("preheader")],
				// 	[ 1.UB, 1.BB ]
				PHINode* UBPHI = Builder.CreatePHI(
					UBLoad->getType(),
					2
				);
				UBPHI->addIncoming( 
					OMPParamMap.find("phi_for_UB_stride")->second,
					DispatchBBMap.find("preheader")->second
				);
				UBPHI->addIncoming( 
					UBLoad,
					DispatchBBMap.find("dispatch")->second
				);

				// 5.Load stride
				Value* StrideLoad = dyn_cast<Value>(Builder.CreateAlignedLoad(
					OMPParamMap.find("pstride")->second,
					dyn_cast<AllocaInst>( OMPParamMap.find("pstride")->second )->getAlignment()
				));

				// 6.Add nsw 5., 3.
				Value* StridedLB = Builder.CreateNSWAdd(
					StrideLoad,
					dyn_cast<Value>(LBPHI)
				);
				// 6.2. Store 6. to LB
				Builder.CreateAlignedStore(
					StridedLB,
					OMPParamMap.find("plower")->second,
					dyn_cast<AllocaInst>( OMPParamMap.find("plower")->second )->getAlignment()
				);

				// 7.0. Add nsw 5., 4.
				Value* StridedUB = Builder.CreateNSWAdd(
					StrideLoad,
					dyn_cast<Value>(UBPHI)
				);
				// 7.ICmp sgt 7.0., UB_calc
				Value* CondValue = Builder.CreateICmp(
					CmpInst::Predicate::ICMP_SGT,
					StridedUB,
					OMPParamMap.find("UB_calc")->second
				);

				// 8.select 7. T:UB_calc, F:7.0.
				Value* SelectVal = Builder.CreateSelect(
					/*Value* C*/CondValue,
					/*Value* True*/OMPParamMap.find("UB_calc")->second,
					/*Value* False*/StridedUB
				);

				// 9.Store 8. to UB
				Builder.CreateAlignedStore(
					SelectVal,
					OMPParamMap.find("pupper")->second,
					dyn_cast<AllocaInst>( OMPParamMap.find("pupper")->second )->getAlignment()
				);





				//this is for phi reduction variable
				storeReductionResult(Builder,targetLoop);



				// 10.Icmp sgt 6., 8.
				CondValue = Builder.CreateICmp(
					CmpInst::Predicate::ICMP_SGT,
					StridedLB,
					SelectVal
				);

				// 11.condbr 10, T:DispatchBB("end") F:DispatchBB("preheader")
				Builder.CreateCondBr(
					CondValue,
					DispatchBBMap.find("end")->second,
					DispatchBBMap.find("preheader")->second
				);

				// 12.phi_for_LB_stride->addIncoming( 6., Builder.GetInsertBB )
				if( PHINode* PHIN = 
						dyn_cast<PHINode>(OMPParamMap.find("phi_for_LB_stride")->second) )
				{
					PHIN->addIncoming( StridedLB, dispatch_inc_BB );
				}

				// 13.phi_for_UB_stride->addIncoming( 8., Builder.GetInsertBB )
				if( PHINode* PHIN = 
						dyn_cast<PHINode>(OMPParamMap.find("phi_for_UB_stride")->second) )
				{
					PHIN->addIncoming( SelectVal, dispatch_inc_BB );
				}
				
				//11.2 Set InsertPoint to end BB
				DispatchBBMap.find("end")->second->insertInto(
					Builder.GetInsertBlock()->getParent()
				);
				Builder.SetInsertPoint( DispatchBBMap.find("end")->second );

				build_kmpc_for_static_fini_call(M,Builder,OutFunc);	
			}
			break;
		case ParamGetCommon::sched_type::kmp_sch_dynamic_chunked:
		case ParamGetCommon::sched_type::kmp_sch_guided_chunked:
		case ParamGetCommon::sched_type::kmp_sch_auto:
		case ParamGetCommon::sched_type::kmp_sch_runtime://in dispatch
			if(true)//this nest is for use Variable in case statement
			{
				Value* NextExist;


				//this is for phi reduction variable
				storeReductionResult(Builder,targetLoop);


				if( CondOperandIntegerSize < 64 )
				{
					NextExist = create_kmpc_dispatch_next_4_call(M,Builder);
				}else
				{
					NextExist = create_kmpc_dispatch_next_8_call(M,Builder);
				}
				Value* CondValue = Builder.CreateICmp(
					CmpInst::Predicate::ICMP_EQ,// ==
					NextExist,
					Builder.getInt32(0)//dispatch_next's return value is i32 type
				);
				Builder.CreateCondBr(
					CondValue,
					/*BasicBlock* True*/DispatchBBMap.find("end")->second,
					/*BasicBlock* False*/DispatchBBMap.find("preheader")->second
				);				
				DispatchBBMap.find("end")->second->insertInto(
					Builder.GetInsertBlock()->getParent()
				);
				Builder.SetInsertPoint( DispatchBBMap.find("end")->second );


			}
			break;
		default:
			errs()<<"this schedule type is what we can't handle for now\n";
	}
}







void ParamGet::ParamGetCommon::storeReductionResult(IRBuilder<> &Builder, Loop* targetLoop)
{
	if(!ReductionSrcMap.empty())
	{
		BasicBlock* LoopExitBB = DispatchBBMap.find("exit")->second;
		BasicBlock* PreheaderBB = DispatchBBMap.find("preheader")->second;
		BasicBlock* CurBB = Builder.GetInsertBlock();
		Value* StoreVal;
		Value* AllocaVal;
		Value* InitialV;
		unsigned align;

		for(auto redpair : ReductionValMap){
			//if ReductionRecordMap has same name element, skip creation of store
			//because there is already exist
			//ReductionRecordMap is map of Store/Load reduction variable
			string redName = redpair.first;
			if(ReductionRecordMap.find(redName) != ReductionRecordMap.end()){
				continue;
			}
			//(debug)
			errs()<<"reditr start\n";

			StoreVal = BuiltValMap.find(redName)->second;
			AllocaVal = BuiltValMap.find(redName + "_Local")->second;
			align = dyn_cast<AllocaInst>(AllocaVal)->getAlignment();
			InitialV = getInitialValueForReductionOperand(Builder,targetLoop,StoreVal,ReductionValMap.find(redName)->second.second);
			// //(debug)
			// errs()<<"StoreVal: ";
			// StoreVal->print(errs());
			// errs()<<"\n";
			// //(debug)
			// errs()<<"StoreVal Type: ";
			// StoreVal->getType()->print(errs());
			// errs()<<"\n";
			// //(debug)
			// errs()<<"CurBB: ";
			// CurBB->print(errs());
			// errs()<<"\n";

			//if reduction target is phi, we use the updated value (incoming value from loop)
			//for the case that phi is updated after loop exit cond
			Value* OriginalRedV = redpair.second.first;
			if(PHINode* origPHIN = dyn_cast<PHINode>(OriginalRedV)){
				PHINode* localPHIN = dyn_cast<PHINode>(StoreVal);
				unsigned incomings = origPHIN->getNumIncomingValues();
				for(unsigned i = 0; i<incomings; i++){
					if(targetLoop->contains(origPHIN->getIncomingBlock(i))){
						StoreVal = localPHIN->getIncomingValue(i);
						//NOTE: localPHIN and origPHIN should have same incomings in same order
						errs()<<"The storing reduction value of phi is chaged to: ";
						StoreVal->print(errs());
						errs()<<"\n";
						break;
					}

				}
			}

			//Create Store to RedVar_Local	
			//Create phi inst
			if( CurBB->empty() ){
				Builder.SetInsertPoint( CurBB );
			}else{
				Builder.SetInsertPoint( CurBB->getFirstNonPHI() );				
			}

			PHINode* PHIN = Builder.CreatePHI(StoreVal->getType(),2);
			PHIN->addIncoming( StoreVal, LoopExitBB );
			PHIN->addIncoming( InitialV, PreheaderBB );

			//(debug)
			errs()<<"PHI: ";
			PHIN->print(errs());
			errs()<<"\n";

			if( (StoreVal = dyn_cast<Value>(PHIN)) )
			{
				//(debug)
				errs()<<"Reduction StoreVal: ";
				StoreVal->print(errs());
				errs()<<"\n";

				Builder.SetInsertPoint( CurBB );
				//create store
				Builder.CreateAlignedStore( 
					StoreVal,
					AllocaVal,
					align
				);
			}
		}
	}//end of ReductionValMap.empty
}






//#############################################################################################################################################################################################################################################################################################################

bool ParamGet::ParamGetCommon::isLoopCounter(Value* checkV){
	Value* loopCounterV = LoopExpr.find("init")->second;
	string loopCounterName = loopCounterV->getName().str();
	if(LoadInst* LoadI = dyn_cast<LoadInst>(loopCounterV)){
		loopCounterName = LoadI->getOperand(0)->getName().str();
	}
	if(checkV->hasName() && checkV->getName().str() == loopCounterName){
		errs()<<"isLoopCounter: yes phi: ";
		checkV->print(errs());
		errs()<<"\n";
		return true;
	}else if(LoadInst* checkLI = dyn_cast<LoadInst>(checkV)){
		Value* checkMemory = checkLI->getOperand(0);
		if(checkMemory->hasName() && checkMemory->getName().str() == loopCounterName){
			errs()<<"isLoopCounter: yes load: ";
			checkV->print(errs());
			errs()<<"\n";
			return true;
		}
	}
	//check whether the checkV is the loop counter
	// if(Instruction* checkI = dyn_cast<Instruction>(checkV))
	// {
	// 	for(Value* opV : checkI->operands()){
	// 	}
	// }		
	errs()<<"isLoopCounter: NO: ";
	checkV->print(errs());
	errs()<<"\n";
	return false;
}







