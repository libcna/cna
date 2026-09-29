// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"

#include <algorithm>
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

TEST(AvatarCatalogTest, EveryEmbeddedCatalogResolvesToExactlyItsVersion) {
    const auto& catalogs = Avatars::embeddedCatalogs();
    ASSERT_FALSE(catalogs.empty());
    EXPECT_EQ(catalogs.front()->version, Avatars::BaseCatalogVersion);
    for (std::size_t index = 0; index < catalogs.size(); ++index) {
        const auto& catalog = catalogs[index];
        if (index > 0) {
            EXPECT_GT(catalog->version, catalogs[index - 1]->version);
        }
        EXPECT_EQ(Avatars::embeddedManifest(catalog->version), catalog);
        EXPECT_EQ(Avatars::catalogManifest(catalog->version), catalog);
    }
    EXPECT_EQ(&Avatars::newestEmbeddedManifest(), catalogs.back().get());
    EXPECT_EQ(Avatars::embeddedManifest(0), nullptr);
    EXPECT_EQ(Avatars::embeddedManifest(static_cast<std::uint16_t>(catalogs.back()->version + 1)), nullptr);
    // Catalogs only grow: every item of an earlier version keeps its id and slot.
    for (std::size_t index = 1; index < catalogs.size(); ++index) {
        for (const auto& item : catalogs[index - 1]->items) {
            const auto* later = catalogs[index]->item(item.id);
            ASSERT_NE(later, nullptr) << item.id;
            EXPECT_EQ(later->slot, item.slot) << item.id;
        }
    }
}

TEST(AvatarCatalogTest, CatalogV1IsFrozen) {
    // The v1 manifest is released: descriptions name it, so its bytes never change.
    const auto& files = Avatars::embeddedCatalogFiles();
    auto manifest = std::find_if(files.begin(), files.end(), [](const auto& file) { return std::string_view(file.name) == "v1/catalog.json"; });
    ASSERT_NE(manifest, files.end());
    EXPECT_EQ(Avatars::sha256Hex(manifest->bytes()), "d794cc4152895876a1f6e3bfcfa494751986fc5b40997f0e42fde94875d63c0b");
    const auto v1 = Avatars::embeddedManifest(1);
    ASSERT_NE(v1, nullptr);
    EXPECT_EQ(v1->items.size(), 18u);
    EXPECT_EQ(v1->authoredHeightMillimeters[0], 1680);
    EXPECT_EQ(v1->authoredHeightMillimeters[1], 1800);
}

TEST(AvatarCatalogTest, CatalogV2IsFrozen) {
    // Accounts, the service's golden fixtures and released builds name catalog 2: its manifest,
    // and through it every file it lists, never changes. New art goes into a new catalog version.
    const auto& files = Avatars::embeddedCatalogFiles();
    auto manifest = std::find_if(files.begin(), files.end(), [](const auto& file) { return std::string_view(file.name) == "v2/catalog.json"; });
    ASSERT_NE(manifest, files.end());
    EXPECT_EQ(Avatars::sha256Hex(manifest->bytes()), "7a27a9d657efdc903e128736b22a1be381dc627c5631b6c7926c9535cf4c2ae1");
    const auto v2 = Avatars::embeddedManifest(2);
    ASSERT_NE(v2, nullptr);
    EXPECT_EQ(v2->items.size(), 39u);
    EXPECT_EQ(v2->featureItems.size(), 4u);
    EXPECT_EQ(v2->authoredHeightMillimeters[0], 1680);
    EXPECT_EQ(v2->authoredHeightMillimeters[1], 1800);
}

