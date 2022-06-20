//===- MemoryMonitor.cpp - Instrument programs to report its internal state ---------------===//
// Copyright (C) 2021  José Wesley de S. Magalhães
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//===----------------------------------------------------------------------===//
//
// This file implements a pass that inserts in a program the code necessary to report
// its internal state at different points during the execution. The data structure used 
// to track this state is called Memory Monitor. The program state is reported using
// inspection point. An inspection point is a program point where the values manipulated
// by the program are observed. In the current implementation, inspection points are
// created at the return point of functions. In that point, the values of all the variables 
// on the stack of the function are printed, along with the state of the heap and the 
// static variables. The user can chooses which memory regions (stack, static, or heap)
// will be inspected and instrument only the main routine or all the user-defined functions
// in the program.
//
//===----------------------------------------------------------------------===//


#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h" //To use the instructions iterator
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/CFG.h" //To iterate over the predecessors of a basic block
#include "llvm/IR/Dominators.h" //To use the dominance tree of a program
#include "llvm/IR/DebugInfo.h" //To get Metadata about a Module
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h" //To use command line flags
#include "llvm/Support/Debug.h" //To use LLVM_DEBUG macro with fine grained debug
#include "llvm/ADT/Statistic.h" // For the STATISTIC macro.

#include "../include/MemoryMonitor.h"

#define DEBUG_TYPE "MemoryMonitor"

using namespace llvm;

//This flag tells the pass to create inspection points only in the main function or in every user-defined function (default).
cl::opt<bool> OnlyMain ("om", cl::init(false), cl::desc("Instrument only the main function of the program"));
//This flag tells the pass to inspect the variables stored in the stack of functions
cl::opt<bool> InsStack ("stk", cl::init(false), cl::desc("Inspect only the variables on the stack of functions"));
//This flag tells the pass to inspect the variables that point to heap-allocated data
cl::opt<bool> InsHeap ("hp", cl::init(false), cl::desc("Inspect only the variables pointing to the heap"));
//This flag tells the pass to inspect the variables stored in static memory
cl::opt<bool> InsStatic ("stc", cl::init(false), cl::desc("Inspect only the static variables"));
//This flag tells the pass to inspect the content pointed by pointers. This characterizes the PRECISE mode
cl::opt<bool> TrackPtr ("pr", cl::init(false), cl::desc("Enables precise mode"));
//This flag tells the pass to inspect the entire heap at the inspection points
cl::opt<bool> InsFullHeap ("fp", cl::init(false), cl::desc("Inspect the entire heap"));

STATISTIC(TotalVars, "Number of variables inspected");
STATISTIC(ExtendedVars, "Number of extended live ranges");
STATISTIC(Var2Stack, "Number of variables shadowed in the stack");
STATISTIC(HeapOperations, "Number of heap operations");
STATISTIC(InstFunc, "Number of functions instrumented");
STATISTIC(DiffVar, "Number of variables with different SSA types");

std::string MemoryMonitor::GetSourceLine(Instruction* I){
  if(I->hasMetadata("dbg")){
    MDNode* MD = I->getMetadata("dbg");
    if(DILocation* Loc = dyn_cast<DILocation>(MD))
      return std::to_string(Loc->getLine());
  }
  return "undertemined";
}

CallInst* MemoryMonitor::InsertFunctionCall(std::string FuncName, Type* ReturnType, std::vector<Type*>ArgsType, std::vector<Value*>Args, IRBuilder<> Builder, bool IsVarArg){
  FunctionType *FuncType = FunctionType::get(ReturnType, ArgsType, IsVarArg);
  FunctionCallee FuncCallee = this->M->getOrInsertFunction(FuncName, FuncType);
  return Builder.CreateCall(FuncCallee, Args);
}

void MemoryMonitor::OpenOutputFile(IRBuilder<> Builder){
  //Creating the global output file.
  StructType* IO_FILE = (StructType::create(M->getContext(), "struct._IO_FILE"));
  StructType* IO_marker = (StructType::create(M->getContext(), "struct._IO_marker"));
  PointerType* IO_FILE_Ptr = IO_FILE->getPointerTo();

  std::vector<Type*> Elements;
  Elements.push_back(Builder.getInt32Ty());
  for(int i=1; i<=11; i++)
      Elements.push_back(Builder.getInt8PtrTy());
  Elements.push_back(IO_marker->getPointerTo());
  Elements.push_back(IO_FILE_Ptr);
  Elements.push_back(Builder.getInt32Ty());
  Elements.push_back(Builder.getInt32Ty());
  Elements.push_back(Builder.getInt64Ty());
  Elements.push_back(Builder.getInt16Ty());
  Elements.push_back(Builder.getInt8Ty());
  Elements.push_back(ArrayType::get(Builder.getInt8Ty(), 1));
  Elements.push_back(Builder.getInt8PtrTy());
  Elements.push_back(Builder.getInt64Ty());
  for(int i=1; i<=4; i++)
    Elements.push_back(Builder.getInt8PtrTy());
  Elements.push_back(Builder.getInt64Ty());
  Elements.push_back(Builder.getInt32Ty());
  Elements.push_back(ArrayType::get(Builder.getInt8Ty(), 20));
  IO_FILE->setBody(Elements, false);

  Elements.clear();
  Elements.push_back(IO_marker->getPointerTo());
  Elements.push_back(IO_FILE_Ptr);
  Elements.push_back(Builder.getInt32Ty());
  IO_marker->setBody(Elements, false);  

  //Get the basename of the file
  std::string ProgramName = this->M->getSourceFileName();
  
  GlobalVariable* IO_FILE_Definition = new GlobalVariable(*(this->M), IO_FILE_Ptr, false, GlobalValue::CommonLinkage, Constant::getNullValue(PointerType::getUnqual(IO_FILE)), ProgramName + "_Output");
  IO_FILE_Definition->setDSOLocal(true);
  this->OutputFile = IO_FILE_Definition;
  this->OutputFileType = IO_FILE_Ptr;
  
  //Opening the output file
  std::vector<Type*> ArgsType;
  std::vector<Value*> Args;
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt8PtrTy());

  Args.push_back(Builder.CreateGlobalStringPtr( StringRef(ProgramName + "_Output"), "str"));
  Args.push_back(Builder.CreateGlobalStringPtr(StringRef("w"), "str"));
  Builder.CreateStore(InsertFunctionCall("fopen", IO_FILE_Ptr, ArgsType, Args, Builder, false), this->OutputFile);
}

void MemoryMonitor::CloseOutputFile(Value* OutputFilePtr, IRBuilder<> Builder){
  FunctionCallee FcloseCall = M->getOrInsertFunction("fclose", Builder.getInt32Ty(), this->OutputFileType);
  Builder.CreateCall(FcloseCall, OutputFilePtr);
}

bool MemoryMonitor::ShouldProcessType(DIType *DIT){
  if(!DIT)
    return true;
  
  if(isa<DIBasicType>(DIT))
    return true;
  
  if(DIDerivedType* DIDT = dyn_cast<DIDerivedType>(DIT)){
    if(DIDT->getTag() == dwarf::DW_TAG_member || DIDT->getTag() == dwarf::DW_TAG_ptr_to_member_type)
      return false;
    else
      return ShouldProcessType(DIDT->getBaseType());
  }
  
  if(DICompositeType* DICT = dyn_cast<DICompositeType>(DIT)){
    if(DICT->getTag() == dwarf::DW_TAG_array_type)
      return dyn_cast<llvm::DISubrange>(DICT->getElements()[0])->getCount().is<llvm::ConstantInt*>();
    else
      return DICT->getElements().size() > 0;
  }
    
  if(isa<DISubroutineType>(DIT))
      return false;
  
  return true;
}

int MemoryMonitor::GetTypeFormat(DIType* DIT){
  //The return of this function is a code representing the format of the data which will be inserted in the 
  //pointers table. It tells the table how to print this data.
  int Format;
  unsigned TypeEncoding;
  
  if(!DIT)
    return 14; //void type
  
  if(DIBasicType* DIBT = dyn_cast<DIBasicType>(DIT))
    TypeEncoding = DIBT->getEncoding();
  
  if(DIDerivedType* DIDT = dyn_cast<DIDerivedType>(DIT))
    TypeEncoding = DIDT->getTag();
  
  if(DICompositeType* DICT = dyn_cast<DICompositeType>(DIT)){
    if(DICT->getTag() ==  dwarf::DW_TAG_enumeration_type)
      return 6;
    TypeEncoding = DICT->getTag();
  }
  
  //These values of encoding are based on the DWARF tags
  switch(TypeEncoding){
    case dwarf::DW_ATE_float:
      Format = DIT->getName() == "double" ? 1 : 2;
      break;
      
    case dwarf::DW_ATE_signed:
      if(DIT->getName() == "short")
        Format = 3;
      else if(DIT->getName() == "long int")
        Format = 4;
      else if(DIT->getName() == "long long int")
        Format = 5;
      else
        Format = 6;
      break;
      
    case dwarf::DW_ATE_signed_char:
      Format = 7;
      break;
      
    case dwarf::DW_ATE_unsigned_char:
      Format = 8;
      break;
      
    case dwarf::DW_ATE_unsigned:
      if(DIT->getName() == "unsigned short")
        Format = 9;
      else if(DIT->getName() == "long unsigned int")
        Format = 10;
      else if(DIT->getName() == "long long unsigned int")
        Format = 11;
      else
        Format = 12;
      break;
      
    case dwarf::DW_TAG_pointer_type:
      Format = 13;
      break;
      
    case dwarf::DW_TAG_array_type:
      Format = 15;
      break;
     
    case dwarf::DW_TAG_union_type:
      Format = 16;
      break;
      
    case dwarf::DW_TAG_structure_type:
      Format = 17;
      break;
         
    case dwarf::DW_TAG_const_type:  
    case dwarf::DW_TAG_typedef:
      return GetTypeFormat(dyn_cast<DIDerivedType>(DIT)->getBaseType());
          
    default:
      #define DEBUG_TYPE "tt"
      LLVM_DEBUG(dbgs() << "Unknow format type! Type Name: " << DIT->getName() <<"\n"; DIT->dump(););
      #undef DEBUG_TYPE
      break;
  }
  return Format;
}

