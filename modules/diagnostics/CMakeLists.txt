# SPDX-License-Identifier: MS-PL

file(GLOB CNA_DIAGNOSTICS_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_diagnostics Diagnostics ${CNA_DIAGNOSTICS_SOURCES})

if(CNA_BUILD_BENCHMARKS)
    add_executable(cna_diagnostics_benchmark benchmarks/DiagnosticsBenchmark.cpp)
    target_link_libraries(cna_diagnostics_benchmark PRIVATE
        cna_diagnostics CNA::ProjectOptions)
endif()
