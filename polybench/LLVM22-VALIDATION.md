# LLVM 22 PolyBench検証シェル 利用手順

## 1. 概要

検証用シェルには、次の2種類の検証経路があります。

1. 逐次版と自動並列版の2版比較
2. 逐次版、手動OpenMP版、自動並列版の3版比較

コンパイルと実行は別スクリプトです。すべてのコマンドは次のディレクトリから実行できます。

```bash
cd ~/auto-parallel/polybench
```

## 2. 事前準備

独自パスとLegacy PM専用ドライバを先にビルドします。

```bash
cmake --build \
  ~/auto-parallel/auto-parallel-passes/build-llvm22 \
  --parallel
```

必要な成果物は次の2つです。

```text
auto-parallel-passes/build-llvm22/AutoParallelLegacy.so
auto-parallel-passes/build-llvm22/auto-parallel-legacy-driver
```

## 3. 逐次版と自動並列版の検証

### 3.1 1プログラムをコンパイル

```bash
./compile-llvm22.sh bicg
```

引数を省略した場合も`bicg`を使用します。

この処理では次を生成します。

```text
bicg.ll
bicg.normalized.ll
bicg.directive.ll
bicg.parallel.ll
bicg.sequential
bicg.parallel
directive.log
paramget.log
```

既定の入力は次です。

```text
polybench_no_omp
```

既定の出力先は次です。

```text
prog/llvm22-no-omp/<benchmark>/
```

### 3.2 1プログラムを実行・比較

```bash
OMP_NUM_THREADS=4 ./run-llvm22.sh bicg
```

逐次版と自動並列版を実行し、配列ダンプを比較します。

```text
sequential.dump
parallel.dump
sequential.time
parallel.time
result.diff          不一致の場合だけ生成
```

配列値は画面には表示せず、`.dump`ファイルだけに保存します。画面には実行時間、
配列が一致したかを示すPASS/FAIL、各実行の終了コード、保存先を表示します。

### 3.3 コンパイルから比較まで一括実行

```bash
OMP_NUM_THREADS=4 ./verify-llvm22.sh bicg
```

これは次の2コマンドを順番に呼ぶ互換用スクリプトです。

```bash
./compile-llvm22.sh bicg
./run-llvm22.sh bicg
```

### 3.4 全30プログラムをコンパイル

```bash
./compile-all-llvm22.sh
```

1件が失敗しても残りを続行します。集計結果は次に保存されます。

```text
prog/llvm22-no-omp/compile-summary.tsv
```

各プログラムのログ:

```text
prog/llvm22-no-omp/<benchmark>/compile.log
```

### 3.5 全30プログラムを実行

```bash
OMP_NUM_THREADS=4 ./run-all-llvm22.sh
```

集計結果:

```text
prog/llvm22-no-omp/run-summary.tsv
```

各プログラムのログ:

```text
prog/llvm22-no-omp/<benchmark>/run.log
```

画面に表示される進捗、時間、判定をまとめて保存する場合:

```bash
OMP_NUM_THREADS=4 ./run-all-llvm22.sh 2>&1 | tee run-all.log
```

全30件をコンパイルから実行まで1コマンドで行う旧互換スクリプトもあります。

```bash
OMP_NUM_THREADS=4 ./verify-all-llvm22.sh
```

通常は工程と失敗箇所を分けやすい`compile-all-llvm22.sh`と
`run-all-llvm22.sh`の使用を推奨します。

## 4. 逐次・手動OpenMP・自動並列の3版比較

3版は次の入力から生成します。

| 版 | 入力 | 並列化方法 |
|---|---|---|
| `sequential` | `polybench_no_omp` | 並列化なし |
| `manual` | `polybench-c-4.2` | ソース内の手書きOpenMP pragma |
| `automatic` | `polybench_no_omp` | 独自LLVMパス |

### 4.1 1プログラムの3版をコンパイル

```bash
./compile-three-llvm22.sh bicg
```

主な実行ファイル:

```text
prog/llvm22-three/bicg/bicg.sequential
prog/llvm22-three/bicg/bicg.manual
prog/llvm22-three/bicg/bicg.parallel
```

`bicg.parallel`が自動並列版です。

### 4.2 1プログラムを複数回計測

```bash
RUNS=3 OMP_NUM_THREADS=4 ./run-three-llvm22.sh bicg
```

`RUNS`は各版の実行回数です。省略時は3回です。

より安定した値を取る場合:

```bash
RUNS=10 OMP_NUM_THREADS=4 ./run-three-llvm22.sh bicg
```

出力:

```text
sequential.dump
manual.dump
automatic.dump
sequential.time.1 ... sequential.time.N
manual.time.1     ... manual.time.N
automatic.time.1  ... automatic.time.N
timings.tsv
averages.tsv
sequential-vs-manual.diff       不一致時
sequential-vs-automatic.diff    不一致時
```