std::string MemoryMonitor::MakeTypeName(DIType* DIT){
  std::string TypeName;
  
  if(!DIT)
    TypeName = "void";
  else if(DIBasicType* DIBT = dyn_cast<DIBasicType>(DIT))
    TypeName = DIBT->getName().str();
  else if(DIDerivedType* DIDT = dyn_cast<DIDerivedType>(DIT)){
    switch(DIDT->getTag()){
      case dwarf::DW_TAG_pointer_type:
        TypeName = "pointer to " + MakeTypeName(DIDT->getBaseType());
        break;
        
      case dwarf::DW_TAG_const_type:
        TypeName = "const " + MakeTypeName(DIDT->getBaseType());
        break;
        
      case dwarf::DW_TAG_typedef:
        TypeName = DIDT->getName();
        break;
        
      default:
        break;
    }
  }
  else if(DICompositeType* DICT = dyn_cast<DICompositeType>(DIT)){
    switch(DICT->getTag()){
      case dwarf::DW_TAG_array_type:
        TypeName = "array of " + MakeTypeName(DICT->getBaseType());
        break;
        
      case dwarf::DW_TAG_structure_type:
        TypeName = "struct " + DICT->getName().str();
        break;
        
      case dwarf::DW_TAG_union_type:
        TypeName = "union " + DICT->getName().str();
        break;
        
      case dwarf::DW_TAG_enumeration_type:
        TypeName = "enum " + DICT->getName().str();
        break;
      
      default:
        break;
    }
  }
  return TypeName;
}

std::string MemoryMonitor::MakeTypeName(Type* T){
  std::string TypeName;
  
  if(T->isVoidTy())
    TypeName = "void";
  
  if(T->isIntegerTy()){
    switch(dyn_cast<IntegerType>(T)->getBitWidth()){
      case 8:
        TypeName = "char";
        break;
        
      case 16:
        TypeName = "short";
        break;
        
      case 32:
        TypeName = "int";
        break;
        
      case 64:
        TypeName = "long";
        break;
    }
  }
  
  if(T->isFloatTy())
    TypeName = "float";
    
  if(T->isDoubleTy())
    TypeName = "double";
  
  if(T->isPointerTy())
    TypeName = "pointer to " + MakeTypeName(dyn_cast<PointerType>(T)->getElementType());
  
  if(T->isArrayTy())
    TypeName = "array of " + MakeTypeName(T->getArrayElementType());
  
  if(T->isStructTy()){
    //The struct type names are like struct.Name 
    if(dyn_cast<StructType>(T)->isLiteral() || dyn_cast<StructType>(T)->isOpaque())
      TypeName = "Literal or opaque struct";
    if(T->getStructName().startswith("union"))
      TypeName = "union "  + T->getStructName().substr(6).str();
    else
      TypeName = "struct " + T->getStructName().substr(7).str();
  }
  
  return TypeName;  
}

void MemoryMonitor::WriteTypeDescriptor(const char* TypeName, int QuantFields, int Format, int Offset, int BaseTypeIndex, FILE* TypeTableFile, int* TypeTableSize, DINodeArray Fields){
  #define DEBUG_TYPE "tt"
  LLVM_DEBUG(dbgs() << "Creating type table entry " << TypeName <<". Number of Fields = " << QuantFields <<"\n";);
  #undef DEBUG_TYPE
  fwrite(TypeName, sizeof(char), 129, TypeTableFile);
  fwrite(&QuantFields, sizeof(int), 1, TypeTableFile);
  (*TypeTableSize)++;
  
  if(Fields.size() > 0){
    for(unsigned i = 0; i < Fields.size(); i++){
      DIDerivedType* Field = dyn_cast<DIDerivedType>(Fields[i]);
      std::string FieldName = Field->getName().str();
      int FieldFormat = GetTypeFormat(Field->getBaseType());
      int FieldBaseTypeIndex = FieldFormat;
      if(!ShouldProcessType(Field->getBaseType()))
        FieldFormat = FieldBaseTypeIndex = 18;
      else if(DIDerivedType* DIDT = dyn_cast<DIDerivedType>(Field->getBaseType())){
        //Get the base type index for this type          
        for(auto &T : this->TypeIndexes){
          if(std::get<2>(T) == DIDT->getBaseType()){
            FieldBaseTypeIndex = std::get<1>(T);
            break;
          }
        }
      }
      else if(DICompositeType* DICT = dyn_cast<DICompositeType>(Field->getBaseType())){
        //If a field within an struct type is an array of scalars, we use the base type index to access the 
        //type descriptor of that array
        if(DICT->getTag() == dwarf::DW_TAG_array_type && isa<DIBasicType>(DICT->getBaseType())){
          for(auto &T : this->TypeIndexes){
            if(std::get<2>(T) == Field->getBaseType()){
              FieldBaseTypeIndex = std::get<1>(T);
              break;
            }
          }
        }
      }
      
      int FieldOffset = (int)(Field->getOffsetInBits() / 8);
      //If the name of the field is greater than 128 characters, we truncate it
      if(FieldName.size() > 128)
        FieldName = FieldName.substr(0, 125) + "...";
      
      #define DEBUG_TYPE "tt"
      LLVM_DEBUG(dbgs() << "Field Name: " << FieldName << " Format: " << FieldFormat << " Offset: " << FieldOffset << " Base Type Index: " << FieldBaseTypeIndex << "\n";);
      #undef DEBUG_TYPE
      fwrite(FieldName.c_str(), sizeof(char), 129, TypeTableFile);
      fwrite(&FieldFormat ,sizeof(int), 1, TypeTableFile);
      fwrite(&FieldOffset ,sizeof(int), 1, TypeTableFile);
      fwrite(&FieldBaseTypeIndex ,sizeof(int), 1, TypeTableFile);
    }
   }
   else{
     #define DEBUG_TYPE "tt"
     LLVM_DEBUG(dbgs() << "Format: " << Format << " Offset: " << Offset << " Base: " << BaseTypeIndex <<"\n";);
     #undef DEBUG_TYPE
     fwrite("", sizeof(char), 129, TypeTableFile);
     fwrite(&Format, sizeof(int), 1, TypeTableFile);
     fwrite(&Offset, sizeof(int), 1, TypeTableFile);
     fwrite(&BaseTypeIndex ,sizeof(int), 1, TypeTableFile);
   }
}

void MemoryMonitor::CreateTypeDescriptor(DIType* DIT, FILE* TypeTableFile, int* TypeTableSize){
  DINodeArray Fields;
  std::string TypeName = MakeTypeName(DIT);
  int Format = GetTypeFormat(DIT);
  int BaseTypeIndex = Format;
  int QuantFields;
  int Offset;
  
  //Void type
  if(!DIT){
    QuantFields = 1;
    Offset = 0;
  }
  else if(isa<DIBasicType>(DIT)){
    QuantFields = 1;
    Offset = 0;
  }
  else if(DIDerivedType* DIDT = dyn_cast<DIDerivedType>(DIT)){
    //Get the base type index for this type          
    for(auto &T : this->TypeIndexes){
      if(std::get<2>(T) == DIDT->getBaseType()){
        BaseTypeIndex = std::get<1>(T);
        break;
      }
    }
    QuantFields = 1;
    Offset = 0;
  }
  else if(DICompositeType* DICT = dyn_cast<DICompositeType>(DIT)){
    switch(DICT->getTag()){
      case dwarf::DW_TAG_array_type:{
        //Format = BaseTypeIndex = getTypeFormat(DICT->getBaseType());
        QuantFields = 1;
        auto Count = dyn_cast<DISubrange>(DICT->getElements()[0])->getCount();
        Offset = Count.get<ConstantInt*>()->getSExtValue(); 
        break;
      }
        
      case dwarf::DW_TAG_structure_type:
        Fields = DICT->getElements();
        QuantFields = (int)Fields.size();
        break;
        
      case dwarf::DW_TAG_union_type:
        QuantFields = 1;
        Offset = (DICT->getSizeInBits() / 8);
        break;
        
      case dwarf::DW_TAG_enumeration_type:
        QuantFields = 1;
        Offset = 0;
        break;
      
      default:
        break;
    }
  }
  else{
    #define DEBUG_TYPE "tt"
    LLVM_DEBUG(dbgs() << "Not creating " << TypeName <<".\n"; DIT->dump(););
    #undef DEBUG_TYPE 
    return;
  }
  
  WriteTypeDescriptor(TypeName.c_str(), QuantFields, Format, Offset, BaseTypeIndex, TypeTableFile, TypeTableSize, Fields);
}

