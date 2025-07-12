Swift-Sim is a modular and hybrid GPU simulation framework.

This is a newer and improved version of the Swift-Sim simulator.  Based on the framework we proposed in DATE 2025: Swift-Sim: A Modular and Hybrid GPU Architecture Simulation Framework, we have made the following improvements:

1、Instead of simulating only the representative warp's instructions, we now simulate all SASS instructions, effectively reducing errors caused by inter-block divergence.

2、We modify the memory subsystem modeling, simulating the execution of all memory instructions rather than just a subset, improving the accuracy of memory performance analysis.

3、Several bugs in the original version have been fixed, further enhancing the correctness and robustness of the simulator. 

As a result, the simulation speed of Swift-Sim-Accurate is reduced compared to the original version, but the accuracy has been significantly improved.

https://github.com/xurongxiang/Swift-GPUSim/blob/main/README.md
