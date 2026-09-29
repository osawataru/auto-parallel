#include "llvm/Transforms/DirectiveInsertion.h"
#include "llvm/Transforms/DirectiveMarker.h"

extern "C" llvm::ModulePass *createAutoParallelDirectiveInsertionPass()
{
  return new DirectiveInsertion();
}

#include "llvm/Support/CommandLine.h"
static cl::list<std::string> TargetFuncs("ditarget", cl::desc("Specify function to parralelize"), cl::CommaSeparated, cl::OneOrMore);

// Opaque pointers do not carry their pointee type.  Recover an element type
// from an instruction that actually accesses the argument instead.
static Type *getAccessedType(Value *Pointer) {
  for (User *U : Pointer->users()) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      if (GEP->getPointerOperand() == Pointer)
        return GEP->getSourceElementType();
    }
    if (auto *Load = dyn_cast<LoadInst>(U)) {
      if (Load->getPointerOperand() == Pointer)
        return Load->getType();
    }
    if (auto *Store = dyn_cast<StoreInst>(U)) {
      if (Store->getPointerOperand() == Pointer)
        return Store->getValueOperand()->getType();
    }
  }
  return nullptr;
}

bool DirectiveInsertion::runOnModule(Module &M)
{    
	LLVMContext &TheContext = M.getContext();
	IRBuilder<> Builder(TheContext);
  errs()<<"[DirectiveInsertion] start\n";
  errs()<<"[DirectiveInsertion] Target Functions\n";
  for(auto &t: TargetFuncs)
    errs()<<t + "\n";
  errs()<<"\n";

  for(auto fitr = M.begin(); fitr != M.end(); ++fitr)
  {
    Function &F = *(fitr);
    // errs()<<"[DirectiveInsertion] [Function: "<<F.getName().str()<<"]\n";
    string funcName = F.getName().str();

    bool isFEmptyDeclaration = (F.empty() && F.isDeclaration());
    //bool isFuncTarget = std::find(std::begin(TargetFuncs), std::end(TargetFuncs), funcName) != std::end(TargetFuncs);
    bool isFuncTarget = true;
    for(std::string tf : TargetFuncs){
      isFuncTarget = funcName.find(tf)!=std::string::npos;
      if(!isFuncTarget)
        break;
    }
    if(isFEmptyDeclaration || !isFuncTarget){
      continue;
    }

    LI = &getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    if(LI->empty()){
      continue;
    }
    errs()<<"[DirectiveInsertion] [Function: "<<F.getName().str()<<"]\n";
    
    //TODO
    // 2024 added =========================================
    // add noalias attribute
    for(auto arg = F.arg_begin(); arg != F.arg_end(); arg++){
      // ポインタ
      if(arg->getType()->isPointerTy()){
        unsigned argNo = arg->getArgNo();
        //debug
        errs()<<"[NA] checking arg: "<<*arg<<"\n";

        Type *elementTy = getAccessedType(arg);
        if(elementTy == nullptr){
          errs()<<"[NA] Skip: pointee type is unavailable for opaque pointer\n";
          continue;
        }

        // void
        if(elementTy->isIntegerTy(8)){
          vector<Value*> intEightVec;
          getValueUse(arg, F, &intEightVec);
          //debug
          errs()<<"[NA] ---------intEightVec---------\n";
          for(Value* v : intEightVec){
            v->print(errs());
            errs()<<"\n";
          }
          errs()<<"[NA] -----------------------\n";

          // voidかi8か判定
          bool ieFlag = false;
          for(auto ie: intEightVec){
            if(dyn_cast<BitCastInst>(ie)){
              errs()<<"[NA] This attr may be void\n";
              ieFlag = true;
              continue;
            }
          }
          if(!ieFlag && !hasSameAddr(M, F, argNo)){
            errs()<<"[NA] NoAlias Attr add to "<<*arg<<"\n";
            arg->addAttr(Attribute::NoAlias);
          }
        }
        // 配列
        else if(ArrayType* array = dyn_cast<ArrayType>(elementTy)){
          Type* elemTy = getArrayElemTy(array);
          if(!dyn_cast<PointerType>(elemTy) && !hasSameAddr(M, F, argNo)){
            errs()<<"[NA] NoAlias Attr add to "<<*arg<<"\n";
            arg->addAttr(Attribute::NoAlias);
          }
        }
        // 構造体
        else if(StructType* st = dyn_cast<StructType>(elementTy)){
          if(!hasStructElemPointerTy(st) && !hasSameAddr(M, F, argNo)){
            errs()<<"[NA] NoAlias Attr add to "<<*arg<<"\n";
            arg->addAttr(Attribute::NoAlias);
          }
        }
        // 上記以外（スカラーのポインタ）
        else{
          if(!hasSameAddr(M, F, argNo)){
            errs()<<"[NA] NoAlias Attr add to "<<*arg<<"\n";
            arg->addAttr(Attribute::NoAlias);
          }
        }
      }
    }
    // =========================================

    /*
		//call private detect pass
    errs()<<"[DirectiveInsertion] call AllPrivateDetect\n";
    AllPrivateDetect *APD = &getAnalysis<AllPrivateDetect>(F);
    errs()<<"[DirectiveInsertion] check AllPrivateDetect\n";
    APD->startCheck();
    errs()<<"[DirectiveInsertion] end AllPrivateDetect\n";
    errs()<<"get AllPrivateDetect\n";
    if(APD != nullptr){
      AllPrivateMap = APD->getAllPrivate();
      PrivateMap = APD->getPrivate();
      LastPrivateMap = APD->getLastPrivate();
      FirstPrivateMap = APD->getFirstPrivate();
    }else{
      errs()<<"[DirectiveInsertion:EE] we can't get AllPrivateDetect class object\n";
      continue;
    }
		//debug
		errs()<<"[DirectiveInsertion]--- privatizable variables ---\n";
		for(auto mappair : AllPrivateMap){
			errs()<<"all: ";
			mappair.second->print(errs());
			errs()<<"\n";
		}
    //----------------
		for(auto mappair : PrivateMap){
			errs()<<"private: ";
			mappair.second->print(errs());
			errs()<<"\n";
		}
		for(auto mappair : LastPrivateMap){
			errs()<<"lastprivate: ";
		 	mappair.second->print(errs());
		 	errs()<<"\n";
		}
		for(auto mappair : FirstPrivateMap){
		 	errs()<<"firstprivate: ";
		 	mappair.second->print(errs());
		 	errs()<<"\n";
		}
		errs()<<"-------------------------------\n";
    */

		//call reduction detect pass
    ReductionDetect *RD = &getAnalysis<ReductionDetect>(F);
    RD->startCheck();
    errs()<<"get ReductionDetect\n";
    if(RD != nullptr){
      ReductionableMap = RD->getReductionable();
      UseAllCounterMap = RD->getUseAllCounterMap();  // 2024 added
      UsePartialCounterMap = RD->getUsePartialCounterMap();  // 2024 added
    }else{
      errs()<<"[DirectiveInsertion:EE] we can't get ReductionDetect class object\n";
      continue;
    }
		//debug
		errs()<<"[DirectiveInsertion]--- reductionable variables ---\n";
		for(auto mappair : ReductionableMap){
			errs()<<"reductionable elem: ";
			mappair.second->print(errs());
			errs()<<"\n";
		}
		errs()<<"[DirectiveInsertion]--- UseAllCounterMap ---\n";
		for(auto mappair : UseAllCounterMap){
      errs()<<mappair.first<<",  ";
      mappair.second->print(errs());
			errs()<<"\n";
		}
		errs()<<"[DirectiveInsertion]--- UsePartialCounterMap ---\n";
		for(auto mappair : UsePartialCounterMap){
      errs()<<mappair.first<<",  ";
      mappair.second.first->print(errs());
      errs()<<",  ";
      mappair.second.second->print(errs());
      errs()<<"\n";
		}
    



    //call DependencyCheck pass
    DependencyCheck *DC = &getAnalysis<DependencyCheck>(F);
    //check whether this loop is independent
    LI = &getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    // LoopInfo provide only for most outer loops
    for(auto litr = LI->begin(); litr != LI->end(); ++litr)//LoopInfo does not provide vector<Loop*>
    {
      Loop* L = *litr;
      checkRecursively(M,Builder,DC,L);
    }//end of liter
  } 
  return false;
}

