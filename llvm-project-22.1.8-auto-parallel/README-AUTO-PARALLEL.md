# LLVM 22.1.8 AutoParallel tree

このディレクトリは `llvm-project-8.0.0.src` 全体をコピーして作成し、
LLVM、Clang、OpenMPのソースを22.1.8へ置換した開発ツリーである。

独自実装、Legacy Pass Manager登録、`llvm.directive` 定義は
`llvm-project-8.0.0.src` から無修正で取得している。
LLVM 8で分散していた解析・変換コードを、ビルド分離のため次の1 MODULEへ集約している。

```text
llvm-22.1.8.src/lib/Transforms/AutoParallel/
├── AllPrivateDetect.cpp
├── DependencyCheck.cpp
├── DirectiveInsertion.cpp
├── ParamGet.cpp
├── ReductionDetect.cpp
└── CMakeLists.txt
```

公開ヘッダーと独自intrinsic定義はLLVM側から参照する必要があるため、
`include/llvm/` 以下の既存配置を維持している。処理本体は
`lib/Transforms/AutoParallel/` だけを編集すればよい。

## 構成

```text
llvm-project-22.1.8-auto-parallel/
├── clang/
├── llvm-22.1.8.src/
├── openmp/
└── README-AUTO-PARALLEL.md
```

## 初回構成

```bash
mkdir -p build-llvm22-auto-parallel
cd build-llvm22-auto-parallel

cmake \
  -DLLVM_ENABLE_PROJECTS="clang;openmp" \
  -DLLVM_ENABLE_DUMP=ON \
  -DCLANG_ANALYZER_ENABLE_Z3_SOLVER=OFF \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DLLVM_USE_LINKER=lld \
  -DLLVM_TARGETS_TO_BUILD="AArch64;X86" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -G "Unix Makefiles" \
  ../llvm-project-22.1.8-auto-parallel/llvm-22.1.8.src
```

## 独自実装だけの再ビルド

初回に依存ライブラリをビルドした後は、次の専用ターゲットだけを指定する。

```bash
cd build-llvm22-auto-parallel
make LLVMAutoParallel -j"$(nproc)"
```

`lib/Transforms/AutoParallel/` 内のソースを変更した場合、このコマンドは
変更された独自ソースと `LLVMAutoParallel` MODULEだけを更新する。
LLVM全体の再ビルドは行わない。
