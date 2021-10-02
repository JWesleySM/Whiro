#ifndef MEMORYMONITOR_H
#define MEMORYMONITOR_H

//! A class to insert verification code in benchmarks and create the inspection points.
/*!
	This class is responsible to insert all the necessary instrumentation into a program to insert the verification code. It tracks the values of a variable through the LLVM IR, and create the inspection points just before the return of every function. All the variables are printed to an extern file which will be created by the instrumented program.
*/

class MemoryMonitor : public llvm::ModulePass {
	public:
		static char ID; // Pass identification, replacement for typeid
    
		//! Class constructor. To initialize the pass identifier.
   	MemoryMonitor() : ModulePass(ID) {}

		//! Class destructor.
		~ MemoryMonitor() {}
    
		//! This method performs the work of the LLVM pass.
		/*! It processes an LLVM IR file of a program, which contains all the information attached to that
		@param M is an LLVM Module, which is the IR form of the program.
		@return true if the program is modified, or false otherwise.
		*/
    bool runOnModule(llvm::Module &M) override;

	private:
	  //-- Fields --//
	  
	  // A pointer to the Module being processed.
		llvm::Module *M;
	  // A pointer to the output file.
	  llvm::Value* OutputFile;
	  // A pointer to type of the output file.
	  llvm::PointerType* OutputFileType;
	  // A boolean indicating whether the monitor should filter memory regions
	  bool MemFilter;
	  // A class to access all the metadata nodes in a Module.
		llvm::DebugInfoFinder DbgFinder;
		// A vector that associates at compilation time debug types and its names with its indexes in the Type Table
		std::vector<std::tuple<std::string, int, llvm::DIType*>> TypeIndexes;
	  // A pointer to the stack map of the function currently being instrumented
	  std::map<std::string, std::pair<llvm::DIVariable*, std::vector<llvm::DbgVariableIntrinsic*>>>CurrentStackMap;
	  // A map holding information about the static variables found in the program
	  std::map<std::string, std::pair<llvm::DIVariable*, llvm::Value*>> StaticMap;
	  // A boolean indicating whether a inspection point in the current function was already created. Used to get the 
	  // right number of variables inspected in a function
	  bool FirstInspection;
		
		//-- Methods --//
		
		
		/** 
		 * This method returns the source code location relative to an Instruction as a string, 
		 * if it is possible to retrieve it.
		 * @param I is the LLVM instruction to recover the code location
		 */
		std::string getSourceLine(llvm::Instruction* I);
		
		/** 
		 * This method inserts in the program a call instruction to any function
		 * @param FuncName is the name of the callee function
		 * @param ReturnType is the callee's return type
		 * @param ArgsType is a vector containing the types of the arguments of the callee
		 * @param Args is a vector containing the values to be used as actual arguments in the function call
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 * @param IsVarArg tells whether the function to be called has a variable number of arguments
		 * @return the call instruction
		 */
		llvm::CallInst* insertFunctionCall(std::string FuncName, llvm::Type* ReturnType, std::vector<llvm::Type*>ArgsType, std::vector<llvm::Value*>Args, llvm::IRBuilder<> Builder, bool IsVarArg);
		
		/** 
		 * This method inserts the instructions to open the output file. This method creates the output
		 * pointer file as a global variable and calls the standard C function "fopen" to open the
     * file.
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */ 
		void openOutputFile(llvm::IRBuilder<> Builder);
		
		/**
		 * This method inserts the instructions to close the output file. It calls the standard C
		 * C function 'flose' to close the file.
		 * @param OutputFilePtr is a pointer to the output file
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		void closeOutputFile(llvm::Value* OutputFilePtr, llvm::IRBuilder<> Builder);
		
		/**
		 * This method states whether the monitor should create a type descriptor for a given debug type.
		 * Subroutine types and members/pointers to members to struct fields do not have descriptors
		 * @param DIT is an LLVM debug type
		 */
		bool shouldProcessType(llvm::DIType *DIT);
		
		/**
		 * This method returns an integer corresponding to the format of a debug type, for reporting purposes.
		 * @param DIT is an LLVM debug type
		 * @return the type format of DIT
		 */
		int getTypeFormat(llvm::DIType* T);
		
		/**
		 * This method returns the complete name of a debug type.
		 * @param DIT is an LLVM debug type
		 * @return the complete name of DIT
		 */
		std::string makeTypeName(llvm::DIType* DIT);
		