std::pair<std::string, int> MemoryMonitor::CreateTypeTable(){  
  std::string TypeTableFileName = this->M->getSourceFileName();
  std::size_t ExtensionIndex = TypeTableFileName.rfind('.');
  TypeTableFileName.erase(ExtensionIndex);
  TypeTableFileName += "_TypeTable.bin";
  FILE* TypeTableFile = fopen(TypeTableFileName.c_str(), "wb");
  
  int TypeIndex = 0;
  
  //First, construct the type indexes
  for(DIType* DIT : this->DbgFinder.types()){
    
    if(!ShouldProcessType(DIT))
      continue;
    
    std::string TypeName = MakeTypeName(DIT);
    //If the name of the type is greater than 128 characters, we truncate it
    if(TypeName.size() > 128)
      TypeName = TypeName.substr(0, 125) + "...";
   
    this->TypeIndexes.push_back(std::make_tuple(TypeName, TypeIndex, DIT));
    TypeIndex++;
  }
  
 //Next, we construct the table
  int TypeTableSize = 0;
  
  for(auto &T : this->TypeIndexes)
    CreateTypeDescriptor(std::get<2>(T), TypeTableFile, &TypeTableSize);
    
  fclose(TypeTableFile);
  #define DEBUG_TYPE "tt"
  LLVM_DEBUG(
    dbgs() << "Type Table:\n";
    for(auto &T : this->TypeIndexes)
      dbgs() << std::get<1>(T) << " -> " << std::get<0>(T) <<"\n";
    dbgs() << "Type table size " << TypeTableSize << "\n";
  );
  #define DEBUG_TYPE "tt"
  return std::make_pair(TypeTableFileName, TypeTableSize);
}

void MemoryMonitor::OpenTypeTable(std::string ProgramName, int Size, IRBuilder<> Builder){
  std::vector<Type*>ArgsType;
  std::vector<Value*>Args;
  
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt32Ty());
  ArgsType.push_back(Builder.getInt32Ty());
  ArgsType.push_back(Builder.getInt32Ty());
  ArgsType.push_back(Builder.getInt32Ty());
  
  Args.push_back(Builder.CreateGlobalStringPtr(StringRef(ProgramName), "str"));
  Args.push_back(ConstantInt::get(Builder.getInt32Ty(), Size));
  ConstantInt* Heap = InsHeap ? ConstantInt::get(Builder.getInt32Ty(), 1) : ConstantInt::get(Builder.getInt32Ty(), 0);
  ConstantInt* Stack = InsStack ? ConstantInt::get(Builder.getInt32Ty(), 1) : ConstantInt::get(Builder.getInt32Ty(), 0);
  ConstantInt* PreciseMode = TrackPtr ? ConstantInt::get(Builder.getInt32Ty(), 1) : ConstantInt::get(Builder.getInt32Ty(), 0);
  Args.push_back(Heap);
  Args.push_back(Stack);
  Args.push_back(PreciseMode);
  InsertFunctionCall("WhiroOpenTypeTable", Builder.getVoidTy(), ArgsType, Args, Builder, false);
}

Value* MemoryMonitor::CreateFunctionCounter(Function* F, IRBuilder<> Builder){
  //The function counter is global.
  std::string CounterName = F->getName().str() + "_counter";
  GlobalVariable* Counter = new GlobalVariable(*(this->M), Builder.getInt32Ty(), false, GlobalValue::CommonLinkage, Constant::getNullValue(Builder.getInt32Ty()), CounterName);
  Counter->setName(CounterName);
  Counter->setDSOLocal(true);
  
  //The increment of that counter is inserted at the beginning of the function.
  Builder.SetInsertPoint(&F->getEntryBlock(), F->getEntryBlock().getFirstNonPHI()->getIterator());
  //To store a value in static memory, we first need to load it.
  Value* CounterLoad = Builder.CreateLoad(Counter, Counter->getName());
  Value* CounterInc = Builder.CreateNSWAdd(CounterLoad, ConstantInt::get(Builder.getInt32Ty(), 1));
  Builder.CreateStore(CounterInc, Counter);
  return CounterInc;
  
}

Value* MemoryMonitor::CastPointerToVoid(Value* Ptr, IRBuilder<> Builder){ 
  if(CastInst::isCastable(Ptr->getType(), Builder.getInt8PtrTy())){
    //If the type of the pointee value is castable to void*, we get the best cast instruction to do it
    Instruction::CastOps CastOP = CastInst::getCastOpcode(Ptr, false, Builder.getInt8PtrTy(), false);
    if(CastInst::castIsValid(CastOP, Ptr, Builder.getInt8PtrTy())){
      Value* PtrCast = CastInst::Create(CastOP, Ptr, Builder.getInt8PtrTy());
      Builder.Insert(dyn_cast<Instruction>(PtrCast));
      return PtrCast;
    }
  }
  
  //If the type is not 'castable' to void*, we return a null pointer
  return nullptr;
}

void MemoryMonitor::InsertHeapEntry(Value* HeapPtr, Type* AllocatedType, Value* Size, Value* ArrayStep, IRBuilder<> Builder){
  #define DEBUG_TYPE "memon"
  LLVM_DEBUG(
    Instruction* HeapPtrAsInst = dyn_cast<Instruction>(HeapPtr);
    dbgs() << "Inserting entry in the Heap Table. Allocation at line " << GetSourceLine(HeapPtrAsInst) << "\n";
  );
  #undef DEBUG_TYPE
  
  std::vector<Type*>ArgsType;
  std::vector<Value*>Args;
  
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt64Ty());
  ArgsType.push_back(Builder.getInt64Ty());
  ArgsType.push_back(Builder.getInt32Ty());
  
  if(AllocatedType->isPointerTy())
    AllocatedType = dyn_cast<PointerType>(AllocatedType)->getElementType();

  int TypeIndex = GetTypeIndex(AllocatedType);
  //If for some reason we could not determine the type index of this variable, we are not able to inspect it
  if(TypeIndex == 50000)
      return;
  
  //If this pointer is not void*, we need to cast it
  HeapPtr = (HeapPtr->getType() != Builder.getInt8PtrTy()) ? CastPointerToVoid(HeapPtr, Builder) : HeapPtr;
  Args.push_back(HeapPtr);
  Args.push_back(Size);
  Args.push_back(ArrayStep);
  Args.push_back(ConstantInt::get(Builder.getInt32Ty(), TypeIndex));
  InsertFunctionCall("WhiroInsertHeapEntry", Builder.getVoidTy(), ArgsType, Args, Builder, false);
}

void MemoryMonitor::UpdateHeapEntrySize(Value* HeapPtr, Value* NewSize, IRBuilder<> Builder){
  #define DEBUG_TYPE "memon"
  LLVM_DEBUG(
    Instruction* HeapPtrAsInst = dyn_cast<Instruction>(HeapPtr);
    dbgs() << "Updating size of heap entry. Reallocation at function " << HeapPtrAsInst->getFunction()->getName() << " at line " << GetSourceLine(HeapPtrAsInst) << ". File: " << HeapPtrAsInst->getFunction()->getSubprogram()->getFile()->getFilename() << "\n"; HeapPtrAsInst->dump();
  );
  #undef DEBUG_TYPE
  
  std::vector<Type*> ArgsType;
  std::vector<Value*> Args;
  
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt64Ty());
  
  //If this pointer is not void*, we need to cast it
  HeapPtr = (HeapPtr->getType() != Builder.getInt8PtrTy()) ? CastPointerToVoid(HeapPtr, Builder) : HeapPtr;
  Args.push_back(HeapPtr);
  Args.push_back(NewSize);
  InsertFunctionCall("WhiroUpdateHeapEntrySize", Builder.getVoidTy(), ArgsType, Args, Builder, false);  
}

void MemoryMonitor::DeleteHeapEntry(Value* HeapPtr, IRBuilder<> Builder){
  #define DEBUG_TYPE "memon"
  LLVM_DEBUG(
    Instruction* HeapPtrAsInst = dyn_cast<Instruction>(HeapPtr);
    dbgs() << "Setting Heap Table entry as unreachable. Deallocation at function " << HeapPtrAsInst->getFunction()->getName() << " at line " << GetSourceLine(HeapPtrAsInst) << ". File: " << HeapPtrAsInst->getFunction()->getSubprogram()->getFile()->getFilename() << "\n"; HeapPtrAsInst->dump();
  );
  #undef DEBUG_TYPE
  
  std::vector<Type*>ArgsType;
  std::vector<Value*>Args;
  
  ArgsType.push_back(Builder.getInt8PtrTy());
  Args.push_back(HeapPtr);
  InsertFunctionCall("WhiroDeleteHeapEntry", Builder.getVoidTy(), ArgsType, Args, Builder, false);
}

