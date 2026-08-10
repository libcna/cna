# Skia and EasyGL API-contract comparison

SKIA-109 compares public API contracts only where the asserted operation can be preserved by the
accepted raster 2D renderer. It compiles the same source against Skia and EasyGL; renderer names and
explicit capability branches may differ, but exception precedence, caller-buffer integrity,
observable state, pixels, and object lifetime must not differ without a recorded reason.

## Shared results

Eight source pairs were added to the Skia suite in SKIA-109. Five earlier exact-source pairs cover
the remaining disposal and transfer requirements. All 13 pairs pass in both Debug configurations.

| Shared source / contract | Skia CTest | EasyGL CTest | Result and comparison |
|---|---|---|---|
| `easygl_device_validation_test.cpp` | `Skia_Contract_DeviceValidation` | `EasyGL_DeviceValidation` | Both enforce >16 before null-entry validation, null backbuffer data, and Present-while-target-bound. Sixteen live vertex bindings work on EasyGL; Skia instead verifies its stable SKIA-102 `CreateVertexBuffer` refusal. |
| `easygl_texture2d_partial_rect_test.cpp` | `Skia_Contract_Texture2D_PartialRect` | `EasyGL_Texture2D_PartialRect_RoundTrip` | Exact partial rectangle and non-zero `startIndex` upload/readback bytes on both. |
| `easygl_surface_format_throws_test.cpp` | `Skia_Contract_SurfaceFormat` | `EasyGL_SurfaceFormat_Throws` | `Color` construction succeeds and every listed unsupported format throws on 2D/cube/volume resource entry points. |
| `sprite_font_test.cpp` | `Skia_Contract_SpriteFontProperties` | `EasyGL_SpriteFont_Properties` | Exact metrics, fallback validation, setters, and character list. Rendering is intentionally not needed. |
| `viewport_reset_after_resize_test.cpp` | `Skia_Contract_ViewportResetAfterResize` | `EasyGL_ViewportResetAfterResize` | Custom viewport survives Present without resize and resets after the real SDL resize settles. |
| `backbuffer_first_read_test.cpp` | `Skia_Contract_BackbufferFirstRead` | `EasyGL_BackbufferFirstRead` | Every fork-isolated first read synchronously replaces poison and returns the exact 37x23 pattern, including subrect and guarded destination windows. |
| `backbuffer_headless_reject_test.cpp` | `Skia_Contract_BackbufferReject` | `EasyGL_BackbufferReject` | Both raster renderers take the success branch while preserving validation precedence and caller guards. |
| `rendertarget_pass_boundary_test.cpp` | `Skia_Contract_RenderTargetPassBoundary` | `EasyGL_RenderTarget_PassBoundary` | Exact 2D/cube Preserve/Discard, ordered Clear, viewport/scissor, switching, and readback results. EasyGL additionally runs real-MSAA legs; Skia declares and skips them because requests above one sample are rejected. |
| `easygl_disposed_resource_test.cpp` | `Skia_EasyGL_DisposedResource` | `EasyGL_DisposedResource` | Exact disposed-object exception behavior, including shared pre-renderer validation. |
| `texture2d_getdata_contract_test.cpp` | `Skia_Texture2D_GetDataContract` | `EasyGL_Texture2D_GetDataContract` | Exact element-size, rectangle, capacity, level, and destination-integrity matrix. |
| `texture2d_getdata_transfer_range_test.cpp` | `Skia_Texture2D_GetDataTransferRange` | `EasyGL_Texture2D_GetDataTransferRange` | Exact `startIndex`/`elementCount` destination-window behavior. |
| `texturecube_texture3d_getdata_contract_test.cpp` | `Skia_TextureStorage_GetDataContract` | `EasyGL_CubeVolume_GetDataContract` | All 56 cube/volume readback and rejection checks pass. Skia supplies the previously accepted bounded CPU transfer store, not sampling. |
| `texturecube_texture3d_setdata_contract_test.cpp` | `Skia_TextureStorage_SetDataContract` | `EasyGL_CubeVolume_SetDataContract` | All 56 cube/volume upload and rejection checks pass under the same transfer-only boundary. |

## Explained exclusions

The initial inventory attempted five EasyGL lifecycle fixtures that had been labelled
`2d-direct`: bound-resource disposal, move semantics, resource events, device-dispose ordering,
and the 80-resource leak loop. Each has mandatory `VertexBuffer` and `IndexBuffer` construction,
so each correctly reaches Skia's stable `CreateVertexBuffer` refusal before completing. The same
is true of EasyGL's broad double-disposal fixture. These six rows are now classified `3d` under
the matrix's own most-demanding-mandatory-leg rule; they are not weakened with conditional skips.

The 2D resource behavior they overlap remains executable through `Skia_Texture2D_Dispose`,
`Skia_DisposedGuards`, `Skia_DoubleDispose`, `Skia_Ownership`, `Skia_RenderTarget2D_Lifetime`,
`Skia_RenderTargetCube_GetDataContract`, and `Skia_ResourceBudget`. Buffer construction itself is
covered by `Skia_3D_Refusal`. Thus the comparison records a deliberate scope boundary, not an
unexplained contract discrepancy.

The broad EasyGL RenderTarget2D property fixture also requires multisampled targets. Raster Skia
still intentionally refuses real sample counts, while SKIA-131–132 now support mipmapped Color
targets with deterministic resolve-generated descendants. Their property, generation and refusal
contracts are covered by `Skia_RenderTarget2D_MipStorage`, `MipGeneration`,
`Skia_EasyGL_RenderTarget2D_MipComplete`, and `Skia_RenderTarget2D_MsaaPolicy` without weakening
the EasyGL expectation.

## Test defect found

`EasyGL_DeviceValidation` originally populated a vector with 16 default `VertexBufferBinding`
objects and expected no exception. Every entry was null, so the shared API correctly threw
`ArgumentNullException`; both EasyGL and Skia failed the same stale assertion. The fixture now
separates three cases: 17 entries throw `ArgumentOutOfRangeException`, 16 null entries throw
`ArgumentNullException`, and 16 live entries either succeed on a 3D renderer or reach Skia's
explicit 3D resource boundary. This restores a real validation-precedence test instead of hiding
the null-entry contract.

## Validation

- The eight new Skia registrations pass 8/8 in Debug (13.22 seconds), Release (13.36 seconds),
  and ASan with `detect_leaks=0` (13.60 seconds).
- Their eight EasyGL counterparts pass 8/8 in Debug (11.67 seconds).
- The five previously registered shared contracts pass 5/5 under Skia (1.83 seconds) and EasyGL
  (1.19 seconds).
- The complete Debug Skia suite passes 132/132 in 21.66 seconds with `--parallel 8`: 16 Raster,
  113 Display, and three Audit tests.
- The 347-entry EasyGL/Skia matrix, 248-row capability ledger, and SKIA-102 3D-refusal audit all
  pass after the classification corrections.
