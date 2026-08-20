#pragma once

namespace CNA::Internal::Renderers::OpenGL1
{
    /**
     * @brief Runtime-detected OpenGL version/extension support for the OPENGL1 renderer.
     *
     * Computed once, right after the GL context is made current (in
     * OpenGL1Renderer's constructor), from glGetString(GL_VERSION)/
     * glGetString(GL_EXTENSIONS) directly -- no GL/GLES loader library and no EasyGL
     * dependency (plans/plan_opengl1.md's own scope rule: OPENGL1 "MUST NOT depend on EasyGL").
     * Consulted by later OPENGL1 phases (RenderTarget2D via FBO, DualTextureEffect via
     * multitexture, cube maps, mipmap generation) instead of assuming a fixed OpenGL 1.1
     * baseline; a strict 1.1 driver simply reports every flag here as false and those
     * features stay unavailable, matching plans/plan_opengl1.md's documented "Intentional OpenGL
     * 1.x limitations".
     */
    struct OpenGL1Capabilities
    {
        /** @brief Parsed major version from GL_VERSION (e.g. 4 for "4.6.0 ..."). */
        int versionMajor = 1;
        /** @brief Parsed minor version from GL_VERSION. */
        int versionMinor = 1;

        /** @brief EXT_framebuffer_object/ARB_framebuffer_object or core (>=3.0) -- needed for RenderTarget2D/cube via FBO. */
        bool framebufferObject = false;
        /** @brief ARB_multitexture or core (>=1.3) -- needed for a DualTextureEffect fixed-function path. */
        bool multitexture = false;
        /** @brief ARB/EXT_texture_cube_map or core (>=1.3) -- needed for EnvironmentMapEffect cube maps. */
        bool textureCubeMap = false;
        /** @brief SGIS_generate_mipmap/core GL_GENERATE_MIPMAP (>=1.4), or framebufferObject's glGenerateMipmap -- needed for mipmap generation. */
        bool generateMipmap = false;
        /** @brief EXT_texture_filter_anisotropic -- drives GraphicsCapability::AnisotropicFiltering and ApplySamplerState's maxAnisotropy. */
        bool anisotropicFiltering = false;
        /** @brief GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, the driver's real ceiling. 1.0f (no-op value) when anisotropicFiltering is false. */
        float maxAnisotropy = 1.0f;
        /** @brief ARB_occlusion_query or core (>=1.5) -- needed for real OcclusionQuery support (item 23). */
        bool occlusionQuery = false;
        /** @brief Core (>=1.4) -- needed for glBlendColor/glBlendFuncSeparate/glBlendEquationSeparate (items 17/18/19). */
        bool extendedBlend = false;
    };

    /**
     * @brief Queries the current OpenGL context (must already be current) for its version and
     * the extension/capability subset the OPENGL1 renderer cares about.
     *
     * @return The detected capabilities.
     */
    OpenGL1Capabilities DetectOpenGL1Capabilities();
}