// TODO
// 2024 added =========================================
Type* DirectiveInsertion::getArrayElemTy(Type* type){
  if(ArrayType* arrayType = dyn_cast<ArrayType>(type))
    return getArrayElemTy(arrayType->getElementType());
  else
    return type;
}

bool DirectiveInsertion::hasStructElemPointerTy(StructType* type){
  for(Type* elemTy : type->elements()){
    if(dyn_cast<PointerType>(elemTy))
      return true;
  }
  return false;
}

// コード全体で、argNo番目の引数と他の引数に同じ変数が渡されているか
bool DirectiveInsertion::hasSameAddr(Module &M, Function &checkF, unsigned argNo){
  for(Function &F : M)
    for(BasicBlock &BB : F)
      for(Instruction &I : BB)
        if(CallInst* CI = dyn_cast<CallInst>(&I))
          if(CI->getCalledFunction() == &checkF){
            //debug
            //errs()<<"[NA] "<<__func__<<": ";
            //CI->print(errs());
            //errs()<<"\n";

            Value* checkingOp = CI->getArgOperand(argNo);
            for(int aitr=0; aitr<(int)CI->arg_size(); aitr++){
              if((int)argNo == aitr)
                continue;

              Value* op = CI->getArgOperand(aitr);
              if(checkingOp == op){
                errs()<<"[NA] same addr: "<<checkingOp<<", "<<op<<"\n";
                return true;
              }
            }
          }
  return false;
}

