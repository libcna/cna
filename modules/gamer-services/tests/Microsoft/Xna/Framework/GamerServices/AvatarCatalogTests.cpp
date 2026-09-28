// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"

#include <set>
#include <stdexcept>
#include <string>

namespace Avatars = CNA::Internal::GamerServices::Avatars;

TEST(AvatarCatalogTest, TheSkeletonIsTheXnaTable) {
    const auto& parents = Avatars::parentBones();
    EXPECT_EQ(parents[0], -1);
    EXPECT_EQ(parents[19], 14);
    EXPECT_EQ(parents[70], 60);
    for (int bone = 1; bone < Avatars::BoneCount; ++bone) {
        EXPECT_LT(parents[bone], bone);
        EXPECT_EQ(Avatars::boneIndex(Avatars::boneName(bone)), bone);
    }
    EXPECT_EQ(Avatars::boneName(19), "Head");
    EXPECT_EQ(Avatars::boneName(4), "Slot04");
    EXPECT_EQ(Avatars::boneIndex("NotABone"), -1);
}

TEST(AvatarCatalogTest, TheEmbeddedManifestListsTheCodecCatalog) {
    const auto& manifest = Avatars::embeddedManifest();
    EXPECT_EQ(manifest.version, Avatars::CatalogVersion);
    for (const auto& item : Avatars::catalogItems()) {
        const auto* listed = manifest.item(item.id);
        ASSERT_NE(listed, nullptr) << item.id;
        EXPECT_EQ(listed->slot, item.slot);
        EXPECT_EQ(listed->name, item.name);
    }
    EXPECT_EQ(manifest.items.size(), Avatars::catalogItems().size());
    EXPECT_EQ(manifest.authoredHeightMillimeters[0], Avatars::authoredHeightMillimeters(0));
    EXPECT_EQ(manifest.authoredHeightMillimeters[1], Avatars::authoredHeightMillimeters(1));
}

TEST(AvatarCatalogTest, EveryEmbeddedAssetMatchesItsHashAndParses) {
    const auto& manifest = Avatars::embeddedManifest();
    for (const auto& [name, asset] : manifest.assets) {
        const auto bytes = Avatars::resolveAsset(manifest, name);
        ASSERT_TRUE(bytes.has_value()) << name;
        EXPECT_EQ(bytes->view.size(), asset.size);
        if (name.ends_with(".glb")) {
            EXPECT_NO_THROW((void)Avatars::parseAvatarGlb(bytes->view)) << name;
        }
    }
}

TEST(AvatarCatalogTest, ItemsAreFittedToTheirBody) {
    const auto& manifest = Avatars::embeddedManifest();
    for (int body = 0; body < 2; ++body) {
        const auto bodyGlb = Avatars::parseAvatarGlb(Avatars::resolveAsset(manifest, manifest.bodies[body])->view);
        std::set<std::string> features;
        for (const auto& primitive : bodyGlb.primitives) {
            features.insert(primitive.feature);
        }
        EXPECT_EQ(features, (std::set<std::string>{"", "eyeLeft", "eyeRight", "eyebrowLeft", "eyebrowRight", "mouth"}));
        for (const auto& item : manifest.items) {
            const auto glb = Avatars::parseAvatarGlb(Avatars::resolveAsset(manifest, item.assets[body])->view);
            for (int bone = 0; bone < Avatars::BoneCount; ++bone) {
                EXPECT_LT(Microsoft::Xna::Framework::Vector3::Distance(glb.bindTranslations[bone], bodyGlb.bindTranslations[bone]), 1e-4f)
                    << item.name << " bone " << bone;
            }
            EXPECT_FALSE(glb.primitives.empty()) << item.name;
        }
    }
}

TEST(AvatarCatalogTest, EveryPresetIsAnimatedOnNamedBonesOnly) {
    const auto& library = Avatars::clipLibrary();
    ASSERT_EQ(library.clips.size(), 31u);
    for (const auto& clip : library.clips) {
        EXPECT_GT(clip.duration, 2.5f) << clip.name;
        EXPECT_FALSE(clip.expressions.empty()) << clip.name;
        for (const auto& track : clip.tracks) {
            // Helper slots (unnamed in XNA) are never animated by CNA presets.
            EXPECT_FALSE(Avatars::boneName(track.bone).starts_with("Slot")) << clip.name;
            EXPECT_TRUE(!track.translation || track.bone == 0);
        }
    }
}

TEST(AvatarCatalogTest, MalformedAssetsAreRejected) {
    const auto& manifest = Avatars::embeddedManifest();
    const auto bytes = Avatars::resolveAsset(manifest, manifest.bodies[0]);
    std::vector<std::uint8_t> copy(bytes->view.begin(), bytes->view.end());
    EXPECT_THROW((void)Avatars::parseAvatarGlb(std::span(copy).first(copy.size() / 2)), std::runtime_error);
    std::vector<std::uint8_t> garbage(4096, 0x41);
    EXPECT_THROW((void)Avatars::parseAvatarGlb(garbage), std::runtime_error);
    EXPECT_THROW((void)Avatars::parseAvatarGlb({}), std::runtime_error);
    EXPECT_FALSE(Avatars::resolveAsset(manifest, "missing.glb").has_value());
    EXPECT_FALSE(Avatars::resolveAsset(manifest, "../catalog.json").has_value());
}

TEST(AvatarCatalogTest, MalformedManifestsAreRejected) {
    EXPECT_THROW((void)Avatars::parseManifest("not json"), std::runtime_error);
    EXPECT_THROW((void)Avatars::parseManifest("{}"), std::runtime_error);
    EXPECT_THROW((void)Avatars::parseManifest(R"({"format":1,"rig":"cna-avatar-71","catalogVersion":1,"assets":[{"name":"../x.glb","sha256":"00","size":1}]})"),
                 std::runtime_error);
}

TEST(AvatarCatalogTest, AnAssembledAvatarUsesItsDescription) {
    Avatars::AvatarDescriptor descriptor;
    descriptor.bodyType = 0;
    descriptor.heightMillimeters = 1600;
    descriptor.colors[static_cast<std::size_t>(Avatars::AvatarColorSlot::Top)] = {200, 10, 20};
    descriptor.items = {3, 22, 42, 61, 80, 101};
    const auto model = Avatars::buildAvatarModel(descriptor);
    EXPECT_FLOAT_EQ(model->height, 1.6f);
    EXPECT_TRUE(model->substitutedItems.empty());
    bool topTinted = false;
    int decals = 0;
    float highest = 0.0f;
    for (const auto& part : model->parts) {
        topTinted |= std::abs(part.color.X - 200.0f / 255.0f) < 1e-3f && part.feature == Avatars::AvatarFeature::None;
        decals += part.feature != Avatars::AvatarFeature::None;
        for (const auto& vertex : part.vertices) {
            highest = std::max(highest, vertex.position.Y);
        }
    }
    EXPECT_TRUE(topTinted);
    EXPECT_EQ(decals, 7);
    // The tallest vertex is the hat/hair top, a little above the head itself.
    EXPECT_GT(highest, 1.6f);
    EXPECT_LT(highest, 1.75f);
    // An unknown required item falls back to the slot's default instead of failing.
    auto unknown = descriptor;
    unknown.catalogVersion = Avatars::CatalogVersion + 1;
    unknown.items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Top)] = 999;
    const auto substituted = Avatars::buildAvatarModel(unknown);
    ASSERT_EQ(substituted->substitutedItems.size(), 1u);
    EXPECT_EQ(substituted->substitutedItems[0], 999);
}
