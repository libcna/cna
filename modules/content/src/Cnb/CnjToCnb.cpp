// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnjToCnb.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelFromCnj.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Internal/CnjCanonicalRead.hpp"
#include "CNA/Internal/CnjSourceFile.hpp"
#include "CNA/Internal/Json.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Internal/CnjEnvelope.hpp"
#include "CNA/Internal/Json.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"

using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;

namespace CNA::Content::Cnb
{
    namespace
    {
        std::string ReadWholeTextFile(const std::string& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                throw ContentLoadException("cnj-to-cnb: cannot open '" + path + "'.");
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            return ss.str();
        }

        /// Loads through CNA's own .cnj readers by handing ContentManager the literal file name.
        /// Naming the file rather than the logical asset matters: a bare logical name would send
        /// Load<T>() through its own .xnb/.cnb tiers first, so a previously compiled sibling .cnb
        /// would be recompiled from itself instead of from the .cnj the caller asked for.
        template <typename T>
        T LoadThroughCnjReader(const std::string& cnjPath, const std::string& contentRoot)
        {
            namespace fs = std::filesystem;
            ContentManager cm(nullptr, contentRoot);
            const std::string literalName =
                fs::path(cnjPath).lexically_normal().filename().string();
            const fs::path parent = fs::path(cnjPath).lexically_normal().parent_path();
            const fs::path rootPath =
                (contentRoot.empty() ? fs::path(".") : fs::path(contentRoot)).lexically_normal();
            const fs::path relative = parent.lexically_relative(rootPath);
            const std::string name = (relative.empty() || relative == fs::path("."))
                                          ? literalName
                                          : (relative / literalName).generic_string();
            return cm.Load<T>(name);
        }
    }

    namespace
    {
        using CNA::Internal::JsonValue;

        /// Resolves a file a .cnj names, through the SAME containment rule the runtime readers
        /// use. A compiler that resolved paths more permissively than the runtime would be the
        /// soft way into a path the runtime refuses, so this shares the helper rather than doing
        /// its own join.
        std::string ResolveSidecar(const std::string& cnjPath, const std::string& root,
                                    const std::string& named, const char* field)
        {
            if (named.empty())
            {
                throw ContentLoadException("cnj-to-cnb: '" + cnjPath + "' has no '" +
                                            std::string(field) + "' field.");
            }
            return CNA::Internal::ResolveCnjSourceFileSafely(cnjPath, root, named)
                .resolvedNativePath;
        }

        /// Records a file whose contents are now inside the `.cnb`, under the name the `.cnj`
        /// itself used (plans/plan_cnb.md `CNBF-118`).
        ///
        /// `CnjToCnbResult::absorbedFiles` documents "paths as they were written in the source
        /// `.cnj`", and this used to record `filename()` instead -- so `art/hero.png` and
        /// `ui/hero.png` both came back as `hero.png`, which a build script matching the list
        /// against what it generated cannot tell apart, and which does not identify a file at all
        /// once a document names one in a subdirectory.
        void RecordAbsorbed(std::vector<std::string>& absorbed, const std::string& authoredName)
        {
            absorbed.push_back(std::filesystem::path(authoredName).generic_string());
        }

        std::vector<std::uint8_t> CompileTexture2DCnj(const std::string& json,
                                                       const std::string& cnjPath,
                                                       const std::string& root,
                                                       const std::string& name,
                                                       std::vector<std::string>& absorbed)
        {
            const JsonValue document = CNA::Internal::ParseJson(json);
            const JsonValue* sourceFile = document.FindMember("sourceFile");
            if (sourceFile == nullptr || !sourceFile->IsString())
            {
                throw ContentLoadException(
                    "cnj-to-cnb: Texture2D .cnj '" + cnjPath + "' has no 'sourceFile' naming an "
                    "image; a self-contained Texture2D .cnj is not a form CNA defines.");
            }
            const std::string imagePath =
                ResolveSidecar(cnjPath, root, sourceFile->stringValue, "sourceFile");

            CnbImageImportOptions options;
            // The same reader the runtime Texture2D .cnj path uses, so a document either keys out
            // the same pixels in both routes or is refused by both (CNBF-118). It used to CLAMP an
            // out-of-range component here and silently drop a malformed array, while the runtime
            // pushed the same text through std::stoi.
            options.colorKey = CNA::Internal::ReadCnjColorKey(
                document, "Texture2D .cnj '" + cnjPath + "'");
            const CnbTextureData texture = ImportImageAsCnbTexture2D(imagePath, options);
            RecordAbsorbed(absorbed, sourceFile->stringValue);
            return EncodeTexture2DToCnb(texture, name);
        }

