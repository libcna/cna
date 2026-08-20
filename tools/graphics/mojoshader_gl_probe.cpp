// SPDX-License-Identifier: MS-PL
//
// plans/plan_fx.md FX-062 existence gate: prove that the pinned MojoShader's OpenGL adapter
// (mojoshader_opengl.c) links and renders a committed compiled effect's shader pair against a
// real GLES3 context this machine can create -- before any EasyGL renderer code is written for
// it.
//
// EasyGL is the shared implementation behind OPENGLES2/OPENGLES3/OPENGL33/OPENGL4/WEBGL1/WEBGL2
// (plans/plan_fx.md section 10.3). Its own stock shaders are authored once in GLSL ES 3.00 and string-
// rewritten to whichever dialect the active profile needs at runtime -- but that pipeline is
// irrelevant here: MojoShader emits already-correct-dialect GLSL for whatever profile
// MOJOSHADER_glCreateContext is asked for, entirely in parallel to EasyGL's own shaders. This
// probe targets GLSLES3 (OPENGLES3's native dialect, and EasyGL's own default/fallback profile
// per EasyGLRenderer.cpp's RequestedGlContext()).
//
// This probe deliberately links only MojoShader and SDL3 -- no CNA, no EasyGL. If it needed
// EasyGL to work, FX-062 would not be independent of it and the whole plan would change.
//
// Usage:
//   cna_mojoshader_gl_probe <file.fxb>...             -- reflection/bind exercise, no rendering
//   cna_mojoshader_gl_probe --render <file.fxb>        -- links, draws a full-screen quad into an
//                                                          offscreen FBO, reads pixels back
//
// The --render golden values match the ones independently hand-verified for the SDL_GPU adapter
// (tools/graphics/mojoshader_sdlgpu_probe.cpp, plans/plan_fx.md FX-071's golden-pixel investigation):
// technique 0 pass 0 (MainVertexShader/MainPixelShader, a 1x1 white FxTexture) lands on
// (3,6,10,13); technique 0 pass 1 / technique 1 pass 0 (FlatPixelShader, no sampling) on
// (20,41,61,82) -- both confirmed byte-identical to the SDL_GPU adapter's own golden pixels. Both
// exercise the shared, renderer-neutral MojoShader Effect Framework preshader interpreter
// (mojoshader_effects.c) that FX-071 found and fixed two real bugs in, so this cross-backend match
// is also a second, independent confirmation that fix is correct.
//
// Getting there found two more real, GL-adapter-specific bugs this probe exists to catch before
// EasyGL renderer code ever depends on them:
//
// 1. Every MOJOSHADER_gl* function relevant to MOJOSHADER_effectShaderContext omits the leading
//    context-pointer argument the typedefs declare (MojoShader's OpenGL adapter keeps its context
//    as implicit thread-local state, not an explicit per-call parameter -- unlike SDL_GPU/D3D11),
//    and MOJOSHADER_glCompileShader additionally has no `mainfn` parameter at all. A direct
//    function-pointer cast into those slots (this file's first attempt, mirroring the SDL_GPU
//    probe's MakeSdlBackend) silently misaligns every argument -- it reproduced as
//    MOJOSHADER_parse() failing with "invalid swizzle" on every committed effect, immediately,
//    regardless of which GLSL/GLSLES/GLSLES3 profile was requested, which is what proved it was a
//    calling-convention bug here rather than a profile-specific codegen defect. See the trampoline
//    functions below MakeGlBackend.
//
// 2. The generated GLSL vertex shader references a `uniform float vpFlip` for the GL/D3D9
//    coordinate-system mismatch (and a paired depth-range remap on gl_Position.z) that
//    MOJOSHADER_effectCommitChanges' ordinary register-file uniform push never populates --
//    MOJOSHADER_glProgramViewportInfo() has to be called separately, after
//    MOJOSHADER_glProgramReady(), before every draw whose render target could have changed.
//    Skipping it leaves vpFlip at GLSL's zero-initialized default, which zeroes gl_Position.y for
//    every vertex and collapses any geometry to zero screen area -- reproduced as a draw that
//    reported no GL error at any step (valid program bound, attributes correctly located and
//    enabled) yet changed zero pixels.

