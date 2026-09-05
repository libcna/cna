// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ModelContent.hpp"

namespace CNA::Content::Pipeline
{
    /**
     * @brief Converts the XNA-shaped model graph into the canonical XNB model, which is the one
     *        form the writer takes.
     *
     * The façade owns no writer of its own: a `ModelContent` reaches an `.xnb` by becoming this
     * structure and going through `CNA::Internal::Xnb`, exactly as every other content type does.
     *
     * @param model The processed model.
     * @return The canonical model, with one shared resource per distinct vertex buffer, index
     *         buffer and material.
     * @throws Microsoft::Xna::Framework::Content::Pipeline::PipelineException when a part names no
     *         vertex buffer, or a material is of a type no stock effect covers.
     */
    [[nodiscard]] CNA::Internal::Xnb::XnbModelData ToCanonicalModel(
        const Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelContent& model);
}
