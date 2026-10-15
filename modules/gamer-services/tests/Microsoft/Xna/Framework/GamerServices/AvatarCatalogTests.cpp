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

TEST(AvatarCatalogTest, TheBaselineCatalogV3IsTheOnlyCatalogThisReleaseShips) {
    // Catalogs 1 and 2 were development catalogs and are not shipped: v3 is the baseline and, until
    // a v4 is released, the only catalog compiled in.
    EXPECT_EQ(Avatars::BaseCatalogVersion, 3);
    const auto& catalogs = Avatars::embeddedCatalogs();
    ASSERT_EQ(catalogs.size(), 1u);
    EXPECT_EQ(catalogs.front()->version, 3);
    EXPECT_EQ(&Avatars::newestEmbeddedManifest(), catalogs.front().get());
    const auto& files = Avatars::embeddedCatalogFiles();
    EXPECT_EQ(files.size(), catalogs.front()->assets.size() + 1);  // the listed assets and catalog.json
    for (const auto& file : files) {
        EXPECT_TRUE(std::string_view(file.name).starts_with("v3/")) << file.name;
    }
    EXPECT_EQ(Avatars::availableCatalogVersions(), (std::vector<std::uint16_t>{3}));
}

TEST(AvatarCatalogTest, DevelopmentCatalogsBelowTheBaselineAreNeverResolved) {
    for (long long version : {0LL, 1LL, 2LL, 65536LL}) {
        EXPECT_FALSE(Avatars::isSupportedCatalogVersion(version)) << version;
    }
    for (long long version : {3LL, 4LL, 65535LL}) {
        EXPECT_TRUE(Avatars::isSupportedCatalogVersion(version)) << version;
    }
    for (std::uint16_t version : {std::uint16_t{1}, std::uint16_t{2}}) {
        EXPECT_EQ(Avatars::embeddedManifest(version), nullptr);
        EXPECT_EQ(Avatars::installedManifest(version), nullptr);
        EXPECT_EQ(Avatars::catalogManifest(version), nullptr);
        const auto load = Avatars::loadCatalogAsync(version);
        std::lock_guard guard(load->lock);
        EXPECT_TRUE(load->done);
        EXPECT_EQ(load->manifest, nullptr);
    }
    // A description naming one is not drawn as anything, not even the default avatar; its ids are
    // never read against catalog 3, although catalog 3 has every one of them.
    for (std::uint16_t version : {std::uint16_t{1}, std::uint16_t{2}}) {
        Avatars::AvatarDescriptor descriptor;
        descriptor.catalogVersion = version;
        descriptor.items = {3, 22, 42, 61, 80, 101};
        EXPECT_THROW((void)Avatars::buildAvatarModel(descriptor), std::runtime_error) << version;
        const auto load = Avatars::loadAvatarAsync(descriptor);
        std::lock_guard guard(load->lock);
        EXPECT_TRUE(load->done);
        EXPECT_EQ(load->model, nullptr);
        EXPECT_FALSE(load->error.empty());
    }
}

namespace {
// The entries of the first catalog v3 release (2026-09-30): what format 1 and 2 descriptions name.
const std::vector<std::pair<Avatars::AvatarItemSlot, std::vector<std::uint16_t>>>& firstRelease()
{
    using Slot = Avatars::AvatarItemSlot;
    static const std::vector<std::pair<Slot, std::vector<std::uint16_t>>> entries{
        {Slot::Hair, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}},  {Slot::Top, {20, 21, 22, 23, 24, 25, 26, 27, 28}},
        {Slot::Bottom, {40, 41, 42, 43, 44, 45}},      {Slot::Shoes, {60, 61, 62, 63, 64}},
        {Slot::Glasses, {80, 81, 82, 83}},             {Slot::Hat, {100, 101, 102, 103, 104}},
        {Slot::FacialHair, {120, 121, 122, 123}}};
    return entries;
}
}

