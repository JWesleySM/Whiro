#include "../include/Whiro.h"

extern TypeDescriptor* TypeTable;
extern HeapEntry* HeapTable;
//Usage mode settings
int MemFilter, InsHeap, InsStack, Precise;

//Executable and Linkable Format (ELF) program segments
extern char etext, edata, end; 

void WhiroInspectData(FILE* OutputFile, void* Data, TypeDescriptor* DataType, char* Name, char* FuncName, int CallCounter){
  char* DataNameFull = NULL;
  for(int i = 0; i < DataType->QuantFields; i++){
    //We use the DataNameFull to append the names of the data while reporting
    DataNameFull = (char*)malloc(strlen(Name) + strlen(DataType->Fields[i].Name) + 2);
    strcpy(DataNameFull, Name);
    if(DataType->Fields[i].Name[0] != '\0')
   		strcat(DataNameFull, "-");
		strcat(DataNameFull, DataType->Fields[i].Name);
		
	  switch(DataType->Fields[i].Format){
		  case 1:
		    fprintf(OutputFile, "%s %s %d : %.2lf\n", DataNameFull, FuncName, CallCounter, *(double*)(Data + DataType->Fields[i].Offset));
				break;
				
			case 2:
			  fprintf(OutputFile, "%s %s %d : %.2f\n", DataNameFull, FuncName, CallCounter, *(float*)(Data + DataType->Fields[i].Offset));
				break;
				
			case 3:
			  fprintf(OutputFile, "%s %s %d : %hi\n", DataNameFull, FuncName, CallCounter, *(short*)(Data + DataType->Fields[i].Offset));
				break;
				
			case 4:
			  fprintf(OutputFile, "%s %s %d : %ld\n", DataNameFull, FuncName, CallCounter, *(long*)(Data + DataType->Fields[i].Offset));
				break;
				
			case 5:
			  fprintf(OutputFile, "%s %s %d : %lld\n", DataNameFull, FuncName, CallCounter, *(long long*)(Data + DataType->Fields[i].Offset));
				break;
				
			case 6:
			  fprintf(OutputFile, "%s %s %d : %d\n", DataNameFull, FuncName, CallCounter, *(int*)(Data + DataType->Fields[i].Offset));
				break;
			
			case 7:
			  //We check if the character is printable. If it is not, we print is as '@'.
			  //That is the same approach Linux does when printing binary files
			  if(isprint(*(char*)(Data + DataType->Fields[i].Offset)))
					  fprintf(OutputFile, "%s %s %d : %c\n", DataNameFull, FuncName, CallCounter, *(char*)(Data + DataType->Fields[i].Offset));
					else
					  fprintf(OutputFile, "%s %s %d : @\n", DataNameFull, FuncName, CallCounter);
				break;
			
			case 8:
			  //We check if the character is printable. If it is not, we print is as '@'.
			  //That is the same approach Linux does when printing binary files
				if(isprint(*(unsigned char*)(Data + DataType->Fields[i].Offset)))
					fprintf(OutputFile, "%s %s %d : %u\n", DataNameFull, FuncName, CallCounter, *(unsigned char*)(Data + DataType->Fields[i].Offset));
			  else
				  fprintf(OutputFile, "%s %s %d : @\n", DataNameFull, FuncName, CallCounter);
				break;
				
			case 9:
			  fprintf(OutputFile, "%s %s %d : %hu\n", DataNameFull, FuncName, CallCounter,  *(unsigned short*)(Data + DataType->Fields[i].Offset));
				break;
				
			case 10:
			  fprintf(OutputFile, "%s %s %d : %lu\n", DataNameFull, FuncName, CallCounter, *(unsigned long*)(Data + DataType->Fields[i].Offset));
				break;
				
			case 11:
			  fprintf(OutputFile, "%s %s %d : %llu\n", DataNameFull, FuncName, CallCounter, *(unsigned long long*)(Data + DataType->Fields[i].Offset));
				break;
				
			case 12:
			  fprintf(OutputFile, "%s %s %d : %u\n", DataNameFull, FuncName, CallCounter, *(unsigned int*)(Data + DataType->Fields[i].Offset));
				break;
					
			case 13:{
				if(Precise){
				  void** Next = (Data + DataType->Fields[i].Offset);
				  WhiroTrackPointer(OutputFile, *Next, DataType->Fields[i].BaseTypeIndex, DataNameFull, FuncName, CallCounter);
				}
				else
				  fprintf(OutputFile, "%s %s %d : pointer to %s\n", Name, FuncName, CallCounter, TypeTable[DataType->Fields[i].BaseTypeIndex].Name);
				break;
			}
			
			case 14:
				fprintf(OutputFile, "%s %s %d : void\n", DataNameFull, FuncName, CallCounter);
				break;
				
			case 15:{
			  TypeDescriptor ElementType = TypeTable[DataType->Fields[i].BaseTypeIndex];
			  int Hashcode = WhiroComputeHashcode((Data + DataType->Fields[i].Offset), ElementType.Fields[0].Offset, ElementType.Fields[0].Offset, ElementType.Fields[0].Format);
			  fprintf(OutputFile, "%s %s %d : %d\n", DataNameFull, FuncName, CallCounter, Hashcode);
			  break;
			}
			
			case 16:
			  WhiroInspectUnion(OutputFile, (char*)Data, DataType->Fields[i].Offset, Name, FuncName, CallCounter);
			  break;
			  
			case 17:{
			  TypeDescriptor StructType = TypeTable[DataType->Fields[i].BaseTypeIndex];
			  WhiroInspectData(OutputFile, (Data + DataType->Fields[i].Offset), &StructType, Name, FuncName, CallCounter);
			  break;
			}
			  
			case 18:
			  fprintf(OutputFile, "%s %s %d : non-inspectable value\n", DataNameFull, FuncName, CallCounter);
			  break;
				
			default:
				printf("Unkown Format %d (Inspect Data)\n", DataType->Fields[i].Format);
				break;
			}
			
    free(DataNameFull);
	  DataNameFull = NULL;
	}
}

