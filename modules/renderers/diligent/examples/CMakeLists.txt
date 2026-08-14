# diligent renderer example/test registration (module-local; this file is the
# former cmake/Tests/DiligentTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plan_diligent.md DILIGENT-16: real-GPU pixel proof for the Diligent Engine renderer.
# Skips (exit 77) rather than fails when no device can be created, since Diligent picks its
# native API at runtime and a headless machine may have none of them.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND CNA_GRAPHICS_RENDERER STREQUAL "DILIGENT")
    enable_testing()

    # plan_diligent.md DILIGENT-16. Same link shape as the other renderers' GPU test binaries; the
    # extra cna_link_diligent() call is needed because cna_link_diligent() keeps DiligentCore
    # PRIVATE to the renderer target (mirroring how the WebGPU renderer keeps wgpu-native private).
    macro(cna_diligent_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                CNA
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA ${RENDERER_TARGET} SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # plan_diligent.md DILIGENT-67: registers a Diligent CTest TWICE -- once under the default
    # preference order (today: Vulkan first, see GetDeviceTypePreferenceOrder()) and again with
    # CNA_DILIGENT_DEVICE=opengl forced. Without this, `ctest -R "^Diligent"` only ever exercises
    # whichever device type sorts first, silently never running the OpenGL device type at all
    # unless a human remembers to export the override by hand -- exactly how the OpenGL-specific
    # defects DILIGENT-30/DILIGENT-66 found went unnoticed for as long as they did. Same
    # executable, same properties, twice the coverage; both variants read the override via
    # std::getenv("CNA_DILIGENT_DEVICE") inside the renderer's own device-creation loop, which
    # every construction path (GraphicsDeviceManager or a test's own direct IGraphicsRenderer
    # construction, e.g. Diligent_BackbufferReadbackBounds) goes through identically.
    macro(cna_register_diligent_test name command)
        cna_register_renderer_test(NAME ${name} COMMAND ${command}
            TIMEOUT 90 LABELS "GraphicsSmoke;Diligent"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
            SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA Diligent smoke")
        cna_register_renderer_test(NAME ${name}_OpenGL COMMAND ${command}
            TIMEOUT 90 LABELS "GraphicsSmoke;Diligent;DiligentOpenGL"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_DILIGENT_DEVICE=opengl"
            SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA Diligent smoke")
    endmacro()

    # Diligent's Linux native window is X11/XCB only (design decision 8), so these run under X11
    # even on a Wayland host. Exit code 77 / the "[SKIP]" line means "no usable device here",
    # which is a skip rather than a failure -- a headless build machine has no GPU to prove
    # anything on, and pretending otherwise is exactly what plan_diligent.md's "Verification
    # status" section forbids.
    cna_diligent_test(cna_test_diligent_clear_readback diligent_clear_readback_test.cpp)
    cna_register_diligent_test(Diligent_2D cna_test_diligent_clear_readback)

    cna_diligent_test(cna_test_diligent_colored3d diligent_colored3d_test.cpp)
    cna_register_diligent_test(Diligent_3D cna_test_diligent_colored3d)

    # plan_diligent.md DILIGENT-20/DILIGENT-21: off-screen rendering, target readback, sampling the
    # unbound target, and the target's own depth buffer.
    # plan_diligent.md DILIGENT-31/DILIGENT-32: per-pixel alpha-test discard and BasicEffect fog.
    cna_diligent_test(cna_test_diligent_alphatest_fog diligent_alphatest_fog_test.cpp)
    cna_register_diligent_test(Diligent_AlphaTestFog cna_test_diligent_alphatest_fog)

    # plan_diligent.md DILIGENT-33/DILIGENT-34: two-layer modulate and cube-map reflection.
    cna_diligent_test(cna_test_diligent_dualtexture_envmap
                      diligent_dualtexture_envmap_test.cpp)
    cna_register_diligent_test(Diligent_DualTextureEnvMap cna_test_diligent_dualtexture_envmap)

    # plan_diligent.md DILIGENT-35: the 72-bone palette and FNA's WeightsPerVertex semantics.
    # plan_diligent.md DILIGENT-24: several simultaneous render targets and per-slot write masks.
    # plan_diligent.md DILIGENT-22: per-face render-into and readback, plus EnvironmentMapEffect
    # sampling the render target back (the real reason to make a cube render target).
    cna_diligent_test(cna_test_diligent_rendertargetcube diligent_rendertargetcube_test.cpp)
    cna_register_diligent_test(Diligent_RenderTargetCube cna_test_diligent_rendertargetcube)

    cna_diligent_test(cna_test_diligent_mrt diligent_mrt_test.cpp)
    cna_register_diligent_test(Diligent_MRT cna_test_diligent_mrt)

    cna_diligent_test(cna_test_diligent_skinned diligent_skinned_test.cpp)
    cna_register_diligent_test(Diligent_Skinned cna_test_diligent_skinned)

    cna_diligent_test(cna_test_diligent_rendertarget diligent_rendertarget_test.cpp)
    cna_register_diligent_test(Diligent_RenderTarget cna_test_diligent_rendertarget)

    # plan_diligent.md DILIGENT-41: real IQuery/QUERY_TYPE_OCCLUSION-backed occlusion queries.
    cna_diligent_test(cna_test_diligent_occlusionquery diligent_occlusionquery_test.cpp)
    cna_register_diligent_test(Diligent_OcclusionQuery cna_test_diligent_occlusionquery)

    # plan_diligent.md DILIGENT-25: back buffer and RenderTarget2D MSAA, device-probed clamping.
    cna_diligent_test(cna_test_diligent_msaa diligent_msaa_test.cpp)
    cna_register_diligent_test(Diligent_MSAA cna_test_diligent_msaa)

    # plan_diligent.md DILIGENT-43: hardware instancing (DrawInstancedPrimitivesEx).
    cna_diligent_test(cna_test_diligent_instanced diligent_instanced_test.cpp)
    cna_register_diligent_test(Diligent_Instanced cna_test_diligent_instanced)

    # REMED-GFX-202: the two per-binding properties the removed instanceVb/instanceVertexOffset/
    # instanceFrequency trio could not carry -- the instance binding's own VertexOffset and its
    # InstanceFrequency -- plus the geometry binding's VertexOffset on the same route. Each leg is
    # built so that "consumed" and "ignored" produce different, in-bounds pixels.
    cna_diligent_test(cna_test_diligent_instance_binding_offsets
                      diligent_instance_binding_offsets_test.cpp)
    cna_register_diligent_test(Diligent_InstanceBindingOffsets
                               cna_test_diligent_instance_binding_offsets)

    # plan_diligent.md DILIGENT-45: vertexStart/startIndex/baseVertex sub-range coverage.
    cna_diligent_test(cna_test_diligent_drawoffset diligent_drawoffset_test.cpp)
    cna_register_diligent_test(Diligent_DrawOffset cna_test_diligent_drawoffset)

    # plan_diligent.md DILIGENT-44: SetDataOptions (Discard/NoOverwrite) streaming hints.
    cna_diligent_test(cna_test_diligent_setdataoptions diligent_setdataoptions_test.cpp)
    cna_register_diligent_test(Diligent_SetDataOptions cna_test_diligent_setdataoptions)

    # plan_diligent.md DILIGENT-37: per-vertex lighting (PreferPerPixelLighting == false).
    cna_diligent_test(cna_test_diligent_vertexlit diligent_vertexlit_test.cpp)
    cna_register_diligent_test(Diligent_VertexLit cna_test_diligent_vertexlit)

    # plan_diligent.md DILIGENT-36: PbrEffect (glTF metallic-roughness BRDF).
    cna_diligent_test(cna_test_diligent_pbr diligent_pbr_test.cpp)
    cna_register_diligent_test(Diligent_Pbr cna_test_diligent_pbr)

    # GLTF-213: exact mid-grey transfer witnesses on the rigid and skinned Diligent programs.
    cna_diligent_test(cna_test_diligent_pbr_srgb_transfer
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_srgb_transfer_test.cpp)
    cna_register_diligent_test(Diligent_Pbr_SrgbTransfer cna_test_diligent_pbr_srgb_transfer)

    # GLTF-343/344: KHR_materials_ior and factor-only specular alter F0/F90 on both PBR programs.
    cna_diligent_test(cna_test_diligent_pbr_fresnel_factors
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp)
    cna_register_diligent_test(Diligent_Pbr_FresnelFactors cna_test_diligent_pbr_fresnel_factors)

    # plan_diligent.md DILIGENT-49: RasterizerState.DepthBias/SlopeScaleDepthBias pixel verification.
    cna_diligent_test(cna_test_diligent_depthbias diligent_depthbias_test.cpp)
    cna_register_diligent_test(Diligent_DepthBias cna_test_diligent_depthbias)

    # plan_diligent.md DILIGENT-50: GraphicsDevice.ReferenceStencil pixel verification.
    cna_diligent_test(cna_test_diligent_referencestencil diligent_referencestencil_test.cpp)
    cna_register_diligent_test(Diligent_ReferenceStencil cna_test_diligent_referencestencil)

    # plan_diligent.md DILIGENT-51: RasterizerState.FillMode::WireFrame pixel verification.
    cna_diligent_test(cna_test_diligent_fillmode diligent_fillmode_test.cpp)
    cna_register_diligent_test(Diligent_FillMode cna_test_diligent_fillmode)

    # plan_diligent.md DILIGENT-52: anisotropic texture filtering pixel verification.
    cna_diligent_test(cna_test_diligent_anisotropic diligent_anisotropic_test.cpp)
    cna_register_diligent_test(Diligent_Anisotropic cna_test_diligent_anisotropic)

    # plan_diligent.md DILIGENT-53: SpriteFont glyph placement/spacing/newline/flip pixel test.
    cna_diligent_test(cna_test_diligent_spritefont diligent_spritefont_test.cpp)
    cna_register_diligent_test(Diligent_SpriteFont cna_test_diligent_spritefont)

    # plan_diligent.md DILIGENT-54: Model/ModelMesh/ModelMeshPart/ModelBone orchestration test.
    cna_diligent_test(cna_test_diligent_model diligent_model_test.cpp)
    cna_register_diligent_test(Diligent_Model cna_test_diligent_model)

    # plan_diligent.md DILIGENT-55: Texture2D mip-level SetData/GetData round-trip test.
    cna_diligent_test(cna_test_diligent_mip diligent_mip_test.cpp)
    cna_register_diligent_test(Diligent_Mip cna_test_diligent_mip)

    # plan_diligent.md DILIGENT-56: NPOT Texture2D real GPU round-trip test.
    cna_diligent_test(cna_test_diligent_npot diligent_npot_test.cpp)
    cna_register_diligent_test(Diligent_Npot cna_test_diligent_npot)

    # plan_diligent.md DILIGENT-26: RenderTarget2D mip generation content pixel verification.
    cna_diligent_test(cna_test_diligent_rendertarget_mipgen
                      diligent_rendertarget_mipgen_test.cpp)
    cna_register_diligent_test(Diligent_RenderTargetMipGen cna_test_diligent_rendertarget_mipgen)

    # plan_diligent.md DILIGENT-58: PipelineKey completeness -- ScissorTestEnable toggles must not
    # be swallowed by the immutable-PSO cache across SpriteBatch/indexed-3D/instanced draws.
    cna_diligent_test(cna_test_diligent_scissor_pipelinecache
                      diligent_scissor_pipelinecache_test.cpp)
    cna_register_diligent_test(Diligent_ScissorPipelineCache cna_test_diligent_scissor_pipelinecache)

    # plan_diligent.md DILIGENT-60: BlendState.MultiSampleMask reaches
    # Dg::GraphicsPipelineDesc::SampleMask instead of being silently discarded.
    cna_diligent_test(cna_test_diligent_multisamplemask
                      diligent_multisamplemask_test.cpp)
    cna_register_diligent_test(Diligent_MultiSampleMask cna_test_diligent_multisamplemask)

    # plan_diligent.md DILIGENT-65: hardware instancing respects real vertex/instance stream
    # strides instead of a hardcoded slot-0 stride 16 / AUTO_STRIDE-derived slot-1 stride 64.
    cna_diligent_test(cna_test_diligent_instanced_stride
                      diligent_instanced_stride_test.cpp)
    cna_register_diligent_test(Diligent_InstancedStride cna_test_diligent_instanced_stride)

    # plan_diligent.md DILIGENT-61: SupportsCapability() must agree with what the corresponding
    # Create/Apply operation actually does on the live device.
    cna_diligent_test(cna_test_diligent_capability_consistency
                      diligent_capability_consistency_test.cpp)
    cna_register_diligent_test(Diligent_CapabilityConsistency
                                cna_test_diligent_capability_consistency)

    # plan_diligent.md DILIGENT-63: ReadBackbuffer() must be valid and bounded for every
    # CnaPresentationMode, constructed directly against IGraphicsRenderer (not through
    # GraphicsDeviceManager -- see the test file's own header for why). Unlike every other
    # Diligent test, this one includes DiligentRenderer.hpp directly (to construct the
    # renderer itself), so it needs cna_link_diligent()'s own include dirs -- the same reason
    # CnaTests calls it for DiligentDeviceSelectionTests.cpp (cmake/UnitTests.cmake). Still gets
    # the dual-device treatment: CNA_DILIGENT_DEVICE is read inside the renderer's own
    # device-creation loop regardless of construction path.
    cna_diligent_test(cna_test_diligent_backbuffer_readback_bounds
                      diligent_backbuffer_readback_bounds_test.cpp)
    cna_link_diligent(cna_test_diligent_backbuffer_readback_bounds)
    cna_register_diligent_test(Diligent_BackbufferReadbackBounds
                                cna_test_diligent_backbuffer_readback_bounds)

    # plan_diligent.md DILIGENT-57: runtime device selection and SDL window API selection must
    # resolve every CNA_DILIGENT_DEVICE alias through one shared parser. Public-API-only (through
    # GraphicsDeviceManager, unlike Diligent_BackbufferReadbackBounds), so no cna_link_diligent().
    # NOT registered through cna_register_diligent_test(): this binary's own fork+exec supervisor
    # already exercises vulkan/vk/opengl/gl/gles/auto/bogus internally on every run regardless of
    # what CNA_DILIGENT_DEVICE is set to at the ctest level (each leg sets/unsets it itself in the
    # child process) -- an "_OpenGL" twin would run the identical internal matrix a second time for
    # no new coverage, just doubling this already-longest test's ~6s runtime.
    cna_diligent_test(cna_test_diligent_device_selection_integration
                      diligent_device_selection_integration_test.cpp)
    cna_register_renderer_test(NAME Diligent_DeviceSelectionIntegration
        COMMAND cna_test_diligent_device_selection_integration
        TIMEOUT 120 LABELS "GraphicsSmoke;Diligent"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_diligent.md DILIGENT-59: hand-derived proof of the four lighting-fidelity fixes
    # (emissive isolation, specular*alpha, multi-light additive, non-uniform-world normal).
    cna_diligent_test(cna_test_diligent_lighting_fidelity
                      diligent_lighting_fidelity_test.cpp)
    cna_register_diligent_test(Diligent_LightingFidelity cna_test_diligent_lighting_fidelity)
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
