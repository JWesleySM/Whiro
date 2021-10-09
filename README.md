# Introduction

The internal state is formed by the values that a program manipulates. Said values are stored in different memory regions: stack, heap and static memory. The goal of this project is to report said state at different points during the execution of a program. To do so, we The ability to inspect the program state is useful for several reasons such as debugging, program verification, and data visualization.  

# Whiro
Whiro is a framework that inserts in a program the code necessary to report its internal state.
Whiro has two main components:

- An LLVM pass that implements the static components of the Memory Monitor and it is responsible to instrument the program
- A library written in C that implements the dynamic components of the Memory Monitor

# Whiro Workflow

Put an image (maybe the final one of your dissertation?)

The figure above depicts a summary of how Whiro works
At compilation time, the instrumentation pass builds the static components of the memory monitor. First, it gathers information concerning the types present in the original program and builds the type table. Then, it analyzes the entire intermediate representation to create the global map G and the stack maps S for each function in the program. Using this information, the monitor inserts all the required instrumentation in the program to track and report its internal state. The instrumented program is statically linked against the bytecodes containing the dynamic components of the memory monitor.  At execution time, the instrumented version of the program reads the type table T and executes normally while updating the heap table H and using the composite inspector as an auxiliary library at the inspection points.

# Building and Usage
Currently, the only software necessary to run Whiro is LLVM. If you want to use Whiro script, Bash will be also a requirement.

### Building LLVM
In order to use Whiro, [download](http://releases.llvm.org) and [build](http://releases.llvm.org/10.0.0/docs/GettingStarted.html) LLVM on your system. To do this, you can follow the instructions in these links, watch this [video](https://youtu.be/l0LI_7KeFtw), or type the set of commands below:

```
git clone https://github.com/llvm/llvm-project.git
cd llvm-project
mkdir build
cd build
cmake -G "Unix Makefiles" -DLLVM_ENABLE_PROJECTS=clang -DLLVM_ENABLE_ASSERTIONS=On ../llvm
cmake --build .
```

**_Important_**: Whiro was developed on the top of LLVM version 10.0.0. Subsequent versions might break the program due to changes in the namespace.  So, right after cloning its repository, you might want to check out the tag to the 10.0 release:

``` -DLLVM_ENABLE_PROJECTS=clang
git clone https://github.com/llvm/llvm-project.git
git checkout llvmorg-10.0.0 
``` 

### Building Whiro
Clone this repository and build the Memory Monitor as an LLVM pass. Define a couple of path variables:
```
$ LLVM_INSTALL_DIR=</path/to/llvm/>
$ LLVM_BIN=$LLVM_INSTALL_DIR/bin
```
For example, if you built LLVM from source the commands above, the paths will be like:
```
$ LLVM_INSTALL_DIR=/path/to/llvm-project/build
$ LLVM_BIN=/path/to/llvm-project/build/bin
```
Then, create the build directory and run CMake to prepare the build files. We will use the folder build for that. You can use any build tool that you want, e.g. ninja or make.
```
$ git clone git clone https://github.com/JWesleySM/Whiro
$ cd Whiro
$ mkdir build
$ cd build
$ cmake -DLLVM_INSTALL_DIR=$LLVM_INSTALL_DIR ../
```
Next, build the project:
```
$ cmake --build .
```
### Running 
Instrument the program as:

```
$LLVM_BIN/opt -load /build/lib/libMemoryMonitor.so -memoryMonitor (or libMemoryMonitor.dylib if you're running on macOS) program.bc -o program.wbc
```

The _program.wbc_ file is the modified bytecode which contains the instrumentation on it. Using this bytecode, one can produce a new program that will run normally and also run the code to create the output file. However, the new program must be linked against the dynamic components of the Memory Monitor, i.e., the Auxiliary State. You can do it either statically or dinamically. To link them statically, follow the steps below:

Compile the components:
```
$LLVM_BIN/clang -c -emit-llvm ./lib/HeapTable.c -o ./lib/HeapTable.bc
$LLVM_BIN/clang -c -emit-llvm ./lib/TypeTable.c -o ./lib/TypeTable.bc
$LLVM_BIN/clang -c -emit-llvm ./lib/CompositeInspector.c -o ./lib/CompositeInspector.bc
$LLVM_BIN/clang -c -emit-llvm ./lib/ArrayHashCalculator.c -o ./lib/ArrayHashCalculator.bc
```
Link against the instrumented bytecode:
```
$LLVM_BIN/llvm-link ./lib/ArrayHashCalculator.bc program.wbc -o program.wbc
$LLVM_BIN/llvm-link ./lib/CompositeInspector.bc program.wbc -o program.wbc
$LLVM_BIN/llvm-link ./lib/TypeTable.bc program.wbc -o program.wbc
$LLVM_BIN/llvm-link ./lib/HeapTable.bc program.wbc -o program.wbc
```
To generate the new program, you can use the LLVM static compiler and clang:
```
$LLVM_BIN/llc program.wbc -o program.s
$LLVM_BIN/clang program.s -o program.out
```
The _program.out_ file is the program with the code to report its internal state. Notice that, this program will read the type table file. Make sure it is able to do it. The [runWhiro.sh](https://github.com/JWesleySM/NewWhiro/blob/main/Benchmarks/runWhiro.sh) script is a good reference to this workflow.

# Customizations
Whiro allows different options to customize the amount of program state that is tracked. The user can configure the granularity of inspection points, to encompass, for instance, either the return statement of every function, or the last statement of the program that is visible to the compiler. Similarly, state can be configured to include values stored in global, stack-allocated and heap-allocated variables, or any combination of them. Those options are used in the instrumentation pass. The options are the following:

* **-om** : inspect the state of the program only at the _main_ function
* **-pr**: track the contents pointed by pointers
* **-stc** : inspect only the static variables
* **-stk**: inspect only the variables that reside on the stack of the functions
* **-hp**: inspect only the variables that point to heap-allocated memory. Notice that by enabling this option, the option *trackPtr* is automatically enabled
* **-fp**: inspect the entire heap, i.e., all the data blocks allocated in the heap

A user can combine those different options. For example, the code below:

``` $LLVM_BIN/opt -load /build/lib/libMemoryMonitor.so -memoryMonitor -stc -hp -om program.bc -o program.wbc ```

It tells Whiro to inspect only the variables allocated in static memory and the variables that point to the heap, only at the return point of function _main_. By default, the options **-stc**, **-stk**, and **-hp** are enabled, but if a user manually chooses one of them, the others are automatically disabled.

### Debug options
Whiro has a debug mode. You can use it using the LLVM opt's **-debug-only** option. There are two debug modes:

* **memon** : debug the instrumentation performed by the Memory Monitor
* **tt** : debug the construction of the Type Table

You can use them separately or together:

``` $LLVM_BIN/opt -load /build/lib/libMemoryMonitor.so -memoryMonitor -debug-only=memon,tt program.bc -o program.wbc ```

The code above will debug both the instrumentation and the Type Table construction

### Statistics

Whiro computes some statistics during instrumentation that can be viewed using the LLVM **-stats** option. The statistics are:

* Number of variables inspected
* Number of extended live ranges
* Number of variables shadowed in the stack
* Number of heap operations
* Number of functions instrumented
* Number of variables with different SSA types







