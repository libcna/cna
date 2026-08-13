#include "CNA/Internal/Renderers/OpenGL1/OpenGL1Capabilities.hpp"
#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <cstdio>
#include <string>

namespace CNA::Internal::Renderers::OpenGL1
{
namespace
{
    // `padded` must already be wrapped in leading/trailing spaces (see DetectOpenGL1Capabilities
    // below) so this matches whole extension tokens, not a substring of an unrelated one.
    bool HasExtensionToken(const std::string& padded, const char* name)
    {
        const std::string token = std::string(" ") + name + " ";
        return padded.find(token) != std::string::npos;
    }
}

OpenGL1Capabilities DetectOpenGL1Capabilities()
{
    OpenGL1Capabilities caps;

    if (const GLubyte* versionStr = glGetString(GL_VERSION))
    {
        // GL_VERSION is "major.minor[.release] [vendor-specific info]" -- scanning just the
        // leading "%d.%d" is robust to both the plain form and a compatibility-profile driver's
        // vendor suffix (e.g. "4.6.0 NVIDIA 550.100" or "4.6 (Compatibility Profile) Mesa ...").
        int major = 1, minor = 1;
        if (std::sscanf(reinterpret_cast<const char*>(versionStr), "%d.%d", &major, &minor) == 2)
        {
            caps.versionMajor = major;
            caps.versionMinor = minor;
        }
    }

    std::string extensions;
    if (const GLubyte* extStr = glGetString(GL_EXTENSIONS))
        extensions = reinterpret_cast<const char*>(extStr);
    const std::string padded = " " + extensions + " ";

    auto coreAtLeast = [&](int major, int minor)
    {
        return caps.versionMajor > major || (caps.versionMajor == major && caps.versionMinor >= minor);
    };

    caps.multitexture = coreAtLeast(1, 3) || HasExtensionToken(padded, "GL_ARB_multitexture");
    caps.textureCubeMap = coreAtLeast(1, 3)
        || HasExtensionToken(padded, "GL_ARB_texture_cube_map")
        || HasExtensionToken(padded, "GL_EXT_texture_cube_map");
    caps.framebufferObject = coreAtLeast(3, 0)
        || HasExtensionToken(padded, "GL_ARB_framebuffer_object")
        || HasExtensionToken(padded, "GL_EXT_framebuffer_object");
    caps.generateMipmap = coreAtLeast(1, 4)
        || HasExtensionToken(padded, "GL_SGIS_generate_mipmap")
        || caps.framebufferObject;
    caps.occlusionQuery = coreAtLeast(1, 5) || HasExtensionToken(padded, "GL_ARB_occlusion_query");
    caps.extendedBlend = coreAtLeast(1, 4);
    caps.anisotropicFiltering = HasExtensionToken(padded, "GL_EXT_texture_filter_anisotropic");
    if (caps.anisotropicFiltering)
    {
        GLfloat maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        caps.maxAnisotropy = maxAniso;
    }

    return caps;
}
}
