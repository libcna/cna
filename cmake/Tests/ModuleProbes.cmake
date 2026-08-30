# Minimal-link module probes and mechanical modularization gates
# (plans/MODULARIZATION_PLAN.md §2.3/§4). Each probe is a tiny standalone consumer of exactly one
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
    # plans/plan_runtimerenderer.md RTR-P9-21: deliberately ALSO requires single-renderer mode. This
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

    # cmake/UnitTests.cmake applies the repository-wide exit-77 skip convention before this
    # file registers the module link-closure tests. Apply it to this later registration batch as
    # well, so generators without CMakeFiles/<target>.dir/link.txt (notably Ninja) report the
    # checker script's documented exit 77 as SKIPPED instead of FAILED.
    get_property(_cna_link_closure_tests DIRECTORY PROPERTY TESTS)
    list(FILTER _cna_link_closure_tests INCLUDE REGEX "^ModuleLinkClosure_")
    if(_cna_link_closure_tests)
        set_tests_properties(${_cna_link_closure_tests} PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    if(Python3_Interpreter_FOUND)
        add_test(NAME RendererIdentityRegistry
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_renderer_identities.py")

        # plans/plan_binding.md CBIND-043: the C API coverage matrix is a GATE, not a report.
        #
        # The complete inventory is generated from every public Microsoft/** and CNA/** header.
        # Its compact tracked summary carries counts and a hash, so a new public C++ symbol without
        # a mapping makes the gate stale without committing the multi-megabyte per-symbol report.
        #
        # Deliberately NOT inside if(CNA_BUILD_C_API): the check reads headers and mappings and
        # builds nothing, and the rule it enforces is about the C++ surface. Gating it on the C API
        # being built would mean the ordinary build -- the one most changes are made in -- never
        # notices that a new public symbol went unmapped.
        add_test(NAME CApiCoverageMatrix
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/c-api/generate_coverage_inventory.py" --check)

        # plans/plan_binding.md CBIND-038: the pure-C compatibility matrix, also a gate.
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

        # plans/plan_binding.md CBIND-042A: the known-limitations matrix, generated from the same
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

        # plans/plan_binding.md CBIND-064: the export count that appears in PROSE, which no gate read.
        #
        # `abi_baseline.json` measures how many `cna_*` symbols the library exports, and
        # CApiAbiBaseline fails when that changes without review. Four sentences repeat the number
        # -- in ABI_VERSIONING.md, CONSUMING.md, LIMITATIONS.md and RELEASE_GATE.md -- and nothing
        # checked them: they said 2,720 for months against a measured 2,838, were corrected by hand
        # on 2026-08-17, and went stale again at the very next slice that added exports. The plan
        # recorded that as "nothing prevents it happening again".
        #
        # Verified to catch it by reinstating the historical 2,720 in CONSUMING.md, which turns
        # this from pass to a failure naming the file, the line and both numbers. Build-free: it
        # reads the baseline and the documents.
        add_test(NAME CApiDocExportCounts
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/c-api/check_doc_export_counts.py" --check)

        # plans/plan_binding.md CBIND-065: does anything actually CALL each route?
        #
        # The coverage matrix answers a different question -- it maps every public C++ symbol to a
        # C route and to a *rule's* test description, so a rule covering twenty symbols credits its
        # test to all twenty even where the test exercises twelve. That is how 78 exported routes
        # came to have no caller at all while their matrix rows read implemented with evidence:
        # the whole gyroscope acquisition surface, 49 media-library routes, and the streaming
        # wave-bank constructor whose header made a promise no test had ever checked (it was
        # wrong -- see xact.h).
        #
        # This gate asks the mechanical question instead and ratchets the answer. It stands at 0
        # uncovered, so a route arriving without a caller fails the build rather than waiting for
        # someone to notice. Build-free, like the gates above.
        add_test(NAME CApiRouteTestCoverage
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/c-api/check_route_test_coverage.py" --check)

        # plans/plan_binding.md CBIND-067: the generated CNA_Bool contract test must not go stale.
        #
        # The test itself compiles and runs beside the other smoke tests; this checks that the
        # checked-in file still matches what the headers say, so a route declared with a new flag
        # parameter cannot sit uncovered behind a generator nobody re-ran. Build-free.
        add_test(NAME CApiBoolContractCurrent
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/c-api/generate_bool_contract_test.py" --check)

        # plans/plan_binding.md CBIND-042B: the experimental release gate.
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

        # plans/plan_binding.md CBIND-039: the ABI layout and export baseline.
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

        # CBIND-076: the baseline above compares the export list with a recorded baseline, so export
        # drift is caught -- but a header declaring a route the library never exports leaves both
        # halves self-consistent while a consumer written against the header fails at its call site.
        # A count cannot see that; the set difference can. Reported by the C# binding, which runs
        # the same comparison on its own side.
        if(TARGET cna_c_api AND UNIX AND NOT APPLE)
            add_test(NAME CApiDeclaredExports
                COMMAND Python3::Interpreter
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/c-api/check_declared_exports.py"
                    --library $<TARGET_FILE:cna_c_api>)
        endif()

        # plans/plan_runtimerenderer.md RTR-P1-6/P3-17/P12-15: renderer-specific production
        # decisions stay behind descriptors or virtuals, and every renderer family keeps exactly
        # one descriptor translation unit.
        add_test(NAME RuntimeRendererDiscipline
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_runtime_renderer_discipline.py")

        # plans/plan_runtimerenderer.md RTR-P6-19/P12-15: keep the configure-time combination
        # restrictions synchronized with their public documentation.
        add_test(NAME RendererCombinationRegistry
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_renderer_combinations.py")

        # plans/plan_runtimerenderer.md RTR-P6-3: keeps RENDERER_TARGET from going back to being a
        # scalar that families read out of a global. The rule it enforces cannot be checked by
        # building: in a single-renderer build a wrong read gives the right answer, because the
        # default is the only renderer there is. Only a multi-renderer build would notice, and
        # only for the families that build there -- so the invariant is checked directly instead.
        add_test(NAME RendererTargetDiscipline
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_renderer_target_discipline.py")
    endif()

    # plans/plan_runtimerenderer.md RTR-P6-1: CNA_GRAPHICS_RENDERER names the DEFAULT renderer chosen
    # from CNA_GRAPHICS_RENDERERS, so it must be a member of that list.
    #
    # Each case runs cmake/RendererDefaultSelection.cmake for real in `cmake -P` script mode, which
    # is why the resolution was split into its own file. Testing it by configuring the whole
    # project three times would take minutes and write three build trees; this takes milliseconds
    # and exercises the same code. Same pattern as CnaAudioPlatformSelection_* above.
    #
    # The REJECT case is the one that matters: the contract used to be enforced by substituting the
    # list's first entry and printing a STATUS line, so an invalid default produced a build that
    # succeeded and then answered every later renderer question about a renderer nobody asked for.
    set(_cna_renderer_default_cases
        # name|renderer|set (commas, or EMPTY)|outcome|expected text
        "SingleRenderer|HEADLESS|EMPTY|ACCEPT|renderer set -- HEADLESS"
        "DefaultInsideTheSet|SOFTWARE|HEADLESS,SOFTWARE,STUB|ACCEPT|renderer set -- SOFTWARE"
        "DefaultIsListedFirst|STUB|HEADLESS,SOFTWARE,STUB|ACCEPT|renderer set -- STUB"
        "DefaultOutsideTheSet|SOFTWARE|OPENGL4,VULKAN|REJECT|is not a member of")
    foreach(_cna_renderer_default_case IN LISTS _cna_renderer_default_cases)
        string(REPLACE "|" ";" _cna_renderer_default_fields "${_cna_renderer_default_case}")
        list(GET _cna_renderer_default_fields 0 _cna_case_name)
        list(GET _cna_renderer_default_fields 1 _cna_case_renderer)
        list(GET _cna_renderer_default_fields 2 _cna_case_set)
        list(GET _cna_renderer_default_fields 3 _cna_case_outcome)
        list(GET _cna_renderer_default_fields 4 _cna_case_expected)
        add_test(NAME CnaRendererDefaultSelection_${_cna_case_name}
            COMMAND ${CMAKE_COMMAND}
                -DCNA_RENDERER_DEFAULT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/RendererDefaultSelection.cmake
                -DCNA_RENDERER_DEFAULT_RENDERER=${_cna_case_renderer}
                -DCNA_RENDERER_DEFAULT_SET=${_cna_case_set}
                -DCNA_RENDERER_DEFAULT_OUTCOME=${_cna_case_outcome}
                -DCNA_RENDERER_DEFAULT_EXPECTED=${_cna_case_expected}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Tests/RendererDefaultCase.cmake)
        set_tests_properties(CnaRendererDefaultSelection_${_cna_case_name}
            PROPERTIES LABELS "renderer;configuration")
    endforeach()
endif()
