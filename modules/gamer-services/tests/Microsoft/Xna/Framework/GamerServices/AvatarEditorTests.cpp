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

TEST(AvatarEditorTest, PagesShowBodyFeaturesAndStyle) {
    AvatarEditorModel model(newest(),storedAvatar(newest(),5),1);
    EXPECT_EQ(model.category(),EditorCategory::Body);
    EXPECT_EQ(labels(model),(std::vector<std::string>{"Body","Height","Build","Skin"}));
    model.turnPage(1);
    EXPECT_EQ(model.category(),EditorCategory::Features);
    const auto features=labels(model);
    ASSERT_FALSE(features.empty());
    EXPECT_EQ(features.front(),"Eye color");
    const auto body=model.descriptor().bodyType==1?1:0;
    if(!newest()->faceControls[body].empty()) {
        EXPECT_NE(std::ranges::find(features,"Facial hair"),features.end());
        EXPECT_NE(std::ranges::find(features,"Jaw width"),features.end());
        EXPECT_EQ(std::set<std::string>(features.begin(),features.end()).size(),features.size());
    }
    model.turnPage(1);
    EXPECT_EQ(labels(model),(std::vector<std::string>{"Hair","Hair color","Top","Top color","Bottom","Bottom color",
        "Shoes","Shoe color","Glasses","Hat","Accessory color"}));
    model.turnPage(1);
    EXPECT_EQ(model.category(),EditorCategory::Body);
    model.turnPage(-1);
    EXPECT_EQ(model.category(),EditorCategory::Style);
}

TEST(AvatarEditorTest, CatalogOneOffersNoFaceShapeOrFacialHair) {
    AvatarEditorModel model(first(),storedAvatar(first(),5),1);
    pageTo(model,EditorCategory::Features);
    EXPECT_EQ(labels(model),(std::vector<std::string>{"Eye color"}));
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
    for(int i=0;i<100;++i)model.adjust(-1);
    EXPECT_EQ(model.descriptor().heightMillimeters,Avatars::MinimumHeightMillimeters);
    EXPECT_EQ(model.field(model.selection()).value,"1.45 m");

    ASSERT_TRUE(selectRow(model,"Build"));
    for(int i=0;i<20;++i)model.adjust(-1);
    EXPECT_EQ(model.descriptor().build,0);
    EXPECT_EQ(model.field(model.selection()).value,"-8");
    EXPECT_FLOAT_EQ(*model.field(model.selection()).slider,-1.0f);
    for(int i=0;i<8;++i)model.adjust(1);
    EXPECT_EQ(model.descriptor().build,Avatars::NeutralFaceParameter);
    EXPECT_EQ(model.field(model.selection()).value,"0");
    for(int i=0;i<20;++i)model.adjust(1);
    EXPECT_EQ(model.descriptor().build,255);
    EXPECT_EQ(model.field(model.selection()).value,"+8");
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
}

TEST(AvatarEditorTest, ItemsCycleThroughTheCatalogAndOptionalSlotsOfferNone) {
    AvatarEditorModel model(newest(),storedAvatar(newest(),17),1);
    pageTo(model,EditorCategory::Style);
    ASSERT_TRUE(selectRow(model,"Glasses"));
    std::set<std::uint16_t> seen;
    const auto slot=static_cast<std::size_t>(Avatars::AvatarItemSlot::Glasses);
    const auto count=std::ranges::count_if(newest()->items,[](const auto& item){return item.slot==Avatars::AvatarItemSlot::Glasses;});
    for(int i=0;i<=count;++i) {
        seen.insert(model.descriptor().items[slot]);
        model.adjust(1);
    }
    EXPECT_EQ(seen.size(),static_cast<std::size_t>(count)+1);
    EXPECT_TRUE(seen.contains(0));
    while(model.descriptor().items[slot]!=0)model.adjust(1);
    EXPECT_EQ(model.field(model.selection()).value,"None");

    ASSERT_TRUE(selectRow(model,"Hair"));
    for(int i=0;i<40;++i) {
        EXPECT_NE(model.descriptor().items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Hair)],0);
        model.adjust(-1);
    }
    ASSERT_TRUE(selectRow(model,"Top"));
    while(newest()->item(model.descriptor().items[static_cast<std::size_t>(Avatars::AvatarItemSlot::Top)])->name!="top_hoodie")
        model.adjust(1);
    EXPECT_EQ(model.field(model.selection()).value,"Hoodie");
}

