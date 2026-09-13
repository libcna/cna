// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Content/Pipeline/ContentPipeline.hpp"

/**
 * @file
 * @brief The canonical `.x` and `.fbx` source routes (plans/plan_xnapipeline_parity.md
 *        `XNAPP-021`, Phases 15 and 16).
 *
 * The two model readers and the XNA `ModelProcessor` were implemented and measured against the
 * genuine runtime before this file existed, and none of them was registered: `XnaComponentNames`
 * mapped `XImporter` and `FbxImporter` onto `CNA.XImporter` and `CNA.FbxImporter`, which no
 * registry contained, so a `.contentproj` naming a model built nothing and said nothing. This is
 * the registration, and it is deliberately thin -- it owns no parser, no processing rule and no
 * writer, only the wiring that lets the components that do own them be scheduled by the one
 * coordinator.
 */
namespace CNA::Content::Pipeline
{
    /** @brief Stable in-memory type identity for an imported XNA scene graph. */
    inline constexpr const char* ImportedXnaNodeGraphType =
        "Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent";

    /**
     * @brief Registers the `.x` and `.fbx` source routes and the processors they reach.
     *
     * Five components: the two importers, the model processor whose output is the canonical
     * processed model both writers already take, and -- under XNA's own names, because XNA's
     * `ModelProcessor` and `MaterialProcessor` reach them by name -- the material processor and a
     * texture processor for the nested builds a model's materials start.
     *
     * @param registry Mutable registry to configure before builds begin.
     */
    void RegisterXnaModelSourceContentPipeline(ContentPipelineRegistry& registry);
}