TEST(AvatarCatalogTest, EmbeddedAssetsResolveByContentNotByName) {
    const auto v1 = Avatars::embeddedManifest(1);
    // A manifest naming "body.female.glb" with the male body's contents gets the male body; one
    // whose listed contents are compiled in nowhere gets nothing (no service here).
    auto swapped = *v1;
    swapped.assets.at("body.female.glb") = swapped.assets.at("body.male.glb");
    swapped.assets.at("body.female.glb").name = "body.female.glb";
    const auto male = Avatars::resolveAsset(*v1, "body.male.glb");
    const auto resolved = Avatars::resolveAsset(swapped, "body.female.glb");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->view.data(), male->view.data());
    auto foreign = *v1;
    foreign.assets.at("hair_bob.female.glb").sha256 = std::string(64, 'a');
    EXPECT_FALSE(Avatars::resolveAsset(foreign, "hair_bob.female.glb").has_value());
}

TEST(AvatarCatalogTest, AnUnavailableCatalogNeverReadsItsIdsAgainstAnotherVersion) {
    // No service is configured, so a version past the compiled-in ones cannot be obtained. Its
    // ids (which happen to exist in v1) must not be looked up in any other version.
    Avatars::AvatarDescriptor descriptor;
    descriptor.catalogVersion = static_cast<std::uint16_t>(Avatars::newestEmbeddedManifest().version + 7);
    descriptor.items = {3, 22, 42, 61, 80, 101};
    const auto model = Avatars::buildAvatarModel(descriptor);
    EXPECT_TRUE(model->catalogUnavailable);
    EXPECT_EQ(model->substitutedItems, (std::vector<std::uint16_t>{3, 22, 42, 61, 80, 101}));
    EXPECT_FLOAT_EQ(model->height, 1.7f);
}

