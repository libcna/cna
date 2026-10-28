// SPDX-License-Identifier: MS-PL
// Curve: the .cnj curve, .cnb and .xnb.
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/ContentRoot.hpp"
#include "CNA/Internal/TitlePath.hpp"
#include "CNA/Internal/GltfImport/CgltfFileCallbacks.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "System/IServiceProvider.hpp"
#include "CNA/Logger.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Internal/CnjCanonicalRead.hpp"
#include "CNA/Internal/CnjEnvelope.hpp"
#include "CNA/Internal/CnjMorphSidecarEXT.hpp"
#include "CNA/Internal/CnjSourceFile.hpp"
#include "CNA/Internal/Graphics/ModelMaterialVariantsEXT.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Json.hpp"
#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/Xnb/XnbTypeReaderTable.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "System/IO/FileStream.hpp"
#include "System/IO/MemoryStream.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <filesystem>
#include <optional>
#include <fstream>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include "BuiltinContentLoaders.hpp"

namespace Microsoft::Xna::Framework::Content
{
    namespace
    {
        using namespace detail;

        // CNJ CNB-44: self-contained JSON port of CurveContentTypeReader.hpp's already-
        // FNA-verified field order/shape -- "preLoop"/"postLoop" (CurveLoopType names, default
        // "Constant" when omitted) + "keys" (position/value/tangentIn/tangentOut/continuity,
        // tangentIn/tangentOut/continuity default to 0.0/0.0/"Smooth" when omitted per key).
        class CurveTypeReader : public LooseFileContentTypeReader<Microsoft::Xna::Framework::Curve>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnj"};
            }

            Microsoft::Xna::Framework::Curve Read(const std::string& path, ContentManager& /*cm*/) override
            {
                const std::string json = ReadTextFile(path);
                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "Curve", path);
                RejectSourceFileForSelfContainedCnj(envelope, "Curve", path);
                return CNA::Internal::ReadCnjCurve(CNA::Internal::ParseJson(json), path);
            }
        };
    } // anonymous namespace

    void detail::BuiltinContent<Microsoft::Xna::Framework::Curve>::RegisterCnbLoader()
    {
        ContentManager::RegisterBuiltinCnbLoader(
            CNA::Content::Cnb::CnbAssetTypeId::Curve, "Microsoft.Xna.Framework.Curve",
            [](const CNA::Content::Cnb::CnbDocument& document, ContentManager&,
               const std::string&) -> std::any
            { return std::any(CNA::Content::Cnb::DecodeCurveFromCnb(document)); });
    }

    void detail::BuiltinContent<Microsoft::Xna::Framework::Curve>::Register(ContentManager& manager)
    {
        manager.RegisterBuiltinTypeReader<Microsoft::Xna::Framework::Curve, CurveTypeReader>();
        RegisterCnbLoader();
        detail::RegisterDeclaredXnbReaders(&CNA::Internal::Xnb::RegisterCurveXnbReader);
    }
}
