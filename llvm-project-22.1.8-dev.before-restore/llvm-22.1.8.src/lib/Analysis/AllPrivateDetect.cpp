


#include "llvm/Analysis/AllPrivateDetect.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

namespace llvm {
  FunctionPass *createAllPrivateDetectPass(){return new AllPrivateDetect();}
}
char AllPrivateDetect::ID = 0;
static RegisterPass<AllPrivateDetect> AllPrivateDetectRegistration(
    "allprivatedetect", "Detect private and lastprivate variables.", true,
    false);



bool AllPrivateDetect::runOnFunction(Function &F)
{
  // //debug
  // // errs()<<"------show Func--------";
  // // F.print(errs()); errs()<<"\n";
  errs()<<"[Function: "<<F.getName().str()<<"]\n";
  string funcName = F.getName().str();

  if( F.empty() 
      //|| funcName.find("kernel") == string::npos
  ){
    return false;
  }
  
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  if( LI->empty() ){ 
    return false; 
  }
  DC = &getAnalysis<DependencyCheck>();
  SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  // startCheck();
  errs()<<"[AllPrivateDetect] return runOnFunction\n";
  return false;
}

void AllPrivateDetect::startCheck()
{
  // LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  // DC = &getAnalysis<DependencyCheck>();
  errs()<<"[AllPrivateDetect] start\n";
  //LoopInfo provide only for most outer loops
  for(auto litr = LI->begin(); litr != LI->end(); ++litr)
  {
    Loop* L = *litr;
    checkRecursively(L);
  }//end of liter
  errs()<<"[AllPrivateDetect] end\n";
}

//if we don't check a loop's subloop, we can't detect 3 or more nested loop's dependency
void AllPrivateDetect::checkRecursively(Loop* L)
{
  checkLoop(L);
  for(Loop* subL : *L){
    checkRecursively(subL);
  }
}

void AllPrivateDetect::checkLoop(Loop* L)
{
  LoopCounterMap = &DC->getLoopCounterMap(L);
  CanDepMap = &DC->getDependencyCandidate(L);
  AllPathBBMap = &DC->getAllPathBBMap(L);
  if(LoopCounterMap == nullptr || CanDepMap == nullptr || AllPathBBMap == nullptr){
    errs()<<"[AllPrivateDetect:EE] Failed to get DependencyCheck pass's results\n";
    exit(1);
  }

  /* LoopCounterMap
  first string: this inst's name
  second map: LoopExpr: first + "_init", "_incr", and "_cond" can be accessed
  */
  //debug
  for(auto mappair : *LoopCounterMap){
    // errs()<<"[AllPrivateDetect] loopcounter name: ";
    // errs()<<mappair.first;
    // errs()<<"\n";
    for(auto elempair : mappair.second){
      // errs()<<"elem: "<<elempair.first;
      // elempair.second->print(errs());
      // errs()<<"\n";
    }
    // errs()<<"\n";
  }
  string loopCounterName;
  Value* counterV;
  for(auto mappair : *LoopCounterMap){
    Value* incrV = mappair.second.find(mappair.first+"_cond")->second;
    if(Instruction* I = dyn_cast<Instruction>(incrV)){
      if(LI->getLoopDepth(I->getParent()) == L->getLoopDepth()){
        counterV =  mappair.second.find(mappair.first+"_init")->second;
        loopCounterName = mappair.first;
        break;
      }
    }
  }
  // SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  ConstantInt* incNum = nullptr;
  //To get the full dependency, loop counter should be phi (else we can't analyze array access dependency)   
  if( dyn_cast<PHINode>(counterV) ){
    //debug
    // errs()<<"loop counter is:";
    // counterV->print(errs());
    // errs()<<"\n";
    incNum = getConstantLoopStep(counterV);
  }

  //debug
  // DI = &getAnalysis<DependenceAnalysisWrapperPass>().getDI();
  // errs()<<"-- This instruction may cause a dependency --\n";
  // for( auto pairptr : *CanDepMap ){
  //   errs()<< pairptr.first << ": ";
  //   pairptr.second->print(errs());
  //   errs()<<"\n";
  // }
  //---- end of dependency check ----

  //check whether any private variable exists.
  bool updateFlag = true;
  while(updateFlag){
    updateFlag = false;
    updateFlag |= recordLastPrivate(loopCounterName, L);
    updateFlag |= recordPrivate(loopCounterName, L);
    if(incNum != nullptr){
      updateFlag |= recordFirstPrivate(loopCounterName, L, incNum);
    }
    if(updateFlag){
      for(auto mappair : PrivateMap){
        AllPrivateMap.insert(make_pair(mappair.first, mappair.second));
      }
      for(auto mappair : LastPrivateMap){
        AllPrivateMap.insert(make_pair(mappair.first, mappair.second));
      }
      for(auto mappair : FirstPrivateMap){
        AllPrivateMap.insert(make_pair(mappair.first, mappair.second));
      }
    }
    //debug
    // else{
    //   errs()<<"[FirstPrivateDetect] we can't analyze with out full dependency, which can obtain if loop counter's LB, UB, and INC are constant numbers.\n";
    // }
  }
  
  //debug
  errs()<<"### private variables ###\n";
  for(auto mappair : AllPrivateMap){
    // errs()<<""<<mappair.first<<"\n";
    mappair.second->print(errs());
    errs()<<"\n";
  }
  errs()<<"### CanDepMap ###\n";
  for(auto mappair : *CanDepMap){
    errs()<<mappair.first<<": ";
    mappair.second->print(errs());
    errs()<<"\n";
  }
  errs()<<"### all private variable check is Over ###\n\n\n";

}


