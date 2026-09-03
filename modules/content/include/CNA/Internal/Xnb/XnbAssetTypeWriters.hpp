// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "CNA/Internal/Xnb/XnbTypeWriter.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief A `Texture2D` asset ready for serialization
     *        (plans/plan_xnapipeline.md `XNAP-23`).
     *
     * `XnbTextureData` already carries a @ref XnbTextureKind discriminant, but a registry keyed by
     * C++ type needs three distinct types to select between `Texture2DReader`, `Texture3DReader`
     * and `TextureCubeReader`. These three wrappers make the choice explicit at the call site
     * instead of hiding it in a runtime branch that could silently emit the wrong reader.
     */
    struct XnbTexture2DContent
    {
        /** @brief Canonical texture fields and level bytes; `kind` must be `Texture2D`. */
        XnbTextureData texture;

        /** @brief Compares the complete texture payload. */
        bool operator==(const XnbTexture2DContent& other) const = default;
    };

    /** @brief A `Texture3D` asset ready for serialization. */
    struct XnbTexture3DContent
    {
        /** @brief Canonical texture fields and level bytes; `kind` must be `Texture3D`. */
        XnbTextureData texture;

        /** @brief Compares the complete texture payload. */
        bool operator==(const XnbTexture3DContent& other) const = default;
    };

    /** @brief A `TextureCube` asset ready for serialization. */
    struct XnbTextureCubeContent
    {
        /** @brief Canonical texture fields and level bytes; `kind` must be `TextureCube`. */
        XnbTextureData texture;

        /** @brief Compares the complete texture payload. */
        bool operator==(const XnbTextureCubeContent& other) const = default;
    };

    /**
     * @brief An already-compiled `Effect` ready for serialization (plans/plan_xnapipeline.md
     *        `XNAP-29`).
     *
     * The writer serializes bytecode it is given; it never compiles shader source and never
     * embeds source text pretending to be bytecode. Producing bytecode a genuine XNA 4.0 runtime
     * accepts is a separate, unsolved problem tracked as `XNAP-84`.
     */
    struct XnbCompiledEffectContent
    {
        /** @brief Complete compiled effect bytecode exactly as the target runtime expects it. */
        std::vector<std::uint8_t> bytecode;

        /** @brief Compares the complete bytecode. */
        bool operator==(const XnbCompiledEffectContent& other) const = default;
    };

    /**
     * @brief Returns the reader identity CNA writes for `Texture2D`.
     * @return The identity, evidenced by a committed fixture's own type-reader table.
     */
    [[nodiscard]] XnbReaderIdentity XnbTexture2DReaderIdentity();

    /**
     * @brief Returns the reader identity CNA writes for `Texture3D`.
     * @return The identity, derived from the rule the committed fixtures establish.
     */
    [[nodiscard]] XnbReaderIdentity XnbTexture3DReaderIdentity();

    /**
     * @brief Returns the reader identity CNA writes for `TextureCube`.
     * @return The identity, evidenced by a committed fixture's own type-reader table.
     */
    [[nodiscard]] XnbReaderIdentity XnbTextureCubeReaderIdentity();

    /**
     * @brief Registers every built-in asset writer: textures, `SpriteFont`, `SoundEffect`, `Song`,
     *        `Video`, vertex/index resources, the stock effects, compiled effects and `Model`.
     *
     * @param registry Mutable registry to configure.
     */
    void RegisterBuiltInAssetXnbWriters(XnbTypeWriterRegistry& registry);
}
