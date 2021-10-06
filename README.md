# Introduction

The internal state is formed by the values that a program manipulates. Said values are stored in different memory regions: stack, heap and static memory. The goal of this project is to report said state at different points during the execution of a program. To do so, we The ability to inspect the program state is useful for several reasons such as debugging, program verification, and data visualization.  

# Whiro
Whiro is a framework that inserts in a program the code necessary to report its internal state.
Whiro has two main components:

- An LLVM pass that implements the static components of the Memory Monitor and it is responsible to instrument the program
- A library written in C that implements the dynamic components of the Memory Monitor

# Whiro Workflow

Put an image (maybe the final one of your dissertation?)

# Building and Usage

# Customization