		/**
		 * This method returns the complete name of an IR type.
		 * @param T is an LLVM IR type
		 * @return the complete name of T
		 */
		std::string makeTypeName(llvm::Type* T);
		
		/**
		 * This method serializes an entry for a given type in the Type Table binary file.
		 * @param Name is the name of the type
		 * @param QuantFields is the number of fields defined in the type
		 * @param Format is the format of the type
		 * @param Offset is the offset of the type
		 * @param BaseTypeIndex is the index of the type descriptor of base type, if it exitsts
		 * @param TypeTableFile is a pointer to the Type Table binary file
		 * @param TypeTableSize holds the size of the Type Table. It is incremented in this method
		 * @param Fields is an array containing the description of every field in case of the type is a
		 * product type (C structs)
		 */
		void writeTypeDescriptor(const char* TypeName, int QuantFields, int Format, int Offset, int BaseTypeIndex, FILE* TypeTableFile, int* TypeTableSize, llvm::DINodeArray Fields);
		
		/**
		 * This method creates a type descriptor for a given debug type.
		 * @param DIT is an LLVM debug type
		 * @param TypeTableFile is a pointer to the Type Table binary file
		 * @param TypeTableSize holds the size of the Type Table.
		 */
		void createTypeDescriptor(llvm::DIType* DIT, FILE* TypeTableFile, int* TypeTableSize);
		
		/**
		 * This method creates the Type Table file.
		 * @return a tuple containing the name and the size of the Type Table
		 */
		std::pair<std::string, int> createTypeTable();
		
		/**
		 * This method inserts the instructions to open the Type Table file.
		 * @param ProgramName is the name of the program being instrumented
		 * @param Size is the size of the type table (number of types in the source code)
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		void openTypeTable(std::string ProgramName, int Size, llvm::IRBuilder<> Builder);
		
		/**
		 * This method creates a counter for a function in the program and inserts the code to increment
		 * it at the beginning of the function.
		 * @param F is a pointer to the function being instrumented
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		llvm::Value* createFunctionCounter(llvm::Function* F, llvm::IRBuilder<> Builder);
		
		/**
		 * This method receives a pointer to any valid type in LLVM IR and casts it to a pointer
		 * to void. In LLVM IR, void is represented by a integer of 8 bits. The function uses the
		 * CastInst LLVM API to decide which one is the best instruction to perform the cast.
		 * @param Ptr is the pointer to be casted
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 * @return the instruction which casts the pointer
		 */
		llvm::Value* castPointerToVoid(llvm::Value* Ptr, llvm::IRBuilder<> Builder);
		
		/**
		 * This method injects code to insert an entry in the Heap Table.
		 * @param HeapPtr is a pointer to an heap address
		 * @param AllocatedType is the type of the newly allocated heap block
		 * @param Size is the size of the allocated heap block
		 * @param ArrayStep is the increment the pointer Ptr, so Whiro can visit all data allocated in that block
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		void insertHeapEntry(llvm::Value* HeapPtr, llvm::Type* AllocatedType, llvm::Value* Size, llvm::Value* ArrayStep, llvm::IRBuilder<> Builder);
		
		/**
		 * This method injects code to update the size of an entry in the Heap Table.
		 * @param HeapPtr is a pointer to an heap address
		 * @param NewSize is the size of the heap entry
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		void updateHeapEntrySize(llvm::Value* HeapPtr, llvm::Value* NewSize, llvm::IRBuilder<> Builder);
		
		/**
		 * This method injects code to 'delete' an entry in the Heap Table. Whiro actually marks it as unreachable
		 * data
		 * @param HeapPtr is a pointer to an heap address
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		void deleteHeapEntry(llvm::Value* HeapPtr, llvm::IRBuilder<> Builder);
		
		/**
		 * This method inserts code to update the Heap Table according to the type of a heap operation.
		 * The standard functions 'malloc', 'realloc', 'calloc', and 'free' are manipulated by this method.
		 * @param HeapOP is an LLVM instruction that operates in the heap
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		void handleHeapOperation(llvm::CallInst* HeapOp, llvm::IRBuilder<> Builder);
		
		/**
		 * This method get the format specifier to be used to print a variable with that type in 
		 * the IR.
		 * @param VarType is a LLVM debug type, which represents the type of a variable 
		 * in the source code.
		 * @return the a string containing the format specifier used to print a variable of 
		 * the given type
			*/
		std::string getFormatSpecifier(llvm::DIType* VarType);
		
