// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/GamerServices/AvatarEditor.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"

#include <algorithm>
#include <random>
#include <set>
#include <string>

namespace Avatars = CNA::Internal::GamerServices::Avatars;
namespace Service = CNA::Internal::GamerServices;
using Avatars::AvatarEditorModel;
using Avatars::EditorCategory;

namespace {
std::shared_ptr<const Avatars::CatalogManifest> newest() {return Avatars::embeddedCatalogs().back();}
std::shared_ptr<const Avatars::CatalogManifest> first() {return Avatars::embeddedManifest(Avatars::BaseCatalogVersion);}

std::vector<std::uint8_t> storedAvatar(const std::shared_ptr<const Avatars::CatalogManifest>& catalog,std::uint32_t seed) {
    std::mt19937 random(seed);
    return Avatars::encode(Avatars::randomDescriptor(*catalog,std::nullopt,random));
}

void pageTo(AvatarEditorModel& model,EditorCategory page) {
    while(model.category()!=page)model.turnPage(1);
}

// Selects the row with this label on the page shown.
bool selectRow(AvatarEditorModel& model,const std::string& label) {
    for(std::size_t row=0;row<model.fieldCount();++row) {
        if(model.field(row).label==label) {
            while(model.selection()!=row)model.select(1);
            return true;
        }
    }
    return false;
}

std::vector<std::string> labels(const AvatarEditorModel& model) {
    std::vector<std::string> result;
    for(std::size_t row=0;row<model.fieldCount();++row)result.push_back(model.field(row).label);
    return result;
}
}

TEST(AvatarEditorTest, StartsFromTheStoredAvatarInTheCatalogItEdits) {
    const auto stored=storedAvatar(newest(),7);
    AvatarEditorModel model(newest(),stored,1);
    EXPECT_EQ(model.encoded(),stored);
    EXPECT_FALSE(model.differsFromStored());
    EXPECT_EQ(model.descriptor(),*Avatars::decode(stored));
}

TEST(AvatarEditorTest, AnOlderCatalogsAvatarIsCarriedIntoTheEditedCatalog) {
    if(newest()->version==Avatars::BaseCatalogVersion)GTEST_SKIP()<<"only catalog 1 is compiled in";
    const auto stored=storedAvatar(first(),11);
    const auto before=*Avatars::decode(stored);
    AvatarEditorModel model(newest(),stored,1);
    EXPECT_EQ(model.descriptor().catalogVersion,newest()->version);
    EXPECT_EQ(model.descriptor().colors,before.colors);
    EXPECT_EQ(model.descriptor().heightMillimeters,before.heightMillimeters);
    for(std::size_t slot=0;slot<Avatars::AvatarItemSlotCount;++slot) {
        const auto id=model.descriptor().items[slot];
        if(newest()->item(before.items[slot]))EXPECT_EQ(id,before.items[slot]);
        if(id)EXPECT_EQ(static_cast<std::size_t>(newest()->item(id)->slot),slot);
    }
    // Saving would store the carried-over avatar; nothing is written until the player saves.
    EXPECT_TRUE(model.differsFromStored());
}

TEST(AvatarEditorTest, WithoutAStoredAvatarItStartsFromARandomOne) {
    for(const std::vector<std::uint8_t>& stored:{std::vector<std::uint8_t>{},std::vector<std::uint8_t>(1021,0)}) {
        AvatarEditorModel model(newest(),stored,3);
        EXPECT_TRUE(Avatars::decode(model.encoded()).has_value());
        EXPECT_EQ(model.descriptor().catalogVersion,newest()->version);
        EXPECT_TRUE(model.differsFromStored());
    }
}

