file(GLOB_RECURSE CNA_CONTENT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# REMED-BUILD-013 (discovered while verifying it): VideoContentTypeReader.cpp constructs a
# Video (Media::Video::Video(...)) -- a genuine, previously-latent gap in the FFmpeg
# exclusion list, never hit before because every FFmpeg-unavailable target (MinGW
# D3D9/D3D11/D3D12, Emscripten, Android) that links a full CNA never got far enough in a
# real build to reach it.
if(NOT CNA_FFMPEG_AVAILABLE)
    list(FILTER CNA_CONTENT_SOURCES EXCLUDE REGEX ".*/src/Xnb/VideoContentTypeReader\\.cpp$")
endif()

cna_add_module(cna_content Content ${CNA_CONTENT_SOURCES})
target_link_libraries(cna_content PUBLIC cna_graphics_core cna_audio cna_media cna_math cna_core)
cna_link_sharp_runtime(cna_content PUBLIC Core.Base IO)
target_link_libraries(cna_content PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)
# plan_cnj.md CNB-70 (Phase 13D): CNA::Internal::GltfImport::GltfImportCore (used by both
# ContentManager.cpp's GltfModelTypeReader and tools/gltf_to_cnj) needs cgltf.h.
# plan_cnj.md CNB-88 (Phase 14E): RemapOcclusionImageForDualTextureEXT needs
# stb_image.h/stb_image_write.h.
target_include_directories(cna_content PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party/cgltf
        ${PROJECT_SOURCE_DIR}/third_party/stb
)
if(CNA_DRACO_AVAILABLE)
    target_link_libraries(cna_content PRIVATE draco::draco)
endif()
