## Phantom Symbolic Execution Engine

- **mit.cpp**: the implementation of SE using modular interval theory as backend
- **sase.cpp**: the implementation of SE using boolector smt solver as backend
- **phantom.cpp**: source file containing the emulation engine

#### How to compile:
**prerequisite**: boolector smt solver should be installed first.
```
make all;
```
#### How to execute:
SE using "mit":
```
./selfie -c code.c -o code
./phantom -l code -i 0
```
SE using SMT solver:
```
./selfie -c code.c -o code
./phantom -l code -k 0
```

input syscall in source code should be defined as:
```
x = input(lo, up, step);
```

#### Sample Benchmarks:
can be found in benchmarks folder.