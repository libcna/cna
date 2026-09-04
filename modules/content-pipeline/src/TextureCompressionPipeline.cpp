// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-53: the seam between the runtime-side texture processor and the
// build-time-only block encoder.

#include "CNA/Content/Pipeline/TextureCompressionPipeline.hpp"

#include <stdexcept>

#include "CNA/Content/Cnb/CnbTextureFormat.hpp"

namespace CNA::Content::Pipeline
{
    TextureBlockEncoder MakeBlockCompressionTextureEncoder(BlockCompressionOptions options)
    {
        return [options](const Cnb::CnbTextureFormat format, std::span<const std::uint8_t> rgba,
                         const std::uint32_t width, const std::uint32_t height)
        {
            BlockCompressionFormat target = BlockCompressionFormat::Bc1;
            switch (format)
            {
            case Cnb::CnbTextureFormat::Bc1: target = BlockCompressionFormat::Bc1; break;
            case Cnb::CnbTextureFormat::Bc2: target = BlockCompressionFormat::Bc2; break;
            case Cnb::CnbTextureFormat::Bc3: target = BlockCompressionFormat::Bc3; break;
            default:
                throw std::invalid_argument(
                    "the CNA block encoder produces BC1, BC2 and BC3; " +
                    Cnb::CnbTextureFormatToString(format) + " was requested.");
            }
            return EncodeBlockCompressedImage(target, rgba, width, height, options);
        };
    }
}
