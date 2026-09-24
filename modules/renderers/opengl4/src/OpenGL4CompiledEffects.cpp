// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0020: the renderer side of OpenGL4's compiled XNA Effect
// support -- the MojoShader context, the ordinary/indexed/instanced/multi-stream draw routes, the
// render-target row-order correction and the SpriteBatch route. Each is EasyGL's desktop-profile
// behaviour (plans/plan_fx.md FX-062, FX-080, FX-082, FX-083, FX-088, FX-099, FX-118, FX-128) over
// raw desktop OpenGL 4.1 core; the runtime itself is OpenGL4CompiledEffect.cpp. Built only when
// CNA_OPENGL4_COMPILED_EFFECTS is on.
//
// What is deliberately not here: EasyGL's context-loss release/recreate pair (FX-108). Desktop
// OpenGL 4 has no context loss, so the MojoShader context, the shared vertex array object and the
// row-order copies live exactly as long as this renderer.
#if defined(CNA_OPENGL4_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/OpenGL4/OpenGL4CompiledEffect.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"

#include "Fna3dStockEffectBlobs.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace CNA::Internal::Renderers::OpenGL4::GL4;

namespace CNA::Internal::Renderers::OpenGL4
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        /// XNA's four HiDef vertex samplers live on native units 16..19, after the complete
        /// sixteen-slot pixel range (MojoShader's MOJOSHADER_XNA4_VERTEX_TEXTURES layout).
        constexpr int kVertexSamplerUnitOffset = 16;

        /// MOJOSHADER_glGetProcAddress carries an opaque `data` pointer alongside the function name;
        /// it carries this renderer's platform loader, so MojoShader resolves every GL function
        /// through the same loader the renderer's own entry points came from.
        void* GlProcAddressTrampoline(const char* fnname, void* data)
        {
            auto loader = reinterpret_cast<CNA::Platform::GlProcAddressLoader>(data);
            return loader != nullptr ? loader(fnname) : nullptr;
        }

        GLenum ToGLPrimitive(PrimitiveType primitive)
        {
            switch (primitive)
            {
            case PrimitiveType::TriangleList:  return GL_TRIANGLES;
            case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
            case PrimitiveType::LineList:      return GL_LINES;
            case PrimitiveType::LineStrip:     return GL_LINE_STRIP;
            case PrimitiveType::PointListEXT:  return GL_POINTS;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
            }
        }

        int VertexCountForPrimitives(PrimitiveType primitive, int primitiveCount)
        {
            switch (primitive)
            {
            case PrimitiveType::TriangleList:  return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList:      return primitiveCount * 2;
            case PrimitiveType::LineStrip:     return primitiveCount + 1;
            case PrimitiveType::PointListEXT:  return primitiveCount;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
            }
        }

        MOJOSHADER_usage ToMojoShaderUsage(VertexElementUsage usage)
        {
            switch (usage)
            {
            case VertexElementUsage::Position:          return MOJOSHADER_USAGE_POSITION;
            case VertexElementUsage::Color:             return MOJOSHADER_USAGE_COLOR;
            case VertexElementUsage::TextureCoordinate: return MOJOSHADER_USAGE_TEXCOORD;
            case VertexElementUsage::Normal:            return MOJOSHADER_USAGE_NORMAL;
            case VertexElementUsage::Binormal:          return MOJOSHADER_USAGE_BINORMAL;
            case VertexElementUsage::Tangent:           return MOJOSHADER_USAGE_TANGENT;
            case VertexElementUsage::BlendIndices:      return MOJOSHADER_USAGE_BLENDINDICES;
            case VertexElementUsage::BlendWeight:       return MOJOSHADER_USAGE_BLENDWEIGHT;
            case VertexElementUsage::Depth:             return MOJOSHADER_USAGE_DEPTH;
            case VertexElementUsage::Fog:               return MOJOSHADER_USAGE_FOG;
            case VertexElementUsage::PointSize:         return MOJOSHADER_USAGE_POINTSIZE;
            case VertexElementUsage::Sample:            return MOJOSHADER_USAGE_SAMPLE;
            case VertexElementUsage::TessellateFactor:  return MOJOSHADER_USAGE_TESSFACTOR;
            }
            throw std::invalid_argument(
                "OpenGL4 compiled effect: unrecognized VertexElementUsage ordinal " +
                std::to_string(static_cast<int>(usage)));
        }

        /// Component count, wire type and normalisation of one XNA VertexElementFormat, for
        /// MOJOSHADER_glSetVertexAttribute.
        struct GlAttributeFormat
        {
            unsigned int size = 0;
            MOJOSHADER_attributeType type = MOJOSHADER_ATTRIBUTE_FLOAT;
            int normalized = 0;
        };

        GlAttributeFormat ToGlAttributeFormat(VertexElementFormat format)
        {
            switch (format)
            {
            case VertexElementFormat::Single:  return {1, MOJOSHADER_ATTRIBUTE_FLOAT, 0};
            case VertexElementFormat::Vector2: return {2, MOJOSHADER_ATTRIBUTE_FLOAT, 0};
            case VertexElementFormat::Vector3: return {3, MOJOSHADER_ATTRIBUTE_FLOAT, 0};
            case VertexElementFormat::Vector4: return {4, MOJOSHADER_ATTRIBUTE_FLOAT, 0};
            case VertexElementFormat::Color:   return {4, MOJOSHADER_ATTRIBUTE_UBYTE, 1};
            case VertexElementFormat::Byte4:   return {4, MOJOSHADER_ATTRIBUTE_UBYTE, 0};
            case VertexElementFormat::Short2:  return {2, MOJOSHADER_ATTRIBUTE_SHORT, 0};
            case VertexElementFormat::Short4:  return {4, MOJOSHADER_ATTRIBUTE_SHORT, 0};
            case VertexElementFormat::NormalizedShort2:
                return {2, MOJOSHADER_ATTRIBUTE_SHORT, 1};
            case VertexElementFormat::NormalizedShort4:
                return {4, MOJOSHADER_ATTRIBUTE_SHORT, 1};
            case VertexElementFormat::HalfVector2:
                return {2, MOJOSHADER_ATTRIBUTE_HALF_FLOAT, 0};
            case VertexElementFormat::HalfVector4:
                return {4, MOJOSHADER_ATTRIBUTE_HALF_FLOAT, 0};
            }
            throw std::invalid_argument(
                "OpenGL4 compiled effect: unrecognized VertexElementFormat ordinal " +
                std::to_string(static_cast<int>(format)));
        }

        /// The public XNA name of a reflected sampler's dimension, for a refusal's text.
        [[nodiscard]] const char* SamplerKindName(MOJOSHADER_samplerType type)
        {
            switch (type)
            {
            case MOJOSHADER_SAMPLER_CUBE:   return "samplerCUBE (TextureCube)";
            case MOJOSHADER_SAMPLER_VOLUME: return "sampler3D (Texture3D)";
            default:                        return "sampler2D (Texture2D)";
            }
        }

        /// Removes a texture from the GL target read by one reflected sampler.
        ///
        /// FNA passes a null XNA texture through to FNA3D, whose OpenGL driver binds texture zero
        /// to the target that occupied that slot, so a null Effect parameter is a valid state, not
        /// a draw-time error. The reflected sampler dimension names the target this draw observes.
        void UnbindSamplerTexture(int unit, MOJOSHADER_samplerType type)
        {
            GLenum target = GL_TEXTURE_2D;
            if (type == MOJOSHADER_SAMPLER_CUBE)
                target = GL_TEXTURE_CUBE_MAP;
            else if (type == MOJOSHADER_SAMPLER_VOLUME)
                target = GL_TEXTURE_3D;
            gl4_glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
            glBindTexture(target, 0);
            if (unit != 0)
                gl4_glActiveTexture(GL_TEXTURE0);
        }

        /// Mip levels a render target allocated: HasDefinedMipLevel answers true exactly for its
        /// allocated chain.
        [[nodiscard]] int RenderTargetLevelCount(const OpenGL4RenderTargetRenderer& target)
        {
            int levels = 1;
            while (target.HasDefinedMipLevel(levels)) ++levels;
            return levels;
        }

        /// plans/plan_fx.md FX-082: the streams a compiled-effect draw reads its attributes from, in
        /// public binding-slot order. The internal staged routes (DrawUser*, SpriteBatch) bind no
        /// public VertexBufferBinding and leave vertexStreamCount at 0; they contribute the one
        /// buffer the draw named. A per-instance stream keeps its InstanceFrequency so the divisor
        /// can be set after the program is ready.
        std::vector<OpenGL4Renderer::CompiledEffectStreamEXT> CollectCompiledEffectStreams(
            const OpenGL4VertexBufferRenderer& primary, const GpuDrawParams& params)
        {
            std::vector<OpenGL4Renderer::CompiledEffectStreamEXT> streams;
            streams.reserve(static_cast<std::size_t>(std::max(params.vertexStreamCount, 1)));
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const GpuVertexStreamBinding& stream =
                    params.vertexStreams[static_cast<std::size_t>(i)];
                const auto* buffer = static_cast<const OpenGL4VertexBufferRenderer*>(stream.buffer);
                if (buffer == nullptr) continue;
                const std::size_t stride = stream.strideInBytes > 0
                    ? static_cast<std::size_t>(stream.strideInBytes)
                    : buffer->GetStride();
                OpenGL4Renderer::CompiledEffectStreamEXT entry;
                entry.buffer = buffer;
                entry.stride = stride;
                entry.baseByteOffset =
                    static_cast<std::size_t>(std::max(stream.vertexOffset, 0)) * stride;
                entry.instanceFrequency = stream.instanceFrequency > 0
                    ? static_cast<unsigned int>(stream.instanceFrequency) : 0u;
                entry.binding = &stream;
                streams.push_back(entry);
            }
            if (streams.empty())
            {
                OpenGL4Renderer::CompiledEffectStreamEXT entry;
                entry.buffer = &primary;
                entry.stride = primary.GetStride();
                streams.push_back(entry);
            }
            return streams;
        }

        /// A compiled effect's vertex shader declares arbitrary semantics, so a stride alone cannot
        /// describe its input: every bound stream must carry a real declaration.
        void RequireCompiledEffectDeclarations(
            const std::vector<OpenGL4Renderer::CompiledEffectStreamEXT>& streams)
        {
            for (const auto& stream : streams)
            {
                if (stream.buffer != nullptr && !stream.buffer->GetDeclarationElements().empty())
                    continue;
                throw System::NotSupportedException(
                    "CNA OpenGL4: a compiled-effect draw needs every bound vertex buffer's own "
                    "VertexDeclaration; this renderer does not infer one from stride for this "
                    "route.");
            }
        }

        /// Leaves the compiled-effect vertex array bound for one draw and unbinds it on every exit,
        /// so a refused draw cannot leave it current for an unrelated later bind.
        class ScopedCompiledVertexArray
        {
        public:
            explicit ScopedCompiledVertexArray(unsigned int vao) { gl4_glBindVertexArray(vao); }
            ~ScopedCompiledVertexArray() { gl4_glBindVertexArray(0); }
            ScopedCompiledVertexArray(const ScopedCompiledVertexArray&) = delete;
            ScopedCompiledVertexArray& operator=(const ScopedCompiledVertexArray&) = delete;
        };
    }

    // ------------------------------------------------------------------------------------
    // Context and registry
    // ------------------------------------------------------------------------------------

    MOJOSHADER_glContext* OpenGL4Renderer::GetMojoShaderContextEXT()
    {
        if (mojoShaderContext_ != nullptr)
            return mojoShaderContext_;

        // MOJOSHADER_glCreateContext looks its GL functions up itself, through the loader this
        // renderer's own entry points were resolved with (GlProcAddressTrampoline). GLSL 1.20 is
        // named explicitly: MOJOSHADER_glBestProfile prefers `glspirv` on a recent desktop driver,
        // and that adapter cannot link a valid pixel-only pass (plans/plan_fx.md FX-128).
        void* loaderData = reinterpret_cast<void*>(platformContext_->GetLoader());
        mojoShaderContext_ = MOJOSHADER_glCreateContext(
            MOJOSHADER_PROFILE_GLSL120, GlProcAddressTrampoline, loaderData,
            nullptr, nullptr, nullptr);
        if (mojoShaderContext_ != nullptr)
            MOJOSHADER_glMakeContextCurrent(mojoShaderContext_);
        return mojoShaderContext_;
    }

    void OpenGL4Renderer::MakeMojoShaderContextCurrentEXT() const
    {
        // GL4-0021: MojoShader's context record is not the GL context -- the GL one first.
        platformContext_->EnsureCurrent();
        if (mojoShaderContext_ != nullptr)
            MOJOSHADER_glMakeContextCurrent(mojoShaderContext_);
    }

    void OpenGL4Renderer::RegisterCompiledEffectEXT(OpenGL4CompiledEffect* effect)
    {
        if (effect == nullptr ||
            std::find(compiledEffects_.begin(), compiledEffects_.end(), effect) !=
                compiledEffects_.end())
        {
            return;
        }
        compiledEffects_.push_back(effect);
    }

    void OpenGL4Renderer::UnregisterCompiledEffectEXT(OpenGL4CompiledEffect* effect)
    {
        compiledEffects_.erase(
            std::remove(compiledEffects_.begin(), compiledEffects_.end(), effect),
            compiledEffects_.end());
    }

    void OpenGL4Renderer::ReleaseCompiledEffectResourcesEXT()
    {
        // A compiled effect can outlive its device: the CNAEXT engine layer holds post-process
        // passes by shared_ptr, and a pass's SpriteBatch owns the embedded SpriteEffect. Deleting
        // that effect later would call MOJOSHADER_glDeleteShader against the context destroyed
        // below, so every live effect releases its native state now, while this GL context is
        // still current, and forgets this renderer.
        MakeMojoShaderContextCurrentEXT();
        const std::vector<OpenGL4CompiledEffect*> effects = std::move(compiledEffects_);
        compiledEffects_.clear();
        for (OpenGL4CompiledEffect* effect : effects)
        {
            if (effect != nullptr)
                effect->DetachFromRenderer();
        }
        if (mojoShaderContext_ != nullptr)
        {
            MOJOSHADER_glMakeContextCurrent(nullptr);
            MOJOSHADER_glDestroyContext(mojoShaderContext_);
            mojoShaderContext_ = nullptr;
        }

        if (compiledEffectVao_ != 0)
            gl4_glDeleteVertexArrays(1, &compiledEffectVao_);
        compiledEffectVao_ = 0;
        for (CompiledEffectFlippedSource& copy : compiledFlippedSources_)
        {
            if (copy.framebuffer != 0) gl4_glDeleteFramebuffers(1, &copy.framebuffer);
            if (copy.texture != 0) glDeleteTextures(1, &copy.texture);
            copy = {};
        }
        if (compiledFlipReadFbo_ != 0)
            gl4_glDeleteFramebuffers(1, &compiledFlipReadFbo_);
        compiledFlipReadFbo_ = 0;
    }

    std::unique_ptr<ICompiledEffectRuntime> OpenGL4Renderer::CreateCompiledEffect(
        const std::uint8_t* effectCode, std::size_t effectCodeBytes)
    {
        EnsureCallingThreadContext();
        return std::make_unique<OpenGL4CompiledEffect>(*this, effectCode, effectCodeBytes);
    }

    unsigned int OpenGL4Renderer::EnsureCompiledEffectVaoEXT()
    {
        if (compiledEffectVao_ == 0)
            gl4_glGenVertexArrays(1, &compiledEffectVao_);
        return compiledEffectVao_;
    }

    // ------------------------------------------------------------------------------------
    // Render-target row order (plans/plan_fx.md FX-099)
    // ------------------------------------------------------------------------------------

    unsigned int OpenGL4Renderer::AcquireCompiledEffectFlippedSourceEXT(
        int slot, const OpenGL4RenderTargetRenderer& source)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots)
        {
            throw System::NotSupportedException(
                "CNA OpenGL4: a compiled Effect's sampler slot is outside this renderer's range.");
        }
        // Reading a target while drawing into it is undefined in XNA too, and here it would also
        // be an invalid framebuffer blit. Named, not silently produced.
        bool sourceIsCurrentTarget =
            bound_->rt2D == static_cast<const IRenderTargetRenderer*>(&source);
        for (int i = 0; i < bound_->mrtCount; ++i)
            sourceIsCurrentTarget = sourceIsCurrentTarget ||
                                    bound_->mrt[static_cast<std::size_t>(i)].rt2D == &source;
        if (sourceIsCurrentTarget)
        {
            throw System::NotSupportedException(
                "CNA OpenGL4: a compiled Effect cannot sample the RenderTarget2D it is drawing "
                "into.");
        }

        const int width = source.GetWidth();
        const int height = source.GetHeight();
        const int levelCount = RenderTargetLevelCount(source);
        const int surfaceFormat = source.GetSurfaceFormatEXT();
        // The copy keeps the target's own storage: a float shadow map holds depth differences
        // far below 1/255, and copying it through RGBA8 turns them into self-shadow stripes.
        Detail::RenderTargetColorStorage colorStorage{};
        if (!Detail::MapRenderTargetColorFormat(surfaceFormat, colorStorage))
        {
            throw System::NotSupportedException(
                "CNA OpenGL4: a compiled Effect cannot copy a render target with an unsupported "
                "SurfaceFormat.");
        }

        // Saved BEFORE anything below binds a framebuffer: the lazy creation binds this slot's
        // copy as the draw target, and restoring that instead of the caller's target would send
        // the compiled draw that follows into the copy.
        const Detail::ScopedFramebufferBindings framebuffers;

        CompiledEffectFlippedSource& copy = compiledFlippedSources_[static_cast<std::size_t>(slot)];
        if (copy.texture != 0 &&
            (copy.width != width || copy.height != height || copy.levelCount != levelCount ||
             copy.surfaceFormat != surfaceFormat))
        {
            if (copy.framebuffer != 0) gl4_glDeleteFramebuffers(1, &copy.framebuffer);
            glDeleteTextures(1, &copy.texture);
            copy = {};
        }
        if (copy.texture == 0)
        {
            glGenTextures(1, &copy.texture);
            // Allocated on its own unit, so preparing a later sampler's copy cannot replace an
            // earlier sampler's binding on whichever unit happened to be active.
            gl4_glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + slot));
            glBindTexture(GL_TEXTURE_2D, copy.texture);
            {
                const Detail::ScopedUnpackState unpack(1);
                for (int level = 0; level < levelCount; ++level)
                {
                    glTexImage2D(GL_TEXTURE_2D, level,
                                 static_cast<GLint>(colorStorage.internalFormat),
                                 std::max(1, width >> level), std::max(1, height >> level), 0,
                                 colorStorage.pixelFormat, colorStorage.pixelType, nullptr);
                }
            }
            // REMED-GFX-174: a one-level copy under a mip-using filter must stay complete.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levelCount - 1);
            // The target itself samples with Direct3D 9's expansion of its missing channels
            // (Single as (R,1,1,1)); its copy is sampled in its place and has to agree.
            Detail::ApplyRenderTargetChannelSwizzle(GL_TEXTURE_2D, surfaceFormat);
            gl4_glGenFramebuffers(1, &copy.framebuffer);
            gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copy.framebuffer);
            gl4_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                       copy.texture, 0);
            copy.width = width;
            copy.height = height;
            copy.levelCount = levelCount;
            copy.surfaceFormat = surfaceFormat;
        }
        if (compiledFlipReadFbo_ == 0)
            gl4_glGenFramebuffers(1, &compiledFlipReadFbo_);

        {
            // glBlitFramebuffer honours the scissor test, which has nothing to do with this copy.
            const ScopedScissorTestDisabled fullSurface;
            // The target's own colour texture is attached to this renderer's read framebuffer, so
            // a multisample target is read from its resolved single-sample texture and neither of
            // its own framebuffers is disturbed.
            gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, compiledFlipReadFbo_);
            gl4_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                       source.GetColorGLHandle(), 0);
            gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copy.framebuffer);
            // The destination Y range runs the other way, which is the whole correction.
            gl4_glBlitFramebuffer(0, 0, width, height, 0, height, width, 0, GL_COLOR_BUFFER_BIT,
                                  GL_NEAREST);
            // Not left attached: a target deleted later would otherwise stay allocated behind
            // this framebuffer until the next copy replaced the attachment.
            gl4_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                       0, 0);
        }

        if (levelCount > 1)
        {
            // The blit fills level 0 only; the rest of the chain is rebuilt from it, exactly as
            // UnbindAsRenderTarget does for the target itself.
            const Detail::ScopedTextureBinding binding(GL_TEXTURE_2D, copy.texture);
            gl4_glGenerateMipmap(GL_TEXTURE_2D);
        }
        return copy.texture;
    }

    // ------------------------------------------------------------------------------------
    // Draw-time binding
    // ------------------------------------------------------------------------------------

    void OpenGL4Renderer::BindCompiledEffectForDrawEXT(
        const CompiledEffectStreamEXT* streams, std::size_t streamCount,
        ICompiledEffectRuntime& runtime, const ITextureRenderer* spriteBatchSlotZeroTexture,
        const Microsoft::Xna::Framework::Graphics::TextureCollection* deviceTextures,
        const Microsoft::Xna::Framework::Graphics::SamplerStateCollection* deviceSamplerStates,
        const Microsoft::Xna::Framework::Graphics::TextureCollection* deviceVertexTextures,
        const Microsoft::Xna::Framework::Graphics::SamplerStateCollection*
            deviceVertexSamplerStates)
    {
        using Microsoft::Xna::Framework::Graphics::VertexElement;

        auto* effect = dynamic_cast<OpenGL4CompiledEffect*>(&runtime);
        if (effect == nullptr || effect->renderer_ != this)
        {
            throw std::runtime_error(
                "OpenGL4 compiled effect: the applied effect was not created by this renderer.");
        }
        if (streams == nullptr || streamCount == 0)
        {
            throw std::runtime_error(
                "OpenGL4 compiled effect: a compiled-effect draw needs at least one vertex "
                "stream.");
        }

        MakeMojoShaderContextCurrentEXT();
        MOJOSHADER_glShader* vertexShader = nullptr;
        MOJOSHADER_glShader* pixelShader = nullptr;
        MOJOSHADER_glGetBoundShaders(&vertexShader, &pixelShader);
        if (vertexShader == nullptr || pixelShader == nullptr)
        {
            throw std::runtime_error(
                "OpenGL4 compiled effect: the applied pass bound no shader pair.");
        }

        // plans/plan_fx.md FX-098: force MojoShader to re-issue glUseProgram for this pass.
        // MOJOSHADER_glBindProgram shadows the current program and MOJOSHADER_glProgramReady never
        // calls glUseProgram, so once any stock draw, SpriteBatch flush or ShaderEffect has made
        // its own program current, MojoShader still believes its program is -- and the next
        // compiled draw would run the stock program instead. Unbinding and rebinding the same pair
        // is the public way to say "assume nothing"; the pair comes from the linker cache, so
        // nothing is recompiled, and the bounce also resets the enabled-array bookkeeping the
        // attribute binding below repopulates.
        MOJOSHADER_glBindShaders(nullptr, nullptr);
        MOJOSHADER_glBindShaders(vertexShader, pixelShader);

        const MOJOSHADER_parseData* vertexParseData = MOJOSHADER_glGetShaderParseData(vertexShader);
        const MOJOSHADER_parseData* pixelParseData = MOJOSHADER_glGetShaderParseData(pixelShader);
        if (vertexParseData == nullptr || pixelParseData == nullptr)
        {
            throw std::runtime_error(
                "OpenGL4 compiled effect: the applied pass's shaders have no reflection.");
        }

        // Vertex attributes. plans/plan_fx.md FX-082: every shader input is searched for across
        // every bound stream and bound from ITS OWN buffer, stride and VertexOffset.
        // MOJOSHADER_glSetVertexAttribute calls glVertexAttribPointer immediately, so the array
        // buffer bound at that moment is what each attribute captures. A shader input no stream
        // supplies fails loudly rather than sampling stale vertex data.
        struct BoundAttribute
        {
            MOJOSHADER_usage usage;
            int index;
            unsigned int divisor;
        };
        std::vector<BoundAttribute> boundAttributes;
        boundAttributes.reserve(
            static_cast<std::size_t>(std::max(vertexParseData->attribute_count, 0)));

        for (int i = 0; i < vertexParseData->attribute_count; ++i)
        {
            const MOJOSHADER_attribute& shaderInput = vertexParseData->attributes[i];
            const VertexElement* match = nullptr;
            const CompiledEffectStreamEXT* matchStream = nullptr;
            for (std::size_t s = 0; s < streamCount && match == nullptr; ++s)
            {
                const CompiledEffectStreamEXT& stream = streams[s];
                if (stream.buffer == nullptr) continue;
                const auto& elements = stream.buffer->GetDeclarationElements();
                for (std::size_t elementIndex = 0; elementIndex < elements.size(); ++elementIndex)
                {
                    const VertexElement& element = elements[elementIndex];
                    const int effectiveUsageIndex = stream.binding != nullptr
                        ? stream.binding->EffectiveUsageIndex(elementIndex,
                                                              element.getUsageIndexProperty())
                        : element.getUsageIndexProperty();
                    if (ToMojoShaderUsage(element.getVertexElementUsageProperty()) ==
                            shaderInput.usage &&
                        effectiveUsageIndex == shaderInput.index)
                    {
                        match = &element;
                        matchStream = &stream;
                        break;
                    }
                }
            }
            if (match == nullptr)
            {
                const char* name = shaderInput.name != nullptr ? shaderInput.name : "<unnamed>";
                throw System::NotSupportedException(
                    "CNA OpenGL4: this compiled effect's vertex shader requires attribute '" +
                    std::string(name) + "' (usage " +
                    std::to_string(static_cast<int>(shaderInput.usage)) + ", index " +
                    std::to_string(shaderInput.index) + "), but none of the " +
                    std::to_string(streamCount) +
                    " vertex stream(s) supplied to this draw declares an element with that usage "
                    "and usage index.");
            }

            const GlAttributeFormat glFormat =
                ToGlAttributeFormat(match->getVertexElementFormatProperty());
            const void* offset = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(matchStream->baseByteOffset) +
                static_cast<std::uintptr_t>(match->getOffsetProperty()));
            gl4_glBindBuffer(GL_ARRAY_BUFFER, matchStream->buffer->VboHandle());
            MOJOSHADER_glSetVertexAttribute(
                shaderInput.usage, shaderInput.index, glFormat.size, glFormat.type,
                glFormat.normalized, static_cast<unsigned int>(matchStream->stride), offset);
            boundAttributes.push_back(
                BoundAttribute{shaderInput.usage, shaderInput.index,
                               matchStream->instanceFrequency});
        }

        // Pixel-stage samplers follow FNA's GraphicsDevice slot semantics: an Effect parameter
        // supplies a non-null texture, a null parameter leaves the device slot in force, and when
        // both are null the reflected target is explicitly unbound and the draw still proceeds --
        // FNA3D's OpenGL VerifySampler takes the same path.
        for (int i = 0; i < pixelParseData->sampler_count; ++i)
        {
            const MOJOSHADER_sampler& sampler = pixelParseData->samplers[i];
            // A ps_1_4 BEM instruction needs the destination-numbered bump matrix uniform but
            // performs no texture lookup; the MojoShader patch marks that synthetic uniform carrier
            // with bit 1 while ordinary TEXBEM/L sampling uses bit 0.
            if ((sampler.texbem & 2) != 0 && (sampler.texbem & 1) == 0)
                continue;
            Texture* texture = nullptr;
            Microsoft::Xna::Framework::Graphics::SamplerState samplerState;
            bool samplerAssigned = false;
            effect->GetBoundSamplerEXT(static_cast<std::uint32_t>(sampler.index),
                                       /*vertexStage=*/false, texture, samplerState,
                                       samplerAssigned);
            Texture* selectedTexture = texture;
            Detail::CompiledSamplerTexture nativeTexture;
            nativeTexture.ownedTexture2D = effect->boundTexture2DResources_[sampler.index];
            nativeTexture.ownedVolume = effect->boundTexture3DResources_[sampler.index];
            nativeTexture.ownedCube = effect->boundTextureCubeResources_[sampler.index];
            nativeTexture.texture2D = nativeTexture.ownedTexture2D.get();
            nativeTexture.volume = nativeTexture.ownedVolume.get();
            nativeTexture.cube = nativeTexture.ownedCube.get();
            // plans/plan_fx.md FX-080: SpriteBatch overwrites slot 0 with the drawn texture after the
            // effect's pass applies, exactly as FNA's SpriteBatch does with Textures[0].
            if (sampler.index == 0 && spriteBatchSlotZeroTexture != nullptr)
            {
                nativeTexture = Detail::CompiledSamplerTexture{};
                nativeTexture.texture2D = spriteBatchSlotZeroTexture;
            }
            else if (deviceTextures != nullptr)
            {
                selectedTexture = (*deviceTextures)[sampler.index];
                nativeTexture = Detail::ResolveCompiledSamplerTexture(selectedTexture);
            }
            const std::string slotName = std::to_string(sampler.index) + " ('" +
                (sampler.name != nullptr ? sampler.name : "<unnamed>") + "')";
            if (!nativeTexture.Resolved())
            {
                if (selectedTexture != nullptr)
                {
                    throw System::NotSupportedException(
                        "CNA OpenGL4: this compiled effect's pixel shader samples slot " +
                        slotName + ", but the texture bound there is not owned by this OpenGL4 "
                        "graphics device.");
                }
                UnbindSamplerTexture(static_cast<int>(sampler.index), sampler.type);
                continue;
            }
            // plans/plan_fx.md FX-110: GL binds a texture per TARGET, so a cube bound where the
            // shader declared sampler2D would leave the sampler reading an incomplete 2D target --
            // black, silently. Named instead.
            if (sampler.type != nativeTexture.Kind())
            {
                throw System::NotSupportedException(
                    "CNA OpenGL4: this compiled effect's pixel shader declares " +
                    std::string(SamplerKindName(sampler.type)) + " at slot " + slotName +
                    ", but the texture bound there is a " +
                    SamplerKindName(nativeTexture.Kind()) + ". The dimensions must match.");
            }
            // plans/plan_fx.md FX-099: a render target's rows are stored the other way up from an
            // uploaded texture, and MojoShader's GLSL carries none of this renderer's
            // sampling-time correction; a corrected copy is bound instead, only for the sources
            // SampledRowOrderIsBottomUp names, so nothing is flipped twice.
            if (SampledRowOrderIsBottomUp(nativeTexture.texture2D))
            {
                const auto* renderTarget =
                    dynamic_cast<const OpenGL4RenderTargetRenderer*>(nativeTexture.texture2D);
                if (renderTarget == nullptr)
                {
                    throw System::NotSupportedException(
                        "CNA OpenGL4: this compiled effect samples a rendered texture whose row "
                        "order this renderer cannot correct (slot " +
                        std::to_string(sampler.index) + ").");
                }
                const unsigned int corrected = AcquireCompiledEffectFlippedSourceEXT(
                    static_cast<int>(sampler.index), *renderTarget);
                gl4_glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + sampler.index));
                glBindTexture(GL_TEXTURE_2D, corrected);
            }
            else
            {
                nativeTexture.BindGL(static_cast<int>(sampler.index));
            }
            // plans/plan_fx.md FX-083: the pass's own sampler_state block reaches the GPU here. A
            // slot no pass assigned keeps whatever the game (or SpriteBatch.Begin) selected.
            if (deviceSamplerStates != nullptr)
            {
                samplerState = (*deviceSamplerStates)[sampler.index];
                samplerAssigned = true;
            }
            if (samplerAssigned)
            {
                ApplySamplerState(static_cast<int>(sampler.index),
                                  static_cast<int>(samplerState.getFilterProperty()),
                                  static_cast<int>(samplerState.getAddressUProperty()),
                                  static_cast<int>(samplerState.getAddressVProperty()),
                                  samplerState.getMaxAnisotropyProperty());
                ApplySamplerAddressW(static_cast<int>(sampler.index),
                                     static_cast<int>(samplerState.getAddressWProperty()));
                ApplySamplerMipState(static_cast<int>(sampler.index),
                                     samplerState.getMaxMipLevelProperty(),
                                     samplerState.getMipMapLevelOfDetailBiasProperty());
            }
        }

        // XNA 4.0's four independent HiDef vertex sampler slots, on native units 16..19: logical
        // vertex register zero must never alias pixel register zero.
        for (int i = 0; i < vertexParseData->sampler_count; ++i)
        {
            const MOJOSHADER_sampler& sampler = vertexParseData->samplers[i];
            if (sampler.index < 0 || sampler.index >= 4)
            {
                throw System::NotSupportedException(
                    "CNA OpenGL4: a compiled vertex sampler register is outside XNA's four-slot "
                    "HiDef range.");
            }
            const int nativeUnit = kVertexSamplerUnitOffset + sampler.index;
            Texture* texture = nullptr;
            Microsoft::Xna::Framework::Graphics::SamplerState samplerState;
            bool samplerAssigned = false;
            effect->GetBoundSamplerEXT(static_cast<std::uint32_t>(sampler.index),
                                       /*vertexStage=*/true, texture, samplerState,
                                       samplerAssigned);
            Texture* selectedTexture = texture;
            Detail::CompiledSamplerTexture nativeTexture;
            nativeTexture.ownedTexture2D = effect->boundVertexTexture2DResources_[sampler.index];
            nativeTexture.ownedVolume = effect->boundVertexTexture3DResources_[sampler.index];
            nativeTexture.ownedCube = effect->boundVertexTextureCubeResources_[sampler.index];
            nativeTexture.texture2D = nativeTexture.ownedTexture2D.get();
            nativeTexture.volume = nativeTexture.ownedVolume.get();
            nativeTexture.cube = nativeTexture.ownedCube.get();
            if (deviceVertexTextures != nullptr)
            {
                selectedTexture = (*deviceVertexTextures)[sampler.index];
                nativeTexture = Detail::ResolveCompiledSamplerTexture(selectedTexture);
            }
            const std::string slotName = std::to_string(sampler.index) + " ('" +
                (sampler.name != nullptr ? sampler.name : "<unnamed>") + "')";
            if (!nativeTexture.Resolved())
            {
                if (selectedTexture != nullptr)
                {
                    throw System::NotSupportedException(
                        "CNA OpenGL4: this compiled effect's vertex shader samples slot " +
                        slotName + ", but the texture bound there is not owned by this OpenGL4 "
                        "graphics device.");
                }
                UnbindSamplerTexture(nativeUnit, sampler.type);
                continue;
            }
            if (sampler.type != nativeTexture.Kind())
            {
                throw System::NotSupportedException(
                    "CNA OpenGL4: this compiled effect's vertex shader declares " +
                    std::string(SamplerKindName(sampler.type)) + " at slot " + slotName +
                    ", but the texture bound there is a " +
                    SamplerKindName(nativeTexture.Kind()) + ". The dimensions must match.");
            }
            if (SampledRowOrderIsBottomUp(nativeTexture.texture2D))
            {
                const auto* renderTarget =
                    dynamic_cast<const OpenGL4RenderTargetRenderer*>(nativeTexture.texture2D);
                if (renderTarget == nullptr)
                {
                    throw System::NotSupportedException(
                        "CNA OpenGL4: this compiled effect samples a rendered vertex texture "
                        "whose row order cannot be corrected (slot " +
                        std::to_string(sampler.index) + ").");
                }
                const unsigned int corrected =
                    AcquireCompiledEffectFlippedSourceEXT(nativeUnit, *renderTarget);
                gl4_glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + nativeUnit));
                glBindTexture(GL_TEXTURE_2D, corrected);
            }
            else
            {
                nativeTexture.BindGL(nativeUnit);
            }
            if (deviceVertexSamplerStates != nullptr)
            {
                samplerState = (*deviceVertexSamplerStates)[sampler.index];
                samplerAssigned = true;
            }
            if (samplerAssigned)
            {
                ApplySamplerState(nativeUnit, static_cast<int>(samplerState.getFilterProperty()),
                                  static_cast<int>(samplerState.getAddressUProperty()),
                                  static_cast<int>(samplerState.getAddressVProperty()),
                                  samplerState.getMaxAnisotropyProperty());
                ApplySamplerAddressW(nativeUnit,
                                     static_cast<int>(samplerState.getAddressWProperty()));
                ApplySamplerMipState(nativeUnit, samplerState.getMaxMipLevelProperty(),
                                     samplerState.getMipMapLevelOfDetailBiasProperty());
            }
        }
        // Every bind above left its own unit active; unit 0 is what the rest of the renderer
        // assumes between draws.
        gl4_glActiveTexture(GL_TEXTURE0);

        for (int i = 0; i < pixelParseData->sampler_count; ++i)
        {
            const MOJOSHADER_sampler& sampler = pixelParseData->samplers[i];
            if (sampler.texbem == 0 || sampler.index < 0 || sampler.index >= 16)
                continue;
            const auto& state = compiledLegacyBumpMapEnvs_[static_cast<std::size_t>(sampler.index)];
            MOJOSHADER_glSetLegacyBumpMapEnv(
                static_cast<unsigned int>(sampler.index), state.matrix[0], state.matrix[1],
                state.matrix[2], state.matrix[3], state.luminanceScale, state.luminanceOffset);
        }

        // Pushes the shared register files (populated by ApplyPass()'s internal
        // MOJOSHADER_effectCommitChanges) into the bound program and enables the attribute arrays
        // MOJOSHADER_glSetVertexAttribute flagged above.
        MOJOSHADER_glProgramReady();

        // plans/plan_fx.md FX-082: instance step rates, written for EVERY bound attribute through
        // the shared compiled-effect array object -- a divisor left over from a previous instanced
        // draw would otherwise still be in force on a per-vertex attribute.
        for (const BoundAttribute& attribute : boundAttributes)
        {
            const int location =
                MOJOSHADER_glGetVertexAttribLocation(attribute.usage, attribute.index);
            if (location >= 0)
                gl4_glVertexAttribDivisor(static_cast<GLuint>(location), attribute.divisor);
        }

        // Direct3D 9 coordinate fixups the generated GLSL applies through injected uniforms,
        // which MOJOSHADER_effectCommitChanges does not populate and which depend on the target.
        //
        // plans/plan_fx.md FX-088: `renderTargetBound` is reported as 0 even when one IS bound. The
        // flag makes MojoShader negate gl_Position.y (FNA3D's emulation of Direct3D 9's top-down
        // targets, paired there with an inverted front face). This renderer never flips geometry
        // for a framebuffer object and corrects the bottom-up texel order where it is observed,
        // exactly like EasyGL; reporting a bound target would mirror compiled geometry against
        // every other draw. The sizes still describe the real target, so VPOS converts
        // gl_FragCoord.y with the height actually being rendered into.
        int targetW = 0, targetH = 0;
        if (!GetBoundRenderTargetSize(targetW, targetH))
            GetPhysicalSize(targetW, targetH);
        MOJOSHADER_glProgramViewportInfo(targetW, targetH, targetW, targetH,
                                         /*renderTargetBound=*/0);

        // Ordinary compiled geometry takes the same Direct3D 9 pixel-centre correction as stock
        // geometry (BindDrawParams). SpriteBatch's compiled route does not: its quads already
        // sample XNA texel centres, and the translation would shift every post-process lookup by
        // almost half a pixel. A multisampled destination suppresses the correction too.
        int viewportX = 0, viewportY = 0, viewportW = 0, viewportH = 0;
        GetGlViewport(viewportX, viewportY, viewportW, viewportH);
        bool multisampledDestination = false;
        if (bound_->rt2D != nullptr && bound_->rt2D->GetMultiSampleCount() > 0)
            multisampledDestination = true;
        if (bound_->cube != nullptr && bound_->cube->GetMultiSampleCount() > 0)
            multisampledDestination = true;
        for (int slot = 0; slot < bound_->mrtCount; ++slot)
        {
            const OpenGL4MrtBinding& target = bound_->mrt[static_cast<std::size_t>(slot)];
            if ((target.rt2D != nullptr && target.rt2D->GetMultiSampleCount() > 0) ||
                (target.cube != nullptr && target.cube->GetMultiSampleCount() > 0))
                multisampledDestination = true;
        }
        const bool correctPixelCenter = !multisampledDestination &&
                                        spriteBatchSlotZeroTexture == nullptr;
        const float pixelCenterX = viewportW > 0 && correctPixelCenter
            ? xnaPixelCenterScale_ / static_cast<float>(viewportW)
            : 0.0f;
        const float pixelCenterY = viewportH > 0 && correctPixelCenter
            ? -xnaPixelCenterScale_ / static_cast<float>(viewportH)
            : 0.0f;
        MOJOSHADER_glProgramPixelCenterInfo(pixelCenterX, pixelCenterY);

        // This renderer keeps OpenGL's counter-clockwise front face while XNA/Direct3D 9 VFACE
        // counts clockwise triangles as front-facing: only the shader-visible sign is inverted;
        // culling keeps the established face mapping.
        MOJOSHADER_glProgramVFaceFlipInfo(1);
    }

    // ------------------------------------------------------------------------------------
    // Draw routes
    // ------------------------------------------------------------------------------------

    void OpenGL4Renderer::DrawCompiledPrimitivesEXT(const IVertexBufferRenderer& vb,
                                                    PrimitiveType primitive, int primitiveCount,
                                                    const GpuDrawParams& params)
    {
        const auto& primary = static_cast<const OpenGL4VertexBufferRenderer&>(vb);
        const auto streams = CollectCompiledEffectStreams(primary, params);
        RequireCompiledEffectDeclarations(streams);
        const ScopedCompiledVertexArray vao(EnsureCompiledEffectVaoEXT());
        BindCompiledEffectForDrawEXT(streams.data(), streams.size(), *params.compiledEffectRuntime,
                                     nullptr, params.compiledDeviceTextures,
                                     params.compiledDeviceSamplerStates,
                                     params.compiledDeviceVertexTextures,
                                     params.compiledDeviceVertexSamplerStates);
        // glDrawArrays' `first` advances every bound stream by that many of its own records --
        // the rule the stock multi-stream route relies on too.
        glDrawArrays(ToGLPrimitive(primitive), params.vertexStart,
                     VertexCountForPrimitives(primitive, primitiveCount));
    }

    void OpenGL4Renderer::DrawCompiledIndexedPrimitivesEXT(const IVertexBufferRenderer& vb,
                                                           const IIndexBufferRenderer& ib,
                                                           PrimitiveType primitive,
                                                           int primitiveCount, bool instanced,
                                                           int instanceCount,
                                                           const GpuDrawParams& params)
    {
        const auto& primary = static_cast<const OpenGL4VertexBufferRenderer&>(vb);
        const auto& indices = static_cast<const OpenGL4IndexBufferRenderer&>(ib);
        const auto streams = CollectCompiledEffectStreams(primary, params);
        RequireCompiledEffectDeclarations(streams);
        const ScopedCompiledVertexArray vao(EnsureCompiledEffectVaoEXT());
        BindCompiledEffectForDrawEXT(streams.data(), streams.size(), *params.compiledEffectRuntime,
                                     nullptr, params.compiledDeviceTextures,
                                     params.compiledDeviceSamplerStates,
                                     params.compiledDeviceVertexTextures,
                                     params.compiledDeviceVertexSamplerStates);
        const GLenum indexType = indices.IsThirtyTwoBit() ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        const std::size_t indexSize = indices.IsThirtyTwoBit() ? 4 : 2;
        const void* indexOffset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(std::max(params.startIndex, 0)) * indexSize);
        // Desktop GL has glDrawElementsBaseVertex: a positive baseVertex advances every per-vertex
        // stream natively and leaves instance streams at their own VertexOffset, which is XNA's
        // rule. A negative one is folded into the index slice, as on the stock route.
        DrawIndexedWithBaseVertexFallback(indices, ToGLPrimitive(primitive),
                                          VertexCountForPrimitives(primitive, primitiveCount),
                                          indexType, indexOffset, params.startIndex,
                                          params.baseVertex, instanced, instanceCount);
    }

    // ------------------------------------------------------------------------------------
    // SpriteBatch (plans/plan_fx.md FX-080, FX-118, FX-120)
    // ------------------------------------------------------------------------------------

    bool OpenGL4SpriteBatchRenderer::BatchFlushesThroughCompiledEffect() const
    {
        return customEffect_ != nullptr && customEffect_->GetCompiledRuntimePtr() != nullptr;
    }

    void OpenGL4SpriteBatchRenderer::ApplyCompiledSpriteVertexShader(int logicalWidth,
                                                                     int logicalHeight)
    {
        using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
        if (customEffect_ == nullptr || logicalWidth <= 0 || logicalHeight <= 0)
        {
            throw std::runtime_error(
                "CNA OpenGL4: the XNA SpriteBatch vertex effect is unavailable.");
        }
        if (spriteCompiledEffect_ == nullptr)
        {
            const auto& bytes = CNA::Internal::Renderers::Fna3d::StockEffectBlobs::kSpriteEffectFxb;
            auto spriteEffect = std::make_unique<OpenGL4CompiledEffect>(*owner_, bytes,
                                                                        sizeof(bytes));
            const auto& parameters = spriteEffect->GetDescription().parameters;
            const auto matrix = std::find_if(
                parameters.begin(), parameters.end(),
                [](const CompiledEffectParameterDescription& parameter)
                {
                    return parameter.name == "MatrixTransform";
                });
            if (matrix == parameters.end())
            {
                throw std::runtime_error(
                    "CNA OpenGL4: embedded XNA SpriteEffect has no MatrixTransform parameter.");
            }
            spriteMatrixParameterIndex_ = matrix->runtimeIndex;
            spriteCompiledEffect_ = std::move(spriteEffect);
        }

        const Matrix projection = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logicalWidth), static_cast<float>(logicalHeight), 0.0f,
            0.0f, -1.0f);
        const Matrix combined = transform_ * projection;
        const float values[16] = {
            combined.M11, combined.M21, combined.M31, combined.M41,
            combined.M12, combined.M22, combined.M32, combined.M42,
            combined.M13, combined.M23, combined.M33, combined.M43,
            combined.M14, combined.M24, combined.M34, combined.M44,
        };
        spriteCompiledEffect_->SetParameterValue(spriteMatrixParameterIndex_, values,
                                                 sizeof(values));
        spriteCompiledEffect_->SetTechnique(0);

        GraphicsDevice& graphicsDevice = customEffect_->getGraphicsDeviceInternal();
        CompiledEffectDeviceState deviceState;
        deviceState.blend = &graphicsDevice.getBlendStateProperty();
        deviceState.depthStencil = &graphicsDevice.getDepthStencilStateProperty();
        deviceState.rasterizer = &graphicsDevice.getRasterizerStateProperty();
        deviceState.samplerStates = &graphicsDevice.getSamplerStatesProperty();
        deviceState.vertexSamplerStates = &graphicsDevice.getVertexSamplerStatesProperty();
        CompiledEffectPassStateChanges ignoredChanges;
        spriteCompiledEffect_->ApplyPass(0, deviceState, ignoredChanges);
    }

    void OpenGL4SpriteBatchRenderer::FlushBatchWithCompiledEffect()
    {
        using Microsoft::Xna::Framework::Graphics::EffectTechnique;
        using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
        using Microsoft::Xna::Framework::Graphics::TextureCollection;

        // FNA applies its stock SpriteEffect before every custom-effect batch. A custom pass may
        // assign only a pixel shader, as Microsoft's SpriteEffects sample does; Direct3D then
        // retains the stock vertex shader and its MatrixTransform. That inheritance is reproduced
        // through the same compiled XNA SpriteEffect, applied before the custom pass.
        ICompiledEffectRuntime* runtime = customEffect_->GetCompiledRuntimePtr();
        if (runtime == nullptr || currentTexture_ == nullptr)
        {
            pendingVertices_.clear();
            pendingIndices_.clear();
            currentTexture_ = nullptr;
            return;
        }

        // FNA's private sprite vertex is POSITION0 Vector3, COLOR0 and TEXCOORD0. This batch keeps
        // colour expanded to four floats, and the same three-component position, so the stock
        // vertex shader and the caller's depth state observe layerDepth.
        static const VertexDeclaration kSpriteDeclaration(
            static_cast<int>(sizeof(Vertex)),
            {
                VertexElement(static_cast<int>(offsetof(Vertex, x)),
                              VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(static_cast<int>(offsetof(Vertex, u)),
                              VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
                VertexElement(static_cast<int>(offsetof(Vertex, r)),
                              VertexElementFormat::Vector4, VertexElementUsage::Color, 0),
            });

        const int vertexCount = static_cast<int>(pendingVertices_.size());
        const int indexCount = static_cast<int>(pendingIndices_.size());

        // plans/plan_fx.md FX-120: retained, not created per flush -- the shared compiled-effect
        // vertex array object records the element buffer, and a per-flush buffer would leave it
        // naming a deleted object.
        if (compiledSpriteVertexBuffer_ == nullptr)
        {
            compiledSpriteVertexBuffer_ = owner_->CreateVertexBuffer(
                static_cast<int>(kMaxVerticesPerBatch));
            compiledSpriteVertexBuffer_->SetVertexDeclaration(kSpriteDeclaration);
        }
        if (compiledSpriteIndexBuffer_ == nullptr)
        {
            compiledSpriteIndexBuffer_ = owner_->CreateIndexBuffer16(
                static_cast<int>(kMaxSpritesPerBatch * 6));
        }
        compiledSpriteVertexBuffer_->SetData(pendingVertices_.data(), vertexCount, sizeof(Vertex));
        compiledSpriteIndexBuffer_->SetData16(pendingIndices_.data(), indexCount);
        const auto* vertexBuffer =
            static_cast<const OpenGL4VertexBufferRenderer*>(compiledSpriteVertexBuffer_.get());
        const auto* indexBuffer =
            static_cast<const OpenGL4IndexBufferRenderer*>(compiledSpriteIndexBuffer_.get());

        // The viewport is still this renderer's business: a batch drawn into a render target
        // rasterises at the target's size (a cube face included), not the window's.
        int logicalWidth = 0;
        int logicalHeight = 0;
        int rtW = 0, rtH = 0;
        if (owner_->GetBoundRenderTargetSize(rtW, rtH) && rtW > 0 && rtH > 0)
        {
            owner_->SetGlViewport(0, 0, rtW, rtH);
            logicalWidth = rtW;
            logicalHeight = rtH;
        }
        else
        {
            int physW = 0, physH = 0;
            owner_->GetPhysicalSize(physW, physH);
            if (physW > 0 && physH > 0) owner_->SetGlViewport(0, 0, physW, physH);
            owner_->GetLogicalSize(logicalWidth, logicalHeight);
        }
        owner_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_,
                                  pendingMaxAnisotropy_);
        owner_->ApplySamplerMipState(0, pendingMaxMipLevel_, pendingLodBias_);
        // VULKAN-167: the batch's own AddressW wins over the W-follows-U default.
        if (pendingAddressW_ >= 0) owner_->ApplySamplerAddressW(0, pendingAddressW_);

        OpenGL4Renderer::CompiledEffectStreamEXT stream;
        stream.buffer = vertexBuffer;
        stream.stride = sizeof(Vertex);

        // FNA draws the batch once per pass of the effect's current technique, applying each pass
        // and then overwriting Textures[0] with the drawn texture. Both are reproduced here.
        EffectTechnique* technique = customEffect_->getCurrentTechniqueProperty();
        const int passCount =
            technique != nullptr ? technique->getPassesProperty().getCountProperty() : 0;
        if (passCount == 0)
        {
            throw System::InvalidOperationException(
                "CNA OpenGL4: a compiled Effect used with SpriteBatch must have a current "
                "technique with at least one pass.");
        }
        ApplyCompiledSpriteVertexShader(logicalWidth, logicalHeight);
        const unsigned int vao = owner_->EnsureCompiledEffectVaoEXT();
        GraphicsDevice& graphicsDevice = customEffect_->getGraphicsDeviceInternal();
        const TextureCollection& deviceTextures = graphicsDevice.getTexturesProperty();
        const SamplerStateCollection& deviceSamplerStates = graphicsDevice.getSamplerStatesProperty();
        const TextureCollection& deviceVertexTextures = graphicsDevice.getVertexTexturesProperty();
        const SamplerStateCollection& deviceVertexSamplerStates =
            graphicsDevice.getVertexSamplerStatesProperty();
        for (int pass = 0; pass < passCount; ++pass)
        {
            technique->getPassesProperty()[pass]->Apply();
            owner_->ApplyStencilPrimitiveTopology(PrimitiveType::TriangleList);
            const ScopedCompiledVertexArray boundVao(vao);
            owner_->BindCompiledEffectForDrawEXT(&stream, 1, *runtime, currentTexture_,
                                                 &deviceTextures, &deviceSamplerStates,
                                                 &deviceVertexTextures,
                                                 &deviceVertexSamplerStates);
            gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer->IboHandle());
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_SHORT,
                           nullptr);
        }

        pendingVertices_.clear();
        pendingIndices_.clear();
        currentTexture_ = nullptr;
    }
}

#endif  // CNA_OPENGL4_COMPILED_EFFECTS
