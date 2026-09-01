cmake_minimum_required(VERSION 3.14)
project(PolyBench)

set(PRG_LIST correlation;covariance;2mm;3mm;atax;bicg;doitgen;cholesky;durbin;gramschmidt;lu;ludcmp;trisolv;gemm;gemver;gesummv;symm;syr2k;syrk;trmm;deriche;floyd-warshall;nussinov;adi;fdtd-2d;heat-3d;jacobi-1d;jacobi-2d;seidel-2d;)
set(LOC_LIST datamining/correlation;datamining/covariance;linear-algebra/kernels/2mm;linear-algebra/kernels/3mm;linear-algebra/kernels/atax;linear-algebra/kernels/bicg;linear-algebra/kernels/doitgen;linear-algebra/solvers/cholesky;linear-algebra/solvers/durbin;linear-algebra/solvers/gramschmidt;linear-algebra/solvers/lu;linear-algebra/solvers/ludcmp;linear-algebra/solvers/trisolv;linear-algebra/blas/gemm;linear-algebra/blas/gemver;linear-algebra/blas/gesummv;linear-algebra/blas/symm;linear-algebra/blas/syr2k;linear-algebra/blas/syrk;linear-algebra/blas/trmm;medley/deriche;medley/floyd-warshall;medley/nussinov;stencils/adi;stencils/fdtd-2d;stencils/heat-3d;stencils/jacobi-1d;stencils/jacobi-2d;stencils/seidel-2d;)

list(LENGTH PRG_LIST BENCHLIST_LEN)
MATH(EXPR BENCHLIST_LAST_IDX "${BENCHLIST_LEN}-1")
message("len = ${BENCHLIST_LEN}")

set(POLYBENCH_C ${CMAKE_SOURCE_DIR}/utilities/polybench.c)

if(OPTIMIZE_FAST)
    set(CMAKE_C_FLAGS_RELEASE "-O3 -ffast-math -ffp-contract=fast")
else()
    set(CMAKE_C_FLAGS_RELEASE "-O2")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

foreach(loop_idx RANGE ${BENCHLIST_LAST_IDX})
    list(GET PRG_LIST ${loop_idx} prg)
    list(GET LOC_LIST ${loop_idx} loc)
    message("${prg} | ${loc}")
    add_executable(${prg} ${POLYBENCH_C} ${CMAKE_SOURCE_DIR}/${loc}/${prg}.c)
    include_directories(${prg} ${CMAKE_SOURCE_DIR}/utilities)
    target_link_libraries(${prg} m)
endforeach()

enable_testing()
add_test(NAME compile-polybench COMMAND bash build.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

foreach(loop_idx RANGE ${BENCHLIST_LAST_IDX})
    list(GET PRG_LIST ${loop_idx} prg)
    list(GET LOC_LIST ${loop_idx} loc)
    add_test(NAME test-${prg} COMMAND qemu-aarch64-static ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${prg})
    set_tests_properties(test-${prg} PROPERTIES DEPENDS compile-polybench)
endforeach()
