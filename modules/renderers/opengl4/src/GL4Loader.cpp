// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/OpenGL4/GL4Loader.hpp"

#include <cstdio>
#include <cstring>

namespace CNA::Internal::Renderers::OpenGL4::GL4
{
    PFNGL4GENBUFFERSPROC               gl4_glGenBuffers               = nullptr;
    PFNGL4BINDBUFFERPROC               gl4_glBindBuffer               = nullptr;
    PFNGL4BUFFERDATAPROC               gl4_glBufferData               = nullptr;
    PFNGL4BUFFERSUBDATAPROC            gl4_glBufferSubData            = nullptr;
    PFNGL4DELETEBUFFERSPROC            gl4_glDeleteBuffers            = nullptr;

    PFNGL4GENVERTEXARRAYSPROC          gl4_glGenVertexArrays          = nullptr;
    PFNGL4BINDVERTEXARRAYPROC          gl4_glBindVertexArray          = nullptr;
    PFNGL4DELETEVERTEXARRAYSPROC       gl4_glDeleteVertexArrays       = nullptr;
    PFNGL4VERTEXATTRIBPOINTERPROC      gl4_glVertexAttribPointer      = nullptr;
    PFNGL4ENABLEVERTEXATTRIBARRAYPROC  gl4_glEnableVertexAttribArray  = nullptr;
    PFNGL4DISABLEVERTEXATTRIBARRAYPROC gl4_glDisableVertexAttribArray = nullptr;
    PFNGL4VERTEXATTRIBIPOINTERPROC     gl4_glVertexAttribIPointer     = nullptr;

    PFNGL4CREATESHADERPROC             gl4_glCreateShader             = nullptr;
    PFNGL4SHADERSOURCEPROC             gl4_glShaderSource             = nullptr;
    PFNGL4COMPILESHADERPROC            gl4_glCompileShader            = nullptr;
    PFNGL4GETSHADERIVPROC              gl4_glGetShaderiv              = nullptr;
    PFNGL4GETSHADERINFOLOGPROC         gl4_glGetShaderInfoLog         = nullptr;
    PFNGL4DELETESHADERPROC             gl4_glDeleteShader             = nullptr;
    PFNGL4CREATEPROGRAMPROC            gl4_glCreateProgram            = nullptr;
    PFNGL4ATTACHSHADERPROC             gl4_glAttachShader             = nullptr;
    PFNGL4LINKPROGRAMPROC              gl4_glLinkProgram              = nullptr;
    PFNGL4GETPROGRAMIVPROC             gl4_glGetProgramiv             = nullptr;
    PFNGL4GETPROGRAMINFOLOGPROC        gl4_glGetProgramInfoLog        = nullptr;
    PFNGL4DELETEPROGRAMPROC            gl4_glDeleteProgram            = nullptr;
    PFNGL4USEPROGRAMPROC               gl4_glUseProgram               = nullptr;
    PFNGL4BINDATTRIBLOCATIONPROC       gl4_glBindAttribLocation       = nullptr;

    PFNGL4GETUNIFORMLOCATIONPROC       gl4_glGetUniformLocation       = nullptr;
    PFNGL4UNIFORM1IPROC                gl4_glUniform1i                = nullptr;
    PFNGL4UNIFORM1FPROC                gl4_glUniform1f                = nullptr;
    PFNGL4UNIFORM2FPROC                gl4_glUniform2f                = nullptr;
    PFNGL4UNIFORM3FPROC                gl4_glUniform3f                = nullptr;
    PFNGL4UNIFORM4FPROC                gl4_glUniform4f                = nullptr;
    PFNGL4UNIFORM1FVPROC               gl4_glUniform1fv               = nullptr;
    PFNGL4UNIFORM2FVPROC               gl4_glUniform2fv               = nullptr;
    PFNGL4UNIFORMMATRIX4FVPROC         gl4_glUniformMatrix4fv         = nullptr;

    PFNGL4ACTIVETEXTUREPROC            gl4_glActiveTexture            = nullptr;
    PFNGL4GENERATEMIPMAPPROC           gl4_glGenerateMipmap           = nullptr;

    PFNGL4BLENDFUNCSEPARATEPROC        gl4_glBlendFuncSeparate        = nullptr;
    PFNGL4BLENDEQUATIONSEPARATEPROC    gl4_glBlendEquationSeparate    = nullptr;
    PFNGL4BLENDCOLORPROC               gl4_glBlendColor               = nullptr;

