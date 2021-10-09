#ifndef HEAP_TABLE_H
#define HEAP_TABLE_H
/**
 * This structure describes a heap-allocated data.
 * TypeIndex is the index to access the type descriptor of that data
 * Size is the number of elements allocated
 * ArrayStep is the increment the pointer to that data, so Whiro 
 * can visit all data allocated in that block
 */
typedef struct {
  int TypeIndex;
	int Size;
	int ArrayStep; 
} HeapData;

/**
 * This structure describes an entry from the heap table
 * Key is the address of that entry
 * Data describes the type of the data stored in this entry
 * Visited is a flag indicating whether that block was visited when traversing the heap graph
 * Free is a flag indicante whether this block holds valid data
 * hh is the member to make this entry "hashable"
 */
typedef struct {
  void* Key;
  HeapData* Data;
	int Visited;
	int Free;
  UT_hash_handle hh;
} HeapEntry;

/**
 * This function is responsible to insert a new entry in the heap table H. This entry
 * corresponds to the heap block addressed by Block. It first checks whether there exists
 * an entry holding that address. In a positive case, it just updates its size and type
 * index. Otherwise, the function allocates a new entry and sets it up
 * @param Block is the heap address
 * @param Size is the number of elements allocated
 * @param ArrayStep is the increment the pointer to Block, so Whiro can visit all data
 * allocated in that block
 * @param TypeIndex is the type to access the type descriptor of that data
 */
void WhiroInsertHeapEntry(void* Block, int ArraySize, int ArrayPtrStep, int TypeIndex);


/**
 * This function updates the size of a H entry when there is a heap reallocation in the
 * original program.
 * @param Block is the heap address
 * @param NewSize is the new size of the entry
 */
void WhiroUpdateHeapEntrySize(void* Block, int NewSize);

/**
 * This function sets the heap entry addressed by Block to unreachable, if such entry
 * exists in the table
 * @param Block is the heap address
 */
void WhiroDeleteHeapEntry(void* Block);

/**
 * This function takes an entry from the heap table and report the data contained in
 * it at some inspection point. It prints it together with the calling context, formed by
 * the pointer name in the source code, the name of the function in which the inspection
 * point is defined, and the current value of the call counter of that function. This 
 * function also it sets Entry as visited.
 * @param OutputFile is a pointer to the output file of the program
 * @param Entry is the heap table entry to be inspected
 * @param PtrName is the name of the pointer that points to that heap address in the
 * source code
 * @param FuncName is the name of the function from the source code that it is being
 * inspected
 * @param CallCounter is the current value of FuncName
 * @param FollowPtr indicates whether Whiro should follow the chain of reachability of
 * this data in case it points to somewhere in memory
*/
void WhiroInspectHeapData(FILE* OutputFile, HeapEntry* Entry, char* PtrName, char* FuncName, int CallCounter, int FollowPtr);

/**
 * This function reports an entry from the heap table that has a size greater than 1. 
 * It reports it as an array. 
 * @param OutputFile is a pointer to the output file of the program
 * @param Entry is the heap table entry to be inspected
 * @param PtrName is the name of the pointer that points to that heap address in the
 * source code
 * @param FuncName is the name of the function from the source code that it is being
 * inspected
 * @param CallCounter is the current value of FuncName
 */
void WhiroInspectHeapArray(FILE* OutputFile, HeapEntry* Entry, char* PtrName, char* FuncName, int CallCounter);

/**
 * This function reports all the contents of the Heap Table, that is, all the heap-
 * allocated data manipulated by the program.
 * @param OutputFile is a pointer to the output file of the program
 * @param FuncName is the name of the function from the source code that it is being
 * inspected
 * @param CallCounter is the current value of FuncName
 */
void WhiroInspectEntireHeap(FILE* OutputFile, char* FuncName, int CallCounter);

/**
 * This function sets the entire heap table as univisited. Whiro uses it to report aliases.
 */
void WhiroSetAllHeapUnivisited();

#endif