`timings.tsv`には全測定値が入ります。

```text
variant  run  seconds  exit_status
```

平均値は計算しません。各回の実測時間を`timings.tsv`で確認します。

### 4.3 配列結果の判定

3版すべてが実際の配列値を`.dump`へ出力します。

```text
sequential.dump
manual.dump
automatic.dump
```

実行スクリプトは次を完全一致で比較します。

```bash
cmp -s sequential.dump manual.dump
cmp -s sequential.dump automatic.dump
```

不一致の場合はunified diffを生成し、終了コード2を返します。

### 4.4 全30プログラムの3版をコンパイル

```bash
./compile-three-all-llvm22.sh
```

集計結果:

```text
prog/llvm22-three/compile-summary.tsv
```

### 4.5 全30プログラムを3版で計測

```bash
RUNS=3 OMP_NUM_THREADS=4 ./run-three-all-llvm22.sh
```

推奨例:

```bash
RUNS=10 OMP_NUM_THREADS=4 \
  ./run-three-all-llvm22.sh 2>&1 | tee three-all.log
```

全件集計:

```text
prog/llvm22-three/run-summary.tsv
```

ベンチマーク別ログ:

```text
prog/llvm22-three/<benchmark>/run-three.log
```

## 5. 環境変数

| 変数 | 既定値 | 用途 |
|---|---|---|
| `OMP_NUM_THREADS` | `4` | 手動版・自動版のスレッド数 |
| `RUNS` | `3` | 3版比較で各版を実行する回数 |
| `POLYBENCH_DIR` | `polybench_no_omp` | 2版比較の入力ディレクトリ |
| `LLVM22_RESULT_DIR` | `prog/llvm22-no-omp` | 2版比較の出力先 |
| `SEQUENTIAL_SOURCE_DIR` | `polybench_no_omp` | 3版比較の逐次・自動版入力 |
| `MANUAL_SOURCE_DIR` | `polybench-c-4.2` | 3版比較の手動OpenMP入力 |
| `THREE_RESULT_DIR` | `prog/llvm22-three` | 3版比較の出力先 |
| `OMP_INCLUDE_DIR` | プロジェクト内OpenMPヘッダー | OpenMPヘッダーの場所 |
| `OMP_LIBRARY_DIR` | プロジェクト内`build/lib` | `libomp`の場所 |

入力・出力を一時的に変更する例:

```bash
POLYBENCH_DIR=/path/to/polybench \
LLVM22_RESULT_DIR=/tmp/llvm22-result \
./compile-llvm22.sh bicg
```

## 6. 終了コード

| コード | 意味 |
|---|---|
| `0` | コンパイル成功、または全比較成功 |
| `1` | ファイル不足、コンパイル・実行失敗など |
| `2` | 配列ダンプ不一致 |
| `128+signal` | Segmentation faultなどシグナル終了 |

全件用スクリプトは個別の失敗後も続行し、最後に1件以上失敗していれば非0で終了します。

## 7. ログの確認順序

コンパイル失敗時:

1. `compile-summary.tsv`
2. `<benchmark>/compile.log`または`compile-three.log`
3. `<benchmark>/directive.log`
4. `<benchmark>/paramget.log`
5. `<benchmark>.directive.ll`または`<benchmark>.parallel.ll`

実行結果不一致時:

1. `run-summary.tsv`
2. `run.log`または`run-three.log`
3. `sequential-vs-manual.diff`
4. `sequential-vs-automatic.diff`または`result.diff`
5. 各`.dump`ファイル

## 8. 推奨検証手順

全30件について3版の性能と結果を検証する場合:

```bash
cd ~/auto-parallel
cmake --build auto-parallel-passes/build-llvm22 --parallel

cd polybench
./compile-three-all-llvm22.sh
RUNS=10 OMP_NUM_THREADS=4 \
  ./run-three-all-llvm22.sh 2>&1 | tee three-all.log

column -ts $'\t' prog/llvm22-three/compile-summary.tsv
column -ts $'\t' prog/llvm22-three/run-summary.tsv
```

短時間で動作だけ確認する場合:

```bash
./compile-three-llvm22.sh bicg
RUNS=3 OMP_NUM_THREADS=4 ./run-three-llvm22.sh bicg
```

## 9. 計測上の注意

- 同じマシン、同じスレッド数、できるだけ同じ負荷状態で比較します。
- 実行時間が非常に短いプログラムはばらつきが大きいため、`RUNS=10`以上を推奨します。
- speedupは結果の正しさを示しません。`.dump`の比較も確認してください。
- 浮動小数点reductionでは演算順序により微小な差が発生する可能性があります。現在の判定は文字列の完全一致です。
- `polybench_no_omp`には配列ダンプに加えて既存の内部ハッシュ検査も残っています。
