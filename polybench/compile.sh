#!/bin/bash

#for benchmark in bicg
for benchmark in correlation covariance 2mm 3mm atax bicg doitgen mvt gemm gemver gesummv symm syr2k syrk trmm cholesky durbin gramschmidt lu ludcmp trisolv deriche floyd-warshall nussinov adi fdtd-2d heat-3d jacobi-1d jacobi-2d seidel-2d
do
  src_dir=${benchmark}
  if [ "$benchmark" = "correlation" ] \
    || [ "$benchmark" = "covariance" ] ; then
    src_dir=datamining/${src_dir}
  elif [ "$benchmark" = "gemm" ] \
    || [ "$benchmark" = "gemver" ] \
    || [ "$benchmark" = "gesummv" ] \
    || [ "$benchmark" = "symm" ]\
    || [ "$benchmark" = "syr2k" ] \
    || [ "$benchmark" = "syrk" ]\
    || [ "$benchmark" = "trmm" ] ; then
    src_dir=linear-algebra/blas/${src_dir}
  elif [ "$benchmark" = "2mm" ] \
    || [ "$benchmark" = "3mm" ] \
    || [ "$benchmark" = "atax" ] \
    || [ "$benchmark" = "bicg" ] \
    || [ "$benchmark" = "doitgen" ] \
    || [ "$benchmark" = "mvt" ] ; then
    src_dir=linear-algebra/kernels/${src_dir}
  elif [ "$benchmark" = "cholesky" ] \
    || [ "$benchmark" = "durbin" ] \
    || [ "$benchmark" = "gramschmidt" ]\
    || [ "$benchmark" = "lu" ] \
    || [ "$benchmark" = "ludcmp" ]\
    || [ "$benchmark" = "trisolv" ] ; then
    src_dir=linear-algebra/solvers/${src_dir}
  elif [ "$benchmark" = "deriche" ] \
    || [ "$benchmark" = "floyd-warshall" ]\
    || [ "$benchmark" = "nussinov" ]  ; then
    src_dir=medley/${src_dir}
  elif [ "$benchmark" = "adi" ]\
    || [ "$benchmark" = "fdtd-2d" ] \
    || [ "$benchmark" = "heat-3d" ] \
    || [ "$benchmark" = "jacobi-1d" ] \
    || [ "$benchmark" = "jacobi-2d" ] \
    || [ "$benchmark" = "seidel-2d" ] ; then
    src_dir=stencils/${src_dir}
  fi

  # hagane
  build/bin/clang -S -emit-llvm -Xclang -disable-O0-optnone -I prog/polybench-c-4.2/utilities/ -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o prog/hagane/src/${benchmark}.ll prog/polybench-c-4.2/${src_dir}/${benchmark}.c
  build/bin/opt -licm -mem2reg -early-cse -S -o prog/hagane/src/${benchmark}.licm.ll prog/hagane/src/${benchmark}.ll
  build/bin/opt -S -load build/lib/LLVMDirectiveInsertion.so -directiveinsertion -ditarget kernel -o prog/hagane/src/${benchmark}.directive.ll prog/hagane/src/${benchmark}.licm.ll 2> prog/hagane/log/${benchmark}.txt
  build/bin/opt -paramget -S -o prog/hagane/src/${benchmark}.paramget.ll prog/hagane/src/${benchmark}.directive.ll
  build/bin/clang -fopenmp -I llvm-project-8.0.0.src/openmp/runtime/exports/common.dia.50.ompt.optional/include -lm -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS  -o prog/hagane/src/${benchmark}.out prog/polybench-c-4.2/utilities/polybench.c prog/hagane/src/${benchmark}.paramget.ll

  # openmp(ソースコードレベルの並列化)
  build/bin/clang -fopenmp -I llvm-project-8.0.0.src/openmp/runtime/exports/common.dia.50.ompt.optional/include -I prog/polybench-c-4.2/utilities/ -lm -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o prog/clevel/src/${benchmark}.out prog/polybench-c-4.2/utilities/polybench.c prog/polybench-c-4.2/${src_dir}/${benchmark}.c
  
  # 逐次
  build/bin/clang -S -emit-llvm -Xclang -disable-O0-optnone -I prog/polybench-c-4.2/utilities/ -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o prog/seq/src/${benchmark}.ll prog/polybench-c-4.2/${src_dir}/${benchmark}.c
  build/bin/opt -licm -mem2reg -early-cse -S -o prog/seq/src/${benchmark}.licm.ll prog/seq/src/${benchmark}.ll
  build/bin/clang -I prog/polybench-c-4.2/utilities/ -lm -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o prog/seq/src/${benchmark}.out prog/polybench-c-4.2/utilities/polybench.c prog/seq/src/${benchmark}.licm.ll
done
