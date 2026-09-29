


#include "llvm/Analysis/ReductionDetect.h"

namespace llvm {
  FunctionPass *createReductionDetectPass(){return new ReductionDetect();}
}
char ReductionDetect::ID = 0;
static RegisterPass<ReductionDetect> ReductionDetectRegistration(
    "reductiondetect",
    "Detect reduction variables used by auto parallelization.", true, false);


bool ReductionDetect::runOnFunction(Function &F)
{
  // //debug
  // // errs()<<"------show Func--------";
  // // F.print(errs()); errs()<<"\n";
  // errs()<<"[Function: "<<F.getName().str()<<"]\n";
  if(F.empty()){
    return false;
  }
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  if(LI->empty()){ 
    return false; 
  }
  DC = &getAnalysis<DependencyCheck>();
  targetF = &F; // 2024 added

  return false;
}

void ReductionDetect::startCheck()
{
  // LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  // DC = &getAnalysis<DependencyCheck>();
  errs()<<"[ReductionDetect] start\n";
  //LoopInfo provide only for most outer loops
  for(auto litr = LI->begin(); litr != LI->end(); ++litr){
    Loop* L = *litr;
    checkRecursively(L);
  }//end of loop iteration
  errs()<<"[ReductionDetect] end\n";

}


//-###################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################### 

//if we don't check a loop's subloop, we can't detect 3 or more nested loop's dependency
void ReductionDetect::checkRecursively(Loop* L)
{
  for(Loop* subL : *L){
    checkRecursively(subL);
  }
  checkLoop(L);
}


void ReductionDetect::checkLoop(Loop* L)
{
  LoopCounterMap = &DC->getLoopCounterMap(L);
  CanDepMap = &DC->getDependencyCandidate(L);
  AllPathBBMap = &DC->getAllPathBBMap(L);
  AliasVec = DC->getAliasVec();  // 2024 added
  if(LoopCounterMap == nullptr || CanDepMap == nullptr || AllPathBBMap == nullptr){
    // errs()<<"[ReductionDetect:EE] Failed to get DependencyCheck pass's results\n";
    exit(1);
  }
  /* LoopCounterMap
  first string: this inst's name
  second map: LoopExpr: first + "_init", "_incr", and "_cond" can be accessed
  */
  //debug
  for(auto mappair : *LoopCounterMap){
    // errs()<<"[ReductionDetect] loopcounter name: ";
    // errs()<<mappair.first;
    // errs()<<"\n";
    // for(auto elempair : mappair.second){
    //   errs()<<"elem: "<<elempair.first;
    //   elempair.second->print(errs());
    //   errs()<<"\n";
    // }
    // errs()<<"\n";
  }
  //
  string loopCounterName;
  for(auto mappair : *LoopCounterMap){
    Value* incrV = mappair.second.find(mappair.first+"_cond")->second;
    if(Instruction* I = dyn_cast<Instruction>(incrV)){
      if(LI->getLoopDepth(I->getParent()) == L->getLoopDepth()){
        // counterV =  mappair.second.find(mappair.first+"_init")->second;
        loopCounterName = mappair.first;
        break;
      }
    }
  }

  // check Reduction
  // SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  for(auto mappair : *CanDepMap){
    //skip the loop counter
    Value* depV = mappair.second;
    //debug
    // errs()<<"Check Dependency: ";
    // depV->print(errs());
    // errs()<<"\n::: start :::\n";
    //check start

    if(Instruction* depI = dyn_cast<Instruction>(depV)){
      //NOTE: for now, we only check the phi inst      
      RedCalcMap.clear();
      checkReductionCandidate(depI, L);
    }
  }
}


//-###################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################### reduction

