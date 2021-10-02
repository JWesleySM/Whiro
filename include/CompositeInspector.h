#ifndef COMPOSITE_INSPECTOR_H
#define COMPOSITE_INSPECTOR_H

/**
 * This function prints any type of data manipulated by the program. It receives a type
 * descriptor and print every field within data data. It is usually used to print non-scalar
 * variables, except for unions.
 * @param OutputFile is a pointer to the output file of the program
 * @param Data is the value to be inspected
 * @param DataType is the type descriptor of Data
 * @param Name is the name of the variable holding Data in the program
 * @param FuncName is the name of the function currently being inspected
 * @param CallCounter is the current value of the call counter of FuncName
 */
void inspectData(FILE* OutputFile, void* Data, TypeDescriptor* DataType, char* Name, char* FuncName, int CallCounter);

/**
 * This function is responsible to report a value pointed by a pointer in the program. If
 * the instrumentation mode is Fast, this function only prints the type of the pointer.
 * If the instrumentation mode is Precise, it calls trackPointer.
 * @param OutputFile is a pointer to the output file of the program
 * @param Ptr is the pointer to be inspected
 * @param TypeIndex is the type to access the type descriptor of that data
 * @param Name is the name of the variable holding Data in the program
 * @param FuncName is the name of the function currently being inspected
 * @param CallCounter is the current value of the call counter of FuncName
 */
void inspectPointer(FILE* OutputFile, void* Ptr, int TypeIndex, char* Name, char* FuncName, int CallCounter);

/**
 * This function will track the pointer to print its contents. It checks if Ptr
 * is pointing to an address stored in the heap table. If yes, the inspectHeapData 
 * is called to inspecct the value accordingly. Otherwise, it checks if P tr is 
 * pointing to a location within ELF segments. In a positive case, this function 
 * retrieves the type descriptor accessing T using TypeIndex and calls printData
 * which will derreference Ptr and print its contents.
 * @param OutputFile is a pointer to the output file of the program
 * @param Ptr is the pointer to be inspected
 * @param TypeIndex is the type to access the type descriptor of that data
 * @param Name is the name of the variable holding Data in the program
 * @param FuncName is the name of the function currently being inspected
 * @param CallCounter is the current value of the call counter of FuncName
 */
void trackPointer(FILE* OutputFile, void* Ptr, int TypeIndex, char* Name, char* FuncName, int CallCounter);

/**
 * This function is in charge of inspecting variables of union type. Whiro 
 * prints unions as bitmaps. It iterates the pointer to U nion and prints every
 * byte of it.
 * @param Union is the pointer to an union
 * @param Size is the size of the largest composing part in the union type
 * @param Name is the name of the variable holding Union in the program
 * @param FuncName is the name of the function currently being inspected
 * @param CallCounter is the current value of the call counter of FuncName
 */
void inspectUnion(FILE* OutputFile, char* Union, size_t Size, char* Name, char* FuncName, int CallCounter);

/**
 * This function inspects a structure type. It retrieves the type descriptor using
 * the type index and calls inspectData.
 * @param OutputFile is a pointer to the output file of the program
 * @param Struct is the struct to be inspected
 * @param TypeIndex is the type to access the type descriptor of that data
 * @param Name is the name of the variable holding Data in the program
 * @param FuncName is the name of the function currently being inspected
 * @param CallCounter is the current value of the call counter of FuncName
 */
void inspectStruct(FILE* OutputFile, void* Struct, int TypeIndex, char* Name, char* FuncName, int CallCounter);


#endif