void DirectiveInsertion::getValueUse(Value* V, Function &F, vector<Value*>* vec){
  // debug
  //errs()<<"[NA] "<<__func__<<": ";
  //V->print(errs());
  //errs()<<"\n";

  for(BasicBlock &BB : F)
    for(Instruction &I : BB){
      if(I.getOpcode()==Instruction::Store && I.getOperand(0) == V){
        vec->push_back(&I);
        getValueUse(I.getOperand(1), F, vec);
      }
      else
        for(Value* opeV : I.operands()){
          if(opeV == V){
            // debug
            //errs()<<"[@] NAVec insert: ";
            //I.print(errs());
            //errs()<<"\n";

            if(find(vec->begin(), vec->end(), &I) == vec->end()){
              vec->push_back(&I);
              getValueUse(&I, F, vec);
            }
          }
        }
    }
}

// =========================================

//if we don't check a loop's subloop, we can't detect 3 or more nested loop's dependency
void DirectiveInsertion::checkRecursively(Module &M, IRBuilder<> &Builder, DependencyCheck *DC,  Loop* L)
{
  CanDepMap = DC->getDependencyCandidate(L);
  insertDirective(M,Builder,DC,L);
  for(Loop* subL : *L){
    checkRecursively(M,Builder,DC,subL);
  }
}


//########################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################

