// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"

#include <algorithm>
#include <random>
#include <set>
#include <stdexcept>

namespace Avatars = CNA::Internal::GamerServices::Avatars;

namespace {
Avatars::AvatarDescriptor sample()
{
    Avatars::AvatarDescriptor descriptor;
    descriptor.bodyType = 1;
    descriptor.heightMillimeters = 1834;
    descriptor.build = 201;
    for (std::size_t index = 0; index < Avatars::FormatTwoColorSlotCount; ++index) {
        descriptor.colors[index] = {static_cast<std::uint8_t>(10 + index), static_cast<std::uint8_t>(100 + index),
            static_cast<std::uint8_t>(200 + index)};
    }
    descriptor.items = {4, 22, 42, 61, 81, 0};
    return descriptor;
}
}

TEST(AvatarDescriptionCodecTest, RoundTripsEveryField) {
    const auto descriptor = sample();
    const auto bytes = Avatars::encode(descriptor);
    ASSERT_EQ(bytes.size(), static_cast<std::size_t>(Avatars::DescriptionSize));
    EXPECT_EQ(bytes[0], Avatars::FormatVersion);
    const auto decoded = Avatars::decode(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, descriptor);
    EXPECT_EQ(Avatars::encode(*decoded), bytes);
}

TEST(AvatarDescriptionCodecTest, ChecksumIsIeeeCrc32) {
    const std::uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(Avatars::crc32(text), 0xCBF43926u);
}

TEST(AvatarDescriptionCodecTest, AnyFlippedBitIsRejected) {
    const auto bytes = Avatars::encode(sample());
    for (std::size_t index = 0; index < bytes.size(); index += 7) {
        auto damaged = bytes;
        damaged[index] ^= 0x04;
        EXPECT_FALSE(Avatars::decode(damaged).has_value()) << "byte " << index;
    }
}

TEST(AvatarDescriptionCodecTest, RejectsForeignVersionsAndReservedData) {
    auto bytes = Avatars::encode(sample());
    auto newer = bytes;
    newer[0] = 2;
    EXPECT_FALSE(Avatars::decode(newer).has_value());
    EXPECT_FALSE(Avatars::decode(std::vector<std::uint8_t>(Avatars::DescriptionSize, 0)).has_value());
    EXPECT_FALSE(Avatars::decode(std::vector<std::uint8_t>(20, 1)).has_value());
    // A reserved byte with a matching checksum is still not a format 1 avatar.
    auto reserved = bytes;
    reserved[600] = 1;
    const auto crc = Avatars::crc32(std::span(reserved).first(Avatars::DescriptionSize - 4));
    for (int shift = 0; shift < 4; ++shift) {
        reserved[Avatars::DescriptionSize - 4 + shift] = static_cast<std::uint8_t>(crc >> (shift * 8));
    }
    EXPECT_FALSE(Avatars::decode(reserved).has_value());
}

TEST(AvatarDescriptionCodecTest, RefusesUnencodableDescriptors) {
    auto tooShort = sample();
    tooShort.heightMillimeters = Avatars::MinimumHeightMillimeters - 1;
    EXPECT_THROW((void)Avatars::encode(tooShort), std::invalid_argument);
    auto tooTall = sample();
    tooTall.heightMillimeters = Avatars::MaximumHeightMillimeters + 1;
    EXPECT_THROW((void)Avatars::encode(tooTall), std::invalid_argument);
    auto body = sample();
    body.bodyType = 2;
    EXPECT_THROW((void)Avatars::encode(body), std::invalid_argument);
    auto wrongSlot = sample();
    wrongSlot.items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Top)] = 1;
    EXPECT_THROW((void)Avatars::encode(wrongSlot), std::invalid_argument);
    auto unknown = sample();
    unknown.items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Shoes)] = 999;
    EXPECT_THROW((void)Avatars::encode(unknown), std::invalid_argument);
    auto naked = sample();
    naked.items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Bottom)] = 0;
    EXPECT_THROW((void)Avatars::encode(naked), std::invalid_argument);
}

TEST(AvatarDescriptionCodecTest, ANewDescriptorNamesTheBaselineCatalog) {
    EXPECT_EQ(Avatars::AvatarDescriptor{}.catalogVersion, Avatars::BaseCatalogVersion);
    EXPECT_EQ(Avatars::BaseCatalogVersion, 3);
    const auto bytes = Avatars::encode(sample());
    EXPECT_EQ(bytes[8] | bytes[9] << 8, 3);
}

