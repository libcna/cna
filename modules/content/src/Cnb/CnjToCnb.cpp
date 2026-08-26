// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnjToCnb.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
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

        throw ContentLoadException(
            "cnj-to-cnb: '" + cnjPath + "' has .cnj type '" + envelope.type +
            "', which the CNB compiler does not support. Supported types are Curve and "
            "AnimationClip.");
    }
}
