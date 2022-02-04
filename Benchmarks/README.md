# Trying Whiro

This folder contains some programs for you to try Whiro. The _runWhiro.sh_ scrips executes Whiro [workflow](https://github.com/JWesleySM/NewWhiro#whiro-workflow) in every program stored here. You can pass the 
customization [options](https://github.com/JWesleySM/NewWhiro#customizations) directly to this script. It supports the following options:

* **-dmm**: show debug information about Whiro's Memory Monitor during instrumentation
* **-dtt**: show debug information about Whiro's Type Table construction during instrumentation
* **-om**:  inspect only the 'main' function from the program
* **-stk**: inspect only the variables in the stack of functions
* **-stc**: inspect only static-allocated data
* **-hp**:  inspect only heap-allocated data
* **-fp**:  report the entire heap at every inspection point
* **-pr**:  enable precise instrumentation mode (track the contents pointed by pointer variables)
* **-h**:   displays usage

**Important**: In order to use this script, set the path to your LLVM installation at line 5.

Example of usage:
```
# Inspect the contents pointerd by pointer variables in the main function showing debug information during instrumentation
./runWhiro.sh -dmm -p -stc
```
