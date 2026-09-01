


#include "llvm/Analysis/DependencyCheck.h"

namespace llvm {
  FunctionPass *createDependencyCheckPass(){return new DependencyCheck();}
}
char DependencyCheck::ID = 0;
INITIALIZE_PASS_BEGIN(DependencyCheck, "dependencycheck", "Provide dependency check result to our parallelization analysis passes and our directive insertion pass.", true, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DependenceAnalysisWrapperPass)
INITIALIZE_PASS_END(DependencyCheck, "dependencycheck", "Provide dependency check result to our parallelization analysis passes and our directive insertion pass.", true, false)


bool DependencyCheck::runOnFunction(Function &F)
{
  if( F.empty() ){
    return false;
  }
  targetF = &F;
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  if( LI->empty() ){ 
    return false; 
  }
  // SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  DI = &getAnalysis<DependenceAnalysisWrapperPass>().getDI();
  
  return false;
}

//-######################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################

DependencyCheck::ValMap &DependencyCheck::getDependencyCandidate(Loop* L)
{
  // TODO
  // 2024 added ===================================
  getNAVec();

  //debug
  errs()<<"[NA] ---------NAVec---------\n";
  for(Value* v : NAVec){
    v->print(errs());
    errs()<<"\n";
  }
  errs()<<"[NA] -----------------------\n";
  // =========================================

  CanDepMap.clear();
  LoopCounterMap = getLoopCounterMap(L);
  //record the dependency which can be resolved by clauses
  string loopCounterName;
  for(auto mappair : LoopCounterMap){
    Value* incrV = mappair.second.find(mappair.first+"_incr")->second;
    if(Instruction* I = dyn_cast<Instruction>(incrV)){
      if(LI->getLoopDepth(I->getParent()) == L->getLoopDepth()){
        loopCounterName = mappair.first;
        break;
      }
    }
  }
  // errs()<<"Checking loopCounterName: "<<loopCounterName<<"\n";
  collectDependencyCandidate(L, loopCounterName);
  //also check sub loops
  // for(auto subL : *L){
  //   string loopCounterName;
  //   for(auto mappair : LoopCounterMap){
  //     Value* incrV = mappair.second.find(mappair.first+"_incr")->second;
  //     if(Instruction* I = dyn_cast<Instruction>(incrV)){
  //       if(LI->getLoopDepth(I->getParent()) == subL->getLoopDepth()){
  //         loopCounterName = mappair.first;
  //         break;
  //       }
  //     }
  //   }
  //   errs()<<"Checking loopCounterName: "<<loopCounterName<<"\n";
  //   collectDependencyCandidate(subL, loopCounterName);
  // }

  //debug
  //errs()<<"[DependencyCheck]------- show CanDepMap ------\n";
  for(auto mappair : CanDepMap){
    errs()<< mappair.first << ": ";
    mappair.second->print(errs());
    errs()<<"\n";
  }
  //errs()<<"------------------\n";
  
  return CanDepMap;
}

//TODO
// 2024 added ===================================
void DependencyCheck::getNAVec(){
  // make ArgMap
  for(auto arg = targetF->arg_begin(); arg != targetF->arg_end(); arg++){
    if(!(arg->hasNoAliasAttr()))
      continue;

    for(BasicBlock &BB : *targetF)
      for(Instruction &I : BB){
        if(I.getOpcode()==Instruction::Store){
          // debug
          //errs()<<"[@] ";
          //I.print(errs());
          //errs()<<"\n";

          if(I.getOperand(0) == arg){
            //errs()<<"[@] oprand and arg are same.\n";

            if(find(NAVec.begin(), NAVec.end(), &I) == NAVec.end()){
              NAVec.push_back(&I);
              getNAVec(I.getOperand(1));
            }
          }
        }
        else if(I.getOpcode()==Instruction::GetElementPtr){
          // debug
          //errs()<<"[@] ";
          //I.print(errs());
          //errs()<<"\n";

          if(I.getOperand(0) == arg){
            //errs()<<"[@] oprand and arg are same.\n";

            if(find(NAVec.begin(), NAVec.end(), &I) == NAVec.end()){
              NAVec.push_back(&I);
              getNAVec(&I);
            }
          }
        }
      }
  }
}

void DependencyCheck::getNAVec(Value* V){
  // debug
  //errs()<<"[@] search: ";
  //V->print(errs());
  //errs()<<"\n";

  for(BasicBlock &BB : *targetF)
    for(Instruction &I : BB){
      for(Value* opeV : I.operands()){

        if(opeV == V){
          // debug
          //errs()<<"[@] NAVec insert: ";
          //I.print(errs());
          //errs()<<"\n";

          if(find(NAVec.begin(), NAVec.end(), &I) == NAVec.end()){
            NAVec.push_back(&I);
            getNAVec(&I);
          }
        }
      }
    }
}
// ===================================