//-######################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################



//private common


bool AllPrivateDetect::checkLastPrivateCandidate(Loop* L, string loopCounterName)
{
  bool resolvedFlag = false;
  for(auto mappair : *CanDepMap){
    string dependencyName = mappair.first;
    Value* dependencyValue = mappair.second;
    // errs()<<"-- check: "<<dependencyName;
    // dependencyValue->print(errs());
    // errs()<<"\n";

    //if already checked the candidate, skip it
    if(AllPrivateMap.find(dependencyName) != AllPrivateMap.end()){
      continue;
    }
    if(LastPrivateMap.find(dependencyName) != LastPrivateMap.end()){
      continue;
    }

    if(Instruction* dependencyI = dyn_cast<Instruction>(dependencyValue)){
      if(dependencyI->getOpcode() == Instruction::Store){
        Value* checkingMemory = dependencyI->getOperand(1);
        Value* checkV = dependencyI->getOperand(0);
        if( isPrivatizable(checkV, dependencyI, L, loopCounterName) 
        ){
          //debug
          // errs()<<"Checking memory: ";
          // checkingMemory->print(errs());
          // errs()<<"\n";
          if( (checkingMemory->hasName() && checkingMemory->getName().str() == loopCounterName) || 
              isLoadedAfterStored(checkingMemory,L))
          {
            //for the lastprivate, we check whether the use after loop exists
            // errs()<<"we can privatize this. Next we check whether it should be lastprivate\n";
            if( isUsedAfterLoop(checkingMemory,L) ){
              LastPrivateMap.insert(make_pair(
                dependencyName, dependencyValue
              ));
              resolvedFlag = true;
            }
          }
        }
      }//end of checkingMemory (dependencyI is Instruction::Store)
    }
    if(LastPrivateMap.find(dependencyName) != LastPrivateMap.end()){
      // errs()<<"---> lastprivatizable: "<< dependencyName <<"\n";
    }else{
      // errs()<<"--> NOT lastprivatizable: "<< dependencyName <<"\n";
    }
  }
  return resolvedFlag;
}


bool AllPrivateDetect::checkPrivateCandidate(Loop* L,    string loopCounterName)
{
  bool resolvedFlag = false;
  for(auto mappair : *CanDepMap){
    string dependencyName = mappair.first;
    Value* dependencyValue = mappair.second;
    // errs()<<"-- check: "<<dependencyName;
    // dependencyValue->print(errs());
    // errs()<<"\n";

    //if already checked the candidate, skip it
    if(AllPrivateMap.find(dependencyName) != AllPrivateMap.end()){
      continue;
    }

    if(Instruction* dependencyI = dyn_cast<Instruction>(dependencyValue)){
      if(dependencyI->getOpcode() == Instruction::Store){
        Value* checkingMemory = dependencyI->getOperand(1);
        Value* checkV = dependencyI->getOperand(0);
        if( isPrivatizable(checkV, dependencyI, L, loopCounterName) 
        ){
          //debug
          // errs()<<"Checking memory: ";
          // checkingMemory->print(errs());
          // errs()<<"\n";
          if((checkingMemory->hasName() && checkingMemory->getName().str() == loopCounterName) || isLoadedAfterStored(checkingMemory,L))
          {
            PrivateMap.insert(make_pair(
              dependencyName, dependencyValue
            ));
            resolvedFlag = true;
          }
        }
      }//end of checkingMemory (dependencyI is Instruction::Store)
    }
    if(PrivateMap.find(dependencyName) != PrivateMap.end()){
      // errs()<<"---> privatizable: "<< dependencyName <<"\n";
    }else{
      // errs()<<"--> NOT privatizable: "<< dependencyName <<"\n";
    }
  }
  return resolvedFlag;
}


