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
/** @brief CNA encoding stored in byte 0 for descriptions using anything only format 3 can say:
 * eyebrows, ears, accessories, makeup, proportions, the second face block, the extended colours, or
 * a catalog item that needs format 3. */
inline constexpr std::uint8_t ExtendedFormatVersion = 3;
/** @brief Face-shape bytes a description carries (catalog faceControls give their meaning): the
 * first FormatTwoFaceParameterCount in format 2, all of them in format 3. */
inline constexpr std::size_t FaceParameterCount = 32;
/** @brief Face-shape bytes format 2 carries. */
inline constexpr std::size_t FormatTwoFaceParameterCount = 16;
/** @brief The face-shape byte that leaves a face as authored. */
inline constexpr std::uint8_t NeutralFaceParameter = 128;
/** @brief The baseline avatar catalog: the oldest catalog version CNA supports, compiled into every
 * build. Versions below it were development catalogs that never shipped; a description, manifest or
 * catalog pack naming one is refused, never read against another catalog. Later versions are
 * future catalogs, drawable when compiled in or installed as a pack. */
inline constexpr std::uint16_t BaseCatalogVersion = 3;
/** @brief Whether a catalog version can be supported at all: the baseline or a later version.
 * @param version Catalog version as read (any integer). @return False below the baseline and
 * outside the 16-bit range a description encodes. */
constexpr bool isSupportedCatalogVersion(long long version)
{
    return version >= BaseCatalogVersion && version <= 0xffff;
}
/** @brief Shortest encodable avatar, feet to top of head. */
inline constexpr std::uint16_t MinimumHeightMillimeters = 1450;
/** @brief Tallest encodable avatar, feet to top of head. */
inline constexpr std::uint16_t MaximumHeightMillimeters = 2050;

/** @brief Kinds of catalog entry a description can name. The first six are the wardrobe slots of
 * AvatarDescriptor::items; FacialHair and Ears are feature items (models fitted to the face);
 * Earrings to Rings are accessories (format 3); Eyebrows to Lips are face styles (expression atlas
 * tiles, format 3). The numeric values are part of the service protocol (catalog item slots). */
enum class AvatarItemSlot : std::uint8_t {
    Hair, Top, Bottom, Shoes, Glasses, Hat, FacialHair, Ears, Earrings, Wristwear, Gloves, Rings,
    Eyebrows, EyeShadow, Eyeliner, Blush, Lips
};
/** @brief Number of wardrobe slots (AvatarDescriptor::items). */
inline constexpr std::size_t AvatarItemSlotCount = 6;
/** @brief Number of AvatarItemSlot values. */
inline constexpr std::size_t AvatarItemKindCount = 17;
/** @brief Number of accessory slots (AvatarDescriptor::accessories), Earrings to Rings. */
inline constexpr std::size_t AvatarAccessorySlotCount = 4;
/** @brief Number of makeup slots (AvatarDescriptor::makeup), EyeShadow to Lips. */
inline constexpr std::size_t AvatarMakeupSlotCount = 4;
/** @brief The accessory slot an AvatarItemSlot is, as an index into AvatarDescriptor::accessories.
 * @param slot Earrings, Wristwear, Gloves or Rings. @return Index. */
constexpr std::size_t accessoryIndex(AvatarItemSlot slot)
{
    return static_cast<std::size_t>(slot) - static_cast<std::size_t>(AvatarItemSlot::Earrings);
}
/** @brief The makeup slot an AvatarItemSlot is, as an index into AvatarDescriptor::makeup.
 * @param slot EyeShadow, Eyeliner, Blush or Lips. @return Index. */
constexpr std::size_t makeupIndex(AvatarItemSlot slot)
{
    return static_cast<std::size_t>(slot) - static_cast<std::size_t>(AvatarItemSlot::EyeShadow);
}
/** @brief Which of a pair an accessory is worn on (earrings, wristwear, rings; gloves are always a pair). */
enum class AvatarSides : std::uint8_t { Both, Left, Right };

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

/** @brief Colors a description carries, in encoding order: the first seven in every format, the
 * rest in format 3. */