#include "mojoshader.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Minimal hand-declared GL/GLES function pointer types and constants -- deliberately not pulling
// in a vendored GL header, matching mojoshader_sdlgpu_probe.cpp's "no extra dependency" spirit.
// These signatures and enum values are part of the stable GL/GLES ABI.
namespace
{
    using GLenum = unsigned int;
    using GLboolean = unsigned char;
    using GLuint = unsigned int;
    using GLint = int;
    using GLsizei = int;
    using GLfloat = float;
    using GLbitfield = unsigned int;

    constexpr GLenum kGlColorBufferBit = 0x00004000;
    constexpr GLenum kGlTexture2D = 0x0DE1;
    constexpr GLenum kGlRgba = 0x1908;
    constexpr GLenum kGlUnsignedByte = 0x1401;
    constexpr GLenum kGlTextureMinFilter = 0x2801;
    constexpr GLenum kGlTextureMagFilter = 0x2800;
    constexpr GLenum kGlNearest = 0x2600;
    constexpr GLenum kGlTexture0 = 0x84C0;
    constexpr GLenum kGlFramebuffer = 0x8D40;
    constexpr GLenum kGlColorAttachment0 = 0x8CE0;
    constexpr GLenum kGlFramebufferComplete = 0x8CD5;
    constexpr GLenum kGlTriangles = 0x0004;
    constexpr GLenum kGlArrayBuffer = 0x8892;
    constexpr GLenum kGlStaticDraw = 0x88E4;