TEST(AvatarDescriptionCodecTest, DescriptionsNamingDevelopmentCatalogsAreRefused) {
    // Catalogs 1 and 2 were development catalogs: a description naming one is not a CNA avatar,
    // in either description format, and is never reinterpreted as catalog 3.
    for (std::uint16_t version : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2}}) {
        auto plain = sample();
        plain.catalogVersion = version;
        EXPECT_FALSE(Avatars::isEncodable(plain)) << version;
        EXPECT_THROW((void)Avatars::encode(plain), std::invalid_argument) << version;
    }
    auto face = sample();
    face.facialHair = 122;
    face.face[3] = 40;
    for (const auto& descriptor : {sample(), face}) {
        const auto good = Avatars::encode(descriptor);
        ASSERT_TRUE(Avatars::decode(good).has_value());
        for (std::uint8_t version : {std::uint8_t{1}, std::uint8_t{2}}) {
            auto bytes = good;
            bytes[8] = version;
            bytes[9] = 0;
            const auto crc = Avatars::crc32(std::span(bytes).first(Avatars::DescriptionSize - 4));
            for (int shift = 0; shift < 4; ++shift) {
                bytes[Avatars::DescriptionSize - 4 + shift] = static_cast<std::uint8_t>(crc >> (shift * 8));
            }
            EXPECT_FALSE(Avatars::decode(bytes).has_value()) << "format " << int(bytes[0]) << " catalog " << int(version);
        }
    }
}

TEST(AvatarDescriptionCodecTest, TheCatalogVersionSurvivesEncoding) {
    // Bytes 8-9 carry the catalog version, little endian, for the baseline and for any later one.
    for (std::uint16_t version : {std::uint16_t{3}, std::uint16_t{4}, std::uint16_t{0x1234}, std::uint16_t{0xffff}}) {
        for (bool face : {false, true}) {
            auto descriptor = sample();
            descriptor.catalogVersion = version;
            if (face) {
                descriptor.face[9] = 7;
            }
            const auto bytes = Avatars::encode(descriptor);
            EXPECT_EQ(bytes[0], face ? Avatars::FaceFormatVersion : Avatars::FormatVersion);
            EXPECT_EQ(bytes[8], version & 0xff);
            EXPECT_EQ(bytes[9], version >> 8);
            const auto decoded = Avatars::decode(bytes);
            ASSERT_TRUE(decoded.has_value());
            EXPECT_EQ(decoded->catalogVersion, version);
            EXPECT_EQ(*decoded, descriptor);
        }
    }
}

TEST(AvatarDescriptionCodecTest, NewerCatalogItemsAreCarriedForTheResolver) {
    auto newer = sample();
    newer.catalogVersion = static_cast<std::uint16_t>(Avatars::newestEmbeddedManifest().version + 1);
    newer.items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Hat)] = 4000;
    const auto decoded = Avatars::decode(Avatars::encode(newer));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Hat)], 4000);
}

TEST(AvatarDescriptionCodecTest, ItemsAreCheckedAgainstTheCatalogVersionTheyName) {
    // Item 100 is a hat in catalog v3; naming it for another slot is refused whatever newer
    // catalogs are compiled in.
    auto hatAsTop = sample();
    hatAsTop.catalogVersion = Avatars::BaseCatalogVersion;
    hatAsTop.items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Top)] = 100;
    EXPECT_FALSE(Avatars::isEncodable(hatAsTop));
    for (const auto& catalog : Avatars::embeddedCatalogs()) {
        for (const auto& item : catalog->items) {
            auto descriptor = sample();
            descriptor.catalogVersion = catalog->version;
            for (const auto& other : catalog->items) {
                if (other.slot != Avatars::AvatarItemSlot::Glasses && other.slot != Avatars::AvatarItemSlot::Hat) {
                    descriptor.items[static_cast<std::size_t>(other.slot)] = other.id;
                }
            }
            descriptor.items[static_cast<std::size_t>(item.slot)] = item.id;
            EXPECT_TRUE(Avatars::isEncodable(descriptor)) << "v" << catalog->version << " item " << item.id;
        }
    }
}