//if there is an dependence between statements in loop, push it into CanDepVec vector
void DependencyCheck::collectDependencyCandidate(Loop* L,   string loopCounterName)
{
  for(BasicBlock* BB : L->blocks()){
    for(Instruction &candInst : *BB){
      Instruction* candI = &candInst;
      // errs()<<"I:\t";
      // candI->print(errs());errs()<<"\n";

      //if it is a loop counter, skip.
      if(candI->hasName() &&
          ( candI->getName().str() == loopCounterName || 
            CanDepMap.find(candI->getName().str()) != CanDepMap.end() ) 
        ){
        //debug
        // errs()<<"Loop counter or the dependency already recorded is skipped: ";
        // candI->print(errs());
        // errs()<<"\n";
        continue;
      }
      //if phi, check whether it refer to it self
      //this is for reduction detection
      else if(candI->getOpcode() == Instruction::PHI)
      {      
        vector<Instruction*> checkedCandIVec;  //2024 added
        //set name
        if(!candI->hasName()){
          candI->setName("dependency_candidate.reduction");
        }
        string phistr = candI->getName().str();

        //check dependency
        //if it is defined in sub loop, skip.
        if( LI->getLoopDepth(candI->getParent()) != L->getLoopDepth() ){
          //debug
          // errs()<<"Phi insts defined in sub loop are skipped: ";
          // candI->print(errs());
          // errs()<<"\n";
          continue;
        }
        //if it is loop counter, skip it.
        else if(LoopCounterMap.find(phistr) != LoopCounterMap.end()){
          //debug
          // errs()<<"phi: loop counter is skipped: ";
          // candI->print(errs());
          // errs()<<"\n";
          continue;
        }
        //if it is updating it self in loop, it can be reduction or dependency
        else if( isReferToItself(phistr,candI,L,&checkedCandIVec) ){
          //if the all the user of this phi is not in thi loop nest, it does not cause dependency
          bool UsedInThisLoopFlag = false;
          for(Value* userV : candI->users()){
            if(Instruction* userI = dyn_cast<Instruction>(userV)){
              if(L->contains(userI) && LI->getLoopDepth(userI->getParent()) == L->getLoopDepth()){
                UsedInThisLoopFlag = true;
                break;
              }
            }
          }
          if(UsedInThisLoopFlag){
            //debug
            errs()<<"phi: reduction candidate: ";
            candI->print(errs());
            errs()<<"\n";

            CanDepMap.insert(make_pair(phistr, dyn_cast<Value>(candI)));
          }else{
            //debug
            // errs()<<"phi: not used in this loop nest: ";
            // candI->print(errs());
            // errs()<<"\n";
            continue;
          }
        }
      }
      //check store inst's dependency
      //this is for reduction candidate and private candidate
      else if(candI->getOpcode() == Instruction::Store)
      {
        //collect dependence comparing Instruction in same loop
        for(BasicBlock* BB : L->blocks()){
          for(Instruction &I : *BB){
            // //added for thesis eval
            // if(I.getOpcode() != Instruction::Load || I.getOpcode() != Instruction::Store){
            //   continue;
            // }
            // //check whether any dependency exists
            // errs()<<"[DependencyCheck] checking: \n";
            // errs()<<"Dep of store side: ";
            // candI->print(errs());
            // errs()<<"\n";
            // errs()<<"Dep of load side: ";
            // I.print(errs());
            // errs()<<"\n";

            auto Dep = DI->depends(candI,&I,true);
            if( Dep && (Dep->isFlow() || Dep->isAnti()) )
            {
              //debug
              //errs()<<"[DependencyCheck] this inst causes dependency: \n";
              //errs()<<"Dep of store side: ";
              //candI->print(errs());
              //errs()<<"\n";
              //errs()<<"Dep of load side: ";
              //I.print(errs());
              //errs()<<"\n";

              //we set name only for memory
              Value* StoreMemory = candI->getOperand(1);
              if(!StoreMemory->hasName()){
                StoreMemory->setName("dependency_candidate");
              }
              string dependencyStoreName = StoreMemory->getName().str();
              //alse we register the name of loaded memory
              Value* LoadMemory = I.getOperand(0);
              if(!LoadMemory->hasName()){
                LoadMemory->setName("dependency_candidate");
              }
              string dependencyLoadName = LoadMemory->getName().str();

              //WARNING: for the safe detection of dependency, array subscripts may determine the pair as no dependency that actualy depends
              // //if the memory is GEP, restrictly check dependency.
              // bool counterUsedFlag = false;
              // if(checkMemoryDependency(StoreMemory,LoadMemory,L,loopCounterName,counterUsedFlag)){
              //   if(counterUsedFlag){
              //     errs()<<"[DependencyCheck] This inst does NOT cause dependency. this GEP accesses independent element in each iteration\n";
              //     candI->print(errs());
              //     errs()<<"\n";
              //     continue;
              //   }
              //   errs()<<"[DependencyCheck] this memory causes dependency.\n";
              // }

              //TODO
              // 2024 added ====================================
              //debug
              //errs()<<"[@] ";
              //Dep->dump(errs());
              //candI->print(errs());
              //errs()<<",  ";
              //I.print(errs());
              //errs()<<"\n";

              // エイリアス
              if(Dep->isConfused()){
                if(find(NAVec.begin(), NAVec.end(), dyn_cast<Value>(&I)) != NAVec.end()){
                  //debug
                  //errs()<<"[DC] find in NoAliasVec: ";
                  //I.print(errs());
                  //errs()<<"\n";
                  
                  continue;
                }
                AliasVec.push_back(candI);
              }
              // ====================================

              //we record only store inst, but also load inst's name.
              CanDepMap.insert(make_pair(
                dependencyStoreName, dyn_cast<Value>(candI)
              ));
              CanDepMap.insert(make_pair(
                dependencyLoadName, dyn_cast<Value>(candI)
              ));
            }
          }
        }
      }//end of candI->getOpcode()

    }//end one Inst Dep Check
  }
}

