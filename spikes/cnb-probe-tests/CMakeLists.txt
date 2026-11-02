# SPDX-License-Identifier: MS-PL

add_executable(cna_cnb_mmap_probe ../cnb-mmap-spike/measure.cpp)
add_executable(cna_cnb_mmap_probe_faults ../cnb-mmap-spike/measure.cpp faults.cpp)
foreach(target cna_cnb_mmap_probe cna_cnb_mmap_probe_faults)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_options(${target} PRIVATE -O2)
endforeach()
target_link_options(cna_cnb_mmap_probe_faults PRIVATE
    -Wl,--wrap=open -Wl,--wrap=open64 -Wl,--wrap=read -Wl,--wrap=mmap -Wl,--wrap=mmap64)
target_compile_definitions(cna_cnb_mmap_probe_faults PRIVATE _LARGEFILE64_SOURCE=1)
set(probe_arguments --mmap $<TARGET_FILE:cna_cnb_mmap_probe>
    --mmap-faults $<TARGET_FILE:cna_cnb_mmap_probe_faults>)
set(probe_targets cna_cnb_mmap_probe cna_cnb_mmap_probe_faults)

find_package(ZLIB QUIET)
find_path(CNA_CNB_PROBE_ZSTD_INCLUDE_DIR zstd.h)
if(TARGET CNA::Zstd AND ZLIB_FOUND AND CNA_CNB_PROBE_ZSTD_INCLUDE_DIR)
    add_executable(cna_cnb_compression_probe ../cnb-compression-spike/measure.cpp)
    add_executable(cna_cnb_compression_probe_faults ../cnb-compression-spike/measure.cpp faults.cpp)
    foreach(target cna_cnb_compression_probe cna_cnb_compression_probe_faults)
        target_compile_features(${target} PRIVATE cxx_std_23)
        target_compile_options(${target} PRIVATE -O2)
        target_include_directories(${target} PRIVATE "${CNA_CNB_PROBE_ZSTD_INCLUDE_DIR}")
        target_link_libraries(${target} PRIVATE CNA::Zstd ZLIB::ZLIB)
    endforeach()
    target_compile_definitions(cna_cnb_compression_probe_faults PRIVATE CNA_PROBE_COMPRESSION_FAULTS=1)
    target_link_options(cna_cnb_compression_probe_faults PRIVATE
        -Wl,--wrap=ZSTD_compress -Wl,--wrap=ZSTD_decompress
        -Wl,--wrap=compress2 -Wl,--wrap=uncompress)
    list(APPEND probe_arguments --compression $<TARGET_FILE:cna_cnb_compression_probe>
        --compression-faults $<TARGET_FILE:cna_cnb_compression_probe_faults>)
    list(APPEND probe_targets cna_cnb_compression_probe cna_cnb_compression_probe_faults)
endif()
add_dependencies(CnaTests ${probe_targets})
add_test(NAME CnbMeasurementProbeTests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_probes.py" ${probe_arguments})
set_tests_properties(CnbMeasurementProbeTests PROPERTIES LABELS "content;cnb;tools" TIMEOUT 120)
