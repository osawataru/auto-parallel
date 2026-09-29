# LLVM 22 AutoParallel development tree

This directory is a development copy of `llvm-project-22.1.8.src`.
The original source tree is left unchanged.

The built-in AutoParallel implementation is grouped into one LLVM component:

```
llvm-22.1.8.src/lib/Transforms/AutoParallel/
```

The public headers remain in their existing LLVM include locations, and the
custom `llvm.directive` intrinsic remains in `include/llvm/IR/Intrinsics.td`.
The `auto-parallel-legacy-driver` tool links `LLVMAutoParallel` statically, so
this is an in-tree, built-in configuration rather than an external plugin.

## Configure a development build

Use a build directory separate from the old source tree. Ninja is recommended
for fast dependency checks. Add the compiler launcher options only when
`ccache` is installed.

```bash
cmake \
  -S llvm-project-22.1.8-dev/llvm-22.1.8.src \
  -B build-llvm22-dev \
  -G Ninja \
  -DLLVM_ENABLE_PROJECTS="clang;openmp" \
  -DLLVM_ENABLE_DUMP=ON \
  -DCLANG_ANALYZER_ENABLE_Z3_SOLVER=OFF \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DLLVM_USE_LINKER=lld \
  -DLLVM_TARGETS_TO_BUILD="AArch64;X86" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

Build only the development driver and its dependencies:

```bash
cmake --build build-llvm22-dev \
  --target auto-parallel-legacy-driver \
  --parallel
```

After editing one of the five files in `lib/Transforms/AutoParallel`, the same
command recompiles the changed source, recreates the small AutoParallel
library, and relinks the driver. It does not build all LLVM tools.

Changing `include/llvm/IR/Intrinsics.td` still regenerates intrinsic headers
and can trigger a wider rebuild.