//-######################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################

bool DependencyCheck::checkMemoryDependency(Value* StoreMemory, Value* LoadMemory, Loop* L, string loopCounterName, bool &counterUsedFlag)
{
  if(CastInst* storeCI = dyn_cast<CastInst>(StoreMemory)){
    if(CastInst* loadCI = dyn_cast<CastInst>(LoadMemory)){
      Value* srcStoreMemory = storeCI->getOperand(0);
      Value* srcLoadMemory = loadCI->getOperand(0);
      return checkMemoryDependency(srcStoreMemory, srcLoadMemory, L, loopCounterName, counterUsedFlag);
    }
  }else if(dyn_cast<GetElementPtrInst>(StoreMemory)){
    //debug
    errs()<<"[DependencyCheck]--- start checking GEP subscripts ---\n";
    errs()<<"storeMemory: ";
    StoreMemory->print(errs());
    errs()<<"\n";
    errs()<<"LoadMemory: ";
    LoadMemory->print(errs());
    errs()<<"\n";
    errs()<<"loopCounterName: "<<loopCounterName<<"\n";
    //
    return checkGEPSubscripts(StoreMemory, LoadMemory, L, loopCounterName,counterUsedFlag);
  }
  errs()<<"[DependencyCheck] Sorry, we can't check this memory: \n";
  errs()<<"storeMemory: ";
  StoreMemory->print(errs());
  errs()<<"\n";
  errs()<<"LoadMemory: ";
  LoadMemory->print(errs());
  errs()<<"\n";

  return false;
}



