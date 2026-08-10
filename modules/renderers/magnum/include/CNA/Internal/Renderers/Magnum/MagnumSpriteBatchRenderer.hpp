// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "MagnumStateMapping.hpp"

#include <Magnum/GL/Buffer.h>
#include <Magnum/GL/Mesh.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::Magnum
{
    class MagnumRenderer;

    /** @brief Magnum-backed SpriteBatch: quads accumulated per texture and flushed in one draw. */
    class MagnumSpriteBatchRenderer : public ISpriteBatchRenderer
    {
    public:
        /** @brief One sprite corner: position, texture coordinate and tint. */
        struct Vertex
        {
            /** @brief Position X in the batch's own logical coordinate space. */
            float x;
            /** @brief Position Y in the batch's own logical coordinate space. */
            float y;
            /** @brief Texture coordinate U. */
            float u;
            /** @brief Texture coordinate V. */
            float v;
            /** @brief Tint red, 0..1. */
            float r;
            /** @brief Tint green, 0..1. */
            float g;
            /** @brief Tint blue, 0..1. */
            float b;
            /** @brief Tint alpha, 0..1. */
            float a;
        };

        /**
         * @brief Creates the batch's own buffers and mesh.
         *
         * @param graphicsRenderer Renderer that owns this batch, consulted for the destination
         *                        extent the sprite projection is built from.
         */
        explicit MagnumSpriteBatchRenderer(MagnumRenderer* graphicsRenderer);
        ~MagnumSpriteBatchRenderer() override = default;

        /** @brief Opens the batch; sprites drawn before this are rejected. */
        void Begin() override;
        /** @brief Flushes anything still pending and closes the batch. */
        void End() override;
        /**
         * @brief Sets the transform applied before the sprite projection.
         *
         * @param m Transform matrix from `SpriteBatch.Begin`.
         */
        void SetTransformMatrix(const Matrix& m) override;
        /**
         * @brief Selects a custom effect whose own program replaces the built-in sprite program.
         *
         * @param effect Effect from `SpriteBatch.Begin`, or nullptr for the built-in program.
         */
        void SetCustomEffect(Effect* effect) override;
        /**
         * @brief Records the sampler filter subsequent flushes apply to the source texture.
         *
         * @param textureFilter Raw `TextureFilter` ordinal.
         */
        void SetSamplerFilter(int textureFilter) override;
        /**
         * @brief Records the addressing modes subsequent flushes apply to the source texture.
         *
         * @param addressU Raw `TextureAddressMode` ordinal for U.
         * @param addressV Raw `TextureAddressMode` ordinal for V.
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;
        /**
         * @brief Queues a whole texture at a position, untinted.
         *
         * @param texture Source texture.
         * @param x       Destination left edge.
         * @param y       Destination top edge.
         */
        void Draw(const ITextureRenderer& texture, float x, float y) override;
        /**
         * @brief Queues a source rectangle into a destination rectangle with a tint.
         *
         * @param texture              Source texture.
         * @param destinationRectangle Destination rectangle in logical coordinates.
         * @param sourceRectangle      Source rectangle in texels.
         * @param color                Tint.
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        /**
         * @brief Queues a rotated, flipped, origin-relative sprite.
         *
         * @param texture              Source texture.
         * @param destinationRectangle Destination rectangle in logical coordinates.
         * @param sourceRectangle      Source rectangle in texels.
         * @param color                Tint.
         * @param rotation             Rotation in radians about @p origin.
         * @param origin               Rotation origin, in source-rectangle texels.
         * @param effects              Horizontal/vertical flip flags.
         * @param layerDepth           Sort depth; accepted for API parity and not used by this
         *                             renderer, whose batches are drawn in submission order.
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

    private:
        void FlushBatch();

        MagnumRenderer* graphicsRenderer_ = nullptr;
        std::unique_ptr<Mg::GL::Buffer> vertexBuffer_;
        std::unique_ptr<Mg::GL::Buffer> indexBuffer_;
        std::unique_ptr<Mg::GL::Mesh> mesh_;
        bool begun_ = false;

        std::vector<Vertex> pendingVertices_;
        std::vector<uint16_t> pendingIndices_;
        const ITextureRenderer* currentTexture_ = nullptr;
        /// Resolved once per bound source: a batch is one texture by construction, so whether that
        /// source stores its rows bottom-up is a binding-time decision, not a per-sprite one.
        bool currentTextureBottomUp_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        Effect* customEffect_ = nullptr;
        MagnumSamplerState sampler_{};
    };
}
