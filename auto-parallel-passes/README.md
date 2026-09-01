# Auto Parallel passes

このディレクトリは、改造版 LLVM 8 に埋め込まれていた自動並列化機能を、
LLVM 本体のソースツリーから分離して保存するためのものです。

## 内容

- `include/`: LLVM 22プラグインのヘッダー（今後ここを直接編集する）
- `lib/`: LLVM 22プラグインの実装（今後ここを直接編集する）
- `legacy/llvm8/`: 抽出時点のLLVM 8独自実装（比較・移植元、編集しない）
- `docs/`: 引継ぎ元の設計資料

`legacy/llvm8/include/` と `legacy/llvm8/lib/` は、抽出時点の
`llvm-project-8.0.0.src/llvm-8.0.0.src/` に入っていた最新版です。
今後のLLVM 22移植はルート直下の `include/` と `lib/` に直接書き込みます。

## 重要

ルートの `include/` と `lib/` には、LLVM 8の処理本体とLegacy Pass Manager形式を
保った外部共有ライブラリ用コードがあります。
比較用の無変更版は `legacy/llvm8/` に保存しています。

外部化のため変更済みなのは次の範囲です。

- LLVM本体用includeパスを `AutoParallel/...` に変更
- LLVM本体の中央登録に依存せず、各パスを `RegisterPass` で自己登録
- CMakeで `AutoParallelLegacy` 共有ライブラリとしてビルド
- New PM移行用の `Plugin.cpp` はビルド対象から外して保存

解析結果の受け渡し、typed pointer、削除・変更されたLLVM API、古いOpenMPコード生成
など、処理内部のLLVM 22移植はまだ行っていません。

また `ParamGet` は `Intrinsic::directive` を参照するため、LLVM 8 本体には
`include/llvm/IR/Intrinsics.td` の変更が必要です。必要な本体変更は
`integration/llvm8-changes.md` にまとめています。

## LLVM 22への移植順序（Legacy PMを維持する場合）

1. 独自 intrinsic を通常の関数または metadata に置き換える
2. `DependencyCheck` のLLVM 22 API差分を直す
3. `ReductionDetect` と `AllPrivateDetect` のLLVM 22 API差分を直す
4. `DirectiveInsertion` のopaque pointer対応を行う
5. `ParamGet` を opaque pointer と現在の OpenMP ABI に対応させる
6. 必要な場合だけ `AllPrivateDetect` を修復して移植する

## LLVM 22プラグインのビルド

```bash
cmake \
  -S . \
  -B build-llvm22 \
  -DLLVM_DIR=/usr/lib/llvm-22/lib/cmake/llvm \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build-llvm22 -j1
```

内部移植が完了した後のロード方法:

```bash
opt-22 \
  -load ./build-llvm22/AutoParallelLegacy.so \
  -directiveinsertion \
  input.ll \
  -S -o output.ll
```

## 現在のビルド状態

外部Legacy PMプラグインの構成とCMake生成は完了しています。`getAnalysis<...>()`は
Legacy PMのまま利用できています。処理内部がLLVM 8のままなので、現在のビルドは
LLVM 22でのSCEVヘッダー構成とopaque pointer APIの差分で停止します。

登録名は次のとおりです。

- Function pass: `dependencycheck`, `reductiondetect`, `allprivatedetect`
- Module pass: `directiveinsertion`, `paramget`

実行時はNew PM用の `-load-pass-plugin` ではなく、Legacy用の `-load`を使用します。
`Plugin.cpp`を再び有効にする場合は、各パスのNew PM移植とCMakeへの再追加が必要です。
