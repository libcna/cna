// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbLoaderRegistry.hpp"

#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"

namespace CNA::Content
{
    namespace
    {
        struct Registration
        {
            std::string debugTypeName;
            CnbLoaderRegistry::LoaderFn loader;
        };

        std::unordered_map<std::uint32_t, Registration>& Table()
        {
            static std::unordered_map<std::uint32_t, Registration> table;
            return table;
        }
    }

    void CnbLoaderRegistry::Register(std::uint32_t assetTypeId, const std::string& debugTypeName,
                                     LoaderFn loader)
    {
        if (assetTypeId == Cnb::CnbAssetTypeId::Invalid)
        {
            throw std::invalid_argument(
                "CnbLoaderRegistry::Register(): asset type 0 is not a valid asset type.");
        }
        if (debugTypeName.empty())
        {
            throw std::invalid_argument(
                "CnbLoaderRegistry::Register(): debugTypeName must not be empty.");
        }
        if (!loader)
        {
            throw std::invalid_argument("CnbLoaderRegistry::Register(): loader must not be empty.");
        }

        auto& table = Table();
        const auto existing = table.find(assetTypeId);
        if (existing != table.end())
        {
            if (existing->second.debugTypeName == debugTypeName)
            {
                // Same type registering itself again -- tolerated, exactly as
                // ContentTypeReaderManager::AddTypeCreator tolerates a repeat registration.
                return;
            }
            throw std::logic_error(
                "CnbLoaderRegistry::Register(): asset type id " +
                Cnb::AssetTypeIdToString(assetTypeId) + " is already registered for '" +
                existing->second.debugTypeName + "'; refusing to re-register it for '" +
                debugTypeName +
                "'. Two custom type names whose FNV-1a hashes collide must not share a loader.");
        }

        table.emplace(assetTypeId, Registration{debugTypeName, std::move(loader)});
    }

    bool CnbLoaderRegistry::Remove(std::uint32_t assetTypeId)
    {
        return Table().erase(assetTypeId) != 0u;
    }

    void CnbLoaderRegistry::Clear() { Table().clear(); }

    bool CnbLoaderRegistry::IsRegistered(std::uint32_t assetTypeId)
    {
        return Table().find(assetTypeId) != Table().end();
    }

    const CnbLoaderRegistry::LoaderFn* CnbLoaderRegistry::Find(std::uint32_t assetTypeId)
    {
        const auto it = Table().find(assetTypeId);
        return it == Table().end() ? nullptr : &it->second.loader;
    }

    std::string CnbLoaderRegistry::RegisteredTypeName(std::uint32_t assetTypeId)
    {
        const auto it = Table().find(assetTypeId);
        return it == Table().end() ? std::string() : it->second.debugTypeName;
    }

    void CnbLoaderRegistry::RegisterBuiltIns()
    {
        // Curve and AnimationClip need nothing but their own codecs -- no GraphicsDevice, no
        // external references -- so they are registered here. Model's loader has to build real
        // VertexBuffer/IndexBuffer/Effect objects, so it registers itself from ContentManager.cpp
        // where those helpers already live.
        Register(Cnb::CnbAssetTypeId::Curve, "Microsoft.Xna.Framework.Curve",
                 [](const Cnb::CnbDocument& document,
                    Microsoft::Xna::Framework::Content::ContentManager&,
                    const std::string&) -> std::any
                 { return std::any(Cnb::DecodeCurveFromCnb(document)); });

        Register(Cnb::CnbAssetTypeId::AnimationClip,
                 "Microsoft.Xna.Framework.Graphics.AnimationClipEXT",
                 [](const Cnb::CnbDocument& document,
                    Microsoft::Xna::Framework::Content::ContentManager&,
                    const std::string&) -> std::any
                 { return std::any(Cnb::DecodeAnimationClipFromCnb(document)); });
    }
}
