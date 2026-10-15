// SPDX-License-Identifier: MS-PL
// The content of CNA's avatar catalog v3 (revision 2): every model is sound geometry, every
// reference of the manifest resolves to what the editor and the random generator expect, and what
// an avatar wears is assembled where and as its description says.
#include <gtest/gtest.h>

#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <string>
#include <iostream>
#include <tuple>

namespace Avatars = CNA::Internal::GamerServices::Avatars;
using Microsoft::Xna::Framework::Vector3;
using Avatars::AvatarItemSlot;

namespace {
std::shared_ptr<const Avatars::CatalogManifest> catalog()
{
    return Avatars::embeddedManifest(Avatars::BaseCatalogVersion);
}

Avatars::AvatarDescriptor dressed(std::uint8_t body)
{
    Avatars::AvatarDescriptor descriptor;
    descriptor.catalogVersion = Avatars::BaseCatalogVersion;
    descriptor.bodyType = body;
    descriptor.heightMillimeters = body ? 1800 : 1680;
    descriptor.items = {1, 20, 40, 60, 0, 0};
    return descriptor;
}

std::uint16_t first(AvatarItemSlot slot)
{
    const auto entries = catalog()->itemsOf(slot);
    return entries.empty() ? 0 : entries.front()->id;
}

struct Totals {
    std::size_t vertices = 0;
    double sumX = 0.0;
};

Totals totals(const Avatars::AvatarModel& model)
{
    Totals out;
    for (const auto& part : model.parts) {
        out.vertices += part.vertices.size();
        for (const auto& vertex : part.vertices) {
            out.sumX += vertex.position.X;
        }
    }
    return out;
}

int countParts(const Avatars::AvatarModel& model, Avatars::AvatarFeature feature)
{
    return static_cast<int>(std::ranges::count_if(model.parts, [&](const auto& part) { return part.feature == feature; }));
}
}

TEST(AvatarCatalogContentTest, EveryModelIsSoundGeometry) {
    // Finite positions inside an avatar's reach, unit normals, finite texture coordinates,
    // non-negative weights summing to one on real bones, in-range indices and next to no
    // degenerate triangles: what every renderer can draw without special cases.
    const auto manifest = catalog();
    int models = 0;
    for (const auto& [name, asset] : manifest->assets) {
        if (!name.ends_with(".glb")) {
            continue;
        }
        const auto glb = Avatars::parseAvatarGlb(Avatars::resolveAsset(*manifest, name)->view);
        if (name == manifest->animationsAsset) {
            EXPECT_EQ(glb.clips.size(), 31u);
            continue;
        }
        ++models;
        ASSERT_FALSE(glb.primitives.empty()) << name;
        for (const auto& primitive : glb.primitives) {
            ASSERT_FALSE(primitive.vertices.empty()) << name;
            ASSERT_FALSE(primitive.indices.empty()) << name;
            ASSERT_EQ(primitive.indices.size() % 3, 0u) << name;
            for (const auto index : primitive.indices) {
                ASSERT_LT(index, primitive.vertices.size()) << name;
            }
            for (const auto& vertex : primitive.vertices) {
                const auto& p = vertex.position;
                ASSERT_TRUE(std::isfinite(p.X) && std::isfinite(p.Y) && std::isfinite(p.Z)) << name;
                ASSERT_TRUE(std::abs(p.X) < 1.2f && std::abs(p.Z) < 1.2f && p.Y > -0.05f && p.Y < 2.3f) << name;
                ASSERT_NEAR(vertex.normal.Length(), 1.0f, 0.02f) << name;
                ASSERT_TRUE(std::isfinite(vertex.uv.X) && std::isfinite(vertex.uv.Y) && std::abs(vertex.uv.X) <= 16.0f &&
                            std::abs(vertex.uv.Y) <= 16.0f)
                    << name;
                const auto& w = vertex.weights;
                ASSERT_TRUE(w.X >= 0.0f && w.Y >= 0.0f && w.Z >= 0.0f && w.W >= 0.0f) << name;
                ASSERT_NEAR(w.X + w.Y + w.Z + w.W, 1.0f, 1e-3f) << name;
                for (const auto joint : vertex.joints) {
                    ASSERT_LT(joint, Avatars::BoneCount) << name;
                }
            }
            std::size_t degenerate = 0;
            for (std::size_t t = 0; t < primitive.indices.size(); t += 3) {
                const auto& a = primitive.vertices[primitive.indices[t]].position;
                const auto& b = primitive.vertices[primitive.indices[t + 1]].position;
                const auto& c = primitive.vertices[primitive.indices[t + 2]].position;
                degenerate += Vector3::Cross(b - a, c - a).Length() < 1e-10f;
            }
            EXPECT_LE(degenerate * 50, primitive.indices.size() / 3) << name << " has " << degenerate << " degenerate triangles";
        }
    }
    EXPECT_GT(models, 300);
}

