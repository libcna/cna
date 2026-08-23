#pragma once

// plans/plan_runtimerenderer.md phase P11: EasyGL's GL profile as a RUNTIME value.
//
// EasyGL is the one implementation family serving several public renderer identities -- OPENGLES2,
// OPENGLES3, OPENGL33, WEBGL1 and WEBGL2 (plans/plan_glbackends.md). Until this header existed the choice
// between them was a compile definition (CNA_GL_PROFILE_*), which is why two of them could never
// coexist in one binary: the same translation units would have had to be compiled twice with
// different defines, an ODR violation rather than a mere name clash.
//
// What made this tractable is that EasyGL never carried five shader corpora. It stores ONE GLSL
// ES 3.00 source per shader and adapts it -- to "#version 330 core" for desktop core profile, or to
// "#version 100" plus a syntax transform for the ES 2.0 dialects. That adaptation was already a
// runtime string transform; only its selection was compile-time.

#include "CNA/GraphicsRendererType.hpp"

namespace CNA::Internal::Renderers::EasyGL
{
    /**
     * @brief Which of EasyGL's five public GL identities a renderer instance is serving.
     *
     * One enumerator per public identity rather than per GLSL generation, deliberately: the
     * generations do not partition the identities cleanly (OPENGLES2 and WEBGL1 share a shader
     * dialect but reach it through different context types), and the existing code distinguishes
     * them at several points. Deriving generation from the identity is the job of the predicates
     * below, which keeps a single place to correct if one of those distinctions turns out to be
     * wrong.
     */
    enum class GlProfile
    {
        /** @brief Native OpenGL ES 2.0 context, GLSL ES 1.00. */
        OpenGLES2,

        /** @brief Native OpenGL ES 3.0 context, GLSL ES 3.00. EasyGL's default profile. */
        OpenGLES3,

        /** @brief Desktop OpenGL 3.3 core profile, GLSL 3.30. */
        OpenGL33,

        /** @brief Browser WebGL 1 context, GLSL ES 1.00. Emscripten only. */
        WebGL1,

        /** @brief Browser WebGL 2 context, GLSL ES 3.00. Emscripten only. */
        WebGL2
    };

    /**
     * @brief The profile this build was configured for.
     *
     * The one remaining reader of the CNA_GL_PROFILE_* compile definitions, kept so a
     * single-renderer build keeps its existing default without every call site naming a profile.
     * In a multi-renderer build each descriptor names its own profile explicitly and this value is
     * merely the default of the build's default identity.
     */
    inline constexpr GlProfile kCompileTimeGlProfile =
#if defined(CNA_GL_PROFILE_OPENGL33)
        GlProfile::OpenGL33;
#elif defined(CNA_GL_PROFILE_WEBGL1)
        GlProfile::WebGL1;
#elif defined(CNA_GL_PROFILE_WEBGL2)
        GlProfile::WebGL2;
#elif defined(CNA_GL_PROFILE_OPENGLES2)
        GlProfile::OpenGLES2;
#else
        GlProfile::OpenGLES3;
#endif

    /**
     * @brief The GL profile of the context currently active on this thread.
     *
     * EasyGL's implementation is spread across member functions and free helpers, and the profile
     * has to be reachable from all of them. Threading it through every signature would touch far
     * more code than the behaviour being changed, so it lives here instead.
     *
     * Thread-local because that is what it actually describes. OpenGL state is already
     * thread-and-current-context scoped: every GL call in this renderer applies to whichever
     * context is current on the calling thread, and the profile is a property of that context. So
     * this mirrors the model the surrounding code already obeys rather than adding a new one.
     *
     * Set by EasyGLRenderer's constructor and whenever it makes its context current. The one shape
     * it cannot describe is two EasyGL renderers with different profiles being used from the same
     * thread without either making its context current in between -- which is already impossible,
     * since their GL calls would be going to whichever context happened to be current.
     *
     * @return A reference to this thread's active profile.
     */
    [[nodiscard]] inline GlProfile& ActiveGlProfile()
    {
        static thread_local GlProfile profile = kCompileTimeGlProfile;
        return profile;
    }

