// SPDX-License-Identifier: MS-PL
// ContentManager::RegisterBuiltinLoaders(): every built-in content family at once.
//
// Link boundary: the one translation unit that names every family's registration. Load<T>()
// never calls it -- each type registers its own readers (detail::BuiltinContent) -- so only a
// program that asks for everything links everything.
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"

namespace Microsoft::Xna::Framework::Content
{
    void ContentManager::RegisterBuiltinLoaders()
    {
        detail::BuiltinContent<Graphics::Texture2D>::Register(*this);
        detail::BuiltinContent<Graphics::TextureCube>::Register(*this);
        detail::BuiltinContent<std::shared_ptr<Graphics::Texture3D>>::Register(*this);
        detail::BuiltinContent<Audio::SoundEffect>::Register(*this);
        detail::BuiltinContent<std::shared_ptr<Graphics::Effect>>::Register(*this);
        detail::BuiltinContent<Graphics::SpriteFont>::Register(*this);
        detail::BuiltinContent<Graphics::Model>::Register(*this);
        detail::BuiltinContent<Graphics::AnimationClipEXT>::Register(*this);
        detail::BuiltinContent<Microsoft::Xna::Framework::Curve>::Register(*this);
        detail::BuiltinContent<std::shared_ptr<Graphics::SkinnedModelEXT>>::Register(*this);
        detail::BuiltinContent<Media::Song>::Register(*this);
        detail::BuiltinContent<Media::Video>::Register(*this);
        detail::RegisterEveryBuiltinXnbReader();
    }
}