TEST(AvatarCatalogTest, CatalogV3RevisionsKeepTheFirstReleaseAndAddOnlyFormat3Entries) {
    // Catalog v3 is revised in place: every entry of the first release keeps its id, kind and name,
    // and is still named by format 1 and 2 descriptions; everything added needs format 3, which a
    // reader of only formats 1 and 2 never receives (the service projects it).
    const auto v3 = Avatars::embeddedManifest(3);
    ASSERT_NE(v3, nullptr);
    EXPECT_EQ(v3->revision, 3);
    std::set<std::uint16_t> first;
    for (const auto& [slot, ids] : firstRelease()) {
        for (auto id : ids) {
            first.insert(id);
            const auto* entry = v3->item(id, slot);
            ASSERT_NE(entry, nullptr) << id;
            EXPECT_EQ(entry->format, 1) << id;
        }
    }
    EXPECT_EQ(v3->item(1)->name, "hair_short");
    EXPECT_EQ(v3->item(20)->name, "top_tshirt");
    EXPECT_EQ(v3->item(100)->name, "hat_cap");
    EXPECT_EQ(v3->featureItem(122)->name, "facial_beard");
    for (const auto* list : {&v3->items, &v3->featureItems, &v3->faceStyles}) {
        for (const auto& entry : *list) {
            EXPECT_EQ(entry.format, first.contains(entry.id) ? 1 : 3) << entry.name;
        }
    }
    EXPECT_EQ(v3->authoredHeightMillimeters[0], 1680);
    EXPECT_EQ(v3->authoredHeightMillimeters[1], 1800);
}

TEST(AvatarCatalogTest, CatalogV3HasTheItemFeatureStyleAndAnimationContract) {
    const auto v3 = Avatars::embeddedManifest(3);
    ASSERT_NE(v3, nullptr);
    using Slot = Avatars::AvatarItemSlot;
    for (const auto& feature : v3->itemsOf(Slot::FacialHair)) {
        EXPECT_TRUE(feature->name.starts_with("facial_")) << feature->name;
        // Facial hair is drawn at random for male bodies only.
        EXPECT_EQ(feature->randomWeight[0], 0.0f) << feature->name;
    }
    EXPECT_FALSE(v3->itemsOf(Slot::Ears).empty());
    for (auto slot : {Slot::Eyebrows, Slot::EyeShadow, Slot::Eyeliner, Slot::Blush, Slot::Lips}) {
        EXPECT_FALSE(v3->itemsOf(slot).empty()) << static_cast<int>(slot);
    }
    for (int body = 0; body < 2; ++body) {
        ASSERT_GE(v3->faceControls[body].size(), Avatars::FormatTwoFaceParameterCount);
        for (std::size_t index = 0; index < Avatars::FormatTwoFaceParameterCount; ++index) {
            EXPECT_EQ(v3->faceControls[body][index].parameter, static_cast<int>(index));
            EXPECT_FALSE(v3->faceControls[body][index].deformers.empty());
        }
        EXPECT_FALSE(v3->headSurface[body].radii.empty());
    }
    std::vector<std::uint16_t> covering;
    for (const auto& item : v3->items) {
        if (item.hairCover) {
            EXPECT_EQ(item.slot, Slot::Hat) << item.name;
            covering.push_back(item.id);
        }
        if (item.slot == Slot::Glasses) {
            EXPECT_EQ(item.follow, Avatars::ItemFollow::Eyes) << item.name;
        }
    }
    for (std::uint16_t id : {100, 101, 102, 103}) {
        EXPECT_NE(std::ranges::find(covering, id), covering.end()) << id;
    }
    EXPECT_EQ(std::ranges::find(covering, 104), covering.end());
    EXPECT_EQ(v3->animationsAsset, "animations.glb");
    EXPECT_EQ(v3->faceAsset, "face_features.png");
    EXPECT_EQ(Avatars::clipLibrary().clips.size(), 31u);
}