void MemoryMonitor::HandleHeapOperation(CallInst* HeapOp, IRBuilder<> Builder){
  //Once a heap operation is found, we need to update the Heap Table
  Builder.SetInsertPoint(HeapOp->getNextNode());
  
  //If this is a deallocation, we'll set the corresponding address as unreachable in the table
  if(HeapOp->getCalledFunction()->getName() == "free"){
    DeleteHeapEntry(HeapOp->getOperand(0), Builder);
    HeapOperations++;
    return;
  }
  
  //Otherwise, this operation is either an allocation or reallocation
  //Since the return of an allocation function is of type i8*, and the funciton that updates the heap table receives 
  //a void* type, the monitor uses HeapOp to avoid a needless cast. However, HeapType is used to get the actual type
  //of the newly allocated block
  Type* HeapType = nullptr;
  if(BitCastInst* BCI = dyn_cast<BitCastInst>(HeapOp->getNextNonDebugInstruction()))
    HeapType = BCI->getType();
  else
    HeapType = HeapOp->getType();
  
  //We need to get the amount of objects allocated
  Type* AllocatedType = dyn_cast<PointerType>(HeapType)->getElementType();
  const DataLayout DL = this->M->getDataLayout();
  uint64_t AllocatedTypeSize = DL.getTypeAllocSize(AllocatedType).getFixedSize();
  //If HeapOp is a call to 'realloc', the number of allocated bytes is the second argument in the call. If it is an allocation
  //function, such as 'malloc' or 'calloc', this number is the first argument. 
  Value* AllocatedBytes = (HeapOp->getCalledFunction()->getName() == "realloc") ? HeapOp->getOperand(1) : HeapOp->getOperand(0);
  
  //If we are allocating a constant amount of bytes, then we know the allocated amount at static time.
  //otherwise, we need to insert a instruction to compute such value at running time.
  Value* QuantAllocated = nullptr; 
  if(ConstantInt* C = dyn_cast<ConstantInt>(AllocatedBytes))
    QuantAllocated = ConstantInt::get(Builder.getInt64Ty(), (C->getSExtValue() / AllocatedTypeSize));
  else
    QuantAllocated = Builder.CreateUDiv(AllocatedBytes, ConstantInt::get(Builder.getInt64Ty(), AllocatedTypeSize));
  
  
  if(HeapOp->getCalledFunction()->getName() == "realloc")
    UpdateHeapEntrySize(HeapOp, QuantAllocated, Builder);
  else
    InsertHeapEntry(HeapOp, HeapType, QuantAllocated, QuantAllocated, Builder);   
 
  HeapOperations++;
}

std::string MemoryMonitor::GetFormatSpecifier(DIType* VarType){
  std::string FormatSpecifier = "";
  unsigned TypeEncoding;
  
  if(!VarType)
    return "%d\n";
  
  if(DIBasicType* DIBT = dyn_cast<DIBasicType>(VarType))
    TypeEncoding = DIBT->getEncoding();
  else if(DIDerivedType* DIDT = dyn_cast<DIDerivedType>(VarType))
    return GetFormatSpecifier(DIDT->getBaseType());
  else if(DICompositeType* DICT = dyn_cast<DICompositeType>(VarType)){
    if(DICT->getTag() == dwarf::DW_TAG_array_type || DICT->getTag() == dwarf::DW_TAG_enumeration_type) //We always print integer in case of enumerations or arrays (hashcode)
    return "%d\n";
  }
  
  //These values of encoding are based on the DWARF tags
  switch(TypeEncoding){
    case dwarf::DW_ATE_float:
      FormatSpecifier = VarType->getName() == "double" ? "%.2lf\n" : "%.2f\n";
      break;
      
    case dwarf::DW_ATE_signed:
      if(VarType->getName() == "short")
        FormatSpecifier = "%hi\n";
      else if(VarType->getName() == "long int")
        FormatSpecifier = "%ld\n";
      else if(VarType->getName() == "long long int")
        FormatSpecifier = "%lld\n";
      else
        FormatSpecifier = "%d\n";
      break;
      
    case dwarf::DW_ATE_signed_char:
      FormatSpecifier = "%c\n";
      break;
      
    case dwarf::DW_ATE_unsigned_char:
      FormatSpecifier = "%u\n";
      break;
      
    case dwarf::DW_ATE_unsigned:
      if(VarType->getName() == "unsigned short")
        FormatSpecifier = "%hu\n";
      else if(VarType->getName() == "long unsigned int")
        FormatSpecifier = "%lu\n";
      else if(VarType->getName() == "long long unsigned int")
        FormatSpecifier = "%llu\n";
      else
        FormatSpecifier = "%u\n";
      break;
      
    case dwarf::DW_ATE_address:
      FormatSpecifier = "%u\n";
      break;
      
    default:
      #define DEBUG_TYPE "memon"
      LLVM_DEBUG(VarType->dump(); dbgs() << "Unknow format type!\n");
      #undef DEBUG_TYPE
      break;
  }
  return FormatSpecifier;
}

int MemoryMonitor::GetTypeIndex(Type* T){
  std::string TypeName = MakeTypeName(T);
  
  //50000 is a special value that the memory monitor uses to tell Whiro it does not
  //know how to inspect a variable of this type
  if(TypeName == "Literal or opaque struct")
    return 50000;
  
  for(auto &T : this->TypeIndexes){
    if(std::get<0>(T) == TypeName)
      return std::get<1>(T);
  }
  
  for(auto &T : this->TypeIndexes){
    if(std::get<0>(T).find("unsigned ") != std::string::npos){
      if(std::get<0>(T).substr(std::get<0>(T).find("unsigned ") + 9) == TypeName)
        return std::get<1>(T);
    }
    
    if(TypeName == "long"){
      if(std::get<0>(T).find("long") != std::string::npos)
        return std::get<1>(T);
    }
  }
  
  #define DEBUG_TYPE "memon"
  LLVM_DEBUG(dbgs() << "Unknow type index! Type name: " << TypeName << "\n"; T->dump(););
  #undef DEBUG_TYPE
  return 50000;
    
}

void MemoryMonitor::InspectScalar(DIVariable* Scalar, Value* ValidDef, Value* OutputFilePtr, Value* CallCounter,  IRBuilder<> Builder, bool Scalarized){
  if(ValidDef->getType()->isPointerTy()){
    PointerType* PT = dyn_cast<PointerType>(ValidDef->getType());
    while(PT->getElementType()->isPointerTy()){
      ValidDef = Builder.CreateLoad(ValidDef);
      PT = dyn_cast<PointerType>(PT->getElementType());
    }
  }
  
  //We inspect scalar using the standard function 'fprintf'  
  //STDOUText is the string which is argument to the fprint function. It contains the name and scope
  //of the variable, plus its format specifier.
  std::string STDOUTText = Scalar->getName().str() + " ";
  STDOUTText += (isa<DIGlobalVariable>(Scalar)) ?  "(Static) " + Builder.GetInsertBlock()->getParent()->getName().str() : Scalar->getScope()->getName().str();
  STDOUTText +=  std::string(" %d"); //To print the call counter value
  
  if(Scalarized)
    STDOUTText += " (scalarized)";
  
  STDOUTText += std::string(" : ");
  std::string Format = GetFormatSpecifier(Scalar->getType());
  STDOUTText += Format;
  
  //if we're about to print a float variable, LLVM first converts it to a double.
  if(Format == "%.2f\n"){
    Instruction::CastOps Cast_OP = CastInst::getCastOpcode(ValidDef, false, Builder.getDoubleTy(), false);
    ValidDef = Builder.Insert(dyn_cast<Instruction>(CastInst::Create(Cast_OP, ValidDef, Builder.getDoubleTy())));
  }
  
  //Uncomment this line to ignore I/O printing time
  //return;
  std::vector<Type*> ArgsType;
  std::vector<Value*> Args;
  ArgsType.push_back(this->OutputFileType);
  ArgsType.push_back(Builder.getInt8PtrTy());
  
  Args.push_back(OutputFilePtr);
  Args.push_back(Builder.CreateGlobalStringPtr(StringRef(STDOUTText), "str"));
  Args.push_back(CallCounter);
  Args.push_back(ValidDef);
   
  InsertFunctionCall("fprintf", Builder.getInt32Ty(), ArgsType, Args, Builder, true);
}