        std::vector<std::uint8_t> CompileTexture3DCnj(const std::string& json,
                                                      const std::string& cnjPath,
                                                      const std::string& root,
                                                      const std::string& name,
                                                      std::vector<std::string>& absorbed)
        {
            const JsonValue document = CNA::Internal::ParseJson(json);
            // The same reader the runtime Texture3D .cnj path uses (CNBF-118). Both routes used to
            // cast numberValue straight to an integer, so 3.7 became 3 and a non-finite value was
            // undefined behaviour -- and the two casts had different widths, so they could accept
            // different documents.
            const CNA::Internal::CnjTexture3DDescription description =
                CNA::Internal::ReadCnjTexture3DDescription(
                    document, "Texture3D .cnj '" + cnjPath + "'");

            const std::string sidecarPath =
                ResolveSidecar(cnjPath, root, description.dataFile, "data");
            std::ifstream sidecar(sidecarPath, std::ios::binary);
            if (!sidecar.is_open())
            {
                throw ContentLoadException("cnj-to-cnb: cannot open '" + sidecarPath + "'.");
            }
            std::vector<std::uint8_t> pixels((std::istreambuf_iterator<char>(sidecar)),
                                              std::istreambuf_iterator<char>());
            if (pixels.size() != description.expectedByteCount)
            {
                throw ContentLoadException(
                    "cnj-to-cnb: Texture3D .cnj '" + cnjPath + "' declares " +
                    std::to_string(description.width) + "x" + std::to_string(description.height) +
                    "x" + std::to_string(description.depth) + ", which needs " +
                    std::to_string(description.expectedByteCount) +
                    " Rgba8 bytes, but its sidecar holds " + std::to_string(pixels.size()) + ".");
            }

            CnbTextureData volume;
            volume.width = description.width;
            volume.height = description.height;
            volume.depth = description.depth;
            volume.faceCount = 1u;
            volume.mipCount = 1u;
            CnbTextureRepresentation representation;
            representation.format = CnbTextureFormat::Rgba8;
            representation.levels.push_back(std::move(pixels));
            volume.representations.push_back(std::move(representation));
            RecordAbsorbed(absorbed, description.dataFile);
            return EncodeTexture3DToCnb(volume, name);
        }

        std::vector<std::uint8_t> CompileTextureCubeCnj(const std::string& json,
                                                         const std::string& cnjPath,
                                                         const std::string& root,
                                                         const std::string& name,
                                                         std::vector<std::string>& absorbed)
        {
            const JsonValue document = CNA::Internal::ParseJson(json);
            const JsonValue* sourceFile = document.FindMember("sourceFile");
            if (sourceFile == nullptr || !sourceFile->IsString())
            {
                throw ContentLoadException(
                    "cnj-to-cnb: TextureCube .cnj '" + cnjPath + "' has no 'sourceFile' naming a "
                    "DDS cube map.");
            }
            const std::string ddsPath =
                ResolveSidecar(cnjPath, root, sourceFile->stringValue, "sourceFile");
            const CnbTextureData cube = ImportDdsAsCnbTextureCube(ddsPath);
            RecordAbsorbed(absorbed, sourceFile->stringValue);
            return EncodeTextureCubeToCnb(cube, name);
        }

        std::vector<std::uint8_t> CompileSoundEffectCnj(const std::string& json,
                                                         const std::string& cnjPath,
                                                         const std::string& root,
                                                         const std::string& name,
                                                         std::vector<std::string>& absorbed)
        {
            const JsonValue document = CNA::Internal::ParseJson(json);
            const JsonValue* sourceFile = document.FindMember("sourceFile");
            if (sourceFile == nullptr || !sourceFile->IsString())
            {
                throw ContentLoadException(
                    "cnj-to-cnb: SoundEffect .cnj '" + cnjPath + "' has no 'sourceFile' naming a "
                    "WAV.");
            }
            const std::string wavPath =
                ResolveSidecar(cnjPath, root, sourceFile->stringValue, "sourceFile");
            const CnbSoundEffectData sound = ImportWavAsCnbSoundEffect(wavPath);
            RecordAbsorbed(absorbed, sourceFile->stringValue);
            return EncodeSoundEffectToCnb(sound, name);
        }

