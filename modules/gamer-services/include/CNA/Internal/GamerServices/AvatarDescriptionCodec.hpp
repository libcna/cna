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
/** @brief CNA encoding stored in byte 0 for descriptions that use only format 1's fields;
 * nonzero, so the XNA IsValid rule holds. */
inline constexpr std::uint8_t FormatVersion = 1;
/** @brief CNA encoding stored in byte 0 for descriptions with facial hair or a shaped face. */
inline constexpr std::uint8_t FaceFormatVersion = 2;
/** @brief Face-shape bytes a format 2 description carries (catalog faceControls give their meaning). */
inline constexpr std::size_t FaceParameterCount = 16;
/** @brief The face-shape byte that leaves a face as authored. */
inline constexpr std::uint8_t NeutralFaceParameter = 128;
/** @brief The first catalog version, compiled into every build for good. */
inline constexpr std::uint16_t BaseCatalogVersion = 1;
/** @brief Shortest encodable avatar, feet to top of head. */
inline constexpr std::uint16_t MinimumHeightMillimeters = 1450;
/** @brief Tallest encodable avatar, feet to top of head. */
inline constexpr std::uint16_t MaximumHeightMillimeters = 2050;

/** @brief Wardrobe slots a description selects one catalog item for; FacialHair is the slot of a
 * catalog's feature items (format 2), not one of the six wardrobe slots. */
enum class AvatarItemSlot : std::uint8_t { Hair, Top, Bottom, Shoes, Glasses, Hat, FacialHair };
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
    std::uint16_t catalogVersion=BaseCatalogVersion;
    /** @brief Colors indexed by AvatarColorSlot. */
    std::array<AvatarColor,AvatarColorSlotCount> colors{};
    /** @brief Catalog item id per AvatarItemSlot; 0 means none (only Glasses and Hat may be none). */
    std::array<std::uint16_t,AvatarItemSlotCount> items{};
    /** @brief Facial-hair feature item id of the catalog, 0 for none (format 2). */
    std::uint16_t facialHair=0;
    /** @brief Face-shape parameters, NeutralFaceParameter for the authored face (format 2). */
    std::array<std::uint8_t,FaceParameterCount> face=[] {
        std::array<std::uint8_t,FaceParameterCount> neutral{};
        neutral.fill(NeutralFaceParameter);
        return neutral;
    }();
    /** @brief Whether anything needs format 2 (facial hair or a shaped face). @return True for format 2. */
    [[nodiscard]] bool usesFaceFormat() const;
    /** @brief Field equality. @param other Descriptor. @return Equal. */
    bool operator==(const AvatarDescriptor& other) const=default;
};

/** @brief IEEE CRC-32 of a byte range. @param bytes Input. @return Checksum. */
std::uint32_t crc32(std::span<const std::uint8_t> bytes);

/** @brief Checks field ranges, and item/slot agreement when the catalog version the descriptor
 * names is compiled in (other versions are checked by the service and the asset resolver).
 * @param descriptor Candidate. @return True when encodable. */
bool isEncodable(const AvatarDescriptor& descriptor);

/** @brief Encodes a descriptor into a public 1021-byte description: format 1 when it uses only
 * format 1's fields, else format 2. @param descriptor Logical avatar; must satisfy isEncodable. @return Buffer.
 * @throws std::invalid_argument for an unencodable descriptor. */
std::vector<std::uint8_t> encode(const AvatarDescriptor& descriptor);

/** @brief Decodes a CNA description; foreign, damaged or newer-format buffers decode to nothing.
 * @param bytes Public buffer. @return Descriptor, or empty when the buffer is not a CNA avatar. */
std::optional<AvatarDescriptor> decode(std::span<const std::uint8_t> bytes);

/** @brief Builds a random avatar from the newest compiled-in catalog. @param bodyType 0/1, or empty
 * for either. @param random Entropy source. @return Encodable descriptor. */
AvatarDescriptor randomDescriptor(std::optional<std::uint8_t> bodyType,std::mt19937& random);
}
