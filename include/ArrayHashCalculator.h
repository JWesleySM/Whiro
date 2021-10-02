#ifndef ARRAY_HASH_CALCULATOR_H
#define ARRAY_HASH_CALCULATOR_H

//Float point precision when computing hash code for floating point values
#define FpPrecision 100

/**
 * This method returns the array index as string to print pretty array dimensions.
 * @param Index is the index of the array.
 * @return the string corresponding to that array index.
 */
char* getArrayIndexAsString(int Index);

/**
 * This method computes the hashcode for a 1D array.
 * @param Array is a pointer to the beginning of the array.
 * @param Size is the amount of elements in the array.
 * @param Format is the type of the elements of the array.
 * @return the hashcode.
 */
int computeHashcode1D(void* Array, int Size, int Format);

/**
 * This method computes the hashcode for a ND array. It traverses the array in a
 * 'flat' way. It receives a pointer to the beginning of the array and increment it
 * using a step value. Using this step, we can reach all the 1D arrays inside this
 * one.
 * @param Array is a pointer to the beginning of the array.
 * @param Step is the step that the base pointer must take.
 * @param Total_Elements is the amout of elements of the array.
 * @return the hashcode computed for this array.
 */
int computeHashcode(void* Array, int TotalElements, int Step, int Format);

#endif