void ReductionDetect::checkReductionCandidate(Instruction* depI, Loop* L)
{
  // errs()<<"------check reduction-------\n";
  bool ReductionFlag = true;
  bool ReferItSelfFlag = false;
  Value* redMemory = nullptr;
  string reductionName;
  //for phi
  if(PHINode* PHIN = dyn_cast<PHINode>(depI))
  {
    //if it has no name, we abort check
    if(!depI->hasName()){
      // errs()<<"[ReductionDetect] Checked phi inst should have name: ";
      // depI->print(errs());
      // errs()<<"\n";
      exit(1);
    }
    reductionName = depI->getName().str();
    //else if it is one of loop counters, we abort check
    if(LoopCounterMap->find(reductionName) != LoopCounterMap->end()){
      //debug
      // errs()<<"This is one of phi loop counters: ";
      // depI->print(errs());
      // errs()<<"\n";
      return;
    }

    //the incoming value which is from loop is checked for SCEVable
    unsigned incNum = PHIN->getNumIncomingValues();
    for(unsigned i=0; i<incNum; i++){
      BasicBlock* incomingBB = PHIN->getIncomingBlock(i);
      if( L->contains(incomingBB) && LI->getLoopDepth(incomingBB) == L->getLoopDepth() ){
        Value* incomingV = PHIN->getIncomingValue(i);
        ReductionFlag &= usesReductionableOperands(incomingV,reductionName,redMemory,L,ReferItSelfFlag);
      }//end of L->contains(incomingV)
    }//end of phi

  }//end of PHIN
  else if(StoreInst* SI = dyn_cast<StoreInst>(depI))
  {
    Value* storeV = SI->getOperand(0);
    redMemory = SI->getOperand(1);
    //if it has no name, we abort check
    if(!redMemory->hasName()){
      // errs()<<"[ReductionDetect] Checked store memory should have name. Please set name: ";
      // redMemory->print(errs());
      // errs()<<"\n";
      exit(1);
    }

    //else if it is one of loop counters, we abort check
    reductionName = redMemory->getName().str();
    if(LoopCounterMap->find(reductionName) != LoopCounterMap->end()){
      //debug
      // errs()<<"This is one of load loop counters: ";
      // depI->print(errs());
      // errs()<<"\n";
      return;
    }

    if(!storeV->hasName()){
      storeV->setName( reductionName + ".reduction" );
    }
    ReductionFlag = usesReductionableOperands(storeV, reductionName, redMemory, L, ReferItSelfFlag);
  }
  else{
    // errs()<<"[ReductionDetect] We didn't expect that the inst causes dependency: ";
    // depI->print(errs());
    // errs()<<"\n";
    exit(1);
  }

  //check the reduction-ability
  if( ReductionFlag && ReferItSelfFlag ){
    //TODO
    // 2024 added ===================================
    errs()<<"[RD] checking instruction: "<<*depI<<"\n";
    if(!dyn_cast<StoreInst>(depI)){
      errs()<<"[RD] not store inst: "<<*depI<<"\n";
      return;
    }

    // noaliasか
    //debug
    errs()<<"[RD] ---------AliasVec---------\n";
    for(Value* v : AliasVec){
      v->print(errs());
      errs()<<"\n";
    }
    errs()<<"[RD] -----------------------\n";
    for(Value* v : AliasVec){
      if(v == depI){
        errs()<<"[RD] find in AliasVec, cannot parallelize: "<<*depI<<"\n";
        return;
      }
    }
    
    // GEP探索
    vector<Value*> GEPVec;
    getGEPUse(depI->getOperand(1), &GEPVec);
    //debug
    errs()<<"[RD] ---------GEPVec---------\n";
    for(Value* v : GEPVec){
      v->print(errs());
      errs()<<"\n";
    }
    errs()<<"[RD] -----------------------\n";

    // ループカウンタ記録
    vector<Value*> LoopCounterVec;
    getLoopCounterVec(L, &LoopCounterVec);
    //debug
    errs()<<"[RD] ---------LoopCounterVec---------\n";
    for(Value* v : LoopCounterVec){
      v->print(errs());
      errs()<<"\n";
    }
    errs()<<"[RD] -----------------------\n";

    //判定
    for(auto mappair : UsePartialCounterMap)
      if(mappair.second.first == dyn_cast<Value>(depI)){
        errs()<<"[RD] "<<*depI<<" is already in UsePartialCounterMap\n";
        return;
      }
    for(auto mappair : UseAllCounterMap)
      if(mappair.second == dyn_cast<Value>(depI)){
        errs()<<"[RD] "<<*depI<<" is already in UseAllCounterMap\n";
        return;
      }
    for(auto mappair : ReductionableMap)
      if(mappair.second == dyn_cast<Value>(depI)){
        errs()<<"[RD] "<<*depI<<" is already in ReductionableMap\n";
        return;
      }

    unsigned depth = getUsedCounterDepth(&GEPVec, &LoopCounterVec);
    if(depth == LoopCounterVec.size()){
      errs()<<"[RD] use all counter, no need to use reduction.\n";
      UseAllCounterMap.insert(make_pair(reductionName, dyn_cast<Value>(depI)));
      return;
    }
    else if(depth == 0){
      errs()<<"[RD] should use reduction.\n";
    }
    else{
      errs()<<"[RD] cannot parallelize. depth: "<<depth<<"\n";
      UsePartialCounterMap.insert(make_pair(reductionName, make_pair(dyn_cast<Value>(depI), LoopCounterVec[depth-1])));
      return;
    }
    // ===================================

    // errs()<<"[ReductionDetect] Dependency entry's calculation is reduction.\n";
    //check the user
    for(auto elempair : RedCalcMap){
      Value* elemV = elempair.second;
      //debug
      // errs()<<"Reduction calculation element: ";
      // elemV->print(errs());
      // errs()<<"\n";
      //
      if( !isUsedOnlyForRedCalc(elemV,reductionName,redMemory,L) ){
        // errs()<<"[ReductionDetect] NOT reduction-able. Calculation inter result is used not only for reduction calculation: ";
        break;
      }
    }
    if( !isUsedOnlyForRedCalc(dyn_cast<Value>(depI),reductionName,redMemory,L) ){
      // errs()<<"[ReductionDetect] NOT reduction-able. Reduction result is used not only for reduction calculation: ";
    }else{
      // errs()<<"[ReductionDetect] Reduction-able: ";
      ReductionableMap.insert(make_pair(reductionName, dyn_cast<Value>(depI)));
    }
  }else{
    // errs()<<"[ReductionDetect] NOT reduction-able. Dependency entry's calculation is not reduction: ";
  }
  // depI->print(errs());
  // errs()<<"\n";
}

