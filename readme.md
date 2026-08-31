# 注意
研究室githubの鋼レポジトリに置いてあるコードは古いものなので注意







# 引継ぎ資料
## 連絡先
Tk.04256800@gmail.com

## papers
論文

## polybench
polybenchベンチマーク

cコード内にOpenMPの指示文を追加してある

評価で使用した各シェルスクリプト入り

## presen
発表資料

## Hikitugi_jingu
神宮先輩（本研究の引継ぎ元）の引継ぎ資料

## llvm-project-8.0.0.src_20250131
柳田が変更を加えた並列コード生成機能搭載済LLVMのソースファイル

研究室githubの鋼プロジェクト -> llvm-hagane -> yanagidaブランチに置いてあるのは古いやつなので注意






# llvm-project-8.0.0.src_20250131
## 注意
llvm8.0.0がベースです  
新しいバージョンでは動きません

## LLVMに追加してあるもの
|パス|概要|
|---|---|
| /llvm-project-8.0.0.src/llvm-8.0.0.src/lib/Analysis/DependencyCheck.cpp | データ依存の解析 |
| /llvm-project-8.0.0.src/llvm-8.0.0.src/lib/Analysis/ReductionDetect.cpp | リダクション演算の解析 |
| /llvm-project-8.0.0.src/llvm-8.0.0.src/lib/Transforms/DirectiveInsertion/DirectiveInsertion.cpp | 指示文挿入パス |
| /llvm-project-8.0.0.src/llvm-8.0.0.src/lib/Transforms/Scalar/ParamGet.cpp | 並列IRコード生成パス |

`/llvm-project-8.0.0.src/llvm-8.0.0.src/lib/Analysis/AllPrivateDetect.cpp`（private指示節の解析機能）もあるが、エラーを吐くため無効化中

柳田が追加した部分には`// 2024 added`を付けてある

## ビルド
想定ディレクトリ
```
workdir
├── build
└── llvm-project-8.0.0.src
    ├── clang
    ├── llvm-8.0.0.src
    └── openmp
```

```
cd build

cmake -DLLVM_ENABLE_PROJECTS="clang;openmp" -DLLVM_ENABLE_DUMP=ON -DCLANG_ANALYZER_ENABLE_Z3_SOLVER=OFF -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLLVM_USE_LINKER=lld -DLLVM_TARGETS_TO_BUILD="AArch64;X86" -DCMAKE_BUILD_TYPE=RelWithDebInfo -G "Unix Makefiles" ../llvm-project-8.0.0.src/llvm-8.0.0.src

make
```

## 使い方
```
build/bin/clang -S -emit-llvm -Xclang -disable-O0-optnone -o prog.ll prog.c
build/bin/opt -licm -early-cse -mem2reg -S -o prog.licm.ll prog.ll
build/bin/opt -S -load build/lib/LLVMDirectiveInsertion.so -directiveinsertion -ditarget funcA,funcB -o prog.insert.ll prog.licm.ll
build/bin/opt -paramget -S -o prog.paramget.ll prog.insert.ll
build/bin/clang -fopenmp -o prog.parallel.out prog.paramget.ll
```

|命令|概要|
|---|---|
| build/bin/clang -S -emit-llvm -Xclang -disable-O0-optnone -o prog.ll prog.c | IRコードへ変換 |
| build/bin/opt -licm -early-cse -mem2reg -S -o prog.licm.ll prog.ll | 並列コード生成機能使用前の最適化 |
| build/bin/opt -S -load build/lib/LLVMDirectiveInsertion.so -directiveinsertion -ditarget funcA,funcB -o prog.insert.ll prog.licm.ll | 並列化指示文を挿入 |
| build/bin/opt -paramget -S -o prog.paramget.ll prog.insert.ll | 並列IRコードを生成 |
| build/bin/clang -fopenmp -I llvm-project-8.0.0.src/openmp/runtime/exports/common.dia.50.ompt.optional/include -o prog.parallel.out prog.paramget.ll | 並列IRコードから実行ファイルを生成 |

並列化指示文の挿入では、並列化する関数を`-ditarget`オプションで指定する  
例）kernel() -> `-ditarget kernel`

## エラー
### omp.h not found
```
-I llvm-project-8.0.0.src/openmp/runtime/exports/common.dia.50.ompt.optional/include
```
が必要

### cannot open shared object file
```
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:{llvmのワークスペース}/build/lib
```

## 課題
- AllPrivateDetectが動かない
  - ParamGet時にSegる
  - 理由は不明
- リダクション演算が加算にしか対応してない
  - 並列IRコード生成パス（ParamGet.cpp）は対応してるが、指示文挿入パス（DirectiveInsertion.cpp）が対応してない
    - 計算方法の解析が必要（addなのかsubなのかdivなのかmulなのか）
    - Map周りの改善が必要（現状使用しているReductionableMapだけでは足りない　加算以外の演算用のMapが別途必要）
- ループカウンタの初期値、増減まで見る解析
  - polybenchのtrmm
  - 未検討








# polybench-c-4.2
想定ディレクトリ
```
workdir
├── build
├── llvm-project-8.0.0.src
└── prog
    ├── シェルスクリプト
    ├── polybench-c-4.2
```
progは自分で作ってください

各プログラムの正誤や挿入されない指示文については柳田の日誌202412.mdに記載

結果の正誤を確認するためには、ダンプした結果をdiffするしかない

シェルスクリプトを参照

## オプション
- -DPOLYBENCH_DUMP_ARRAYS : 結果をダンプ
- -DPOLYBENCH_DUMP_TIME : 実行時間計測