enum class AvatarColorSlot : std::uint8_t { Skin, Hair, Eyes, Top, Bottom, Shoes, Accessory, Lips, EyeShadow, Blush, Jewelry, Gloves };
/** @brief Number of color slots. */
inline constexpr std::size_t AvatarColorSlotCount = 12;
/** @brief Number of color slots formats 1 and 2 carry. */
inline constexpr std::size_t FormatTwoColorSlotCount = 7;

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
    /** @brief Colors indexed by AvatarColorSlot. A format 3 slot (Lips to Gloves) left black is
     * unset: it is drawn in that slot's natural default (drawnColor), and a description that sets
     * none of them stays in format 1 or 2. */
    std::array<AvatarColor,AvatarColorSlotCount> colors{};
    /** @brief Catalog item id per AvatarItemSlot; 0 means none (only Glasses and Hat may be none). */
    std::array<std::uint16_t,AvatarItemSlotCount> items{};
    /** @brief Facial-hair feature item id of the catalog, 0 for none (format 2). */
    std::uint16_t facialHair=0;
    /** @brief Face-shape parameters, NeutralFaceParameter for the authored face (the first sixteen
     * format 2, the rest format 3). */
    std::array<std::uint8_t,FaceParameterCount> face=[] {
        std::array<std::uint8_t,FaceParameterCount> neutral{};
        neutral.fill(NeutralFaceParameter);
        return neutral;
    }();
    /** @brief Eyebrow face style id, 0 for the catalog's default eyebrows (format 3). */
    std::uint16_t eyebrows=0;
    /** @brief Ear feature item id, 0 for the catalog's default ears (format 3). */
    std::uint16_t ears=0;
    /** @brief Accessory item id per accessory slot (earrings, wristwear, gloves, rings), 0 for none (format 3). */
    std::array<std::uint16_t,AvatarAccessorySlotCount> accessories{};
    /** @brief Which side each accessory is worn on; Both when it has none, and always for gloves (format 3). */
    std::array<AvatarSides,AvatarAccessorySlotCount> sides{};
    /** @brief Makeup face style id per makeup slot (eye shadow, eyeliner, blush, lips), 0 for none (format 3). */
    std::array<std::uint16_t,AvatarMakeupSlotCount> makeup{};
    /** @brief Body proportions, 0 = compact (a larger head, shorter limbs: younger-looking) to 255 =
     * long-limbed; 128 is the authored body (format 3). */
    std::uint8_t proportion=128;
    /** @brief Whether anything needs format 2 or later (facial hair or a shaped face). @return True for format 2 or 3. */
    [[nodiscard]] bool usesFaceFormat() const;
    /** @brief Whether a field only format 3 has is set (catalog items aside: see requiredFormat). @return True for format 3. */
    [[nodiscard]] bool usesExtendedFields() const;
    /** @brief Field equality. @param other Descriptor. @return Equal. */
    bool operator==(const AvatarDescriptor& other) const=default;
};

/** @brief IEEE CRC-32 of a byte range. @param bytes Input. @return Checksum. */
std::uint32_t crc32(std::span<const std::uint8_t> bytes);

/** @brief Checks field ranges, a supported catalog version (isSupportedCatalogVersion), and
 * item/slot agreement when that catalog is compiled in (later versions are checked by the service
 * and the asset resolver). @param descriptor Candidate. @return True when encodable. */
bool isEncodable(const AvatarDescriptor& descriptor);

/** @brief The description format a descriptor is written in: the lowest that can say everything it
 * uses, including catalog items that need format 3 (when the catalog is compiled in).
 * @param descriptor Descriptor. @return 1, 2 or 3. */
std::uint8_t requiredFormat(const AvatarDescriptor& descriptor);

/** @brief Encodes a descriptor into a public 1021-byte description in requiredFormat's format: one
 * canonical byte sequence per descriptor. @param descriptor Logical avatar; must satisfy isEncodable.
 * @return Buffer. @throws std::invalid_argument for an unencodable descriptor. */
std::vector<std::uint8_t> encode(const AvatarDescriptor& descriptor);

/** @brief Decodes a CNA description; foreign, damaged, newer-format or non-canonical buffers (a
 * format higher than its contents need), and descriptions naming a catalog below the baseline,
 * decode to nothing.
 * @param bytes Public buffer. @return Descriptor, or empty when the buffer is not a CNA avatar. */
std::optional<AvatarDescriptor> decode(std::span<const std::uint8_t> bytes);

/** @brief The colors CreateRandom and the avatar editor offer for a slot (a description may carry
 * any RGB value). @param slot Color slot. @return Palette. */
std::span<const AvatarColor> colorPalette(AvatarColorSlot slot);

/** @brief The color an avatar is drawn with for a slot: the description's, or for an unset (black)
 * format 3 slot the first color of its palette. @param descriptor Avatar. @param slot Slot. @return Color. */
AvatarColor drawnColor(const AvatarDescriptor& descriptor,AvatarColorSlot slot);

/** @brief Builds a random avatar from the newest compiled-in catalog. @param bodyType 0/1, or empty
 * for either. @param random Entropy source. @return Encodable descriptor. */
AvatarDescriptor randomDescriptor(std::optional<std::uint8_t> bodyType,std::mt19937& random);
}
