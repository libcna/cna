// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <typeinfo>
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    struct TestTag : System::Object
    {
        const std::string& GetTypeName() const override
        {
            static const std::string name = "TQ046.TestTag";
            return name;
        }
    };
    template<class Collection, class Element>
    void CheckNames(const Collection& collection, Element* first, Element* differentlyCased)
    {
        // Refresh Bone/MeshCollection.TryGetValue IL_0030..0043: Ordinal (enum 4), first match.
        Element* found = differentlyCased;
        EXPECT_TRUE(collection.TryGetValue("Name",found)); EXPECT_EQ(found,first);
        EXPECT_EQ(collection[std::string("Name")],first);
        EXPECT_TRUE(collection.TryGetValue("name",found)); EXPECT_EQ(found,differentlyCased);
        found = first;
        EXPECT_FALSE(collection.TryGetValue("NAME",found)); EXPECT_EQ(found,nullptr);
        found = first;
        EXPECT_FALSE(collection.TryGetValue("missing",found)); EXPECT_EQ(found,nullptr);
        // get_Item(string) IL_000b..0010 calls the default BCL constructor.
        // Pinned .NET 4 KeyNotFoundException::.ctor IL_0001..000b, Arg_KeyNotFound resource.
        try { (void)collection[std::string("missing")]; FAIL() << "missing name accepted"; }
        catch (const System::Collections::Generic::KeyNotFoundException& e)
        {
            EXPECT_EQ(typeid(e),typeid(System::Collections::Generic::KeyNotFoundException));
            EXPECT_STREQ(e.what(),"The given key was not present in the dictionary.");
        }
        catch (...) { FAIL() << "wrong exception type"; }
    }
    template<class Collection, class Element>
    void CheckEmptyName(const Collection& collection, Element* sentinel, const char* parameter)
    {
        // Refresh TryGetValue IL_0000..0012: empty name refuses before writing the out-ref.
        Element* found = sentinel;
        try { (void)collection.TryGetValue("",found); FAIL() << "empty name accepted"; }
        catch (const System::ArgumentNullException& e)
        {
            EXPECT_EQ(typeid(e),typeid(System::ArgumentNullException));
            EXPECT_EQ(e.getParamNameProperty(),parameter);
        }
        catch (...) { FAIL() << "wrong exception type"; }
        EXPECT_EQ(found,sentinel);
        try { (void)collection[std::string()]; FAIL() << "empty indexer name accepted"; }
        catch (const System::ArgumentNullException& e)
        {
            EXPECT_EQ(typeid(e),typeid(System::ArgumentNullException));
            EXPECT_EQ(e.getParamNameProperty(),parameter);
        }
        catch (...) { FAIL() << "wrong exception type"; }
    }
}

TEST(ModelRuntimeCollectionsTest, NamesAreOrdinalFirstMatchAndMissingClearsOutput)
{
    ModelBone a(0,"Name"), b(1,"Name"), c(2,"name");
    ModelMesh x(nullptr,"Name",{}), y(nullptr,"Name",{}), z(nullptr,"name",{});
    Model model(nullptr,{&a,&b,&c},{&x,&y,&z});
    CheckNames(model.getBonesProperty(),&a,&c);
    CheckNames(model.getMeshesProperty(),&x,&z);
    CheckEmptyName(model.getBonesProperty(),&b,"boneName");
    CheckEmptyName(model.getMeshesProperty(),&y,"meshName");
}

TEST(ModelRuntimeCollectionsTest, BoneTransformAndHierarchyAccessorsPreserveIdentityAndOrder)
{
    ModelBone root(0,"root"), a(1,"a"), b(2,"b");
    root.AddChild(&a); root.AddChild(&b);
    const Matrix value(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    // Refresh ModelBone get/set_Transform IL_0000..0007 stores all 16 entries.
    root.setTransformProperty(value);
    EXPECT_EQ(root.getTransformProperty(),value);
    EXPECT_EQ(a.getTransformProperty(),Matrix(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1));
    // ModelBone get_Children/get_Parent/get_Index/get_Name IL_0000..0006.
    EXPECT_EQ(root.getParentProperty(),nullptr);
    EXPECT_EQ(a.getParentProperty(),&root); EXPECT_EQ(b.getParentProperty(),&root);
    ASSERT_EQ(root.getChildrenProperty().getCountProperty(),2);
    EXPECT_EQ(root.getChildrenProperty()[0],&a); EXPECT_EQ(root.getChildrenProperty()[1],&b);
    EXPECT_EQ(a.getIndexProperty(),1); EXPECT_EQ(b.getNameProperty(),"b");
    Model model(nullptr,{&root,&a,&b},{},{},2);
    // Model.get_Root IL_0000..0006 returns the stored root, independent of collection order.
    EXPECT_EQ(model.getRootProperty(),&b);
    EXPECT_EQ(model.getBonesProperty()[0],&root);
}

TEST(ModelRuntimeCollectionsTest, MeshMetadataAndTagsAreIndependentAndClearable)
{
    ModelBone parent(0,"parent");
    ModelMeshPart a, b;
    ModelMesh mesh(nullptr,"mesh",{&a,&b});
    Model model(nullptr,{&parent},{&mesh},{&parent});
    TestTag modelTag, meshTag, partTag;
    // Refresh Model/ModelMesh/ModelMeshPart get_Tag IL_0000..0006, set_Tag ..0007.
    model.setTagProperty(&modelTag); mesh.setTagProperty(&meshTag); a.setTagProperty(&partTag);
    EXPECT_EQ(model.getTagProperty(),&modelTag); EXPECT_EQ(mesh.getTagProperty(),&meshTag);
    EXPECT_EQ(a.getTagProperty(),&partTag); EXPECT_EQ(b.getTagProperty(),nullptr);
    // ModelMesh get_BoundingSphere returns the full stored value, not a transformed bound.
    mesh.setBoundingSphereProperty(BoundingSphere(Vector3(2,3,5),7));
    const auto sphere = mesh.getBoundingSphereProperty();
    EXPECT_EQ(sphere.Center,Vector3(2,3,5)); EXPECT_FLOAT_EQ(sphere.Radius,7);
    EXPECT_EQ(mesh.getNameProperty(),"mesh"); EXPECT_EQ(mesh.getParentBoneProperty(),&parent);
    ASSERT_EQ(mesh.getMeshPartsProperty().getCountProperty(),2);
    EXPECT_EQ(mesh.getMeshPartsProperty()[0],&a); EXPECT_EQ(mesh.getMeshPartsProperty()[1],&b);
    model.setTagProperty(nullptr); mesh.setTagProperty(nullptr); a.setTagProperty(nullptr);
    EXPECT_EQ(model.getTagProperty(),nullptr); EXPECT_EQ(mesh.getTagProperty(),nullptr);
    EXPECT_EQ(a.getTagProperty(),nullptr);
}
