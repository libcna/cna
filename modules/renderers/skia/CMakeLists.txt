# SKIA-160: CNA_SKIA_LINK_TARGET is CNA::Skia (raster, default) or CNA::SkiaGanesh (GANESH
# mode), set by BackendSelection.cmake's CNA_SKIA_MODE branch -- never both; the two are
# mutually exclusive GN builds of the same checkout and cannot be linked into one binary.
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ${CNA_SKIA_LINK_TARGET})
# SKIA-140/141: SkiaTextureBackend.cpp calls CNA::Internal::Graphics::DxtUtil/Bc7Util, which
# live in the CNA graphics core module, not this backend target -- the reverse edge every
# backend now declares (cna_renderer_backend_common_setup) covers exactly this dependency
# (found originally via a real "undefined reference to Bc7Util::DecompressBc7" failure on
# cna_reference_dump).
# The pinned upstream raster archives are built with `skia_enable_ganesh=false` and RTTI
# disabled. GCC/Clang's broad `undefined` group otherwise instruments virtual calls made by
# this adapter with `vptr` checks and then requires typeinfo symbols (for example SkCanvas)
# which those archives deliberately do not export. Keep every other UBSan check enabled and
# exclude only the incompatible RTTI-dependent check at the third-party boundary.
if(CNA_SANITIZE MATCHES "(^|,)undefined(,|$)"
   AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${BACKEND_TARGET} PRIVATE -fno-sanitize=vptr)
endif()
