file(GLOB_RECURSE CNA_MEDIA_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# The stable XNA-facing Video and VideoPlayer implementations always live in cna_media. Only the
# decoder implementation varies: cna_video_ffmpeg supplies it when enabled, while these two small
# fallback TUs preserve the same link-complete API without any FFmpeg headers or libraries.
if(CNA_FFMPEG_AVAILABLE)
    list(FILTER CNA_MEDIA_SOURCES EXCLUDE REGEX
        ".*/src/Internal/AudioDurationProbeUnavailable\\.cpp$")
    list(FILTER CNA_MEDIA_SOURCES EXCLUDE REGEX
        ".*/src/Internal/VideoDecoderUnavailable\\.cpp$")
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