    PFNGL4GENSAMPLERSPROC              gl4_glGenSamplers              = nullptr;
    PFNGL4DELETESAMPLERSPROC           gl4_glDeleteSamplers           = nullptr;
    PFNGL4BINDSAMPLERPROC              gl4_glBindSampler              = nullptr;
    PFNGL4SAMPLERPARAMETERIPROC        gl4_glSamplerParameteri        = nullptr;
    PFNGL4SAMPLERPARAMETERFPROC        gl4_glSamplerParameterf        = nullptr;

    PFNGL4GENFRAMEBUFFERSPROC             gl4_glGenFramebuffers             = nullptr;
    PFNGL4BINDFRAMEBUFFERPROC             gl4_glBindFramebuffer             = nullptr;
    PFNGL4DELETEFRAMEBUFFERSPROC          gl4_glDeleteFramebuffers          = nullptr;
    PFNGL4FRAMEBUFFERTEXTURE2DPROC        gl4_glFramebufferTexture2D        = nullptr;
    PFNGL4CHECKFRAMEBUFFERSTATUSPROC      gl4_glCheckFramebufferStatus      = nullptr;
    PFNGL4GENRENDERBUFFERSPROC            gl4_glGenRenderbuffers            = nullptr;
    PFNGL4BINDRENDERBUFFERPROC            gl4_glBindRenderbuffer            = nullptr;
    PFNGL4DELETERENDERBUFFERSPROC         gl4_glDeleteRenderbuffers         = nullptr;
    PFNGL4RENDERBUFFERSTORAGEPROC         gl4_glRenderbufferStorage         = nullptr;
    PFNGL4RENDERBUFFERSTORAGEMULTISAMPLEPROC gl4_glRenderbufferStorageMultisample = nullptr;
    PFNGL4FRAMEBUFFERRENDERBUFFERPROC     gl4_glFramebufferRenderbuffer     = nullptr;
    PFNGL4BLITFRAMEBUFFERPROC             gl4_glBlitFramebuffer             = nullptr;
    PFNGL4DRAWBUFFERSPROC                 gl4_glDrawBuffers                 = nullptr;

    PFNGL4STENCILFUNCSEPARATEPROC         gl4_glStencilFuncSeparate         = nullptr;
    PFNGL4STENCILOPSEPARATEPROC           gl4_glStencilOpSeparate           = nullptr;
    PFNGL4STENCILMASKSEPARATEPROC         gl4_glStencilMaskSeparate         = nullptr;
    PFNGL4COLORMASKIPROC                  gl4_glColorMaski                  = nullptr;

    PFNGL4TEXIMAGE3DPROC                  gl4_glTexImage3D                  = nullptr;
    PFNGL4TEXSUBIMAGE3DPROC               gl4_glTexSubImage3D               = nullptr;
    PFNGL4FRAMEBUFFERTEXTURELAYERPROC     gl4_glFramebufferTextureLayer     = nullptr;

    PFNGL4GENQUERIESPROC                  gl4_glGenQueries                  = nullptr;
    PFNGL4DELETEQUERIESPROC               gl4_glDeleteQueries               = nullptr;
    PFNGL4BEGINQUERYPROC                  gl4_glBeginQuery                  = nullptr;
    PFNGL4ENDQUERYPROC                    gl4_glEndQuery                    = nullptr;
    PFNGL4GETQUERYOBJECTUIVPROC           gl4_glGetQueryObjectuiv           = nullptr;

    PFNGL4DRAWELEMENTSBASEVERTEXPROC      gl4_glDrawElementsBaseVertex      = nullptr;

    PFNGL4DRAWELEMENTSINSTANCEDPROC       gl4_glDrawElementsInstanced       = nullptr;
    PFNGL4VERTEXATTRIBDIVISORPROC         gl4_glVertexAttribDivisor         = nullptr;

