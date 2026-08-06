// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/Magnum/MagnumSpriteBatchBackend.hpp"

#include "CNA/Internal/Backends/Magnum/MagnumEffectBackend.hpp"
#include "CNA/Internal/Backends/Magnum/MagnumGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Magnum/MagnumRenderTargets.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <Corrade/Containers/ArrayView.h>
#include <Magnum/GL/Attribute.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CNA::Internal::Backends::Magnum
{
    namespace
    {
        using Kind = Mg::GL::DynamicAttribute::Kind;
        using Components = Mg::GL::DynamicAttribute::Components;
        using DataType = Mg::GL::DynamicAttribute::DataType;

        constexpr GLsizei kSpriteVertexStride = sizeof(MagnumSpriteBatchBackend::Vertex);
    }

    MagnumSpriteBatchBackend::MagnumSpriteBatchBackend(MagnumGraphicsBackend* graphicsBackend)
        : graphicsBackend_(graphicsBackend)
        , vertexBuffer_(std::make_unique<Mg::GL::Buffer>(Mg::GL::Buffer::TargetHint::Array))
        , indexBuffer_(std::make_unique<Mg::GL::Buffer>(Mg::GL::Buffer::TargetHint::ElementArray))
        , mesh_(std::make_unique<Mg::GL::Mesh>())
    {
        mesh_->setPrimitive(Mg::GL::MeshPrimitive::Triangles);
        mesh_->addVertexBuffer(*vertexBuffer_, 0, kSpriteVertexStride,
                               Mg::GL::DynamicAttribute{Kind::Generic, 0, Components::Two,
                                                        DataType::Float});
        mesh_->addVertexBuffer(*vertexBuffer_, 2 * sizeof(float), kSpriteVertexStride,
                               Mg::GL::DynamicAttribute{Kind::Generic, 1, Components::Two,
                                                        DataType::Float});
        mesh_->addVertexBuffer(*vertexBuffer_, 4 * sizeof(float), kSpriteVertexStride,
                               Mg::GL::DynamicAttribute{Kind::Generic, 2, Components::Four,
                                                        DataType::Float});
        mesh_->setIndexBuffer(*indexBuffer_, 0, Mg::GL::MeshIndexType::UnsignedShort);
    }

    void MagnumSpriteBatchBackend::Begin()
    {
        // Blend state is deliberately not touched here. SpriteBatch::Begin has already pushed the
        // caller's BlendState through GraphicsDevice, and forcing alpha blending on at this point
        // would both ignore a non-AlphaBlend request and leave that forced state behind for every
        // 3D draw issued after End().
        begun_ = true;
    }

    void MagnumSpriteBatchBackend::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void MagnumSpriteBatchBackend::SetTransformMatrix(const Matrix& m)
    {
        transform_ = m;
    }

    void MagnumSpriteBatchBackend::SetCustomEffect(Effect* effect)
    {
        if (customEffect_ == effect)
            return;
        FlushBatch();
        customEffect_ = effect;
    }

    void MagnumSpriteBatchBackend::SetSamplerFilter(int textureFilter)
    {
        sampler_.filter = textureFilter;
    }

    void MagnumSpriteBatchBackend::SetSamplerAddressMode(int addressU, int addressV)
    {
        sampler_.addressU = addressU;
        sampler_.addressV = addressV;
    }

    void MagnumSpriteBatchBackend::FlushBatch()
    {
        if (pendingVertices_.empty() || graphicsBackend_ == nullptr || currentTexture_ == nullptr)
        {
            pendingVertices_.clear();
            pendingIndices_.clear();
            currentTexture_ = nullptr;
            return;
        }

        // Bind the SAME program the effect itself owns rather than a second copy compiled from the
        // same source: a ShaderEffect's SetUniformXxx() calls write to its own program, and a
        // duplicate would never see them.
        MagnumProgram* program = nullptr;
        if (customEffect_ != nullptr)
        {
            if (auto* backend = dynamic_cast<MagnumEffectBackend*>(
                    customEffect_->GetEffectBackendPtr()))
            {
                if (backend->IsValid())
                    program = &backend->GetProgram();
            }
            customEffect_->Apply();
        }
        if (program == nullptr)
            program = graphicsBackend_->GetStockShaders().Sprite();
        if (program == nullptr)
        {
            pendingVertices_.clear();
            pendingIndices_.clear();
            currentTexture_ = nullptr;
            return;
        }

        int targetWidth = 0;
        int targetHeight = 0;
        const bool haveRenderTarget =
            graphicsBackend_->GetCurrentRenderTarget2DSize(targetWidth, targetHeight)
            && targetWidth > 0 && targetHeight > 0;

        int fullWidth = 0;
        int fullHeight = 0;
        if (haveRenderTarget)
        {
            fullWidth = targetWidth;
            fullHeight = targetHeight;
        }
        else
        {
            graphicsBackend_->GetPhysicalSize(fullWidth, fullHeight);
        }

        // XNA/FNA build the sprite projection from Viewport.Width/Height, so a custom sub-viewport
        // makes sprite coordinates viewport-local. A viewport that is a genuine sub-region of the
        // destination is therefore kept in place and projected against, while the ordinary
        // full-target case resets to the whole destination and projects against the logical size.
        const Mg::Range2Di viewport = graphicsBackend_->GetCurrentViewport();
        const Mg::Vector2i viewportSize = viewport.size();
        const bool customViewport =
            viewportSize.x() > 0 && viewportSize.y() > 0 && fullWidth > 0 && fullHeight > 0
            && (viewport.min().x() != 0 || viewport.min().y() != 0
                || viewportSize.x() != fullWidth || viewportSize.y() != fullHeight);

        int logicalWidth = 0;
        int logicalHeight = 0;
        if (customViewport)
        {
            logicalWidth = viewportSize.x();
            logicalHeight = viewportSize.y();
        }
        else if (haveRenderTarget)
        {
            graphicsBackend_->ApplyViewport(
                Mg::Range2Di{{}, Mg::Vector2i{targetWidth, targetHeight}});
            logicalWidth = targetWidth;
            logicalHeight = targetHeight;
        }
        else
        {
            if (fullWidth > 0 && fullHeight > 0)
            {
                graphicsBackend_->ApplyViewport(
                    Mg::Range2Di{{}, Mg::Vector2i{fullWidth, fullHeight}});
            }
            graphicsBackend_->GetLogicalSize(logicalWidth, logicalHeight);
        }
        if (logicalWidth <= 0 || logicalHeight <= 0)
        {
            logicalWidth = std::max(1, viewportSize.x());
            logicalHeight = std::max(1, viewportSize.y());
        }

        const Matrix ortho = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logicalWidth),
            static_cast<float>(logicalHeight), 0.0f,
            -1.0f, 1.0f);
        float projection[16];
        (transform_ * ortho).ToColumnMajor(projection);
        program->SetMatrix4(program->LocationOf("projection"), projection);
        program->SetInt(program->LocationOf("texture1"), 0);

        graphicsBackend_->ApplySamplerState(0, sampler_.filter, sampler_.addressU,
                                            sampler_.addressV, sampler_.maxAnisotropy);
        graphicsBackend_->BindTextureToSlot(0, currentTexture_);

        vertexBuffer_->setData(
            Corrade::Containers::ArrayView<const void>{
                pendingVertices_.data(), pendingVertices_.size() * sizeof(Vertex)},
            Mg::GL::BufferUsage::DynamicDraw);
        indexBuffer_->setData(
            Corrade::Containers::ArrayView<const void>{
                pendingIndices_.data(), pendingIndices_.size() * sizeof(uint16_t)},
            Mg::GL::BufferUsage::DynamicDraw);

        mesh_->setCount(static_cast<Mg::Int>(pendingIndices_.size()));
        program->draw(*mesh_);

        pendingVertices_.clear();
        pendingIndices_.clear();
        currentTexture_ = nullptr;
    }

    void MagnumSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const int width = texture.GetWidth();
        const int height = texture.GetHeight();
        Draw(texture,
             Rectangle(static_cast<int>(x), static_cast<int>(y), width, height),
             Rectangle(0, 0, width, height),
             Microsoft::Xna::Framework::Color::White);
    }

    void MagnumSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0),
             SpriteEffects::None, 0.0f);
    }

    void MagnumSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color,
                                        float rotation,
                                        const Vector2& origin,
                                        SpriteEffects effects,
                                        float layerDepth)
    {
        if (!begun_)
            throw std::runtime_error("Draw called before Begin()");

        // Sprites are emitted in submission order, so the sort depth carries no ordering here.
        (void)layerDepth;

        if (currentTexture_ != &texture)
        {
            if (currentTexture_ != nullptr)
                FlushBatch();
            currentTexture_ = &texture;
            currentTextureBottomUp_ = SampledRowOrderIsBottomUp(&texture);
        }

        const float textureWidth = static_cast<float>(texture.GetWidth());
        const float textureHeight = static_cast<float>(texture.GetHeight());

        // No clamp into [0,1]: a source rectangle that runs past the texture deliberately produces
        // out-of-range UVs so the bound TextureAddressMode governs edge sampling, which is the
        // classic scrolling/tiling background technique.
        float u1 = static_cast<float>(sourceRectangle.X) / textureWidth;
        float v1 = static_cast<float>(sourceRectangle.Y) / textureHeight;
        float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / textureWidth;
        float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / textureHeight;

        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally))
            std::swap(u1, u2);
        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically))
            std::swap(v1, v2);

        // A render target's texel memory is bottom-up, so each V is mapped to its mirror. This is
        // done on the sprite's own generated quad rather than in the sprite shader because
        // SpriteBatch::Begin may substitute an arbitrary user ShaderEffect whose GLSL this backend
        // does not own -- the vertex data is the only place both programs read from. It composes
        // with SpriteEffects rather than cancelling it, since the mapping is per-component.
        if (currentTextureBottomUp_)
        {
            v1 = 1.0f - v1;
            v2 = 1.0f - v2;
        }

        const float r = static_cast<float>(color.getRProperty()) / 255.0f;
        const float g = static_cast<float>(color.getGProperty()) / 255.0f;
        const float b = static_cast<float>(color.getBProperty()) / 255.0f;
        const float a = static_cast<float>(color.getAProperty()) / 255.0f;

        const float destinationX = static_cast<float>(destinationRectangle.X);
        const float destinationY = static_cast<float>(destinationRectangle.Y);
        const float sourceWidth = static_cast<float>(sourceRectangle.Width);
        const float sourceHeight = static_cast<float>(sourceRectangle.Height);
        const float scaleX = static_cast<float>(destinationRectangle.Width) / sourceWidth;
        const float scaleY = static_cast<float>(destinationRectangle.Height) / sourceHeight;

        const float cornerX[4] = {
            (0.0f - origin.X) * scaleX, (sourceWidth - origin.X) * scaleX,
            (sourceWidth - origin.X) * scaleX, (0.0f - origin.X) * scaleX};
        const float cornerY[4] = {
            (0.0f - origin.Y) * scaleY, (0.0f - origin.Y) * scaleY,
            (sourceHeight - origin.Y) * scaleY, (sourceHeight - origin.Y) * scaleY};

        const float cosine = std::cos(rotation);
        const float sine = std::sin(rotation);

        const float cornerU[4] = {u1, u2, u2, u1};
        const float cornerV[4] = {v1, v1, v2, v2};

        const auto base = static_cast<uint16_t>(pendingVertices_.size());
        for (int corner = 0; corner < 4; ++corner)
        {
            pendingVertices_.push_back(Vertex{
                destinationX + cornerX[corner] * cosine - cornerY[corner] * sine,
                destinationY + cornerX[corner] * sine + cornerY[corner] * cosine,
                cornerU[corner], cornerV[corner], r, g, b, a});
        }

        pendingIndices_.push_back(static_cast<uint16_t>(base + 0));
        pendingIndices_.push_back(static_cast<uint16_t>(base + 1));
        pendingIndices_.push_back(static_cast<uint16_t>(base + 2));
        pendingIndices_.push_back(static_cast<uint16_t>(base + 2));
        pendingIndices_.push_back(static_cast<uint16_t>(base + 3));
        pendingIndices_.push_back(static_cast<uint16_t>(base + 0));
    }
}
