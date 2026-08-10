if(CNA_BUILD_TESTS AND WIN32 AND CNA_GRAPHICS_RENDERER STREQUAL "GLIDE")
    # AUD-006 starts below the full CNA layer on purpose: this contract test builds a tiny x86
    # fake DLL and a loader client directly, so it stays runnable even when an unrelated CNA or
    # sharp-runtime executable dependency is not currently i686-compatible.
    add_library(cna_glide_fake_dll SHARED
        modules/renderers/glide/tests/CNA/Internal/Renderers/Glide/FakeGlide3xDll.cpp)
    set_target_properties(cna_glide_fake_dll PROPERTIES PREFIX "" OUTPUT_NAME "fake_glide3x")

    add_executable(cna_glide_abi_loader_test
        modules/renderers/glide/tests/CNA/Internal/Renderers/Glide/GlideAbiLoaderTests.cpp)
    # The ABI declarations live in the glide renderer module's always-defined header
    # interface (GlideAbi.hpp is self-contained: loader decls + <cstdint> only).
    target_link_libraries(cna_glide_abi_loader_test PRIVATE cna_renderer_glide_headers)
    add_dependencies(cna_glide_abi_loader_test cna_glide_fake_dll)
    if(MINGW)
        # The contract DLL must be independently loadable by Wine; unlike a normal executable,
        # cna_copy_mingw_runtime cannot satisfy a DLL's own libgcc/libstdc++ imports before its
        # DllMain/load stage.
        target_link_options(cna_glide_fake_dll PRIVATE -static-libgcc -static-libstdc++)
        target_link_options(cna_glide_abi_loader_test PRIVATE -static-libgcc -static-libstdc++)
        cna_copy_mingw_runtime(cna_glide_abi_loader_test)
        add_custom_command(TARGET cna_glide_abi_loader_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:cna_glide_fake_dll>
                $<TARGET_FILE_DIR:cna_glide_abi_loader_test>)
    endif()
    if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
        add_test(NAME GlideAbiLoaderContract
            COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:cna_glide_abi_loader_test>)
        set_tests_properties(GlideAbiLoaderContract PROPERTIES
            WORKING_DIRECTORY $<TARGET_FILE_DIR:cna_glide_abi_loader_test>)
    elseif(CMAKE_CROSSCOMPILING AND MINGW)
        find_program(CNA_GLIDE_WINE_EXECUTABLE NAMES wine)
        if(CNA_GLIDE_WINE_EXECUTABLE)
            add_test(NAME GlideAbiLoaderContract
                COMMAND ${CNA_GLIDE_WINE_EXECUTABLE} $<TARGET_FILE:cna_glide_abi_loader_test>)
            set_tests_properties(GlideAbiLoaderContract PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:cna_glide_abi_loader_test>)
        endif()
    elseif(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME GlideAbiLoaderContract COMMAND $<TARGET_FILE:cna_glide_abi_loader_test>)
    endif()

    # This target intentionally has no add_test(): it executes real Glide commands and needs an
    # externally supplied emulator DLL. See docs/glide-renderer.md for the manual dgVoodoo2 loop.
    add_executable(cna_glide_smoke examples/glide_smoke_test.cpp)
    target_link_libraries(cna_glide_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_glide_smoke PRIVATE SDL3::SDL3main)
    endif()
    if(MINGW)
        target_link_options(cna_glide_smoke PRIVATE -static-libgcc -static-libstdc++)
        target_link_options(cna_glide_smoke PRIVATE -Wl,--allow-multiple-definition)
        cna_copy_mingw_runtime(cna_glide_smoke)
        cna_copy_sdl_runtime(cna_glide_smoke)
    endif()
endif()