TEST(AvatarEditorTest, CategoriesOfferWhatTheirNameSays) {
    AvatarEditorModel model(newest(),storedAvatar(newest(),5),1);
    EXPECT_EQ(model.category(),EditorCategory::Body);
    EXPECT_STREQ(Avatars::editorCategoryName(EditorCategory::NoseAndMouth),"Nose & mouth");
    EXPECT_EQ(labels(model),(std::vector<std::string>{"Body","Height","Build"}));
    EXPECT_EQ(model.view(),Avatars::EditorView::Body);
    pageTo(model,EditorCategory::Skin);
    EXPECT_EQ(labels(model),(std::vector<std::string>{"Skin tone"}));
    const auto body=model.descriptor().bodyType==1?1:0;
    if(!newest()->faceControls[body].empty()) {
        pageTo(model,EditorCategory::Face);
        EXPECT_EQ(labels(model),(std::vector<std::string>{"Face shape","Head width","Head length","Jaw","Chin","Cheeks","Ears"}));
        EXPECT_EQ(model.view(),Avatars::EditorView::Head);
        pageTo(model,EditorCategory::Eyes);
        EXPECT_EQ(labels(model),(std::vector<std::string>{"Eye color","Eye shape","Eye size","Eye spacing","Eye height","Eye tilt","Brows"}));
        pageTo(model,EditorCategory::NoseAndMouth);
        EXPECT_EQ(labels(model),(std::vector<std::string>{"Shape","Nose size","Nose width","Nose height","Mouth width","Mouth height"}));
        pageTo(model,EditorCategory::FacialHair);
        EXPECT_EQ(labels(model),(std::vector<std::string>{"Style","Color"}));
    }
    pageTo(model,EditorCategory::Hair);
    EXPECT_EQ(labels(model),(std::vector<std::string>{"Hairstyle","Hair color"}));
    pageTo(model,EditorCategory::Tops);
    EXPECT_EQ(labels(model),(std::vector<std::string>{"Top","Color"}));
    EXPECT_EQ(model.view(),Avatars::EditorView::Upper);
    pageTo(model,EditorCategory::Bottoms);
    EXPECT_EQ(model.view(),Avatars::EditorView::Lower);
    pageTo(model,EditorCategory::Shoes);
    EXPECT_EQ(labels(model),(std::vector<std::string>{"Shoes","Color"}));
    EXPECT_EQ(model.view(),Avatars::EditorView::Feet);
    pageTo(model,EditorCategory::Glasses);
    EXPECT_EQ(labels(model),(std::vector<std::string>{"Glasses","Frame color"}));
    pageTo(model,EditorCategory::Headwear);
    EXPECT_EQ(labels(model),(std::vector<std::string>{"Headwear","Color"}));
    model.turnPage(1);
    EXPECT_EQ(model.category(),EditorCategory::Body);
    model.turnPage(-1);
    EXPECT_EQ(model.category(),EditorCategory::Headwear);
}

TEST(AvatarEditorTest, RowsSayHowTheyAreShown) {
    AvatarEditorModel model(newest(),storedAvatar(newest(),5),1);
    EXPECT_EQ(model.field(0).kind,Avatars::EditorRowKind::Cards);
    EXPECT_EQ(model.options(0).size(),2u);
    EXPECT_EQ(model.field(1).kind,Avatars::EditorRowKind::Slider);
    EXPECT_TRUE(model.options(1).empty());
    pageTo(model,EditorCategory::Hair);
    EXPECT_EQ(model.field(1).kind,Avatars::EditorRowKind::Swatches);
    const auto swatches=model.options(1);
    EXPECT_EQ(swatches.size(),Avatars::colorPalette(Avatars::AvatarColorSlot::Hair).size());
    EXPECT_TRUE(std::ranges::all_of(swatches,[](const auto& option){return option.color.has_value();}));
    const auto styles=model.options(0);
    EXPECT_EQ(std::ranges::count_if(styles,&Avatars::EditorOption::current),1);
    for(const auto& option:styles)EXPECT_TRUE(Avatars::isEncodable(option.result))<<option.label;
}