void WhiroInspectPointer(FILE* OutputFile, void* Ptr, int TypeIndex, char* Name, char* FuncName, int CallCounter){
  if(Precise){
    WhiroTrackPointer(OutputFile, Ptr, TypeIndex, Name, FuncName, CallCounter);
    //After we traverse the table, set all of its nodes as unvisited.
    WhiroSetAllHeapUnivisited();
    return;	
  }
  else{
    fprintf(OutputFile, "%s %s %d : pointer to %s\n", Name, FuncName, CallCounter, TypeTable[TypeIndex].Name);
  }
}

void WhiroTrackPointer(FILE* OutputFile, void* Ptr, int TypeIndex, char* Name, char* FuncName, int CallCounter){
  HeapEntry* Entry;
	HASH_FIND(hh, HeapTable, &Ptr, sizeof(void*), Entry);
	//If this pointer is pointing to the heap, we inspect if the user chose to inspect the heap
	if(Entry){
	  if(MemFilter && !InsHeap)
	    return;

	  WhiroInspectHeapData(OutputFile, Entry, Name, FuncName, CallCounter, 1);
  }
  else if(Ptr){
    //If this pointer is not null and is not pointing to a heap address, then we assume it is pointing to the stack
    if(MemFilter && !InsStack)
      return;
    
    //When tracking data, Whiro might jump to whatever address it recognizes in such a pointer—possibly
    //incurring into a segmentation fault. To deal with this problem, Whiro only prints pointers
    // within specific Executable and Linkable Format (ELF) program segments
    if((char*)Ptr < &etext)
      return;
    
	  WhiroInspectData(OutputFile, Ptr, &TypeTable[TypeIndex], Name, FuncName, CallCounter);
	}
	else
	  //Print the pointer as NULL if it is equal to zero
	  fprintf(OutputFile, "%s %s %d : NULL\n", Name, FuncName, CallCounter);	
}

void WhiroInspectUnion(FILE* OutputFile, char* Union, size_t Size, char* Name, char* FuncName, int CallCounter){
  fprintf(OutputFile, "%s %s %d : ", Name, FuncName, CallCounter);
  for(size_t i = 0; i < Size; i++){
    fprintf(OutputFile, "%d", (int)Union[i]);  
  }
  fprintf(OutputFile, "\n");
}

void WhiroInspectStruct(FILE* OutputFile, void* Struct, int TypeIndex, char* Name, char* FuncName, int CallCounter){
	WhiroInspectData(OutputFile, Struct, &TypeTable[TypeIndex], Name, FuncName, CallCounter);	
}