bool AllPrivateDetect::checkFirstPrivateCandidate(Loop* L, string loopCounterName, ConstantInt* incNum)
{
  bool resolvedFlag = false;
  for(auto mappair : *CanDepMap){
    string dependencyName = mappair.first;
    Value* dependencyValue = mappair.second;
    // errs()<<"-- check: "<<dependencyName;
    // dependencyValue->print(errs());
    // errs()<<"\n";

    //if already checked the candidate, skip it
    if(AllPrivateMap.find(dependencyName) != AllPrivateMap.end()){
      continue;
    }

    if(Instruction* ptrI = dyn_cast<Instruction>(dependencyValue)){
      if(ptrI->getOpcode() == Instruction::Store){
        Value* checkingMemory = ptrI->getOperand(1);
        Value* rootMemory = getRootOfMemory(checkingMemory);
        //get the type of root memory
        Type* rootType = rootMemory->getType();
        if(AllocaInst* AI = dyn_cast<AllocaInst>(rootMemory)){
          rootType = AI->getAllocatedType();
        }else if(GlobalValue* GV = dyn_cast<GlobalValue>(rootMemory)){
          rootType = GV->getValueType();
        }
        // errs()<<"The root is:";
        // rootMemory->print(errs());
        // errs()<<"\n";

        if( dyn_cast<ArrayType>(rootType) )
        {
          //debug
          // errs()<<"This memory points the array: ";
          // checkingMemory->print(errs());
          // errs()<<"\n";
          // errs()<<"The array is:";
          // rootMemory->print(errs());
          // errs()<<"\n";
          //set name of root memory
          if(!rootMemory->hasName()){
            rootMemory->setName("firstprivate.array");
          }
          string arrayName = rootMemory->getName().str();
          Value* storedValue = ptrI->getOperand(0);
          if( isAccessingSameArray(arrayName, storedValue) )
          {
            // errs()<<"accessing same array.\n";
            vector<Value*> DepLoadVec; 
            collectDependingLoad(ptrI, storedValue, L, DepLoadVec);
            //debug
            bool sameDirectionFlag = true;
            for(Value* elemV : DepLoadVec){
              // errs()<<"depending load: ";
              // elemV->print(errs());
              // errs()<<"\n";
              
              //get depend direction if possible
              auto Dep = DI->depends(dyn_cast<Instruction>(elemV), ptrI, true);
              //if store(ptrI) index is greater than load(elemV), 
              //the dependence direction is LT(1). Else GT(4)
              //
              //if a detail result is not obtained, we give up this analysis
              unsigned loopLevel = L->getLoopDepth();
              if( loopLevel <= Dep->getLevels() && loopLevel > 0 ){
                unsigned depDirection = Dep->getDirection(loopLevel);
                // errs()<<"dep direction"<<depDirection<<"\n";
                if( depDirection != Dependence::DVEntry::NE ||  
                    depDirection != Dependence::DVEntry::ALL 
                ){
                  // errs()<<"we can use full dependency.\n";
                  // errs()<<"with incNum: ";
                  // incNum->print(errs());
                  // errs()<<"\n";
                  if( (incNum->isNegative() && 
                        (depDirection == Dependence::DVEntry::GT || depDirection == Dependence::DVEntry::GE)) ||
                      (!incNum->isNegative() && 
                        (depDirection == Dependence::DVEntry::LT || depDirection == Dependence::DVEntry::LE))
                  ){
                      // errs()<<"loop increment direction and dependence direction is same.\n";
                      continue;
                  }
                }
              }
              // errs()<<"[FirstPrivateDetect] we can't continue analysis because we can't use full dependency.\n"; 
              sameDirectionFlag = false;
              break;
            }//end of DepLoadVec

            //if all the dependences' direction are same to loop increment direction,
            //we can make it firstprivate
            if(sameDirectionFlag){
              if(Instruction* dependencyI = dyn_cast<Instruction>(dependencyValue)){
                if( isPrivatizable(checkingMemory, dependencyI, L, loopCounterName) ){
                  // errs()<<"This value is firstprivatizable.\n";
                    FirstPrivateMap.insert(make_pair(
                      dependencyName, dependencyValue
                    ));
                }
              }
            }
          }//end of isAccesingSameArray
        }//end of CompositeType
        //debug
        else{
          // errs()<<"we only set the variable which is the array that should be initialized before parallel execution as firstprivate.\n";
        }
      }//end of checkingMemory (ptrI is Instruction::Store)
    }

    //debug//report the result of check
    if(FirstPrivateMap.find(dependencyName) != FirstPrivateMap.end()){
      // errs()<<"---> firstprivatizable: "<< dependencyName <<"\n";
    }else{
      // errs()<<"--> NOT firstprivatizable: "<< dependencyName <<"\n";
    }
  }
  return resolvedFlag;
}