void MemoryMonitor::InspectPointer(DIVariable* Pointer, Value* ValidDef, Value* OutputFilePtr, Value* CallCounter, IRBuilder<> Builder){
  //If the SSADef is a slot in the stack (AllocaInst), we need to read from that address
  if(dyn_cast<AllocaInst>(ValidDef) || isa<GlobalValue>(ValidDef))
    ValidDef = Builder.CreateLoad(ValidDef);
  
  //Get the type descriptor index to this SSADef
  Type* FinalType = ValidDef->getType();
  if(FinalType->isPointerTy())
    FinalType = dyn_cast<PointerType>(FinalType)->getElementType();        
  
  int TypeIndex = GetTypeIndex(FinalType);
  //If for some reason we could not determine the type index of this variable, we are not able to inspect it
  if(TypeIndex == 50000)
    return;
  
  //If this Value is not pointer is not void*, we need to cast it
  ValidDef = (ValidDef->getType() != Builder.getInt8PtrTy()) ? CastPointerToVoid(ValidDef, Builder) : ValidDef;
  
  std::string Scope = (isa<DIGlobalVariable>(Pointer)) ? "(Static) " + Builder.GetInsertBlock()->getParent()->getName().str() : Pointer->getScope()->getName().str();
  
  //Uncomment this line to ignore I/O printing time
  //return;
  std::vector<Type*> ArgsType;
  std::vector<Value*> Args;
      
  ArgsType.push_back(this->OutputFileType);
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt32Ty());
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt32Ty());  
  
  Args.push_back(OutputFilePtr);
  Args.push_back(ValidDef);
  Args.push_back(ConstantInt::get(Builder.getInt32Ty(), TypeIndex));
  Args.push_back(Builder.CreateGlobalStringPtr(Pointer->getName()));
  Args.push_back(Builder.CreateGlobalStringPtr(Scope));
  Args.push_back(CallCounter);
  
  InsertFunctionCall("WhiroInspectPointer", Builder.getVoidTy(), ArgsType, Args, Builder, false);
}

void MemoryMonitor::InspectUnion(DIVariable* Union, Value* ValidDef, DICompositeType* UnionType, Value* OutputFilePtr, Value* CallCounter, IRBuilder<> Builder){
  //The address pointed by the pointer is retrieved using the pointer.
  if(ValidDef->getType()->isPointerTy()){
    PointerType* PT = dyn_cast<PointerType>(ValidDef->getType());
    while(PT->getElementType()->isPointerTy()){
      ValidDef = Builder.CreateLoad(ValidDef);
      PT = dyn_cast<PointerType>(PT->getElementType());
    }
  }
    
  //If this pointer is not void*, we need to cast it
  ValidDef = (ValidDef->getType() != Builder.getInt8PtrTy()) ? CastPointerToVoid(ValidDef, Builder) : ValidDef;
  
  std::string Scope = (isa<DIGlobalVariable>(Union)) ? "(Static) " + Builder.GetInsertBlock()->getParent()->getName().str() : Union->getScope()->getName().str();  
  
   //Uncomment this line to ignore I/O printing time
  //return;
  std::vector<Type*> ArgsType;
  std::vector<Value*> Args;
        
  ArgsType.push_back(this->OutputFileType);
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt64Ty());
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt32Ty());
  
  Args.push_back(OutputFilePtr);
  Args.push_back(ValidDef);
  Args.push_back(ConstantInt::get(Builder.getInt64Ty(), (UnionType->getSizeInBits() / 8)));
  Args.push_back(Builder.CreateGlobalStringPtr(Union->getName()));
  Args.push_back(Builder.CreateGlobalStringPtr(Scope));
  Args.push_back(CallCounter);
  
  InsertFunctionCall("WhiroInspectUnion", Builder.getVoidTy(), ArgsType, Args, Builder, false);
}

void MemoryMonitor::InspectStruct(DIVariable* Struct, Value* ValidDef, Value* OutputFilePtr, Value* CallCounter,  IRBuilder<> Builder){
  //If the SSADef is a pointer to a struct, we need to load it to get the actual value
  if(ValidDef->getType()->isPointerTy()){
    PointerType* PT = dyn_cast<PointerType>(ValidDef->getType());
    while(PT->getElementType()->isPointerTy()){
      ValidDef = Builder.CreateLoad(ValidDef);
      PT = dyn_cast<PointerType>(PT->getElementType());
    }
  }
  
  //If we have scalarized struct value, Whiro prints it as a scalar
  if(!ValidDef->getType()->isPointerTy() || ValidDef->getType()->isSingleValueType()){
    InspectScalar(Struct, ValidDef, OutputFilePtr, CallCounter, Builder, true);
    return;
  }
  
  //Get the type descriptor index to this SSADef
  Type* FinalType = ValidDef->getType();
  if(FinalType->isPointerTy())
    FinalType = dyn_cast<PointerType>(FinalType)->getElementType();        
  
  int TypeIndex = GetTypeIndex(FinalType);
  //If for some reason we could not determine the type index of this variable, we are not able to inspect it
  if(TypeIndex == 50000)
    return;
  
  //If this pointer is not void*, we need to cast it
  ValidDef = (ValidDef->getType() != Builder.getInt8PtrTy()) ? CastPointerToVoid(ValidDef, Builder) : ValidDef;
  
  std::string Scope = (isa<DIGlobalVariable>(Struct)) ? "(Static) " + Builder.GetInsertBlock()->getParent()->getName().str() : Struct->getScope()->getName().str();
  
  //Uncomment this line to ignore I/O printing time
  //return;  
  std::vector<Type*> ArgsType;
  std::vector<Value*> Args;
        
  ArgsType.push_back(this->OutputFileType);
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt32Ty());
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt32Ty());
  
  Args.push_back(OutputFilePtr);
  Args.push_back(ValidDef);
  Args.push_back(ConstantInt::get(Builder.getInt32Ty(), TypeIndex));
  Args.push_back(Builder.CreateGlobalStringPtr(Struct->getName()));
  Args.push_back(Builder.CreateGlobalStringPtr(Scope));
  Args.push_back(CallCounter);
  
  InsertFunctionCall("WhiroInspectStruct", Builder.getVoidTy(), ArgsType, Args, Builder, false);
}

void MemoryMonitor::InspectArray(DIVariable* Array, Value* ValidDef, DICompositeType* ArrayType, Value* OutputFilePtr, llvm::Value* CallCounter, IRBuilder<> Builder){
  if(ValidDef->getType()->isPointerTy()){
    PointerType* PT = dyn_cast<PointerType>(ValidDef->getType());
    while(PT->getElementType()->isPointerTy()){
      ValidDef = Builder.CreateLoad(ValidDef);
      PT = dyn_cast<PointerType>(PT->getElementType());
    }
  }
  //If we have scalarized array value, Whiro prints it as a scalar
  if(!ValidDef->getType()->isPointerTy() || ValidDef->getType()->isSingleValueType()){
    InspectScalar(Array, ValidDef, OutputFilePtr, CallCounter, Builder, true);
    return;
  }
  
  DINodeArray Subranges = ArrayType->getElements();
  //Get total of elements. If it is constant, just access it. Otherwise, insert instructions to compute it
  //based on the number of elements in every dimension of the array
  Value* TotalElem = nullptr;
  if(Array->getType()->getSizeInBits() > 0)   
    TotalElem = ConstantInt::get(Builder.getInt64Ty(), (Array->getType()->getSizeInBits() / dyn_cast<DICompositeType>(Array->getType())->getBaseType()->getSizeInBits()));
  else{
    //If the size of the array is not constant, we insert instructions to compute such size
    TotalElem = ConstantInt::get(Builder.getInt64Ty(), 1);
    for(unsigned i = 0; i < Subranges.size(); i++){
      Value* DimSize = nullptr;
      auto Count = dyn_cast<DISubrange>(Subranges[i])->getCount();
      //If the number of elements in dimension 'i' is given by a constant, it is straightforward to get it.
      //Otherwise, this number is given by a variable and we need to get a final SSA definition for it.
      if(Count.is<ConstantInt*>())
        DimSize = Count.get<ConstantInt*>();
      else if(Count.is<DIVariable*>()){
        DIVariable* DV = Count.get<DIVariable*>();
        DimSize = GetValidDef(this->CurrentStackMap[DV->getName().str()].second, Builder.GetInsertBlock(), nullptr, Builder);
      }
      TotalElem = Builder.CreateMul(TotalElem, DimSize);
    }
  }
  
  //Get the array step, that is, the size of the outermost dimension
  auto Count = dyn_cast<DISubrange>(Subranges[Subranges.size() - 1])->getCount();
  Value* Step = nullptr;
  if(Count.is<ConstantInt*>())
    Step = Count.get<ConstantInt*>();
  else if(Count.is<DIVariable*>()){
    DIVariable* DV = Count.get<DIVariable*>();
    Step = GetValidDef(this->CurrentStackMap[DV->getName().str()].second, Builder.GetInsertBlock(), nullptr, Builder);
  }
  
  //If this Value is not pointer is not void*, we need to cast it
  ValidDef = (ValidDef->getType() != Builder.getInt8PtrTy()) ? CastPointerToVoid(ValidDef, Builder) : ValidDef;
    
  int Format = GetTypeFormat(ArrayType->getBaseType());
  
  //Uncomment this line to ignore I/O printing time
  //return;
  std::vector<Type*> ArgsType;
  std::vector<Value*> Args;
  
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt64Ty());
  ArgsType.push_back(Builder.getInt64Ty());
  ArgsType.push_back(Builder.getInt32Ty());
  
  Args.push_back(ValidDef);
  Args.push_back(TotalElem);
  Args.push_back(Step);
  Args.push_back(ConstantInt::get(Builder.getInt32Ty(), Format));
  
  InspectScalar(Array, InsertFunctionCall("WhiroComputeHashcode", Builder.getInt32Ty(), ArgsType, Args, Builder, false), OutputFilePtr, CallCounter, Builder, false);  
}

