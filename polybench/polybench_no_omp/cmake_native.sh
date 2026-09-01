#!/bin/bash
mkdir -p build/gcc && cmake -S. -Bbuild/gcc -DCMAKE_BUILD_TYPE=Debug
mkdir -p build/clang && cmake -S. -Bbuild/clang -DCMAKE_BUILD_TYPE=Debug
mkdir -p build/gcc-o2 && cmake -S. -Bbuild/gcc-o2 -DCMAKE_BUILD_TYPE=Release
mkdir -p build/clang-o2 && cmake -S. -Bbuild/clang-o2 -DCMAKE_BUILD_TYPE=Release
mkdir -p build/gcc-of && cmake -S. -Bbuild/gcc-of -DCMAKE_BUILD_TYPE=Release -DOPTIMIZE_FAST=ON
mkdir -p build/clang-of && cmake -S. -Bbuild/clang-of -DCMAKE_BUILD_TYPE=Release -DOPTIMIZE_FAST=ON