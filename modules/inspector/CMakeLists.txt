# SPDX-License-Identifier: MS-PL

# Inspector is deliberately not part of the CNA umbrella. A game must opt into both this build
# option and CNA::Inspector on its own link line; otherwise no Inspector code reaches the binary.
if(CNA_BUILD_INSPECTOR)
    if(EMSCRIPTEN OR ANDROID OR CNA_APPLE_IOS)
        message(FATAL_ERROR
            "CNA_BUILD_INSPECTOR currently supports desktop Windows, Linux, macOS, and POSIX "
            "desktop targets. Use the compiled-out default on Web, Android, and iOS.")
    endif()

    find_package(Threads REQUIRED)
    file(GLOB CNA_INSPECTOR_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
    list(FILTER CNA_INSPECTOR_SOURCES EXCLUDE REGEX "/InspectorMain\\.cpp$")
    cna_add_module(cna_inspector Inspector ${CNA_INSPECTOR_SOURCES})
    target_link_libraries(cna_inspector PUBLIC cna_diagnostics cna_core_headers
        PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(cna_inspector PRIVATE ws2_32 bcrypt)
    endif()

    add_executable(cna-inspector src/InspectorMain.cpp)
    target_link_libraries(cna-inspector PRIVATE cna_inspector CNA::ProjectOptions)
    set_target_properties(cna-inspector PROPERTIES OUTPUT_NAME "cna-inspector")

    # A game that opts in exactly as docs/inspector.md describes, so the agent, the bridge and the
    # browser UI can be exercised end to end against real engine data. It stays a console
    # program on Windows because it prints the port and token the bridge needs.
    if(CNA_BUILD_EXAMPLES)
        add_executable(cna_inspector_demo examples/inspector_demo.cpp)
        target_link_libraries(cna_inspector_demo PRIVATE CNA CNA::Inspector SHARP_RUNTIME)
        set_target_properties(cna_inspector_demo PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    endif()

    if(CNA_BUILD_BENCHMARKS)
        add_executable(cna_inspector_benchmark benchmarks/InspectorBenchmark.cpp)
        target_link_libraries(cna_inspector_benchmark PRIVATE
            cna_inspector CNA::ProjectOptions Threads::Threads)
    endif()
endif()

# This target exists in both compiled-out and compiled-in benchmark configurations. It has no
# Inspector include or link dependency and is the zero-overhead control executable.
if(CNA_BUILD_BENCHMARKS)
    add_executable(cna_inspector_compiled_out_benchmark
        benchmarks/InspectorCompiledOutBenchmark.cpp)
    target_link_libraries(cna_inspector_compiled_out_benchmark PRIVATE CNA::ProjectOptions)
endif()