TEST(AvatarEditorTest, CatalogOneOffersNoFaceShapeOrFacialHair) {
    AvatarEditorModel model(first(),storedAvatar(first(),5),1);
    pageTo(model,EditorCategory::Face);
    EXPECT_TRUE(labels(model).empty());
    pageTo(model,EditorCategory::Eyes);
    EXPECT_EQ(labels(model),(std::vector<std::string>{"Eye color"}));
    pageTo(model,EditorCategory::FacialHair);
    EXPECT_EQ(labels(model),(std::vector<std::string>{"Color"}));
}

TEST(AvatarEditorTest, HeightAndBuildStepWithinTheirRanges) {
    AvatarEditorModel model(newest(),storedAvatar(newest(),9),1);
    ASSERT_TRUE(selectRow(model,"Height"));
    const auto height=model.descriptor().heightMillimeters;
    EXPECT_TRUE(model.adjust(1));
    EXPECT_EQ(model.descriptor().heightMillimeters,height/10*10+10);
    for(int i=0;i<100;++i)model.adjust(1);
    EXPECT_EQ(model.descriptor().heightMillimeters,Avatars::MaximumHeightMillimeters);
    EXPECT_FALSE(model.adjust(1));
    EXPECT_FLOAT_EQ(*model.field(model.selection()).slider,1.0f);
    for(int i=0;i<100;++i)model.adjust(-1);
    EXPECT_EQ(model.descriptor().heightMillimeters,Avatars::MinimumHeightMillimeters);
    EXPECT_EQ(model.field(model.selection()).value,"1.45 m");

    ASSERT_TRUE(selectRow(model,"Build"));
    for(int i=0;i<20;++i)model.adjust(-1);
    EXPECT_EQ(model.descriptor().build,0);
    EXPECT_EQ(model.field(model.selection()).value,"Very slim");
    EXPECT_FLOAT_EQ(*model.field(model.selection()).slider,-1.0f);
    for(int i=0;i<8;++i)model.adjust(1);
    EXPECT_EQ(model.descriptor().build,Avatars::NeutralFaceParameter);
    EXPECT_EQ(model.field(model.selection()).value,"Average");
    model.adjust(1);
    EXPECT_EQ(model.field(model.selection()).value,"A little heavy");
    model.adjust(3);
    EXPECT_EQ(model.field(model.selection()).value,"Heavy");
    for(int i=0;i<20;++i)model.adjust(1);
    EXPECT_EQ(model.descriptor().build,255);
    EXPECT_EQ(model.field(model.selection()).value,"Very heavy");
}

TEST(AvatarEditorTest, ChangingBodyTypeKeepsTheRelativeHeight) {
    const auto& catalog=*newest();
    auto descriptor=*Avatars::decode(storedAvatar(newest(),13));
    descriptor.bodyType=0;
    descriptor.heightMillimeters=static_cast<std::uint16_t>(catalog.authoredHeightMillimeters[0]+40);
    AvatarEditorModel model(newest(),Avatars::encode(descriptor),1);
    ASSERT_TRUE(selectRow(model,"Body"));
    EXPECT_EQ(model.field(model.selection()).value,"Female");
    EXPECT_TRUE(model.adjust(1));
    EXPECT_EQ(model.descriptor().bodyType,1);
    EXPECT_EQ(model.field(model.selection()).value,"Male");
    EXPECT_EQ(model.descriptor().heightMillimeters,catalog.authoredHeightMillimeters[1]+40);
    EXPECT_TRUE(model.choose(0,0));
    EXPECT_EQ(model.descriptor().bodyType,0);
    EXPECT_EQ(model.descriptor().heightMillimeters,catalog.authoredHeightMillimeters[0]+40);
    EXPECT_FALSE(model.choose(0,0));
    EXPECT_FALSE(model.choose(0,2));
}

