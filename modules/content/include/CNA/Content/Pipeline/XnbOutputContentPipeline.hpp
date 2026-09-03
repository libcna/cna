// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "CNA/Internal/Xnb/XnbFileOptions.hpp"

namespace CNA::Content::Pipeline
{
    /**
     * @brief Stable identifiers for the XNB root types CNA can emit
     *        (plans/plan_xnapipeline.md `XNAP-61`).
     *
     * The build manifest records an output's asset type as a number so a schema change invalidates
     * stale artifacts. CNB output uses CNB's own asset type IDs; XNB output uses this parallel,
     * CNA-owned numbering. The values are persisted in build manifests, so they are append-only:
     * never renumber an existing member.
     */
    enum class XnbOutputAssetId : std::uint32_t
    {
        /** @brief `Microsoft.Xna.Framework.Graphics.Texture2D`. */
        Texture2D = 1u,
        /** @brief `Microsoft.Xna.Framework.Graphics.Texture3D`. */
        Texture3D = 2u,
        /** @brief `Microsoft.Xna.Framework.Graphics.TextureCube`. */
        TextureCube = 3u,
        /** @brief `Microsoft.Xna.Framework.Graphics.SpriteFont`. */
        SpriteFont = 4u,
        /** @brief `Microsoft.Xna.Framework.Audio.SoundEffect`. */
        SoundEffect = 5u,
        /** @brief `Microsoft.Xna.Framework.Media.Song`. */
        Song = 6u,
        /** @brief `Microsoft.Xna.Framework.Media.Video`. */
        Video = 7u,
        /** @brief `Microsoft.Xna.Framework.Graphics.Model`. */
        Model = 8u,
        /** @brief `Microsoft.Xna.Framework.Curve`. */
        Curve = 9u,
        /** @brief `Microsoft.Xna.Framework.Graphics.Effect`, from already-compiled bytecode. */
        Effect = 10u,
    };

    /**
     * @brief Returns a canonical, stable digest of the container options a writer is bound to.
     *
     * Two builds that differ only in target platform, container version, graphics profile,
     * compression or reader-name spelling produce different bytes from identical inputs, so the
     * options belong in the writer's own build version: the incremental manifest then invalidates
     * an artifact built under other options without needing a new fingerprint domain.
     *
     * @param options The container options.
     * @return A short, deterministic, human-readable digest such as `"w5rn-x"`.
     */
    [[nodiscard]] std::string XnbOutputOptionsDigest(
        const CNA::Internal::Xnb::XnbFileOptions& options);

    /**
     * @brief Converts a canonical CNB texture into the XNB texture representation.
     *
     * The first representation whose format the selected container version can express is chosen,
     * in the order the producer recorded them. A texture with no expressible representation is
     * refused rather than silently re-encoded, because a silent format change is exactly the kind
     * of loss this pipeline must diagnose.
     *
     * @param texture Canonical CNB texture data.
     * @param kind The XNB texture shape to emit.
     * @param options Container options, which decide which formats are expressible.
     * @param diagnosticName Logical asset name used in the failure message.
     * @return Canonical XNB texture data ready for serialization.
     * @throws CNA::Internal::Xnb::XnbWriteException when no representation is expressible or the
     *         declared shape does not match @p kind.
     */
    [[nodiscard]] CNA::Internal::Xnb::XnbTextureData ConvertCnbTextureToXnb(
        const Cnb::CnbTextureData& texture, CNA::Internal::Xnb::XnbTextureKind kind,
        const CNA::Internal::Xnb::XnbFileOptions& options, const std::string& diagnosticName);

    /**
     * @brief Registers every XNB output writer, bound to one set of container options.
     *
     * Importers and processors are not registered here: they are format-neutral and are already
     * registered by RegisterBuiltInContentPipeline(). Only the writer axis differs per format, so
     * a build can select `.xnb` output for any source route the pipeline already supports.
     *
     * @param registry Mutable registry to configure before builds begin.
     * @param options Container options every registered writer emits.
     */
    void RegisterXnbOutputContentPipeline(ContentPipelineRegistry& registry,
                                          const CNA::Internal::Xnb::XnbFileOptions& options);
}
