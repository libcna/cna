// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureFormat.hpp"
#include "CNA/Content/Xnb/XnbTypeWriter.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace CNA::Content::Xnb
{
    /**
     * @brief Highest `SurfaceFormat` enumerator value an `.xnb` file may name
     *        (plans/plan_xnapipeline.md `XNAP-010`).
     *
     * The XNA 4.0 numbering runs `0 = Color` through `19 = HdrBlendable`. CNA's own
     * `SurfaceFormat` continues past that with renderer extensions (`ColorBgraEXT`, `Bc7EXT`, …)
     * that no XNA reader knows, so writing one would produce a file only CNA could interpret.
     */
    inline constexpr int XnbMaxSurfaceFormatValue = 19;

    /**
     * @brief Returns the wire value of an XNA 4.0 surface format.
     *
     * @param format The runtime surface format.
     * @return Its XNA 4.0 enumerator value.
     * @throws XnbWriteException when @p format is a CNA extension with no XNA 4.0 identity.
     */
    [[nodiscard]] std::int32_t XnbSurfaceFormatValue(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format);

    /**
     * @brief Maps a canonical CNB texture format onto its XNA 4.0 surface format.
     *
     * Every input produces either an XNA 4.0 value or a named refusal: a CNB-only format
     * (`Bgra8`, `Rgba8Srgb`, `R8`, `R16`, `Bc3Srgb`, `Bc7`, `Bc7Srgb`) has no XNA 4.0 counterpart
     * and is refused rather than silently approximated by a format with different semantics.
     *
     * @param format The canonical CNB storage format.
     * @return The equivalent XNA 4.0 surface format.
     * @throws XnbWriteException when @p format has no XNA 4.0 counterpart.
     */
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::SurfaceFormat XnbSurfaceFormatFor(
        Cnb::CnbTextureFormat format);

    /**
     * @brief A canonical texture tagged with the XNA texture shape it is written as.
     *
     * `Cnb::CnbTextureData` describes all three shapes with the same fields, and the shape decides
     * both the serialized type name and the payload layout, so it is carried explicitly rather
     * than guessed from `faceCount`/`depth`.
     *
     * A CNB texture may hold several representations of the same image; `.xnb` has room for
     * exactly one, so the writer is told which.
     */
    struct XnbTextureAsset
    {
        /** @brief The XNA texture type this asset is written as. */
        enum class Shape
        {
            /** @brief `Texture2D`: one face, depth 1. */
            Texture2D,
            /** @brief `Texture3D`: one face, depth at least 1. */
            Texture3D,
            /** @brief `TextureCube`: six square faces, depth 1. */
            TextureCube,
        };

        /** @brief The XNA texture type. */
        Shape shape = Shape::Texture2D;

        /** @brief The canonical texture; exactly one of its representations is written. */
        Cnb::CnbTextureData data;

        /** @brief Index of the representation to write. */
        std::size_t representation = 0u;
    };

    /** @brief A canonical sprite font written as an XNA `SpriteFont`. */
    struct XnbSpriteFontAsset
    {
        /** @brief The canonical font, atlas included. */
        Cnb::CnbSpriteFontData data;

        /** @brief Index of the atlas representation to write. */
        std::size_t representation = 0u;
    };

    /**
     * @brief Returns the serialized type name of the concrete XNA texture type for a shape.
     *
     * One C++ type covers all three shapes, so a texture is always addressed by name rather than
     * through `XnbTypeKey<T>`; this function is where that name comes from.
     *
     * @param shape The XNA texture type.
     * @return `Microsoft.Xna.Framework.Graphics.Texture2D`, `…Texture3D` or `…TextureCube`.
     */
    [[nodiscard]] std::string XnbTextureTypeName(XnbTextureAsset::Shape shape);

    /**
     * @brief Registers the asset-level `.xnb` type writers.
     *
     * Registers `Texture2D`, `Texture3D`, `TextureCube` (`XNAP-011`), `SpriteFont` (`XNAP-014`),
     * `SoundEffect` (`XNAP-016`), `Song` and `Video` (`XNAP-017`), plus the closed generic
     * collection writers `SpriteFont`'s payload needs.
     *
     * Requires `RegisterBuiltInXnbTypeWriters()` to have run on the same registry first, because
     * these writers dispatch to the primitive and math writers it installs.
     *
     * @param registry Registry to configure before it is frozen.
     * @throws XnbWriteException when a prerequisite writer is missing or a type is already
     *         registered.
     */
    void RegisterXnbAssetTypeWriters(XnbTypeWriterRegistry& registry);

    /** @brief `Microsoft.Xna.Framework.Graphics.SpriteFont`. */
    template <> struct XnbTypeKey<XnbSpriteFontAsset>
    { static std::string Name() { return "Microsoft.Xna.Framework.Graphics.SpriteFont"; } };

    /** @brief `Microsoft.Xna.Framework.Audio.SoundEffect`. */
    template <> struct XnbTypeKey<Cnb::CnbSoundEffectData>
    { static std::string Name() { return "Microsoft.Xna.Framework.Audio.SoundEffect"; } };

    /** @brief `Microsoft.Xna.Framework.Media.Song`. */
    template <> struct XnbTypeKey<Cnb::CnbSongData>
    { static std::string Name() { return "Microsoft.Xna.Framework.Media.Song"; } };

    /** @brief `Microsoft.Xna.Framework.Media.Video`. */
    template <> struct XnbTypeKey<Cnb::CnbVideoData>
    { static std::string Name() { return "Microsoft.Xna.Framework.Media.Video"; } };

    /** @brief `Microsoft.Xna.Framework.Graphics.VertexDeclaration`. */
    template <> struct XnbTypeKey<CNA::Internal::Xnb::XnbVertexDeclarationData>
    { static std::string Name() { return "Microsoft.Xna.Framework.Graphics.VertexDeclaration"; } };

    /** @brief `Microsoft.Xna.Framework.Graphics.VertexBuffer`. */
    template <> struct XnbTypeKey<CNA::Internal::Xnb::XnbVertexBufferData>
    { static std::string Name() { return "Microsoft.Xna.Framework.Graphics.VertexBuffer"; } };

    /** @brief `Microsoft.Xna.Framework.Graphics.IndexBuffer`. */
    template <> struct XnbTypeKey<CNA::Internal::Xnb::XnbIndexBufferData>
    { static std::string Name() { return "Microsoft.Xna.Framework.Graphics.IndexBuffer"; } };

    /** @brief `Microsoft.Xna.Framework.Graphics.BasicEffect`. */
    template <> struct XnbTypeKey<CNA::Internal::Xnb::XnbBasicEffectData>
    { static std::string Name() { return "Microsoft.Xna.Framework.Graphics.BasicEffect"; } };

    /** @brief `Microsoft.Xna.Framework.Graphics.AlphaTestEffect`. */
    template <> struct XnbTypeKey<CNA::Internal::Xnb::XnbAlphaTestEffectData>
    { static std::string Name() { return "Microsoft.Xna.Framework.Graphics.AlphaTestEffect"; } };

    /** @brief `Microsoft.Xna.Framework.Graphics.DualTextureEffect`. */
    template <> struct XnbTypeKey<CNA::Internal::Xnb::XnbDualTextureEffectData>
    { static std::string Name() { return "Microsoft.Xna.Framework.Graphics.DualTextureEffect"; } };

    /** @brief `Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect`. */
    template <> struct XnbTypeKey<CNA::Internal::Xnb::XnbEnvironmentMapEffectData>
    { static std::string Name() { return "Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect"; } };

    /** @brief `Microsoft.Xna.Framework.Graphics.SkinnedEffect`. */
    template <> struct XnbTypeKey<CNA::Internal::Xnb::XnbSkinnedEffectData>
    { static std::string Name() { return "Microsoft.Xna.Framework.Graphics.SkinnedEffect"; } };

    /** @brief `Microsoft.Xna.Framework.Graphics.Model`. */
    template <> struct XnbTypeKey<CNA::Internal::Xnb::XnbModelData>
    { static std::string Name() { return "Microsoft.Xna.Framework.Graphics.Model"; } };

    /**
     * @brief A compiled `Effect`'s opaque bytecode payload.
     *
     * **Serializing an `Effect` is not compiling one.** The payload is XNA D3D9 Effect Framework
     * bytecode, which CNA does not produce; this writer stores bytes a caller already has, from a
     * pre-compiled effect. See `docs/xna-content-pipeline.md` for that boundary
     * (plans/plan_xnapipeline.md `XNAP-021`).
     */
    struct XnbCompiledEffect
    {
        /** @brief Complete compiled effect bytecode, exactly as the runtime will receive it. */
        std::vector<std::uint8_t> bytecode;
    };

    /** @brief `Microsoft.Xna.Framework.Graphics.Effect`. */
    template <> struct XnbTypeKey<XnbCompiledEffect>
    { static std::string Name() { return "Microsoft.Xna.Framework.Graphics.Effect"; } };

    /**
     * @brief Registers the `Model` graph writers: buffers, stock effects, `Effect` and `Model`.
     *
     * Separate from RegisterXnbAssetTypeWriters() because a producer that only writes textures,
     * fonts and sounds has no use for them, and because the model graph is the one part of the
     * format that uses shared resources.
     *
     * Requires RegisterBuiltInXnbTypeWriters() to have run on the same registry first.
     *
     * @param registry Registry to configure before it is frozen.
     * @throws XnbWriteException when a prerequisite writer is missing or a type is registered twice.
     */
    void RegisterXnbModelTypeWriters(XnbTypeWriterRegistry& registry);
}