    using PFNGLCLEARCOLOR = void (*)(GLfloat, GLfloat, GLfloat, GLfloat);
    using PFNGLCLEAR = void (*)(GLbitfield);
    using PFNGLVIEWPORT = void (*)(GLint, GLint, GLsizei, GLsizei);
    using PFNGLGENTEXTURES = void (*)(GLsizei, GLuint*);
    using PFNGLBINDTEXTURE = void (*)(GLenum, GLuint);
    using PFNGLTEXIMAGE2D = void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                     const void*);
    using PFNGLTEXPARAMETERI = void (*)(GLenum, GLenum, GLint);
    using PFNGLACTIVETEXTURE = void (*)(GLenum);
    using PFNGLGENFRAMEBUFFERS = void (*)(GLsizei, GLuint*);
    using PFNGLBINDFRAMEBUFFER = void (*)(GLenum, GLuint);
    using PFNGLFRAMEBUFFERTEXTURE2D = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
    using PFNGLCHECKFRAMEBUFFERSTATUS = GLenum (*)(GLenum);
    using PFNGLGENVERTEXARRAYS = void (*)(GLsizei, GLuint*);
    using PFNGLBINDVERTEXARRAY = void (*)(GLuint);
    using PFNGLGENBUFFERS = void (*)(GLsizei, GLuint*);
    using PFNGLBINDBUFFER = void (*)(GLenum, GLuint);
    using PFNGLBUFFERDATA = void (*)(GLenum, long, const void*, GLenum);
    using PFNGLDELETEBUFFERS = void (*)(GLsizei, const GLuint*);
    using PFNGLDRAWARRAYS = void (*)(GLenum, GLint, GLsizei);
    using PFNGLREADPIXELS = void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    using PFNGLFINISH = void (*)();
    using PFNGLGETERROR = GLenum (*)();
    using PFNGLDELETEFRAMEBUFFERS = void (*)(GLsizei, const GLuint*);
    using PFNGLDELETETEXTURES = void (*)(GLsizei, const GLuint*);
    using PFNGLDELETEVERTEXARRAYS = void (*)(GLsizei, const GLuint*);

    struct GlFunctions
    {
        PFNGLCLEARCOLOR ClearColor = nullptr;
        PFNGLCLEAR Clear = nullptr;
        PFNGLVIEWPORT Viewport = nullptr;
        PFNGLGENTEXTURES GenTextures = nullptr;
        PFNGLBINDTEXTURE BindTexture = nullptr;
        PFNGLTEXIMAGE2D TexImage2D = nullptr;
        PFNGLTEXPARAMETERI TexParameteri = nullptr;
        PFNGLACTIVETEXTURE ActiveTexture = nullptr;
        PFNGLGENFRAMEBUFFERS GenFramebuffers = nullptr;
        PFNGLBINDFRAMEBUFFER BindFramebuffer = nullptr;
        PFNGLFRAMEBUFFERTEXTURE2D FramebufferTexture2D = nullptr;
        PFNGLCHECKFRAMEBUFFERSTATUS CheckFramebufferStatus = nullptr;
        PFNGLGENVERTEXARRAYS GenVertexArrays = nullptr;
        PFNGLBINDVERTEXARRAY BindVertexArray = nullptr;
        PFNGLGENBUFFERS GenBuffers = nullptr;
        PFNGLBINDBUFFER BindBuffer = nullptr;
        PFNGLBUFFERDATA BufferData = nullptr;
        PFNGLDELETEBUFFERS DeleteBuffers = nullptr;
        PFNGLDRAWARRAYS DrawArrays = nullptr;
        PFNGLREADPIXELS ReadPixels = nullptr;
        PFNGLFINISH Finish = nullptr;
        PFNGLGETERROR GetError = nullptr;
        PFNGLDELETEFRAMEBUFFERS DeleteFramebuffers = nullptr;
        PFNGLDELETETEXTURES DeleteTextures = nullptr;
        PFNGLDELETEVERTEXARRAYS DeleteVertexArrays = nullptr;

        bool LoadAll()
        {
            bool ok = true;
            auto load = [&](auto& slot, const char* name)
            {
                using SlotType = std::decay_t<decltype(slot)>;
                slot = reinterpret_cast<SlotType>(SDL_GL_GetProcAddress(name));
                if (slot == nullptr)
                {
                    std::fprintf(stderr, "missing GL function: %s\n", name);
                    ok = false;
                }
            };
            load(ClearColor, "glClearColor");
            load(Clear, "glClear");
            load(Viewport, "glViewport");
            load(GenTextures, "glGenTextures");
            load(BindTexture, "glBindTexture");
            load(TexImage2D, "glTexImage2D");
            load(TexParameteri, "glTexParameteri");
            load(ActiveTexture, "glActiveTexture");
            load(GenFramebuffers, "glGenFramebuffers");
            load(BindFramebuffer, "glBindFramebuffer");
            load(FramebufferTexture2D, "glFramebufferTexture2D");
            load(CheckFramebufferStatus, "glCheckFramebufferStatus");
            load(GenVertexArrays, "glGenVertexArrays");
            load(BindVertexArray, "glBindVertexArray");
            load(GenBuffers, "glGenBuffers");
            load(BindBuffer, "glBindBuffer");
            load(BufferData, "glBufferData");
            load(DeleteBuffers, "glDeleteBuffers");
            load(DrawArrays, "glDrawArrays");
            load(ReadPixels, "glReadPixels");
            load(Finish, "glFinish");
            load(GetError, "glGetError");
            load(DeleteFramebuffers, "glDeleteFramebuffers");
            load(DeleteTextures, "glDeleteTextures");
            load(DeleteVertexArrays, "glDeleteVertexArrays");
            return ok;
        }
    };

    std::vector<unsigned char> ReadFile(const char* path)
    {
        std::FILE* file = std::fopen(path, "rb");
        if (file == nullptr) return {};
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        std::vector<unsigned char> bytes(size > 0 ? static_cast<std::size_t>(size) : 0u);
        if (!bytes.empty() && std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size())
        {
            bytes.clear();
        }
        std::fclose(file);
        return bytes;
    }

    void* GlProcAddressTrampoline(const char* fnname, void* data)
    {
        (void) data;
        return reinterpret_cast<void*>(SDL_GL_GetProcAddress(fnname));
    }

    // plans/plan_fx.md FX-062 finding: unlike the SDL_GPU/D3D11 adapters, every MOJOSHADER_gl* Effects-
    // relevant function omits the leading context-pointer argument the MOJOSHADER_effectShaderContext
    // typedefs declare -- MojoShader's OpenGL adapter keeps its context as implicit thread-local
    // state (set via MOJOSHADER_glMakeContextCurrent), not an explicit per-call parameter, and
    // MOJOSHADER_glCompileShader additionally has no `mainfn` parameter at all. A direct cast of the
    // real 6/1/2/2/6/0/0-argument GL functions into these 8/1/2/3/7/1/1-argument slots silently
    // misaligns every argument (each real function reads its parameters starting one or two
    // positions early) -- this reproduced as MOJOSHADER_parse() failing with "invalid swizzle" on
    // every committed effect, immediately, regardless of which GLSL/GLSLES/GLSLES3 profile was
    // requested, which is what proved it was a calling-convention bug in this probe rather than a
    // profile-specific codegen defect. These trampolines drop the unused leading argument(s) instead.
    void* CompileShaderTrampoline(const void* ctx, const char* mainfn,
                                  const unsigned char* tokenbuf, unsigned int bufsize,
                                  const MOJOSHADER_swizzle* swiz, unsigned int swizcount,
                                  const MOJOSHADER_samplerMap* smap, unsigned int smapcount)
    {
        (void) ctx;
        (void) mainfn;
        return MOJOSHADER_glCompileShader(tokenbuf, bufsize, swiz, swizcount, smap, smapcount);
    }

    void DeleteShaderTrampoline(const void* ctx, void* shader)
    {
        (void) ctx;
        MOJOSHADER_glDeleteShader(static_cast<MOJOSHADER_glShader*>(shader));
    }

    void BindShadersTrampoline(const void* ctx, void* vshader, void* pshader)
    {
        (void) ctx;
        MOJOSHADER_glBindShaders(static_cast<MOJOSHADER_glShader*>(vshader),
                                 static_cast<MOJOSHADER_glShader*>(pshader));
    }

    void GetBoundShadersTrampoline(const void* ctx, void** vshader, void** pshader)
    {
        (void) ctx;
        MOJOSHADER_glGetBoundShaders(reinterpret_cast<MOJOSHADER_glShader**>(vshader),
                                     reinterpret_cast<MOJOSHADER_glShader**>(pshader));
    }

    void MapUniformBufferMemoryTrampoline(const void* ctx, float** vsf, int** vsi,
                                          unsigned char** vsb, float** psf, int** psi,
                                          unsigned char** psb)
    {
        (void) ctx;
        MOJOSHADER_glMapUniformBufferMemory(vsf, vsi, vsb, psf, psi, psb);
    }

    void UnmapUniformBufferMemoryTrampoline(const void* ctx)
    {
        (void) ctx;
        MOJOSHADER_glUnmapUniformBufferMemory();
    }

    const char* GetErrorTrampoline(const void* ctx)
    {
        (void) ctx;
        return MOJOSHADER_glGetError();
    }

    /// Builds the backend MojoShader requires, wired entirely to its own OpenGL adapter.
    MOJOSHADER_effectShaderContext MakeGlBackend(MOJOSHADER_glContext* context)
    {
        MOJOSHADER_effectShaderContext backend{};
        backend.shaderContext = context;
        backend.compileShader = CompileShaderTrampoline;
        // shaderAddRef and getParseData are the only two slots whose real GL signature already
        // matches the typedef exactly (neither takes a context argument in either form).
        backend.shaderAddRef = (MOJOSHADER_shaderAddRefFunc) MOJOSHADER_glShaderAddRef;
        backend.deleteShader = DeleteShaderTrampoline;
        backend.getParseData = (MOJOSHADER_getParseDataFunc) MOJOSHADER_glGetShaderParseData;
        backend.bindShaders = BindShadersTrampoline;
        backend.getBoundShaders = GetBoundShadersTrampoline;
        backend.mapUniformBufferMemory = MapUniformBufferMemoryTrampoline;
        backend.unmapUniformBufferMemory = UnmapUniformBufferMemoryTrampoline;
        backend.getError = GetErrorTrampoline;
        return backend;
    }

    /// Walks one effect's passes, binding each pass's shader pair the way a renderer would.
    int ExercisePasses(const char* path, MOJOSHADER_glContext* context,
                       const MOJOSHADER_effect* effect)
    {
        int boundPasses = 0;
        for (int t = 0; t < effect->technique_count; ++t)
        {
            const MOJOSHADER_effectTechnique& technique = effect->techniques[t];
            for (unsigned int p = 0; p < technique.pass_count; ++p)
            {
                MOJOSHADER_effectStateChanges changes{};
                MOJOSHADER_effect* mutableEffect = const_cast<MOJOSHADER_effect*>(effect);
                MOJOSHADER_effectSetTechnique(mutableEffect, &technique);
                unsigned int passCount = 0;
                MOJOSHADER_effectBegin(mutableEffect, &passCount, /*saveShaderState=*/0, &changes);
                MOJOSHADER_effectBeginPass(mutableEffect, p);

                MOJOSHADER_glShader* vertex = nullptr;
                MOJOSHADER_glShader* pixel = nullptr;
                MOJOSHADER_glGetBoundShaders(&vertex, &pixel);
                if (vertex == nullptr || pixel == nullptr)
                {
                    std::printf("%s: technique %d pass %u bound no shader pair\n", path, t, p);
                    MOJOSHADER_effectEndPass(mutableEffect);
                    MOJOSHADER_effectEnd(mutableEffect);
                    return 1;
                }

                const MOJOSHADER_parseData* vertexData = MOJOSHADER_glGetShaderParseData(vertex);
                const MOJOSHADER_parseData* pixelData = MOJOSHADER_glGetShaderParseData(pixel);
                std::printf("%s: technique %d pass %u -- vertex %s (%d uniforms, %d samplers), "
                            "pixel %s (%d uniforms, %d samplers)\n",
                            path, t, p,
                            vertexData != nullptr ? vertexData->profile : "<none>",
                            vertexData != nullptr ? vertexData->uniform_count : -1,
                            vertexData != nullptr ? vertexData->sampler_count : -1,
                            pixelData != nullptr ? pixelData->profile : "<none>",
                            pixelData != nullptr ? pixelData->uniform_count : -1,
                            pixelData != nullptr ? pixelData->sampler_count : -1);

                ++boundPasses;
                MOJOSHADER_effectEndPass(mutableEffect);
                MOJOSHADER_effectEnd(mutableEffect);
            }
        }
        (void) context;

        if (boundPasses == 0)
        {
            std::printf("%s: no pass bound anything\n", path);
            return 1;
        }
        return 0;
    }

    /// plans/plan_fx.md FX-062 golden-pixel check: links the selected technique/pass's shaders
    /// (PROBE_TECHNIQUE/PROBE_PASS env vars, default 0/0), draws a full-screen quad shaped for
    /// CnaConformanceEffect.fx's MainVertexShader (float3 POSITION0, float2 TEXCOORD0) into an
    /// offscreen RGBA8 FBO, and reports whether the centre pixel matches the value independently
    /// hand-verified for the SDL_GPU adapter. No CNA/EasyGL code anywhere in this path.
    int RenderAndReadback(const char* path, const GlFunctions& gl, MOJOSHADER_effect* effect)
    {
        int techniqueIndex = 0;
        int passIndex = 0;
        if (const char* t = SDL_getenv("PROBE_TECHNIQUE")) techniqueIndex = std::atoi(t);
        if (const char* p = SDL_getenv("PROBE_PASS")) passIndex = std::atoi(p);

        if (effect->technique_count <= techniqueIndex ||
            effect->techniques[techniqueIndex].pass_count <= static_cast<unsigned int>(passIndex))
        {
            std::printf("%s: no technique %d pass %d to render\n", path, techniqueIndex, passIndex);
            return 1;
        }

        MOJOSHADER_effectStateChanges changes{};
        MOJOSHADER_effectSetTechnique(effect, &effect->techniques[techniqueIndex]);
        unsigned int passCount = 0;
        MOJOSHADER_effectBegin(effect, &passCount, /*saveShaderState=*/0, &changes);
        MOJOSHADER_effectBeginPass(effect, passIndex);
        // Unlike the SDL_GPU adapter, MOJOSHADER_glBindShaders (called internally by
        // effectBeginPass/CommitChanges) links AND binds (glUseProgram) the program in one step --
        // there is no separate explicit link call to make here.

        MOJOSHADER_glShader* vertexShader = nullptr;
        MOJOSHADER_glShader* pixelShader = nullptr;
        MOJOSHADER_glGetBoundShaders(&vertexShader, &pixelShader);
        if (vertexShader == nullptr || pixelShader == nullptr)
        {
            std::printf("%s: --render: technique %d pass %d bound no shader pair\n", path,
                        techniqueIndex, passIndex);
            return 1;
        }

        const MOJOSHADER_parseData* pixelParseData = MOJOSHADER_glGetShaderParseData(pixelShader);
        std::printf("%s: --render: pixel uniform_count=%d sampler_count=%d\n", path,
                    pixelParseData->uniform_count, pixelParseData->sampler_count);

        // Full-screen quad in clip space: Transform is the identity, so MainVertexShader's
        // mul(Position, Transform) passes Position straight through -- these need to already be
        // NDC. Same geometry as mojoshader_sdlgpu_probe.cpp's --render mode.
        struct Vertex { float x, y, z, u, v; };
        const Vertex vertices[6] = {
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
            {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
            { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
            { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
        };

        // GLES3 core requires a bound VAO for any draw call, and (unlike desktop GL/GLES2) forbids
        // client-side vertex arrays entirely: glVertexAttribPointer raises GL_INVALID_OPERATION if
        // GL_ARRAY_BUFFER is unbound and the pointer argument is not NULL. Real vertex data, not a
        // raw CPU pointer offset into `vertices`.
        GLuint vao = 0;
        gl.GenVertexArrays(1, &vao);
        gl.BindVertexArray(vao);

        GLuint vbo = 0;
        gl.GenBuffers(1, &vbo);
        gl.BindBuffer(kGlArrayBuffer, vbo);
        gl.BufferData(kGlArrayBuffer, static_cast<long>(sizeof(vertices)), vertices,
                     kGlStaticDraw);

        // 1x1 white texture for FxSampler.
        GLuint whiteTexture = 0;
        gl.GenTextures(1, &whiteTexture);
        gl.ActiveTexture(kGlTexture0);
        gl.BindTexture(kGlTexture2D, whiteTexture);
        const unsigned char white[4] = {255, 255, 255, 255};
        gl.TexImage2D(kGlTexture2D, 0, static_cast<GLint>(kGlRgba), 1, 1, 0, kGlRgba,
                     kGlUnsignedByte, white);
        gl.TexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<GLint>(kGlNearest));
        gl.TexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<GLint>(kGlNearest));

        // Offscreen colour render target.
        constexpr GLsizei kWidth = 64, kHeight = 64;
        GLuint colorTarget = 0;
        gl.GenTextures(1, &colorTarget);
        gl.BindTexture(kGlTexture2D, colorTarget);
        gl.TexImage2D(kGlTexture2D, 0, static_cast<GLint>(kGlRgba), kWidth, kHeight, 0, kGlRgba,
                     kGlUnsignedByte, nullptr);
        gl.TexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<GLint>(kGlNearest));
        gl.TexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<GLint>(kGlNearest));

        GLuint fbo = 0;
        gl.GenFramebuffers(1, &fbo);
        gl.BindFramebuffer(kGlFramebuffer, fbo);
        gl.FramebufferTexture2D(kGlFramebuffer, kGlColorAttachment0, kGlTexture2D, colorTarget, 0);
        if (gl.CheckFramebufferStatus(kGlFramebuffer) != kGlFramebufferComplete)
        {
            std::printf("%s: --render: framebuffer incomplete\n", path);
            return 1;
        }
        // Rebinding colorTarget above (for the FBO attachment) clobbered texture unit 0's
        // GL_TEXTURE_2D binding, left pointing at colorTarget instead of whiteTexture -- and
        // sampling the same texture currently bound as the framebuffer's colour attachment is a
        // feedback-loop hazard with undefined results besides. Put whiteTexture back on unit 0.
        gl.ActiveTexture(kGlTexture0);
        gl.BindTexture(kGlTexture2D, whiteTexture);

        gl.Viewport(0, 0, kWidth, kHeight);
        gl.ClearColor(50.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f);
        gl.Clear(kGlColorBufferBit);

        // Vertex attributes: CnaConformanceEffect's VertexIn declares float4 Position : POSITION0,
        // float2 TexCoord : TEXCOORD0. MOJOSHADER_glSetVertexAttribute no-ops for an attribute the
        // bound program's vertex shader does not use, mirroring how the array of reflected
        // attributes drives the SDL_GPU adapter's binding elsewhere in this codebase. GLES3 core
        // forbids client-side vertex arrays (glVertexAttribPointer raises GL_INVALID_OPERATION with
        // no GL_ARRAY_BUFFER bound and a non-NULL pointer argument), so `ptr` here is a byte offset
        // into the bound vbo, not a raw CPU pointer into `vertices`.
        MOJOSHADER_glSetVertexAttribute(
            MOJOSHADER_USAGE_POSITION, 0, 3, MOJOSHADER_ATTRIBUTE_FLOAT, 0, sizeof(Vertex),
            reinterpret_cast<const void*>(offsetof(Vertex, x)));
        MOJOSHADER_glSetVertexAttribute(
            MOJOSHADER_USAGE_TEXCOORD, 0, 2, MOJOSHADER_ATTRIBUTE_FLOAT, 0, sizeof(Vertex),
            reinterpret_cast<const void*>(offsetof(Vertex, u)));

        // Pushes uniforms from the shared register files (already populated by
        // MOJOSHADER_effectBeginPass's internal CommitChanges call) into the bound program, and
        // enables the vertex attribute arrays MOJOSHADER_glSetVertexAttribute flagged above.
        MOJOSHADER_glProgramReady();
        // GL/D3D9 coordinate-system fixups the generated GLSL applies via injected uniforms
        // (vpFlip, and a D3D9 [0,1] -> GL [-1,1] depth remap): unlike the Effect Framework's
        // ordinary register-file uniforms, these are NOT populated by MOJOSHADER_effectCommitChanges
        // at all -- they need this separate call, after ProgramReady, every time the render target
        // changes. Skipping it leaves vpFlip at GLSL's zero-initialized default, which zeroes
        // gl_Position.y for every vertex and collapses any geometry to zero area -- the second real
        // bug this probe found (the first was the calling-convention mismatch above MakeGlBackend).
        MOJOSHADER_glProgramViewportInfo(kWidth, kHeight, kWidth, kHeight, /*renderTargetBound=*/1);

        gl.DrawArrays(kGlTriangles, 0, 6);
        gl.Finish();

        const GLenum err = gl.GetError();
        if (err != 0)
        {
            std::printf("%s: --render: glGetError()=0x%04X after draw\n", path,
                        static_cast<unsigned int>(err));
        }

        std::vector<unsigned char> pixels(static_cast<std::size_t>(kWidth) * kHeight * 4);
        gl.ReadPixels(0, 0, kWidth, kHeight, kGlRgba, kGlUnsignedByte, pixels.data());

        int nonBlack = 0;
        unsigned char sample[4] = {0, 0, 0, 0};
        for (std::size_t i = 0; i < static_cast<std::size_t>(kWidth) * kHeight; ++i)
        {
            const unsigned char* p = &pixels[i * 4];
            if (p[0] != 50 || p[1] != 50 || p[2] != 50)
            {
                ++nonBlack;
                sample[0] = p[0]; sample[1] = p[1]; sample[2] = p[2]; sample[3] = p[3];
            }
        }
        const unsigned char* centerPixel =
            &pixels[(static_cast<std::size_t>(kHeight / 2) * kWidth + kWidth / 2) * 4];
        std::printf("%s: --render: %dx%d target, changedFromClear=%d, sample=(%d,%d,%d,%d), "
                    "center=(%d,%d,%d,%d)\n",
                    path, kWidth, kHeight, nonBlack, sample[0], sample[1], sample[2], sample[3],
                    centerPixel[0], centerPixel[1], centerPixel[2], centerPixel[3]);

        gl.DeleteFramebuffers(1, &fbo);
        gl.DeleteTextures(1, &colorTarget);
        gl.DeleteTextures(1, &whiteTexture);
        gl.DeleteVertexArrays(1, &vao);
        gl.DeleteBuffers(1, &vbo);

        return nonBlack > 0 ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: cna_mojoshader_gl_probe [--render] <file.fxb>...\n");
        return 2;
    }

    bool renderMode = false;
    int firstFile = 1;
    if (std::strcmp(argv[1], "--render") == 0)
    {
        renderMode = true;
        firstFile = 2;
    }
    if (firstFile >= argc)
    {
        std::fprintf(stderr, "usage: cna_mojoshader_gl_probe [--render] <file.fxb>...\n");
        return 2;
    }

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 3;
    }

    // Matches EasyGLRenderer.cpp's RequestedGlContext() default/OPENGLES3 fallback branch.
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow("cna_mojoshader_gl_probe", 64, 64,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 4;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr)
    {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 5;
    }
    SDL_GL_MakeCurrent(window, glContext);

    int actualMajor = 0, actualMinor = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &actualMajor);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &actualMinor);
    std::printf("GL context: %d.%d ES\n", actualMajor, actualMinor);

    GlFunctions gl;
    if (!gl.LoadAll())
    {
        std::fprintf(stderr, "failed to load required GL functions\n");
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 6;
    }

    const char* bestProfile = MOJOSHADER_glBestProfile(GlProcAddressTrampoline, nullptr,
                                                        nullptr, nullptr, nullptr);
    std::printf("MOJOSHADER_glBestProfile: %s\n", bestProfile != nullptr ? bestProfile : "<null>");
    const char* profileOverride = SDL_getenv("PROBE_PROFILE");
    const char* profile = profileOverride != nullptr ? profileOverride : MOJOSHADER_PROFILE_GLSLES3;
    std::printf("requested MojoShader profile: %s\n", profile);
    MOJOSHADER_glContext* context = MOJOSHADER_glCreateContext(
        profile, GlProcAddressTrampoline, nullptr, nullptr, nullptr, nullptr);
    if (context == nullptr)
    {
        std::fprintf(stderr, "MOJOSHADER_glCreateContext failed: %s\n", MOJOSHADER_glGetError());
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 7;
    }
    MOJOSHADER_glMakeContextCurrent(context);

    int failures = 0;
    for (int i = firstFile; i < argc; ++i)
    {
        const std::vector<unsigned char> bytes = ReadFile(argv[i]);
        if (bytes.empty())
        {
            std::fprintf(stderr, "cannot read %s\n", argv[i]);
            ++failures;
            continue;
        }

        MOJOSHADER_effectShaderContext backend = MakeGlBackend(context);
        MOJOSHADER_effect* effect = MOJOSHADER_compileEffect(
            bytes.data(), static_cast<unsigned int>(bytes.size()),
            nullptr, 0, nullptr, 0, &backend);

        if (effect == nullptr || effect->error_count > 0)
        {
            if (effect != nullptr)
            {
                for (int e = 0; e < effect->error_count; ++e)
                {
                    std::printf("%s: error: %s\n", argv[i],
                                effect->errors[e].error != nullptr ? effect->errors[e].error
                                                                   : "<null>");
                }
            }
            else
            {
                std::printf("%s: compileEffect returned nothing\n", argv[i]);
            }
            ++failures;
            MOJOSHADER_deleteEffect(effect);
            continue;
        }

        failures += renderMode ? RenderAndReadback(argv[i], gl, effect)
                               : ExercisePasses(argv[i], context, effect);
        MOJOSHADER_deleteEffect(effect);
    }

    MOJOSHADER_glMakeContextCurrent(nullptr);
    MOJOSHADER_glDestroyContext(context);
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::printf("%s\n", failures == 0
        ? "MojoShader's OpenGL adapter binds committed effects with no EasyGL linked."
        : "one or more effects failed");
    return failures == 0 ? 0 : 1;
}
