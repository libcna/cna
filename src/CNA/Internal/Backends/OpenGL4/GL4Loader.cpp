// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/OpenGL4/GL4Loader.hpp"

#include <cstdio>

namespace CNA::Internal::Backends::OpenGL4::GL4
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

    namespace
    {
        template <typename Fn>
        bool Resolve(GetProcAddressFn getProcAddress, const char* name, Fn& out)
        {
            void* p = getProcAddress(name);
            if (!p)
            {
                std::fprintf(stderr, "CNA: OpenGL4 backend: failed to resolve GL entry point '%s'\n", name);
                return false;
            }
            out = reinterpret_cast<Fn>(p);
            return true;
        }
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