void MemoryMonitor::InspectVariable(DIVariable* Var, DIType* VarType, Value* ValidDef, Value* OutputFilePtr, Value* CallCounter, IRBuilder<> Builder){
  //If this is the first inspection point created in this function, update the number of 
  //variables inspected, only for local variables. The static are already counted
  if(this->FirstInspection && isa<DILocalVariable>(Var))
    TotalVars++;
  
  if(isa<GlobalVariable>(ValidDef))
    ValidDef = Builder.CreateLoad(ValidDef);
  
  //Whiro analyzes the type of a variable to decide how it will inspect it
  //Inspecting scalar variables  
  if(isa<DIBasicType>(VarType)){
    InspectScalar(Var, ValidDef, OutputFilePtr, CallCounter, Builder, false);
    return;
  }
  
  //Inspecting pointers
   if(DIDerivedType* DIDT = dyn_cast<DIDerivedType>(VarType)){
     if(DIDT->getTag() == dwarf::DW_TAG_pointer_type){
       if(DIDT->getBaseType()){
         if(isa<DISubroutineType>(DIDT->getBaseType())) //Whiro does not inspect pointer to functions
           return;
       }
       InspectPointer(Var, ValidDef, OutputFilePtr, CallCounter, Builder);
       return;
     }
   }
   
   //Inspecting variables of composite types
   if(DICompositeType* DICT = dyn_cast<DICompositeType>(VarType)){
     switch(DICT->getTag()){
       case dwarf::DW_TAG_union_type:
         InspectUnion(Var, ValidDef, DICT, OutputFilePtr, CallCounter, Builder);
         return;
         
       case dwarf::DW_TAG_structure_type:
         InspectStruct(Var, ValidDef, OutputFilePtr, CallCounter, Builder);
         return;
         
       case dwarf::DW_TAG_array_type:
         if(isa<DIBasicType>(DICT->getBaseType()))
           InspectArray(Var, ValidDef, DICT, OutputFilePtr, CallCounter, Builder);
         else{
           //Inspect non-scalar array
           errs() << "Do not inspect non-scalar arrays\n";
         }
         return;
         
       case dwarf::DW_TAG_enumeration_type: //Whiro reports enumerations as simple integers
         InspectScalar(Var, ValidDef, OutputFilePtr, CallCounter, Builder, false);
         return;
     }
       
   } 
}

void MemoryMonitor::InspectEntireHeap(Value* OutputFilePtr, StringRef FuncName, Value* CallCounter, IRBuilder<> Builder){
  std::vector<Type*> ArgsType;
  std::vector<Value*> Args;
        
  ArgsType.push_back(this->OutputFileType);
  ArgsType.push_back(Builder.getInt8PtrTy());
  ArgsType.push_back(Builder.getInt32Ty());
    
  Args.push_back(OutputFilePtr);
  Args.push_back(Builder.CreateGlobalStringPtr(FuncName, "str"));
  Args.push_back(CallCounter);
  
  InsertFunctionCall("WhiroInspectEntireHeap", Builder.getVoidTy(), ArgsType, Args, Builder, false);
}

Type* MemoryMonitor::GetLargestType(std::vector<DbgVariableIntrinsic*>Trace){
  Type* LargestType = dyn_cast<DbgValueInst>(Trace.front())->getValue()->getType();
  for(auto &Def : Trace){
    Type* DefType = dyn_cast<DbgValueInst>(Def)->getValue()->getType();
    if(this->M->getDataLayout().getTypeAllocSize(DefType) > this->M->getDataLayout().getTypeAllocSize(LargestType))
      LargestType = DefType;
  }
  
  return LargestType;
}

AllocaInst* MemoryMonitor::ShadowInStack(std::vector<DbgVariableIntrinsic*>Trace, std::map<std::string, AllocaInst*>*ShadowVars, IRBuilder<> Builder){
  #define DEBUG_TYPE "memon"
  LLVM_DEBUG(dbgs() << "Could not extend variable. Shadowing in the stack\n";);
  #undef DEBUG_TYPE
  bool IsDiffVar = false;
  
  //If the variable already has a shadow slot in the stack, we just return it
  std::map<std::string, AllocaInst*>::iterator it = ShadowVars->find(Trace.front()->getVariable()->getName().str());
  if(it != ShadowVars->end()){
    return it->second;
  }
  
  //Create a stack slot at the beginning of the function. This slot must hold a value with the
  //largest type found in a trace of a variable. Initialize it with null
  Builder.SetInsertPoint(&(dyn_cast<Instruction>(Trace.front())->getFunction()->getEntryBlock()), dyn_cast<Instruction>(Trace.front())->getFunction()->getEntryBlock().begin());  
  Type* VarLargestType = GetLargestType(Trace);
  AllocaInst* VarAddr = Builder.CreateAlloca(VarLargestType, this->M->getDataLayout().getAllocaAddrSpace(), nullptr, Trace.front()->getVariable()->getName());
  VarAddr->setAlignment(Align(this->M->getDataLayout().getABITypeAlignment(VarLargestType)));
  Builder.CreateStore(Constant::getNullValue(VarLargestType), VarAddr);
  
  //Traverse the trace of a variable storing each SSA definition into the newly created stack slot
  for(auto &Def : Trace){
    Value* DefValue = dyn_cast<DbgValueInst>(Def)->getValue();
    
    //The monitor needs to decide where to put the store instruction. The following criteria is adopeted
    // 1. if the definition is NOT in the same block as the debug instruction, we create the store right after the definition
    // 2. otherwise, we create the store right after the debug instruction. 
    // 3. if the definition is a phi instruction, we insert the store at the first non-phi instruction, so we do not broke the IR Module
    // 4. if the definition is a constant, we insert the store after the debug instruction
    if(Instruction* I = dyn_cast<Instruction>(DefValue)){
      if(I->getParent() != Def->getParent()){
        if(isa<PHINode>(I))
          Builder.SetInsertPoint(I->getParent(), I->getParent()->getFirstNonPHI()->getIterator());
        else
          Builder.SetInsertPoint(I->getParent(), I->getNextNode()->getIterator());
      }
      else
        Builder.SetInsertPoint(Def->getParent(), dyn_cast<Instruction>(Def)->getNextNode()->getIterator());
    }
    else
      Builder.SetInsertPoint(Def->getParent(), dyn_cast<Instruction>(Def)->getNextNode()->getIterator());
    
    //If this definition has a type other than the largest, we need to cast it before storing
    if(DefValue->getType() != VarLargestType){
      Instruction::CastOps Cast_OP = CastInst::getCastOpcode(DefValue, false, VarLargestType, false);
      if(CastInst::castIsValid(Cast_OP, DefValue, VarLargestType)){
        Value* Cast = CastInst::Create(Cast_OP, DefValue, VarLargestType);
        Builder.Insert(dyn_cast<Instruction>(Cast));
        Builder.CreateStore(Cast, VarAddr);
        IsDiffVar = true;
      }
      
    }
    else
      Builder.CreateStore(DefValue, VarAddr);
  }
  
  //Save the slot in the shadow map
  ShadowVars->insert(std::make_pair(Trace.front()->getVariable()->getName().str(), VarAddr));
  Var2Stack++;
  if(IsDiffVar)
    DiffVar++;
  return VarAddr;
}

