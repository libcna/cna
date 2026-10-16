// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include <algorithm>
#include <stdexcept>

namespace CNA::Internal::GamerServices::Avatars {
namespace {
// Format 1 layout; format 2 keeps it byte for byte and adds the facial-hair id and the first face
// block after the item ids; format 3 keeps format 2 and adds eyebrows, ears, accessories, makeup,
// five colours, proportions and the second face block. Everything after the format's last field up
// to the checksum is reserved and must be zero, so a later format can use it only by changing byte 0.
constexpr std::size_t MagicOffset=1, BodyOffset=4, HeightOffset=5, BuildOffset=7, CatalogOffset=8;
constexpr std::size_t ColorOffset=10, ItemOffset=ColorOffset+FormatTwoColorSlotCount*3;
constexpr std::size_t ReservedOffset=ItemOffset+AvatarItemSlotCount*2, ChecksumOffset=DescriptionSize-4;
constexpr std::size_t FacialHairOffset=ReservedOffset, FaceOffset=FacialHairOffset+2;
constexpr std::size_t FaceReservedOffset=FaceOffset+FormatTwoFaceParameterCount;
constexpr std::size_t EyebrowsOffset=FaceReservedOffset, EarsOffset=EyebrowsOffset+2;
constexpr std::size_t AccessoryOffset=EarsOffset+2;                                  // id (2) + sides (1) each
constexpr std::size_t MakeupOffset=AccessoryOffset+AvatarAccessorySlotCount*3;
constexpr std::size_t ExtendedColorOffset=MakeupOffset+AvatarMakeupSlotCount*2;
constexpr std::size_t ProportionOffset=ExtendedColorOffset+(AvatarColorSlotCount-FormatTwoColorSlotCount)*3;
constexpr std::size_t ExtendedFaceOffset=ProportionOffset+1;
constexpr std::size_t ExtendedReservedOffset=ExtendedFaceOffset+(FaceParameterCount-FormatTwoFaceParameterCount);
static_assert(EyebrowsOffset==61&&AccessoryOffset==65&&MakeupOffset==77&&ExtendedColorOffset==85&&ProportionOffset==100&&
              ExtendedFaceOffset==101&&ExtendedReservedOffset==117,"docs/avatars.md documents this layout");
constexpr std::array<std::uint8_t,3> Magic{'C','N','A'};

bool optionalSlot(AvatarItemSlot slot){return slot==AvatarItemSlot::Glasses||slot==AvatarItemSlot::Hat;}

std::uint16_t read16(std::span<const std::uint8_t> bytes,std::size_t at){return static_cast<std::uint16_t>(bytes[at]|bytes[at+1]<<8);}
void write16(std::vector<std::uint8_t>& bytes,std::size_t at,std::uint16_t value){bytes[at]=value&0xff;bytes[at+1]=value>>8;}

// Palettes CreateRandom and the editor draw from; a description may carry any RGB value. Skin runs
// from light to deep in even steps of one natural ramp; the first twelve hair colours are natural
// (black to white), the last five deliberate fashion colours; eyes are natural.
constexpr AvatarColor SkinTones[]={{255,229,208},{249,214,187},{241,200,166},{232,186,150},{222,170,132},{208,154,112},
    {194,138,96},{176,120,82},{158,104,70},{138,90,60},{118,76,50},{98,63,42},{82,52,36},{66,42,30}};
constexpr AvatarColor HairColors[]={{28,24,22},{46,34,28},{70,48,34},{98,66,42},{128,90,56},{160,118,74},{196,156,98},
    {226,198,140},{146,66,38},{184,92,52},{142,138,134},{222,220,214},
    {72,84,156},{180,70,120},{110,64,150},{60,134,110},{232,120,170}};
constexpr AvatarColor EyeColors[]={{58,38,28},{88,56,34},{122,84,46},{128,104,52},{94,122,72},{70,128,106},{72,112,158},
    {104,146,190},{118,126,138},{44,42,46}};
constexpr AvatarColor ClothColors[]={{220,60,56},{240,150,40},{250,210,60},{90,180,80},{50,150,200},{60,80,180},{130,80,170},
    {230,120,170},{240,240,236},{60,62,70},{120,90,60},{40,110,110},
    {30,32,36},{150,152,156},{36,52,92},{120,36,48},{108,116,64},{196,174,130},{232,220,196},{92,144,196},{246,196,180},
    {150,200,190},{188,90,60},{70,150,140}};
constexpr AvatarColor LipColors[]={{176,86,96},{190,110,104},{156,66,74},{200,56,64},{214,98,88},{132,52,76},{150,92,78},
    {226,140,138}};
constexpr AvatarColor ShadowColors[]={{138,108,98},{160,120,84},{110,86,110},{92,94,104},{196,164,96},{84,110,150},
    {98,124,86},{176,112,128}};
constexpr AvatarColor BlushColors[]={{226,128,124},{236,148,120},{214,104,112},{200,96,122},{232,156,146}};
constexpr AvatarColor JewelryColors[]={{212,176,98},{200,202,206},{214,150,130},{60,60,64},{170,124,70},{232,232,236}};
}

std::span<const AvatarColor> colorPalette(AvatarColorSlot slot)
{
    switch(slot) {
    case AvatarColorSlot::Skin:return SkinTones;
    case AvatarColorSlot::Hair:return HairColors;
    case AvatarColorSlot::Eyes:return EyeColors;
    case AvatarColorSlot::Lips:return LipColors;
    case AvatarColorSlot::EyeShadow:return ShadowColors;
    case AvatarColorSlot::Blush:return BlushColors;
    case AvatarColorSlot::Jewelry:return JewelryColors;
    default:return ClothColors;
    }
}

AvatarColor drawnColor(const AvatarDescriptor& descriptor,AvatarColorSlot slot)
{
    const auto& color=descriptor.colors[static_cast<std::size_t>(slot)];
    if(static_cast<std::size_t>(slot)<FormatTwoColorSlotCount||color!=AvatarColor{})return color;
    return colorPalette(slot).front();
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes)
{
    std::uint32_t crc=0xffffffffu;
    for(auto byte:bytes) {
        crc^=byte;
        for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
    }
    return ~crc;
}

bool AvatarDescriptor::usesFaceFormat() const
{
    return facialHair!=0||std::any_of(face.begin(),face.begin()+FormatTwoFaceParameterCount,
        [](std::uint8_t value){return value!=NeutralFaceParameter;});
}

bool AvatarDescriptor::usesExtendedFields() const
{
    auto set=[](const auto& values){return std::ranges::any_of(values,[](auto value){return value!=decltype(value){};});};
    return eyebrows!=0||ears!=0||set(accessories)||set(sides)||set(makeup)||proportion!=NeutralFaceParameter||
        std::any_of(face.begin()+FormatTwoFaceParameterCount,face.end(),[](std::uint8_t value){return value!=NeutralFaceParameter;})||
        std::any_of(colors.begin()+FormatTwoColorSlotCount,colors.end(),[](const AvatarColor& color){return color!=AvatarColor{};});
}

namespace {
// The entry of a compiled-in catalog a descriptor names in a slot, or null.
const CatalogItem* named(const CatalogManifest& manifest,std::uint16_t id,AvatarItemSlot slot)
{
    const auto* entry=manifest.entry(id);
    return entry&&entry->slot==slot?entry:nullptr;
}

// The highest format a descriptor's catalog items need; empty when its catalog is not compiled in.
std::optional<std::uint8_t> itemFormat(const AvatarDescriptor& descriptor)
{
    const auto manifest=embeddedManifest(descriptor.catalogVersion);
    if(!manifest)return std::nullopt;
    std::uint8_t format=FormatVersion;
    auto need=[&](std::uint16_t id){if(const auto* entry=id?manifest->entry(id):nullptr)format=std::max(format,entry->format);};
    for(auto id:descriptor.items)need(id);
    need(descriptor.facialHair);
    return format;
}
}

std::uint8_t requiredFormat(const AvatarDescriptor& descriptor)
{
    if(descriptor.usesExtendedFields())return ExtendedFormatVersion;
    return std::max(descriptor.usesFaceFormat()?FaceFormatVersion:FormatVersion,itemFormat(descriptor).value_or(FormatVersion));
}

bool isEncodable(const AvatarDescriptor& descriptor)
{
    // A development catalog below the baseline is not a catalog any CNA build has, so its ids mean
    // nothing; it is refused rather than read against another version.
    if(descriptor.bodyType>1||!isSupportedCatalogVersion(descriptor.catalogVersion))return false;
    if(descriptor.heightMillimeters<MinimumHeightMillimeters||descriptor.heightMillimeters>MaximumHeightMillimeters)return false;
    for(std::size_t slot=0;slot<AvatarItemSlotCount;++slot)
        if(descriptor.items[slot]==0&&!optionalSlot(static_cast<AvatarItemSlot>(slot)))return false;
    for(std::size_t index=0;index<AvatarAccessorySlotCount;++index) {
        // A side belongs to a worn accessory, and gloves are always a pair.
        if(descriptor.sides[index]>AvatarSides::Right)return false;
        const bool pair=index==accessoryIndex(AvatarItemSlot::Gloves);
        if(descriptor.sides[index]!=AvatarSides::Both&&(descriptor.accessories[index]==0||pair))return false;
    }
    // Ids of a later catalog this build does not compile in are checked by the service and the resolver.
    const auto manifest=embeddedManifest(descriptor.catalogVersion);
    if(!manifest)return true;
    for(std::size_t slot=0;slot<AvatarItemSlotCount;++slot) {
        const auto id=descriptor.items[slot];
        if(id&&!manifest->item(id,static_cast<AvatarItemSlot>(slot)))return false;
    }
    auto optional=[&](std::uint16_t id,AvatarItemSlot slot){return id==0||named(*manifest,id,slot)!=nullptr;};
    if(!optional(descriptor.facialHair,AvatarItemSlot::FacialHair)||!optional(descriptor.ears,AvatarItemSlot::Ears)||
       !optional(descriptor.eyebrows,AvatarItemSlot::Eyebrows))return false;
    for(std::size_t index=0;index<AvatarAccessorySlotCount;++index)
        if(!optional(descriptor.accessories[index],static_cast<AvatarItemSlot>(static_cast<std::size_t>(AvatarItemSlot::Earrings)+index)))
            return false;
    for(std::size_t index=0;index<AvatarMakeupSlotCount;++index)
        if(!optional(descriptor.makeup[index],static_cast<AvatarItemSlot>(static_cast<std::size_t>(AvatarItemSlot::EyeShadow)+index)))
            return false;
    return true;
}

std::vector<std::uint8_t> encode(const AvatarDescriptor& descriptor)
{
    if(!isEncodable(descriptor))throw std::invalid_argument("avatar descriptor outside the CNA encoding");
    std::vector<std::uint8_t> bytes(DescriptionSize,0);
    const auto format=requiredFormat(descriptor);
    bytes[0]=format;
    std::ranges::copy(Magic,bytes.begin()+MagicOffset);
    bytes[BodyOffset]=descriptor.bodyType;
    write16(bytes,HeightOffset,descriptor.heightMillimeters);
    bytes[BuildOffset]=descriptor.build;
    write16(bytes,CatalogOffset,descriptor.catalogVersion);
    auto putColor=[&](std::size_t at,const AvatarColor& color){bytes[at]=color.r;bytes[at+1]=color.g;bytes[at+2]=color.b;};
    for(std::size_t index=0;index<FormatTwoColorSlotCount;++index)putColor(ColorOffset+index*3,descriptor.colors[index]);
    for(std::size_t index=0;index<AvatarItemSlotCount;++index)write16(bytes,ItemOffset+index*2,descriptor.items[index]);
    if(format>=FaceFormatVersion) {
        write16(bytes,FacialHairOffset,descriptor.facialHair);
        std::copy_n(descriptor.face.begin(),FormatTwoFaceParameterCount,bytes.begin()+FaceOffset);
    }
    if(format>=ExtendedFormatVersion) {
        write16(bytes,EyebrowsOffset,descriptor.eyebrows);
        write16(bytes,EarsOffset,descriptor.ears);
        for(std::size_t index=0;index<AvatarAccessorySlotCount;++index) {
            write16(bytes,AccessoryOffset+index*3,descriptor.accessories[index]);
            bytes[AccessoryOffset+index*3+2]=static_cast<std::uint8_t>(descriptor.sides[index]);
        }
        for(std::size_t index=0;index<AvatarMakeupSlotCount;++index)write16(bytes,MakeupOffset+index*2,descriptor.makeup[index]);
        for(std::size_t index=FormatTwoColorSlotCount;index<AvatarColorSlotCount;++index)
            putColor(ExtendedColorOffset+(index-FormatTwoColorSlotCount)*3,descriptor.colors[index]);
        bytes[ProportionOffset]=descriptor.proportion;
        std::copy(descriptor.face.begin()+FormatTwoFaceParameterCount,descriptor.face.end(),bytes.begin()+ExtendedFaceOffset);
    }
    const auto crc=crc32(std::span(bytes).first(ChecksumOffset));
    for(int shift=0;shift<4;++shift)bytes[ChecksumOffset+shift]=static_cast<std::uint8_t>(crc>>(shift*8));
    return bytes;
}

std::optional<AvatarDescriptor> decode(std::span<const std::uint8_t> bytes)
{
    if(bytes.size()!=static_cast<std::size_t>(DescriptionSize)||bytes[0]<FormatVersion||bytes[0]>ExtendedFormatVersion)return std::nullopt;
    const auto format=bytes[0];
    if(!std::equal(Magic.begin(),Magic.end(),bytes.begin()+MagicOffset))return std::nullopt;
    std::uint32_t stored=0;
    for(int shift=0;shift<4;++shift)stored|=static_cast<std::uint32_t>(bytes[ChecksumOffset+shift])<<(shift*8);
    if(stored!=crc32(bytes.first(ChecksumOffset)))return std::nullopt;
    const std::size_t reserved=format==FormatVersion?ReservedOffset:format==FaceFormatVersion?FaceReservedOffset:ExtendedReservedOffset;
    if(std::any_of(bytes.begin()+static_cast<std::ptrdiff_t>(reserved),bytes.begin()+ChecksumOffset,[](auto byte){return byte!=0;}))
        return std::nullopt;
    AvatarDescriptor descriptor;
    descriptor.bodyType=bytes[BodyOffset];
    descriptor.heightMillimeters=read16(bytes,HeightOffset);
    descriptor.build=bytes[BuildOffset];
    descriptor.catalogVersion=read16(bytes,CatalogOffset);
    auto getColor=[&](std::size_t at){return AvatarColor{bytes[at],bytes[at+1],bytes[at+2]};};
    for(std::size_t index=0;index<FormatTwoColorSlotCount;++index)descriptor.colors[index]=getColor(ColorOffset+index*3);
    for(std::size_t index=0;index<AvatarItemSlotCount;++index)descriptor.items[index]=read16(bytes,ItemOffset+index*2);
    if(format>=FaceFormatVersion) {
        descriptor.facialHair=read16(bytes,FacialHairOffset);
        std::copy_n(bytes.begin()+FaceOffset,FormatTwoFaceParameterCount,descriptor.face.begin());
    }
    if(format>=ExtendedFormatVersion) {
        descriptor.eyebrows=read16(bytes,EyebrowsOffset);
        descriptor.ears=read16(bytes,EarsOffset);
        for(std::size_t index=0;index<AvatarAccessorySlotCount;++index) {
            descriptor.accessories[index]=read16(bytes,AccessoryOffset+index*3);
            const auto side=bytes[AccessoryOffset+index*3+2];
            if(side>static_cast<std::uint8_t>(AvatarSides::Right))return std::nullopt;
            descriptor.sides[index]=static_cast<AvatarSides>(side);
        }
        for(std::size_t index=0;index<AvatarMakeupSlotCount;++index)descriptor.makeup[index]=read16(bytes,MakeupOffset+index*2);
        for(std::size_t index=FormatTwoColorSlotCount;index<AvatarColorSlotCount;++index)
            descriptor.colors[index]=getColor(ExtendedColorOffset+(index-FormatTwoColorSlotCount)*3);
        descriptor.proportion=bytes[ProportionOffset];
        std::copy_n(bytes.begin()+ExtendedFaceOffset,FaceParameterCount-FormatTwoFaceParameterCount,
                    descriptor.face.begin()+FormatTwoFaceParameterCount);
    }
    // One canonical encoding per avatar: a buffer in a higher format than its contents need is not
    // one. Which items need format 3 is known only for a compiled-in catalog; for a later one the
    // fields decide the lowest format, and the service has checked the items.
    if(!isEncodable(descriptor))return std::nullopt;
    const auto required=requiredFormat(descriptor);
    if(itemFormat(descriptor)?required!=format:required>format)return std::nullopt;
    return descriptor;
}

AvatarDescriptor randomDescriptor(std::optional<std::uint8_t> bodyType,std::mt19937& random)
{
    return randomDescriptor(newestEmbeddedManifest(),bodyType,random);
}
}
