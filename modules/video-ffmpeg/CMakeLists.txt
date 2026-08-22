# SPDX-License-Identifier: MS-PL

file(GLOB_RECURSE CNA_VIDEO_FFMPEG_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_library(cna_video_ffmpeg STATIC ${CNA_VIDEO_FFMPEG_SOURCES})
add_library(CNA::VideoFfmpeg ALIAS cna_video_ffmpeg)
target_link_libraries(cna_video_ffmpeg PUBLIC cna_build_flags)
target_include_directories(cna_video_ffmpeg PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../media/include")
cna_link_sharp_runtime(cna_video_ffmpeg PRIVATE Core.Base)

# PkgConfig imported targets carry their matching include paths, link paths and libraries. No
# FFmpeg target is exposed by cna_media itself; this optional archive is the entire native edge.
target_link_libraries(cna_video_ffmpeg PRIVATE
    PkgConfig::LIBAVCODEC
    PkgConfig::LIBAVFORMAT
    PkgConfig::LIBAVUTIL
    PkgConfig::LIBSWRESAMPLE)
