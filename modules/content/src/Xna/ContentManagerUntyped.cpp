// SPDX-License-Identifier: MS-PL
// The untyped `.xnb` route: an external reference whose type the referencing reader does not know
// (ContentReader::ReadExternalReference()). It may name any asset, so it registers every built-in
// `.xnb` reader -- which is why it is a translation unit of its own.
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
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Content
{
    std::any ContentManager::LoadUntypedXnbReference(const std::string& assetName)
    {
        if (disposed_)
        {
            throw System::ObjectDisposedException("ContentManager");
        }

        const AssetCacheKey cacheKey{std::type_index(typeid(std::any)), NormalizeKey(assetName)};
        const auto cached = loadedAssets_.find(cacheKey);
        if (cached != loadedAssets_.end())
        {
            return cached->second;
        }

        const auto stream = OpenStream(assetName);
        if (!stream)
        {
            throw ContentLoadException(
                "ContentManager: OpenStream returned no stream for asset '" + assetName + "'.");
        }
        const auto xnbBytes = ReadStreamToEnd(*stream, assetName);
        const std::string xnbPath =
            ResolveExistingAssetPath(BuildAssetPath(assetName) + ".xnb");

        detail::RegisterEveryBuiltinXnbReader();
        std::any result = LoadXnbAssetUntyped(xnbBytes, xnbPath, assetName);
        loadedAssets_[cacheKey] = result;
        return result;
    }

    std::any ContentManager::LoadXnbAssetUntyped(const std::vector<std::uint8_t>& bytes,
                                                  const std::string& xnbPath,
                                                  const std::string& assetName)
    {
        const auto header = ReadXnbHeader(bytes, xnbPath);

        switch (header.compression)
        {
            case CNA::Internal::Xnb::XnbCompression::None:
            {
                System::IO::MemoryStream bodyStream(
                    bytes.data() + 10,
                    static_cast<int32_t>(bytes.size()) - 10);
                ContentReader contentReader(this, &bodyStream, assetName,
                                            header.version, header.platform);
                return contentReader.ReadAsset();
            }
            case CNA::Internal::Xnb::XnbCompression::Lzx:
            {
                System::IO::MemoryStream sizeStream(
                    reinterpret_cast<const uint8_t*>(bytes.data()) + 10, 4);
                System::IO::BinaryReader sizeReader(&sizeStream, true);
                const int32_t decompressedSize = sizeReader.ReadInt32();
                const int32_t compressedSize = header.totalLength - 14;
                const auto decompressed = CNA::Internal::Xnb::DecompressXnbPayload(
                    bytes.data() + 14,
                    compressedSize, decompressedSize, xnbPath);
                System::IO::MemoryStream bodyStream(
                    decompressed.data(), static_cast<int32_t>(decompressed.size()));
                ContentReader contentReader(this, &bodyStream, assetName,
                                            header.version, header.platform);
                return contentReader.ReadAsset();
            }
            case CNA::Internal::Xnb::XnbCompression::Lz4:
                throw ContentLoadException(
                    "'" + xnbPath + "' uses MonoGame's Lz4 compression, which CNA does not yet "
                    "support (plans/plan_xnb.md XNB-30C).");
            case CNA::Internal::Xnb::XnbCompression::Unknown:
            default:
                throw ContentLoadException(
                    "'" + xnbPath + "' has an unrecognized compression flag combination.");
        }
    }

    std::any ContentReader::ReadExternalReference()
    {
        const std::string externalReference = ReadString();
        if (externalReference.empty())
        {
            return {};
        }
        if (!contentManager_)
        {
            throw ContentLoadException(
                "'" + assetName_ + "' references external asset '" + externalReference +
                "', but this ContentReader has no owning ContentManager to load it through.");
        }
        const std::string resolved = detail::ResolveExternalReferencePath(assetName_, externalReference);
        return contentManager_->LoadUntypedXnbReference(resolved);
    }
}
