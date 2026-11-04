// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <string>
#include <typeinfo>
#include <vector>
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NullReferenceException.hpp"
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
namespace
{
    constexpr const char* NoEffect = "ModelMeshPart has a null Effect.";
    constexpr const char* NoMatrices = "This model contains a custom effect which does not implement the IEffectMatrices interface, so it cannot be drawn using Model.Draw. Instead, call ModelMesh.Draw after setting the appropriate effect parameters.";
    template<class Exception, class F> void ExpectExact(F action, const char* message)
    {
        try { action(); FAIL() << "expected refusal"; }
        catch (const Exception& e)
        {
            EXPECT_EQ(typeid(e),typeid(Exception));
            if (message != nullptr) EXPECT_STREQ(e.what(),message);
        }
        catch (...) { FAIL() << "wrong exception type"; }
    }
    struct SpyEffect : Effect, IEffectMatrices
    {
        explicit SpyEffect(GraphicsDevice& device, int identity, std::vector<int>& trace)
            : Effect(device), identity(identity), trace(trace) {}
        Matrix world{},view{},projection{};
        std::vector<Matrix> appliedWorlds,appliedViews,appliedProjections;
        std::vector<std::string> techniques;
        int identity;
        std::vector<int>& trace;
        int worldSets=0,viewSets=0,projectionSets=0;
        EffectTechnique* switchTo=nullptr;
        Matrix getWorldProperty() const override { return world; }
        Matrix getViewProperty() const override { return view; }
        Matrix getProjectionProperty() const override { return projection; }
        void setWorldProperty(const Matrix& x) override { world=x; ++worldSets; }
        void setViewProperty(const Matrix& x) override { view=x; ++viewSets; }
        void setProjectionProperty(const Matrix& x) override { projection=x; ++projectionSets; }
        void OnApply() override
        {
            trace.push_back(identity); appliedWorlds.push_back(world);
            appliedViews.push_back(view);appliedProjections.push_back(projection);
            techniques.push_back(getCurrentTechniqueProperty()->getNameProperty());
            if (switchTo != nullptr)
            {
                auto* next=switchTo; switchTo=nullptr; setCurrentTechniqueProperty(next);
            }
        }
        EffectTechnique* AddTechnique(const std::string& name, int count)
        {
            EffectTechnique technique(this,name,0,false);
            for (int i=0;i<count;++i)
                technique.getPassesProperty().Add(EffectPass(this,"p"+std::to_string(i),
                                                           technique.getIdInternal(),i));
            getTechniquesProperty().Add(std::move(technique));
            return getTechniquesProperty()[name];
        }
    };
}
TEST(ModelRuntimeDrawCpuTest, NullEffectRefusesEvenForAnEmptyPart)
{
    ModelMeshPart part;
    ModelMesh mesh(nullptr,{&part});
    // Refresh ModelMesh.Draw IL_001e..0032 validates Effect before the part's draw gate.
    // Message is the embedded FrameworkResources.ModelHasNoEffect, not inferred from its key.
    ExpectExact<System::InvalidOperationException>([&]{mesh.Draw();},NoEffect);
}
TEST(ModelRuntimeDrawCpuTest, NullCollectionEffectReportsXnaMessage)
{
    ModelBone root(0,"root"); ModelMesh mesh(nullptr,{});
    // CNA's mutable collection seam supplies the state guarded by Refresh Model.Draw IL_0081..008f.
    mesh.getEffectsPropertyMutable().Add(nullptr);
    Model model(nullptr,{&root},{&mesh},{&root});
    const auto id=Matrix::getIdentityProperty();
    ExpectExact<System::InvalidOperationException>([&]{model.Draw(id,id,id);},NoEffect);
}
TEST(ModelRuntimeDrawCpuTest, MissingParentRefusesBeforeInspectingEffects)
{
    ModelBone root(0,"root"); ModelMesh mesh(nullptr,{});
    Model model(nullptr,{&root},{&mesh});
    const auto id=Matrix::getIdentityProperty();
    // Refresh Model.Draw IL_0055..0060 dereferences ParentBone even if Effects is empty.
    ExpectExact<System::NullReferenceException>([&]{model.Draw(id,id,id);},nullptr);
}
TEST(ModelRuntimeDrawGpuTest, EmptyPartsApplyEveryCurrentPassInPartOrder)
{
    GraphicsDevice device;
    std::vector<int> trace;
    SpyEffect a(device,1,trace), b(device,2,trace);
    a.setCurrentTechniqueProperty(a.AddTechnique("selected",2));
    a.AddTechnique("unused",3);
    b.setCurrentTechniqueProperty(b.AddTechnique("selected",1));
    ModelMeshPart p0(nullptr,nullptr,0,0,0,0),p1(nullptr,nullptr,0,0,0,0),p2;
    ModelMesh mesh(&device,{&p0,&p1,&p2});
    p0.setEffectProperty(&a);p1.setEffectProperty(&b);p2.setEffectProperty(&a);
    // Refresh ModelMesh.Draw IL_0033..0060: one pass loop for every part, not every unique effect.
    // ModelMeshPart.Draw IL_0000..0007 skips geometry for NumVertices<=0 after Apply.
    mesh.Draw();
    EXPECT_EQ(trace,(std::vector<int>{1,1,2,1,1}));
    EXPECT_EQ(a.techniques,(std::vector<std::string>{"selected","selected","selected","selected"}));
    EXPECT_EQ(b.techniques,(std::vector<std::string>{"selected"}));
    EXPECT_EQ(a.worldSets,0);EXPECT_EQ(a.viewSets,0);EXPECT_EQ(a.projectionSets,0);
    trace.clear();p0.SetNumVertices(-1);
    mesh.Draw();
    EXPECT_EQ(trace,(std::vector<int>{1,1,2,1,1}));
}
TEST(ModelRuntimeDrawGpuTest, PassLoopReReadsCurrentTechniqueAfterApply)
{
    GraphicsDevice device; std::vector<int> trace;
    SpyEffect effect(device,1,trace);
    auto* first=effect.AddTechnique("first",2);
    auto* second=effect.AddTechnique("second",3);
    effect.setCurrentTechniqueProperty(first);effect.switchTo=second;
    ModelMeshPart part;ModelMesh mesh(&device,{&part});part.setEffectProperty(&effect);
    // Refresh Draw captures count at IL_0033..0043 but re-fetches CurrentTechnique at IL_0049.
    mesh.Draw();
    EXPECT_EQ(trace,(std::vector<int>{1,1}));
    EXPECT_EQ(effect.techniques,(std::vector<std::string>{"first","second"}));
    trace.clear();effect.techniques.clear();
    auto* shorter=effect.AddTechnique("shorter",1);
    effect.setCurrentTechniqueProperty(first);effect.switchTo=shorter;
    // Refresh EffectPassCollection.get_Item(int) IL_001f..0020 returns null for pass 1;
    // ModelMesh.Draw IL_005a callvirt then throws NullReferenceException.
    ExpectExact<System::NullReferenceException>([&]{mesh.Draw();},nullptr);
    EXPECT_EQ(trace,(std::vector<int>{1}));
    EXPECT_EQ(effect.techniques,(std::vector<std::string>{"first"}));
}
TEST(ModelRuntimeDrawGpuTest, ModelSetsEachUniqueEffectOncePerMeshBeforeApplyingPasses)
{
    GraphicsDevice device;std::vector<int> trace;
    SpyEffect a(device,1,trace),b(device,2,trace);
    a.setCurrentTechniqueProperty(a.AddTechnique("a",1));
    b.setCurrentTechniqueProperty(b.AddTechnique("b",1));
    ModelBone root(0,"root"),child(1,"child");root.AddChild(&child);
    root.setTransformProperty(Matrix(2,0,0,0, 0,3,0,0, 0,0,4,0, 5,6,7,1));
    child.setTransformProperty(Matrix(1,0,0,0, 0,1,0,0, 0,0,1,0, 11,13,17,1));
    ModelMeshPart p0,p1,p2,p3;
    ModelMesh m0(nullptr,{&p0,&p1,&p2}),m1(nullptr,{&p3});
    p0.setEffectProperty(&a);p1.setEffectProperty(&a);p2.setEffectProperty(&b);p3.setEffectProperty(&a);
    Model model(nullptr,{&root,&child},{&m0,&m1},{&child,&root});
    const Matrix world(5,0,0,0, 0,7,0,0, 0,0,11,0, 19,23,29,1);
    const Matrix view(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    const Matrix projection(16,15,14,13, 12,11,10,9, 8,7,6,5, 4,3,2,1);
    // Refresh Model.Draw IL_00a6..00cc: absolute[parent.Index]*world, then view/projection.
    // Child absolute translation=(27,45,75); *world gives (154,338,854).
    // Root *world gives (44,65,106). Scales=(10,21,44). All arithmetic is exact binary32.
    const Matrix childWorld(10,0,0,0, 0,21,0,0, 0,0,44,0, 154,338,854,1);
    const Matrix rootWorld(10,0,0,0, 0,21,0,0, 0,0,44,0, 44,65,106,1);
    model.Draw(world,view,projection);
    EXPECT_EQ(trace,(std::vector<int>{1,1,2,1}));
    EXPECT_EQ(a.appliedWorlds,(std::vector<Matrix>{childWorld,childWorld,rootWorld}));
    EXPECT_EQ(b.appliedWorlds,(std::vector<Matrix>{childWorld}));
    EXPECT_EQ(a.worldSets,2);EXPECT_EQ(a.viewSets,2);EXPECT_EQ(a.projectionSets,2);
    EXPECT_EQ(b.worldSets,1);EXPECT_EQ(b.viewSets,1);EXPECT_EQ(b.projectionSets,1);
    EXPECT_EQ(a.appliedViews,(std::vector<Matrix>{view,view,view}));
    EXPECT_EQ(b.appliedViews,(std::vector<Matrix>{view}));
    EXPECT_EQ(a.appliedProjections,(std::vector<Matrix>{projection,projection,projection}));
    EXPECT_EQ(b.appliedProjections,(std::vector<Matrix>{projection}));
}
TEST(ModelRuntimeDrawGpuTest, ModelReportsTheExactNonMatrixEffectMessage)
{
    GraphicsDevice device; Effect effect(device);
    ModelBone root(0,"root"); ModelMeshPart part;ModelMesh mesh(&device,{&part});
    part.setEffectProperty(&effect);Model model(nullptr,{&root},{&mesh},{&root});
    const auto id=Matrix::getIdentityProperty();
    // Refresh Model.Draw IL_0090..00a5, embedded ModelHasNoIEffectMatrices resource.
    ExpectExact<System::InvalidOperationException>([&]{model.Draw(id,id,id);},NoMatrices);
}
TEST(ModelRuntimeDrawGpuTest, PositiveVertexCountAppliesBeforeRefusingNullVertexBuffer)
{
    GraphicsDevice device;std::vector<int> trace;SpyEffect effect(device,1,trace);
    effect.setCurrentTechniqueProperty(effect.AddTechnique("selected",1));
    ModelMeshPart part(nullptr,nullptr,3,1,0,0);ModelMesh mesh(&device,{&part});
    part.setEffectProperty(&effect);
    // Refresh ModelMesh.Draw IL_005a..0060 applies first; Part.Draw IL_0009..000f dereferences VB.
    ExpectExact<System::NullReferenceException>([&]{mesh.Draw();},nullptr);
    EXPECT_EQ(trace,(std::vector<int>{1}));
}
TEST(ModelRuntimeDrawGpuTest, VertexOffsetIsBoundBeforeTheZeroPrimitiveRefusal)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) GTEST_SKIP()<<"3D required";
    VertexBuffer vb(device,5);
    std::vector<int> trace;SpyEffect effect(device,1,trace);
    effect.setCurrentTechniqueProperty(effect.AddTechnique("selected",1));
    ModelMeshPart part(&vb,nullptr,3,0,7,2);ModelMesh mesh(&device,{&part});part.setEffectProperty(&effect);
    // Refresh Part.Draw IL_0015..0022 binds offset=2, IL_0034..0049 draws baseVertex=0.
    // NumVertices>0 must reach DrawIndexedPrimitives even if PrimitiveCount==0.
    try { mesh.Draw(); FAIL()<<"zero primitive draw accepted"; }
    catch (const System::ArgumentOutOfRangeException& e) { EXPECT_EQ(e.getParamNameProperty(),"primitiveCount"); }
    catch (...) { FAIL()<<"wrong exception type"; }
    EXPECT_EQ(trace,(std::vector<int>{1}));
    const auto bindings=device.GetVertexBuffers();ASSERT_EQ(bindings.size(),1u);
    EXPECT_EQ(bindings[0].getVertexBufferProperty(),&vb);EXPECT_EQ(bindings[0].getVertexOffsetProperty(),2);
}