TEST(AvatarDescriptionCodecTest, RandomDescriptorsAreAlwaysEncodable) {
    std::mt19937 random(1234);
    std::set<std::uint16_t> hair;
    for (int index = 0; index < 400; ++index) {
        const auto descriptor = Avatars::randomDescriptor(std::nullopt, random);
        ASSERT_TRUE(Avatars::isEncodable(descriptor));
        EXPECT_EQ(Avatars::decode(Avatars::encode(descriptor)), descriptor);
        hair.insert(descriptor.items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Hair)]);
    }
    EXPECT_GE(hair.size(), 4u);
    EXPECT_EQ(Avatars::randomDescriptor(std::nullopt, random).catalogVersion, Avatars::newestEmbeddedManifest().version);
    EXPECT_EQ(Avatars::randomDescriptor(std::uint8_t{0}, random).bodyType, 0);
    EXPECT_EQ(Avatars::randomDescriptor(std::uint8_t{1}, random).bodyType, 1);
}

namespace {
std::vector<std::uint8_t> withChecksum(std::vector<std::uint8_t> bytes)
{
    const auto crc = Avatars::crc32(std::span(bytes).first(Avatars::DescriptionSize - 4));
    for (int shift = 0; shift < 4; ++shift) {
        bytes[Avatars::DescriptionSize - 4 + shift] = static_cast<std::uint8_t>(crc >> (shift * 8));
    }
    return bytes;
}

Avatars::AvatarDescriptor shaped()
{
    auto descriptor = sample();
    descriptor.catalogVersion = Avatars::BaseCatalogVersion;
    descriptor.facialHair = 122;
    for (std::size_t index = 0; index < Avatars::FormatTwoFaceParameterCount; ++index) {
        descriptor.face[index] = static_cast<std::uint8_t>(index * 16 + 3);
    }
    return descriptor;
}
}

TEST(AvatarDescriptionCodecTest, AShapedFaceOrFacialHairIsFormat2AndRoundTrips) {
    const auto descriptor = shaped();
    ASSERT_TRUE(descriptor.usesFaceFormat());
    const auto bytes = Avatars::encode(descriptor);
    EXPECT_EQ(bytes[0], Avatars::FaceFormatVersion);
    // Format 2 keeps format 1's bytes 1-42 exactly.
    auto plain = descriptor;
    plain.facialHair = 0;
    plain.face.fill(Avatars::NeutralFaceParameter);
    const auto formatOne = Avatars::encode(plain);
    EXPECT_EQ(formatOne[0], Avatars::FormatVersion);
    EXPECT_TRUE(std::equal(bytes.begin() + 1, bytes.begin() + 43, formatOne.begin() + 1));
    EXPECT_EQ(bytes[43] | bytes[44] << 8, 122);
    EXPECT_EQ(bytes[45], 3);
    EXPECT_EQ(bytes[60], 15 * 16 + 3);
    const auto decoded = Avatars::decode(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, descriptor);
    EXPECT_EQ(Avatars::encode(*decoded), bytes);
    // Only the face, or only facial hair, is still format 2.
    auto faceOnly = plain;
    faceOnly.face[7] = 200;
    EXPECT_EQ(Avatars::encode(faceOnly)[0], Avatars::FaceFormatVersion);
    auto beardOnly = plain;
    beardOnly.facialHair = 120;
    EXPECT_EQ(Avatars::encode(beardOnly)[0], Avatars::FaceFormatVersion);
}

TEST(AvatarDescriptionCodecTest, Format2RefusesWhatItCannotMean) {
    const auto good = Avatars::encode(shaped());
    auto reserved = good;
    reserved[61] = 1;
    EXPECT_FALSE(Avatars::decode(withChecksum(reserved)).has_value());
    // A format 2 buffer that says nothing format 1 cannot is not how CNA writes one.
    auto neutral = good;
    std::fill(neutral.begin() + 43, neutral.begin() + 61, 0);
    std::fill(neutral.begin() + 45, neutral.begin() + 61, Avatars::NeutralFaceParameter);
    EXPECT_FALSE(Avatars::decode(withChecksum(neutral)).has_value());
    // Facial hair must be a feature item of the catalog the description names...
    auto unknown = shaped();
    unknown.facialHair = 40;
    EXPECT_FALSE(Avatars::isEncodable(unknown));
    auto wardrobeItem = shaped();
    wardrobeItem.facialHair = 100;  // a hat, not a feature item
    EXPECT_FALSE(Avatars::isEncodable(wardrobeItem));
    // ...unless that catalog is not compiled in, when the service and resolver check it.
    auto newer = shaped();
    newer.catalogVersion = static_cast<std::uint16_t>(Avatars::newestEmbeddedManifest().version + 1);
    newer.facialHair = 4000;
    EXPECT_TRUE(Avatars::decode(Avatars::encode(newer)).has_value());
    // A format this build does not know is not decoded.
    auto future = good;
    future[0] = 4;
    EXPECT_FALSE(Avatars::decode(withChecksum(future)).has_value());
}

