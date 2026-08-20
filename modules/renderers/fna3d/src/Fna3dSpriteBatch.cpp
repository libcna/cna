// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Fna3d/Fna3dEnumMapping.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dVertexLayouts.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <cstring>
#include <vector>

namespace CNA::Internal::Renderers::Fna3d
{
    namespace
    {
        constexpr int kSpriteVertexStride = 24;
        // Six 16-bit indices address four vertices per sprite. Quad 16383 ends at vertex 65535;
        // one more quad would wrap the generated base index back to zero.
        constexpr std::size_t kMaxQuadsPerDraw = 16384;

        std::uint32_t PackColor(const Color& color)
        {
            // XNA's Color is already stored AABBGGRR, which is the byte order
            // FNA3D_VERTEXELEMENTFORMAT_COLOR expects for a COLOR0 element.
            return static_cast<std::uint32_t>(color.getRProperty()) |
                   (static_cast<std::uint32_t>(color.getGProperty()) << 8) |
                   (static_cast<std::uint32_t>(color.getBProperty()) << 16) |
                   (static_cast<std::uint32_t>(color.getAProperty()) << 24);
        }
    }

    Fna3dSpriteBatchRenderer::Fna3dSpriteBatchRenderer(
        std::shared_ptr<Fna3dDeviceState> deviceState)
        : deviceState_(deviceState)
        , transform_(Matrix::getIdentityProperty())
    {
    }

    Fna3dSpriteBatchRenderer::~Fna3dSpriteBatchRenderer() = default;

    void Fna3dSpriteBatchRenderer::Begin()
    {
        (void) deviceState_->RequireRenderer("SpriteBatch.Begin");
        vertices_.clear();
        batchTexture_ = nullptr;
    }

    void Fna3dSpriteBatchRenderer::End()
    {
        (void) deviceState_->RequireRenderer("SpriteBatch.End");
        Flush();
    }

