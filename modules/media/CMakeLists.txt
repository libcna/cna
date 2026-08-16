file(GLOB_RECURSE CNA_MEDIA_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# Exclude FFmpeg-dependent sources on platforms where FFmpeg is unavailable.
if(NOT CNA_FFMPEG_AVAILABLE)
    list(FILTER CNA_MEDIA_SOURCES EXCLUDE REGEX ".*/src/Internal/VideoDecoder\\.cpp$")
    list(FILTER CNA_MEDIA_SOURCES EXCLUDE REGEX ".*/src/Xna/Video/VideoPlayer\\.cpp$")
    list(FILTER CNA_MEDIA_SOURCES EXCLUDE REGEX ".*/src/Xna/Video/Video\\.cpp$")
endif()

cna_add_module(cna_media Media ${CNA_MEDIA_SOURCES})
target_link_libraries(cna_media PUBLIC cna_audio cna_graphics_core)
cna_link_sharp_runtime(cna_media PUBLIC Core.Base IO)
# MediaPlayer.cpp compiles FrameworkDispatcher.hpp (the accepted audio<->media pump cycle),
# whose declarations reach into TouchPanel -- an input-module include the global root used
# to satisfy silently. The input archive was already in every media link closure through
# audio's own private edge, so this states the real compile dependency without changing
# the accepted link closure.
target_link_libraries(cna_media PRIVATE cna_input)

if(CNA_FFMPEG_AVAILABLE)
    # PkgConfig::<prefix> IMPORTED targets (see modules/CMakeLists.txt) carry their own
    # INTERFACE include dirs, link dirs, and libraries together, so this is the only
    # target_link_libraries() call needed -- no separate target_include_directories()/
    # target_link_directories() call, unlike raw _LIBRARIES/_INCLUDE_DIRS variables.
    target_link_libraries(cna_media PRIVATE
        PkgConfig::LIBAVCODEC
        PkgConfig::LIBAVFORMAT
        PkgConfig::LIBAVUTIL
        PkgConfig::LIBSWRESAMPLE
    )
endif()
