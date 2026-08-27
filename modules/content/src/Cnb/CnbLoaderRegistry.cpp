// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbLoaderRegistry.hpp"

#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content
{
    namespace
    {
        struct Registration
        {
            std::string canonicalTypeName;
            CnbLoaderRegistry::LoaderFn loader;
        };

        // One function-local static for the table and one for its lock, both with the guaranteed
        // thread-safe initialisation C++11 gives function-local statics. A namespace-scope object
        // would additionally be exposed to static-initialisation order across translation units,
        // which matters here because ContentManager's constructor registers built-ins and a game
        // may well construct one from a static initialiser of its own.
        std::unordered_map<std::uint32_t, Registration>& Table()
        {
            static std::unordered_map<std::uint32_t, Registration> table;
            return table;
        }

        std::shared_mutex& TableMutex()
        {
            static std::shared_mutex mutex;
            return mutex;
        }
    }

    void CnbLoaderRegistry::Register(std::uint32_t assetTypeId,
                                     const std::string& canonicalTypeName, LoaderFn loader)
    {
        if (assetTypeId == Cnb::CnbAssetTypeId::Invalid)
        {
            throw std::invalid_argument(
                "CnbLoaderRegistry::Register(): asset type 0 is not a valid asset type.");
        }
        if (canonicalTypeName.empty())
        {
            throw std::invalid_argument(
                "CnbLoaderRegistry::Register(): canonicalTypeName must not be empty.");
        }
        if (!loader)
        {
            throw std::invalid_argument("CnbLoaderRegistry::Register(): loader must not be empty.");
        }
        // A custom identifier IS the hash of its canonical name, and the load path compares that
        // name against the one the file carries. Registering a name the identifier does not hash
        // to would therefore make every file of that type unloadable -- with a confusing
        // "names X but is registered for Y" error at load time, far from the mistake. Caught here
        // instead, at the registration that is wrong.
        if (Cnb::IsCustomAssetTypeId(assetTypeId) &&
            Cnb::CnbAssetTypeIdFromName(canonicalTypeName) != assetTypeId)
        {
            throw std::invalid_argument(
                "CnbLoaderRegistry::Register(): custom asset type id " +
                Cnb::AssetTypeIdToString(assetTypeId) + " is not the identifier '" +
                canonicalTypeName +
                "' hashes to. A custom identifier must be minted with "
                "CnbAssetTypeIdFromName(canonicalTypeName).");
        }

        const std::unique_lock lock(TableMutex());
        auto& table = Table();
        const auto existing = table.find(assetTypeId);
        if (existing != table.end())
        {
            if (existing->second.canonicalTypeName == canonicalTypeName)
            {
                // Same type registering itself again -- tolerated, exactly as
                // ContentTypeReaderManager::AddTypeCreator tolerates a repeat registration.
                return;
            }
            throw std::logic_error(
                "CnbLoaderRegistry::Register(): asset type id " +
                Cnb::AssetTypeIdToString(assetTypeId) + " is already registered for '" +
                existing->second.canonicalTypeName + "'; refusing to re-register it for '" +
                canonicalTypeName +
                "'. Two custom type names whose FNV-1a hashes collide must not share a loader.");
        }

        table.emplace(assetTypeId, Registration{canonicalTypeName, std::move(loader)});
    }

    bool CnbLoaderRegistry::Remove(std::uint32_t assetTypeId)
    {
        const std::unique_lock lock(TableMutex());
        return Table().erase(assetTypeId) != 0u;
    }

    void CnbLoaderRegistry::Clear()
    {
        const std::unique_lock lock(TableMutex());
        Table().clear();
    }

    bool CnbLoaderRegistry::IsRegistered(std::uint32_t assetTypeId)
    {
        const std::shared_lock lock(TableMutex());
        return Table().find(assetTypeId) != Table().end();
    }

    std::optional<CnbLoaderRegistry::LoaderFn> CnbLoaderRegistry::Find(std::uint32_t assetTypeId)
    {
        const std::shared_lock lock(TableMutex());
        const auto it = Table().find(assetTypeId);
        if (it == Table().end()) { return std::nullopt; }
        return it->second.loader;
    }

    std::string CnbLoaderRegistry::RegisteredTypeName(std::uint32_t assetTypeId)
    {
        const std::shared_lock lock(TableMutex());
        const auto it = Table().find(assetTypeId);
        return it == Table().end() ? std::string() : it->second.canonicalTypeName;
    }

    CnbLoaderRegistry::LoaderFn CnbLoaderRegistry::ResolveForDocument(
        const Cnb::CnbDocument& document)
    {
        const std::uint32_t assetTypeId = document.AssetTypeId();
        const std::string& fileTypeName = document.Metadata().assetTypeName;

        LoaderFn loader;
        std::string registeredName;
        {
            const std::shared_lock lock(TableMutex());
            const auto it = Table().find(assetTypeId);
            if (it != Table().end())
            {
                loader = it->second.loader;
                registeredName = it->second.canonicalTypeName;
            }
        }

        if (!loader)
        {
            throw ContentLoadException(
                "'" + document.Origin() + "' holds a " + Cnb::AssetTypeIdToString(assetTypeId) +
                " asset, which this build of CNA has no .cnb loader for" +
                (fileTypeName.empty() ? "." : " (the file names it '" + fileTypeName + "')."));
        }

        if (Cnb::IsCustomAssetTypeId(assetTypeId))
        {
            // CnbWriter refuses to produce a custom-typed file without this, so its absence means
            // the file was written by something that is not CNA -- or by a CNA old enough to
            // predate the rule. Either way there is nothing to check the identifier against, and
            // dispatching on 31 bits of hash alone is how one game's level file gets decoded as
            // another's.
            if (!document.Metadata().present || fileTypeName.empty())
            {
                throw ContentLoadException(
                    "'" + document.Origin() + "' declares custom asset type " +
                    Cnb::AssetTypeIdToString(assetTypeId) +
                    " but carries no canonical type name. CNB requires one for a custom type,"
                    " because a custom identifier is a 31-bit hash and two unrelated types can"
                    " collide.");
            }
            if (fileTypeName != registeredName)
            {
                throw ContentLoadException(
                    "'" + document.Origin() + "' declares custom type '" + fileTypeName +
                    "', but asset type id " + Cnb::AssetTypeIdToString(assetTypeId) +
                    " is registered for '" + registeredName +
                    "'. These are different types whose identifiers collide; refusing to decode"
                    " one as the other.");
            }
        }

        return loader;
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