TEST(AvatarCatalogContentTest, TheCatalogOffersAWardrobeInEveryKind) {
    // The breadth catalog v3 revision 2 ships, per editor category.
    const std::map<AvatarItemSlot, std::size_t> least{
        {AvatarItemSlot::Hair, 35}, {AvatarItemSlot::Top, 28}, {AvatarItemSlot::Bottom, 17}, {AvatarItemSlot::Shoes, 14},
        {AvatarItemSlot::Glasses, 13}, {AvatarItemSlot::Hat, 15}, {AvatarItemSlot::FacialHair, 16}, {AvatarItemSlot::Ears, 6},
        {AvatarItemSlot::Earrings, 6}, {AvatarItemSlot::Wristwear, 6}, {AvatarItemSlot::Gloves, 4}, {AvatarItemSlot::Rings, 3},
        {AvatarItemSlot::Eyebrows, 12}, {AvatarItemSlot::EyeShadow, 4}, {AvatarItemSlot::Eyeliner, 3}, {AvatarItemSlot::Blush, 2},
        {AvatarItemSlot::Lips, 3}};
    const auto manifest = catalog();
    for (const auto& [slot, count] : least) {
        EXPECT_GE(manifest->itemsOf(slot).size(), count) << static_cast<int>(slot);
    }
    EXPECT_GE(manifest->outfits.size(), 10u);
    EXPECT_GE(manifest->facePresets.size(), 50u);
    EXPECT_EQ(manifest->faceControls[0].size(), Avatars::FaceParameterCount);
    EXPECT_EQ(manifest->faceControls[1].size(), Avatars::FaceParameterCount);
}