    PFNGL4GETSTRINGIPROC                  gl4_glGetStringi                  = nullptr;
    PFNGL4DISPATCHCOMPUTEPROC             gl4_glDispatchCompute            = nullptr;
    PFNGL4BINDBUFFERBASEPROC              gl4_glBindBufferBase              = nullptr;
    PFNGL4GETINTEGERI_VPROC               gl4_glGetIntegeri_v               = nullptr;
    PFNGL4BINDIMAGETEXTUREPROC            gl4_glBindImageTexture            = nullptr;
    PFNGL4MEMORYBARRIERPROC               gl4_glMemoryBarrier               = nullptr;
    PFNGL4DRAWARRAYSINDIRECTPROC          gl4_glDrawArraysIndirect          = nullptr;
    PFNGL4DRAWELEMENTSINDIRECTPROC        gl4_glDrawElementsIndirect        = nullptr;
    PFNGL4QUERYCOUNTERPROC                gl4_glQueryCounter                = nullptr;
    PFNGL4GETQUERYOBJECTUI64VPROC         gl4_glGetQueryObjectui64v         = nullptr;
    PFNGL4DRAWELEMENTSINSTANCEDBASEVERTEXBASEINSTANCEPROC
        gl4_glDrawElementsInstancedBaseVertexBaseInstance = nullptr;
    PFNGL4GETINTERNALFORMATIVPROC         gl4_glGetInternalformativ         = nullptr;

    namespace
    {
        template <typename Fn>
        bool Resolve(GetProcAddressFn getProcAddress, const char* name, Fn& out)
        {
            void* p = getProcAddress(name);
            if (!p)
            {
                std::fprintf(stderr, "CNA: OpenGL4 renderer: failed to resolve GL entry point '%s'\n", name);
                return false;
            }
            out = reinterpret_cast<Fn>(p);
            return true;
        }

        template <typename Fn>
        void ResolveOptional(GetProcAddressFn getProcAddress, const char* name, Fn& out)
        {
            out = reinterpret_cast<Fn>(getProcAddress(name));
        }

        [[nodiscard]] bool VersionAtLeast(const int major, const int minor,
                                          const int requiredMajor, const int requiredMinor)
        {
            return major > requiredMajor ||
                   (major == requiredMajor && minor >= requiredMinor);
        }

        [[nodiscard]] bool QueryFloatRenderability(const GLenum internalFormat, bool& supported)
        {
            while (glGetError() != GL_NO_ERROR) {}
            GLint available = GL_FALSE;
            GLint renderable = GL_NONE;
            gl4_glGetInternalformativ(GL_TEXTURE_2D, internalFormat,
                                      GL_INTERNALFORMAT_SUPPORTED, 1, &available);
            gl4_glGetInternalformativ(GL_TEXTURE_2D, internalFormat,
                                      GL_FRAMEBUFFER_RENDERABLE, 1, &renderable);
            if (glGetError() != GL_NO_ERROR)
                return false;
            supported = available == GL_TRUE &&
                        (renderable == GL_FULL_SUPPORT || renderable == GL_CAVEAT_SUPPORT);
            return true;
        }
    }

    ModernCapabilities ClassifyModernCapabilities(const ModernCapabilityInputs& inputs)
    {
        ModernCapabilities result;
        result.contextMajor = inputs.contextMajor;
        result.contextMinor = inputs.contextMinor;

        const bool core43 = VersionAtLeast(inputs.contextMajor, inputs.contextMinor, 4, 3);
        result.computeShadersNative =
            (core43 || inputs.computeShaderExtension) && inputs.computeEntryPoints;
        result.shaderStorageBuffersNative =
            (core43 || inputs.shaderStorageBufferExtension) &&
            inputs.shaderStorageBufferEntryPoints;
        result.imageLoadStoreNative =
            (core43 || inputs.imageLoadStoreExtension) && inputs.imageLoadStoreEntryPoints;
        result.textureArraysNative =
            (VersionAtLeast(inputs.contextMajor, inputs.contextMinor, 3, 0) ||
             inputs.textureArrayExtension) && inputs.textureArrayEntryPoints;
        result.indirectDrawingNative =
            (VersionAtLeast(inputs.contextMajor, inputs.contextMinor, 4, 0) ||
             inputs.indirectDrawingExtension) && inputs.indirectDrawingEntryPoints;
        result.gpuTimersNative =
            (VersionAtLeast(inputs.contextMajor, inputs.contextMinor, 3, 3) ||
             inputs.timerQueryExtension) && inputs.timerQueryEntryPoints;
        result.baseInstanceDrawingNative =
            (VersionAtLeast(inputs.contextMajor, inputs.contextMinor, 4, 2) ||
             inputs.baseInstanceExtension) && inputs.baseInstanceEntryPoints;
        result.internalFormatQueriesNative =
            (core43 || inputs.internalFormatQuery2Extension) &&
            inputs.internalFormatQueryEntryPoints;
        return result;
    }

