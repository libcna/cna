# plans/plan_xnapipeline.md XNAP-51/XNAP-52/XNAP-90: build-time-only content pipeline components.
#
# This module exists to keep a boundary, not for tidiness. cna_content is linked by every game
# that loads content at run time; a font rasterizer is needed only while content is being built.
# Putting the .spritefont route here means FreeType never enters a runtime game's link closure,
# and gives the same home to any future build-time dependency (a texture-compression encoder, a
# model importer) instead of each one widening cna_content.
#
# Only cna_content_compiler links it, so the boundary is enforced by the dependency graph rather
# than by convention.
file(GLOB_RECURSE CNA_CONTENT_PIPELINE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

cna_add_module(cna_content_pipeline ContentPipelineBuild ${CNA_CONTENT_PIPELINE_SOURCES})
target_link_libraries(cna_content_pipeline PUBLIC cna_content)
cna_link_sharp_runtime(cna_content_pipeline PUBLIC Core.Base IO)

# CNA_ENABLE_FONT_PIPELINE is a three-state switch, matching CNA_ENABLE_VIDEO's shape:
#   OFF  -- do not probe or link FreeType; a .spritefont build fails with a message saying so.
#   AUTO -- use FreeType when the host provides it.
#   ON   -- require it, and fail configuration when it is absent.
set(CNA_FREETYPE_AVAILABLE OFF)
if(NOT CNA_ENABLE_FONT_PIPELINE STREQUAL "OFF")
    if(CNA_ENABLE_FONT_PIPELINE STREQUAL "ON")
        find_package(Freetype REQUIRED)
        set(CNA_FREETYPE_AVAILABLE ON)
    else()
        find_package(Freetype QUIET)
        if(Freetype_FOUND)
            set(CNA_FREETYPE_AVAILABLE ON)
        endif()
    endif()
endif()

if(CNA_FREETYPE_AVAILABLE)
    target_compile_definitions(cna_content_pipeline PRIVATE CNA_HAVE_FREETYPE=1)
    target_link_libraries(cna_content_pipeline PRIVATE Freetype::Freetype)
    message(STATUS
        "CNA: SpriteFont source pipeline enabled (${CNA_ENABLE_FONT_PIPELINE}; FreeType "
        "${FREETYPE_VERSION_STRING})")
else()
    message(STATUS
        "CNA: SpriteFont source pipeline disabled (${CNA_ENABLE_FONT_PIPELINE}); a .spritefont "
        "build reports that this configuration has no font rasterizer")
endif()
set(CNA_FREETYPE_AVAILABLE ${CNA_FREETYPE_AVAILABLE} PARENT_SCOPE)