bool DependencyCheck::checkGEPSubscripts(Value* storeGEPValue, Value* loadGEPValue, Loop* L, string loopCounterName, bool &counterUsedFlag)
{
  GetElementPtrInst* storeGEPI = dyn_cast<GetElementPtrInst>(storeGEPValue);
  GetElementPtrInst* loadGEPI = dyn_cast<GetElementPtrInst>(loadGEPValue);
  if(storeGEPI == nullptr || loadGEPI == nullptr){
    errs()<<"[DependencyCheck] One of the depending memory is NOT GetElementPtr\n";
    return false;
  }
  vector<pair<string,Value*>> storeIdxVector;
  collectGEPUsedValues(storeGEPI,L,storeIdxVector);
  vector<pair<string,Value*>> loadIdxVector;
  collectGEPUsedValues(loadGEPI,L,loadIdxVector);

  bool IndependencyFlag = true;
  //if the index opeNum is not same, it is not same expression.
  if(storeIdxVector.size() != loadIdxVector.size()){
    errs()<<"Used index num is not same\n";
    IndependencyFlag = false;
  }

  //if loop contains loadGEPI, we also check whether the loop counter used in index. if loop counter is not used in this GEPI, this should be reduction or loop-carried dependency. return false
  auto litr = loadIdxVector.begin();
  for(auto sitr = storeIdxVector.begin(); sitr != storeIdxVector.end(); ++sitr, ++litr){//we should iterate loadIdxVector, so we use begin - end for loop
    string storeName = (*sitr).first;
    Value* storeV = (*sitr).second;
    errs()<<"checking store: ";
    storeV->print(errs());
    errs()<<"\n";

    //for the independent subscript, GEP should include loop counter
    //NOTE: please check loop nest level
    if(storeName == loopCounterName){
      errs()<<"[DependencyCheck] GEP uses loop counter\n";
      counterUsedFlag = true;
    }
    if(IndependencyFlag){ 
      string loadName = (*litr).first;
      Value* loadV = (*litr).second;
      //check independency
      if( dyn_cast<Constant>(storeV) || dyn_cast<Constant>(loadV)){
        IndependencyFlag = isSameConstant(storeV,loadV);
        // if( !isSameConstant(storeV,loadV) ){
        //   // return false;
        // }
      }
      else if(Instruction* storeI = dyn_cast<Instruction>(storeV)){
        if( !L->contains(storeI) && storeI->getOpcode() != Instruction::PHI ){
          //check whether the opcode of storeI and loadI is same
          Instruction* loadI = dyn_cast<Instruction>(loadV);
          if(loadI == nullptr || storeI->getOpcode() != loadI->getOpcode() ){
            //debug
            errs()<<"[DependencyCheck] Used index's Opcode is not same\n";
            errs()<<"store: ";
            storeV->print(errs());
            errs()<<"\n";
            errs()<<"load: ";
            loadV->print(errs());
            errs()<<"\n";
            IndependencyFlag = false;
            // return false;
          }
        }
        //if the var used in store index map is not found in load index map, these subscripts are not same. return false
        else if(storeName != loadName){
          //debug
          errs()<<"[DependencyCheck] Used index (Instruction) is not same\n";
          errs()<<"store: ";
          storeV->print(errs());
          errs()<<"\n";
          errs()<<"load: ";
          loadV->print(errs());
          errs()<<"\n";
          IndependencyFlag = false;
          // return false;
        }
      }
      else if(storeName != loadName){
        //debug
        errs()<<"[DependencyCheck] Used index (Value) is not same\n";
        errs()<<"store: ";
        storeV->print(errs());
        errs()<<"\n";
        errs()<<"load: ";
        loadV->print(errs());
        errs()<<"\n";
        IndependencyFlag = false;
        // return false;
      }
    }

  }
  //FIXME: ループカウンタの係数が0でないことを確認しなければならない．
  if(!counterUsedFlag){
    errs()<<"[DependencyCheck] This subscript position does not include the checking loop's counter.\n";
    //if GEPI's pointer is GEP, also we check it by calling this procedure recurrently.
    //-> we check each index: A[i][j] vs A[i+1][j] -> we check 2nd index (j vs j). then check 1st index (i vs i+1)
    Value* storePtr = storeGEPI->getPointerOperand();
    Value* loadPtr = loadGEPI->getPointerOperand();
    // errs()<<"storePtr: ";
    // storePtr->print(errs());
    // errs()<<"\n";
    // errs()<<"loadPtr: ";
    // loadPtr->print(errs());
    // errs()<<"\n";
    if(Instruction* storePtrI = dyn_cast<Instruction>(storePtr)){
      // errs()<<"storePtrI is contained: "<< L->contains(storePtrI) <<"\n";
      if( dyn_cast<GetElementPtrInst>(storePtr) && L->contains(storePtrI) ){
        return checkGEPSubscripts(storePtr, loadPtr, L, loopCounterName, counterUsedFlag);
      }
    }
    return false;
  }
  return IndependencyFlag;
}
//
void DependencyCheck::collectGEPUsedValues(GetElementPtrInst* GEPI, Loop* L, vector<pair<string,Value*>> &usedValVector)
{
  //check subscript //by using vector, we also check the order of appearance.
  for(Value* idxV : GEPI->indices())
  {
    //debug
    // errs()<<"checking GEP idx: ";
    // idxV->print(errs());
    // errs()<<"\n";
    traceOp(idxV,L,usedValVector);
  }
  //debug
  for(auto vecpair : usedValVector){
    errs()<<"recorded GEP idx: "<<vecpair.first;
    vecpair.second->print(errs());
    errs()<<"\n";
  }
  return;
}
//
void DependencyCheck::traceOp(Value* usedV, Loop* L, vector<pair<string,Value*>> &usedValVector)
{
  //set name
  string usedName = "GEP.dependency.check.";
  if(!usedV->hasName()){    
    usedV->setName(usedName);
  }
  usedName = usedV->getName();

  //record the value used in index
  if(Instruction* usedI = dyn_cast<Instruction>(usedV)){
    //if defined outside loop, record the value as it is.
    //else if it is phi, record the value as it is.
    //else (in the case that the value can cast to instruction), trace the operand, and record as it is
    if(L->contains(usedI) && usedI->getOpcode() != Instruction::PHI){
      for(Value* opeV : usedI->operands()){//NOTE: we can't use "auto" to get opeV. 
        traceOp(opeV,L,usedValVector);
      }
    }
    usedValVector.push_back(make_pair(usedName,usedV));
  }else if(ConstantExpr* CE = dyn_cast<ConstantExpr>(usedV)){
    Instruction* CEI = CE->getAsInstruction();
    traceOp(CEI,L,usedValVector);
  }
  //Constant, Argument, Alloca, and etc. which is not instruction (that means it is not defined in loop)
  else{
    usedValVector.push_back(make_pair(usedName,usedV));
  }
}

