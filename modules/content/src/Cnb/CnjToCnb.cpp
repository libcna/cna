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

        void RecordAbsorbed(std::vector<std::string>& absorbed, const std::string& path)
        {
            absorbed.push_back(std::filesystem::path(path).filename().string());
        }

        std::optional<std::array<std::uint8_t, 3>> ReadColorKey(const JsonValue& root)
        {
            const JsonValue* field = root.FindMember("colorKey");
            if (field == nullptr || field->type != CNA::Internal::JsonType::Array || field->arrayValue.size() != 3u)
            {
                return std::nullopt;
            }
            std::array<std::uint8_t, 3> key{};
            for (std::size_t i = 0; i < 3u; ++i)
            {
                if (!field->arrayValue[i].IsNumber()) { return std::nullopt; }
                const double v = field->arrayValue[i].numberValue;
                key[i] = static_cast<std::uint8_t>(v < 0.0 ? 0.0 : (v > 255.0 ? 255.0 : v));
            }
            return key;
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
            options.colorKey = ReadColorKey(document);
            const CnbTextureData texture = ImportImageAsCnbTexture2D(imagePath, options);
            RecordAbsorbed(absorbed, imagePath);
            return EncodeTexture2DToCnb(texture, name);
        }

        std::vector<std::uint8_t> CompileTexture3DCnj(const std::string& json,
                                                      const std::string& cnjPath,
                                                      const std::string& root,
                                                      const std::string& name,
                                                      std::vector<std::string>& absorbed)
        {
            const JsonValue document = CNA::Internal::ParseJson(json);
            const JsonValue* w = document.FindMember("width");
            const JsonValue* h = document.FindMember("height");
            const JsonValue* d = document.FindMember("depth");
            const JsonValue* dataField = document.FindMember("data");
            if (w == nullptr || !w->IsNumber() || h == nullptr || !h->IsNumber() ||
                d == nullptr || !d->IsNumber())
            {
                throw ContentLoadException("cnj-to-cnb: Texture3D .cnj '" + cnjPath +
                                            "' is missing a numeric width/height/depth.");
            }
            if (dataField == nullptr || !dataField->IsString() || dataField->stringValue.empty())
            {
                throw ContentLoadException("cnj-to-cnb: Texture3D .cnj '" + cnjPath +
                                            "' is missing a 'data' field naming a raw pixel "
                                            "sidecar.");
            }
            const auto width = static_cast<std::int64_t>(w->numberValue);
            const auto height = static_cast<std::int64_t>(h->numberValue);
            const auto depth = static_cast<std::int64_t>(d->numberValue);
            if (width <= 0 || height <= 0 || depth <= 0)
            {
                throw ContentLoadException("cnj-to-cnb: Texture3D .cnj '" + cnjPath +
                                            "' has a non-positive dimension.");
            }

            const std::string sidecarPath =
                ResolveSidecar(cnjPath, root, dataField->stringValue, "data");
            std::ifstream sidecar(sidecarPath, std::ios::binary);
            if (!sidecar.is_open())
            {
                throw ContentLoadException("cnj-to-cnb: cannot open '" + sidecarPath + "'.");
            }
            std::vector<std::uint8_t> pixels((std::istreambuf_iterator<char>(sidecar)),
                                              std::istreambuf_iterator<char>());
            const std::uint64_t expected =
                static_cast<std::uint64_t>(width) * height * depth * 4u;
            if (pixels.size() != expected)
            {
                throw ContentLoadException(
                    "cnj-to-cnb: Texture3D .cnj '" + cnjPath + "' declares " +
                    std::to_string(width) + "x" + std::to_string(height) + "x" +
                    std::to_string(depth) + ", which needs " + std::to_string(expected) +
                    " Rgba8 bytes, but its sidecar holds " + std::to_string(pixels.size()) + ".");
            }

            CnbTextureData volume;
            volume.width = static_cast<std::uint32_t>(width);
            volume.height = static_cast<std::uint32_t>(height);
            volume.depth = static_cast<std::uint32_t>(depth);
            volume.faceCount = 1u;
            volume.mipCount = 1u;
            CnbTextureRepresentation representation;
            representation.format = CnbTextureFormat::Rgba8;
            representation.levels.push_back(std::move(pixels));
            volume.representations.push_back(std::move(representation));
            RecordAbsorbed(absorbed, sidecarPath);
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
            RecordAbsorbed(absorbed, ddsPath);
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
            RecordAbsorbed(absorbed, wavPath);
            return EncodeSoundEffectToCnb(sound, name);
        }

        std::vector<std::uint8_t> CompileSpriteFontCnj(const std::string& json,
                                                        const std::string& cnjPath,
                                                        const std::string& root,
                                                        const std::string& name,
                                                        std::vector<std::string>& absorbed)
        {
            const JsonValue document = CNA::Internal::ParseJson(json);
            const JsonValue* textureField = document.FindMember("texture");
            if (textureField == nullptr || !textureField->IsString() ||
                textureField->stringValue.empty())
            {
                throw ContentLoadException("cnj-to-cnb: SpriteFont .cnj '" + cnjPath +
                                            "' has no 'texture' field naming its atlas.");
            }

            CnbSpriteFontData font;
            const JsonValue* lineSpacing = document.FindMember("lineSpacing");
            font.lineSpacing = lineSpacing != nullptr && lineSpacing->IsNumber()
                                    ? static_cast<std::int32_t>(lineSpacing->numberValue)
                                    : 0;
            const JsonValue* spacing = document.FindMember("spacing");
            font.spacing = spacing != nullptr && spacing->IsNumber()
                                ? static_cast<float>(spacing->numberValue)
                                : 0.0f;
            const JsonValue* defaultCharacter = document.FindMember("defaultCharacter");
            if (defaultCharacter != nullptr && defaultCharacter->IsString() &&
                !defaultCharacter->stringValue.empty())
            {
                font.defaultCharacter = static_cast<SharpRuntime::charcs>(
                    static_cast<unsigned char>(defaultCharacter->stringValue[0]));
            }

            const JsonValue* glyphs = document.FindMember("glyphs");
            if (glyphs == nullptr || glyphs->type != CNA::Internal::JsonType::Array || glyphs->arrayValue.empty())
            {
                throw ContentLoadException("cnj-to-cnb: SpriteFont .cnj '" + cnjPath +
                                            "' has no non-empty 'glyphs' array.");
            }
            const auto rect = [&](const JsonValue& g, const char* key)
            {
                const JsonValue* a = g.FindMember(key);
                if (a == nullptr || a->type != CNA::Internal::JsonType::Array || a->arrayValue.size() != 4u)
                {
                    throw ContentLoadException("cnj-to-cnb: SpriteFont .cnj '" + cnjPath +
                                                "' has a glyph with no 4-element '" +
                                                std::string(key) + "'.");
                }
                return Microsoft::Xna::Framework::Rectangle(
                    static_cast<int>(a->arrayValue[0].numberValue),
                    static_cast<int>(a->arrayValue[1].numberValue),
                    static_cast<int>(a->arrayValue[2].numberValue),
                    static_cast<int>(a->arrayValue[3].numberValue));
            };
            for (const JsonValue& g : glyphs->arrayValue)
            {
                const JsonValue* ch = g.FindMember("char");
                if (ch == nullptr || !ch->IsNumber())
                {
                    throw ContentLoadException("cnj-to-cnb: SpriteFont .cnj '" + cnjPath +
                                                "' has a glyph with no numeric 'char'.");
                }
                font.characters.push_back(static_cast<SharpRuntime::charcs>(ch->numberValue));
                font.glyphBounds.push_back(rect(g, "source"));
                font.cropping.push_back(rect(g, "crop"));
                const JsonValue* k = g.FindMember("kerning");
                if (k == nullptr || k->type != CNA::Internal::JsonType::Array || k->arrayValue.size() != 3u)
                {
                    throw ContentLoadException("cnj-to-cnb: SpriteFont .cnj '" + cnjPath +
                                                "' has a glyph with no 3-element 'kerning'.");
                }
                font.kerning.emplace_back(static_cast<float>(k->arrayValue[0].numberValue),
                                           static_cast<float>(k->arrayValue[1].numberValue),
                                           static_cast<float>(k->arrayValue[2].numberValue));
            }

            // The atlas is decoded through the same pure image path Texture2D compilation uses and
            // EMBEDDED, so the compiled font needs nothing beside it at runtime.
            const std::string atlasPath =
                ResolveSidecar(cnjPath, root, textureField->stringValue, "texture");
            font.atlas = ImportImageAsCnbTexture2D(atlasPath, {});
            RecordAbsorbed(absorbed, atlasPath);
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
        // Baseline only: the per-type ceiling belongs to the reader that owns the type, and the
        // reader is what actually loads the document a line or two below.
        CNA::Internal::ValidateCnjEnvelopeBaseline(envelope, cnjPath, /*maxVersion=*/2);

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