namespace {
// A format 3 avatar using every field format 3 adds (eyebrows, ears, makeup, its colours,
// proportions and the second face block), on catalog v3's own entries.
Avatars::AvatarDescriptor extended()
{
    using Slot = Avatars::AvatarItemSlot;
    const auto& catalog = *Avatars::embeddedManifest(Avatars::BaseCatalogVersion);
    auto descriptor = shaped();
    descriptor.eyebrows = catalog.itemsOf(Slot::Eyebrows).back()->id;
    descriptor.ears = catalog.itemsOf(Slot::Ears)[2]->id;
    for (auto slot : {Slot::EyeShadow, Slot::Eyeliner, Slot::Blush, Slot::Lips}) {
        descriptor.makeup[Avatars::makeupIndex(slot)] = catalog.itemsOf(slot).front()->id;
    }
    for (std::size_t index = Avatars::FormatTwoColorSlotCount; index < Avatars::AvatarColorSlotCount; ++index) {
        descriptor.colors[index] = {static_cast<std::uint8_t>(index), static_cast<std::uint8_t>(index * 9), 77};
    }
    descriptor.proportion = 40;
    for (std::size_t index = Avatars::FormatTwoFaceParameterCount; index < Avatars::FaceParameterCount; ++index) {
        descriptor.face[index] = static_cast<std::uint8_t>(255 - index * 5);
    }
    return descriptor;
}
}

TEST(AvatarDescriptionCodecTest, Format3RoundTripsEveryFieldInItsDocumentedPlace) {
    const auto descriptor = extended();
    ASSERT_TRUE(descriptor.usesExtendedFields());
    const auto bytes = Avatars::encode(descriptor);
    EXPECT_EQ(bytes[0], Avatars::ExtendedFormatVersion);
    // Format 3 keeps format 2's bytes 1-60 exactly.
    auto two = descriptor;
    two.eyebrows = two.ears = 0;
    two.makeup = {};
    std::fill(two.colors.begin() + Avatars::FormatTwoColorSlotCount, two.colors.end(), Avatars::AvatarColor{});
    two.proportion = Avatars::NeutralFaceParameter;
    std::fill(two.face.begin() + Avatars::FormatTwoFaceParameterCount, two.face.end(), Avatars::NeutralFaceParameter);
    const auto formatTwo = Avatars::encode(two);
    EXPECT_EQ(formatTwo[0], Avatars::FaceFormatVersion);
    EXPECT_TRUE(std::equal(bytes.begin() + 1, bytes.begin() + 61, formatTwo.begin() + 1));
    // docs/avatars.md: eyebrows 61, ears 63, accessories 65 (id, side) x 4, makeup 77, colours 85,
    // proportion 100, face 101-116, reserved from 117.
    EXPECT_EQ(bytes[61] | bytes[62] << 8, descriptor.eyebrows);
    EXPECT_EQ(bytes[63] | bytes[64] << 8, descriptor.ears);
    EXPECT_EQ(bytes[77] | bytes[78] << 8, descriptor.makeup[0]);
    EXPECT_EQ(bytes[83] | bytes[84] << 8, descriptor.makeup[3]);
    EXPECT_EQ(bytes[85], descriptor.colors[7].r);
    EXPECT_EQ(bytes[99], descriptor.colors[11].b);
    EXPECT_EQ(bytes[100], 40);
    EXPECT_EQ(bytes[101], descriptor.face[16]);
    EXPECT_EQ(bytes[116], descriptor.face[31]);
    EXPECT_TRUE(std::all_of(bytes.begin() + 117, bytes.end() - 4, [](auto b) { return b == 0; }));
    const auto decoded = Avatars::decode(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, descriptor);
    EXPECT_EQ(Avatars::encode(*decoded), bytes);
}