bool DependencyCheck::isSameConstant(Value* storeV, Value* loadV)
{
  //NOTE: ConstantExpr is handled at traceOp

  if(dyn_cast<ConstantInt>(storeV) && dyn_cast<ConstantInt>(loadV)){
    const APInt &storeC = dyn_cast<ConstantInt>(storeV)->getValue();
    const APInt &loadC = dyn_cast<ConstantInt>(loadV)->getValue();
    return (storeC == loadC);
  }
  else if(dyn_cast<ConstantFP>(storeV) && dyn_cast<ConstantFP>(loadV)){
    const APFloat &storeC = dyn_cast<ConstantFP>(storeV)->getValueAPF();
    const APFloat &loadC = dyn_cast<ConstantFP>(loadV)->getValueAPF();
    APFloatBase::cmpResult result = storeC.compare(loadC);
    return (result = APFloatBase::cmpResult::cmpEqual);
  }
  ////
  else if(
    (dyn_cast<ConstantAggregateZero>(storeV) && dyn_cast<ConstantAggregateZero>(loadV)) || 
    (dyn_cast<ConstantPointerNull>(storeV) && dyn_cast<ConstantPointerNull>(loadV)) ||
    (dyn_cast<ConstantTokenNone>(storeV) && dyn_cast<ConstantTokenNone>(loadV)) ||
    (dyn_cast<UndefValue>(storeV) && dyn_cast<UndefValue>(loadV))
  ){
    return true;
  }
  else if(dyn_cast<ConstantDataSequential>(storeV) && dyn_cast<ConstantDataSequential>(loadV)){
    ConstantDataSequential* storeC = dyn_cast<ConstantDataSequential>(storeV);
    ConstantDataSequential* loadC = dyn_cast<ConstantDataSequential>(loadV);
    unsigned elemNum = storeC->getNumElements();
    if(elemNum != loadC->getNumElements()){
      return false;
    }
    for(unsigned i=0; i<elemNum; i++){
      Constant* storeE = storeC->getElementAsConstant(i);
      Constant* loadE = loadC->getElementAsConstant(i);
      if( !isSameConstant(storeE, loadE) ){
        return false;
      }
    }
    return true;
  }
  //BlockAddress
  //ConstantAggregate <- ConstantArray, ConstantStruct, ConstantVector
  //GlobalValue <-  {GlobalIndirectSymbol <- GlobalAlias, GlobalFunc},
  //                {GlobalObject <- Function, GlobalVariable}
  errs()<<"[DependencyCheck] we can't check this constants (maybe one of them is not Constant)\n";
  errs()<<"storeV: ";
  storeV->print(errs());
  errs()<<"\n";
  errs()<<"loadV: ";
  loadV->print(errs());
  errs()<<"\n";
  return false;


}

//-######################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################


DependencyCheck::MapMap &DependencyCheck::getLoopCounterMap(Loop* L)
{
  LoopCounterMap.clear();
  collectLoopCounter(L);
  for(auto subL : *L){
    collectLoopCounter(subL);
  }
  return LoopCounterMap;
}

void DependencyCheck::collectLoopCounter(Loop* L)
{
  SmallVector<BasicBlock*, 8> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);
  for(BasicBlock* ExitingBlock : ExitingBlocks){
    if(BranchInst* BI = dyn_cast<BranchInst>(ExitingBlock->getTerminator())){
      if(BI->isConditional()){
        if(Instruction* CondInst = dyn_cast<Instruction>(BI->getCondition())){
          //the right successor won't contained in parallel execution 
          if(!ExitingBlock->hasName()){
            ExitingBlock->setName( "ExitingBlock" );          
          }
          ValMap LoopInfoMap;
          string loopCounterName;
          traceLoopCounter(CondInst, ExitingBlock, L, LoopInfoMap, loopCounterName);
          if(!LoopInfoMap.empty()){
            LoopInfoMap.insert(make_pair(
              loopCounterName+"_cond",
              CondInst
            ));
            LoopCounterMap.insert(make_pair(
              loopCounterName,
              LoopInfoMap
            ));
          }
          break;
        }//end of CondInst
      }
    }
  }
}

