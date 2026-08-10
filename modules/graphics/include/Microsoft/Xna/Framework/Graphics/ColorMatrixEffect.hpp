// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace CNA::Internal::Renderers { struct GpuDrawParams; }

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief A fixed CPU colour transform for SpriteBatch on the GDI and SOFTWARE renderers.
     *
     * This is a CNA extension, not an XNA shader effect. It deliberately accepts only a fixed
     * RGBA colour matrix and offset, so neither renderer needs to parse or silently ignore shader
     * source. Pass it to `SpriteBatch::Begin(..., &effect)`; it transforms each sprite's sampled
     * and tinted source colour before ordinary `BlendState` processing. It is unsupported by GPU
     * renderers unless they explicitly add the same contract.
     *
     * The matrix is row-major: `out[row] = dot(matrix[row], inRGBA) + offset[row]`. Every output
     * component is clamped to [0,1]. The default is the identity transform.
     */
    NOXNA class ColorMatrixEffect final : public Effect
    {
    public:
        /** @brief Constructs an identity colour transform for @p device. */
        explicit ColorMatrixEffect(GraphicsDevice& device);

        /** @brief Creates an independent effect with the same matrix and offset. */
        [[nodiscard]] Effect* Clone() override;

        /** @brief Returns CNA's fully qualified type name for this extension. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Replaces the row-major 4×4 RGBA transform matrix; all values must be finite. */
        NOXNA void SetColorMatrix(const std::array<float, 16>& rowMajor);
        /** @brief Returns the current row-major 4×4 RGBA transform matrix. */
        NOXNA [[nodiscard]] const std::array<float, 16>& GetColorMatrix() const;

        /** @brief Replaces the finite RGBA value added after the matrix multiplication. */
        NOXNA void SetColorOffset(const Vector4& offset);
        /** @brief Returns the current post-matrix RGBA offset. */
        NOXNA [[nodiscard]] Vector4 GetColorOffset() const;

        /** @brief Sets Rec. 709 grayscale RGB rows and preserves alpha unchanged. */
        NOXNA void SetGrayscale();
        /** @brief Restores the identity matrix and zero offset. */
        NOXNA void Reset();

        /**
         * @brief Copies this fixed effect's state into a CPU SpriteBatch draw description.
         *
         * Renderer-private routing point used by the shared GDI/SOFTWARE rasterizer; normal
         * `Effect::FillGpuDrawParams()` is intentionally not overridden, so this extension cannot
         * accidentally alter a 3D draw.
         */
        NOXNA void FillSpriteDrawParams(CNA::Internal::Renderers::GpuDrawParams& params) const;

    protected:
        /** @brief No GPU program exists for this fixed CPU effect. */
        void OnApply() override {}

    private:
        explicit ColorMatrixEffect(const ColorMatrixEffect& cloneSource);

        std::array<float, 16> matrix_ = {
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        Vector4 offset_ = Vector4::Zero;
    };
}
