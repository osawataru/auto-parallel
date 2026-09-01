# LLVM 8本体側の変更点

独自実装以外に、元のLLVM 8へ以下の登録変更が加えられています。

## 独自 intrinsic

`include/llvm/IR/Intrinsics.td`:

```tablegen
def int_directive : Intrinsic<[],[llvm_metadata_ty],[IntrArgMemOnly],"llvm.directive">;
```

## 解析パスの登録

- `include/llvm/Analysis/Passes.h`
- `include/llvm/InitializePasses.h`
- `include/llvm/LinkAllPasses.h`
- `lib/Analysis/Analysis.cpp`
- `lib/Analysis/CMakeLists.txt`

登録対象:

```text
DependencyCheck
ReductionDetect
AllPrivateDetect
```

## 変換パスの登録

- `include/llvm/Transforms/Scalar.h`
- `lib/Transforms/CMakeLists.txt`
- `lib/Transforms/Scalar/CMakeLists.txt`
- `lib/Transforms/Scalar/Scalar.cpp`

登録対象:

```text
DirectiveInsertion
ParamGet
```

## LLVM 22での扱い

これらのLegacy Pass登録をLLVM 22へそのままコピーしないこと。
LLVM 22では `PassPluginLibraryInfo` と `PassBuilder` を使用する外部プラグインとして
登録する。独自 `llvm.directive` intrinsicもLLVM本体への変更を避けるため、通常の
宣言関数またはmetadataによる表現へ変更するのが望ましい。

