#!/bin/bash

echo "  -> Moving to project directory"
cd ./Demo/CORTEX_LM3S811_GCC/

echo "  -> Clean old files"
make clean

echo "  -> Building project"
make

echo "  -> Launch QEMU"
qemu-system-arm -machine lm3s811evb -kernel gcc/RTOSDemo.axf -serial stdio
# qemu-system-arm -M lm3s811evb -kernel gcc/RTOSDemo.axf
