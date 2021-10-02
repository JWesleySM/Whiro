#!/bin/bash

set -e
WHIRODIR=../
LLVM=/home/jw/llvm-10/build/bin

debugMM=""
debugTT=""
heap=""
stack=""
static=""
onlymain=""
precise=""
fullheap=""
help=false

function usage(){
  echo "Usage: runWhiro [OPTION]... "
  echo ""
  echo "The following arguments can be passed to this script:"
  echo " -dmm:     show debug information about Whiro's Memory Monitor during instrumentation"
  echo " -dtt:     show debug information about Whiro's Type Table construction during instrumentation"
  echo " -om:      inspect only the 'main' function from the program"
  echo " -stk:     inspect only the variables in the stack of functions"
  echo " -stc:     inspect only static-allocated data"
  echo " -hp:      inspect only heap-allocated data"
  echo " -fp:      report the entire heap at every inspection point"
  echo " -p:       enable Precise instrumentation mode (track the contents pointed by pointer variables)"
  echo " -h:       displays this help"
}

function compileComponents(){
	$LLVM/clang -O3 -c -w -emit-llvm $WHIRODIR/lib/HeapTable.c -o $WHIRODIR/lib/HeapTable.bc
	$LLVM/clang -O3 -c -w -emit-llvm $WHIRODIR/lib/TypeTable.c -o $WHIRODIR/lib/TypeTable.bc
	$LLVM/clang -O3 -c -w -emit-llvm $WHIRODIR/lib/CompositeInspector.c -o $WHIRODIR/lib/CompositeInspector.bc
	$LLVM/clang -O3 -c -w -emit-llvm $WHIRODIR/lib/ArrayHashCalculator.c -o $WHIRODIR/lib/ArrayHashCalculator.bc
}

function instrumentAndRun(){
  ProgramName="${1%.c}"
	if [[ ! -d $ProgramName"-Output" ]]; then
		mkdir $ProgramName"-Output"
	fi
	$LLVM/clang -Xclang -disable-O0-optnone -fno-discard-value-names -c -emit-llvm -g $1 -S -o "${ProgramName}.bc"
	$LLVM/opt -mem2reg -mergereturn "${ProgramName}.bc" -S -o "${ProgramName}.bc"
	$LLVM/opt -load $WHIRODIR/build/lib/libMemoryMonitor.so -memoryMonitor $debugMM $debugTT $stack $heap $static $onlymain $precise $fullheap -stats "${ProgramName}.bc" -S -o "${ProgramName}.wbc"
	$LLVM/llvm-link $WHIRODIR/lib/ArrayHashCalculator.bc "${ProgramName}.wbc" -S -o "${ProgramName}.wbc"
  $LLVM/llvm-link $WHIRODIR/lib/CompositeInspector.bc "${ProgramName}.wbc" -S -o "${ProgramName}.wbc"
  $LLVM/llvm-link $WHIRODIR/lib/TypeTable.bc "${ProgramName}.wbc" -S -o "${ProgramName}.wbc"
  $LLVM/llvm-link $WHIRODIR/lib/HeapTable.bc "${ProgramName}.wbc" -S -o "${ProgramName}.wbc"
	$LLVM/llc "${ProgramName}.wbc" -o "${ProgramName}.s"
	$LLVM/clang "${ProgramName}.s" -o "${ProgramName}.out"
	echo "Running"
	echo ""
	./"${ProgramName}.out" a
  mv "$1_Output" ./$ProgramName"-Output/"
}

for arg in "$@"; do
  case "$arg" in
    "-dmm")debugMM="-debug-only=memon";;
    "-dtt")debugTT="-debug-only=tt";;
    "-om")onlymain="-onlyMain";;
    "-stk")stack="-insStack";;
    "-stc")static="-insStatic";;
    "-hp")heap="-insHeap";;
    "-fh")fullheap="-insFullHeap";;
    "-p")precise="-trackPtr";;	
    "-h")help=true;;
  esac
done

if [[ "$help" = true ]]; then
  usage
  exit 1
fi

compileComponents

for Program in *.c; do
  instrumentAndRun $Program
done