//TODO
//2024 added ================================
void ReductionDetect::getGEPUse(Value* V, vector<Value*>* vec){
  // debug
  //errs()<<"[RD] "<<__func__<<": ";
  //V->print(errs());
  //errs()<<"\n";

  vec->push_back(V);
  if(Instruction* inst = dyn_cast<Instruction>(V))
    for(Value* opV : inst->operands())
      if(find(vec->begin(), vec->end(), opV) == vec->end())
        getGEPUse(opV, vec);
}

// Lから最外ループまでのループカウンタを記録
void ReductionDetect::getLoopCounterVec(Loop* L, vector<Value*>* LoopCounterVec){
  MapMap tmpLCMap = DC->getLoopCounterMap(L);
  //debug
  errs()<<"[RD] ---------tmpLCMap---------\n";
  for(auto mappair : tmpLCMap){
    errs()<<mappair.first;
    errs()<<"\n";
    for(auto elempair : mappair.second){
      errs()<<"elem: "<<elempair.first;
      elempair.second->print(errs());
      errs()<<"\n";
    }
    errs()<<"\n";
  }
  errs()<<"[RD] ------------------\n";

  Value* checkingLoopCounter = nullptr;  // 探索中ループカウンタ
  for(auto mappair : tmpLCMap){
    map<string,Value*> tmpMap = mappair.second;
    for(auto elempair : tmpMap)
      checkingLoopCounter = elempair.second;
  }

  LoopCounterVec->push_back(checkingLoopCounter);

  if(Loop* outerLoop = L->getParentLoop())
    getLoopCounterVec(outerLoop, LoopCounterVec);
}

unsigned ReductionDetect::getUsedCounterDepth(vector<Value*>* GEPVec, vector<Value*>* LoopCounterVec){
  unsigned depth = 0;
  for(Value* lc : *LoopCounterVec){
    if(find(GEPVec->begin(), GEPVec->end(), lc) == GEPVec->end())
      return depth;
    depth++;
  }
  return depth;
}
// ================================

//-###################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################### load/store memory


