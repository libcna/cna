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
    # plan_runtimerenderer.md RTR-P9-21: deliberately ALSO requires single-renderer mode. This
    # asserts the probe closure links no native graphics SDK at all, which is a true statement about
    # a HEADLESS-only build and a FALSE one about a multi-renderer build that happens to default to
    # HEADLESS -- such a binary legitimately links Vulkan, Skia or whatever else it was built with.
    # Converting this to list membership would have made it fail for a correct build.
    if(CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS" AND NOT CNA_MULTI_RENDERER
            AND Python3_Interpreter_FOUND)
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
    # Same reasoning as the HEADLESS closure above: "the selected renderer's SDK and nothing from
    # any other family" is only a meaningful claim when exactly one renderer is compiled in.
    if(CNA_GRAPHICS_RENDERER STREQUAL "VULKAN" AND NOT CNA_MULTI_RENDERER
            AND Python3_Interpreter_FOUND)
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

        # plan_runtimerenderer.md RTR-P6-3: keeps RENDERER_TARGET from going back to being a
        # scalar that families read out of a global. The rule it enforces cannot be checked by
        # building: in a single-renderer build a wrong read gives the right answer, because the
        # default is the only renderer there is. Only a multi-renderer build would notice, and
        # only for the families that build there -- so the invariant is checked directly instead.
        add_test(NAME RendererTargetDiscipline
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_renderer_target_discipline.py")
    endif()
endif()
