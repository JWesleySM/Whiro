#include "../include/Whiro.h"

char* WhiroGetArrayIndexAsString(int Index){
  int QuantDigits = 0, Num = Index;
  while(Num !=0){
    QuantDigits++;
    Num /= 10;
  }
  char IndexString[QuantDigits + 3];
  snprintf(IndexString, QuantDigits + 3, "[%d]", Index);
  return IndexString;
}

int WhiroComputeHashcode1D(void* Array, int Size, int Format){
  //Traverse an array of 1 dimension and compute a hashcode value
  int Hashcode = 1;
  for(int i = 0; i < Size; i++){
    switch(Format){
      case 1:
        Hashcode = 31 * Hashcode + (((int)*((double*)Array + i)) == NULL ? 0 : (int)*((double *)Array + i) * FpPrecision);
        break;
      
      case 2:
        Hashcode = 31 * Hashcode + (((int)*((float*)Array + i)) == NULL ? 0 : (int)*((float *)Array + i)  * FpPrecision);
        break;
      
      case 3:
        Hashcode = 31 * Hashcode + (((short)*((short*)Array + i)) == NULL ? 0 : (short)*((short *)Array + i));
        break;
      
      case 4:
        Hashcode = 31 * Hashcode + (((long)*((long*)Array + i)) == NULL ? 0 : (long)*((long *)Array + i));
        break;
        
      case 5:
        Hashcode = 31 * Hashcode + (((long long)*((long long*)Array + i)) == NULL ? 0 : (long long)*((long long*)Array + i));
        break;
      
      case 6:
        Hashcode = 31 * Hashcode + (((int)*((int*)Array + i)) == NULL ? 0 : (int)*((int*)Array + i));
        break;
      
      case 7:
        Hashcode = 31 * Hashcode + (((char)*((char*)Array + i)) == NULL ? 0 : (char)*((char*)Array + i));
        break;
      
      case 8:
        Hashcode = 31 * Hashcode + (((unsigned char)*((unsigned char*)Array + i)) == NULL ? 0 : (unsigned char)*((unsigned char*)Array + i));
        break;
        
      case 9:
        Hashcode = 31 * Hashcode + (((unsigned short)*((unsigned short*)Array + i)) == NULL ? 0 : (unsigned short)*((unsigned short *)Array + i));
        break;
      
      case 10:
        Hashcode = 31 * Hashcode + (((unsigned long)*((unsigned long*)Array + i)) == NULL ? 0 : (unsigned long)*((unsigned long *)Array + i));
        break;
        
      case 11:
        Hashcode = 31 * Hashcode + (((unsigned long long)*((unsigned long long*)Array + i)) == NULL ? 0 : (unsigned long long)*((unsigned long long*)Array + i));
        break;
      
      case 12:
        Hashcode = 31 * Hashcode + (((unsigned int)*((unsigned int*)Array + i)) == NULL ? 0 : (unsigned int)*((unsigned int*)Array + i));
        break;

    }
  }
  return Hashcode;
}

int WhiroComputeHashcode(void* Array, int TotalElements, int Step, int Format){
  //Traverse an array with N dimensions and compute a hashcode value for it
  int Hashcode = 0;
  for(int i = 0; i < TotalElements; i += Step){
    switch(Format){
      case 1:
        Hashcode += WhiroComputeHashcode1D((double*)Array + i, Step, Format);
        break;
      
      case 2:
        Hashcode += WhiroComputeHashcode1D((float*)Array + i, Step, Format);
        break;
      
      case 3:
        Hashcode += WhiroComputeHashcode1D((short*)Array + i, Step, Format);
        break;
      
      case 4:
        Hashcode += WhiroComputeHashcode1D((long*)Array + i, Step, Format);
        break;
        
      case 5:
        Hashcode += WhiroComputeHashcode1D((long long*)Array + i, Step, Format);
        break;
      
      case 6:
        Hashcode += WhiroComputeHashcode1D((int*)Array + i, Step, Format);
        break;
      
      case 7:
        Hashcode += WhiroComputeHashcode1D((char*)Array + i, Step, Format);
        break;
      
      case 8:
        Hashcode += WhiroComputeHashcode1D((unsigned char*)Array + i, Step, Format);
        break;
        
      case 9:
        Hashcode += WhiroComputeHashcode1D((unsigned short*)Array + i, Step, Format);
        break;
      
      case 10:
        Hashcode += WhiroComputeHashcode1D((unsigned long*)Array + i, Step, Format);
        break;
      
      case 11:
        Hashcode += WhiroComputeHashcode1D((unsigned long long*)Array + i, Step, Format);
        break;
      
      case 12:
        Hashcode += WhiroComputeHashcode1D((unsigned int*)Array + i, Step, Format);
        break;
        
      default:
        printf("Not a array of scalar type (Array Hash Calculator)\n");
        break;
    }
  }
  return Hashcode;
}
