file(GLOB_RECURSE CNA_CONTENT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

cna_add_module(cna_content Content ${CNA_CONTENT_SOURCES})
# plans/plan_content_pipeline.md CP-009: the equivalence-hardened glTF -> CNJ orchestration is
# still physically owned by the legacy tool while it is migrated. Compile that one implementation
# into the content library with only its CLI/oracle entry points suppressed; both cna-content and
# cna_tool_gltf_to_cnb then call the same linked implementation instead of compiling private copies.
target_sources(cna_content PRIVATE
        ${CNA_SOURCE_DIR}/tools/gltf_to_cnj/gltf_to_cnj.cpp
)
target_compile_definitions(cna_content PRIVATE CNA_GLTF_TO_CNJ_NO_MAIN=1)
target_link_libraries(cna_content PUBLIC cna_graphics_core cna_audio cna_media cna_math cna_core)
cna_link_sharp_runtime(cna_content PUBLIC Core.Base IO)
cna_link_sharp_runtime(cna_content PRIVATE Security.Cryptography)
# plans/plan_cnj.md CNB-70 (Phase 13D): CNA::Internal::GltfImport::GltfImportCore (used by both
# ContentManager.cpp's GltfModelTypeReader and tools/gltf_to_cnj) needs cgltf.h.
# plans/plan_cnj.md CNB-88 (Phase 14E): RemapOcclusionImageForDualTextureEXT needs
# stb_image.h/stb_image_write.h.
# CNA_SOURCE_DIR, not CMAKE_SOURCE_DIR: these headers are vendored in *this* repository, and
# CMAKE_SOURCE_DIR points at the consumer's root whenever CNA is used via add_subdirectory.
target_include_directories(cna_content PRIVATE
        ${CNA_SOURCE_DIR}/third_party/cgltf
        ${CNA_SOURCE_DIR}/third_party/stb
        ${CNA_SOURCE_DIR}/tools/gltf_to_cnj
)
if(CNA_CNB_ZSTD_AVAILABLE)
    target_compile_definitions(cna_content PRIVATE CNA_CNB_HAVE_ZSTD=1)
    target_include_directories(cna_content PRIVATE ${CNA_ZSTD_INCLUDE_DIRS})
    target_link_libraries(cna_content PRIVATE ${CNA_ZSTD_LIBRARIES})
endif()

if(CNA_DRACO_AVAILABLE)
    target_link_libraries(cna_content PRIVATE cna_draco)
endif()