		/**
		 * This method returns the index to the descriptor of an IR type
		 * @param T is an LLVM IR type
		 * @return the index to access the descriptor of T in the Type Table
			*/
		int getTypeIndex(llvm::Type* T);
		
		/**
		 * This method inserts code to inspect variables of scalar types. We use the standard fprint function to 
		 * report the values of this kind of variable
		 * @param Scalar is an LLVM scalar debug variable
		 * @param ValidDef is the SSA definition associated with 'Scalar' that is valid at the inspection point
		 * @param OutputFilePtr is a pointer to the output file
		 * @param CallCounter is the LLVM value corresponding to the function counter 
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 * @param Scalarized indicates whether this variable is an array or struct value scalarized by some optimization
		 */
		void inspectScalar(llvm::DIVariable* Scalar, llvm::Value* ValidDef, llvm::Value* OutputFilePtr, llvm::Value* CallCounter,  llvm::IRBuilder<> Builder, bool Scalarized);
		
		/**
		 * This method inserts code to inspect pointer variables.
		 * @param Pointer is an LLVM pointer debug variable
		 * @param ValidDef is the SSA definition associated with 'Pointer' that is valid at the inspection point
		 * @param OutputFilePtr is a pointer to the output file
		 * @param CallCounter is the LLVM value corresponding to the function counter 
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		void inspectPointer(llvm::DIVariable* Pointer, llvm::Value* ValidDef, llvm::Value* OutputFilePtr, llvm::Value* CallCounter, llvm::IRBuilder<> Builder);
		
		/**
		 * This method inserts code to inspect union variables.
		 * @param Pointer is an LLVM union debug variable
		 * @param ValidDef is the SSA definition associated with 'Union' that is valid at the inspection point
		 * @param OutputFilePtr is a pointer to the output file
		 * @param CallCounter is the LLVM value corresponding to the function counter 
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		void inspectUnion(llvm::DIVariable* Union, llvm::Value* ValidDef, llvm::DICompositeType* UnionType, llvm::Value* OutputFilePtr, llvm::Value* CallCounter, llvm::IRBuilder<> Builder);
		
		/**
		 * This method inserts code to inspect struct variables.
		 * @param Pointer is an LLVM struct debug variable
		 * @param ValidDef is the SSA definition associated with 'Struct' that is valid at the inspection point
		 * @param UnionType is the actual type of Union. We need this because the union variable might have a 
		 * qualified type, and we are interested in the base type 
		 * @param OutputFilePtr is a pointer to the output file
		 * @param CallCounter is the LLVM value corresponding to the function counter 
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		void inspectStruct(llvm::DIVariable* Struct, llvm::Value* ValidDef, llvm::Value* OutputFilePtr, llvm::Value* CallCounter, llvm::IRBuilder<> Builder);
		
		/**
		 * This method inserts code to inspect array variables. We insert code to compute a hashcode and print said
		 * value as a scalar.
		 * @param Pointer is an LLVM array debug variable
		 * @param ValidDef is the SSA definition associated with 'Scalar' that is valid at the inspection point
		 * @param ArrayType is the actual type of Array. We need this because the array variable might have a 
		 * qualified type, and we are interested in the base type 
		 * @param OutputFilePtr is a pointer to the output file
		 * @param CallCounter is the LLVM value corresponding to the function counter 
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		void inspectArray(llvm::DIVariable* Array, llvm::Value* ValidDef, llvm::DICompositeType* ArrayType, llvm::Value* OutputFilePtr, llvm::Value* CallCounter, llvm::IRBuilder<> Builder);
		
		/**
		 * This method inspect and decide how to report the value of a variable based on its type.
		 * @param Var is an LLVM  debug variable
		 * @param VarType is the LLVM debug type holding the actual type of 'Var' (ignoring qualified types)
		 * @param ValidDef is the SSA definition associated with 'Scalar' that is valid at the inspection point
		 * @param OutputFilePtr is a pointer to the output file
		 * @param CallCounter is the LLVM value corresponding to the function counter 
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		void inspectVariable(llvm::DIVariable* Var, llvm::DIType* VarType, llvm::Value* ValidDef, llvm::Value* OutputFilePtr, llvm::Value* CallCounter, llvm::IRBuilder<> Builder);	
		 
		 /**
		 * This method inserts code to call the function that will report all heap-allocated data in the program at
		 * an inspection point
		 * @param OutputFilePtr is a pointer to the output file
		 * @param FuncName is the name of the function currently being instrumented
		 * @param CallCounter is the LLVM value corresponding to the function counter 
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */  
	  void inspectEntireHeap(llvm::Value* OutputFilePtr, llvm::StringRef FuncName, llvm::Value* CallCounter, llvm::IRBuilder<> Builder);
	  