TEST(AvatarEditorTest, ColorsStepThroughThePaletteAndACustomColorJoinsIt) {
    auto descriptor=*Avatars::decode(storedAvatar(newest(),19));
    descriptor.colors[static_cast<std::size_t>(Avatars::AvatarColorSlot::Skin)]={1,2,3};
    AvatarEditorModel model(newest(),Avatars::encode(descriptor),1);
    ASSERT_TRUE(selectRow(model,"Skin"));
    EXPECT_EQ(model.field(model.selection()).value,"Custom");
    EXPECT_EQ(*model.field(model.selection()).swatch,(Avatars::AvatarColor{1,2,3}));
    const auto palette=Avatars::colorPalette(Avatars::AvatarColorSlot::Skin);
    EXPECT_TRUE(model.adjust(1));
    EXPECT_EQ(model.descriptor().colors[0],palette[0]);
    EXPECT_EQ(model.field(model.selection()).value,"1 of "+std::to_string(palette.size()));
    model.adjust(-1);
    EXPECT_EQ(model.descriptor().colors[0],palette.back());
}

TEST(AvatarEditorTest, FaceShapeAndFacialHairUseFormatTwo) {
    const auto body=0;
    if(newest()->faceControls[body].empty()||newest()->featureItems.empty())GTEST_SKIP()<<"no format 2 features";
    auto descriptor=*Avatars::decode(storedAvatar(newest(),23));
    descriptor.bodyType=body;
    descriptor.facialHair=0;
    descriptor.face.fill(Avatars::NeutralFaceParameter);
    const auto stored=Avatars::encode(descriptor);
    EXPECT_EQ(stored[0],Avatars::FormatVersion);
    AvatarEditorModel model(newest(),stored,1);
    pageTo(model,EditorCategory::Features);
    ASSERT_TRUE(selectRow(model,"Jaw width"));
    EXPECT_EQ(model.field(model.selection()).value,"0");
    EXPECT_TRUE(model.adjust(2));
    EXPECT_EQ(model.field(model.selection()).value,"+2");
    EXPECT_EQ(model.encoded()[0],Avatars::FaceFormatVersion);
    EXPECT_TRUE(model.adjust(-2));
    EXPECT_EQ(model.encoded(),stored);
    ASSERT_TRUE(selectRow(model,"Facial hair"));
    EXPECT_EQ(model.field(model.selection()).value,"None");
    EXPECT_TRUE(model.adjust(1));
    EXPECT_EQ(model.field(model.selection()).value,"Mustache");
    EXPECT_EQ(model.encoded()[0],Avatars::FaceFormatVersion);
}

TEST(AvatarEditorTest, EveryReachableValueEncodes) {
    AvatarEditorModel model(newest(),storedAvatar(newest(),29),1);
    for(int page=0;page<Avatars::EditorCategoryCount;++page) {
        for(std::size_t row=0;row<model.fieldCount();++row) {
            while(model.selection()!=row)model.select(1);
            for(int step=0;step<24;++step) {
                model.adjust(step<12?1:-1);
                const auto bytes=model.encoded();
                const auto decoded=Avatars::decode(bytes);
                ASSERT_TRUE(decoded.has_value())<<model.field(row).label;
                EXPECT_EQ(*decoded,model.descriptor());
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

TEST(AvatarEditorTest, SelectionWrapsAndSurvivesPageChanges) {
    AvatarEditorModel model(newest(),storedAvatar(newest(),37),1);
    model.select(-1);
    EXPECT_EQ(model.selection(),model.fieldCount()-1);
    model.select(1);
    EXPECT_EQ(model.selection(),0u);
    model.select(3);
    model.turnPage(2);
    EXPECT_EQ(model.selection(),0u);
    EXPECT_FALSE(model.field(99).label.size());
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
