# Minimal-link module probes and mechanical modularization gates
# (MODULARIZATION_PLAN.md §2.3/§4). Each probe is a tiny standalone consumer of exactly one
# CNA module alias; the paired ModuleLinkClosure_* test inspects the probe's generated link
# line and fails when a forbidden archive/library appears, turning every module's real
# dependency closure into a permanent contract. RendererIdentityRegistry mechanically pins
# the 42 public renderer identities across both registries.
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
    # no renderer, no networking, no FFmpeg.
    cna_add_module_probe(probe_math CNA::Math
        "libcna_(?!math)|libCNA_|libSDL3|libenet|libav|cna_renderer_")

    # Core: logging/exceptions only; SDL3 is an accepted PRIVATE implementation detail of
    # Logger.cpp, everything else stays out.
    cna_add_module_probe(probe_core CNA::Core
        "libcna_(?!core|math)|libCNA_|libenet|libav|cna_renderer_")

    # GraphicsCore: may pull math/core/input (declared XNA-semantic cycle) and the selected
    # renderer's archive (factory edge), but no content/media/audio/runtime/devices/CNAEXT/net.
    cna_add_module_probe(probe_graphics CNA::GraphicsCore
        "libcna_(content|media|audio|runtime|devices|cnaext|storage)|libCNA_|libenet|libav")

    # Content: pulls graphics/audio/media by XNA design (type readers construct
    # Texture/SoundEffect/Song/Video), but no runtime layer, devices, CNAEXT or networking.
    cna_add_module_probe(probe_content CNA::Content
        "libcna_(runtime|devices|cnaext|storage)|libCNA_|libenet")

    # Runtime: the full framework below it, but never GamerServices/Net, devices or CNAEXT.
    cna_add_module_probe(probe_runtime CNA::Runtime
        "libcna_(devices|cnaext|storage)|libCNA_|libenet")

    # Input: graphics-core/math/core through the declared graphics<->input cycle (plus the
    # selected renderer via the factory edge), but nothing above it.
    cna_add_module_probe(probe_input CNA::Input
        "libcna_(content|media|audio|runtime|devices|storage|graphics_ext)|libCNA_|libenet|libav")

    # Audio: media (declared audio<->media pump cycle), input (dispatcher TouchPanel pump),
    # graphics-core via media, math/core -- but no content, runtime, devices or networking.
    cna_add_module_probe(probe_audio CNA::Audio
        "libcna_(content|runtime|devices|storage|graphics_ext)|libCNA_|libenet")

    # Media: the same accepted cycle closure as audio, nothing above it.
    cna_add_module_probe(probe_media CNA::Media
        "libcna_(content|runtime|devices|storage|graphics_ext)|libCNA_|libenet")

    # Storage: nothing but the storage archive and sharp-runtime. The PlayerIndex/CNAEXT
    # dependency on the core module is headers-only (cna_core_headers), so even the core
    # archive must stay off the link line.
    cna_add_module_probe(probe_storage CNA::Storage
        "libcna_(?!storage)|libCNA_|libenet|libav|cna_renderer_")

    # Devices (XNA base): the runtime stack below it, but never an extension module or
    # networking.
    cna_add_module_probe(probe_devices CNA::Devices
        "libcna_(devices_ext|graphics_ext)|libCNA_|libenet")

    # DevicesExt: the runtime stack below it, but never the XNA device base (dependency
    # direction devices-ext -> devices is forbidden by design) nor networking.
    cna_add_module_probe(probe_devices_ext CNA::DevicesExt
        "libcna_devices\\.|libcna_graphics_ext|libCNA_|libenet")

    # GraphicsExt (the former cna_noxna STATIC library's implementation): graphics-core closure only.
    cna_add_module_probe(probe_graphics_ext CNA::GraphicsExt
        "libcna_(content|media|audio|runtime|devices|storage)|libCNA_|libenet")

    # CnaExt compatibility umbrella: must COMPOSE both extension modules. The probe runs like
    # every other; its closure gate needs --require, which cna_add_module_probe does not
    # pass, so the composition gate is registered directly below.
    cna_add_module_probe(probe_cnaext CNA::CnaExt)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ModuleLinkClosure_CnaExtComposition
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_module_link_closure.py"
                --build-dir "${CMAKE_BINARY_DIR}"
                --target probe_cnaext
                --forbid "libCNA_|libenet"
                --require "libcna_graphics_ext" --require "libcna_devices_ext")
    endif()

    if(CNA_ENABLE_NET)
        # Net: the networking closure (gamer-services -> runtime stack + enet), but never a
        # device module or an extension module. FFmpeg is legitimately present through the
        # runtime stack's media module, so it is deliberately NOT forbidden here.
        cna_add_module_probe(probe_net CNA::Net
            "libcna_(devices|graphics_ext)")
        if(Python3_Interpreter_FOUND)
            add_test(NAME ModuleLinkClosure_NetHasENet
                COMMAND Python3::Interpreter
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_module_link_closure.py"
                    --build-dir "${CMAKE_BINARY_DIR}"
                    --target probe_net
                    --forbid "libcna_(devices|graphics_ext)"
                    --require "libenet")
        endif()
    endif()

    # HEADLESS is the configuration that proves renderer-neutral graphics needs no native
    # renderer SDK: the whole probe closure must stay free of every native graphics library.
    # The same must hold for the content pipeline (no renderer/native SDK through the type
    # readers) and for both extension modules.
    if(CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS" AND Python3_Interpreter_FOUND)
        set(_cna_native_sdk_forbid
            "vulkan|libGL|GLES|EGL|d3d|dxgi|ddraw|d2d1|bgfx|wgpu|webgpu|glide|gdi32|[Mm]agnum|[Dd]iligent|LLGL|skia|sokol|[Ww]icked|shaderc")
        foreach(_cna_sdkfree_probe IN ITEMS probe_graphics probe_content probe_graphics_ext probe_devices_ext)
            add_test(NAME ModuleLinkClosure_NativeSdkFree_${_cna_sdkfree_probe}
                COMMAND Python3::Interpreter
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_module_link_closure.py"
                    --build-dir "${CMAKE_BINARY_DIR}"
                    --target ${_cna_sdkfree_probe}
                    --forbid "${_cna_native_sdk_forbid}")
        endforeach()
        # Historical name kept so the existing gate's registration survives by NAME.
        add_test(NAME ModuleLinkClosure_GraphicsNativeSdkFree
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_module_link_closure.py"
                --build-dir "${CMAKE_BINARY_DIR}"
                --target probe_graphics
                --forbid "${_cna_native_sdk_forbid}")
    endif()

    # VULKAN closure: the selected renderer's native SDK and nothing from any other family.
    if(CNA_GRAPHICS_RENDERER STREQUAL "VULKAN" AND Python3_Interpreter_FOUND)
        add_test(NAME ModuleLinkClosure_VulkanRendererClosure
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_module_link_closure.py"
                --build-dir "${CMAKE_BINARY_DIR}"
                --target probe_graphics
                --forbid "d3d|dxgi|ddraw|d2d1|bgfx|wgpu|webgpu|glide|gdi32|[Mm]agnum|[Dd]iligent|LLGL|skia|sokol|[Ww]icked|GLESv1|shaderc"
                --require "vulkan")
    endif()

    if(Python3_Interpreter_FOUND)
        add_test(NAME RendererIdentityRegistry
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_renderer_identities.py")

        # plan_binding.md CBIND-043: the C API coverage matrix is a GATE, not a report.
        #
        # docs/c-api/COVERAGE.md is generated from every public Microsoft/** and CNA/** header, so
        # a new public C++ symbol that arrives without its C API row makes the checked-in inventory
        # stale. Verified to catch exactly that: adding one declaration to CNAHelper.hpp turns this
        # from pass to "Coverage inventory is stale", naming the command that fixes it.
        #
        # Deliberately NOT inside if(CNA_BUILD_C_API): the check reads headers and the matrix and
        # builds nothing, and the rule it enforces is about the C++ surface. Gating it on the C API
        # being built would mean the ordinary build -- the one most changes are made in -- never
        # notices that a new public symbol went unmapped.
        add_test(NAME CApiCoverageMatrix
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/c-api/generate_coverage_inventory.py" --check)
    endif()
endif()