void DependencyCheck::traceLoopCounter(Instruction* CondInst, BasicBlock* ExitingBB, Loop* targetLoop, ValMap &LoopInfoMap, string &loopCounterName)
{         
  for(unsigned i=0;i<CondInst->getNumOperands();i++){
    if(Instruction* CondExprInst = dyn_cast<Instruction>(CondInst->getOperand(i))){
      //if load or phi, it may be loopcounter. So we check
                  
      if(CondExprInst->getOpcode() == Instruction::Load)
      {
        Value* LoadMemory = CondExprInst->getOperand(0);
        //nameflag is used to erase the set name if it is not a loop counter
        bool nameflag = LoadMemory->hasName();
        if( !nameflag ){
          LoadMemory->setName("loop_counter_candidate");      
        }
        //get loop counter name
        string LoadMemoryStr = LoadMemory->getName().str();
        if(LoopCounterMap.find(LoadMemoryStr) == LoopCounterMap.end())
        {
          for(BasicBlock* BB : targetLoop->blocks()){
            for(Instruction& SrcI : *BB){
              if(SrcI.getOpcode() == Instruction::Store)
              {
                Value* StoreMemory = SrcI.getOperand(1);
                Value* StoreValue = SrcI.getOperand(0);
                //if store inst (which exists in loop) stores to the memory
                //(which is used in loop exit condition), 
                //we determine it as loop counter.
                if( StoreMemory->hasName() &&
                    StoreMemory->getName().str() == LoadMemoryStr )
                {
                  //init search
                  Value* InitV = getInitValue(CondExprInst,targetLoop);
                  //debug
                  // errs()<<"LoopCounter "<< LoadMemoryStr <<"_init:";
                  // InitV->print(errs());
                  // errs()<<"\n";

                  //incr search
                  Value* IncrV = getIncrValue(StoreValue,targetLoop,LoadMemoryStr);
                  //debug
                  // errs()<<"LoopCounter "<< LoadMemoryStr <<"_incr: ";
                  // IncrV->print(errs());
                  // errs()<<"\n";
                  
                  LoopInfoMap.insert(make_pair(LoadMemoryStr+"_init",InitV));
                  LoopInfoMap.insert(make_pair(LoadMemoryStr+"_incr",IncrV));
                  loopCounterName = LoadMemoryStr;
                  return;
                }
              }
            }
          }

          //NOTE: we change mind that setting name may be useful.
          //if CondExprInst is not loop counter, and it is originaly not have name, erase the name
          // if(LoopCounterMap.find(LoadMemoryStr) == LoopCounterMap.end() && !nameflag){
          //   LoadMemory->setName("");
          // }
        }
        return;
      }//end of the case of Load
      else if( PHINode* PHIN = dyn_cast<PHINode>(CondExprInst) )
      {
        if(!PHIN->hasName()){
          PHIN->setName( "loop_counter_candidate" );
        }    
        string phistr = PHIN->getName().str();

        ValMap LoopCounterInfo;
        unsigned incNum = PHIN->getNumIncomingValues();
        for(unsigned i=0; i<incNum; i++){
          BasicBlock* incomingBB = PHIN->getIncomingBlock(i);
          Value* incomingV = PHIN->getIncomingValue(i);
          if( targetLoop->contains(incomingBB) && 
              LI->getLoopDepth(incomingBB) == targetLoop->getLoopDepth() 
            ){
            vector<Instruction*> checkedCandIVec; //2024 added
            if(isReferToItself(phistr, CondExprInst, targetLoop, &checkedCandIVec)){
              //insert the found incoming value
              LoopInfoMap.insert(make_pair(phistr+"_incr", incomingV));
              LoopInfoMap.insert(make_pair(phistr+"_init", PHIN));
              loopCounterName = phistr;
              //debug
              // errs()<<"Loop Counter is PHI: ";
              // PHIN->print(errs());
              // errs()<<"\n";
              return;
            }else if( Instruction* IncomingI = dyn_cast<Instruction>(incomingV) ){
              // errs()<<"phi tracing: trace again\n";
              traceLoopCounter(IncomingI,ExitingBB,targetLoop,LoopInfoMap,loopCounterName);
            }
          }
        }//end of for(i) loop
        return;
      }//end of PHINode
      //else, check the CondExprInst recursively.
      traceLoopCounter(CondExprInst,ExitingBB,targetLoop,LoopInfoMap,loopCounterName);
    }
  }    
}

Value* DependencyCheck::getInitValue(Instruction* LoopCounterLoad,Loop* targetLoop)
{
  Value* InitV;
  Value* LoadMemory = LoopCounterLoad->getOperand(0);
  if( !(LoopCounterLoad->hasName()) )
  {
    LoopCounterLoad->setName("loopcounter.cand.load");
  }
  string LoadStr = LoopCounterLoad->getName().str();

  for(User* MemUser: LoadMemory->users())
  {
    if(StoreInst* SI = dyn_cast<StoreInst>(MemUser))
    {
      Value* StoreMemory = SI->getOperand(1);
      if( StoreMemory->hasName() 
        && StoreMemory->getName().str() == LoadMemory->getName().str() )
      {
        InitV = SI->getOperand(0);
      }else if( MemUser->hasName() 
        && MemUser->getName().str() == LoadStr )
      {
        break;
      }
    }
  }
  return InitV;
}