//-######################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################



bool AllPrivateDetect::isPrivatizable(Value* CheckValue, Instruction* DependInst, Loop* L,    const string loopCounterName)
{
  //debug
  // errs()<<"used operand: ";
  // CheckValue->print(errs());
  // errs()<<"\n";

  //in private detection, we just check store inst's dependency
  if(DependInst->getOpcode() != Instruction::Store){
    // errs()<<"We only check store inst, NOT for: ";
    // DependInst->print(errs());
    // errs()<<"\n";
  }

  
  if(Instruction* CheckI = dyn_cast<Instruction>(CheckValue)){
    //If load instruction's memory is same as store memory, return true(it causes loop carried dependency)
    Value* DependMemory = DependInst->getOperand(1);
    const string StoreMemoryStr = DependMemory->getName().str();
    if(LoadInst* LoadI = dyn_cast<LoadInst>(CheckValue))
    {
      Value* LoadMemory = LoadI->getOperand(0);
      //if the inst loads from GEP memory, use DI
      if( dyn_cast<GetElementPtrInst>(LoadMemory) &&
          dyn_cast<GetElementPtrInst>(DependMemory) 
        ){
        return checkGEPDependency(DependInst, CheckI, L);
      }
    }
  }
  return isIndependentValue(CheckValue, loopCounterName, L);   
}


bool AllPrivateDetect::checkGEPDependency(Instruction* DependInst, Instruction* CheckI, Loop* L)
{
  auto Dep = DI->depends(DependInst, CheckI, /*PossiblyLoopInvariant*/true );
  if(  Dep && (Dep->isAnti() || Dep->isFlow()) ){
    if(!Dep->isLoopIndependent()){
      // errs()<<"\t:GEP: Loop carried dependency\n";
      return 0;
    }
  }
  // errs()<<"\t:GEP: No loop carried dependency\n";
  return 1;
}
//
bool AllPrivateDetect::checkLoadDependency(const string loopCounterName, Instruction* CheckI)
{
  string loadMemoryName = CheckI->getOperand(0)->getName().str();
  if( loadMemoryName == loopCounterName ){
    //debug
    // errs()<<"\t:Load: Target loop's counter, it does not cause dependency\n";
    return true;
  }else if( AllPrivateMap.find(loadMemoryName) != AllPrivateMap.end() ){
    //debug
    // errs()<<"\t:Load: Private variable, it does not cause dependency\n";
    return true;
  }
  else if( CanDepMap->find(loadMemoryName) == CanDepMap->end() ){
    //debug
    // errs()<<"\t:Load: Data dependency not found in CanDepMap\n";
    return true;
  }
  //debug
  // errs()<<"\t:Load: Dependency exists\n";
  // errs()<<"Load dependency:"<< loadMemoryName <<"\n";
  return false;
}
//
bool AllPrivateDetect::checkPHIDependency(const string loopCounterName,  Instruction* checkI, Loop* L)
{
  if(!checkI->hasName()){
    checkI->setName("phi.independency.check");
  }
  string phiName = checkI->getName().str();
  if(phiName == loopCounterName){
    //debug
    // errs()<<"\t:PHI: This is Loop counter: ";
    // checkI->print(errs());
    // errs()<<"\n";
    return true;
  }else if( AllPrivateMap.find(phiName) != AllPrivateMap.end() ){
    //debug
    // errs()<<"\t:PHI: Private variable, it does not cause dependency:";
    // checkI->print(errs());
    // errs()<<"\n";
    return true;
  }
  else if( CanDepMap->find(phiName) == CanDepMap->end() ){
    //debug
    // errs()<<"\t:Private: PHI has no dependency: ";
    // checkI->print(errs());
    // errs()<<"\n";
    return true;
  }
  //debug
  // errs()<<"\t:Private: PHI is found in CanDepMap: ";
  // checkI->print(errs());
  // errs()<<"\n";
  return false;

  // if(PHINode* PHIN = dyn_cast<PHINode>(checkI)){
  //   for(Value* incomingV : PHIN->incoming_values()){
  //     if(Instruction* incomingI = dyn_cast<Instruction>(incomingV)){
  //       if( isReferToItself(phiName, incomingI, L)){
  //         //debug
  //         errs()<<"Private: PHI refers it self\n";
  //         checkI->print(errs());
  //         errs()<<"\n";
  //         return false;
  //       }
  //     }
  //   }
  // }
}