TEST(AvatarCatalogContentTest, EveryManifestReferenceResolves) {
    const auto manifest = catalog();
    std::set<std::string> names;
    std::vector<const Avatars::CatalogItem*> entries;
    for (const auto* list : {&manifest->items, &manifest->featureItems, &manifest->faceStyles}) {
        for (const auto& entry : *list) {
            entries.push_back(&entry);
        }
    }
    for (const auto* entry : entries) {
        // Names are unique; the editor shows every entry by its label.
        EXPECT_TRUE(names.insert(entry->name).second) << entry->name;
        EXPECT_FALSE(entry->label.empty()) << entry->name;
        // What the first release named keeps format 1; every kind format 3 introduced is format 3.
        const bool extendedKind = entry->slot == AvatarItemSlot::Ears || entry->slot >= AvatarItemSlot::Earrings;
        if (extendedKind) {
            EXPECT_EQ(entry->format, Avatars::ExtendedFormatVersion) << entry->name;
        }
        // Compatibility rules are data: glasses ride on the eyes, earrings hang from the ears,
        // gloves hide rings, hats say how they cover hair.
        if (entry->slot == AvatarItemSlot::Glasses) {
            EXPECT_EQ(entry->follow, Avatars::ItemFollow::Eyes) << entry->name;
        }
        if (entry->slot == AvatarItemSlot::Earrings) {
            EXPECT_EQ(entry->follow, Avatars::ItemFollow::Ears) << entry->name;
        }
        if (entry->slot == AvatarItemSlot::Gloves) {
            EXPECT_EQ(entry->hides, std::vector<AvatarItemSlot>{AvatarItemSlot::Rings}) << entry->name;
        } else {
            EXPECT_TRUE(entry->hides.empty()) << entry->name;
        }
        EXPECT_EQ(entry->hairCover.has_value() && entry->slot != AvatarItemSlot::Hat, false) << entry->name;
    }
    const auto hats = manifest->itemsOf(AvatarItemSlot::Hat);
    EXPECT_GE(std::ranges::count_if(hats, [](const auto* hat) { return hat->hairCover.has_value(); }), 12);
    // Every required kind has a choice the random generator draws for each body.
    for (const auto slot : {AvatarItemSlot::Hair, AvatarItemSlot::Top, AvatarItemSlot::Bottom, AvatarItemSlot::Shoes}) {
        for (int body = 0; body < 2; ++body) {
            const auto items = manifest->itemsOf(slot);
            EXPECT_GE(std::ranges::count_if(items, [&](const auto* item) { return item->randomWeight[body] > 0.0f; }), 8)
                << static_cast<int>(slot) << " body " << body;
        }
    }
    // Outfits dress an avatar completely from the catalog.
    std::set<std::string> outfits;
    for (const auto& outfit : manifest->outfits) {
        EXPECT_TRUE(outfits.insert(outfit.name).second) << outfit.name;
        for (std::uint8_t body = 0; body < 2; ++body) {
            auto descriptor = dressed(body);
            for (const auto& [slot, id] : outfit.items) {
                if (static_cast<std::size_t>(slot) < Avatars::AvatarItemSlotCount) {
                    if (id || slot == AvatarItemSlot::Hat || slot == AvatarItemSlot::Glasses) {
                        descriptor.items[static_cast<std::size_t>(slot)] = id;
                    }
                } else if (slot >= AvatarItemSlot::Earrings && slot <= AvatarItemSlot::Rings) {
                    descriptor.accessories[Avatars::accessoryIndex(slot)] = id;
                }
            }
            for (const auto& [slot, color] : outfit.colors) {
                descriptor.colors[static_cast<std::size_t>(slot)] = color;
            }
            ASSERT_TRUE(Avatars::isEncodable(descriptor)) << outfit.name;
            EXPECT_TRUE(Avatars::buildAvatarModel(descriptor)->substitutedItems.empty()) << outfit.name;
        }
    }
    // Presets set controls that exist, or the body's height, build and proportions.
    for (const auto& preset : manifest->facePresets) {
        for (const auto& [control, amount] : preset.values) {
            const bool body = preset.group == "body" && (control == "height" || control == "build" || control == "proportion");
            EXPECT_TRUE(body || (manifest->faceParameter(0, control) >= 0 && manifest->faceParameter(1, control) >= 0))
                << preset.group << "/" << preset.name << " sets " << control;
        }
    }
}

TEST(AvatarCatalogContentTest, AccessoriesAreWornOnTheSideTheDescriptionNames) {
    // The avatar's left is +X (XNA avatars face -Z).
    for (const auto slot : {AvatarItemSlot::Earrings, AvatarItemSlot::Wristwear, AvatarItemSlot::Rings}) {
        const auto index = Avatars::accessoryIndex(slot);
        auto descriptor = dressed(0);
        const auto bare = totals(*Avatars::buildAvatarModel(descriptor));
        descriptor.accessories[index] = first(slot);
        ASSERT_NE(descriptor.accessories[index], 0);
        const auto both = totals(*Avatars::buildAvatarModel(descriptor));
        descriptor.sides[index] = Avatars::AvatarSides::Left;
        const auto left = totals(*Avatars::buildAvatarModel(descriptor));
        descriptor.sides[index] = Avatars::AvatarSides::Right;
        const auto right = totals(*Avatars::buildAvatarModel(descriptor));
        EXPECT_GT(left.vertices, bare.vertices) << static_cast<int>(slot);
        EXPECT_GT(right.vertices, bare.vertices) << static_cast<int>(slot);
        EXPECT_EQ(both.vertices - bare.vertices, (left.vertices - bare.vertices) + (right.vertices - bare.vertices)) << static_cast<int>(slot);
        EXPECT_GT(left.sumX - bare.sumX, 0.0) << static_cast<int>(slot);
        EXPECT_LT(right.sumX - bare.sumX, 0.0) << static_cast<int>(slot);
    }
}