Value* DependencyCheck::getIncrValue(Value* CheckV, const Loop* targetLoop,const string LoopCounterStr)
{
  //debug
  // errs()<<"Incr Check:";
  // CheckV->print(errs());
  // errs()<<"\n";

  if(Instruction* I = dyn_cast<Instruction>(CheckV))
  {
    Value* IncrV;
    //check operands repeatedly 
    if(dyn_cast<BinaryOperator>(CheckV)){
      for(Value* OpV : I->operand_values()){
        IncrV = getIncrValue(OpV, targetLoop, LoopCounterStr);
      }
      return IncrV;
    }
    //if load inst, check whether it is a loop counter.
    else if(LoadInst* LoadI =dyn_cast<LoadInst>(CheckV)){
      Value* LoadMemory = LoadI->getOperand(0);
      if( LoadMemory->hasName() && LoadMemory->getName().str() == LoopCounterStr ){
        return CheckV;
      }
    }
    //if phi inst, check whether it is a loop counter.
    else if(PHINode *PHIN = dyn_cast<PHINode>(CheckV)){
      for(Value* incomingV : PHIN->incoming_values()){
        //to avoid infinity loop of phi checking, we check the incoming value only comes out of the loop (loop invariant)
        if(Instruction* incomingI = dyn_cast<Instruction>(incomingV)){
          if(!targetLoop->contains(incomingI)){
            IncrV = getIncrValue(incomingV,targetLoop,LoopCounterStr);
          }
        }
      }
    }
    //if it is neither load, phi, or binary operator, check its operands
    // else{
    //   // for(Value* opV : I->operand_values())
    //   // {
    //   //   IncrV = getIncrValue(opV,targetLoop,LoopCounterStr);
    //   // }
    //   // return IncrV;
    // }
  }//end of I = dyn_cast<Instruction>)(CheckV)
  return CheckV;
}

bool DependencyCheck::isReferToItself(const string phistr, Instruction* CandI, const Loop* L, vector<Instruction*>* checkedCandIVec)
{
  //judge whether it refer it self
  //debug
  //errs()<<"[DependencyCheck] refer to itself checkI: ";
  //CandI->print(errs());
  //errs()<<"\n";
  //errs()<<"phistr: "<<phistr;
  //errs()<<"\n";

  // 2024 added
  for(Instruction* inst : *checkedCandIVec)
    if(inst == CandI)
      return false;
  checkedCandIVec->push_back(CandI);

  if(PHINode* PHIN = dyn_cast<PHINode>(CandI)){
    unsigned incNum = PHIN->getNumIncomingValues();
    for(unsigned i=0; i<incNum; i++){
      BasicBlock* incomingBB = PHIN->getIncomingBlock(i);
      Value* incomingV = PHIN->getIncomingValue(i);
      //debug
      //errs()<<"[DependencyCheck] phi: incoming: ";
      //incomingV->print(errs());
      //errs()<<"\n";

      if(incomingV->hasName() && incomingV->getName().str() == phistr){
        return true;
      }else if( L->contains(incomingBB) && LI->getLoopDepth(incomingBB) == L->getLoopDepth() ){
        if(Instruction* useI = dyn_cast<Instruction>(incomingV)){
          //set name for check of this phi's self refering
          if(!CandI->hasName()){
            CandI->setName("phi.self.refer.check");
          }
          string checkName = CandI->getName().str();
          if( isReferToItself(checkName,useI,L,checkedCandIVec) ){
            // errs()<<"[DependencyCheck] phi: self refering phi.\n";
            if(checkName == phistr){
              // errs()<<"[DependencyCheck] Reduction candidate self refering phi: ";
              // CandI->print(errs());
              // errs()<<"\n";
              return true;
            }else{
              // errs()<<"[DependencyCheck] Other self refering phi: ";
              // CandI->print(errs());
              // errs()<<"\n";
              return false;
            }
          }
        }
      }//end of L->contains(incomingV)
    }//end of for(i)
    return false;
  }else{
    for(Value* opV: CandI->operands()){
      //debug
      // errs()<<"[DependencyCheck] inst's operand check in refer to itself: ";
      // opV->print(errs());
      // errs()<<"\n";
      if(Instruction* useI = dyn_cast<Instruction>(opV)){
        if(useI->hasName() && useI->getName().str() == phistr){
          return true;
        }
        if( L->contains(useI) && LI->getLoopDepth(useI->getParent()) == L->getLoopDepth() ){
          if( isReferToItself(phistr,useI,L,checkedCandIVec) ){
            return true;
          }
        }
      }
    }//end of opV
    return false;
  }
  //debug
  // errs()<<"[DependencyCheck] Unreachable return in Refer To Itself Check: ";
  // CandI->print(errs());
  // errs()<<"\n";
  exit(1);
  // return false;
}


//-######################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################

DependencyCheck::BBMap& DependencyCheck::getAllPathBBMap(Loop* L)
{
  BasicBlock* entryBB = &targetF->getEntryBlock();
  BasicBlock* headerBB = L->getHeader();
  if(!headerBB->hasName()){
    headerBB->setName("loop.header");
  }
  AllPathBBMap.clear();
  recordAllPath(entryBB, L);
  return AllPathBBMap;
}


