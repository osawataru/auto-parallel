# LLVM 22本体組み込み版

## 目的

`auto-parallel-passes/` はLLVM 22向け外部モジュール版として保存する。
それとは別に、移植済みコードをLLVM 22.1.8のソースツリーへコピーし、
LLVM本体と同時にコンパイルするLLVM 8互換構成を用意する。

## 2つの版

| 版 | 場所 | 読み込み方法 |
|---|---|---|
| 外部モジュール版 | `auto-parallel-passes/` | 独立してビルドし、外部から読み込む |
| LLVM本体組み込み版 | `llvm-project-22.1.8-dev/llvm-22.1.8.src/` | LLVMの各ライブラリと `opt` へ静的リンクする |

本体組み込み版は外部プラグインを使用しない。New Pass Manager用パスを
LLVM本体の `PassRegistry.def` に登録し、標準の `opt -passes=` から直接
実行する。

## 組み込み場所

- 解析: `include/llvm/Analysis/`、`lib/Analysis/`
- ディレクティブ挿入: `include/llvm/Transforms/`、
  `lib/Transforms/DirectiveInsertion/`
- ParamGet: `include/llvm/Transforms/Scalar/`、`lib/Transforms/Scalar/`
- New PMパス定義: `include/llvm/Transforms/AutoParallel.h`
- New PMパス登録: `lib/Passes/PassRegistry.def`
- 独自intrinsic: `include/llvm/IR/Intrinsics.td` の `llvm.directive`

各ソースは `LLVMAutoParallel` コンポーネントへ登録され、`LLVMPasses` と
LLVM 22版 `opt` へ静的リンクされる。

## 構成とビルド

```bash
cmake -S llvm-project-22.1.8.src/llvm-22.1.8.src \
  -B build-llvm22.1.8 \
  -G "Unix Makefiles" \
  -DLLVM_ENABLE_PROJECTS="clang;openmp" \
  -DLLVM_TARGETS_TO_BUILD="AArch64;X86" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang-22 \
  -DCMAKE_CXX_COMPILER=clang++-22 \
  -DLLVM_USE_LINKER=lld \
  -DLLVM_ENABLE_DUMP=ON

cmake --build build-llvm22-dev --target opt --parallel "$(nproc)"
```

`LLVM_ENABLE_DUMP=ON` は、移植元のパスがデバッグ時にLLVM IRの
`dump()` メソッドを呼び出すために必要である。

LLVM、Clang、OpenMPを含む構成済みターゲットをすべてビルドする場合は、
次を実行する。

```bash
cmake --build build-llvm22.1.8 --parallel "$(nproc)" \
  2>&1 | tee build-llvm22.1.8/build.log
```

## 実行

```bash
build-llvm22-dev/bin/opt \
  -passes='auto-parallel-directive,verify' \
  -ditarget=kernel -S input.ll -o directive.ll

build-llvm22-dev/bin/opt \
  -passes='auto-parallel-paramget,verify' \
  -S directive.ll -o parallel.ll
```

標準最適化から独自変換までを1回の `opt` で実行する場合は、パスの実行
単位を明示する。

```bash
build-llvm22-dev/bin/opt \
  -passes='module(function(mem2reg,early-cse,loop-mssa(licm)),auto-parallel-directive,auto-parallel-paramget,verify)' \
  -ditarget=kernel -S input.ll -o parallel.ll
```

本体組み込み版の `directive.ll` には通常関数
`__auto_parallel_directive` ではなく、LLVM 8版と同じ考え方の
`call void @llvm.directive(metadata ...)` が生成される。