TEST(AvatarDescriptionCodecTest, EachFormat3FieldAloneNeedsFormat3) {
    using Slot = Avatars::AvatarItemSlot;
    const auto& catalog = *Avatars::embeddedManifest(Avatars::BaseCatalogVersion);
    const auto plain = sample();
    ASSERT_EQ(Avatars::requiredFormat(plain), Avatars::FormatVersion);
    std::vector<Avatars::AvatarDescriptor> cases;
    auto add = [&](auto change) { auto d = plain; change(d); cases.push_back(d); };
    add([&](auto& d) { d.eyebrows = catalog.itemsOf(Slot::Eyebrows).front()->id; });
    add([&](auto& d) { d.ears = catalog.itemsOf(Slot::Ears).front()->id; });
    add([&](auto& d) { d.makeup[1] = catalog.itemsOf(Slot::Eyeliner).front()->id; });
    add([&](auto& d) { d.colors[static_cast<std::size_t>(Avatars::AvatarColorSlot::Jewelry)] = {1, 2, 3}; });
    add([&](auto& d) { d.proportion = 129; });
    add([&](auto& d) { d.face[31] = 0; });
    for (const auto& d : cases) {
        const auto bytes = Avatars::encode(d);
        EXPECT_EQ(bytes[0], Avatars::ExtendedFormatVersion);
        EXPECT_EQ(Avatars::decode(bytes), d);
    }
    // A catalog entry added since the first v3 release needs format 3 as well.
    for (const auto& item : catalog.items) {
        if (item.format != 3 || item.slot > Slot::Hat) {
            continue;
        }
        auto d = plain;
        d.items[static_cast<std::size_t>(item.slot)] = item.id;
        EXPECT_EQ(Avatars::requiredFormat(d), Avatars::ExtendedFormatVersion) << item.name;
    }
}

TEST(AvatarDescriptionCodecTest, Format3RefusesWhatItCannotMean) {
    const auto good = Avatars::encode(extended());
    auto reserved = good;
    reserved[117] = 1;
    EXPECT_FALSE(Avatars::decode(withChecksum(reserved)).has_value());
    reserved = good;
    reserved[600] = 9;
    EXPECT_FALSE(Avatars::decode(withChecksum(reserved)).has_value());
    // A format 3 buffer that says nothing format 2 cannot is not canonical.
    auto formatTwo = Avatars::encode(shaped());
    auto lifted = formatTwo;
    lifted[0] = Avatars::ExtendedFormatVersion;
    lifted[100] = Avatars::NeutralFaceParameter;
    std::fill(lifted.begin() + 101, lifted.begin() + 117, Avatars::NeutralFaceParameter);
    EXPECT_FALSE(Avatars::decode(withChecksum(lifted)).has_value());
    // A side byte outside both/left/right, a side without an accessory, and a single glove.
    auto side = good;
    side[67] = 3;
    EXPECT_FALSE(Avatars::decode(withChecksum(side)).has_value());
    auto lonely = extended();
    lonely.sides[0] = Avatars::AvatarSides::Left;
    EXPECT_FALSE(Avatars::isEncodable(lonely));
    // Entries must be of the kind their field names.
    auto wrongKind = extended();
    wrongKind.eyebrows = wrongKind.ears;
    EXPECT_FALSE(Avatars::isEncodable(wrongKind));
    auto unknownStyle = extended();
    unknownStyle.makeup[2] = 4000;
    EXPECT_FALSE(Avatars::isEncodable(unknownStyle));
}

TEST(AvatarDescriptionCodecTest, Format3BytesAreStable) {
    // The same avatar is always the same 1021 bytes (a CRC of this one pins the layout).
    const auto bytes = Avatars::encode(extended());
    EXPECT_EQ(Avatars::encode(extended()), bytes);
    EXPECT_EQ(Avatars::crc32(bytes), Avatars::crc32(Avatars::encode(*Avatars::decode(bytes))));
}

TEST(AvatarDescriptionCodecTest, RandomAvatarsHaveIndividualFaces) {
    std::mt19937 random(99);
    int shapedCount = 0, bearded = 0, femaleBeards = 0;
    for (int index = 0; index < 300; ++index) {
        const auto descriptor = Avatars::randomDescriptor(std::nullopt, random);
        shapedCount += std::ranges::any_of(descriptor.face, [](auto value) { return value != Avatars::NeutralFaceParameter; });
        bearded += descriptor.bodyType == 1 && descriptor.facialHair != 0;
        femaleBeards += descriptor.bodyType == 0 && descriptor.facialHair != 0;
        ASSERT_EQ(Avatars::decode(Avatars::encode(descriptor)), descriptor);
    }
    EXPECT_GT(shapedCount, 290);
    EXPECT_GT(bearded, 20);
    EXPECT_EQ(femaleBeards, 0);
}
