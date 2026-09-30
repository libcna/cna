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
    for (std::size_t index = 0; index < Avatars::AvatarColorSlotCount; ++index) {
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
    // A reserved byte with a matching checksum is still not a v1 avatar.
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

TEST(AvatarDescriptionCodecTest, NewerCatalogItemsAreCarriedForTheResolver) {
    auto newer = sample();
    newer.catalogVersion = static_cast<std::uint16_t>(Avatars::newestEmbeddedManifest().version + 1);
    newer.items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Hat)] = 4000;
    const auto decoded = Avatars::decode(Avatars::encode(newer));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Hat)], 4000);
}

TEST(AvatarDescriptionCodecTest, ItemsAreCheckedAgainstTheCatalogVersionTheyName) {
    // Item 100 is a hat in catalog v1; naming it for another slot is refused whatever newer
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
    descriptor.catalogVersion = 2;
    descriptor.facialHair = 122;
    for (std::size_t index = 0; index < Avatars::FaceParameterCount; ++index) {
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
    const auto v1 = Avatars::encode(plain);
    EXPECT_EQ(v1[0], Avatars::FormatVersion);
    EXPECT_TRUE(std::equal(bytes.begin() + 1, bytes.begin() + 43, v1.begin() + 1));
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
    auto v1Beard = shaped();
    v1Beard.catalogVersion = 1;
    EXPECT_FALSE(Avatars::isEncodable(v1Beard));
    // ...unless that catalog is not compiled in, when the service and resolver check it.
    auto newer = shaped();
    newer.catalogVersion = static_cast<std::uint16_t>(Avatars::newestEmbeddedManifest().version + 1);
    newer.facialHair = 4000;
    EXPECT_TRUE(Avatars::decode(Avatars::encode(newer)).has_value());
    // A format this build does not know is not decoded.
    auto future = good;
    future[0] = 3;
    EXPECT_FALSE(Avatars::decode(withChecksum(future)).has_value());
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