TEST(AvatarCatalogContentTest, GlovesHideRings) {
    auto descriptor = dressed(1);
    descriptor.accessories[Avatars::accessoryIndex(AvatarItemSlot::Gloves)] = first(AvatarItemSlot::Gloves);
    const auto gloved = totals(*Avatars::buildAvatarModel(descriptor));
    descriptor.accessories[Avatars::accessoryIndex(AvatarItemSlot::Rings)] = first(AvatarItemSlot::Rings);
    EXPECT_EQ(totals(*Avatars::buildAvatarModel(descriptor)).vertices, gloved.vertices);
    descriptor.accessories[Avatars::accessoryIndex(AvatarItemSlot::Gloves)] = 0;
    const auto bare = totals(*Avatars::buildAvatarModel(dressed(1)));
    EXPECT_GT(totals(*Avatars::buildAvatarModel(descriptor)).vertices, bare.vertices);
}

TEST(AvatarCatalogContentTest, MakeupDrawsAsFaceDecalsOfItsStyle) {
    const auto plain = Avatars::buildAvatarModel(dressed(0));
    EXPECT_EQ(countParts(*plain, Avatars::AvatarFeature::Static), 0);
    EXPECT_EQ(countParts(*plain, Avatars::AvatarFeature::Lips), 0);
    auto descriptor = dressed(0);
    for (const auto slot : {AvatarItemSlot::EyeShadow, AvatarItemSlot::Eyeliner, AvatarItemSlot::Blush, AvatarItemSlot::Lips}) {
        descriptor.makeup[Avatars::makeupIndex(slot)] = first(slot);
    }
    const auto madeUp = Avatars::buildAvatarModel(descriptor);
    EXPECT_TRUE(madeUp->substitutedItems.empty());
    // Shadow, liner and blush on each side; lips follow the mouth's expression.
    EXPECT_EQ(countParts(*madeUp, Avatars::AvatarFeature::Static), 6);
    EXPECT_EQ(countParts(*madeUp, Avatars::AvatarFeature::Lips), 1);
    const auto* lips = catalog()->item(first(AvatarItemSlot::Lips), AvatarItemSlot::Lips);
    for (const auto& part : madeUp->parts) {
        if (part.feature == Avatars::AvatarFeature::Lips) {
            EXPECT_FLOAT_EQ(part.alpha, lips->opacity);
        }
        if (part.feature == Avatars::AvatarFeature::Static) {
            EXPECT_GE(part.tile, 0);
        }
    }
    // Eyeliner has its own tile for a closed eye; the other makeup does not change with the eyes.
    EXPECT_EQ(std::ranges::count_if(madeUp->parts, [](const auto& part) { return part.closedTile >= 0; }), 2);
    for (std::size_t tile = 0; tile < 14; ++tile) {
        EXPECT_EQ(madeUp->lipTiles[tile], lips->tiles[tile]);
    }
}

