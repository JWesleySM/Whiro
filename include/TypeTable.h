#ifndef TYPE_TABLE_H 
#define TYPE_TABLE_H 

#define MAX_NAME_LENGTH 128

/* This structure represents a field within a type. Every type has at least one field.
 * Product types such as C structs has multiple Fields
 * Name is the name of the field
 * Format is a integer corresponding to the format specifier of the field
 * Offset is the field offset within the type
 * BaseTypeIndex is the index to the type descriptor of the base type of this field.
 * (in case this is a derived type)
 */
typedef struct Field{
  char Name[MAX_NAME_LENGTH];
  int Format;
  int Offset;
  int BaseTypeIndex;
} Field;

/**This structure represents a Type Descriptor. A metadata containing the description of 
 * a type in the source code of the program  
 * Name is the name of the type
 * QuantFields is the number of different fields within the type. Default is 1
 * Fields is the array of fields within the type
 */
typedef struct TypeDescriptor{
  char Name[MAX_NAME_LENGTH];
  int QuantFields;
  Field* Fields;
} TypeDescriptor;

/** 
 * This method inserts in the program a call instruction to any function
 * @param ProgramName is the name of the program. It is used to open the type table file
 * @param TableSize is the size of the type table
 * @param InsHeapArg is true if the heap is to be inspected
 * @param InsStackArg is true if the values store in the stack are to be inspected
 * @param PreciseArg is true for the Precise mode and false for Fast
 */
void WhiroOpenTypeTable(const char* ProgramName, int TableSize, int InsHeapArg, int InsStackArg, int PreciseArg);

/** 
 * This method identifies if a given format corresponds to a scalar type
 * @param Format is an integer corresponding to the format specifier of a type
 * @return true if this is a scalar type and false otherwise
 */
int WhiroIsScalarType(int Format);

#endif
