// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Content/Pipeline/BlockCompression.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /**
     * @brief Returns the block-compression encoder the texture processor calls.
     *
     * This is the adapter that lets a build-time-only encoder reach a processor that lives in
     * the runtime content module: `cna_content` declares the callable's shape and never links
     * an encoder, and `cna_content_pipeline` -- which only a content compiler links -- supplies
     * one. A registry that never receives it still builds every uncompressed texture.
     *
     * @param options Encoder effort and policy applied to every block.
     * @return A callable suitable for RegisterTexture2DContentPipeline().
     * @throws std::invalid_argument, from the returned callable, for a CNB format that is not
     *         one of the three S3TC formats.
     */
    [[nodiscard]] TextureBlockEncoder MakeBlockCompressionTextureEncoder(
        BlockCompressionOptions options = {});
}