bool AllPrivateDetect::isLoadedAfterStored(Value* checkingMemory, Loop* L)
{
  if(!checkingMemory->hasName()){
    // errs()<<"[PrivateDetect] private memory should have name.\n";
  }
  string checkingMemoryName = checkingMemory->getName().str();

  //collect load    //collect store
  BasicBlock* headerBB = L->getHeader();
  if(!headerBB->hasName()){
    headerBB->setName("load.after.store.check");
  }
  string headerName = headerBB->getName().str();
  bool LoadAfterStoreFlag = true;
  checkBranchRecursively(checkingMemoryName,L,headerName,headerBB,LoadAfterStoreFlag);
  return LoadAfterStoreFlag;
}

//------------------------------

void AllPrivateDetect::checkBranchRecursively(string checkingMemoryName, Loop* L, string headerName, BasicBlock* BB, bool &LoadAfterStoreFlag)
{
  //if the result was already obtainde, return.
  if(!LoadAfterStoreFlag){
    return;
  }
  //check store or load in BB. if found, return. 
  if(checkBB(checkingMemoryName,L,BB,LoadAfterStoreFlag)){
    // errs()<<"[AllPrivateDetect] load or store found.\n";
    return;
  }
  Instruction* termI = BB->getTerminator();
  vector<BasicBlock*> sucBBs;
  getSuccessorBBs(termI,sucBBs);
  for(BasicBlock* nextBB : sucBBs){
    if(nextBB->hasName() && nextBB->getName().str() == headerName){
      // errs()<<"[AllPrivateDetect] branch to header BB. we skip this.\n";
      continue;
    }
    checkBranchRecursively(checkingMemoryName,L,headerName,nextBB,LoadAfterStoreFlag);
  }
  return;
}

bool AllPrivateDetect::checkBB(string checkingMemoryName, Loop* L, BasicBlock* BB,bool &LoadAfterStoreFlag)
{
  for(Instruction &I : *BB){
    if(I.getOpcode() == Instruction::Store)
    {
      Value* checkedMemory = I.getOperand(1);
      if( checkedMemory->hasName() &&
          checkedMemory->getName().str() == checkingMemoryName ){
        // errs()<<"Stored before load. This is privatizable\n";
        LoadAfterStoreFlag &= true;
        return true;
      }
    }
    //load
    else if(I.getOpcode() == Instruction::Load)
    {
      Value* checkedMemory = I.getOperand(0);
      if( checkedMemory->hasName() && 
          checkedMemory->getName().str() == checkingMemoryName ){
        // errs()<<"Loaded before store. This is NOT privatizable\n";
        LoadAfterStoreFlag = false;
        return true;
      }
    }
  }//end of I
  return false;
}

