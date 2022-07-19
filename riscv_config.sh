#!/bin/bash

cmake -S llvm -B build -G Unix\ Makefiles -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra;libcxx;libcxxabi;libunwind;lldb;compiler-rt;lld;polly' \
    -DDEFAULT_SYSROOT="/media/hdd0/research/riscv/riscv-gnu-toolchain/build/riscv64-unknown-linux-gnu" \
    -DLLVM_TARGETS_TO_BUILD="RISCV" \
    -DGCC_INSTALL_PREFIX="/media/hdd1/research/riscv/riscv-gnu-toolchain/build"

#cmake -S llvm -B build -G Unix\ Makefiles \
#    -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra;libcxx;libcxxabi;libunwind;lldb;compiler-rt;lld;polly' \
#    -DCMAKE_SYSTEM_NAME=riscv64 \
#    -DLLVM_TABLEGEN=/bin/llvm-tblgen \
#    -DLLVM_DEFAULT_TARGET_TRIPLE=riscv64-unknown-linux-gnu \
#    -DLLVM_TARGET_ARCH=riscv64 \
#    -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=riscv64
    
    
    #-DDEFAULT_SYSROOT="/media/hdd0/research/riscv/riscv-gnu-toolchain/build/riscv64-unknown-linux-gnu" \
    #-DLLVM_TARGETS_TO_BUILD="RISCV" \
    #-DGCC_INSTALL_PREFIX="/media/hdd0/research/riscv/riscv-gnu-toolchain/build"