    ModernCapabilities DiscoverModernCapabilities(const GetProcAddressFn getProcAddress)
    {
        ResolveOptional(getProcAddress, "glGetStringi", gl4_glGetStringi);
        ResolveOptional(getProcAddress, "glDispatchCompute", gl4_glDispatchCompute);
        ResolveOptional(getProcAddress, "glBindBufferBase", gl4_glBindBufferBase);
        ResolveOptional(getProcAddress, "glGetIntegeri_v", gl4_glGetIntegeri_v);
        ResolveOptional(getProcAddress, "glBindImageTexture", gl4_glBindImageTexture);
        ResolveOptional(getProcAddress, "glMemoryBarrier", gl4_glMemoryBarrier);
        ResolveOptional(getProcAddress, "glDrawArraysIndirect", gl4_glDrawArraysIndirect);
        ResolveOptional(getProcAddress, "glDrawElementsIndirect", gl4_glDrawElementsIndirect);
        ResolveOptional(getProcAddress, "glQueryCounter", gl4_glQueryCounter);
        ResolveOptional(getProcAddress, "glGetQueryObjectui64v", gl4_glGetQueryObjectui64v);
        ResolveOptional(getProcAddress, "glDrawElementsInstancedBaseVertexBaseInstance",
                        gl4_glDrawElementsInstancedBaseVertexBaseInstance);
        ResolveOptional(getProcAddress, "glGetInternalformativ", gl4_glGetInternalformativ);

        ModernCapabilityInputs inputs;
        glGetIntegerv(GL_MAJOR_VERSION, &inputs.contextMajor);
        glGetIntegerv(GL_MINOR_VERSION, &inputs.contextMinor);
        if (inputs.contextMajor <= 0)
        {
            const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            if (version != nullptr)
                std::sscanf(version, "%d.%d", &inputs.contextMajor, &inputs.contextMinor);
        }

        const auto hasExtension = [](const char* requested) {
            if (gl4_glGetStringi == nullptr)
                return false;
            GLint count = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &count);
            for (GLint index = 0; index < count; ++index)
            {
                const auto* extension = reinterpret_cast<const char*>(
                    gl4_glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index)));
                if (extension != nullptr && std::strcmp(extension, requested) == 0)
                    return true;
            }
            return false;
        };

        inputs.computeShaderExtension = hasExtension("GL_ARB_compute_shader");
        inputs.shaderStorageBufferExtension =
            hasExtension("GL_ARB_shader_storage_buffer_object");
        inputs.imageLoadStoreExtension = hasExtension("GL_ARB_shader_image_load_store");
        inputs.textureArrayExtension = hasExtension("GL_EXT_texture_array");
        inputs.indirectDrawingExtension = hasExtension("GL_ARB_draw_indirect");
        inputs.timerQueryExtension = hasExtension("GL_ARB_timer_query");
        inputs.baseInstanceExtension = hasExtension("GL_ARB_base_instance");
        inputs.internalFormatQuery2Extension = hasExtension("GL_ARB_internalformat_query2");

        inputs.computeEntryPoints = gl4_glDispatchCompute != nullptr;
        inputs.shaderStorageBufferEntryPoints =
            gl4_glBindBufferBase != nullptr && gl4_glGetIntegeri_v != nullptr;
        inputs.imageLoadStoreEntryPoints =
            gl4_glBindImageTexture != nullptr && gl4_glMemoryBarrier != nullptr;
        inputs.textureArrayEntryPoints =
            gl4_glTexImage3D != nullptr && gl4_glTexSubImage3D != nullptr &&
            gl4_glFramebufferTextureLayer != nullptr;
        inputs.indirectDrawingEntryPoints =
            gl4_glDrawArraysIndirect != nullptr && gl4_glDrawElementsIndirect != nullptr;
        inputs.timerQueryEntryPoints =
            gl4_glQueryCounter != nullptr && gl4_glGetQueryObjectui64v != nullptr;
        inputs.baseInstanceEntryPoints =
            gl4_glDrawElementsInstancedBaseVertexBaseInstance != nullptr;
        inputs.internalFormatQueryEntryPoints = gl4_glGetInternalformativ != nullptr;

        ModernCapabilities result = ClassifyModernCapabilities(inputs);
        if (result.internalFormatQueriesNative)
        {
            result.rgba16FloatRenderableKnown =
                QueryFloatRenderability(GL_RGBA16F, result.rgba16FloatRenderable);
            result.rgba32FloatRenderableKnown =
                QueryFloatRenderability(GL_RGBA32F, result.rgba32FloatRenderable);
        }
        return result;
    }

    bool LoadGL4Functions(GetProcAddressFn getProcAddress)
    {
        bool ok = true;
        ok &= Resolve(getProcAddress, "glGenBuffers", gl4_glGenBuffers);
        ok &= Resolve(getProcAddress, "glBindBuffer", gl4_glBindBuffer);
        ok &= Resolve(getProcAddress, "glBufferData", gl4_glBufferData);
        ok &= Resolve(getProcAddress, "glBufferSubData", gl4_glBufferSubData);
        ok &= Resolve(getProcAddress, "glDeleteBuffers", gl4_glDeleteBuffers);

        ok &= Resolve(getProcAddress, "glGenVertexArrays", gl4_glGenVertexArrays);
        ok &= Resolve(getProcAddress, "glBindVertexArray", gl4_glBindVertexArray);
        ok &= Resolve(getProcAddress, "glDeleteVertexArrays", gl4_glDeleteVertexArrays);
        ok &= Resolve(getProcAddress, "glVertexAttribPointer", gl4_glVertexAttribPointer);
        ok &= Resolve(getProcAddress, "glEnableVertexAttribArray", gl4_glEnableVertexAttribArray);
        ok &= Resolve(getProcAddress, "glDisableVertexAttribArray", gl4_glDisableVertexAttribArray);
        ok &= Resolve(getProcAddress, "glVertexAttribIPointer", gl4_glVertexAttribIPointer);

        ok &= Resolve(getProcAddress, "glCreateShader", gl4_glCreateShader);
        ok &= Resolve(getProcAddress, "glShaderSource", gl4_glShaderSource);
        ok &= Resolve(getProcAddress, "glCompileShader", gl4_glCompileShader);
        ok &= Resolve(getProcAddress, "glGetShaderiv", gl4_glGetShaderiv);
        ok &= Resolve(getProcAddress, "glGetShaderInfoLog", gl4_glGetShaderInfoLog);
        ok &= Resolve(getProcAddress, "glDeleteShader", gl4_glDeleteShader);
        ok &= Resolve(getProcAddress, "glCreateProgram", gl4_glCreateProgram);
        ok &= Resolve(getProcAddress, "glAttachShader", gl4_glAttachShader);
        ok &= Resolve(getProcAddress, "glLinkProgram", gl4_glLinkProgram);
        ok &= Resolve(getProcAddress, "glGetProgramiv", gl4_glGetProgramiv);
        ok &= Resolve(getProcAddress, "glGetProgramInfoLog", gl4_glGetProgramInfoLog);
        ok &= Resolve(getProcAddress, "glDeleteProgram", gl4_glDeleteProgram);
        ok &= Resolve(getProcAddress, "glUseProgram", gl4_glUseProgram);
        ok &= Resolve(getProcAddress, "glBindAttribLocation", gl4_glBindAttribLocation);

        ok &= Resolve(getProcAddress, "glGetUniformLocation", gl4_glGetUniformLocation);
        ok &= Resolve(getProcAddress, "glUniform1i", gl4_glUniform1i);
        ok &= Resolve(getProcAddress, "glUniform1f", gl4_glUniform1f);
        ok &= Resolve(getProcAddress, "glUniform2f", gl4_glUniform2f);
        ok &= Resolve(getProcAddress, "glUniform3f", gl4_glUniform3f);
        ok &= Resolve(getProcAddress, "glUniform4f", gl4_glUniform4f);
        ok &= Resolve(getProcAddress, "glUniform1fv", gl4_glUniform1fv);
        ok &= Resolve(getProcAddress, "glUniform2fv", gl4_glUniform2fv);
        ok &= Resolve(getProcAddress, "glUniformMatrix4fv", gl4_glUniformMatrix4fv);

        ok &= Resolve(getProcAddress, "glActiveTexture", gl4_glActiveTexture);
        ok &= Resolve(getProcAddress, "glGenerateMipmap", gl4_glGenerateMipmap);

        ok &= Resolve(getProcAddress, "glBlendFuncSeparate", gl4_glBlendFuncSeparate);
        ok &= Resolve(getProcAddress, "glBlendEquationSeparate", gl4_glBlendEquationSeparate);
        ok &= Resolve(getProcAddress, "glBlendColor", gl4_glBlendColor);

        ok &= Resolve(getProcAddress, "glGenSamplers", gl4_glGenSamplers);
        ok &= Resolve(getProcAddress, "glDeleteSamplers", gl4_glDeleteSamplers);
        ok &= Resolve(getProcAddress, "glBindSampler", gl4_glBindSampler);
        ok &= Resolve(getProcAddress, "glSamplerParameteri", gl4_glSamplerParameteri);
        ok &= Resolve(getProcAddress, "glSamplerParameterf", gl4_glSamplerParameterf);

        ok &= Resolve(getProcAddress, "glGenFramebuffers", gl4_glGenFramebuffers);
        ok &= Resolve(getProcAddress, "glBindFramebuffer", gl4_glBindFramebuffer);
        ok &= Resolve(getProcAddress, "glDeleteFramebuffers", gl4_glDeleteFramebuffers);
        ok &= Resolve(getProcAddress, "glFramebufferTexture2D", gl4_glFramebufferTexture2D);
        ok &= Resolve(getProcAddress, "glCheckFramebufferStatus", gl4_glCheckFramebufferStatus);
        ok &= Resolve(getProcAddress, "glGenRenderbuffers", gl4_glGenRenderbuffers);
        ok &= Resolve(getProcAddress, "glBindRenderbuffer", gl4_glBindRenderbuffer);
        ok &= Resolve(getProcAddress, "glDeleteRenderbuffers", gl4_glDeleteRenderbuffers);
        ok &= Resolve(getProcAddress, "glRenderbufferStorage", gl4_glRenderbufferStorage);
        ok &= Resolve(getProcAddress, "glRenderbufferStorageMultisample", gl4_glRenderbufferStorageMultisample);
        ok &= Resolve(getProcAddress, "glFramebufferRenderbuffer", gl4_glFramebufferRenderbuffer);
        ok &= Resolve(getProcAddress, "glBlitFramebuffer", gl4_glBlitFramebuffer);
        ok &= Resolve(getProcAddress, "glDrawBuffers", gl4_glDrawBuffers);

        ok &= Resolve(getProcAddress, "glStencilFuncSeparate", gl4_glStencilFuncSeparate);
        ok &= Resolve(getProcAddress, "glStencilOpSeparate", gl4_glStencilOpSeparate);
        ok &= Resolve(getProcAddress, "glStencilMaskSeparate", gl4_glStencilMaskSeparate);
        ok &= Resolve(getProcAddress, "glColorMaski", gl4_glColorMaski);

        ok &= Resolve(getProcAddress, "glTexImage3D", gl4_glTexImage3D);
        ok &= Resolve(getProcAddress, "glTexSubImage3D", gl4_glTexSubImage3D);
        ok &= Resolve(getProcAddress, "glFramebufferTextureLayer", gl4_glFramebufferTextureLayer);

        ok &= Resolve(getProcAddress, "glGenQueries", gl4_glGenQueries);
        ok &= Resolve(getProcAddress, "glDeleteQueries", gl4_glDeleteQueries);
        ok &= Resolve(getProcAddress, "glBeginQuery", gl4_glBeginQuery);
        ok &= Resolve(getProcAddress, "glEndQuery", gl4_glEndQuery);
        ok &= Resolve(getProcAddress, "glGetQueryObjectuiv", gl4_glGetQueryObjectuiv);

        ok &= Resolve(getProcAddress, "glDrawElementsBaseVertex", gl4_glDrawElementsBaseVertex);

        ok &= Resolve(getProcAddress, "glDrawElementsInstanced", gl4_glDrawElementsInstanced);
        ok &= Resolve(getProcAddress, "glVertexAttribDivisor", gl4_glVertexAttribDivisor);

        return ok;
    }
}
