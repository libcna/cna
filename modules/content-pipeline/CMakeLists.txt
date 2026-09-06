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
cna_link_sharp_runtime(cna_content_pipeline PUBLIC Core.Base IO Collections.Core Xml)

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

# plans/plan_xnapipeline_parity.md XNAPP-201/202/220: the build-time media decoder.
#
# The same three-state shape CNA_ENABLE_FONT_PIPELINE has, and for the same reason: an MP3, a WMA
# or a WMV source needs a demuxer and a decoder while content is being built, and a game that
# loads the .xnb needs neither. Only cna_content_compiler links this module, so the dependency
# graph is what keeps FFmpeg out of a runtime game's link closure -- the runtime's own optional
# FFmpeg backend (modules/video-ffmpeg) is a separate thing and is not what these importers use.
#
#   OFF  -- do not probe or link it; an .mp3/.wma/.wmv build fails with a message saying so.
#   AUTO -- use it when the host provides it (CNA_FFMPEG_AVAILABLE, probed in modules/).
#   ON   -- require it, and fail configuration when it is absent.
set(CNA_ENABLE_MEDIA_PIPELINE "AUTO" CACHE STRING
    "Build-time MP3/WMA/WMV support for the XNA media importers: OFF, AUTO or ON")
set_property(CACHE CNA_ENABLE_MEDIA_PIPELINE PROPERTY STRINGS OFF AUTO ON)

set(CNA_BUILD_MEDIA_AVAILABLE OFF)
if(NOT CNA_ENABLE_MEDIA_PIPELINE STREQUAL "OFF")
    if(CNA_FFMPEG_AVAILABLE)
        set(CNA_BUILD_MEDIA_AVAILABLE ON)
    elseif(CNA_ENABLE_MEDIA_PIPELINE STREQUAL "ON")
        message(FATAL_ERROR
            "CNA: CNA_ENABLE_MEDIA_PIPELINE=ON but libavcodec/libavformat/libavutil/libswresample "
            "were not found. Install them or set CNA_ENABLE_MEDIA_PIPELINE=OFF.")
    endif()
endif()

if(CNA_BUILD_MEDIA_AVAILABLE)
    target_compile_definitions(cna_content_pipeline PRIVATE CNA_HAVE_BUILD_MEDIA=1)
    target_link_libraries(cna_content_pipeline PRIVATE
        PkgConfig::LIBAVCODEC PkgConfig::LIBAVFORMAT PkgConfig::LIBAVUTIL PkgConfig::LIBSWRESAMPLE)
    message(STATUS "CNA: MP3/WMA/WMV source pipeline enabled (${CNA_ENABLE_MEDIA_PIPELINE})")
else()
    message(STATUS
        "CNA: MP3/WMA/WMV source pipeline disabled (${CNA_ENABLE_MEDIA_PIPELINE}); those sources "
        "report that this configuration has no media decoder")
endif()
set(CNA_BUILD_MEDIA_AVAILABLE ${CNA_BUILD_MEDIA_AVAILABLE} PARENT_SCOPE)

# plans/plan_xnapipeline.md XNAP-A5: the effect compiler's configure-time tier.
#
# EffectCompilerService reads CNA_FXC_EXECUTABLE / CNA_FXC_LAUNCHER as compile definitions, and
# its own "no usable effect compiler" diagnostic tells users to pass them with -D. Until this
# block existed nothing turned the cache variable into a definition, so `-DCNA_FXC_EXECUTABLE=...`
# was accepted by CMake and silently ignored -- the same defect as an option a service documents
# and a parser does not implement, one tier down. Declared as cache entries so `cmake -LH` lists
# them, and defined only when set, because an empty definition is not the same as no definition.
set(CNA_FXC_EXECUTABLE "" CACHE STRING
    "Path to an fxc-compatible effect compiler, baked in as the .fx route's configure-time default")
set(CNA_FXC_LAUNCHER "" CACHE STRING
    "Program to run the effect compiler through (typically 'wine'), as the configure-time default")

foreach(_cna_fxc_setting IN ITEMS CNA_FXC_EXECUTABLE CNA_FXC_LAUNCHER)
    if(${_cna_fxc_setting})
        target_compile_definitions(cna_content_pipeline PRIVATE
            ${_cna_fxc_setting}="${${_cna_fxc_setting}}")
        message(STATUS "CNA: ${_cna_fxc_setting}=${${_cna_fxc_setting}}")
    endif()
endforeach()
unset(_cna_fxc_setting)