TEST(AvatarCatalogTest, AManifestMustNameASupportedCatalogVersion) {
    const auto bytes = Avatars::embeddedCatalogFile("v3/catalog.json");
    ASSERT_FALSE(bytes.empty());
    const std::string text(bytes.begin(), bytes.end());
    auto as = [&](int version) {
        auto copy = text;
        const auto at = copy.find("\"catalogVersion\": 3");
        EXPECT_NE(at, std::string::npos);
        copy.replace(at, std::string_view("\"catalogVersion\": 3").size(), "\"catalogVersion\": " + std::to_string(version));
        return copy;
    };
    EXPECT_EQ(Avatars::parseManifest(text).version, 3);
    EXPECT_THROW((void)Avatars::parseManifest(as(1)), std::runtime_error);
    EXPECT_THROW((void)Avatars::parseManifest(as(2)), std::runtime_error);
    EXPECT_THROW((void)Avatars::parseManifest(as(0)), std::runtime_error);
    // A later catalog is a future version, read like the baseline.
    EXPECT_EQ(Avatars::parseManifest(as(4)).version, 4);
}

TEST(AvatarCatalogTest, EmbeddedAssetsResolveByContentNotByName) {
    const auto v3 = Avatars::embeddedManifest(Avatars::BaseCatalogVersion);
    // A manifest naming "body.female.glb" with the male body's contents gets the male body; one
    // whose listed contents are compiled in nowhere gets nothing (no service here).
    auto swapped = *v3;
    swapped.assets.at("body.female.glb") = swapped.assets.at("body.male.glb");
    swapped.assets.at("body.female.glb").name = "body.female.glb";
    const auto male = Avatars::resolveAsset(*v3, "body.male.glb");
    const auto resolved = Avatars::resolveAsset(swapped, "body.female.glb");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_TRUE(std::ranges::equal(resolved->view, male->view));
    auto foreign = *v3;
    foreign.assets.at("hair_bob.female.glb").sha256 = std::string(64, 'a');
    EXPECT_FALSE(Avatars::resolveAsset(foreign, "hair_bob.female.glb").has_value());
}

