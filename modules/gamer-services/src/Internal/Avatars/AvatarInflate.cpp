// SPDX-License-Identifier: MS-PL
// The compiled-in avatar catalog is stored deflated (cmake/EmbedBinaryFiles.cmake COMPRESS); stb_image's
// zlib decoder inflates it. Only that decoder is compiled here, privately to this file.
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_image.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace CNA::Internal::GamerServices::Avatars {
std::vector<std::uint8_t> inflateEmbedded(const EmbeddedFile& file)
{
    if(file.inflatedSize==0)return {file.data,file.data+file.size};
    int length=0;
    char* inflated=stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(file.data),static_cast<int>(file.size),&length);
    if(!inflated)throw std::runtime_error(std::string("avatar catalog file ")+file.name+" could not be inflated");
    std::vector<std::uint8_t> out(reinterpret_cast<std::uint8_t*>(inflated),reinterpret_cast<std::uint8_t*>(inflated)+length);
    std::free(inflated);
    if(out.size()!=file.inflatedSize)throw std::runtime_error(std::string("avatar catalog file ")+file.name+" inflated to the wrong size");
    return out;
}
}
