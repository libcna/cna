// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ColorMatrixEffect.hpp"

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics
{
    ColorMatrixEffect::ColorMatrixEffect(GraphicsDevice& device)
        : Effect(device)
    {
    }

    ColorMatrixEffect::ColorMatrixEffect(const ColorMatrixEffect& cloneSource)
        : Effect(*cloneSource.device_)
        , matrix_(cloneSource.matrix_)
        , offset_(cloneSource.offset_)
    {
    }

    Effect* ColorMatrixEffect::Clone()
    {
        return new ColorMatrixEffect(*this);
    }

    const std::string& ColorMatrixEffect::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.ColorMatrixEffect";
        return name;
    }

    void ColorMatrixEffect::SetColorMatrix(const std::array<float, 16>& rowMajor)
    {
        for (float value : rowMajor)
        {
            if (!std::isfinite(value))
                throw std::invalid_argument("ColorMatrixEffect::SetColorMatrix: every value must be finite");
        }
        matrix_ = rowMajor;
    }

    const std::array<float, 16>& ColorMatrixEffect::GetColorMatrix() const
    {
        return matrix_;
    }

    void ColorMatrixEffect::SetColorOffset(const Vector4& offset)
    {
        if (!std::isfinite(offset.X) || !std::isfinite(offset.Y) ||
            !std::isfinite(offset.Z) || !std::isfinite(offset.W))
            throw std::invalid_argument("ColorMatrixEffect::SetColorOffset: every value must be finite");
        offset_ = offset;
    }

    Vector4 ColorMatrixEffect::GetColorOffset() const
    {
        return offset_;
    }

    void ColorMatrixEffect::SetGrayscale()
    {
        // Rec. 709 luma. Alpha is intentionally independent, matching the other RGB-only visual
        // operations in this backend and allowing normal alpha blending after the transform.
        matrix_ = {
            0.2126f, 0.7152f, 0.0722f, 0.0f,
            0.2126f, 0.7152f, 0.0722f, 0.0f,
            0.2126f, 0.7152f, 0.0722f, 0.0f,
            0.0f,    0.0f,    0.0f,    1.0f,
        };
        offset_ = Vector4::Zero;
    }

    void ColorMatrixEffect::Reset()
    {
        matrix_ = {
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,
        };
        offset_ = Vector4::Zero;
    }

    void ColorMatrixEffect::FillSpriteDrawParams(CNA::Internal::Backends::GpuDrawParams& params) const
    {
        params.cpu2DColorMatrixEnabled = true;
        std::copy(matrix_.begin(), matrix_.end(), std::begin(params.cpu2DColorMatrix));
        params.cpu2DColorOffset[0] = offset_.X;
        params.cpu2DColorOffset[1] = offset_.Y;
        params.cpu2DColorOffset[2] = offset_.Z;
        params.cpu2DColorOffset[3] = offset_.W;
    }
}
