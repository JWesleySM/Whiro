#include "../include/Whiro.h"

TypeDescriptor* TypeTable = NULL;
extern int InsHeap, InsStack, MemFilter, Precise;

void WhiroOpenTypeTable(const char* ProgramName, int TableSize, int InsHeapArg, int InsStackArg, int PreciseArg){
  //Set the usage mode settings  
  InsHeap = InsHeapArg;
  InsStack = InsStackArg;
  MemFilter = InsHeap || InsStack;
  Precise = PreciseArg;
  
  //Allocate and read the Type Table
  TypeTable = (TypeDescriptor*)malloc(sizeof(struct TypeDescriptor) * TableSize);
  FILE* TypeTableFile = fopen(ProgramName, "rb");
  if(TypeTableFile == NULL){
    printf("Error opening Type Table file %s\n", ProgramName);
    exit(1);
  }
  	
  for(int i = 0; i < TableSize; i++){
    fread(&TypeTable[i].Name, sizeof(char), MAX_NAME_LENGTH + 1, TypeTableFile);
    fread(&TypeTable[i].QuantFields, sizeof(int), 1, TypeTableFile);
    TypeTable[i].Fields = (Field*)malloc(sizeof(struct Field) * TypeTable[i].QuantFields);
    for(int j = 0; j < TypeTable[i].QuantFields; j++){
      fread(TypeTable[i].Fields[j].Name, sizeof(char), MAX_NAME_LENGTH + 1, TypeTableFile);
      fread(&TypeTable[i].Fields[j].Format, sizeof(int), 1, TypeTableFile);
      fread(&TypeTable[i].Fields[j].Offset, sizeof(int), 1, TypeTableFile);
      fread(&TypeTable[i].Fields[j].BaseTypeIndex, sizeof(int), 1, TypeTableFile);
   }
  }	 
 
}

int WhiroIsScalarType(int Format){
  return (Format > 0 && Format <= 12) ? 1 : 0;
}