void DirectiveInsertion::insertDirective(Module &M, IRBuilder<> &Builder, DependencyCheck *DC, Loop* L)
{
  /* LoopCounterMap
  first string: this inst's name
  second map: LoopExpr: first + "_init", "_incr", and "_cond" can be accessed
  */
  //debug
  for( auto pairptr : CanDepMap ){
    pairptr.second->print(errs());
    errs()<<"\n";
  }
  //---- end of dependency check ----

  //check whether the dependency is resolve-able
  bool loopIsIndependent = true;
  for( auto mappair : CanDepMap ){
    string depName = mappair.first;
    Instruction* depI = dyn_cast<Instruction>(mappair.second);
    //if the dependency is recorded in AllPrivateMap, it can be resolved
    if( AllPrivateMap.find(depName) != AllPrivateMap.end() ){
      errs()<<"[DirectiveInsertion] privatizable: ";
    }
    //if the dependency is recorded in ReductionableMap, it can be resolved
    else if(ReductionableMap.find(depName) != ReductionableMap.end()){
      errs()<<"[DirectiveInsertion] reductionable: ";
    }
    // TODO
    // 2024 added =========================
    else if(UseAllCounterMap.find(depName) != UseAllCounterMap.end()){
      errs()<<"[DirectiveInsertion] use all counter: ";
    }
    else if(UsePartialCounterMap.find(depName) != UsePartialCounterMap.end()){
      auto itr = UsePartialCounterMap.find(depName);
      MapMap LCMap = DC->getLoopCounterMap(L);
      //debug
      //errs()<<"[DI] --------LCMap---------";
      //for(auto mappair : LCMap){
      //  errs()<<mappair.first;
      //  errs()<<"\n";
      //  for(auto elempair : mappair.second){
      //    errs()<<"elem: "<<elempair.first;
      //    elempair.second->print(errs());
      //    errs()<<"\n";
      //  }
      //  errs()<<"\n";
      //}
      //errs()<<"[DI] ------------------\n";

      Value* LCInit = nullptr;
      for(auto mappair : LCMap)
        for(auto elempair : mappair.second)
          LCInit = elempair.second;

      if(itr->second.second == LCInit){
        errs()<<"[DirectiveInsertion] use part counter, this loop is outermost in used counter: ";
      }
      else{
        errs()<<"[DirectiveInsertion] use part counter, this loop is \"not\" outermost in used counter (this loop is not parallelized): ";
        loopIsIndependent = false;
      }
    }
    // =========================
    //if the dependency exists out of this loop, it does not affect to the loop.
    else if( depI != nullptr && LI->getLoopDepth(depI->getParent()) < L->getLoopDepth()){
      errs()<<"[DirectiveInsertion] Outer dependency (it does not affect to this loop): ";
    }
    else{
      errs()<<"[DirectiveInsertion] Unsolved dependency: ";
      loopIsIndependent = false;
    }
    errs()<< depName << ": ";
    mappair.second->print(errs());
    errs()<<"\n";
  }
  //create directive for independent loop
  if( loopIsIndependent )
  {
    BasicBlock* BB = L->getHeader();// *(L->block_begin());
    insert_llvm_directive_decl(M,Builder);
    Builder.SetInsertPoint( BB->getFirstNonPHI() );
    errs()<<"[DirectiveInsertion] ######### Directive inserted #########\n";
    create_llvm_directive_call(M,Builder,L);
    errs()<<"##################\n";
  }
}

//########################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################