TEST(AvatarCatalogContentTest, EyebrowStylesAndEarShapesChangeOnlyTheHead) {
    const auto plain = Avatars::buildAvatarModel(dressed(1));
    auto browed = dressed(1);
    const auto styles = catalog()->itemsOf(AvatarItemSlot::Eyebrows);
    browed.eyebrows = styles.back()->id;
    const auto withBrows = Avatars::buildAvatarModel(browed);
    EXPECT_NE(withBrows->eyebrowTiles, plain->eyebrowTiles);
    EXPECT_EQ(withBrows->eyebrowTiles[0][0], styles.back()->tiles[0]);
    const auto shapes = catalog()->itemsOf(AvatarItemSlot::Ears);
    ASSERT_GE(shapes.size(), 2u);
    auto eared = dressed(1);
    eared.ears = shapes.back()->id;
    const auto withEars = Avatars::buildAvatarModel(eared);
    EXPECT_TRUE(withEars->substitutedItems.empty());
    const int head = Avatars::boneIndex("Head");
    // The other ears replace the default ones; everything not on the head is untouched.
    auto split = [&](const Avatars::AvatarModel& model) {
        std::multiset<std::tuple<float, float, float>> offHead;
        double onHead = 0.0;
        for (const auto& part : model.parts) {
            for (const auto& vertex : part.vertices) {
                if (std::ranges::none_of(vertex.joints, [&](std::uint8_t joint) { return joint == head; })) {
                    offHead.emplace(vertex.position.X, vertex.position.Y, vertex.position.Z);
                } else {
                    onHead += std::abs(vertex.position.X) + vertex.position.Y + std::abs(vertex.position.Z);
                }
            }
        }
        return std::pair{offHead, onHead};
    };
    const auto [plainBody, plainHead] = split(*plain);
    const auto [earedBody, earedHead] = split(*withEars);
    EXPECT_EQ(earedBody, plainBody);
    EXPECT_NE(earedHead, plainHead);
}

TEST(AvatarCatalogContentTest, ProportionsReshapeTheBodyAtTheSameHeight) {
    const int hips = Avatars::boneIndex("BackLower"), head = Avatars::boneIndex("Head");
    auto at = [&](std::uint8_t proportion) {
        auto descriptor = dressed(0);
        descriptor.proportion = proportion;
        return Avatars::buildAvatarModel(descriptor);
    };
    const auto compact = at(0), neutral = at(Avatars::NeutralFaceParameter), long_ = at(255);
    for (const auto* model : {compact.get(), neutral.get(), long_.get()}) {
        EXPECT_FLOAT_EQ(model->height, 1.68f);
    }
    // Longer legs lift the hips; a compact figure has relatively shorter legs and a larger head.
    EXPECT_LT(compact->bindPositions[hips].Y, neutral->bindPositions[hips].Y);
    EXPECT_GT(long_->bindPositions[hips].Y, neutral->bindPositions[hips].Y);
    EXPECT_LT(compact->bindPositions[head].Y, long_->bindPositions[head].Y);
}

TEST(AvatarCatalogContentTest, RandomAvatarsAreRepeatableVariedAndBelievable) {
    const auto manifest = catalog();
    std::mt19937 a(1234), b(1234);
    for (int round = 0; round < 50; ++round) {
        EXPECT_EQ(Avatars::encode(Avatars::randomDescriptor(*manifest, std::nullopt, a)),
                  Avatars::encode(Avatars::randomDescriptor(*manifest, std::nullopt, b)));
    }
    std::mt19937 random(99);
    std::array<std::set<std::uint16_t>, 2> hair, tops;
    std::array<int, 2> bearded{}, madeUp{}, gloved{}, count{};
    for (int round = 0; round < 1000; ++round) {
        const auto body = static_cast<std::uint8_t>(round % 2);
        const auto descriptor = Avatars::randomDescriptor(*manifest, body, random);
        ASSERT_TRUE(Avatars::isEncodable(descriptor));
        ASSERT_EQ(descriptor.bodyType, body);
        ++count[body];
        hair[body].insert(descriptor.items[0]);
        tops[body].insert(descriptor.items[1]);
        bearded[body] += descriptor.facialHair != 0;
        madeUp[body] += std::ranges::any_of(descriptor.makeup, [](std::uint16_t id) { return id != 0; });
        gloved[body] += descriptor.accessories[Avatars::accessoryIndex(AvatarItemSlot::Gloves)] != 0;
        // Natural colours: skin and eyes always come from their palettes.
        const auto skin = Avatars::colorPalette(Avatars::AvatarColorSlot::Skin);
        EXPECT_NE(std::ranges::find(skin, descriptor.colors[0]), skin.end());
    }
    for (int body = 0; body < 2; ++body) {
        EXPECT_GE(hair[body].size(), 15u) << body;
        EXPECT_GE(tops[body].size(), 15u) << body;
        // Gloves are for an occasion, not for everyone.
        EXPECT_LT(gloved[body] * 10, count[body]) << body;
    }
    // Facial hair follows the catalog's weights (male bodies); makeup is for anyone, more often
    // on female bodies.
    EXPECT_EQ(bearded[0], 0);
    EXPECT_GT(bearded[1] * 5, count[1]);
    EXPECT_GT(madeUp[0], madeUp[1]);
}