    /**
     * @brief Maps a public renderer identity to the EasyGL profile serving it.
     *
     * @param type One of the five GL identities EasyGL implements.
     * @return The corresponding profile; OpenGLES3 (EasyGL's default) for any other identity.
     */
    [[nodiscard]] constexpr GlProfile ToGlProfile(CNA::GraphicsRendererType type)
    {
        switch (type)
        {
            case CNA::GraphicsRendererType::OpenGLES2: return GlProfile::OpenGLES2;
            case CNA::GraphicsRendererType::OpenGL33:  return GlProfile::OpenGL33;
            case CNA::GraphicsRendererType::WebGL1:    return GlProfile::WebGL1;
            case CNA::GraphicsRendererType::WebGL2:    return GlProfile::WebGL2;
            default:                                    return GlProfile::OpenGLES3;
        }
    }

    /**
     * @brief The public renderer identity a profile serves.
     *
     * @param profile The profile.
     * @return Its public identity.
     */
    [[nodiscard]] constexpr CNA::GraphicsRendererType ToRendererType(GlProfile profile)
    {
        switch (profile)
        {
            case GlProfile::OpenGLES2: return CNA::GraphicsRendererType::OpenGLES2;
            case GlProfile::OpenGL33:  return CNA::GraphicsRendererType::OpenGL33;
            case GlProfile::WebGL1:    return CNA::GraphicsRendererType::WebGL1;
            case GlProfile::WebGL2:    return CNA::GraphicsRendererType::WebGL2;
            case GlProfile::OpenGLES3: return CNA::GraphicsRendererType::OpenGLES3;
        }
        return CNA::GraphicsRendererType::OpenGLES3;
    }

    /**
     * @brief Whether this profile compiles GLSL ES 1.00 rather than GLSL ES 3.00 / GLSL 3.30.
     *
     * True for both ES 2.0-dialect profiles. The shader adaptation is identical for them: the
     * shader text a WebGL 1 context accepts and the text a native OpenGL ES 2.0 context accepts are
     * the same dialect, reached through different context types.
     *
     * @param profile The profile.
     * @return true for OpenGLES2 and WebGL1.
     */
    [[nodiscard]] constexpr bool UsesGlslEs100(GlProfile profile)
    {
        return profile == GlProfile::OpenGLES2 || profile == GlProfile::WebGL1;
    }

    /**
     * @brief Whether this profile is a desktop OpenGL core profile rather than a GLES/WebGL one.
     *
     * @param profile The profile.
     * @return true only for OpenGL33.
     */
    [[nodiscard]] constexpr bool IsDesktopCoreProfile(GlProfile profile)
    {
        return profile == GlProfile::OpenGL33;
    }

    /**
     * @brief Reports whether a profile uses the OpenGL ES 2.0 API generation.
     *
     * WebGL 1 shares the relevant API limits with OpenGL ES 2.0: neither has separate read
     * framebuffers, sized RGBA8 texture storage, base-vertex draws, or core instanced draws.
     *
     * @param profile The profile to test.
     * @return true for OpenGLES2 and WebGL1.
     */
    [[nodiscard]] constexpr bool UsesEs2ApiGeneration(GlProfile profile)
    {
        return profile == GlProfile::OpenGLES2 || profile == GlProfile::WebGL1;
    }

    /**
     * @brief Reports whether indexed base-vertex draws require pointer rebasing.
     *
     * `glDrawElementsBaseVertex` is core in desktop OpenGL 3.2, but only in OpenGL ES 3.2
     * and is absent from both WebGL generations. CNA requests an ES 3.0 context for its
     * OpenGLES3 identity, so that profile cannot assume the entry point even when a local
     * driver happens to return a newer context.
     *
     * @param profile The profile to test.
     * @return false only for CNA's OpenGL 3.3 desktop-core profile.
     */
    [[nodiscard]] constexpr bool RequiresBaseVertexPointerRebase(GlProfile profile)
    {
        return profile != GlProfile::OpenGL33;
    }
}