PHINode* MemoryMonitor::ExtendLiveRange(std::vector<DbgVariableIntrinsic*>Trace, BasicBlock* InsBlock, IRBuilder<> Builder){
  #define DEBUG_TYPE "memon"
  LLVM_DEBUG(dbgs() << "Extending Live Range\n";);
  #undef DEBUG_TYPE
  
  bool IsDiffVar = false;
  for(auto &Def : Trace){
    Value* DefValue = dyn_cast<DbgValueInst>(Def)->getValue();
    if(Instruction* I = dyn_cast<Instruction>(DefValue)){
      if(I->getParent() != Def->getParent())
        return nullptr;
    }
  }
  
  Type* VarLargestType = GetLargestType(Trace);
  PHINode* PN = PHINode::Create(VarLargestType, 2);
  
  //Traverse the trace of the variable looking for SSA definitions that reach the inspection block
  for(auto &Def : Trace){
    Value* DefValue = dyn_cast<DbgValueInst>(Def)->getValue();
    auto it = std::find(pred_begin(InsBlock), pred_end(InsBlock), Def->getParent());
    if(it != pred_end(InsBlock)){
      //If the debug instruction is in a predecessor block, but the definition itself is not, we cannot use extend it.
      if(Instruction* I = dyn_cast<Instruction>(DefValue)){
        if(I->getParent() != Def->getParent())
          return nullptr;
      }
      //If this definition has a type other than the largest, we need to cast it before using in the phi instruction
      if(DefValue->getType() != VarLargestType){
        Instruction::CastOps Cast_OP = CastInst::getCastOpcode(DefValue, false, VarLargestType, false);
        if(CastInst::castIsValid(Cast_OP, DefValue, VarLargestType)){
          if(Instruction* I = dyn_cast<Instruction>(DefValue)){
            if(isa<PHINode>(I))
              Builder.SetInsertPoint(I->getParent(), I->getParent()->getFirstNonPHI()->getIterator());
            else
              Builder.SetInsertPoint(I->getParent(), I->getNextNode()->getIterator());
          }
          Value* Cast = CastInst::Create(Cast_OP, DefValue, VarLargestType);
          Builder.Insert(dyn_cast<Instruction>(Cast));
          DefValue = Cast;
          IsDiffVar = true;
        }
      }
      
      
      if(PN->getBasicBlockIndex(Def->getParent()) == -1) //If we do not have a value for this block, add it
        PN->addIncoming(DefValue, Def->getParent());
      else  // otherwise we update it. this will work since the last use from a block will be later in the list
        PN->setIncomingValueForBlock(Def->getParent(), DefValue);
    }
  }
  
  if(IsDiffVar)
    DiffVar++;
  
  //A phi node must have an entry for each predecessor of its block. We create null entries for the predecessor that
  //do not have a definition of the variable being inspected
  if(PN->getNumIncomingValues() > 0){
    for(auto PredIt =pred_begin(InsBlock); PredIt != pred_end(InsBlock); ++PredIt){
      BasicBlock* Pred = *PredIt;
      if(PN->getBasicBlockIndex(Pred) == -1)
        PN->addIncoming(Constant::getNullValue(PN->getType()), Pred);
    }
    Builder.SetInsertPoint(InsBlock, InsBlock->getFirstNonPHI()->getIterator());
    Builder.Insert(PN);
    ExtendedVars++;
    return PN;
  }
  
  //No definitions reach the insertion block. We do not extend in this case
  else 
    return nullptr;
}

Value* MemoryMonitor::GetValidDef(std::vector<DbgVariableIntrinsic*>Trace, BasicBlock* InsBlock, std::map<std::string, AllocaInst*>*ShadowVars, IRBuilder<> Builder){
  Value* ValidDef = nullptr;
  DominatorTree* DT = new DominatorTree(*(InsBlock->getParent()));
  
  //Traverse the trace of the variable to select the definition that will be used to report that variable. We adopt the following criteria:
  //1. if a definition is a stack address, we use it
  //2. otherwise, we select a definition that resides in the block being inspected (the return block of the function)
  //3. if 1 and 2 fail, we use a definition that dominates the inspection block. 
  //In cases 2 and 3, we use the last definition (if there are more than one in a block) or most immediate dominator.
  for(auto &Def : Trace){
    Value* DefValue = Def->isAddressOfVariable() ? dyn_cast<DbgDeclareInst>(Def)->getAddress() : dyn_cast<DbgValueInst>(Def)->getValue();
    if(isa<AllocaInst>(DefValue)){
      ValidDef = DefValue;
      break;
    }
    
    /*if(Instruction* I = dyn_cast<Instruction>(DefValue)){
      if(I->getParent() != Def->getParent()){
        Final_Def = nullptr;
        break;
      }
    }*/
    else if(isa<ReturnInst>(Def->getParent()->getTerminator())){
        ValidDef = DefValue;
    }
    else if(DT->dominates(Def, InsBlock)){
      ValidDef = DefValue;
    }
  }
  
  //If no definition can be safely used to report the variable, the memory monitor tries to extend the live
  //range of said variable with phi instructions. If this does not work as well, the monitor will shadow
  //the variable in the stack of the function being instrumented
  if(!ValidDef){
    ValidDef = ExtendLiveRange(Trace, InsBlock, Builder);
    if(!ValidDef)
      ValidDef = ShadowInStack(Trace, ShadowVars, Builder);
  }
  
  return ValidDef;
}

void MemoryMonitor::CreateInspectionPoint(Value* OutputFilePtr, Value* CallCounter, std::map<std::string, AllocaInst*>*ShadowVars, IRBuilder<> Builder){
  #define DEBUG_TYPE "memon"
  LLVM_DEBUG(dbgs() << "Creating inspection point\n";);
  #undef DEBUG_TYPE
  
  //Inspect all the local variables function plus the static variables according to the Memory Filter    
  //First the local variables
  for(auto &v : this->CurrentStackMap){
    //If the user choose not to inspect heap and stack, there is no need to report any local variable
    if(this->MemFilter){
      if(!InsHeap && !InsStack)
        break;
    }
    DIVariable* Var = v.second.first;
    
    //Whiro does not inspect variables inserted by the compiler or variables that have subroutine type
    if(isa<DILocalVariable>(Var)){
      if(dyn_cast<DILocalVariable>(Var)->isArtificial())
        continue;
    }
    if(isa<DISubroutineType>(Var->getType()))
      continue;
    
    //Whiro reasons on the type system of the source code. This means that some variables may have qualified types, like typedef or const.
    //However, for reporting purposes Whiro will rely on its base type. Because of that, we cannot always use Var->getType() and need to
    //get the base type manually
    DIType* VarType = Var->getType();
    if(DIDerivedType* DIDT = dyn_cast<DIDerivedType>(VarType)){
      while(DIDT->getTag() == dwarf::DW_TAG_typedef || DIDT->getTag() == dwarf::DW_TAG_const_type){
        VarType = DIDT->getBaseType();
        if(isa<DIDerivedType>(VarType))
          DIDT = dyn_cast<DIDerivedType>(VarType);
        else
          break;
      }
    }
    
    //Local variables other than pointer must be stored in the stack. If the user chooses not to inspect the stack, Whiro skips all
    //these variables
    if(this->MemFilter && !InsStack && (VarType->getTag() != dwarf::DW_TAG_pointer_type))
      continue;
    
    #define DEBUG_TYPE "memon"
    LLVM_DEBUG(dbgs() << "Inspecting variable " << Var->getName() <<"\n";);
    #undef DEBUG_TYPE
    
    InspectVariable(Var, VarType, GetValidDef(v.second.second, Builder.GetInsertBlock(), ShadowVars, Builder), OutputFilePtr, CallCounter, Builder);
  }
  
  //Inspect the static variables
  if(MemFilter && !InsStatic){
    this->FirstInspection = false;
    return;
  }
  
  for(auto &g : this->StaticMap){
    DIVariable* Var = g.second.first;
    
    if(isa<DISubroutineType>(Var->getType()))
      continue;
    
    //Whiro reasons on the type system of the source code. This means that some variables may have qualified types, like typedef or const.
    //However, for reporting purposes Whiro will rely on its base type. Because of that, we cannot always use Var->getType() and need to
    //get the base type manually
    DIType* VarType = Var->getType();
    if(DIDerivedType* DIDT = dyn_cast<DIDerivedType>(VarType)){
      while(DIDT->getTag() == dwarf::DW_TAG_typedef || DIDT->getTag() == dwarf::DW_TAG_const_type){
        VarType = DIDT->getBaseType();
        if(isa<DIDerivedType>(VarType))
          DIDT = dyn_cast<DIDerivedType>(VarType);
        else
          break;
      }
    }
    
    #define DEBUG_TYPE "memon"
    LLVM_DEBUG(dbgs() << "Inspecting variable " << Var->getName() << " (Static)\n";);
    #undef DEBUG_TYPE
    
    InspectVariable(Var, VarType, g.second.second, OutputFilePtr, CallCounter, Builder);
  }
  
  this->FirstInspection = false;
}

void MemoryMonitor::UpdateStackMap(DbgVariableIntrinsic* DVI){
  //Whiro inspects only variables in the local scope of the function
  if(DVI->getVariable()->getScope()->getName() != DVI->getFunction()->getName())
    return;
  
  //We do not track debug instructions with null or undef values
  if(!DVI->isAddressOfVariable()){
    Value* Definition = dyn_cast<DbgValueInst>(DVI)->getValue();
    if(Definition){
      if(dyn_cast<UndefValue>(Definition))
        return;
    
      if(Constant* C = dyn_cast<Constant>(Definition)){
        if(C->isNullValue())
          return;
      }
    }
    else
      return;
  }
  else if(!dyn_cast<DbgDeclareInst>(DVI)->getAddress())
      return;
  
  //If we already have an entry for that variable in the stack map, we just append the definition in its trace.
  //Otherwise we create a new entry in the map
  llvm::DIVariable* Var = DVI->getVariable();
  std::map<std::string, std::pair<DIVariable*, std::vector<DbgVariableIntrinsic*>>>::iterator it = this->CurrentStackMap.find(Var->getName().str());
  if(it != this->CurrentStackMap.end()){
    it->second.second.push_back(DVI);
  }
  else{        
    std::vector<DbgVariableIntrinsic*>Trace;
    Trace.push_back(DVI);
    this->CurrentStackMap.insert(std::make_pair(Var->getName().str(), std::make_pair(Var, Trace)));
  }
}

