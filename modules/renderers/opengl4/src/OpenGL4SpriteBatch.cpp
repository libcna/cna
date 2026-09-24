// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0014: OpenGL4's SpriteBatch. CPU-generated quads with
// layer depth, drawn with the device's own blend/depth/rasterizer/sampler state, projected from the
// device Viewport, corrected for render-target row order and submitted in XNA's 2 048-sprite chunks.
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"
#include "CNA/Internal/Renderers/Common/GlStockShaderSources.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "System/NotSupportedException.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

using namespace CNA::Internal::Renderers::OpenGL4::GL4;

namespace CNA::Internal::Renderers::OpenGL4
{
    namespace
    {
        /// D3D9's expansion of a stored format with fewer than four channels: a missing colour
        /// channel reads 1 and a missing alpha reads 1. The sprite program applies it through
        /// uChannelMask/uChannelFill; formats that store all four channels take the identity pair.
        void ApplyChannelExpansion(int maskLocation, int fillLocation, int surfaceFormat)
        {
            if (maskLocation < 0 || fillLocation < 0) return;
            using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
            float mask[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            float fill[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
            case SurfaceFormat::Single:
            case SurfaceFormat::HalfSingle:
                mask[1] = mask[2] = mask[3] = 0.0f;
                fill[1] = fill[2] = fill[3] = 1.0f;
                break;
            case SurfaceFormat::Vector2:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::Rg32:
                mask[2] = mask[3] = 0.0f;
                fill[2] = fill[3] = 1.0f;
                break;
            default:
                break;
            }
            gl4_glUniform4f(maskLocation, mask[0], mask[1], mask[2], mask[3]);
            gl4_glUniform4f(fillLocation, fill[0], fill[1], fill[2], fill[3]);
        }
    }

    OpenGL4SpriteBatchRenderer::OpenGL4SpriteBatchRenderer(OpenGL4Renderer& owner)
        : owner_(&owner)
    {
        const GlStockShaders::GlStockProgramSource source = GlStockShaders::SpriteSource();
        if (!program_.Compile(AdaptGlslEs300ForDesktopCore(source.vertex),
                              AdaptGlslEs300ForDesktopCore(source.fragment)))
        {
            const std::string message =
                "[OpenGL4 GL Error] SpriteBatch program failed to build: " + program_.GetError();
            CNA::Logger::Error(message, CNA::LogCategory::RENDER);
            throw std::runtime_error(message);
        }
        program_.Use();
        if (const int textureLocation = program_.UniformLocation("texture1"); textureLocation >= 0)
            gl4_glUniform1i(textureLocation, 0);
        projectionLocation_ = program_.UniformLocation("projection");
        channelMaskLocation_ = program_.UniformLocation("uChannelMask");
        channelFillLocation_ = program_.UniformLocation("uChannelFill");

        gl4_glGenVertexArrays(1, &vao_);
        gl4_glGenBuffers(1, &vbo_);
        gl4_glGenBuffers(1, &ibo_);
        gl4_glBindVertexArray(vao_);
        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        constexpr GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
        // Position (0) with layer depth, TexCoord (1), Color (2).
        gl4_glEnableVertexAttribArray(0);
        gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
        gl4_glEnableVertexAttribArray(1);
        gl4_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<const void*>(3 * sizeof(float)));
        gl4_glEnableVertexAttribArray(2);
        gl4_glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<const void*>(5 * sizeof(float)));
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl4_glBindVertexArray(0);