TEST(AvatarCatalogContentTest, AFormat3DescriptionIsAnOrdinaryXnaAvatarDescription) {
    // What XNA code sees of an avatar with everything format 3 says: a valid 1021-byte
    // description of its body type and height, whose bytes come back as they went in.
    auto descriptor = dressed(1);
    descriptor.eyebrows = first(AvatarItemSlot::Eyebrows);
    descriptor.ears = first(AvatarItemSlot::Ears);
    descriptor.accessories[Avatars::accessoryIndex(AvatarItemSlot::Earrings)] = first(AvatarItemSlot::Earrings);
    descriptor.sides[Avatars::accessoryIndex(AvatarItemSlot::Earrings)] = Avatars::AvatarSides::Right;
    descriptor.makeup[Avatars::makeupIndex(AvatarItemSlot::Eyeliner)] = first(AvatarItemSlot::Eyeliner);
    descriptor.proportion = 170;
    descriptor.face[20] = 40;
    const auto bytes = Avatars::encode(descriptor);
    ASSERT_EQ(bytes[0], Avatars::ExtendedFormatVersion);
    Microsoft::Xna::Framework::GamerServices::AvatarDescription description(std::vector<SharpRuntime::bytecs>(bytes.begin(), bytes.end()));
    EXPECT_TRUE(description.getIsValidProperty());
    EXPECT_EQ(description.getBodyTypeProperty(), Microsoft::Xna::Framework::GamerServices::AvatarBodyType::Male);
    EXPECT_FLOAT_EQ(description.getHeightProperty(), 1.8f);
    const auto back = description.getDescriptionProperty();
    EXPECT_TRUE(std::equal(back.begin(), back.end(), bytes.begin(), bytes.end()));
}