TEST(AvatarEditorTest, ItemsCycleThroughTheCatalogAndOptionalSlotsOfferNone) {
    AvatarEditorModel model(newest(),storedAvatar(newest(),17),1);
    pageTo(model,EditorCategory::Glasses);
    ASSERT_TRUE(selectRow(model,"Glasses"));
    std::set<std::uint16_t> seen;
    const auto slot=static_cast<std::size_t>(Avatars::AvatarItemSlot::Glasses);
    const auto count=std::ranges::count_if(newest()->items,[](const auto& item){return item.slot==Avatars::AvatarItemSlot::Glasses;});
    EXPECT_EQ(model.options(0).size(),static_cast<std::size_t>(count)+1);
    EXPECT_EQ(model.options(0).front().label,"None");
    for(int i=0;i<=count;++i) {
        seen.insert(model.descriptor().items[slot]);
        model.adjust(1);
    }
    EXPECT_EQ(seen.size(),static_cast<std::size_t>(count)+1);
    EXPECT_TRUE(seen.contains(0));
    while(model.descriptor().items[slot]!=0)model.adjust(1);
    EXPECT_EQ(model.field(model.selection()).value,"None");

    pageTo(model,EditorCategory::Hair);
    ASSERT_TRUE(selectRow(model,"Hairstyle"));
    for(int i=0;i<40;++i) {
        EXPECT_NE(model.descriptor().items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Hair)],0);
        model.adjust(-1);
    }
    pageTo(model,EditorCategory::Tops);
    const auto tops=model.options(0);
    const auto hoodie=std::ranges::find(tops,"Hoodie",&Avatars::EditorOption::label);
    ASSERT_NE(hoodie,tops.end());
    EXPECT_TRUE(model.choose(0,static_cast<std::size_t>(hoodie-tops.begin())));
    EXPECT_EQ(newest()->item(model.descriptor().items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Top)])->name,"top_hoodie");
    EXPECT_EQ(model.field(0).value,"Hoodie");
}

TEST(AvatarEditorTest, ColorsStepThroughThePaletteAndACustomColorJoinsIt) {
    auto descriptor=*Avatars::decode(storedAvatar(newest(),19));
    descriptor.colors[static_cast<std::size_t>(Avatars::AvatarColorSlot::Skin)]={1,2,3};
    AvatarEditorModel model(newest(),Avatars::encode(descriptor),1);
    pageTo(model,EditorCategory::Skin);
    ASSERT_TRUE(selectRow(model,"Skin tone"));
    EXPECT_EQ(model.field(model.selection()).value,"Custom");
    EXPECT_EQ(*model.field(model.selection()).swatch,(Avatars::AvatarColor{1,2,3}));
    EXPECT_TRUE(std::ranges::none_of(model.options(0),&Avatars::EditorOption::current));
    const auto palette=Avatars::colorPalette(Avatars::AvatarColorSlot::Skin);
    EXPECT_TRUE(model.adjust(1));
    EXPECT_EQ(model.descriptor().colors[0],palette[0]);
    EXPECT_EQ(model.field(model.selection()).value,"1 of "+std::to_string(palette.size()));
    model.adjust(-1);
    EXPECT_EQ(model.descriptor().colors[0],palette.back());
    EXPECT_TRUE(model.choose(0,2));
    EXPECT_EQ(model.descriptor().colors[0],palette[2]);
}

TEST(AvatarEditorTest, FaceFeaturesAndFacialHairUseFormatTwo) {
    const auto body=0;
    if(newest()->faceControls[body].empty()||newest()->featureItems.empty())GTEST_SKIP()<<"no format 2 features";
    auto descriptor=*Avatars::decode(storedAvatar(newest(),23));
    descriptor.bodyType=body;
    descriptor.facialHair=0;
    descriptor.face.fill(Avatars::NeutralFaceParameter);
    const auto stored=Avatars::encode(descriptor);
    EXPECT_EQ(stored[0],Avatars::FormatVersion);
    AvatarEditorModel model(newest(),stored,1);
    pageTo(model,EditorCategory::Face);
    ASSERT_TRUE(selectRow(model,"Jaw"));
    EXPECT_EQ(model.field(model.selection()).value,"Average");
    EXPECT_TRUE(model.adjust(2));
    EXPECT_EQ(model.field(model.selection()).value,"A little broad");
    EXPECT_EQ(model.encoded()[0],Avatars::FaceFormatVersion);
    EXPECT_TRUE(model.adjust(-2));
    EXPECT_EQ(model.encoded(),stored);
    pageTo(model,EditorCategory::FacialHair);
    ASSERT_TRUE(selectRow(model,"Style"));
    EXPECT_EQ(model.field(model.selection()).value,"None");
    EXPECT_TRUE(model.adjust(1));
    EXPECT_EQ(model.field(model.selection()).value,"Mustache");
    EXPECT_EQ(model.encoded()[0],Avatars::FaceFormatVersion);
}