        std::vector<std::uint8_t> CompileSpriteFontCnj(const std::string& json,
                                                        const std::string& cnjPath,
                                                        const std::string& root,
                                                        const std::string& name,
                                                        std::vector<std::string>& absorbed)
        {
            const JsonValue document = CNA::Internal::ParseJson(json);
            // The same reader the runtime SpriteFont .cnj path uses (CNBF-118). Both routes had
            // their own reading of this document and neither validated its numbers: a rectangle
            // element that was a string read as 0, a fractional coordinate was truncated, and a
            // 'char' outside the Basic Multilingual Plane was cast into a glyph nothing could
            // match.
            const CNA::Internal::CnjSpriteFontDescription description =
                CNA::Internal::ReadCnjSpriteFontDescription(
                    document, "SpriteFont .cnj '" + cnjPath + "'");

            CnbSpriteFontData font;
            font.lineSpacing = description.lineSpacing;
            font.spacing = description.spacing;
            font.defaultCharacter = description.defaultCharacter;
            font.characters.reserve(description.glyphs.size());
            font.glyphBounds.reserve(description.glyphs.size());
            font.cropping.reserve(description.glyphs.size());
            font.kerning.reserve(description.glyphs.size());
            for (const CNA::Internal::CnjSpriteFontGlyph& glyph : description.glyphs)
            {
                font.characters.push_back(glyph.character);
                font.glyphBounds.push_back(glyph.source);
                font.cropping.push_back(glyph.crop);
                font.kerning.push_back(glyph.kerning);
            }

            // The atlas is decoded through the same pure image path Texture2D compilation uses and
            // EMBEDDED, so the compiled font needs nothing beside it at runtime.
            const std::string atlasPath =
                ResolveSidecar(cnjPath, root, description.textureName, "texture");
            font.atlas = ImportImageAsCnbTexture2D(atlasPath, {});
            RecordAbsorbed(absorbed, description.textureName);
            return EncodeSpriteFontToCnb(font, name);
        }
    }