TEST(AvatarCatalogContentTest, EveryModelMovesSoundlyInEveryAnimation) {
    // Every body, item, feature item and accessory skinned by every preset (at a third and two
    // thirds of it): positions stay finite and with the avatar, and no triangle is torn apart by
    // its weights. A weight on a wrong bone tears an edge to ten times its length and more; the
    // outer side of a sharply bent elbow in a thick garment reaches about three times by geometry
    // alone (2026-10-15: 3.3, a puffer jacket), so an edge of a centimetre or more may not pass
    // four times its bind length. Millimetre edges at hems are left out: tripling them shows nothing.
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Quaternion;
    const auto manifest = catalog();
    const auto& library = Avatars::clipLibrary();
    const auto& parents = Avatars::parentBones();
    std::vector<std::string> models{manifest->bodies[0], manifest->bodies[1]};
    for (const auto* list : {&manifest->items, &manifest->featureItems}) {
        for (const auto& entry : *list) {
            models.push_back(entry.assets[0]);
            models.push_back(entry.assets[1]);
        }
    }
    float worstStretch = 0.0f;
    std::string worstAt;
    for (const auto& name : models) {
        const auto glb = Avatars::parseAvatarGlb(Avatars::resolveAsset(*manifest, name)->view);
        std::array<Vector3, Avatars::BoneCount> bind{};
        for (int bone = 0; bone < Avatars::BoneCount; ++bone) {
            bind[bone] = (parents[bone] < 0 ? Vector3::Zero : bind[parents[bone]]) + glb.bindTranslations[bone];
        }
        for (std::size_t preset = 0; preset < library.clips.size(); ++preset) {
            for (const double at : {1.0 / 3.0, 2.0 / 3.0}) {
                std::array<Quaternion, Avatars::BoneCount> rotations;
                Vector3 root;
                Avatars::sampleClip(library.clips[preset], library.clips[preset].duration * at, rotations, root);
                std::array<Matrix, Avatars::BoneCount> world, skin;
                for (int bone = 0; bone < Avatars::BoneCount; ++bone) {
                    const Matrix local = Matrix::CreateFromQuaternion(rotations[bone]) *
                                         Matrix::CreateTranslation(bone == 0 ? root : glb.bindTranslations[bone]);
                    world[bone] = parents[bone] < 0 ? local : local * world[parents[bone]];
                    skin[bone] = Matrix::CreateTranslation(-bind[bone]) * world[bone];
                }
                for (const auto& primitive : glb.primitives) {
                    std::vector<Vector3> posed(primitive.vertices.size());
                    for (std::size_t v = 0; v < primitive.vertices.size(); ++v) {
                        const auto& vertex = primitive.vertices[v];
                        const float weights[4]{vertex.weights.X, vertex.weights.Y, vertex.weights.Z, vertex.weights.W};
                        Vector3 p = Vector3::Zero;
                        for (int k = 0; k < 4; ++k) {
                            p += Vector3::Transform(vertex.position, skin[vertex.joints[k]]) * weights[k];
                        }
                        posed[v] = p;
                        const auto offset = p - root;
                        ASSERT_TRUE(std::isfinite(p.X) && std::isfinite(p.Y) && std::isfinite(p.Z)) << name;
                        ASSERT_TRUE(std::abs(offset.X) < 1.6f && std::abs(offset.Z) < 1.6f && p.Y > -0.5f && p.Y < 2.6f)
                            << name << " leaves the avatar in " << library.clips[preset].name;
                    }
                    for (std::size_t t = 0; t < primitive.indices.size(); t += 3) {
                        for (int e = 0; e < 3; ++e) {
                            const auto a = primitive.indices[t + e], b = primitive.indices[t + (e + 1) % 3];
                            const float rest = Vector3::Distance(primitive.vertices[a].position, primitive.vertices[b].position);
                            if (rest < 0.01f) continue;
                            const float stretch = Vector3::Distance(posed[a], posed[b]) / rest;
                            if (stretch > worstStretch) {
                                worstStretch = stretch;
                                worstAt = name + " in " + library.clips[preset].name;
                            }
                        }
                    }
                }
            }
        }
    }
    EXPECT_LT(worstStretch, 4.0f) << worstAt;
    std::cout << "worst edge stretch " << worstStretch << " (" << worstAt << ")\n";
}

TEST(AvatarCatalogContentTest, ModelsShareTheTexturesTheyHaveInCommon) {
    // Two avatars in different clothes decode each texture they share (skin, hair, fabrics) once.
    auto a = dressed(0), b = dressed(1);
    b.items = {3, 22, 42, 61, 0, 0};
    const auto first = Avatars::buildAvatarModel(a), second = Avatars::buildAvatarModel(b);
    std::set<const Avatars::AvatarImage*> mine;
    for (const auto& image : first->images) mine.insert(image.get());
    int shared = 0;
    for (const auto& image : second->images) shared += mine.contains(image.get());
    EXPECT_GT(shared, 0);
}