TEST(AvatarEditorTest, PresetsMoveSeveralFeaturesAndBalancedResetsThem) {
    auto descriptor=*Avatars::decode(storedAvatar(newest(),43));
    if(newest()->faceControls[descriptor.bodyType].empty())GTEST_SKIP()<<"no face controls";
    descriptor.face.fill(Avatars::NeutralFaceParameter);
    descriptor.facialHair=0;
    AvatarEditorModel model(newest(),Avatars::encode(descriptor),1);
    pageTo(model,EditorCategory::Face);
    ASSERT_EQ(model.field(0).kind,Avatars::EditorRowKind::Presets);
    EXPECT_EQ(model.field(0).value,"Balanced");
    const auto presets=model.options(0);
    std::vector<std::string> names;
    for(const auto& preset:presets)names.push_back(preset.label);
    EXPECT_EQ(names,(std::vector<std::string>{"Balanced","Round","Long","Heart","Square","Soft"}));
    EXPECT_TRUE(model.choose(0,1));
    EXPECT_EQ(model.field(0).value,"Round");
    const auto round=model.descriptor().face;
    EXPECT_GT(std::ranges::count_if(round,[](auto value){return value!=Avatars::NeutralFaceParameter;}),2);
    // A shape set here leaves the eyes alone.
    pageTo(model,EditorCategory::Eyes);
    EXPECT_EQ(model.field(1).value,"Balanced");
    EXPECT_TRUE(model.choose(1,1));
    pageTo(model,EditorCategory::Face);
    EXPECT_EQ(model.field(0).value,"Round");
    ASSERT_TRUE(selectRow(model,"Cheeks"));
    model.adjust(1);
    EXPECT_EQ(model.field(0).value,"Custom");
    EXPECT_TRUE(model.choose(0,0));
    EXPECT_EQ(model.field(0).value,"Balanced");
    pageTo(model,EditorCategory::Eyes);
    EXPECT_EQ(model.field(1).value,"Bright");
}

TEST(AvatarEditorTest, EveryReachableValueEncodes) {
    AvatarEditorModel model(newest(),storedAvatar(newest(),29),1);
    for(int page=0;page<Avatars::EditorCategoryCount;++page) {
        for(std::size_t row=0;row<model.fieldCount();++row) {
            model.selectRow(row);
            for(int step=0;step<24;++step) {
                model.adjust(step<12?1:-1);
                const auto bytes=model.encoded();
                const auto decoded=Avatars::decode(bytes);
                ASSERT_TRUE(decoded.has_value())<<model.field(row).label;
                EXPECT_EQ(*decoded,model.descriptor());
            }
            for(std::size_t option=0;option<model.options(row).size();++option) {
                model.choose(row,option);
                ASSERT_TRUE(Avatars::decode(model.encoded()).has_value())<<model.field(row).label<<" "<<option;
            }
        }
        model.turnPage(1);
    }
}

TEST(AvatarEditorTest, RandomizeKeepsTheBodyAndRevertAndSaveTrackTheStoredAvatar) {
    const auto stored=storedAvatar(newest(),31);
    AvatarEditorModel model(newest(),stored,1);
    const auto body=model.descriptor().bodyType;
    for(int i=0;i<8;++i) {
        model.randomize();
        EXPECT_EQ(model.descriptor().bodyType,body);
        EXPECT_TRUE(Avatars::decode(model.encoded()).has_value());
    }
    EXPECT_TRUE(model.differsFromStored());
    model.revert();
    EXPECT_EQ(model.encoded(),stored);
    model.randomize();
    const auto saved=model.encoded();
    model.markSaved();
    EXPECT_FALSE(model.differsFromStored());
    model.randomize();
    model.revert();
    EXPECT_EQ(model.encoded(),saved);
}

