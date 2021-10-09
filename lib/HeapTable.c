#include "../include/Whiro.h"

HeapEntry* HeapTable = NULL;
extern TypeDescriptor* TypeTable;

void WhiroPrintTable(){
  HeapEntry *Entry;
	for(Entry = HeapTable; Entry != NULL; Entry = Entry->hh.next)
	  printf("%p ", Entry->Key);
	  
	printf("\n");
}

void WhiroInsertHeapEntry(void* Block, int Size, int ArrayStep, int TypeIndex){
  HeapEntry* Entry;
  //Insert a new entry in the Heap Table
  //If we do not find an entry in the table for this pointers, we create one.
  HASH_FIND(hh, HeapTable, &Block, sizeof(void*), Entry);
  if(Entry == NULL){
    Entry = (HeapEntry*)malloc(sizeof(HeapEntry));
    Entry->Key = Block;
  }
  
  Entry->Data = (HeapData*)malloc(sizeof(HeapData));
  Entry->Data->TypeIndex = TypeIndex;
  Entry->Data->Size = Size;
  Entry->Data->ArrayStep = ArrayStep;
  Entry->Visited = Entry->Free = 0;
  HASH_ADD(hh, HeapTable, Key, sizeof(void*), Entry);
}

void WhiroUpdateHeapEntrySize(void* Block, int NewSize){
  //Update the size of an entry in the Heap Table
  HeapEntry* Entry;
	HASH_FIND(hh, HeapTable, &Block, sizeof(void*), Entry);
	if(Entry){
	  Entry->Data->Size = NewSize;
	  Entry->Data->ArrayStep = NewSize;
	}
}

void WhiroDeleteHeapEntry(void* Block){
  //Set a heap entry as unreachable data
	HeapEntry* Entry;
	HASH_FIND(hh, HeapTable, &Block, sizeof(void*), Entry);
	if(Entry){
		Entry->Free = 1;
		free(Entry->Data);
		Entry->Data = NULL;
	}
}

void WhiroInspectHeapData(FILE* OutputFile, HeapEntry* Entry, char* PtrName, char* FuncName, int CallCounter, int FollowPtr){
  //If this entry was already visited, do not print it again.
	//Otherwise, set is as visited.
	if(Entry->Visited == 1)
		return;
	else
		Entry->Visited = 1;
	
	//If this is unreachable data, Whiro does not inspect it. 
	if(Entry->Free == 1){
	  fprintf(OutputFile, "%s %s %d : freed\n", PtrName, FuncName, CallCounter);
	  return;
	}
	
	if(Entry->Data->Size > 1){
	  WhiroInspectHeapArray(OutputFile, Entry, PtrName, FuncName, CallCounter);
		return;
	}
	else
	  WhiroInspectData(OutputFile, Entry->Key, &TypeTable[Entry->Data->TypeIndex], PtrName, FuncName, CallCounter);
}

void WhiroInspectHeapArray(FILE* OutputFile, HeapEntry* Entry, char* PtrName, char* FuncName, int CallCounter){
  //Inspect an array allocated in the heap
  TypeDescriptor Type = TypeTable[Entry->Data->TypeIndex];
  if(WhiroIsScalarType(Type.Fields[0].Format)){
    //If it is a scalar, compute a hashcode value
	  int Hashcode = WhiroComputeHashcode(Entry->Key, Entry->Data->Size, Entry->Data->ArrayStep, Type.Fields[0].Format);
	  fprintf(OutputFile, "%s %s %d: %d\n", PtrName, FuncName, CallCounter, Hashcode);
	}
  else if(Type.Fields[0].Format == 13){
    //If it is an array of pointers, inspect each position
    for(int i = 0; i < Entry->Data->Size; i++){
			void** Next = (Entry->Key + i);
			char* DataName = WhiroGetArrayIndexAsString(i);
			char* DataFullName = (char*)malloc(strlen(PtrName) + strlen(DataName) + 1);
			strcpy(DataFullName, PtrName);
			strcat(DataFullName, DataName);
			WhiroInspectPointer(OutputFile, *Next, Type.Fields[0].BaseTypeIndex, DataFullName, FuncName, CallCounter);
			free(DataFullName);
		}
   }
   else{
     //Inspect non-scalar array
     printf("Inspect non-scalar array <<<<<<<<<<<>>>>>>>>>>>\n");
   } 
}

void WhiroInspectEntireHeap(FILE* OutputFile, char* FuncName, int CallCounter){
  //Report all the heap-allocated data
  HeapEntry* Entry;
  for(Entry = HeapTable; Entry != NULL; Entry = Entry->hh.next){
    if(Entry->Free == 0)
      WhiroInspectHeapData(OutputFile, Entry, "Heap Data", FuncName, CallCounter, 0);
  }
  
  WhiroSetAllHeapUnivisited();
}

void WhiroSetAllHeapUnivisited(){
	HeapEntry *Entry;
	for(Entry = HeapTable; Entry != NULL; Entry = Entry->hh.next)
		Entry->Visited = 0;
}

