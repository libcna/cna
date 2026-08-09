# Minimal-link module probes and mechanical modularization gates
# (MODULARIZATION_PLAN.md §2.3/§4). Each probe is a tiny standalone consumer of exactly one
# CNA module alias; the paired ModuleLinkClosure_* test inspects the probe's generated link
# line and fails when a forbidden archive/library appears, turning every module's real
# dependency closure into a permanent contract. RendererIdentityRegistry mechanically pins
# the 41 public renderer identities across both registries.
#
# Native-only (the probes exist to inspect host link lines; Emscripten/Android links are
# covered by their own configurations' full builds).
if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT ANDROID)
    find_package(Python3 COMPONENTS Interpreter QUIET)

    function(cna_add_module_probe name module)
        add_executable(${name} tests/modules/${name}.cpp)
        target_link_libraries(${name} PRIVATE ${module})
        add_test(NAME ModuleProbe_${name} COMMAND ${name})
        set(_forbid "${ARGN}")
        if(Python3_Interpreter_FOUND AND _forbid)
            add_test(NAME ModuleLinkClosure_${name}
                COMMAND Python3::Interpreter
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_module_link_closure.py"
                    --build-dir "${CMAKE_BINARY_DIR}"
                    --target ${name}
                    --forbid "${_forbid}")
        endif()
    endfunction()

    # Math: nothing but the math archive and sharp-runtime -- no other CNA module, no SDL,
    # no backend, no networking, no FFmpeg.
    cna_add_module_probe(probe_math CNA::Math
        "libcna_(?!math)|libCNA_|libSDL3|libenet|libav|cna_backend_")

    # Core: logging/exceptions only; SDL3 is an accepted PRIVATE implementation detail of
    # Logger.cpp, everything else stays out.
    cna_add_module_probe(probe_core CNA::Core
        "libcna_(?!core|math)|libCNA_|libenet|libav|cna_backend_")

    # GraphicsCore: may pull math/core/input (declared XNA-semantic cycle) and the selected
    # backend's archive (factory edge), but no content/media/audio/runtime/devices/NOXNA/net.
    cna_add_module_probe(probe_graphics CNA::GraphicsCore
        "libcna_(content|media|audio|runtime|devices|noxna|storage)|libCNA_|libenet|libav")

    # Content: pulls graphics/audio/media by XNA design (type readers construct
    # Texture/SoundEffect/Song/Video), but no runtime layer, devices, NOXNA or networking.
    cna_add_module_probe(probe_content CNA::Content
        "libcna_(runtime|devices|noxna|storage)|libCNA_|libenet")

    # Runtime: the full framework below it, but never GamerServices/Net, devices or NOXNA.
    cna_add_module_probe(probe_runtime CNA::Runtime
        "libcna_(devices|noxna|storage)|libCNA_|libenet")

    # HEADLESS is the configuration that proves backend-neutral graphics needs no native
    # renderer SDK: the whole probe closure must stay free of every native graphics library.
    if(CNA_GRAPHICS_BACKEND STREQUAL "HEADLESS" AND Python3_Interpreter_FOUND)
        add_test(NAME ModuleLinkClosure_GraphicsNativeSdkFree
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_module_link_closure.py"
                --build-dir "${CMAKE_BINARY_DIR}"
                --target probe_graphics
                --forbid "vulkan|libGL|GLES|EGL|d3d|dxgi|ddraw|d2d1|bgfx|wgpu|webgpu|glide|gdi32|[Mm]agnum|[Dd]iligent|LLGL|skia|sokol|[Ww]icked|shaderc")
    endif()

    if(Python3_Interpreter_FOUND)
        add_test(NAME RendererIdentityRegistry
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_renderer_identities.py")
    endif()
endif()