//User check is done after operand check
//check the user of checkV is valid
// for(Value* userV : checkV->users()){
//   if(StoreInst* SI = dyn_cast<StoreInst>(userV)){
//     Value* storeMemory = SI->getOperand(1);
//     if(redMemory == nullptr){
//       if(!storeMemory->hasName()){
//         storeMemory->setName("reduction.memory");
//       }
//       redMemory = storeMemory;
//     }else if(!storeMemory->hasName() || storeMemory->getName() != redMemory->getName()){
//       errs()<<"[ReductionDetect] Storing different memory:\n";
//       errs()<<"[ReductionDetect] Reduction check memory: ";
//       redMemory->print(errs());
//       errs()<<"\n";
//       errs()<<"[ReductionDetect] User memory: ";
//       storeMemory->print(errs());
//       errs()<<"\n";
//       return false;
//     }
//     continue;
//   }else if(PHINode* PHIN = dyn_cast<PHINode>(userV)){
//     //the incoming value which is from loop is checked for SCEVable
//     unsigned incNum = PHIN->getNumIncomingValues();
//     for(unsigned i=0; i<incNum; i++){
//       BasicBlock* incomingBB = PHIN->getIncomingBlock(i);
//       if( L->contains(incomingBB) && LI->getLoopDepth(incomingBB) == L->getLoopDepth() ){
//         Value* incomingV = PHIN->getIncomingValue(i);
//         if( !isReductionableMemory(incomingV,redMemory,L,ReferItSelfFlag) ){
//           errs()<<"[ReductionDetect] User of phi incoming is not reductionable: ";
//           incomingV->print(errs());
//           errs()<<"\n";
//           return false;
//         }
//       }//end of L->contains(incomingV)
//     }//end of phi
//     continue;
//   }else if(LoadInst* LoadI = dyn_cast<LoadInst>(userV)){
//     Value* loadMemory = LoadI->getOperand(0);
//     if(loadMemory->hasName()){
//       if( (redMemory != nullptr && loadMemory->getName() == redMemory->getName()) ||
//           (CanDepMap.find(loadMemory->getName().str()) == CanDepMap.end())
//       ){
//         errs()<<"[ReductionDetect] Reductionable user of load inst: ";
//         userV->print(errs());
//         errs()<<"\n";  
//         continue;
//       }
//     }
//     errs()<<"[ReductionDetect] User of load inst is not reductionable: ";
//   }
// }

//-###################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################### phi