    void Fna3dSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (customEffect_ == effect) return;
        Flush();
        if (effect != nullptr)
        {
            GpuDrawParams params{};
            effect->FillGpuDrawParams(params);
            if (params.compiledEffectRuntime == nullptr)
            {
                throw std::runtime_error(
                    "FNA3D renderer: SpriteBatch custom effects must be compiled XNA Effect "
                    "Framework bytecode; source-pair ShaderEffect is not supported by FNA3D.");
            }
        }
        customEffect_ = effect;
    }

    void Fna3dSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        (void) deviceState_->RequireRenderer("SpriteBatch transform change");
        Flush();
        transform_ = m;
    }

    void Fna3dSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        (void) deviceState_->RequireRenderer("SpriteBatch sampler change");
        Flush();
        samplerFilter_ = textureFilter;
    }

    void Fna3dSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        (void) deviceState_->RequireRenderer("SpriteBatch sampler change");
        Flush();
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void Fna3dSpriteBatchRenderer::SetImmediateMode(bool immediate)
    {
        (void) deviceState_->RequireRenderer("SpriteBatch state change");
        immediate_ = immediate;
    }

    void Fna3dSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const Rectangle destination(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(),
                                    texture.GetHeight());
        const Rectangle source(0, 0, texture.GetWidth(), texture.GetHeight());
        Queue(texture, destination, source, Color::White, 0.0f, Vector2::Zero,
              SpriteEffects::None, 0.0f);
    }

    void Fna3dSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle, const Color& color)
    {
        Queue(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2::Zero,
              SpriteEffects::None, 0.0f);
    }

    void Fna3dSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle, const Color& color,
                                        float rotation, const Vector2& origin,
                                        SpriteEffects effects, float layerDepth)
    {
        Queue(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects,
              layerDepth);
    }

    void Fna3dSpriteBatchRenderer::Queue(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle, const Color& color,
                                         float rotation, const Vector2& origin,
                                         SpriteEffects effects, float layerDepth)
    {
        (void) deviceState_->RequireRenderer("SpriteBatch.Draw");
        const auto* sampled = dynamic_cast<const Fna3dSampledTexture*>(&texture);
        if (sampled == nullptr || sampled->GetFna3dDeviceStateEXT() != deviceState_ ||
            sampled->GetFna3dTextureEXT() == nullptr)
        {
            // A texture from another renderer cannot be sampled by this device. Dropping it
            // silently would render an invisible sprite that looks like a content bug.
            throw std::runtime_error(
                "FNA3D renderer: SpriteBatch received a texture that was not created by this "
                "graphics device or is no longer live.");
        }

        if (batchTexture_ != nullptr && batchTexture_ != &texture)
        {
            Flush();
        }
        batchTexture_ = &texture;

        const float textureWidth = static_cast<float>(texture.GetWidth());
        const float textureHeight = static_cast<float>(texture.GetHeight());
        if (textureWidth <= 0.0f || textureHeight <= 0.0f)
        {
            return;
        }

        float u0 = static_cast<float>(sourceRectangle.X) / textureWidth;
        float v0 = static_cast<float>(sourceRectangle.Y) / textureHeight;
        float u1 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / textureWidth;
        float v1 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / textureHeight;

        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0)
        {
            const float swap = u0;
            u0 = u1;
            u1 = swap;
        }
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0)
        {
            const float swap = v0;
            v0 = v1;
            v1 = swap;
        }

        // XNA scales the origin from source-rectangle texels into destination pixels, so a
        // stretched sprite rotates about the same visual point it would at natural size.
        const float scaleX = sourceRectangle.Width != 0
                                 ? static_cast<float>(destinationRectangle.Width) /
                                       static_cast<float>(sourceRectangle.Width)
                                 : 1.0f;
        const float scaleY = sourceRectangle.Height != 0
                                 ? static_cast<float>(destinationRectangle.Height) /
                                       static_cast<float>(sourceRectangle.Height)
                                 : 1.0f;
        const float originX = origin.X * scaleX;
        const float originY = origin.Y * scaleY;

        const float sin = std::sin(rotation);
        const float cos = std::cos(rotation);
        const float anchorX = static_cast<float>(destinationRectangle.X);
        const float anchorY = static_cast<float>(destinationRectangle.Y);
        const float width = static_cast<float>(destinationRectangle.Width);
        const float height = static_cast<float>(destinationRectangle.Height);

        const auto corner = [&](float localX, float localY, float& outX, float& outY) {
            const float dx = localX - originX;
            const float dy = localY - originY;
            outX = anchorX + dx * cos - dy * sin;
            outY = anchorY + dx * sin + dy * cos;
        };

        const std::uint32_t packed = PackColor(color);
        SpriteVertex quad[4]{};
        corner(0.0f, 0.0f, quad[0].x, quad[0].y);
        corner(width, 0.0f, quad[1].x, quad[1].y);
        corner(width, height, quad[2].x, quad[2].y);
        corner(0.0f, height, quad[3].x, quad[3].y);

        quad[0].u = u0; quad[0].v = v0;
        quad[1].u = u1; quad[1].v = v0;
        quad[2].u = u1; quad[2].v = v1;
        quad[3].u = u0; quad[3].v = v1;

        for (SpriteVertex& vertex : quad)
        {
            vertex.z = layerDepth;
            vertex.color = packed;
            vertices_.push_back(vertex);
        }

        if (immediate_)
        {
            Flush();
        }
    }

    void Fna3dSpriteBatchRenderer::Flush()
    {
        if (vertices_.empty() || batchTexture_ == nullptr)
        {
            vertices_.clear();
            return;
        }

        static_assert(sizeof(SpriteVertex) == kSpriteVertexStride,
                      "the sprite vertex must match the stride-24 VertexPositionColorTexture "
                      "layout the SpriteEffect vertex shader reads");

        Fna3dRenderer& renderer = deviceState_->RequireRenderer("SpriteBatch flush");
        const auto* sampled = dynamic_cast<const Fna3dSampledTexture*>(batchTexture_);
        if (sampled == nullptr || sampled->GetFna3dDeviceStateEXT() != deviceState_ ||
            sampled->GetFna3dTextureEXT() == nullptr)
        {
            throw std::runtime_error(
                "FNA3D renderer: SpriteBatch's queued texture is no longer live on this graphics "
                "device.");
        }

        const std::size_t maxVerticesPerDraw = kMaxQuadsPerDraw * 4;
        for (std::size_t first = 0; first < vertices_.size(); first += maxVerticesPerDraw)
        {
            const std::size_t remaining = vertices_.size() - first;
            const std::size_t vertexCount = std::min(remaining, maxVerticesPerDraw);
            renderer.DrawSpriteRunEXT(sampled->GetFna3dTextureEXT(), vertices_.data() + first,
                                      static_cast<int>(vertexCount), transform_, samplerFilter_,
                                      addressU_, addressV_, customEffect_);
        }
        vertices_.clear();
    }

    std::unique_ptr<ISpriteBatchRenderer> Fna3dRenderer::CreateSpriteBatch()
    {
        return std::make_unique<Fna3dSpriteBatchRenderer>(deviceState_);
    }

    void Fna3dRenderer::DrawSpriteRunEXT(FNA3D_Texture* texture, const void* vertices,
                                         int vertexCount, const Matrix& transform, int filter,
                                         int addressU, int addressV, Effect* customEffect)
    {
        if (texture == nullptr || vertices == nullptr || vertexCount < 4)
        {
            return;
        }
        if (vertexCount % 4 != 0 || vertexCount > static_cast<int>(kMaxQuadsPerDraw * 4))
        {
            throw std::runtime_error(
                "FNA3D renderer: a SpriteBatch draw must contain whole quads and fit the 16-bit "
                "index range.");
        }
        const int quadCount = vertexCount / 4;
        if (quadCount <= 0)
        {
            return;
        }

        if (spriteVertexBuffer_ == nullptr)
        {
            spriteVertexBuffer_ =
                std::make_unique<Fna3dVertexBufferRenderer>(deviceState_, vertexCount,
                                                            kSpriteVertexStride);
        }
        spriteVertexBuffer_->SetDataWithOptions(vertices, vertexCount, kSpriteVertexStride,
                                                SetDataOptions::Discard);

        if (spriteIndexBuffer_ == nullptr || quadCount > spriteIndexQuadCapacity_)
        {
            // Quad indices are a pure function of the quad count, so the buffer is rebuilt only
            // when a batch is larger than any seen so far, never per frame.
            spriteIndexQuadCapacity_ = quadCount > 256 ? quadCount : 256;
            std::vector<std::uint16_t> indices(
                static_cast<std::size_t>(spriteIndexQuadCapacity_) * 6);
            for (int quad = 0; quad < spriteIndexQuadCapacity_; ++quad)
            {
                const auto base = static_cast<std::uint16_t>(quad * 4);
                const std::size_t offset = static_cast<std::size_t>(quad) * 6;
                indices[offset + 0] = static_cast<std::uint16_t>(base + 0);
                indices[offset + 1] = static_cast<std::uint16_t>(base + 1);
                indices[offset + 2] = static_cast<std::uint16_t>(base + 2);
                indices[offset + 3] = static_cast<std::uint16_t>(base + 0);
                indices[offset + 4] = static_cast<std::uint16_t>(base + 2);
                indices[offset + 5] = static_cast<std::uint16_t>(base + 3);
            }
            spriteIndexBuffer_ = std::make_unique<Fna3dIndexBufferRenderer>(
                deviceState_, static_cast<int>(indices.size()), false);
            spriteIndexBuffer_->SetData16(indices.data(), static_cast<int>(indices.size()));
        }

        // XNA's SpriteBatch projection: pixel coordinates with the origin at the top-left, half a
        // pixel offset so texel centres land on pixel centres, and Z passed through as the layer
        // depth. Built explicitly rather than through Matrix::CreateOrthographicOffCenter so the
        // half-pixel term stays visible next to the sprite geometry it compensates for.
        int viewportWidth = 0;
        int viewportHeight = 0;
        // plans/plan_fx.md FX-100: a bound RenderTarget2D is what sprites rasterize into, so it -- not
        // the back buffer -- is what their pixel-space projection must be built from.
        // GetViewportSize() is the renderer-wide LOGICAL extent (input coordinate transforms read
        // the same method), and it always reported the back buffer. A batch drawn into a target
        // smaller than the window therefore projected its pixels into a fraction of clip space and
        // rasterized into a corner of the target -- for an 8x8 target against an 800x600 back
        // buffer, into nothing at all. EasyGL and SDL_GPU both size this from the bound target.
        if (!boundTargets_.empty() &&
            boundTargets_.front().binding.type == FNA3D_RENDERTARGET_TYPE_2D)
        {
            viewportWidth = boundTargets_.front().binding.twod.width;
            viewportHeight = boundTargets_.front().binding.twod.height;
        }
        else if (!boundTargets_.empty() &&
                 boundTargets_.front().binding.type == FNA3D_RENDERTARGET_TYPE_CUBE)
        {
            viewportWidth = boundTargets_.front().binding.cube.size;
            viewportHeight = boundTargets_.front().binding.cube.size;
        }
        else
        {
            GetViewportSize(viewportWidth, viewportHeight);
        }
        const float width = viewportWidth > 0 ? static_cast<float>(viewportWidth) : 1.0f;
        const float height = viewportHeight > 0 ? static_cast<float>(viewportHeight) : 1.0f;

        Matrix projection = Matrix::getIdentityProperty();
        projection.M11 = 2.0f / width;
        projection.M22 = -2.0f / height;
        projection.M33 = 1.0f;
        projection.M41 = -1.0f;
        projection.M42 = 1.0f;
        projection.M44 = 1.0f;

        Fna3dStockEffect& effect = effects_[static_cast<std::size_t>(StockEffectKind::Sprite)];
        effect.SetMatrix("MatrixTransform", Matrix::Multiply(transform, projection));
        effect.Apply(device_);

        // plans/plan_fx.md FX-092: built from the batch's own state and XNA's SamplerState defaults, NOT
        // copied from samplerStates_[0]. It used to start as that slot's current contents and
        // override only the filter and addressing, so `maxAnisotropy`, `maxMipLevel` and
        // `mipMapLevelOfDetailBias` were inherited from whatever wrote the slot last -- a compiled
        // Effect's own sampler_state block, for one, which clamped every later stock sprite draw to
        // the mip level that effect asked for. FNA has no such inheritance: PrepRenderState assigns
        // `GraphicsDevice.SamplerStates[0] = samplerState` wholesale before VerifySampler reads it,
        // so every field comes from the batch. The three defaults below are SamplerState's own, and
        // are what every XNA preset (LinearClamp, PointWrap, ...) carries.
        FNA3D_SamplerState sampler{};
        sampler.filter = ToFna3dTextureFilter(filter);
        sampler.addressU = ToFna3dTextureAddressMode(addressU);
        sampler.addressV = ToFna3dTextureAddressMode(addressV);
        sampler.addressW = sampler.addressU;
        sampler.maxAnisotropy = 4;
        sampler.maxMipLevel = 0;
        sampler.mipMapLevelOfDetailBias = 0.0f;
        if (customEffect == nullptr)
        {
            FNA3D_VerifySampler(device_, 0, texture, &sampler);
            samplerStates_[0] = sampler;
            boundPixelTextures_[0] = texture;
        }

        // Sprite quads are emitted in a fixed winding that a game's own RasterizerState.CullMode
        // has no reason to agree with, and XNA's 2D pipeline is not culled in practice. Culling is
        // therefore disabled for the sprite submission only and the tracked state is restored
        // immediately, so a following 3D draw sees exactly the state the game applied.
        FNA3D_RasterizerState spriteRasterizer = rasterizerState_;
        spriteRasterizer.cullMode = FNA3D_CULLMODE_NONE;
        FNA3D_ApplyRasterizerState(device_, &spriteRasterizer);

        const auto elements = ElementsForStride(kSpriteVertexStride);
        FNA3D_VertexBufferBinding binding{};
        binding.vertexBuffer = spriteVertexBuffer_->GetFna3dBufferEXT();
        binding.vertexOffset = 0;
        binding.instanceFrequency = 0;
        binding.vertexDeclaration.vertexStride = kSpriteVertexStride;
        binding.vertexDeclaration.elementCount = static_cast<int32_t>(elements.size());
        binding.vertexDeclaration.elements = const_cast<FNA3D_VertexElement*>(elements.data());
        // FNA3D resolves a vertex declaration against the currently applied vertex shader.
        // A custom effect can select a different shader for every pass, so its binding must be
        // applied after that pass (the ordinary draw path follows the same shader-then-binding
        // order). Applying it only once before EffectPass.Apply leaves the declaration associated
        // with the stock SpriteEffect and produces no vertices on OpenGL.
        const auto bindVertices = [&]() {
            FNA3D_ApplyVertexBufferBindings(device_, &binding, 1, 1, 0);
        };

        const auto draw = [&]() {
            FNA3D_DrawIndexedPrimitives(device_, FNA3D_PRIMITIVETYPE_TRIANGLELIST, 0, 0,
                                        vertexCount, 0, quadCount * 2,
                                        spriteIndexBuffer_->GetFna3dBufferEXT(),
                                        FNA3D_INDEXELEMENTSIZE_16BIT);
        };
        try
        {
            if (customEffect != nullptr)
            {
                auto* technique = customEffect->getCurrentTechniqueProperty();
                if (technique == nullptr || technique->getPassesProperty().getCountProperty() == 0)
                {
                    throw std::runtime_error(
                        "FNA3D renderer: SpriteBatch custom effect has no current pass.");
                }
                for (int passIndex = 0;
                     passIndex < technique->getPassesProperty().getCountProperty(); ++passIndex)
                {
                    technique->getPassesProperty()[passIndex].Apply();
                    // XNA deliberately sets SpriteBatch's texture after EffectPass.Apply so a
                    // texture parameter inside the effect cannot steal slot zero from the sprite.
                    FNA3D_VerifySampler(device_, 0, texture, &samplerStates_[0]);
                    boundPixelTextures_[0] = texture;
                    bindVertices();
                    draw();
                }
            }
            else
            {
                bindVertices();
                draw();
            }
        }
        catch (...)
        {
            FNA3D_ApplyRasterizerState(device_, &rasterizerState_);
            throw;
        }

        FNA3D_ApplyRasterizerState(device_, &rasterizerState_);
    }
}
