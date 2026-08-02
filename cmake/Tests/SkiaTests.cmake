# The initial SKIA implementation is a deterministic CPU-raster surface presented by SDL. Keep
# its CTests split by execution requirement: raster tests never create a window, while presentation
# tests require a real X11/virtual display. The accelerated helper is deliberately distinct so a
# future Ganesh/Graphite path cannot accidentally inherit raster-only registration.
if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "SKIA")
    enable_testing()

    macro(cna_skia_test target src)
        add_executable(${target} ${src})
        # A handful of raster-boundary fixtures intentionally call virtual Skia APIs directly.
        # Match the adapter's narrow UBSan exception: the pinned no-RTTI Skia archives cannot
        # provide the typeinfo required by `vptr`, while every other undefined check stays live.
        if(CNA_SANITIZE MATCHES "(^|,)undefined(,|$)"
           AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE -fno-sanitize=vptr)
        endif()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    function(cna_register_skia_raster_test name)
        cna_register_backend_test(NAME ${name} COMMAND ${ARGN} TIMEOUT 30 LABELS "Skia;Raster")
    endfunction()

    function(cna_register_skia_display_test name)
        cna_register_backend_test(NAME ${name} COMMAND ${ARGN} TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
            LABELS "Skia;Display")
    endfunction()

    function(cna_register_skia_accelerated_test name)
        cna_register_backend_test(NAME ${name} COMMAND ${ARGN} TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
            LABELS "Skia;Accelerated;Display")
    endfunction()

    # SKIA-1: standard-library source audit. Keep it separate from Raster because it neither links
    # Skia nor exercises pixels; it verifies that every current graphics API entry is classified.
    find_program(CNA_SKIA_LEDGER_PYTHON NAMES python3 REQUIRED)
    cna_register_backend_test(
        NAME Skia_ParityLedger_Audit
        COMMAND "${CNA_SKIA_LEDGER_PYTHON}"
                "${CMAKE_SOURCE_DIR}/scripts/validate_skia_parity_ledger.py"
                "${CMAKE_SOURCE_DIR}"
        TIMEOUT 30
        LABELS "Skia;Audit")
    cna_register_backend_test(
        NAME Skia_TestMatrix_Audit
        COMMAND "${CNA_SKIA_LEDGER_PYTHON}"
                "${CMAKE_SOURCE_DIR}/scripts/validate_skia_test_matrix.py"
                "${CMAKE_SOURCE_DIR}"
        TIMEOUT 30
        LABELS "Skia;Audit")
    cna_register_backend_test(
        NAME Skia_3DDecision_Audit
        COMMAND "${CNA_SKIA_LEDGER_PYTHON}"
                "${CMAKE_SOURCE_DIR}/scripts/validate_skia_3d_decision.py"
                "${CMAKE_SOURCE_DIR}"
        TIMEOUT 30
        LABELS "Skia;Audit")
    cna_register_backend_test(
        NAME Skia_ReleaseGate_Audit
        COMMAND "${CNA_SKIA_LEDGER_PYTHON}"
                "${CMAKE_SOURCE_DIR}/scripts/validate_skia_release_gate.py"
                "${CMAKE_SOURCE_DIR}"
        TIMEOUT 30
        LABELS "Skia;Audit")
    cna_register_backend_test(
        NAME Skia_SuccessorContracts_Audit
        COMMAND "${CNA_SKIA_LEDGER_PYTHON}"
                "${CMAKE_SOURCE_DIR}/scripts/validate_skia_successor_contracts.py"
                "${CMAKE_SOURCE_DIR}"
        TIMEOUT 30
        LABELS "Skia;Audit")

    cna_skia_test(cna_test_skia_surface_raster examples/skia_surface_raster_test.cpp)
    # This test intentionally exercises the internal SkiaSurface boundary directly, so unlike
    # public-API tests it needs the external Skia header root while compiling its own source.
    target_include_directories(cna_test_skia_surface_raster PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_Surface_Raster cna_test_skia_surface_raster)

    cna_skia_test(cna_test_skia_texture_alpha_boundary examples/skia_texture_alpha_boundary_test.cpp)
    target_include_directories(cna_test_skia_texture_alpha_boundary PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_Texture_AlphaBoundary cna_test_skia_texture_alpha_boundary)

    cna_skia_test(cna_test_skia_target_binding_raster examples/skia_target_binding_raster_test.cpp)
    target_include_directories(cna_test_skia_target_binding_raster PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_RenderTargetBinding_Raster cna_test_skia_target_binding_raster)

    cna_skia_test(cna_test_skia_blend_mapping_raster examples/skia_blend_mapping_raster_test.cpp)
    target_include_directories(cna_test_skia_blend_mapping_raster PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_BlendMapping_Raster cna_test_skia_blend_mapping_raster)

    cna_skia_test(cna_test_skia_source_alpha_policy examples/skia_source_alpha_policy_test.cpp)
    target_include_directories(cna_test_skia_source_alpha_policy PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_SourceAlpha_Policy cna_test_skia_source_alpha_policy)

    cna_skia_test(cna_test_skia_runtime_blender_raster examples/skia_runtime_blender_raster_test.cpp)
    target_include_directories(cna_test_skia_runtime_blender_raster PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_RuntimeBlender_Raster cna_test_skia_runtime_blender_raster)

    cna_skia_test(cna_test_skia_generated_blender examples/skia_generated_blender_test.cpp)
    target_include_directories(cna_test_skia_generated_blender PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_GeneratedBlender_Raster cna_test_skia_generated_blender)

    cna_skia_test(cna_test_skia_color_write_mask_raster examples/skia_color_write_mask_raster_test.cpp)
    target_include_directories(cna_test_skia_color_write_mask_raster PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_ColorWriteMask_Raster cna_test_skia_color_write_mask_raster)

    cna_skia_test(cna_test_skia_startup_diagnostic examples/skia_startup_diagnostic_test.cpp)
    cna_register_skia_raster_test(Skia_StartupDiagnostic_Raster cna_test_skia_startup_diagnostic)

    cna_skia_test(cna_test_skia_mip_chain_2d examples/skia_mip_chain_2d_test.cpp)
    cna_register_skia_raster_test(Skia_MipChain2D_Raster cna_test_skia_mip_chain_2d)

    cna_skia_test(cna_test_skia_mip_sampling_raster examples/skia_mip_sampling_raster_test.cpp)
    target_include_directories(cna_test_skia_mip_sampling_raster PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_MipSampling_Raster cna_test_skia_mip_sampling_raster)

    cna_skia_test(cna_test_skia_lifecycle examples/skia_lifecycle_test.cpp)
    target_include_directories(cna_test_skia_lifecycle PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_Lifecycle cna_test_skia_lifecycle)

    cna_skia_test(cna_test_skia_ownership examples/skia_ownership_test.cpp)
    target_include_directories(cna_test_skia_ownership PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_Ownership cna_test_skia_ownership)

    # SKIA-107: correlate the one selected raster mode's runtime diagnostic and capability set
    # with alpha-normalized pixels before/after SDL presenter recovery. This explicitly guards
    # against a silent Skia GPU/fallback mode; there is no fabricated CPU/GPU parity claim. SDL
    # may independently use an accelerated presentation renderer for the completed CPU image.
    cna_skia_test(cna_test_skia_raster_mode_coherence
                  examples/skia_raster_mode_coherence_test.cpp)
    target_include_directories(cna_test_skia_raster_mode_coherence PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_RasterMode_Coherence
                                   cna_test_skia_raster_mode_coherence)

    # SKIA-108: build the shared declarative XNA-oracle renderer against Skia. The registered
    # comparison gate below runs only scene files whose own spritebatchmode=true bypasses every
    # stock-effect/3D path; the renderer remains shared with D3D9 and EasyGL.
    cna_skia_test(cna_oracle_render_skia tools/xna-oracle/CnaOracleRender.cpp)
    cna_register_backend_test(
        NAME Skia_XNA_2D_Oracle
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/run-skia-2d-oracle-diff.sh"
                $<TARGET_FILE:cna_oracle_render_skia>
        TIMEOUT 60
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "Skia;Display")

    # SKIA-109: compile the exact EasyGL sources for backend-independent API contracts. These
    # fixtures assert shared validation precedence, transfer windows, first-read completion,
    # SpriteFont validation, resize state, and immediate target pass boundaries without invoking
    # a 3D draw. Mixed VertexBuffer/IndexBuffer lifecycle fixtures remain at the SKIA-102 boundary.
    cna_skia_test(cna_test_skia_contract_device_validation
                  examples/easygl_device_validation_test.cpp)
    cna_register_skia_display_test(Skia_Contract_DeviceValidation
                                   cna_test_skia_contract_device_validation)

    cna_skia_test(cna_test_skia_contract_texture2d_partial_rect
                  examples/easygl_texture2d_partial_rect_test.cpp)
    cna_register_skia_display_test(Skia_Contract_Texture2D_PartialRect
                                   cna_test_skia_contract_texture2d_partial_rect)

    cna_skia_test(cna_test_skia_contract_surface_format
                  examples/easygl_surface_format_throws_test.cpp)
    cna_register_skia_display_test(Skia_Contract_SurfaceFormat
                                   cna_test_skia_contract_surface_format)

    cna_skia_test(cna_test_skia_contract_spritefont_properties
                  examples/sprite_font_test.cpp)
    cna_register_skia_display_test(Skia_Contract_SpriteFontProperties
                                   cna_test_skia_contract_spritefont_properties)

    cna_skia_test(cna_test_skia_contract_viewport_resize
                  examples/viewport_reset_after_resize_test.cpp)
    cna_register_skia_display_test(Skia_Contract_ViewportResetAfterResize
                                   cna_test_skia_contract_viewport_resize)

    cna_skia_test(cna_test_skia_contract_backbuffer_first_read
                  examples/backbuffer_first_read_test.cpp)
    cna_register_backend_test(
        NAME Skia_Contract_BackbufferFirstRead
        COMMAND cna_test_skia_contract_backbuffer_first_read
        TIMEOUT 300
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "Skia;Display")

    cna_skia_test(cna_test_skia_contract_backbuffer_reject
                  examples/backbuffer_headless_reject_test.cpp)
    cna_register_backend_test(
        NAME Skia_Contract_BackbufferReject
        COMMAND cna_test_skia_contract_backbuffer_reject
        TIMEOUT 300
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "Skia;Display")

    cna_skia_test(cna_test_skia_contract_rendertarget_pass_boundary
                  examples/rendertarget_pass_boundary_test.cpp)
    cna_register_backend_test(
        NAME Skia_Contract_RenderTargetPassBoundary
        COMMAND cna_test_skia_contract_rendertarget_pass_boundary
        TIMEOUT 90
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "Skia;Display")

    cna_skia_test(cna_test_skia_present_interval examples/skia_present_interval_test.cpp)
    target_include_directories(cna_test_skia_present_interval PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_PresentInterval cna_test_skia_present_interval)

    cna_skia_test(cna_test_skia_graphics_capability examples/skia_graphics_capability_test.cpp)
    cna_register_skia_display_test(Skia_GraphicsCapability cna_test_skia_graphics_capability)

    # SKIA-89: keep untagged backend-specific ShaderEffect strings outside the SkSL path and prove
    # that rejection cannot poison the following built-in SpriteBatch session.
    cna_skia_test(cna_test_skia_effect_boundary examples/skia_effect_boundary_test.cpp)
    cna_register_skia_display_test(Skia_Effect_Boundary cna_test_skia_effect_boundary)

    # SKIA-90: the exact stock SpriteEffect is a semantic alias for the already-proven built-in
    # paint path. Derived/custom effects remain rejected rather than being silently ignored.
    cna_skia_test(cna_test_skia_spriteeffect_alias examples/skia_spriteeffect_alias_test.cpp)
    cna_register_skia_display_test(Skia_SpriteEffect_Alias cna_test_skia_spriteeffect_alias)

    # SKIA-91: only the exact CNA_SKIA_SKSL_V1 marker opts a ShaderEffect into the bounded
    # fragment-only runtime-shader ABI. Pixel checks prove its reserved texture child, while bad
    # source/ABI/size cases retain actionable diagnostics and cannot poison the stock path.
    cna_skia_test(cna_test_skia_sksl_effect_prototype
                  examples/skia_sksl_effect_prototype_test.cpp)
    cna_register_skia_display_test(Skia_SkSL_Effect_Prototype
                                   cna_test_skia_sksl_effect_prototype)

    # SKIA-92: reflected scalar/vector/matrix/array writes and one weak live additional 2D child.
    # The fixture also exercises every deterministic validation boundary and clone isolation.
    cna_skia_test(cna_test_skia_sksl_uniform_texture
                  examples/skia_sksl_uniform_texture_test.cpp)
    cna_register_skia_display_test(Skia_SkSL_UniformTexture
                                   cna_test_skia_sksl_uniform_texture)

    # SKIA-93: headless component spike. Alpha-test failure is coverage (clipShader), not a
    # transparent source; dual texture and colour transforms compose in one raster draw. This does
    # not promote their stock 3D Effect types, whose vertex/fog/depth contracts remain absent.
    cna_skia_test(cna_test_skia_effect_emulation_spike
                  examples/skia_effect_emulation_spike_test.cpp)
    target_include_directories(cna_test_skia_effect_emulation_spike PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_Effect_Emulation_Spike
                                  cna_test_skia_effect_emulation_spike)

    # SKIA-94: fragment-like feasibility does not promote stock effects whose public route starts
    # with transformed primitives. Exercise AlphaTestEffect/DualTextureEffect property matrices,
    # atomic 3D refusal, actionable SpriteBatch diagnostics, and immediate 2D recovery.
    cna_skia_test(cna_test_skia_stock_effect_boundary
                  examples/skia_stock_effect_boundary_test.cpp)
    cna_register_skia_display_test(Skia_StockEffect_Boundary
                                   cna_test_skia_stock_effect_boundary)

    # SKIA-96: an internal, headless SkVertices spike for exactly one CPU-projected
    # VertexPositionColorTexture TriangleList. It measures equal-W interpolation and records the
    # unequal-W, homogeneous-clipping and culling gaps; it is not a public Draw* implementation.
    cna_skia_test(cna_test_skia_projected_vertices_spike
                  examples/skia_projected_vertices_spike_test.cpp)
    target_include_directories(cna_test_skia_projected_vertices_spike PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_ProjectedVertices_Spike
                                  cna_test_skia_projected_vertices_spike)

    # SKIA-97: a bounded CPU RGBA8+float-depth target owns triangle coverage, reciprocal-W
    # interpolation and depth ordering, then copies only the completed image to Skia. This remains
    # an internal feasibility/performance measurement, not public 3D support.
    cna_skia_test(cna_test_skia_cpu_depth_raster_spike
                  examples/skia_cpu_depth_raster_spike_test.cpp)
    target_include_directories(cna_test_skia_cpu_depth_raster_spike PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_CpuDepthRaster_Spike
                                  cna_test_skia_cpu_depth_raster_spike)

    # SKIA-98: extend only the isolated CPU bridge with the complete 8-bit stencil state machine:
    # compare/operation matrices, masks, fail/depth-fail/pass ordering, two-sided state and colour
    # write interaction. It remains disconnected from public Draw* and capability reporting.
    cna_skia_test(cna_test_skia_cpu_stencil_spike
                  examples/skia_cpu_stencil_spike_test.cpp)
    cna_register_skia_raster_test(Skia_CpuStencil_Spike
                                  cna_test_skia_cpu_stencil_spike)

    # SKIA-99: isolated CPU input assembly over the selected depth/stencil bridge. It decodes every
    # declared format/usage, preserves 16/32-bit indices, expands all topologies, applies XNA
    # winding/culling before wireframe edges and exercises raw DrawUser offsets/range rejection.
    cna_skia_test(cna_test_skia_cpu_geometry_spike
                  examples/skia_cpu_geometry_spike_test.cpp)
    cna_register_skia_raster_test(Skia_CpuGeometry_Spike
                                  cna_test_skia_cpu_geometry_spike)

    # SKIA-100: one isolated unlit textured BasicEffect route over the CPU bridge, plus a closed
    # stock-effect requirement inventory. This is feasibility evidence, not a public Draw path.
    cna_skia_test(cna_test_skia_cpu_stock_effect_spike
                  examples/skia_cpu_stock_effect_spike_test.cpp)
    target_include_directories(cna_test_skia_cpu_stock_effect_spike PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_CpuStockEffect_Spike
                                  cna_test_skia_cpu_stock_effect_spike)

    # SKIA-102: accepted 2D-only ADR enforcement. One target-atomic fixture covers every backend
    # 3D family and the public buffer/draw/effect/model/query routes, then proves retained 2D and
    # CPU cube/volume transfer behavior still works.
    cna_skia_test(cna_test_skia_3d_refusal examples/skia_3d_refusal_test.cpp)
    cna_register_skia_display_test(Skia_3D_Refusal cna_test_skia_3d_refusal)

    # SKIA-104: a headless negative feasibility spike. Raster framebuffer differences cannot
    # distinguish a fully covered same-colour/destination-preserving draw from zero coverage and
    # cannot recover repeated sample counts, so they are not an occlusion-query implementation.
    cna_skia_test(cna_test_skia_occlusion_query_feasibility
                  examples/skia_occlusion_query_feasibility_test.cpp)
    target_include_directories(cna_test_skia_occlusion_query_feasibility
                               PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_OcclusionQuery_Feasibility
                                  cna_test_skia_occlusion_query_feasibility)

    # SKIA-106: register exact EasyGL sources whose asserted paths are genuinely 2D. Keep EasyGL
    # fixtures containing BasicEffect/DrawUser outside the raster suite despite historical names;
    # their 2D sampler/blend/clip contracts remain covered by Skia-native shared pixel fixtures.
    cna_skia_test(cna_test_skia_textured_quad_readback
                  examples/easygl_textured_quad_test.cpp)
    cna_register_skia_display_test(Skia_TexturedQuad_Readback
                                   cna_test_skia_textured_quad_readback)

    # Reuse representative exact EasyGL sources for every remaining category whose source itself
    # is genuinely 2D. Existing Skia-native/shared fixtures retain the exhaustive edge matrices.
    cna_skia_test(cna_test_skia_easygl_spritebatch_rotation
                  examples/easygl_spritebatch_rotation_test.cpp)
    cna_register_skia_display_test(Skia_EasyGL_SpriteBatch_Rotation
                                   cna_test_skia_easygl_spritebatch_rotation)

    cna_skia_test(cna_test_skia_easygl_spritebatch_sourcerect
                  examples/easygl_spritebatch_sourcerect_test.cpp)
    cna_register_skia_display_test(Skia_EasyGL_SpriteBatch_SourceRect
                                   cna_test_skia_easygl_spritebatch_sourcerect)

    cna_skia_test(cna_test_skia_easygl_spritebatch_layerdepth
                  examples/easygl_spritebatch_layerdepth_test.cpp)
    cna_register_skia_display_test(Skia_EasyGL_SpriteBatch_LayerDepth
                                   cna_test_skia_easygl_spritebatch_layerdepth)

    cna_skia_test(cna_test_skia_easygl_spritefont_single
                  examples/easygl_spritefont_single_glyph_test.cpp)
    cna_register_skia_display_test(Skia_EasyGL_SpriteFont_SingleGlyph
                                   cna_test_skia_easygl_spritefont_single)

    cna_skia_test(cna_test_skia_easygl_address_mode
                  examples/easygl_texture_address_mode_test.cpp)
    cna_register_skia_display_test(Skia_EasyGL_TextureAddressMode
                                   cna_test_skia_easygl_address_mode)

    cna_skia_test(cna_test_skia_easygl_address_mode_mirror
                  examples/easygl_texture_address_mode_mirror_test.cpp)
    cna_register_skia_display_test(Skia_EasyGL_TextureAddressMode_Mirror
                                   cna_test_skia_easygl_address_mode_mirror)

    cna_skia_test(cna_test_skia_easygl_clear_overloads
                  examples/easygl_clear_overloads_test.cpp)
    cna_register_skia_display_test(Skia_EasyGL_ClearOverloads
                                   cna_test_skia_easygl_clear_overloads)

    cna_skia_test(cna_test_skia_easygl_render_target
                  examples/easygl_render_target_test.cpp)
    cna_register_skia_display_test(Skia_EasyGL_RenderTarget2D_Readback
                                   cna_test_skia_easygl_render_target)

    cna_skia_test(cna_test_skia_easygl_disposed_resource
                  examples/easygl_disposed_resource_test.cpp)
    cna_register_skia_display_test(Skia_EasyGL_DisposedResource
                                   cna_test_skia_easygl_disposed_resource)

    cna_skia_test(cna_test_skia_easygl_backbuffer_readback_dimension
                  examples/backbuffer_readback_dimension_test.cpp)
    cna_register_skia_display_test(Skia_EasyGL_BackbufferReadbackDimension
                                   cna_test_skia_easygl_backbuffer_readback_dimension)

    # SKIA-80--SKIA-84: cube/volume resources are bounded CPU transfer storage, not shader-
    # sampleable 3D resources. The direct raster test proves the allocation/validation policy;
    # shared public tests prove every face, mip, rectangle, slice, box, and failure contract.
    cna_skia_test(cna_test_skia_texture_storage_policy examples/skia_texture_storage_policy_test.cpp)
    cna_register_skia_raster_test(Skia_TextureStorage_Policy cna_test_skia_texture_storage_policy)

    cna_skia_test(cna_test_skia_successor_resource_policy
                  examples/skia_successor_resource_policy_test.cpp)
    cna_register_skia_raster_test(Skia_SuccessorResource_Policy
                                  cna_test_skia_successor_resource_policy)

    cna_skia_test(cna_test_skia_rendertargetcube_policy
                  examples/skia_rendertargetcube_policy_test.cpp)
    target_include_directories(cna_test_skia_rendertargetcube_policy PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_RenderTargetCube_Policy
                                  cna_test_skia_rendertargetcube_policy)

    cna_skia_test(cna_test_skia_texturecube_faces examples/easygl_texturecube_faces_test.cpp)
    cna_register_skia_display_test(Skia_TextureCube_Faces_RoundTrip cna_test_skia_texturecube_faces)

    cna_skia_test(cna_test_skia_texturecube_partial_rect examples/easygl_texturecube_partial_rect_test.cpp)
    cna_register_skia_display_test(Skia_TextureCube_PartialRect_RoundTrip cna_test_skia_texturecube_partial_rect)

    cna_skia_test(cna_test_skia_texturecube_mip examples/easygl_texturecube_mip_test.cpp)
    cna_register_skia_display_test(Skia_TextureCube_Mip_RoundTrip cna_test_skia_texturecube_mip)

    cna_skia_test(cna_test_skia_texturecube_content_load
                  examples/easygl_texturecube_content_load_test.cpp)
    cna_register_skia_display_test(Skia_TextureCube_ContentLoad cna_test_skia_texturecube_content_load)

    cna_skia_test(cna_test_skia_texture3d_slices examples/easygl_texture3d_slices_test.cpp)
    cna_register_skia_display_test(Skia_Texture3D_Slices_RoundTrip cna_test_skia_texture3d_slices)

    cna_skia_test(cna_test_skia_texture3d_partial_box examples/easygl_texture3d_partial_box_test.cpp)
    cna_register_skia_display_test(Skia_Texture3D_PartialBox_RoundTrip cna_test_skia_texture3d_partial_box)

    cna_skia_test(cna_test_skia_texture3d_partial_box_readback examples/easygl_texture3d_partial_box_readback_test.cpp)
    cna_register_skia_display_test(Skia_Texture3D_PartialBox_Readback cna_test_skia_texture3d_partial_box_readback)

    cna_skia_test(cna_test_skia_texture3d_mip examples/easygl_texture3d_mip_test.cpp)
    cna_register_skia_display_test(Skia_Texture3D_Mip_RoundTrip cna_test_skia_texture3d_mip)

    cna_skia_test(cna_test_skia_texture_storage_getdata_contract
                  examples/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_skia_display_test(Skia_TextureStorage_GetDataContract
                                   cna_test_skia_texture_storage_getdata_contract)

    cna_skia_test(cna_test_skia_texture_storage_setdata_contract
                  examples/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_skia_display_test(Skia_TextureStorage_SetDataContract
                                   cna_test_skia_texture_storage_setdata_contract)

    cna_skia_test(cna_test_skia_texture2d_getdata_contract examples/texture2d_getdata_contract_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_GetDataContract cna_test_skia_texture2d_getdata_contract)

    cna_skia_test(cna_test_skia_texture2d_getdata_transfer_range examples/texture2d_getdata_transfer_range_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_GetDataTransferRange cna_test_skia_texture2d_getdata_transfer_range)

    cna_skia_test(cna_test_skia_texture_constraints examples/skia_texture_constraints_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_Constraints cna_test_skia_texture_constraints)

    cna_skia_test(cna_test_skia_texture2d_dispose examples/sdlrenderer_texture2d_dispose_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_Dispose cna_test_skia_texture2d_dispose)

    cna_skia_test(cna_test_skia_mipmap_policy examples/skia_mipmap_policy_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_MipmapPolicy cna_test_skia_mipmap_policy)

    cna_skia_test(cna_test_skia_texture2d_mip_construction
                  examples/skia_texture2d_mip_construction_test.cpp)
    target_include_directories(cna_test_skia_texture2d_mip_construction
                               PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_Texture2D_MipConstruction
                                   cna_test_skia_texture2d_mip_construction)

    cna_skia_test(cna_test_skia_texture2d_mip_transfer
                  examples/skia_texture2d_mip_transfer_test.cpp)
    target_include_directories(cna_test_skia_texture2d_mip_transfer
                               PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_Texture2D_MipTransfer
                                   cna_test_skia_texture2d_mip_transfer)

    cna_skia_test(cna_test_skia_texture2d_mip_roundtrip
                  examples/easygl_texture2d_mip_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_MipRoundTrip
                                   cna_test_skia_texture2d_mip_roundtrip)

    cna_skia_test(cna_test_skia_texture2d_mip_generation
                  examples/skia_texture2d_mip_generation_test.cpp)
    target_include_directories(cna_test_skia_texture2d_mip_generation
                               PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_Texture2D_MipGeneration
                                   cna_test_skia_texture2d_mip_generation)

    cna_skia_test(cna_test_skia_sampler_mipmap_filter examples/skia_sampler_mipmap_filter_test.cpp)
    cna_register_skia_display_test(Skia_Sampler_MipmapFilterPolicy cna_test_skia_sampler_mipmap_filter)

    cna_skia_test(cna_test_skia_texture2d_content_mips
                  examples/skia_texture2d_content_mips_test.cpp)
    target_include_directories(cna_test_skia_texture2d_content_mips
                               PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_Texture2D_ContentMips
                                   cna_test_skia_texture2d_content_mips)

    # Pixel-level sampling companion to the CPU readback cases above: two odd NPOT dimensions,
    # source-row selection, PointClamp, and backbuffer readback all pass through the public API.
    cna_skia_test(cna_test_skia_npot_texture examples/sdlrenderer_npot_texture_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_NpotSampling cna_test_skia_npot_texture)

    cna_skia_test(cna_test_skia_spritebatch_begin_end examples/sdlrenderer_spritebatch_begin_end_guard_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_BeginEnd cna_test_skia_spritebatch_begin_end)

    cna_skia_test(cna_test_skia_disposed_guards examples/sdlrenderer_disposed_guards_test.cpp)
    cna_register_skia_display_test(Skia_DisposedGuards cna_test_skia_disposed_guards)

    cna_skia_test(cna_test_skia_double_dispose examples/sdlrenderer_double_dispose_test.cpp)
    cna_register_skia_display_test(Skia_DoubleDispose cna_test_skia_double_dispose)

    cna_skia_test(cna_test_skia_spritebatch_sourcerect examples/sdlrenderer_spritebatch_sourcerect_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_SourceRect cna_test_skia_spritebatch_sourcerect)

    cna_skia_test(cna_test_skia_spritebatch_overloads examples/sdlrenderer_spritebatch_overloads_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Overloads cna_test_skia_spritebatch_overloads)

    cna_skia_test(cna_test_skia_spritebatch_remaining_overloads examples/skia_spritebatch_remaining_overloads_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_RemainingOverloads cna_test_skia_spritebatch_remaining_overloads)

    cna_skia_test(cna_test_skia_spritebatch_stress examples/skia_spritebatch_stress_test.cpp)
    # This intentionally renders and reads back 64 complete frames. On a real display the SDL
    # presenter may pace those frames, so the common 30-second smoke-test limit is too short even
    # though the deterministic workload completes normally (about 61 seconds on the release-gate
    # host). Keep the workload intact and give only this stress test a bounded larger window.
    cna_register_backend_test(
        NAME Skia_SpriteBatch_Stress
        COMMAND cna_test_skia_spritebatch_stress
        TIMEOUT 120
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "Skia;Display")

    # These three presets establish the alpha convention used by the raster backend:
    # opaque source replacement, premultiplied-alpha source-over, and straight-alpha source-over.
    cna_skia_test(cna_test_skia_blend_opaque examples/sdlrenderer_blendstate_opaque_test.cpp)
    cna_register_skia_display_test(Skia_BlendState_Opaque cna_test_skia_blend_opaque)

    cna_skia_test(cna_test_skia_blend_alphablend examples/sdlrenderer_blendstate_alphablend_test.cpp)
    cna_register_skia_display_test(Skia_BlendState_AlphaBlend cna_test_skia_blend_alphablend)

    cna_skia_test(cna_test_skia_blend_nonpremultiplied examples/sdlrenderer_blendstate_nonpremultiplied_test.cpp)
    cna_register_skia_display_test(Skia_BlendState_NonPremultiplied cna_test_skia_blend_nonpremultiplied)

    cna_skia_test(cna_test_skia_blend_additive examples/sdlrenderer_blendstate_additive_test.cpp)
    cna_register_skia_display_test(Skia_BlendState_Additive cna_test_skia_blend_additive)

    cna_skia_test(cna_test_skia_blend_enabled_state examples/skia_blend_enabled_state_test.cpp)
    cna_register_skia_display_test(Skia_BlendEnabled_State cna_test_skia_blend_enabled_state)

    cna_skia_test(cna_test_skia_blend_mapping_policy examples/skia_blend_mapping_policy_test.cpp)
    cna_register_skia_display_test(Skia_BlendMapping_Policy cna_test_skia_blend_mapping_policy)

    cna_skia_test(cna_test_skia_generated_blend_state_policy
                  examples/skia_generated_blend_state_policy_test.cpp)
    cna_register_skia_display_test(Skia_GeneratedBlendState_Policy
                                   cna_test_skia_generated_blend_state_policy)

    cna_skia_test(cna_test_skia_generated_blend_batch_modes
                  examples/skia_generated_blend_batch_modes_test.cpp)
    cna_register_skia_display_test(Skia_GeneratedBlend_BatchModes
                                   cna_test_skia_generated_blend_batch_modes)

    cna_skia_test(cna_test_skia_generated_blend_public_corpus
                  examples/skia_generated_blend_public_corpus_test.cpp)
    cna_register_skia_display_test(Skia_GeneratedBlend_PublicCorpus
                                   cna_test_skia_generated_blend_public_corpus)

    cna_skia_test(cna_test_skia_runtime_blender_policy examples/skia_runtime_blender_policy_test.cpp)
    cna_register_skia_display_test(Skia_RuntimeBlender_Policy cna_test_skia_runtime_blender_policy)

    cna_skia_test(cna_test_skia_color_write_policy examples/skia_color_write_policy_test.cpp)
    cna_register_skia_display_test(Skia_ColorWrite_Policy cna_test_skia_color_write_policy)

    cna_skia_test(cna_test_skia_spritebatch_tint_alpha examples/skia_spritebatch_tint_alpha_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_TintAlpha cna_test_skia_spritebatch_tint_alpha)

    cna_skia_test(cna_test_skia_spritebatch_rotation examples/sdlrenderer_spritebatch_rotation_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Rotation cna_test_skia_spritebatch_rotation)

    cna_skia_test(cna_test_skia_spritebatch_scale examples/sdlrenderer_spritebatch_scale_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Scale cna_test_skia_spritebatch_scale)

    cna_skia_test(cna_test_skia_spritebatch_negative_scale examples/skia_spritebatch_negative_scale_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_NegativeScale cna_test_skia_spritebatch_negative_scale)

    # A non-identity transform must compose after the sprite's own geometry. This is a shared
    # public-API probe: it uses scale and translation, so a translation-only implementation fails.
    cna_skia_test(cna_test_skia_transform_matrix examples/vulkan_transform_matrix_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_TransformMatrix cna_test_skia_transform_matrix)

    cna_skia_test(cna_test_skia_spritebatch_source_rect_linear examples/skia_spritebatch_source_rect_linear_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_SourceRectLinear cna_test_skia_spritebatch_source_rect_linear)

    # Shared public SpriteBatch test: its Begin-supplied RasterizerState must enable/disable the
    # ScissorRectangle for deferred, immediate, and front-to-back submissions.
    cna_skia_test(cna_test_skia_spritebatch_rasterizerstate examples/software_spritebatch_rasterizerstate_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_RasterizerState cna_test_skia_spritebatch_rasterizerstate)

    cna_skia_test(cna_test_skia_rasterizer_state_policy examples/skia_rasterizer_state_policy_test.cpp)
    cna_register_skia_display_test(Skia_RasterizerState_Policy cna_test_skia_rasterizer_state_policy)

    cna_skia_test(cna_test_skia_state_transition examples/skia_state_transition_test.cpp)
    cna_register_skia_display_test(Skia_StateTransition cna_test_skia_state_transition)

    cna_skia_test(cna_test_skia_spritebatch_viewport examples/skia_spritebatch_viewport_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Viewport cna_test_skia_spritebatch_viewport)

    cna_skia_test(cna_test_skia_viewport_project_unproject examples/sdlrenderer_viewport_project_unproject_test.cpp)
    cna_register_skia_display_test(Skia_Viewport_ProjectUnproject cna_test_skia_viewport_project_unproject)

    cna_skia_test(cna_test_skia_sprite_effects examples/sdlrenderer_sprite_effects_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Effects cna_test_skia_sprite_effects)

    cna_skia_test(cna_test_skia_spritebatch_deferred_order examples/sdlrenderer_spritebatch_deferred_order_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_DeferredOrder cna_test_skia_spritebatch_deferred_order)

    cna_skia_test(cna_test_skia_spritebatch_immediate_flush examples/sdlrenderer_spritebatch_immediate_flush_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_ImmediateFlush cna_test_skia_spritebatch_immediate_flush)

    cna_skia_test(cna_test_skia_spritebatch_layerdepth examples/sdlrenderer_spritebatch_layerdepth_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_LayerDepth cna_test_skia_spritebatch_layerdepth)

    cna_skia_test(cna_test_skia_spritebatch_texture_sort examples/sdlrenderer_spritebatch_texture_sort_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_TextureSort cna_test_skia_spritebatch_texture_sort)

    cna_skia_test(cna_test_skia_texture_filter_point_linear examples/sdlrenderer_texture_filter_point_vs_linear_test.cpp)
    cna_register_skia_display_test(Skia_TextureFilter_PointVsLinear cna_test_skia_texture_filter_point_linear)

    cna_skia_test(cna_test_skia_texture_filter_minification examples/skia_texture_filter_minification_test.cpp)
    cna_register_skia_display_test(Skia_TextureFilter_Minification cna_test_skia_texture_filter_minification)

    cna_skia_test(cna_test_skia_spritebatch_sampler_transition examples/skia_spritebatch_sampler_transition_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_SamplerTransition cna_test_skia_spritebatch_sampler_transition)

    cna_skia_test(cna_test_skia_texture_address_axes examples/skia_texture_address_axes_test.cpp)
    cna_register_skia_display_test(Skia_TextureAddressAxes cna_test_skia_texture_address_axes)

    cna_skia_test(cna_test_skia_spritefont_single_glyph examples/sdlrenderer_spritefont_single_glyph_test.cpp)
    cna_register_skia_display_test(Skia_SpriteFont_SingleGlyph cna_test_skia_spritefont_single_glyph)

    cna_skia_test(cna_test_skia_spritefont_multiglyph_spacing examples/sdlrenderer_spritefont_multiglyph_spacing_test.cpp)
    cna_register_skia_display_test(Skia_SpriteFont_MultiGlyphSpacing cna_test_skia_spritefont_multiglyph_spacing)

    cna_skia_test(cna_test_skia_spritefont_newline examples/sdlrenderer_spritefont_newline_test.cpp)
    cna_register_skia_display_test(Skia_SpriteFont_Newline cna_test_skia_spritefont_newline)

    cna_skia_test(cna_test_skia_spritefont_default_char examples/sdlrenderer_spritefont_default_char_test.cpp)
    cna_register_skia_display_test(Skia_SpriteFont_DefaultChar cna_test_skia_spritefont_default_char)

    cna_skia_test(cna_test_skia_spritefont_effects examples/sdlrenderer_spritefont_effects_test.cpp)
    cna_register_skia_display_test(Skia_SpriteFont_Effects cna_test_skia_spritefont_effects)

    cna_skia_test(cna_test_skia_rendertarget_sample examples/sdlrenderer_rendertarget2d_sample_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_SampleAfterUnbind cna_test_skia_rendertarget_sample)

    cna_skia_test(cna_test_skia_rendertarget_golden examples/rendertarget2d_golden_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Golden cna_test_skia_rendertarget_golden)

    cna_skia_test(cna_test_skia_rendertarget_usage examples/sdlrenderer_rendertarget_usage_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Usage cna_test_skia_rendertarget_usage)

    cna_skia_test(cna_test_skia_rendertarget_setdata examples/skia_rendertarget_setdata_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_SetData cna_test_skia_rendertarget_setdata)

    cna_skia_test(cna_test_skia_getbackbuffer_after_rt_unbind examples/sdlrenderer_getbackbufferdata_after_rt_unbind_test.cpp)
    cna_register_skia_display_test(Skia_GetBackBufferData_AfterRtUnbind cna_test_skia_getbackbuffer_after_rt_unbind)

    cna_skia_test(cna_test_skia_active_target_readback examples/skia_active_target_readback_test.cpp)
    cna_register_skia_display_test(Skia_GetBackBufferData_ActiveTarget cna_test_skia_active_target_readback)

    cna_skia_test(cna_test_skia_rendertarget_readback examples/skia_rendertarget_readback_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Readback cna_test_skia_rendertarget_readback)

    cna_skia_test(cna_test_skia_rendertarget_depth_policy examples/skia_rendertarget_depth_policy_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_DepthPolicy cna_test_skia_rendertarget_depth_policy)

    cna_skia_test(cna_test_skia_rendertarget_msaa_policy examples/skia_rendertarget_msaa_policy_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_MsaaPolicy cna_test_skia_rendertarget_msaa_policy)

    cna_skia_test(cna_test_skia_rendertarget_switch examples/skia_rendertarget_switch_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Switch cna_test_skia_rendertarget_switch)

    cna_skia_test(cna_test_skia_rendertarget_scissor examples/skia_rendertarget_scissor_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Scissor cna_test_skia_rendertarget_scissor)

    cna_skia_test(cna_test_skia_rendertarget_lifetime examples/skia_rendertarget_lifetime_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Lifetime cna_test_skia_rendertarget_lifetime)

    # SKIA-85--SKIA-86: RenderTargetCube is a six-surface CPU-raster emulation. These shared
    # contracts cover every face, rendered/uploaded readback, generated mips, Preserve/Discard,
    # truthful zero-sample MSAA clamping, public properties, rebinding, and disposal.
    cna_skia_test(cna_test_skia_rendertargetcube_getdata
                  examples/rendertargetcube_getdata_contract_test.cpp)
    cna_register_skia_display_test(Skia_RenderTargetCube_GetDataContract
                                   cna_test_skia_rendertargetcube_getdata)

    cna_skia_test(cna_test_skia_rendertargetcube_usage
                  examples/rendertargetcube_usage_test.cpp)
    cna_register_skia_display_test(Skia_RenderTargetCube_Usage
                                   cna_test_skia_rendertargetcube_usage)

    cna_skia_test(cna_test_skia_rendertargetcube_properties
                  examples/easygl_rendertargetcube_properties_test.cpp)
    cna_register_skia_display_test(Skia_RenderTargetCube_Properties
                                   cna_test_skia_rendertargetcube_properties)

    cna_skia_test(cna_test_skia_rendertargetcube_plural_binding
                  examples/rendertargetcube_plural_binding_test.cpp)
    target_compile_definitions(cna_test_skia_rendertargetcube_plural_binding PRIVATE
                               CNA_GFX096_CUBE_GETDATA=1)
    cna_register_skia_display_test(Skia_RenderTargetCube_PluralBinding
                                   cna_test_skia_rendertargetcube_plural_binding)

    # SKIA-87--SKIA-88: SkCanvas cannot represent distinct fragment outputs for MRT slots 0--3.
    # Verify that every valid 2--4 target set rejects before mutating target/state/pixels and that
    # shared validation errors retain their deterministic precedence.
    cna_skia_test(cna_test_skia_mrt_rejection examples/skia_mrt_rejection_test.cpp)
    cna_register_skia_display_test(Skia_MRT_Rejection cna_test_skia_mrt_rejection)

    cna_skia_test(cna_test_skia_resize_presentation examples/skia_resize_presentation_test.cpp)
    cna_register_skia_display_test(Skia_Resize_Presentation cna_test_skia_resize_presentation)

    cna_skia_test(cna_test_skia_context_recovery examples/skia_context_recovery_test.cpp)
    target_include_directories(cna_test_skia_context_recovery PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_ContextRecovery cna_test_skia_context_recovery)

    cna_skia_test(cna_test_skia_display_scale examples/skia_display_scale_test.cpp)
    target_include_directories(cna_test_skia_display_scale PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_DisplayScale cna_test_skia_display_scale)

    cna_skia_test(cna_test_skia_presentation_modes examples/skia_presentation_modes_test.cpp)
    target_include_directories(cna_test_skia_presentation_modes PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_PresentationModes cna_test_skia_presentation_modes)

    cna_skia_test(cna_test_skia_window_lifecycle examples/skia_window_lifecycle_test.cpp)
    target_include_directories(cna_test_skia_window_lifecycle PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_WindowLifecycle cna_test_skia_window_lifecycle)

    cna_skia_test(cna_test_skia_resource_budget examples/skia_resource_budget_test.cpp)
    target_include_directories(cna_test_skia_resource_budget PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_ResourceBudget cna_test_skia_resource_budget)

    cna_skia_test(cna_test_skia_presentation_edge examples/skia_presentation_edge_test.cpp)
    cna_register_skia_display_test(Skia_Presentation_Edge cna_test_skia_presentation_edge)

    if(TARGET cna_demo_2d)
        cna_register_skia_display_test(Skia_Demo2D_Smoke cna_demo_2d --smoke 3)
    endif()
endif()