bool ReductionDetect::usesReductionableOperands(Value* checkV, const string reductionName, Value* redMemory, Loop* L, bool &ReferItSelfFlag)
{
  //if constant, return true
  if(!dyn_cast<Instruction>(checkV)){
    // errs()<<"[ReductionDetect] It is not instruction. We assume it reductionable: ";
    // checkV->print(errs());
    // errs()<<"\n";
    return 1;
  }
  else if(Instruction* checkI = dyn_cast<Instruction>(checkV)){
    //If load instruction's memory is same as store memory, return true
    if(checkI->getOpcode() == Instruction::Load){
      Value* loadMemory = checkI->getOperand(0);
      if( loadMemory->hasName() ){
        //errs()<<"loadMemory: ";
        //loadMemory->print(errs());
        //errs()<<"\n";
        //errs()<<"redMemory: ";
        //redMemory->print(errs());
        //errs()<<"\n";

        if( redMemory != nullptr && loadMemory->getName().str() == redMemory->getName().str() ){
          //debug
          // errs()<<"[ReductionDetect] Load: same memory:";
          // checkI->print(errs());
          // errs()<<"\n";
          ReferItSelfFlag = true;
          return 1;
        }
        if( CanDepMap->find(loadMemory->getName().str()) != CanDepMap->end() ){
          //debug
          // errs()<<"[ReductionDetect] Load: dependency entry exists in map:";
          // checkI->print(errs());
          // errs()<<"\n";
          return 0;
        }
      }
      //debug
      // errs()<<"[ReductionDetect] Load: independent:";
      // checkI->print(errs());
      // errs()<<"\n";
      return 1;
    }
    
    //record the used inst (we don't record the load inst)
    if(!checkI->hasName()){ 
      checkI->setName(reductionName + ".used");
    }
    RedCalcMap.insert(make_pair(checkI->getName().str(),checkI));

    //phi check
    if(PHINode* PHIN = dyn_cast<PHINode>(checkI)){
      string checkName = PHIN->getName().str();
      //if it is reduction target phi, it is reductionable
      //this is for the check of phi dependency
      if(checkName == reductionName){
        //debug
        // errs()<<"[ReductionDetect] PHI: refer it self: ";
        // checkV->print(errs());
        // errs()<<"\n";
        ReferItSelfFlag = true;
        return 1;
      }
      else if( CanDepMap->find(checkName) != CanDepMap->end() ){
        //debug
        // errs()<<"[ReductionDetect] PHI: dependency: ";
        // checkV->print(errs());
        // errs()<<"\n";
        return 0;
      }
      //FIXME: we should handle phi incomings. But how?
      else if(LoopCounterMap->find(checkName) != LoopCounterMap->end()){
        //debug
        // errs()<<"[ReductionDetect] PHI: loop counter: ";
        // checkV->print(errs());
        // errs()<<"\n";
        return 1;
      }
      unsigned incNum = PHIN->getNumIncomingValues();
      for(unsigned i=0; i<incNum; i++){
        BasicBlock* incomingBB = PHIN->getIncomingBlock(i);
        if( L->contains(incomingBB) && LI->getLoopDepth(incomingBB) == L->getLoopDepth() ){
          Value* incomingV = PHIN->getIncomingValue(i);
          if( !usesReductionableOperands(incomingV,reductionName,redMemory,L,ReferItSelfFlag) ){
            //debug
            // errs()<<"[ReductionDetect] PHI: incoming is not reductionable: ";
            // incomingV->print(errs());
            // errs()<<"\n";
            return 0;
          }
        }
      }
      //debug
      // errs()<<"[ReductionDetect] PHI: reductionable incoming: ";
      // checkV->print(errs());
      // errs()<<"\n";
      return 1;
    }    
    //for other inst,
    // errs()<<"[ReductionDetect] other inst: ";
    for(Value* opV : checkI->operands()){
      if( !usesReductionableOperands(opV,reductionName,redMemory,L,ReferItSelfFlag) ){
        //debug
        // errs()<<"[ReductionDetect] Other: operand is not reductionable: ";
        // opV->print(errs());
        // errs()<<"\n";
        return 0;
      }
    }
    //debug
    // errs()<<"[ReductionDetect] Other: reductionable operand: ";
    // checkV->print(errs());
    // errs()<<"\n";
    return 1;     
  }

  //else, we can't check this
  // errs()<<"[ReductionDetect] we can't check this: ";
  // checkV->print(errs());
  // errs()<<"\n";
  exit(1);
  // return 0;
}

bool ReductionDetect::isUsedOnlyForRedCalc(Value* usedV, const string reductionName,Value* redMemory, Loop* L)
{
  for(Value* userV : usedV->users()){
    if(Instruction* userI = dyn_cast<Instruction>(userV)){ 
      // //debug
      // errs()<<"userI:\t";
      // userI->print(errs());errs()<<"\n";

      //if reduction is used in out of loop, skip it
      if(!(L->contains(userI))){
        //debug
        // errs()<<"An use out of the loop, we ignore this:";
        // userI->print(errs());errs()<<"\n";
        continue;
      }
      //if memory of store inst is same to the reduction variable, check next   
      else if( userI->getOpcode() == Instruction::Store ){
        Value* StoreMemory = userI->getOperand(1);
        if( StoreMemory->hasName() && redMemory != nullptr && redMemory->hasName() &&
            StoreMemory->getName().str() == redMemory->getName().str() ){
          // errs()<<"Store to reduction memory:";
          // userI->print(errs());errs()<<"\n";
          continue;
        }
      }
      if(userI->hasName()){
        string userName = userI->getName().str();
        //if it is reduction itself, check next
        if(userName == reductionName){
          // errs()<<"This is reduction phi:";
          // userI->print(errs());errs()<<"\n";
          continue;
        }
        //if used in reduction calculation, check next 
        else if(RedCalcMap.find(userName) != RedCalcMap.end()){
          // errs()<<"This is used in reduction calculation:";
          // userI->print(errs());errs()<<"\n";
          continue;
        }
      }
    }
    // errs()<<"FALSE: This is used out of reduction calculation:";
    // userV->print(errs());errs()<<"\n";
    return false;
  }
  //debug
  // errs()<<"Result:"<< FlagOfResult <<"\n";
  return true;
}