namespace {
// A digest of everything an assembled avatar draws with, quantized so the last bit of a float
// cannot change it.
std::string ModelDigest(const Avatars::AvatarModel& model) {
    std::vector<std::uint8_t> bytes;
    auto put = [&](long long value) {
        for (int shift = 0; shift < 64; shift += 8) {
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    };
    auto q = [&](float value, double step) { put(std::llround(value / step)); };
    q(model.height, 1e-5);
    for (const auto& t : model.bindTranslations) {
        q(t.X, 1e-5), q(t.Y, 1e-5), q(t.Z, 1e-5);
    }
    for (const auto& part : model.parts) {
        put(static_cast<long long>(part.vertices.size()));
        for (const auto& v : part.vertices) {
            q(v.position.X, 1e-5), q(v.position.Y, 1e-5), q(v.position.Z, 1e-5);
            q(v.normal.X, 1e-4), q(v.normal.Y, 1e-4), q(v.normal.Z, 1e-4);
            q(v.uv.X, 1e-4), q(v.uv.Y, 1e-4);
            q(v.weights.X, 1e-4), q(v.weights.Y, 1e-4), q(v.weights.Z, 1e-4), q(v.weights.W, 1e-4);
            bytes.insert(bytes.end(), v.joints.begin(), v.joints.end());
        }
        for (auto index : part.indices) {
            put(index);
        }
        q(part.color.X, 1e-4), q(part.color.Y, 1e-4), q(part.color.Z, 1e-4);
        put(part.image), put(static_cast<int>(part.feature)), put(part.layer);
    }
    for (const auto& image : model.images) {
        put(image.width), put(image.height);
        bytes.insert(bytes.end(), image.rgba.begin(), image.rgba.end());
    }
    for (const auto& tile : *model.faceTiles) {
        bytes.insert(bytes.end(), tile.rgba.begin(), tile.rgba.end());
    }
    for (const auto& state : model.face.eyes) {
        for (const auto& side : state) {
            put(side[0]), put(side[1]);
        }
    }
    for (const auto& state : model.face.eyebrows) {
        put(state[0]), put(state[1]);
    }
    for (auto mouth : model.face.mouths) {
        put(mouth);
    }
    return Avatars::sha256Hex(bytes);
}
}

TEST(AvatarCatalogTest, V1DescriptionsAssembleExactlyAsReleased) {
    // Pinned while v1 was the only compiled-in catalog (GSP-B1): embedding later catalogs must
    // not change what a v1 description draws.
    struct Case {
        std::uint8_t body;
        std::uint16_t height;
        std::uint8_t build;
        std::array<std::uint16_t, 6> items;
        const char* digest;
    };
    const Case cases[] = {
        {0, 1680, 128, {3, 20, 42, 60, 0, 0}, "0091f3f492961f6d35b7f55ed0baf3d5466123651495664f01fe1ea2fca7fb04"},
        {1, 1950, 40, {2, 22, 40, 61, 81, 100}, "9f3391987a16bc85c6b28f783c34903c8d362f3af884883cbd4261cd3e4f8c3f"},
        {0, 1500, 220, {4, 23, 41, 61, 80, 101}, "460b636b899667d54ae3a2654d28fe73a7497cef0e9d0bccae4d683d252f89d1"},
    };
    for (const auto& c : cases) {
        Avatars::AvatarDescriptor descriptor;
        descriptor.catalogVersion = 1;
        descriptor.bodyType = c.body;
        descriptor.heightMillimeters = c.height;
        descriptor.build = c.build;
        descriptor.colors = {{{224, 172, 128}, {74, 48, 30}, {60, 110, 160}, {50, 150, 200}, {60, 80, 180}, {240, 240, 236}, {220, 60, 56}}};
        descriptor.items = c.items;
        const auto model = Avatars::buildAvatarModel(descriptor);
        EXPECT_FALSE(model->catalogUnavailable);
        EXPECT_TRUE(model->substitutedItems.empty());
        EXPECT_EQ(ModelDigest(*model), c.digest) << "body " << int(c.body) << " height " << c.height;
    }
}

TEST(AvatarCatalogTest, EveryEmbeddedAssetMatchesItsHashAndParses) {
    for (const auto& catalog : Avatars::embeddedCatalogs()) {
        for (const auto& [name, asset] : catalog->assets) {
            const auto bytes = Avatars::resolveAsset(*catalog, name);
            ASSERT_TRUE(bytes.has_value()) << "v" << catalog->version << " " << name;
            EXPECT_EQ(bytes->view.size(), asset.size);
            if (name.ends_with(".glb")) {
                EXPECT_NO_THROW((void)Avatars::parseAvatarGlb(bytes->view)) << name;
            }
        }
    }
}

TEST(AvatarCatalogTest, ItemsAreFittedToTheirBody) {
    for (const auto& catalog : Avatars::embeddedCatalogs()) {
    const auto& manifest = *catalog;
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
    const auto& manifest = Avatars::newestEmbeddedManifest();
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
    unknown.items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Top)] = 999;
    const auto substituted = Avatars::buildAvatarModel(unknown);
    ASSERT_EQ(substituted->substitutedItems.size(), 1u);
    EXPECT_EQ(substituted->substitutedItems[0], 999);
}

namespace {
Avatars::AvatarDescriptor catalog2(std::uint8_t body, std::array<std::uint16_t, 6> items)
{
    Avatars::AvatarDescriptor descriptor;
    descriptor.catalogVersion = 2;
    descriptor.bodyType = body;
    descriptor.heightMillimeters = body ? 1800 : 1680;
    descriptor.items = items;
    return descriptor;
}

std::size_t vertices(const Avatars::AvatarModel& model)
{
    std::size_t count = 0;
    for (const auto& part : model.parts) {
        count += part.vertices.size();
    }
    return count;
}
}