void AllPrivateDetect::getSuccessorBBs(Instruction* checkI, vector<BasicBlock*> sucBBs)
{
  if( dyn_cast<ReturnInst>(checkI) || dyn_cast<ResumeInst>(checkI) || dyn_cast<UnreachableInst>(checkI) ){
    //debug
    // errs()<<"Reach an end BB of the function. we don't record this path.\n";
    return;
  }else if(BranchInst *termI = dyn_cast<BranchInst>(checkI)){
    for(BasicBlock* successorBB : termI->successors()){
      sucBBs.push_back(successorBB);
    }
    return;
  }
  else if(IndirectBrInst *termI = dyn_cast<IndirectBrInst>(checkI)){
    for(BasicBlock* successorBB : termI->successors()){
      sucBBs.push_back(successorBB);
    }
    return;
  }
  else if(SwitchInst *termI = dyn_cast<SwitchInst>(checkI)){
    for(unsigned i=0; i<termI->getNumSuccessors(); i++){
      BasicBlock* successorBB = termI->getSuccessor(i);
      sucBBs.push_back(successorBB);
    }
    return;
  }
  else if(InvokeInst *termI = dyn_cast<InvokeInst>(checkI)){
    for(unsigned i=0; i<termI->getNumSuccessors(); i++){
      BasicBlock* successorBB = termI->getSuccessor(i);
      sucBBs.push_back(successorBB);
    }
    return;
  }
  else if(CatchSwitchInst *termI = dyn_cast<CatchSwitchInst>(checkI)){
    for(unsigned i=0; i<termI->getNumSuccessors(); i++){
      BasicBlock* successorBB = termI->getSuccessor(i);
      sucBBs.push_back(successorBB);
    }
    return;
  }
  else if(dyn_cast<CleanupReturnInst>(checkI) || dyn_cast<CatchReturnInst>(checkI)){
    // errs()<<"Sorry we can't check this BB because it terminator inst is CleanupReturnInst. This terminator inst have no method to access the successor BB.\n";
    return;
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
  // errs()<<"[AllPrivateDetect] This is not a terminator inst in the llvm ver we support.\n";
  return;
}


//#######################################################################################################################################################################################################################################################################################################################################################
//lastprivate



bool AllPrivateDetect::recordLastPrivate(string loopCounterName, Loop* L)
{
  // errs()<<"------------- LastPrivate variable Check -------------\n";
  bool updateFlag = false;

  // //debug
  // errs()<<"All BBs in the path to loop header:\n";
  // for(auto mappair : AllPathBBMap){
  //   errs()<<"BB: "<<mappair.first<<"\n";
  // }

  //if the phi inst in loop is used after loop, we set it as lastprivate.
  //Since the phi inst is not detected as dependency, we do this manually.
  // errs()<<"-- lastprivate PHI check --\n";
  for(BasicBlock* BB : L->blocks()){
    for(Instruction &I : *BB){
      if( PHINode* PHIN = dyn_cast<PHINode>(&I) )
      {
        Value* checkV = dyn_cast<Value>(&I);
        if( isUsedAfterLoop(checkV,L) && checkV->hasName()){
          string lastName = checkV->getName().str();
          if(LastPrivateMap.find(lastName) == LastPrivateMap.end()){
          //if it is loop counter, set it as lastprivate
            if(checkV->getName().str() == loopCounterName){
              //debug
              // errs()<<"The loop counter is used after loop: ";
              // checkV->print(errs());
              // errs()<<"\n";
              //
              LastPrivateMap.insert(make_pair(lastName, checkV));
              updateFlag = true;
            }
            else
            {
              bool UsedInLoopFlag = false;
              for(User* user : PHIN->users()){
                if(Instruction* userI = dyn_cast<Instruction>(user)){
                  if(L->contains(userI)){
                    UsedInLoopFlag = true;
                    break;
                  }
                }
              }
              //if it is not used in loop, then there is no dependency
              if(!UsedInLoopFlag){
                //check whether the incoming value is using depending value
                bool IndependentFlag = true;
                for(Value* incomingV : PHIN->incoming_values()){
                  if(!isIndependentValue(incomingV,loopCounterName,L)){
                    IndependentFlag = false;
                    break;
                  }
                }
                if(IndependentFlag){
                  //debug
                  // errs()<<"this phi is used after loop and not used in loop:\n";
                  // checkV->print(errs());
                  // errs()<<"\n";
                  LastPrivateMap.insert(make_pair(
                    lastName, checkV
                  ));
                  updateFlag = true;
                }
              }
            }
          }//end of LastPrivateMap.find
        }//end of isUsedAfterLoop == true
      }//end of PHIN
    }
  }
  // errs()<<"-- lastprivate PHI check END --\n";
  for(auto mappair : LastPrivateMap){
    // errs()<<"Last private: ";
    // mappair.second->print(errs());
    // errs()<<"\n";
  }

  return checkLastPrivateCandidate(L,loopCounterName) || updateFlag;
}

bool AllPrivateDetect::isUsedAfterLoop(Value* checkingMemory, Loop* L)
{
  if(Instruction* checkingI = dyn_cast<Instruction>(checkingMemory)){
    for(Value* userV : checkingI->users()){
      // //debug
      // errs()<<"used after loop check: ";
      // userV->print(errs());
      // errs()<<"\n";

      if(Instruction* userI = dyn_cast<Instruction>(userV)){
        BasicBlock* userBB = userI->getParent();
        if( L->contains(userI) ||
            (userBB->hasName() && AllPathBBMap->find(userBB->getName().str()) != AllPathBBMap->end())
          ){
          // errs()<<"This inst is used before loop. It doesn't require lastprivate\n"<<userBB->getName().str()<<"\n";          
          continue;
        }
        // errs()<<"This memory is used after loop.\n";
        return true;
      }
    }//end of userV for loop
  }
  return false;
}

bool AllPrivateDetect::isIndependentValue(Value* checkV, const string loopCounterName, Loop* L)
{
  //debug
  // errs()<<"isIndependent?: ";
  // checkV->print(errs());
  // errs()<<"\n";
  
  //if constant, return true
  if( dyn_cast<Constant>(checkV) || dyn_cast<Argument>(checkV) ){
    //if operand is Constant or Argument, it is not modified at this loop, so return 1 
    return true;
  }
  else if(Instruction* checkI = dyn_cast<Instruction>(checkV)){
    if( L->contains(checkI) && LI->getLoopDepth(checkI->getParent()) < L->getLoopDepth() ){
      // errs()<<"Private: Defined in child loop\n";
      return true;
    }
    //If load instruction's memory is same as store memory, return true(it causes loop carried dependency)
    if(LoadInst* LoadI = dyn_cast<LoadInst>(checkV))
    {
      Value* LoadMemory = LoadI->getOperand(0);
      if(LoadMemory->hasName()){
        return checkLoadDependency(loopCounterName, checkI);
      }
      //if no name, it has no dependency because we set name when detect dependency (CanDapMap)
      return true;
    }
    else if(dyn_cast<PHINode>(checkV))
    {
      return checkPHIDependency(loopCounterName, checkI, L);
    }else
    {
      // load命令以外の場合は，そのオペランドが定数(グローバル変数含む)か，引数であればOK.
      for(Value* opV : checkI->operands()){
        if(dyn_cast<Instruction>(opV)){
          if(!isIndependentValue(opV,loopCounterName,L)){
            return false;
          }
        }
      }
      return true;
    }
  }
  //debug
  // errs()<<"[PrivateDetect] we can't check, we assume this operand is privatizable: ";
  // checkV->print(errs());
  // errs()<<"\n";
  return true;

}


//-######################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################



bool  AllPrivateDetect::recordPrivate(string loopCounterName,  Loop* L)
{
  // errs()<<"----------Private variable Check-----------\n";
  return checkPrivateCandidate(L,loopCounterName);
}


//-######################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################

ConstantInt* AllPrivateDetect::getConstantLoopStep(Value* counterV)
{
  //debug
  // errs()<<"counterV: ";
  // counterV->print(errs());
  // errs()<<"\n";

  //obtain the constant increment value
  ConstantInt* incNum = nullptr;
  if(SE->isSCEVable(counterV->getType())){
    auto counterSCEV = SE->getSCEV(counterV);
    // errs()<<"get constant loop counter:";
    // counterSCEV->print(errs());
    // errs()<<"\n";
    if(const SCEVAddRecExpr* recSCEV = dyn_cast<SCEVAddRecExpr>(counterSCEV)){
      const SCEV* stepSCEV = recSCEV->getStepRecurrence(*SE);
      // errs()<<"loop counter step:";
      // stepSCEV->print(errs());
      // errs()<<"\n";
      if(const SCEVConstant* incSCEV = dyn_cast<SCEVConstant>(stepSCEV) ){
        incNum = incSCEV->getValue();
        // errs()<<"incNum:";
        // incNum->print(errs());
        // errs()<<"\n";
      }
    }
  }
  if(incNum == nullptr){
    // errs()<<"[FirstPrivateDetect] we can't analyze with out full dependency, which can obtain if loop counter's LB, UB, and INC are constant numbers.\n"; 
    // exit(1);
  }else{
    // errs()<<"[FirstPrivateDetect] obtained incNum: ";
    // incNum->print(errs());
    // errs()<<"\n";
  }
  return incNum;
}



bool  AllPrivateDetect::recordFirstPrivate(string loopCounterName,  Loop* L,   ConstantInt* incNum)
{
  //-- first private detection --//
  // errs()<<"-------------- Firstprivate variable check START -------------\n";
  return checkFirstPrivateCandidate(L,loopCounterName, incNum);
}




Value* AllPrivateDetect::getRootOfMemory(Value* checkingValue)
{
  //if checking memory is alloca or argument or global, 
  //(which means it is end of GEP or cast)
  //we return the value of checking memory.
  if( dyn_cast<AllocaInst>(checkingValue) ||
      dyn_cast<Argument>(checkingValue) ||
      dyn_cast<GlobalValue>(checkingValue)
  ){
    return checkingValue;
  }

  if(Instruction* checkingI = dyn_cast<Instruction>(checkingValue))
  {
    if(checkingI->getOpcode() == Instruction::GetElementPtr){
      //index ptr is (GEP->getPointerOperand()) same to operand 0
      Value* indexMemory = checkingI->getOperand(0);
      return getRootOfMemory(indexMemory);
    }
    else if(dyn_cast<CastInst>(checkingI)){
      Value* castedValue = checkingI->getOperand(0);
      return getRootOfMemory(castedValue);
    }
    else if(checkingI->getOpcode() == Instruction::Load){
      Value* loadedValue = checkingI->getOperand(0);
      return getRootOfMemory(loadedValue);
    }
  }

  // errs()<<"[FirstPrivateDetect] we can NOT check whether this is the degregate type:\n\t";    
  // checkingValue->print(errs());
  // errs()<<"\n";
  return checkingValue;
}




bool AllPrivateDetect::isAccessingSameArray(const string arrayName, Value* checkingValue)
{
  //debug
  // errs()<<"checking:";
  // checkingValue->print(errs());
  // errs()<<"\n";

  if( dyn_cast<AllocaInst>(checkingValue) || dyn_cast<Argument>(checkingValue) ||
      dyn_cast<GlobalValue>(checkingValue) ){
    if( checkingValue->hasName() && checkingValue->getName().str() == arrayName ){
      return true;
    }
    return false;
  }
  else if(LoadInst* LoadI = dyn_cast<LoadInst>(checkingValue))
  {
    Value* rootMemory = getRootOfMemory(LoadI->getOperand(0));
    return isAccessingSameArray(arrayName, rootMemory);
  }
  else if(dyn_cast<PHINode>(checkingValue)){
    // errs()<<"[FirstPrivateDetect] can't handle phi in check of same array:";    
    // checkingValue->print(errs());
    // errs()<<"\n";
    return false;
  }
  else if(Instruction* checkingI = dyn_cast<Instruction>(checkingValue)){
    for(Value* opV : checkingI->operands()){
      if( isAccessingSameArray(arrayName, opV) ){
        return true;
      }
    }
    return false;
  }

  // errs()<<"[FirstPrivateDetect] we do NOT check this value:";    
  // checkingValue->print(errs());
  // errs()<<"\n";
  return false;
}




void AllPrivateDetect::collectDependingLoad(Instruction* storeI, Value* checkingValue,   Loop* L, vector<Value*> &DepLoadVec)
{
  //debug
  // errs()<<"collecting:";
  // checkingValue->print(errs());
  // errs()<<"\n";

  errs()<<"storeI:";
  storeI->print(errs());
  errs()<<"\n";
  errs()<<"checkingValue:";
  checkingValue->print(errs());
  errs()<<"\n";

  if(LoadInst* loadI = dyn_cast<LoadInst>(checkingValue)){
    auto Dep = DI->depends(storeI, dyn_cast<Instruction>(checkingValue), /*PossiblyLoopInvariant*/true );
    if( Dep && (Dep->isAnti() || Dep->isFlow()) ){
      if(!Dep->isLoopIndependent()){
        // errs()<<"GEP: Loop carried dependency\n";
        DepLoadVec.push_back(checkingValue);
      }
    }
  }
  else if(dyn_cast<PHINode>(checkingValue)){
    // errs()<<"We don't trace phi. So this judge could be wrong. How to fix?\n";
    // errs()<<"[FirstPrivateDetect] in checking the depending load, we can't handle phi:";
    // checkingValue->print(errs());
    // errs()<<"\n";
    return;
    // for(Value* incomingV : PHIN->incoming_values()){
    //   if(Instruction* incomingI = dyn_cast<Instruction>(incominV)){
    //     if( L->contains( incomingI ) &&
    //         L->getLoopDepth() == LI->getLoopDepth( incomingI->getParent() ) 
    //     ){
    //       collectDependingLoad(storeI, user, L, DepLoadVec);
    //     }
    //   }
    // }//end of incomingV
  }
  else if(Instruction* checkingI = dyn_cast<Instruction>(checkingValue)){
    for(Value* opV : checkingI->operands()){
      collectDependingLoad(storeI, opV, L, DepLoadVec);
    }
  }
  //end of loadI
}


















// //-------end private