    CnjToCnbResult CompileCnjToCnb(const std::string& cnjPath, const std::string& contentRoot,
                                    const std::string& contentName)
    {
        namespace fs = std::filesystem;

        const fs::path source = fs::path(cnjPath).lexically_normal();
        const std::string root =
            contentRoot.empty() ? source.parent_path().string() : contentRoot;
        const std::string name = contentName.empty() ? source.stem().string() : contentName;

        const std::string json = ReadWholeTextFile(cnjPath);
        const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
        // plans/plan_cnb.md CNBF-118: the ceiling is PER TYPE, and it is the same ceiling that
        // type's runtime `.cnj` reader applies.
        //
        // A flat ceiling of 2 was applied to every type, and it was only ever right for `Model` --
        // the one type whose runtime reader accepts version 2. For the other seven, the compiler
        // accepted a `"cnjVersion": 2` document that `ContentManager` refuses, so a build could
        // succeed and the same document could then fail to load. Three of those types are
        // compiled through their runtime reader, which would have caught it; five are compiled by
        // headless importers that never consult one, and for those nothing checked at all.
        //
        // Deriving the ceiling from the type name rather than repeating a number keeps this in
        // step with the readers by construction; `CnbCompilerStrictnessTests` asserts the two
        // agree for every supported type.
        const std::uint32_t maxVersion = envelope.type == "Model" ? 2u : 1u;
        CNA::Internal::ValidateCnjEnvelopeBaseline(envelope, cnjPath,
                                                    static_cast<int>(maxVersion));

        CnjToCnbResult result;
        result.absorbedFiles.push_back(source.filename().string());

        if (envelope.type == "Curve")
        {
            const Curve curve = LoadThroughCnjReader<Curve>(cnjPath, root);
            result.bytes = EncodeCurveToCnb(curve, name);
            result.assetTypeId = CnbAssetTypeId::Curve;
            result.assetTypeName = "Microsoft.Xna.Framework.Curve";
            return result;
        }

        if (envelope.type == "AnimationClip")
        {
            const AnimationClipEXT clip = LoadThroughCnjReader<AnimationClipEXT>(cnjPath, root);
            result.bytes = EncodeAnimationClipToCnb(clip, name);
            result.assetTypeId = CnbAssetTypeId::AnimationClip;
            result.assetTypeName = "Microsoft.Xna.Framework.Graphics.AnimationClipEXT";

            // A clipFile-form AnimationClip .cnj is a two-file asset; the compiled form is one
            // file, so record which sidecar stopped being needed.
            try
            {
                const CNA::Internal::JsonValue rootValue = CNA::Internal::ParseJson(json);
                if (const CNA::Internal::JsonValue* clipFile = rootValue.FindMember("clipFile");
                    clipFile != nullptr && clipFile->IsString() && !clipFile->stringValue.empty())
                {
                    result.absorbedFiles.push_back(clipFile->stringValue);
                }
            }
            catch (const CNA::Internal::JsonParseException&)
            {
                // Unreachable in practice -- the reader above already parsed this document -- and
                // a reporting detail is not worth failing a successful compile over.
            }
            return result;
        }

        if (envelope.type == "Model")
        {
            CnbModelFromCnjResult source = BuildCnbModelFromCnj(cnjPath, root);
            result.bytes = EncodeModelToCnb(source.model, name);
            result.assetTypeId = CnbAssetTypeId::Model;
            result.assetTypeName = "Microsoft.Xna.Framework.Graphics.Model";
            result.absorbedFiles.insert(result.absorbedFiles.end(),
                                        source.absorbedFiles.begin(), source.absorbedFiles.end());
            result.externalReferences = std::move(source.externalReferences);
            return result;
        }

        // plans/plan_cnb.md CNBF-111. Everything below is compiled through the headless pure-data
        // importers: no GraphicsDevice, no audio device. A texture .cnj names an image, a
        // SpriteFont .cnj names an atlas, a SoundEffect .cnj names a WAV -- and each of those is
        // resolved with the SAME containment rule the runtime readers use, so a compiler cannot be
        // the soft way into a path the runtime would refuse.
        if (envelope.type == "Texture2D")
        {
            result.bytes = CompileTexture2DCnj(json, cnjPath, root, name, result.absorbedFiles);
            result.assetTypeId = CnbAssetTypeId::Texture2D;
            result.assetTypeName = "Microsoft.Xna.Framework.Graphics.Texture2D";
            return result;
        }

        if (envelope.type == "TextureCube")
        {
            result.bytes = CompileTextureCubeCnj(json, cnjPath, root, name, result.absorbedFiles);
            result.assetTypeId = CnbAssetTypeId::TextureCube;
            result.assetTypeName = "Microsoft.Xna.Framework.Graphics.TextureCube";
            return result;
        }

        if (envelope.type == "Texture3D")
        {
            result.bytes = CompileTexture3DCnj(json, cnjPath, root, name, result.absorbedFiles);
            result.assetTypeId = CnbAssetTypeId::Texture3D;
            result.assetTypeName = "Microsoft.Xna.Framework.Graphics.Texture3D";
            return result;
        }

        if (envelope.type == "SpriteFont")
        {
            result.bytes = CompileSpriteFontCnj(json, cnjPath, root, name, result.absorbedFiles);
            result.assetTypeId = CnbAssetTypeId::SpriteFont;
            result.assetTypeName = "Microsoft.Xna.Framework.Graphics.SpriteFont";
            return result;
        }

        if (envelope.type == "SoundEffect")
        {
            result.bytes = CompileSoundEffectCnj(json, cnjPath, root, name, result.absorbedFiles);
            result.assetTypeId = CnbAssetTypeId::SoundEffect;
            result.assetTypeName = "Microsoft.Xna.Framework.Audio.SoundEffect";
            return result;
        }

        throw ContentLoadException(
            "cnj-to-cnb: '" + cnjPath + "' has .cnj type '" + envelope.type +
            "', which the CNB compiler does not support. Supported types are Curve, "
            "AnimationClip, Model, Texture2D, Texture3D, TextureCube, SpriteFont and "
            "SoundEffect.");
    }
}