        pendingVertices_.reserve(kMaxVerticesPerBatch);
        pendingIndices_.reserve(kMaxSpritesPerBatch * 6);
    }

    OpenGL4SpriteBatchRenderer::~OpenGL4SpriteBatchRenderer()
    {
        if (ibo_ != 0) gl4_glDeleteBuffers(1, &ibo_);
        if (vbo_ != 0) gl4_glDeleteBuffers(1, &vbo_);
        if (vao_ != 0) gl4_glDeleteVertexArrays(1, &vao_);
    }

    void OpenGL4SpriteBatchRenderer::Begin()
    {
        // Blend, depth, rasterizer and sampler state are the device's: SpriteBatch::Begin applied
        // them through GraphicsDevice immediately before this call, and nothing here overrides them.
        begun_ = true;
    }

    void OpenGL4SpriteBatchRenderer::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void OpenGL4SpriteBatchRenderer::ResolveCurrentTextureRowOrder()
    {
        currentTextureBottomUp_ = currentTexture_ != nullptr &&
                                  SampledRowOrderIsBottomUp(currentTexture_);
    }

    void OpenGL4SpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (customEffect_ == effect) return;
        FlushBatch();
        customEffect_ = effect;
        ResolveCurrentTextureRowOrder();
    }

    void OpenGL4SpriteBatchRenderer::SetSamplerState(int textureFilter, int addressU, int addressV,
                                                     int addressW, int maxAnisotropy,
                                                     int maxMipLevel, float lodBias)
    {
        pendingFilter_ = textureFilter;
        pendingAddressU_ = addressU;
        pendingAddressV_ = addressV;
        pendingAddressW_ = addressW;
        pendingMaxAnisotropy_ = maxAnisotropy;
        pendingMaxMipLevel_ = maxMipLevel;
        pendingLodBias_ = lodBias;
    }

    void OpenGL4SpriteBatchRenderer::FlushBatch()
    {
        if (pendingVertices_.empty()) return;

        // Task 1077: a custom effect draws with the SAME program the Effect owns, so the uniforms
        // its ShaderEffect::SetUniform* calls wrote are the ones the draw reads. Apply() runs here,
        // at submission, which is when XNA applies a SpriteBatch effect's pass.
        OpenGL4RawProgram* program = &program_;
        int projectionLocation = projectionLocation_;
        int maskLocation = channelMaskLocation_;
        int fillLocation = channelFillLocation_;
        if (customEffect_ != nullptr)
        {
            if (customEffect_->GetCompiledRuntimePtr() != nullptr)
                throw System::NotSupportedException(
                    "OpenGL4: SpriteBatch cannot draw with a compiled XNA Effect; this renderer "
                    "does not execute compiled effect bytecode.");
            auto* renderer = dynamic_cast<OpenGL4EffectRenderer*>(customEffect_->GetEffectRendererPtr());
            customEffect_->Apply();
            if (renderer != nullptr && renderer->IsValid())
            {
                program = &renderer->GetProgram();
                projectionLocation = program->UniformLocation("projection");
                maskLocation = program->UniformLocation("uChannelMask");
                fillLocation = program->UniformLocation("uChannelFill");
            }
        }
        program->Use();

        int logW = 0, logH = 0;
        int rtW = 0, rtH = 0;
        const bool haveRt = owner_->GetBoundRenderTargetSize(rtW, rtH) && rtW > 0 && rtH > 0;

        // REMED-GFX-072: XNA builds the sprite ortho from Viewport.Width/Height, so a game-set
        // sub-viewport makes sprite coordinates viewport-local and stays the rasterizer viewport.
        int curVx = 0, curVy = 0, curVw = 0, curVh = 0;
        owner_->GetGlViewport(curVx, curVy, curVw, curVh);
        int fullW = 0, fullH = 0;
        if (haveRt) { fullW = rtW; fullH = rtH; }
        else owner_->GetPhysicalSize(fullW, fullH);

        // The default viewport is the presentation rectangle, which under Letterbox/Overscan is not
        // the whole drawable; GetDefaultViewportRect answers top-left-origin, GL bottom-left.
        int defX = 0, defY = 0, defW = fullW, defH = fullH;
        if (!haveRt) owner_->GetDefaultViewportRect(defX, defY, defW, defH);
        const int defGlY = (fullH > 0) ? (fullH - defY - defH) : defY;

        // The renderer's own record, not live GL: a resize leaves the GL viewport holding the
        // previous presentation rectangle until the next SetViewport().
        const bool customViewport = !owner_->ViewportIsDefaultEXT();
        if (customViewport)
        {
            int logicalW = 0, logicalH = 0;
            if (!haveRt) owner_->GetLogicalSize(logicalW, logicalH);
            if (!haveRt && logicalW > 0 && logicalH > 0 && defW > 0 && defH > 0)
            {
                logW = static_cast<int>(std::lround(static_cast<double>(curVw) * logicalW / defW));
                logH = static_cast<int>(std::lround(static_cast<double>(curVh) * logicalH / defH));
            }
            else
            {
                logW = curVw;
                logH = curVh;
            }
        }
        else if (haveRt)
        {
            owner_->SetGlViewport(0, 0, rtW, rtH);
            logW = rtW;
            logH = rtH;
        }
        else
        {
            // Re-assert the CURRENT presentation rectangle: keeps letterbox bars and repairs a
            // rectangle the last resize left stale.
            if (defW > 0 && defH > 0)
                owner_->SetGlViewport(defX, defGlY, defW, defH);
            owner_->GetLogicalSize(logW, logH);
        }
        if (logW <= 0 || logH <= 0)
        {
            int vx = 0, vy = 0;
            owner_->GetGlViewport(vx, vy, logW, logH);
        }

        // XNA's SpriteBatch matrix keeps POSITION.Z as layer depth (near 0, far -1); the stock
        // sprite program converts D3D clip depth to GL's.
        const Matrix ortho = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logW), static_cast<float>(logH), 0.0f, 0.0f, -1.0f);
        const Matrix combined = transform_ * ortho;
        float orthoCol[16];
        combined.ToColumnMajor(orthoCol);
        if (projectionLocation >= 0)
            gl4_glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, orthoCol);

        currentTexture_->BindGL(0);
        ApplyChannelExpansion(maskLocation, fillLocation, currentTexture_->GetSurfaceFormatEXT());
        owner_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_,
                                  pendingMaxAnisotropy_);
        owner_->ApplySamplerMipState(0, pendingMaxMipLevel_, pendingLodBias_);
        // VULKAN-167: ApplySamplerState sets W = U; a state that set W itself wins.
        if (pendingAddressW_ >= 0) owner_->ApplySamplerAddressW(0, pendingAddressW_);

        gl4_glBindVertexArray(vao_);
        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl4_glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr4>(pendingVertices_.size() * sizeof(Vertex)),
                         pendingVertices_.data(), GL_STREAM_DRAW);
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl4_glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr4>(pendingIndices_.size() * sizeof(std::uint16_t)),
                         pendingIndices_.data(), GL_STREAM_DRAW);

        owner_->ApplyStencilPrimitiveTopology(PrimitiveType::TriangleList);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(pendingIndices_.size()), GL_UNSIGNED_SHORT,
                       nullptr);
        gl4_glBindVertexArray(0);

        pendingVertices_.clear();
        pendingIndices_.clear();
        currentTexture_ = nullptr;
    }

    void OpenGL4SpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), w, h),
             Rectangle(0, 0, w, h), Microsoft::Xna::Framework::Color::White);
    }

    void OpenGL4SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                          const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0),
             SpriteEffects::None, 0.0f);
    }

    void OpenGL4SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                          const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color,
                                          float rotation, const Vector2& origin,
                                          SpriteEffects effects, float layerDepth)
    {
        Draw(texture, static_cast<float>(destinationRectangle.X),
             static_cast<float>(destinationRectangle.Y),
             static_cast<float>(destinationRectangle.Width),
             static_cast<float>(destinationRectangle.Height), sourceRectangle, color, rotation,
             origin, effects, layerDepth);
    }

    // The sub-pixel destination overload builds the quad: XNA keeps a sprite's destination
    // unrounded, so a fractional position lands between pixels and the sampler filters its edges.
    void OpenGL4SpriteBatchRenderer::Draw(const ITextureRenderer& texture, float destinationX,
                                          float destinationY, float destinationWidth,
                                          float destinationHeight, const Rectangle& sourceRectangle,
                                          const Color& color, float rotation, const Vector2& origin,
                                          SpriteEffects effects, float layerDepth)
    {
        if (!begun_) throw std::runtime_error("Draw called before Begin()");

        if (currentTexture_ != &texture)
        {
            if (currentTexture_ != nullptr) FlushBatch();
            currentTexture_ = &texture;
            ResolveCurrentTextureRowOrder();
        }
        // XNA's native dynamic buffers hold 2 048 sprites per submission; a UInt16 base past that
        // would wrap and redraw the first quad.
        if (pendingVertices_.size() >= kMaxVerticesPerBatch)
        {
            FlushBatch();
            currentTexture_ = &texture;
            ResolveCurrentTextureRowOrder();
        }

        const float texW = static_cast<float>(texture.GetWidth());
        const float texH = static_cast<float>(texture.GetHeight());

        // Unclamped, as FNA divides: a source rectangle past the texture lets the address mode
        // (Wrap/Mirror/Clamp) govern edge sampling -- the classic tiling-background technique.
        float u1 = static_cast<float>(sourceRectangle.X) / texW;
        float v1 = static_cast<float>(sourceRectangle.Y) / texH;
        float u2 = u1 + static_cast<float>(sourceRectangle.Width) / texW;
        float v2 = v1 + static_cast<float>(sourceRectangle.Height) / texH;

        // A constant coordinate wholly beyond a clamped axis is reduced to the equivalent edge;
        // only for the stock program, whose coordinates no user shader observes.
        using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
        if (customEffect_ == nullptr &&
            pendingAddressU_ == static_cast<int>(TextureAddressMode::Clamp) && u1 == u2)
        {
            if (u1 < 0.0f) u1 = u2 = 0.0f;
            else if (u1 > 1.0f) u1 = u2 = 1.0f;
        }
        if (customEffect_ == nullptr &&
            pendingAddressV_ == static_cast<int>(TextureAddressMode::Clamp) && v1 == v2)
        {
            if (v1 < 0.0f) v1 = v2 = 0.0f;
            else if (v1 > 1.0f) v1 = v2 = 1.0f;
        }

        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally))
            std::swap(u1, u2);
        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically))
            std::swap(v1, v2);

        // REMED-GFX-147: a render target's texels are bottom-up; mirror V in the quad itself, the
        // one input both the stock and a custom program read.
        if (currentTextureBottomUp_)
        {
            v1 = 1.0f - v1;
            v2 = 1.0f - v2;
        }

        const float r = static_cast<float>(color.getRProperty()) / 255.0f;
        const float g = static_cast<float>(color.getGProperty()) / 255.0f;
        const float b = static_cast<float>(color.getBProperty()) / 255.0f;
        const float a = static_cast<float>(color.getAProperty()) / 255.0f;

        const float sw = static_cast<float>(sourceRectangle.Width);
        const float sh = static_cast<float>(sourceRectangle.Height);
        const float scaleX = destinationWidth / sw;
        const float scaleY = destinationHeight / sh;
        const float ox = origin.X;
        const float oy = origin.Y;
        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);
        const auto place = [&](float px, float py, float& rx, float& ry) {
            rx = destinationX + px * cosR - py * sinR;
            ry = destinationY + px * sinR + py * cosR;
        };

        float x0, y0, x1, y1, x2, y2, x3, y3;
        place((0.0f - ox) * scaleX, (0.0f - oy) * scaleY, x0, y0);
        place((sw - ox) * scaleX, (0.0f - oy) * scaleY, x1, y1);
        place((sw - ox) * scaleX, (sh - oy) * scaleY, x2, y2);
        place((0.0f - ox) * scaleX, (sh - oy) * scaleY, x3, y3);

        const auto base = static_cast<std::uint16_t>(pendingVertices_.size());
        pendingVertices_.push_back({x0, y0, layerDepth, u1, v1, r, g, b, a});
        pendingVertices_.push_back({x1, y1, layerDepth, u2, v1, r, g, b, a});
        pendingVertices_.push_back({x2, y2, layerDepth, u2, v2, r, g, b, a});
        pendingVertices_.push_back({x3, y3, layerDepth, u1, v2, r, g, b, a});
        for (const std::uint16_t offset : {0, 1, 2, 2, 3, 0})
            pendingIndices_.push_back(static_cast<std::uint16_t>(base + offset));

        // The shared front end calls this once per public Draw in Immediate mode; target, viewport
        // and device state may change before End(), and XNA binds them at Draw time.
        if (immediateMode_)
            FlushBatch();
    }
}
