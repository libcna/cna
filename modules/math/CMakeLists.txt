file(GLOB CNA_MATH_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_math Math ${CNA_MATH_SOURCES})
cna_link_sharp_runtime(cna_math PUBLIC Core.Base)
# The math headers use the CNAEXT marker macro from CNA/CNAHelper.hpp -- a headers-only
# dependency on the core module (no core symbol is referenced), so the link closure stays
# "math + sharp-runtime only" exactly as the minimal-link gate pins it.
target_link_libraries(cna_math PUBLIC cna_core_headers)
# XNA's matrix and vector arithmetic is x87 code, which has no fused multiply-add, and the measured
# oracle pins its bits down to the sign of an exact zero (MatrixOracleTests.cpp, XNASWEEP-172).
# Clang 14+ and GCC contract `a * b + c` into an FMA by default wherever the target has one, which
# every arm64 machine does: on Apple silicon Matrix::Invert answered +0.0 for 48 entries XNA answers
# -0.0, because the negated cofactor sum was folded into a single FNMADD. An x86-64 build without
# -mfma has no FMA to contract into, so this changes nothing there.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cna_math PRIVATE -ffp-contract=off)
endif()