	  /**
		 * This method returns the largest IR type found among the SSA definitions in the trace of a variable. By largest, 
		 * we consider the type with the greatest allocation size.
		 * @param Trace is the trace with the definitions of a variable
		 * @return the largest type in Trace
		 */
		llvm::Type* getLargestType(std::vector<llvm::DbgVariableIntrinsic*>Trace);
		
		/**
		 * This method shadows a variable in the stack of the function currently being instrumented. It allocates
		 * a slot in the stack and store every definition from the trace of said variable. It also updates the
		 * ShadowVars map.
		 * @param Trace is the trace with the definitions of a variable
		 * @param ShadowVars is a map of variables that were shadowed in the stack
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 * @return the shadow slot allocated
		 */
		llvm::AllocaInst* shadowInStack(std::vector<llvm::DbgVariableIntrinsic*>Trace, std::map<std::string, llvm::AllocaInst*>*ShadowVars, llvm::IRBuilder<> Builder);
		
		/**
		 * This method extends the live range of a variable by inserting a Phi-instruction with the definitions
		 * that reach the block in which the inspection point is defined.
		 * @param Trace is the trace with the definitions of a variable
		 * @param InsBlock is the block with the inspection point
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 * @return the phi instruction created
		 */
		llvm::PHINode* extendLiveRange(std::vector<llvm::DbgVariableIntrinsic*>Trace, llvm::BasicBlock* InsBlock, llvm::IRBuilder<> Builder);
		
		/**
		 * This method selects which definition of a variable is valid at an inspection point
		 * @param Trace is the trace with the definitions of a variable
		 * @param InsBlock is the block with the inspection point
		 * @param ShadowVars is a map of variables that were shadowed in the stack
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 * @return the valid definition of the variable
		 */
		llvm::Value* getValidDef(std::vector<llvm::DbgVariableIntrinsic*>Trace, llvm::BasicBlock* InsBlock, std::map<std::string, llvm::AllocaInst*>*ShadowVars, llvm::IRBuilder<> Builder);
		
		/**
		 * This method creates an inspection point in a function. It decides which variables will be inspected based on
		 * the usage mode defined by the user.
		 * @param ValidDef is the SSA definition associated with 'Scalar' that is valid at the inspection point
		 * @param OutputFilePtr is a pointer to the output file
		 * @param CallCounter is the LLVM value corresponding to the function counter 
		 * @param ShadowVars is a map of variables that were shadowed in the stack
		 * @param Builder is the LLVM IR builder, to insert instructions and get types
		 */
		void createInspectionPoint(llvm::Value* OutputFilePtr, llvm::Value* CallCounter, std::map<std::string, llvm::AllocaInst*>*ShadowVars, llvm::IRBuilder<> Builder);
		
		/**
		 * This method handles a LLVM debug intrinsic. Said intrinsic describes either the address of a local
		 * variable or a new value assigned to thar variable. Using these informations the monitor constructs
		 * the Trace of a variable.
		 * @param DVI is a LLVM debug variable intrinsic (llvm.dbg.declare or llvm.dbg.value)
		 */
		void updateStackMap(llvm::DbgVariableIntrinsic* DVI);
		
		/**
		 * This method selects the point where inspection point will be created. In the current version, Whiro
		 * inspect the program state at the return point of functions. Before this pass runs, the bytecode of
		 * the program is transformed with -mergereturn, to ensure that every function will have one single
		 * exit point.
		 * @param F is the function being instrumented
		 * @return an instruction from the return block of F. Inspection points will be created right before it
		 */
		llvm::Instruction* getInsertionPoint(llvm::Function& F);
		
		/**
		 * This method inserts all the instrumentation in a function. It creates the call counter, inserts code to
		 * update the Heap Table, if the heap is to be inspected, and it injects the code to report the program
		 * state at inspection points
		 * @param F is the function to be instrumented
		 */
		void instrumentFunction(llvm::Function& F);
		
		/**
		 * This method instrumentats a function, but only inserts code to update the Heap Table. It is useful when
		 * the user chooses to inspect only the main routine of the program, but it wants to use the Precise mode
		 * @param F is the function to be instrumented
		 */
		void instrumentOnlyHeap(llvm::Function& F);
		
    
 };

#endif	
