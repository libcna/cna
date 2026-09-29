// SPDX-License-Identifier: MS-PL
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string_view>
#include <vector>

namespace CNA::Internal::GamerServices::Avatars {
/** @brief Exact public AvatarDescription buffer length. */
inline constexpr int DescriptionSize = 1021;
/** @brief CNA encoding version stored in byte 0; nonzero, so the XNA IsValid rule holds. */
inline constexpr std::uint8_t FormatVersion = 1;
/** @brief Base asset catalog version this build embeds. */
inline constexpr std::uint16_t CatalogVersion = 1;
/** @brief Shortest encodable avatar, feet to top of head. */
inline constexpr std::uint16_t MinimumHeightMillimeters = 1450;
/** @brief Tallest encodable avatar, feet to top of head. */
inline constexpr std::uint16_t MaximumHeightMillimeters = 2050;

/** @brief Wardrobe slots a description selects one catalog item for. */
enum class AvatarItemSlot : std::uint8_t { Hair, Top, Bottom, Shoes, Glasses, Hat };
/** @brief Number of wardrobe slots. */
inline constexpr std::size_t AvatarItemSlotCount = 6;

/** @brief An sRGB color. */
struct AvatarColor {
    /** @brief Red. */
    std::uint8_t r=0;
    /** @brief Green. */
    std::uint8_t g=0;
    /** @brief Blue. */
    std::uint8_t b=0;
    /** @brief Component equality. @param other Color. @return Equal. */
    bool operator==(const AvatarColor& other) const=default;
};

/** @brief Colors a description carries, in encoding order. */
enum class AvatarColorSlot : std::uint8_t { Skin, Hair, Eyes, Top, Bottom, Shoes, Accessory };
/** @brief Number of color slots. */
inline constexpr std::size_t AvatarColorSlotCount = 7;

/** @brief The logical avatar a CNA description encodes. */
struct AvatarDescriptor {
    /** @brief 0 = female, 1 = male (AvatarBodyType values). */
    std::uint8_t bodyType=0;
    /** @brief Feet to top of head. */
    std::uint16_t heightMillimeters=1700;
    /** @brief 0 = slimmest, 255 = heaviest; 128 is the authored body. */
    std::uint8_t build=128;
    /** @brief Catalog version the item ids refer to. */
    std::uint16_t catalogVersion=CatalogVersion;
    /** @brief Colors indexed by AvatarColorSlot. */
    std::array<AvatarColor,AvatarColorSlotCount> colors{};
    /** @brief Catalog item id per AvatarItemSlot; 0 means none (only Glasses and Hat may be none). */
    std::array<std::uint16_t,AvatarItemSlotCount> items{};
    /** @brief Field equality. @param other Descriptor. @return Equal. */
    bool operator==(const AvatarDescriptor& other) const=default;
};

/** @brief One wardrobe item of the embedded v1 catalog. */
struct AvatarCatalogItem {
    /** @brief Stable catalog id. */
    std::uint16_t id;
    /** @brief Slot the item occupies. */
    AvatarItemSlot slot;
    /** @brief Asset name inside the catalog (without body type suffix). */
    std::string_view name;
};

/** @brief Every item of catalog v1, the ids a v1 description may name. @return Items. */
std::span<const AvatarCatalogItem> catalogItems();
/** @brief Finds a v1 catalog item. @param id Item id. @return Item or null. */
const AvatarCatalogItem* findCatalogItem(std::uint16_t id);

/** @brief IEEE CRC-32 of a byte range. @param bytes Input. @return Checksum. */
std::uint32_t crc32(std::span<const std::uint8_t> bytes);

/** @brief Checks field ranges and item/slot agreement for the catalog version the descriptor names.
 * @param descriptor Candidate. @return True when encodable. */
bool isEncodable(const AvatarDescriptor& descriptor);

/** @brief Encodes a descriptor into a public 1021-byte description.
 * @param descriptor Logical avatar; must satisfy isEncodable. @return Buffer.
 * @throws std::invalid_argument for an unencodable descriptor. */
std::vector<std::uint8_t> encode(const AvatarDescriptor& descriptor);

/** @brief Decodes a CNA description; foreign, damaged or newer-format buffers decode to nothing.
 * @param bytes Public buffer. @return Descriptor, or empty when the buffer is not a CNA v1 avatar. */
std::optional<AvatarDescriptor> decode(std::span<const std::uint8_t> bytes);

/** @brief Builds a random catalog v1 avatar. @param bodyType 0/1, or empty for either.
 * @param random Entropy source. @return Encodable descriptor. */
AvatarDescriptor randomDescriptor(std::optional<std::uint8_t> bodyType,std::mt19937& random);

/** @brief Feet-to-head height of an avatar body type as authored. @param bodyType 0/1. @return Millimeters. */
std::uint16_t authoredHeightMillimeters(std::uint8_t bodyType);
}