TEST(AvatarCatalogTest, AnUnavailableCatalogNeverReadsItsIdsAgainstAnotherVersion) {
    // No service is configured, so a version past the compiled-in ones cannot be obtained. Its
    // ids (which happen to exist in v3) must not be looked up in any other version.
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
        put(part.image), put(static_cast<int>(part.feature)), put(part.layer), put(part.tile), put(part.closedTile);
        q(part.alpha, 1e-4), put(part.blend);
    }
    for (const auto& image : model.images) {
        put(image->width), put(image->height);
        bytes.insert(bytes.end(), image->rgba.begin(), image->rgba.end());
    }
    for (const auto& state : model.eyebrowTiles) {
        put(state[0]), put(state[1]);
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

TEST(AvatarCatalogTest, V3DescriptionsAssembleExactlyAsRevised) {
    // A v3 description, format 1 or format 2, draws the same model on every machine: these digests
    // pin catalog v3 revision 3 (a later revision changes them on purpose).
    struct Case {
        std::uint8_t body;
        std::uint16_t height;
        std::uint8_t build;
        std::array<std::uint16_t, 6> items;
        std::uint16_t facialHair;
        std::uint8_t faceValue;
        const char* digest;
    };
    const Case cases[] = {
        {0, 1680, 128, {3, 20, 42, 60, 0, 0}, 0, 128, "1a227a245b0b3ec10745756ac555bc47d66116010644c9cfeb1e9183f7420a7e"},
        {1, 1950, 40, {2, 22, 40, 61, 81, 100}, 0, 128, "ddc02f4284fb4896f417e36f149e7296af58f8b257f0e78022dcacf27d422dfd"},
        {0, 1500, 220, {4, 23, 41, 61, 80, 104}, 0, 128, "df45f7d6021f35a9aaf7a10e3e31093cb89a40129814178028c9cb40fba535cb"},
        {1, 1800, 128, {6, 26, 43, 63, 82, 0}, 122, 200, "157c80076319dde61add4d598e0e40c76ecf851ac01989a5f414d18a861f315d"},
    };
    for (const auto& c : cases) {
        Avatars::AvatarDescriptor descriptor;
        descriptor.catalogVersion = 3;
        descriptor.bodyType = c.body;
        descriptor.heightMillimeters = c.height;
        descriptor.build = c.build;
        descriptor.colors = {{{224, 172, 128}, {74, 48, 30}, {60, 110, 160}, {50, 150, 200}, {60, 80, 180}, {240, 240, 236}, {220, 60, 56}}};
        descriptor.items = c.items;
        descriptor.facialHair = c.facialHair;
        descriptor.face[0] = c.faceValue;  // head width
        descriptor.face[5] = c.faceValue;  // eye size
        ASSERT_EQ(Avatars::encode(descriptor)[0], descriptor.usesFaceFormat() ? Avatars::FaceFormatVersion : Avatars::FormatVersion);
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
        EXPECT_EQ(features, (std::set<std::string>{"", "eyeLeft", "eyeRight", "eyebrowLeft", "eyebrowRight", "mouth", "lips",
                                                    "shadowLeft", "shadowRight", "linerLeft", "linerRight", "blushLeft", "blushRight"}));
        std::vector<Avatars::CatalogItem> models = manifest.items;
        models.insert(models.end(), manifest.featureItems.begin(), manifest.featureItems.end());
        for (const auto& item : models) {
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
    EXPECT_THROW((void)Avatars::parseManifest(R"({"format":1,"rig":"cna-avatar-71","catalogVersion":3,"assets":[{"name":"../x.glb","sha256":"00","size":1}]})"),
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
Avatars::AvatarDescriptor baseline(std::uint8_t body, std::array<std::uint16_t, 6> items)
{
    Avatars::AvatarDescriptor descriptor;
    descriptor.catalogVersion = Avatars::BaseCatalogVersion;
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

TEST(AvatarCatalogTest, FacialHairAndFaceShapeChangeOnlyTheHead) {
    const auto plain = Avatars::buildAvatarModel(baseline(1, {1, 20, 40, 60, 0, 0}));
    auto bearded = baseline(1, {1, 20, 40, 60, 0, 0});
    bearded.facialHair = 122;
    const auto withBeard = Avatars::buildAvatarModel(bearded);
    EXPECT_TRUE(withBeard->substitutedItems.empty());
    // The beard adds exactly its own model's parts (a grown beard and the stubble round it).
    const auto v3 = Avatars::embeddedManifest(Avatars::BaseCatalogVersion);
    const auto beardGlb = Avatars::parseAvatarGlb(Avatars::resolveAsset(*v3, v3->featureItem(122)->assets[1])->view);
    EXPECT_EQ(withBeard->parts.size(), plain->parts.size() + beardGlb.primitives.size());
    auto wide = baseline(1, {1, 20, 40, 60, 0, 0});
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
    auto neutral = baseline(1, {1, 20, 40, 60, 0, 0});
    EXPECT_EQ(ModelDigest(*Avatars::buildAvatarModel(neutral)), ModelDigest(*plain));
}

TEST(AvatarCatalogTest, HairUnderACoveringHatIsFlattenedUnderIt) {
    const auto v3 = Avatars::embeddedManifest(Avatars::BaseCatalogVersion);
    std::uint16_t covering = 0, open = 0;
    for (const auto& item : v3->items) {
        if (item.slot == Avatars::AvatarItemSlot::Hat) {
            (item.hairCover ? covering : open) = item.id;
        }
    }
    ASSERT_NE(covering, 0);
    ASSERT_NE(open, 0);
    auto triangles = [](const Avatars::AvatarModel& model) {
        std::size_t count = 0;
        for (const auto& part : model.parts) {
            count += part.indices.size() / 3;
        }
        return count;
    };
    const auto bare = Avatars::buildAvatarModel(baseline(0, {3, 20, 40, 60, 0, 0}));
    const auto capped = Avatars::buildAvatarModel(baseline(0, {3, 20, 40, 60, 0, covering}));
    const auto banded = Avatars::buildAvatarModel(baseline(0, {3, 20, 40, 60, 0, open}));
    // The covered style drops what the hat hides; a headband keeps the whole style.
    EXPECT_LT(triangles(*capped), triangles(*banded));
    EXPECT_GT(triangles(*banded), triangles(*bare));
}