TEST(AvatarCatalogTest, CatalogV2DescribesFeaturesFaceControlsAndHatHair) {
    const auto v2 = Avatars::embeddedManifest(2);
    ASSERT_NE(v2, nullptr);
    EXPECT_GE(v2->items.size(), 35u);
    EXPECT_GE(v2->featureItems.size(), 4u);
    for (const auto& feature : v2->featureItems) {
        EXPECT_EQ(feature.slot, Avatars::AvatarItemSlot::FacialHair);
        EXPECT_EQ(feature.randomWeight[0], 0.0f) << feature.name;
    }
    for (int body = 0; body < 2; ++body) {
        ASSERT_EQ(v2->faceControls[body].size(), Avatars::FaceParameterCount);
        for (std::size_t index = 0; index < Avatars::FaceParameterCount; ++index) {
            EXPECT_EQ(v2->faceControls[body][index].parameter, static_cast<int>(index));
            EXPECT_FALSE(v2->faceControls[body][index].deformers.empty());
        }
    }
    int covering = 0;
    for (const auto& item : v2->items) {
        EXPECT_EQ(!item.hatAssets[0].empty(), item.slot == Avatars::AvatarItemSlot::Hair) << item.name;
        covering += item.coversHair;
    }
    EXPECT_GE(covering, 3);
    // v1 has none of it.
    const auto v1 = Avatars::embeddedManifest(1);
    EXPECT_TRUE(v1->featureItems.empty());
    EXPECT_TRUE(v1->faceControls[0].empty() && v1->faceControls[1].empty());
}

TEST(AvatarCatalogTest, FacialHairAndFaceShapeChangeOnlyTheHead) {
    const auto plain = Avatars::buildAvatarModel(catalog2(1, {1, 20, 40, 60, 0, 0}));
    auto bearded = catalog2(1, {1, 20, 40, 60, 0, 0});
    bearded.facialHair = 122;
    const auto withBeard = Avatars::buildAvatarModel(bearded);
    EXPECT_TRUE(withBeard->substitutedItems.empty());
    EXPECT_EQ(withBeard->parts.size(), plain->parts.size() + 1);
    auto wide = catalog2(1, {1, 20, 40, 60, 0, 0});
    wide.face[0] = 255;  // head width
    wide.face[5] = 255;  // eye size
    const auto shaped = Avatars::buildAvatarModel(wide);
    ASSERT_EQ(shaped->parts.size(), plain->parts.size());
    const int head = Avatars::boneIndex("Head");
    int moved = 0, movedOffHead = 0;
    for (std::size_t p = 0; p < plain->parts.size(); ++p) {
        ASSERT_EQ(shaped->parts[p].vertices.size(), plain->parts[p].vertices.size());
        for (std::size_t v = 0; v < plain->parts[p].vertices.size(); ++v) {
            const auto& a = plain->parts[p].vertices[v];
            const auto& b = shaped->parts[p].vertices[v];
            if (Microsoft::Xna::Framework::Vector3::Distance(a.position, b.position) > 1e-5f) {
                ++moved;
                movedOffHead += a.joints[0] != head && a.joints[1] != head && a.joints[2] != head && a.joints[3] != head;
            }
        }
    }
    EXPECT_GT(moved, 1000);
    EXPECT_EQ(movedOffHead, 0);
    // A neutral face is exactly the unshaped avatar.
    auto neutral = catalog2(1, {1, 20, 40, 60, 0, 0});
    EXPECT_EQ(ModelDigest(*Avatars::buildAvatarModel(neutral)), ModelDigest(*plain));
}

TEST(AvatarCatalogTest, HairUnderACoveringHatUsesItsHatVariant) {
    const auto v2 = Avatars::embeddedManifest(2);
    std::uint16_t covering = 0, open = 0;
    for (const auto& item : v2->items) {
        if (item.slot == Avatars::AvatarItemSlot::Hat) {
            (item.coversHair ? covering : open) = item.id;
        }
    }
    ASSERT_NE(covering, 0);
    ASSERT_NE(open, 0);
    const auto bare = Avatars::buildAvatarModel(catalog2(0, {3, 20, 40, 60, 0, 0}));
    const auto capped = Avatars::buildAvatarModel(catalog2(0, {3, 20, 40, 60, 0, covering}));
    const auto banded = Avatars::buildAvatarModel(catalog2(0, {3, 20, 40, 60, 0, open}));
    // The covered style drops what the hat hides; a headband keeps the whole style.
    EXPECT_LT(vertices(*capped), vertices(*banded));
    EXPECT_GT(vertices(*banded), vertices(*bare));
}
