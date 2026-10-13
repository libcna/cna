// SPDX-License-Identifier: MS-PL
// CnbLoaderRegistry::Find() and ResolveForDocument(): the two queries that hand out a loader.
//
// Link boundary: asked for a declared built-in loader that is not installed yet, each first
// installs every declared one, which names every built-in content family -- so they live here,
// apart from the registry the load path uses (ResolveRegisteredForDocument), and only a program
// that asks the registry for a loader by hand links every family.
#include "CNA/Content/Cnb/CnbLoaderRegistry.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"

#include <mutex>

namespace CNA::Content
{
    namespace
    {
        namespace Content = Microsoft::Xna::Framework::Content;
        namespace Graphics = Microsoft::Xna::Framework::Graphics;

        void InstallDeclaredLoaders()
        {
            static std::mutex installing;
            const std::lock_guard lock(installing);
            Content::detail::BuiltinContent<Microsoft::Xna::Framework::Curve>::RegisterCnbLoader();
            Content::detail::BuiltinContent<Graphics::AnimationClipEXT>::RegisterCnbLoader();
            Content::detail::BuiltinContent<Graphics::Model>::RegisterCnbLoader();
            Content::detail::BuiltinContent<Graphics::Texture2D>::RegisterCnbLoader();
            Content::detail::BuiltinContent<Graphics::TextureCube>::RegisterCnbLoader();
            Content::detail::BuiltinContent<std::shared_ptr<Graphics::Texture3D>>::RegisterCnbLoader();
            Content::detail::BuiltinContent<Graphics::SpriteFont>::RegisterCnbLoader();
            Content::detail::BuiltinContent<Microsoft::Xna::Framework::Audio::SoundEffect>::RegisterCnbLoader();
            Content::detail::BuiltinContent<Microsoft::Xna::Framework::Media::Song>::RegisterCnbLoader();
            Content::detail::BuiltinContent<Microsoft::Xna::Framework::Media::Video>::RegisterCnbLoader();
        }
    }

    std::optional<CnbLoaderRegistry::LoaderFn> CnbLoaderRegistry::Find(std::uint32_t assetTypeId)
    {
        if (DeclaredButNotInstalled(assetTypeId)) { InstallDeclaredLoaders(); }
        return FindRegistered(assetTypeId);
    }

    CnbLoaderRegistry::LoaderFn CnbLoaderRegistry::ResolveForDocument(
        const Cnb::CnbDocument& document)
    {
        if (DeclaredButNotInstalled(document.AssetTypeId())) { InstallDeclaredLoaders(); }
        return ResolveRegisteredForDocument(document);
    }
}
