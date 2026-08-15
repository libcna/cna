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
            "vulkan|libGL|GLES|EGL|d3d|dxgi|ddraw|d2d1|bgfx|wgpu|webgpu|glide|gdi32|[Mm]agnum|[Dd]iligent|LLGL|IGLLibrary|skia|sokol|[Ww]icked|shaderc")
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
                --forbid "d3d|dxgi|ddraw|d2d1|bgfx|wgpu|webgpu|glide|gdi32|[Mm]agnum|[Dd]iligent|LLGL|IGLLibrary|skia|sokol|[Ww]icked|GLESv1|shaderc"
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

        # plan_binding.md CBIND-038: the pure-C compatibility matrix, also a gate.
        #
        # The declaration and the published matrix must not drift apart, for the same reason the
        # coverage matrix must not: a matrix is read as evidence, so one that lags its declaration
        # is worse than none. Build-free, like the coverage check above.
        add_test(NAME CApiCompatibilityMatrix
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/c-api/generate_compatibility_matrix.py" --check)

        # The matrix itself: every public header compiled on its own, and the umbrella twice, in
        # every declared language mode of every installed toolchain. A header that is not
        # self-contained, or that reaches for a construct newer than the C99 floor, fails here --
        # verified by reinstating a duplicate typedef, which turns the four C99 cells red while the
        # C11 and later cells stay green.
        #
        # Compiles nothing of CNA itself: it needs the public headers and a compiler, which is what
        # lets it run in the ordinary build rather than only where the C API is enabled.
        add_test(NAME CApiHeaderCompatibility
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/c-api/generate_compatibility_matrix.py" --run)

        # plan_binding.md CBIND-042A: the known-limitations matrix, generated from the same
        # inventory the coverage gate checks.
        #
        # A limitations document is the one document a consumer reads to decide whether to adopt an
        # ABI, and the one most likely to rot: nothing breaks when it goes stale. Three rules are
        # enforced mechanically -- every unmapped reason falls under a declared theme, no deferral
        # names a task the plan records as finished, and the counts come from the inventory rather
        # than from prose. Build-free, like the coverage and compatibility gates beside it.
        #
        # It found two stale deferrals on its first run: one to CBIND-035 whose work never landed,
        # and one to CBIND-037 whose work did.
        add_test(NAME CApiLimitations
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/c-api/generate_limitations.py" --check)

        # plan_binding.md CBIND-042B: the experimental release gate.
        #
        # A release gate written as prose is a list of things somebody once believed. This one is a
        # declaration plus a measurement, and the check fails when they disagree **in either
        # direction**: a criterion recorded as met that no longer is, and -- the failure mode a
        # release gate actually dies of -- one recorded as blocked that has quietly become met,
        # because nobody re-reads a document that says "not yet".
        #
        # It currently reports NOT READY, and correctly: every mechanical criterion is met, and two
        # packaging questions await a decision that no implementer may make alone. That is the gate
        # working, not the gate failing, so the test passes while the verdict stands.
        add_test(NAME CApiReleaseGate
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/c-api/check_release_gate.py" --check)

        # plan_binding.md CBIND-039: the ABI layout and export baseline.
        #
        # A generated probe reports what the compiler *actually* laid out -- every struct size,
        # alignment and field offset, every scalar typedef's width, every constant's value -- and it
        # is held against tools/c-api/abi_baseline.json. Additions are permitted and re-recorded; a
        # moved field, a changed value or a vanished export is named as an ABI break.
        #
        # The header half needs no build, so it runs here for the same reason the coverage matrix
        # does: gating it on the C API being enabled would mean the ordinary build -- the one most
        # changes are made in -- never notices a struct field moved. Verified to catch both arms:
        # swapping CNA_Point's two fields reports them as moved, and an added constant is classified
        # as an addition rather than a break.
        add_test(NAME CApiAbiHeaderBaseline
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/c-api/generate_abi_baseline.py" --check
                --compiler "${CMAKE_C_COMPILER}")

        # The other half: the library that was actually built, asked for its own ABI version and its
        # dynamic export list. ELF-only, because reading exports is what makes the symbol half
        # meaningful, and only where the C API target exists.
        if(TARGET cna_c_api AND UNIX AND NOT APPLE)
            set(_abi_baseline_flags "")
            if(CNA_SANITIZE)
                # A sanitized shared object refuses to load into a probe that was not built the same
                # way, so the sanitized tree runs the gate with its own flags rather than skipping it.
                list(APPEND _abi_baseline_flags "--compile-flag=-fsanitize=${CNA_SANITIZE}")
            endif()
            add_test(NAME CApiAbiBaseline
                COMMAND Python3::Interpreter
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/c-api/generate_abi_baseline.py" --check
                    --library $<TARGET_FILE:cna_c_api>
                    --compiler "${CMAKE_C_COMPILER}"
                    ${_abi_baseline_flags})
        endif()

        # plan_runtimerenderer.md RTR-P1-6/P3-17/P12-15: renderer-specific production
        # decisions stay behind descriptors or virtuals, and every renderer family keeps exactly
        # one descriptor translation unit.
        add_test(NAME RuntimeRendererDiscipline
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_runtime_renderer_discipline.py")

        # plan_runtimerenderer.md RTR-P6-19/P12-15: keep the configure-time combination
        # restrictions synchronized with their public documentation.
        add_test(NAME RendererCombinationRegistry
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_renderer_combinations.py")

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