void DirectiveInsertion::insert_llvm_directive_decl(Module &M, IRBuilder<> &Builder)
{
	(void)Builder;
	auto_parallel::getOrInsertDirectiveMarker(M);
}
void DirectiveInsertion::create_llvm_directive_call(Module &M, IRBuilder<> &Builder,  Loop *L)
{
	LLVMContext &TheContext = M.getContext();
	vector<Metadata*> MDVec;
  {
    string pragmaStr = "#pragma omp parallel for";
    MDString* MDStr = MDString::get(TheContext, pragmaStr.c_str());
    if( Metadata* MD = dyn_cast<Metadata>(MDStr) ){
      MDVec.push_back( MD );
    }else{
      errs()<<"[llvm-pass:EE] Sorry MDStr was not casted to MD\n";
    }
  }
  string clauseStr;
  //create private clause
  clauseStr = getPrivateClause("private",PrivateMap,L);
  if( !clauseStr.empty() ){
    MDString* MDStr = MDString::get(TheContext, clauseStr.c_str());
    if( Metadata* MD = dyn_cast<Metadata>(MDStr) ){
      MDVec.push_back( MD );
    }else{
      errs()<<"[llvm-pass:EE] Sorry MDStr was not casted to MD\n";
    }
  }
  //create lastprivate clause
  clauseStr = getPrivateClause("lastprivate",LastPrivateMap,L);
  if( !clauseStr.empty() ){
    MDString* MDStr = MDString::get(TheContext, clauseStr.c_str());
    if( Metadata* MD = dyn_cast<Metadata>(MDStr) ){
      MDVec.push_back( MD );
    }else{
      errs()<<"[llvm-pass:EE] Sorry MDStr was not casted to MD\n";
    }
  }
  //create firstprivate clause
  clauseStr = getPrivateClause("firstprivate",FirstPrivateMap,L);
  if( !clauseStr.empty() ){
    MDString* MDStr = MDString::get(TheContext, clauseStr.c_str());
    if( Metadata* MD = dyn_cast<Metadata>(MDStr) ){
      MDVec.push_back( MD );
    }else{
      errs()<<"[llvm-pass:EE] Sorry MDStr was not casted to MD\n";
    }
  }
  //create reduction clause
  clauseStr = getReductionClause(L);
  if( !clauseStr.empty() ){
    MDString* MDStr = MDString::get(TheContext, clauseStr.c_str());
    if( Metadata* MD = dyn_cast<Metadata>(MDStr) ){
      MDVec.push_back( MD );
    }else{
      errs()<<"[llvm-pass:EE] Sorry MDStr was not casted to MD\n";
    }
  }

  //debug
  for(auto argMeta : MDVec){
    errs()<<"directive arg: ";
    argMeta->print(errs());
    errs()<<"\n";
  }

	// LLVM 22 stores clauses as an attachment on an argument-free marker call.
	// The representation is intentionally isolated in DirectiveMarker.h.
	auto_parallel::createDirectiveMarker(Builder, M, MDVec);
}


//########################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################

string DirectiveInsertion::getPrivateClause(string clauseStr, ValMap checkingMap,  Loop *L)
{
  vector<string> PrivateNames;
  for(auto mappair : checkingMap){
    Value* priV = mappair.second;
    //debug
    // errs()<<"pri arg: ";
    // priV->print(errs());
    // errs()<<"\n";

    if( Instruction* priI = dyn_cast<Instruction>(priV) ){
      if( L->contains(priI) && 
          LI->getLoopDepth(priI->getParent()) == L->getLoopDepth() )
      {
        PrivateNames.push_back(mappair.first);
      }
    }
  }
  string resultStr;
  if( !PrivateNames.empty() ){
    resultStr = clauseStr + "(";
    for(auto priStr : PrivateNames){
      resultStr += priStr + ",";
    }
    resultStr.pop_back();
    resultStr += ")";
  }
  return resultStr;
}


string DirectiveInsertion::getReductionClause(Loop *L)
{
  vector<string> ReductionNames;
  for(auto mappair : ReductionableMap){
    Value* redV = mappair.second;
    if( Instruction* redI = dyn_cast<Instruction>(redV) ){
      if( L->contains(redI) && 
          LI->getLoopDepth(redI->getParent()) == L->getLoopDepth() 
      ){
        ReductionNames.push_back(mappair.first);
      }
    }
  }
  string resultStr;
  if( !ReductionNames.empty() ){
    resultStr = "reduction(+:";
    for(auto priStr : ReductionNames){
      resultStr += priStr + ",";
    }
    resultStr.pop_back();
    resultStr += ")";
  }
  return resultStr;
}


//########################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################