bool DependencyCheck::recordAllPath(BasicBlock* checkBB, Loop* L)
{
  //debug
  if(checkBB->hasName()){
    // errs()<<"record: "<<checkBB->getName().str()<<"\n";
  }else{
    // errs()<<"record: no name\n";
  }

  //if we found the loop header, record this path
  if(L->contains(checkBB)){
    return true;
  }
  //if other loop exist in path, skip and check next bb
  {
    Loop* pathL = LI->getLoopFor(checkBB);
    if( pathL ){
      bool passingFlag = false;
      // const unsigned exitsnum = pathL->getNumBlocks();
      SmallVector<BasicBlock*, 10> Exits;
      pathL->getExitBlocks(Exits);
      for(BasicBlock* exitBB : Exits){
        if( recordAllPath(exitBB,L) ){
          passingFlag = true;
        }
      }
      if(passingFlag){
        for(BasicBlock* loopBB : pathL->blocks()){
          if( !loopBB->hasName() ){
            loopBB->setName("loop.path");
          }
          AllPathBBMap.insert(make_pair( loopBB->getName().str(), loopBB ));
        }
      }
      return passingFlag;
    }
  }


  Instruction* checkI = checkBB->getTerminator();
  if( dyn_cast<ReturnInst>(checkI) || 
      dyn_cast<ResumeInst>(checkI) || 
      dyn_cast<UnreachableInst>(checkI)
  ){
    //debug
    // errs()<<"Reach an end BB of the function. we don't record this path.\n";
    return false;
  }else if(BranchInst *termI = dyn_cast<BranchInst>(checkI)){
    bool passingFlag = false;
    for(BasicBlock* successorBB : termI->successors()){
      if( recordAllPath(successorBB,L) ){
        passingFlag = true;
      }
    }
    if(passingFlag){
      if( !checkBB->hasName() ){
        checkBB->setName("loop.path");
      }
      AllPathBBMap.insert(make_pair( checkBB->getName().str(), checkBB ));
    }
    return passingFlag;
  }
  else if(SwitchInst *termI = dyn_cast<SwitchInst>(checkI)){
    bool passingFlag = false;
    for(unsigned i=0; i<termI->getNumSuccessors(); i++){
      BasicBlock* successorBB = termI->getSuccessor(i);
      if( recordAllPath(successorBB,L) ){
        passingFlag = true;
      }
    }
    if(passingFlag){
      if( !checkBB->hasName() ){
        checkBB->setName("loop.path");
      }
      AllPathBBMap.insert(make_pair( checkBB->getName().str(), checkBB ));
    }
    return passingFlag;
  }
  else if(IndirectBrInst *termI = dyn_cast<IndirectBrInst>(checkI)){
    bool passingFlag = false;
    for(BasicBlock* successorBB : termI->successors()){
      if( recordAllPath(successorBB,L) ){
        passingFlag = true;
      }
    }
    if(passingFlag){
      if( !checkBB->hasName() ){
        checkBB->setName("loop.path");
      }
      AllPathBBMap.insert(make_pair( checkBB->getName().str(), checkBB ));
    }
    return passingFlag;
  }
  else if(InvokeInst *termI = dyn_cast<InvokeInst>(checkI)){
    bool passingFlag = false;
    for(unsigned i=0; i<termI->getNumSuccessors(); i++){
      BasicBlock* successorBB = termI->getSuccessor(i);
      if( recordAllPath(successorBB,L) ){
        passingFlag = true;
      }
    }
    if(passingFlag){
      if( !checkBB->hasName() ){
        checkBB->setName("loop.path");
      }
      AllPathBBMap.insert(make_pair( checkBB->getName().str(), checkBB ));
    }
    return passingFlag;
  }
  else if(dyn_cast<CleanupReturnInst>(checkI) || dyn_cast<CatchReturnInst>(checkI)){
    // errs()<<"Sorry we can't check this BB because it terminator inst is CleanupReturnInst. This terminator inst have no method to access the successor BB.\n";
    return false;
    // bool passingFlag = false;
    // for(unsigned i=0; i<termI->getNumSuccessors(); i++){
    //   BasicBlock* successorBB = termI->getSuccessor(i);
    //   if( recordAllPath(successorBB,L,AllPathBBMap) ){
    //     passingFlag = true;
    //   }
    // }
    // if(passingFlag){
    //   if( !checkBB->hasName() ){
    //      checkBB->setName("loop.path");
    //   }
    //   AllPathBBMap.insert(make_pair( checkBB->getName().str(), checkBB ));
    // }
    // return passingFlag;
  }
  else if(CatchSwitchInst *termI = dyn_cast<CatchSwitchInst>(checkI)){
    bool passingFlag = false;
    for(unsigned i=0; i<termI->getNumSuccessors(); i++){
      BasicBlock* successorBB = termI->getSuccessor(i);
      if( recordAllPath(successorBB,L) ){
        passingFlag = true;
      }
    }
    if(passingFlag){
      if( !checkBB->hasName() ){
        checkBB->setName("loop.path");
      }
      AllPathBBMap.insert(make_pair( checkBB->getName().str(), checkBB ));
    }
    return passingFlag;
  }
  // errs()<<"[DependencyCheck] This is not a terminator inst in the llvm ver we support.\n";
  return false;
}

// //{

//-######################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################


