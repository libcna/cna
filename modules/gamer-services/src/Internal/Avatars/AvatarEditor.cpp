// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AvatarEditor.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace CNA::Internal::GamerServices::Avatars {
namespace {
// Build and face-shape bytes move in sixteenths of their range, so every value is reachable and
// eight steps either way cover it.
constexpr int SliderStep=16;
constexpr int HeightStep=10;

std::size_t at(AvatarColorSlot slot){return static_cast<std::size_t>(slot);}
std::size_t at(AvatarItemSlot slot){return static_cast<std::size_t>(slot);}
bool optionalSlot(AvatarItemSlot slot){return slot==AvatarItemSlot::Glasses||slot==AvatarItemSlot::Hat;}

std::uint8_t stepSlider(std::uint8_t value,int delta)
{
    const int steps=static_cast<int>(std::lround((value-NeutralFaceParameter)/static_cast<double>(SliderStep)))+delta;
    return static_cast<std::uint8_t>(std::clamp(NeutralFaceParameter+steps*SliderStep,0,255));
}
float sliderPosition(std::uint8_t value){return std::clamp((value-NeutralFaceParameter)/127.0f,-1.0f,1.0f);}
std::string sliderText(std::uint8_t value)
{
    const int steps=static_cast<int>(std::lround((value-NeutralFaceParameter)/static_cast<double>(SliderStep)));
    return steps>0?"+"+std::to_string(steps):std::to_string(steps);
}

// "jawWidth" -> "Jaw width"; "hair_side_part" -> "Side part".
std::string readable(std::string_view name)
{
    std::string text;
    for(char c:name) {
        if(c=='_'||c=='-')text+=' ';
        else if(std::isupper(static_cast<unsigned char>(c))&&!text.empty()) {
            text+=' ';
            text+=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else text+=c;
    }
    if(!text.empty())text[0]=static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    return text;
}

const char* slotName(AvatarItemSlot slot)
{
    switch(slot) {
    case AvatarItemSlot::Hair:return "Hair";
    case AvatarItemSlot::Top:return "Top";
    case AvatarItemSlot::Bottom:return "Bottom";
    case AvatarItemSlot::Shoes:return "Shoes";
    case AvatarItemSlot::Glasses:return "Glasses";
    case AvatarItemSlot::Hat:return "Hat";
    default:return "Facial hair";
    }
}
}

AvatarEditorModel::AvatarEditorModel(std::shared_ptr<const CatalogManifest> catalog,std::vector<std::uint8_t> current,std::uint32_t seed)
    : catalog_(std::move(catalog)),stored_(std::move(current)),random_(seed)
{
    const auto decoded=decode(stored_);
    start_=decoded?fitted(*decoded):randomDescriptor(*catalog_,std::nullopt,random_);
    working_=start_;
}

AvatarDescriptor AvatarEditorModel::fitted(const AvatarDescriptor& source) const
{
    // The avatar as the edited catalog can express it: ids it does not have become the slot's
    // first item (or none), and a catalog without face controls keeps the authored face.
    AvatarDescriptor result=source;
    result.catalogVersion=catalog_->version;
    const int body=result.bodyType==1?1:0;
    for(std::size_t slot=0;slot<AvatarItemSlotCount;++slot) {
        const auto kind=static_cast<AvatarItemSlot>(slot);
        const auto* item=catalog_->item(result.items[slot]);
        if(item&&item->slot==kind)continue;
        const auto offered=choices(kind);
        result.items[slot]=optionalSlot(kind)||offered.empty()?0:offered.front();
    }
    if(result.facialHair&&!catalog_->featureItem(result.facialHair))result.facialHair=0;
    if(catalog_->faceControls[body].empty())result.face.fill(NeutralFaceParameter);
    return result;
}

std::vector<std::uint16_t> AvatarEditorModel::choices(AvatarItemSlot slot) const
{
    std::vector<std::uint16_t> ids;
    if(slot==AvatarItemSlot::FacialHair) {
        for(const auto& item:catalog_->featureItems)ids.push_back(item.id);
    } else {
        for(const auto& item:catalog_->items)if(item.slot==slot)ids.push_back(item.id);
    }
    std::ranges::sort(ids);
    if(optionalSlot(slot)||slot==AvatarItemSlot::FacialHair)ids.insert(ids.begin(),0);
    return ids;
}

std::string AvatarEditorModel::itemName(std::uint16_t id,bool feature) const
{
    if(id==0)return "None";
    const auto* item=feature?catalog_->featureItem(id):catalog_->item(id);
    if(!item)return "Item "+std::to_string(id);
    std::string_view name=item->name;
    // Catalog names carry their slot as a prefix ("top_hoodie", "facial_beard").
    if(const auto underscore=name.find('_');underscore!=std::string_view::npos)name.remove_prefix(underscore+1);
    return readable(name);
}

std::vector<AvatarEditorModel::Row> AvatarEditorModel::rows() const
{
    std::vector<Row> list;
    switch(category_) {
    case EditorCategory::Body:
        list.push_back({Kind::BodyType,0,"Body"});
        list.push_back({Kind::Height,0,"Height"});
        list.push_back({Kind::Build,0,"Build"});
        list.push_back({Kind::Color,static_cast<int>(AvatarColorSlot::Skin),"Skin"});
        break;
    case EditorCategory::Features: {
        list.push_back({Kind::Color,static_cast<int>(AvatarColorSlot::Eyes),"Eye color"});
        if(!catalog_->featureItems.empty())list.push_back({Kind::FacialHair,0,"Facial hair"});
        std::vector<bool> seen(FaceParameterCount,false);
        for(const auto& control:catalog_->faceControls[working_.bodyType==1?1:0]) {
            if(seen[static_cast<std::size_t>(control.parameter)])continue;
            seen[static_cast<std::size_t>(control.parameter)]=true;
            list.push_back({Kind::Face,control.parameter,
                control.name.empty()?"Shape "+std::to_string(control.parameter+1):readable(control.name)});
        }
        break;
    }
    case EditorCategory::Style:
        list.push_back({Kind::Item,static_cast<int>(AvatarItemSlot::Hair),"Hair"});
        list.push_back({Kind::Color,static_cast<int>(AvatarColorSlot::Hair),"Hair color"});
        list.push_back({Kind::Item,static_cast<int>(AvatarItemSlot::Top),"Top"});
        list.push_back({Kind::Color,static_cast<int>(AvatarColorSlot::Top),"Top color"});
        list.push_back({Kind::Item,static_cast<int>(AvatarItemSlot::Bottom),"Bottom"});
        list.push_back({Kind::Color,static_cast<int>(AvatarColorSlot::Bottom),"Bottom color"});
        list.push_back({Kind::Item,static_cast<int>(AvatarItemSlot::Shoes),"Shoes"});
        list.push_back({Kind::Color,static_cast<int>(AvatarColorSlot::Shoes),"Shoe color"});
        list.push_back({Kind::Item,static_cast<int>(AvatarItemSlot::Glasses),"Glasses"});
        list.push_back({Kind::Item,static_cast<int>(AvatarItemSlot::Hat),"Hat"});
        list.push_back({Kind::Color,static_cast<int>(AvatarColorSlot::Accessory),"Accessory color"});
        break;
    }
    return list;
}

void AvatarEditorModel::turnPage(int delta)
{
    category_=static_cast<EditorCategory>(((static_cast<int>(category_)+delta)%EditorCategoryCount+EditorCategoryCount)%EditorCategoryCount);
    selection_=0;
}

void AvatarEditorModel::select(int delta)
{
    const auto count=static_cast<int>(fieldCount());
    if(count==0)return;
    selection_=static_cast<std::size_t>(((static_cast<int>(selection_)+delta)%count+count)%count);
}

EditorField AvatarEditorModel::field(std::size_t index) const
{
    const auto list=rows();
    if(index>=list.size())return {};
    const auto& row=list[index];
    EditorField result{row.label,{},std::nullopt,std::nullopt};
    switch(row.kind) {
    case Kind::BodyType:
        result.value=working_.bodyType==1?"Male":"Female";
        break;
    case Kind::Height: {
        char text[16];
        std::snprintf(text,sizeof text,"%.2f m",working_.heightMillimeters/1000.0);
        result.value=text;
        break;
    }
    case Kind::Build:
        result.value=sliderText(working_.build);
        result.slider=sliderPosition(working_.build);
        break;
    case Kind::Color: {
        const auto& color=working_.colors[static_cast<std::size_t>(row.index)];
        const auto palette=colorPalette(static_cast<AvatarColorSlot>(row.index));
        const auto found=std::ranges::find(palette,color);
        result.value=found==palette.end()?"Custom":std::to_string(found-palette.begin()+1)+" of "+std::to_string(palette.size());
        result.swatch=color;
        break;
    }
    case Kind::Item: {
        const auto slot=static_cast<AvatarItemSlot>(row.index);
        result.value=itemName(working_.items[at(slot)],false);
        break;
    }
    case Kind::FacialHair:
        result.value=itemName(working_.facialHair,true);
        break;
    case Kind::Face:
        result.value=sliderText(working_.face[static_cast<std::size_t>(row.index)]);
        result.slider=sliderPosition(working_.face[static_cast<std::size_t>(row.index)]);
        break;
    }
    return result;
}

bool AvatarEditorModel::adjust(int delta)
{
    const auto list=rows();
    if(selection_>=list.size()||delta==0)return false;
    const auto& row=list[selection_];
    const auto before=working_;
    auto cycle=[delta](const std::vector<std::uint16_t>& ids,std::uint16_t current) {
        if(ids.empty())return current;
        const auto found=std::ranges::find(ids,current);
        const int count=static_cast<int>(ids.size());
        const int index=found==ids.end()?(delta>0?-1:0):static_cast<int>(found-ids.begin());
        return ids[static_cast<std::size_t>(((index+delta)%count+count)%count)];
    };
    switch(row.kind) {
    case Kind::BodyType: {
        // Keep the avatar as tall relative to its body as it was.
        const int from=working_.bodyType==1?1:0, to=1-from;
        const int offset=working_.heightMillimeters-catalog_->authoredHeightMillimeters[static_cast<std::size_t>(from)];
        working_.bodyType=static_cast<std::uint8_t>(to);
        working_.heightMillimeters=static_cast<std::uint16_t>(std::clamp<int>(
            catalog_->authoredHeightMillimeters[static_cast<std::size_t>(to)]+offset,MinimumHeightMillimeters,MaximumHeightMillimeters));
        if(catalog_->faceControls[static_cast<std::size_t>(to)].empty())working_.face.fill(NeutralFaceParameter);
        // Rows depend on the body's face controls; stay on the body row.
        break;
    }
    case Kind::Height:
        working_.heightMillimeters=static_cast<std::uint16_t>(std::clamp<int>(
            (working_.heightMillimeters+delta*HeightStep)/HeightStep*HeightStep,MinimumHeightMillimeters,MaximumHeightMillimeters));
        break;
    case Kind::Build:
        working_.build=stepSlider(working_.build,delta);
        break;
    case Kind::Color: {
        auto& color=working_.colors[static_cast<std::size_t>(row.index)];
        const auto palette=colorPalette(static_cast<AvatarColorSlot>(row.index));
        const auto found=std::ranges::find(palette,color);
        const int count=static_cast<int>(palette.size());
        const int index=found==palette.end()?(delta>0?-1:0):static_cast<int>(found-palette.begin());
        color=palette[static_cast<std::size_t>(((index+delta)%count+count)%count)];
        break;
    }
    case Kind::Item: {
        const auto slot=static_cast<AvatarItemSlot>(row.index);
        working_.items[at(slot)]=cycle(choices(slot),working_.items[at(slot)]);
        break;
    }
    case Kind::FacialHair:
        working_.facialHair=cycle(choices(AvatarItemSlot::FacialHair),working_.facialHair);
        break;
    case Kind::Face:
        working_.face[static_cast<std::size_t>(row.index)]=stepSlider(working_.face[static_cast<std::size_t>(row.index)],delta);
        break;
    }
    selection_=std::min(selection_,fieldCount()-1);
    return !(working_==before);
}

void AvatarEditorModel::randomize()
{
    working_=randomDescriptor(*catalog_,working_.bodyType,random_);
    selection_=std::min(selection_,fieldCount()-1);
}

void AvatarEditorModel::revert()
{
    working_=start_;
    selection_=std::min(selection_,fieldCount()-1);
}

void AvatarEditorModel::markSaved()
{
    stored_=encoded();
    start_=working_;
}
}
