// SPDX-License-Identifier: MS-PL
// Random avatars (CreateRandom and the editor's Random): believable people rather than independent
// dice per field. A face starts from the catalog's named shapes and varies a little around them;
// clothes are a ready-made outfit or pieces in colours that go together; hair and eye colours are
// mostly natural; accessories and makeup appear now and then, worn the way people wear them.
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include <algorithm>
#include <cmath>

namespace CNA::Internal::GamerServices::Avatars {
namespace {
std::size_t at(AvatarColorSlot slot){return static_cast<std::size_t>(slot);}

// The cloth palette's neutrals (white, charcoal, brown, black, grey, navy, khaki, cream, denim).
constexpr std::size_t ClothNeutrals[]{8,9,10,12,13,14,17,18,19};
// How often each palette entry is drawn: natural hair colours by how common they are (black and
// browns most, then blonds, reds, greys), the fashion colours rarely; eye colours likewise.
constexpr double HairWeights[]{12,14,14,10,8,6,5,4,3,3,3,2,0.4,0.4,0.4,0.4,0.4};
constexpr double EyeWeights[]{16,16,10,8,7,6,10,6,7,2};

class Dice {
public:
    explicit Dice(std::mt19937& random):random_(random){}
    bool chance(double p){return std::uniform_real_distribution<double>(0.0,1.0)(random_)<p;}
    std::size_t index(std::size_t count){return std::uniform_int_distribution<std::size_t>(0,count-1)(random_);}
    template<typename T> std::size_t weighted(const T& weights)
    {
        return std::discrete_distribution<std::size_t>(std::begin(weights),std::end(weights))(random_);
    }
    double normal(double mean,double spread){return std::normal_distribution<double>(mean,spread)(random_);}
private:
    std::mt19937& random_;
};

// An entry of a kind, by the catalog's per-body weights; 0 when the kind has none.
std::uint16_t pickEntry(const CatalogManifest& catalog,AvatarItemSlot slot,int body,Dice& dice,std::uint8_t maximumFormat=3)
{
    std::vector<const CatalogItem*> entries;
    std::vector<double> weights;
    for(const auto* entry:catalog.itemsOf(slot))
        if(entry->randomWeight[body]>0&&entry->format<=maximumFormat) {
            entries.push_back(entry);
            weights.push_back(entry->randomWeight[body]);
        }
    if(entries.empty()) {
        // A catalog may weight nothing of a kind for a body type: any entry of it will do.
        const auto any=catalog.itemsOf(slot);
        return any.empty()?0:any.front()->id;
    }
    return entries[dice.weighted(weights)]->id;
}

std::uint8_t faceByte(double value){return static_cast<std::uint8_t>(std::clamp<long>(std::lround(128.0+value*127.0),0,255));}

// A preset of a group, "Balanced"/"Natural"/"Average" (the first) twice as likely as each other.
const FacePreset* pickPreset(const CatalogManifest& catalog,std::string_view group,Dice& dice)
{
    std::vector<const FacePreset*> presets;
    std::vector<double> weights;
    for(const auto& preset:catalog.facePresets)
        if(preset.group==group) {
            weights.push_back(presets.empty()?2.0:1.0);
            presets.push_back(&preset);
        }
    return presets.empty()?nullptr:presets[dice.weighted(weights)];
}
}

AvatarDescriptor randomDescriptor(const CatalogManifest& catalog,std::optional<std::uint8_t> bodyType,std::mt19937& random)
{
    Dice dice(random);
    AvatarDescriptor d;
    d.catalogVersion=catalog.version;
    d.bodyType=bodyType?*bodyType:static_cast<std::uint8_t>(dice.index(2));
    const int body=d.bodyType==1?1:0;
    const bool shaped=!catalog.faceControls[body].empty();

    // Body: mostly adult proportions near the authored height; now and then a compact, younger-looking
    // figure, which is also shorter.
    const int authored=catalog.authoredHeightMillimeters[body];
    const bool compact=dice.chance(0.12);
    const double height=authored+dice.normal(compact?-150.0:0.0,compact?45.0:55.0);
    d.heightMillimeters=static_cast<std::uint16_t>(std::clamp<double>(std::round(height/10.0)*10.0,
        std::max<int>(MinimumHeightMillimeters,authored-260),std::min<int>(MaximumHeightMillimeters,authored+200)));
    d.build=static_cast<std::uint8_t>(std::clamp<double>(dice.normal(128.0,28.0),70.0,205.0));
    if(shaped)d.proportion=compact?faceByte(std::clamp(dice.normal(-0.6,0.15),-1.0,-0.3)):faceByte(std::clamp(dice.normal(0.0,0.10),-0.25,0.25));

    // Colours: any skin tone, mostly natural hair and eyes.
    const auto skin=colorPalette(AvatarColorSlot::Skin);
    d.colors[at(AvatarColorSlot::Skin)]=skin[dice.index(skin.size())];
    const auto hairColors=colorPalette(AvatarColorSlot::Hair);
    d.colors[at(AvatarColorSlot::Hair)]=hairColors[std::min(dice.weighted(HairWeights),hairColors.size()-1)];
    const auto eyes=colorPalette(AvatarColorSlot::Eyes);
    d.colors[at(AvatarColorSlot::Eyes)]=eyes[std::min(dice.weighted(EyeWeights),eyes.size()-1)];

    // Face: a named shape for each part of it, then a little individual variation; nothing extreme.
    if(shaped) {
        std::array<double,FaceParameterCount> face{};
        for(const char* group:{"face","eyes","brows","ears","nose","mouth"})
            if(const auto* preset=pickPreset(catalog,group,dice))
                for(const auto& [name,value]:preset->values)
                    if(const int parameter=catalog.faceParameter(body,name);parameter>=0)face[static_cast<std::size_t>(parameter)]=value;
        for(auto& value:face)value=std::clamp(value+dice.normal(0.0,0.10),-0.6,0.6);
        // Only the bytes the catalog gives a meaning to.
        for(const auto& control:catalog.faceControls[body])
            d.face[static_cast<std::size_t>(control.parameter)]=faceByte(face[static_cast<std::size_t>(control.parameter)]);
    }
    if(dice.chance(0.85))d.eyebrows=pickEntry(catalog,AvatarItemSlot::Eyebrows,body,dice);
    if(const auto ears=catalog.itemsOf(AvatarItemSlot::Ears);ears.size()>1&&dice.chance(0.45))d.ears=pickEntry(catalog,AvatarItemSlot::Ears,body,dice);
    // The first ear shape is the default, which a description names as 0.
    if(const auto ears=catalog.itemsOf(AvatarItemSlot::Ears);!ears.empty()&&d.ears==ears.front()->id)d.ears=0;

    d.items[static_cast<std::size_t>(AvatarItemSlot::Hair)]=pickEntry(catalog,AvatarItemSlot::Hair,body,dice);
    if(body==1&&dice.chance(0.45))d.facialHair=pickEntry(catalog,AvatarItemSlot::FacialHair,body,dice);

    // Clothes: a ready-made outfit sometimes, otherwise pieces whose colours go together.
    const auto cloth=colorPalette(AvatarColorSlot::Top);
    auto neutral=[&]{return cloth[ClothNeutrals[dice.index(std::size(ClothNeutrals))]];};
    std::vector<const CatalogOutfit*> outfits;
    for(const auto& outfit:catalog.outfits)
        if(std::ranges::all_of(outfit.items,[&](const auto& entry) {
            const auto* item=entry.second?catalog.item(entry.second,entry.first):nullptr;
            return !entry.second||(item&&item->randomWeight[body]>0.05f);
        }))outfits.push_back(&outfit);
    bool hatChosen=false,glovesChosen=false;
    if(!outfits.empty()&&dice.chance(0.40)) {
        const auto& outfit=*outfits[dice.index(outfits.size())];
        for(const auto& [slot,id]:outfit.items) {
            if(static_cast<std::size_t>(slot)<AvatarItemSlotCount)d.items[static_cast<std::size_t>(slot)]=id;
            else if(slot>=AvatarItemSlot::Earrings&&slot<=AvatarItemSlot::Rings)d.accessories[accessoryIndex(slot)]=id;
            hatChosen|=slot==AvatarItemSlot::Hat;
            glovesChosen|=slot==AvatarItemSlot::Gloves;
        }
        for(const auto& [slot,color]:outfit.colors)d.colors[at(slot)]=color;
    } else {
        for(auto slot:{AvatarItemSlot::Top,AvatarItemSlot::Bottom,AvatarItemSlot::Shoes})
            d.items[static_cast<std::size_t>(slot)]=pickEntry(catalog,slot,body,dice);
        // A top colour, bottoms neutral or two steps round the hue wheel, shoes mostly neutral,
        // accessories matching the top or neutral.
        const auto top=dice.index(cloth.size());
        d.colors[at(AvatarColorSlot::Top)]=cloth[top];
        const auto around=[&](int steps){return cloth[(top+static_cast<std::size_t>(8+steps))%8];};
        d.colors[at(AvatarColorSlot::Bottom)]=dice.chance(0.65)||top>=8?neutral():around(dice.chance(0.5)?2:-2);
        d.colors[at(AvatarColorSlot::Shoes)]=dice.chance(0.7)?neutral():cloth[top];
        d.colors[at(AvatarColorSlot::Accessory)]=dice.chance(0.5)?cloth[top]:neutral();
    }
    // Every format 1 field must stay meaningful for every required slot.
    for(auto slot:{AvatarItemSlot::Top,AvatarItemSlot::Bottom,AvatarItemSlot::Shoes})
        if(!d.items[static_cast<std::size_t>(slot)])d.items[static_cast<std::size_t>(slot)]=pickEntry(catalog,slot,body,dice);
    if(!d.items[static_cast<std::size_t>(AvatarItemSlot::Hair)])d.items[static_cast<std::size_t>(AvatarItemSlot::Hair)]=pickEntry(catalog,AvatarItemSlot::Hair,body,dice);

    // Accessories now and then, worn as people wear them.
    if(dice.chance(0.18))d.items[static_cast<std::size_t>(AvatarItemSlot::Glasses)]=pickEntry(catalog,AvatarItemSlot::Glasses,body,dice);
    if(!hatChosen&&dice.chance(0.13))d.items[static_cast<std::size_t>(AvatarItemSlot::Hat)]=pickEntry(catalog,AvatarItemSlot::Hat,body,dice);
    auto accessory=[&](AvatarItemSlot slot,double probability,std::array<double,3> sides) {
        if(catalog.itemsOf(slot).empty()||!dice.chance(probability))return;
        const auto index=accessoryIndex(slot);
        d.accessories[index]=pickEntry(catalog,slot,body,dice);
        if(slot!=AvatarItemSlot::Gloves)d.sides[index]=static_cast<AvatarSides>(dice.weighted(sides));
    };
    accessory(AvatarItemSlot::Earrings,body==0?0.45:0.08,{0.9,0.05,0.05});
    accessory(AvatarItemSlot::Wristwear,0.25,{0.05,0.70,0.25});
    if(!glovesChosen)accessory(AvatarItemSlot::Gloves,0.03,{1.0,0.0,0.0});
    accessory(AvatarItemSlot::Rings,0.12,{0.10,0.60,0.30});
    const bool jewellery=std::ranges::any_of(d.accessories,[](std::uint16_t id){return id!=0;});
    if(jewellery) {
        // Gold and silver mostly.
        const auto metals=colorPalette(AvatarColorSlot::Jewelry);
        d.colors[at(AvatarColorSlot::Jewelry)]=metals[dice.chance(0.8)?dice.index(2):dice.index(metals.size())];
    }
    if(d.accessories[accessoryIndex(AvatarItemSlot::Gloves)]&&d.colors[at(AvatarColorSlot::Gloves)]==AvatarColor{})
        d.colors[at(AvatarColorSlot::Gloves)]=dice.chance(0.6)?neutral():cloth[dice.index(cloth.size())];

    // Makeup: subtle and occasional, for anyone (more often on female bodies, by the catalog's weights).
    if(dice.chance(body==0?0.32:0.03)) {
        auto style=[&](AvatarItemSlot slot,double probability,std::size_t naturalColors) {
            if(catalog.itemsOf(slot).empty()||!dice.chance(probability))return;
            d.makeup[makeupIndex(slot)]=pickEntry(catalog,slot,body,dice);
            const AvatarColorSlot color=slot==AvatarItemSlot::Lips?AvatarColorSlot::Lips:slot==AvatarItemSlot::Blush?AvatarColorSlot::Blush:
                                        AvatarColorSlot::EyeShadow;
            if(slot!=AvatarItemSlot::Eyeliner) {
                const auto palette=colorPalette(color);
                d.colors[at(color)]=palette[dice.index(std::min(naturalColors,palette.size()))];
            }
        };
        style(AvatarItemSlot::Eyeliner,0.6,0);
        style(AvatarItemSlot::EyeShadow,0.4,4);
        style(AvatarItemSlot::Lips,0.6,4);
        style(AvatarItemSlot::Blush,0.3,3);
    }
    return d;
}
}
