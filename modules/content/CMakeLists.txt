file(GLOB_RECURSE CNA_CONTENT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

cna_add_module(cna_content Content ${CNA_CONTENT_SOURCES})
target_link_libraries(cna_content PUBLIC cna_graphics_core cna_audio cna_media cna_math cna_core)
cna_link_sharp_runtime(cna_content PUBLIC Core.Base IO)
# plans/plan_cnj.md CNB-70 (Phase 13D): CNA::Internal::GltfImport::GltfImportCore (used by both
# ContentManager.cpp's GltfModelTypeReader and tools/gltf_to_cnj) needs cgltf.h.
# plans/plan_cnj.md CNB-88 (Phase 14E): RemapOcclusionImageForDualTextureEXT needs
# stb_image.h/stb_image_write.h.
# CNA_SOURCE_DIR, not CMAKE_SOURCE_DIR: these headers are vendored in *this* repository, and
# CMAKE_SOURCE_DIR points at the consumer's root whenever CNA is used via add_subdirectory.
target_include_directories(cna_content PRIVATE
        ${CNA_SOURCE_DIR}/third_party/cgltf
        ${CNA_SOURCE_DIR}/third_party/stb
)
if(CNA_DRACO_AVAILABLE)
    target_link_libraries(cna_content PRIVATE cna_draco)
endif()