TEST(AvatarEditorTest, RandomAvatarsAreCoherent) {
    AvatarEditorModel model(newest(),storedAvatar(newest(),47),5);
    const auto hair=Avatars::colorPalette(Avatars::AvatarColorSlot::Hair);
    int natural=0,glasses=0,hats=0;
    for(int i=0;i<200;++i) {
        model.randomize();
        const auto& avatar=model.descriptor();
        const auto color=avatar.colors[static_cast<std::size_t>(Avatars::AvatarColorSlot::Hair)];
        natural+=std::ranges::find(hair.first(6),color)!=hair.first(6).end();
        glasses+=avatar.items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Glasses)]!=0;
        hats+=avatar.items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Hat)]!=0;
        // Nothing at an extreme of its range.
        for(auto value:avatar.face)EXPECT_LE(std::abs(value-Avatars::NeutralFaceParameter),80);
        if(avatar.bodyType==0)EXPECT_EQ(avatar.facialHair,0);
    }
    EXPECT_GT(natural,150);
    EXPECT_LT(glasses,80);
    EXPECT_LT(hats,70);
}

TEST(AvatarEditorTest, RandomizingACategoryChangesOnlyIt) {
    AvatarEditorModel model(newest(),storedAvatar(newest(),53),1);
    const auto before=model.descriptor();
    pageTo(model,EditorCategory::Tops);
    for(int i=0;i<6;++i)model.randomizeCategory();
    auto expected=before;
    expected.items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Top)]=model.descriptor().items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Top)];
    expected.colors[static_cast<std::size_t>(Avatars::AvatarColorSlot::Top)]=model.descriptor().colors[static_cast<std::size_t>(Avatars::AvatarColorSlot::Top)];
    EXPECT_EQ(model.descriptor(),expected);
    pageTo(model,EditorCategory::Skin);
    model.randomizeCategory();
    EXPECT_EQ(model.descriptor().items,expected.items);
    EXPECT_EQ(model.descriptor().face,expected.face);
}

TEST(AvatarEditorTest, SelectionStopsAtTheEndsAndResetsOnACategoryChange) {
    AvatarEditorModel model(newest(),storedAvatar(newest(),37),1);
    model.select(-1);
    EXPECT_EQ(model.selection(),0u);
    model.select(10);
    EXPECT_EQ(model.selection(),model.fieldCount()-1);
    model.turnPage(2);
    EXPECT_EQ(model.selection(),0u);
    model.selectRow(99);
    EXPECT_EQ(model.selection(),0u);
    EXPECT_FALSE(model.field(99).label.size());
    EXPECT_TRUE(model.options(99).empty());
    EXPECT_FALSE(model.choose(99,0));
}

TEST(AvatarEditorTest, TheFakeServiceStoresASavedAvatar) {
    Service::ServiceIdentity alice;
    alice.userId="alice-id";
    alice.gamertag="Alice";
    auto fake=Service::makeFakeBackend({alice});
    const auto bytes=storedAvatar(newest(),41);
    const std::vector<unsigned char> description(bytes.begin(),bytes.end());
    EXPECT_EQ(fake->setAvatar("alice-id",description),1);
    EXPECT_EQ(fake->avatars({"alice-id"}).front().description,description);
    EXPECT_EQ(fake->setAvatar("alice-id",description),2);
    EXPECT_ANY_THROW(fake->setAvatar("nobody",description));
    Service::setFakeAvatarsUnreachable(*fake,true);
    EXPECT_ANY_THROW(fake->setAvatar("alice-id",description));
}