Instruction* MemoryMonitor::GetInsertionPoint(Function& F){
  //The inspection points are createad right before the return of the function. We find return basic block
  //of the function and set it as the insertion point of the IRBuilder.
  //We use -mergereturn to ensure that every function will have only one exit point
  llvm::BasicBlock* InsBlock = nullptr;
  for(llvm::BasicBlock &BB : F){
    if(llvm::isa<llvm::ReturnInst>(BB.getTerminator())){
      InsBlock = &BB;
      break;
    }
  }
  
  if(!InsBlock) 
    return nullptr;

  if(InsBlock->getTerminator()->getPrevNode()){
    Instruction* InsPoint = InsBlock->getTerminator();
    while(isa<IntrinsicInst>(InsPoint))
      InsPoint = InsPoint->getPrevNode();
    
    return InsPoint;
  }
  else
    return InsBlock->getTerminator();
}

void MemoryMonitor::InstrumentFunction(Function& F){
  #define DEBUG_TYPE "memon"
  LLVM_DEBUG(dbgs() << "Instrumenting function " << F.getName() << ". File: " << F.getSubprogram()->getFile()->getFilename() << "\n";);
  #undef DEBUG_TYPE
  
  InstFunc++;
  //Create and set the IRBuilder which instruments the program.
  llvm::IRBuilder<> Builder(&F.getEntryBlock(), F.getEntryBlock().getFirstNonPHI()->getIterator());
  
  //Create the function call counter
  llvm::Value* CallCounter = (F.getName().equals("main")) ? ConstantInt::get(Builder.getInt32Ty(), 1) : CreateFunctionCounter(&F, Builder);
  
  //A map containing all the variables gathered in the function, mapped by their names plus a map containing all the variables shadowed in the stack
  std::map<std::string, std::pair<DIVariable*, std::vector<DbgVariableIntrinsic*>>>StackMap;
  this->CurrentStackMap = StackMap;
  std::map<std::string, AllocaInst*>ShadowVars;
  
  for(Instruction &I : instructions(F)){
    if(DbgVariableIntrinsic* DVI = dyn_cast<DbgVariableIntrinsic>(&I)){
      if(DVI->getVariable()->getScope() == F.getSubprogram())
        UpdateStackMap(DVI);
    }
    
    if(CallInst* CI = dyn_cast<CallInst>(&I)){
      Function* CalledFunction = CI->getCalledFunction();
      if(!CalledFunction || CI->isIndirectCall())
        continue;
      //If we found a call that allocates memory in the heap, we handle this allocation.
      if(CalledFunction->isDeclaration()){
        StringRef FuncName = CalledFunction->getName();
        if(FuncName.equals("malloc") || FuncName.equals("realloc") || FuncName.equals("calloc") || FuncName.equals("free"))
          HandleHeapOperation(CI, Builder);
        //Whiro creates inspection points right before halting functions execute
        else if(FuncName.equals("exit")){
          if(OnlyMain){
           if(F.getName() == "main"){
             Builder.SetInsertPoint(&I);
             //Create a reference to the output file.
             Value* OutputFilePtr = Builder.CreateLoad(this->OutputFile);
             CreateInspectionPoint(OutputFilePtr, CallCounter, &ShadowVars, Builder);
             CloseOutputFile(OutputFilePtr, Builder);
           }
          }
          else{
            Builder.SetInsertPoint(&I);
            //Create a reference to the output file.
            Value* OutputFilePtr = Builder.CreateLoad(this->OutputFile);
            CreateInspectionPoint(OutputFilePtr, CallCounter, &ShadowVars, Builder);
            CloseOutputFile(OutputFilePtr, Builder);
          }
        }
      }
    }
  }
  
  //Select the program point to create the inspection point for this function
  Instruction* InsPoint = GetInsertionPoint(F);
  if(!InsPoint){
    errs() << "Whiro could not find the return block of this function. Skipping it.\n";
    this->CurrentStackMap.clear(); 
    return;
  }
  Builder.SetInsertPoint(InsPoint);
  //Create a reference to the output file.
  Value* OutputFilePtr = Builder.CreateLoad(this->OutputFile);
  
  //Creating inspection point  
  if(OnlyMain){
    if(F.getName() == "main")
      CreateInspectionPoint(OutputFilePtr, CallCounter, &ShadowVars, Builder);
  }
  else
    CreateInspectionPoint(OutputFilePtr, CallCounter, &ShadowVars, Builder);
  
  //If this is the main routine, close the output file. Notice that in case of calls to halting functions,
  //we also close the file right before the halting
  if(F.getName() == "main")
    CloseOutputFile(OutputFilePtr, Builder);
  
  if(InsFullHeap)
    InspectEntireHeap(OutputFilePtr, F.getName(), CallCounter, Builder);
  
  this->CurrentStackMap.clear();
}

void MemoryMonitor::InstrumentOnlyHeap(Function& F){
  //Create and set the IRBuilder which instruments the program.
  llvm::IRBuilder<> Builder(&F.getEntryBlock(), F.getEntryBlock().getFirstNonPHI()->getIterator());
  for(Instruction &I : instructions(F)){
    if(CallInst* CI = dyn_cast<CallInst>(&I)){
      Function* CalledFunction = CI->getCalledFunction();
      if(!CalledFunction || CI->isIndirectCall())
        continue;
      //If we found a call that allocates memory in the heap, we handle this allocation.
      if(CalledFunction->isDeclaration()){
        StringRef FuncName = CalledFunction->getName();
        if(FuncName.equals("malloc") || FuncName.equals("realloc") || FuncName.equals("calloc") || FuncName.equals("free"))
          HandleHeapOperation(CI, Builder);
      }
    }
  }
}

bool MemoryMonitor::runOnModule(Module &M){
  #define DEBUG_TYPE "memon"
  LLVM_DEBUG(dbgs() << "Instrumeting program " << M.getSourceFileName() <<".\n";);
  #undef DEBUG_TYPE
  
  //Set some Memory Monitor's components
  this->M = &M;
  this->DbgFinder.processModule(M);
  this->MemFilter = InsHeap || InsStack || InsStatic;
  this->FirstInspection = true;
  if(InsHeap) TrackPtr = true;
  
  //Initialize the statistis to zero. This way they will appear in the -stats output even
  //if they are not incremented during the execution of this pass
  TotalVars = 0;
  ExtendedVars = 0;
  Var2Stack = 0;
  HeapOperations = 0;
  InstFunc = 0;
  
  //Create an IRBuilder to insert calls to the functions that Initialize the dynamic components of
  //the Memory Monitor
  Function* Main = M.getFunction(StringRef("main"));
  if(Main == nullptr){
    errs() << "Program has no main function!\n Aborting instrumentation...\n";
    exit(1);
  }
  
  BasicBlock* InsBlock = &Main->getEntryBlock();
  BasicBlock::iterator InsPoint = Main->getEntryBlock().begin();
  
  while(isa<AllocaInst>(InsPoint)) ++InsPoint;
  IRBuilder<> Builder(InsBlock, InsPoint);
  
  //Collect the global variables before injecting anything in the program
  if(!this->MemFilter || (this->MemFilter && InsStatic)){
    for(GlobalVariable &G : M.globals()){
      if(!G.isConstant()){
        if(!G.hasInitializer()){
          continue;
        }
        DIVariable* Var = nullptr;
        MDNode* GlobalMD = G.getMetadata(StringRef("dbg"));
        if(!GlobalMD)
          continue;
        if(DIGlobalVariableExpression* DIGE = dyn_cast<DIGlobalVariableExpression>(GlobalMD))
          Var = DIGE->getVariable();
        else
          continue;
        
        this->StaticMap.insert(std::make_pair(Var->getName(), std::make_pair(Var, &G)));
        TotalVars++;
      }
    }
  }
  
  //Create the output file.
  OpenOutputFile(Builder);
  
  //Open the Type Table
  std::pair<std::string, int> TypeTableMD = CreateTypeTable();
  OpenTypeTable(TypeTableMD.first, TypeTableMD.second, Builder);
  
  //Instrument the functions in the program
  for(Function &F : M){
    if(OnlyMain && F.getName () != "main"){
      //If the user chooses to keep tracking of pointers, we need to build the heap table regardless of the
      //function inspection granularity.
      if(TrackPtr || InsFullHeap){
        if(!F.isDeclaration())
          InstrumentOnlyHeap(F);
      }
      continue;
    }
    if(!F.isDeclaration())
      InstrumentFunction(F);
    
    this->FirstInspection = true;
  }
  
  #define DEBUG_TYPE "memon"
  LLVM_DEBUG(dbgs() << "\nInstrumentation done!\n --------------------------------------------------\n\n";);
  #undef DEBUG_TYPE
  return true;
}

char MemoryMonitor::ID = 0;
static RegisterPass<MemoryMonitor> X("memoryMonitor", "Memory Monitor Pass");
