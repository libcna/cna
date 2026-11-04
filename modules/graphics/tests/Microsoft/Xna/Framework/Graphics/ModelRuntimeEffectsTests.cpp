// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "System/InvalidOperationException.hpp"
using namespace Microsoft::Xna::Framework::Graphics;
namespace
{
    // Identity tokens only: these tests never draw or dereference the resources.
    template<class T> T* Token(std::uintptr_t id) { return reinterpret_cast<T*>(id); }
    std::vector<Effect*> Snapshot(const ModelMesh& mesh)
    {
        return {mesh.getEffectsProperty().begin(),mesh.getEffectsProperty().end()};
    }
}
TEST(ModelRuntimeEffectsTest, SharedReplacementRemovalAndReadditionKeepInsertionOrder)
{
    ModelMeshPart a,b,c;
    ModelMesh mesh(nullptr,{&a,&b,&c});
    auto* x = Token<Effect>(1); auto* y = Token<Effect>(2); auto* z = Token<Effect>(3);
    // Refresh ModelMeshPart.set_Effect IL_0025..0064 scans siblings by reference identity;
    // IL_006e..00a2 removes unused old effects then appends new unshared non-null effects.
    a.setEffectProperty(x); b.setEffectProperty(y); c.setEffectProperty(x);
    EXPECT_EQ(Snapshot(mesh),(std::vector<Effect*>{x,y}));
    a.setEffectProperty(z);
    EXPECT_EQ(Snapshot(mesh),(std::vector<Effect*>{x,y,z}));
    c.setEffectProperty(y);
    EXPECT_EQ(Snapshot(mesh),(std::vector<Effect*>{y,z}));
    b.setEffectProperty(nullptr);
    EXPECT_EQ(Snapshot(mesh),(std::vector<Effect*>{y,z}));
    c.setEffectProperty(nullptr);
    EXPECT_EQ(Snapshot(mesh),(std::vector<Effect*>{z}));
    a.setEffectProperty(y);
    EXPECT_EQ(Snapshot(mesh),(std::vector<Effect*>{y}));
    c.setEffectProperty(z);
    EXPECT_EQ(Snapshot(mesh),(std::vector<Effect*>{y,z}));
    EXPECT_EQ(a.getEffectProperty(),y); EXPECT_EQ(b.getEffectProperty(),nullptr);
    EXPECT_EQ(c.getEffectProperty(),z);
}
TEST(ModelRuntimeEffectsTest, SameEffectAndAlreadySharedAssignmentsDoNotMutateCollection)
{
    ModelMeshPart a,b;
    ModelMesh mesh(nullptr,{&a,&b});
    auto* x = Token<Effect>(1);
    a.setEffectProperty(x);
    auto cursor = mesh.getEffectsProperty().GetEnumerator();
    ASSERT_TRUE(cursor.MoveNext());
    // Refresh set_Effect IL_0000..0009 is an immediate no-op; the sibling scan also
    // avoids Add/Remove when b adopts x or relinquishes a still-shared x.
    a.setEffectProperty(x); b.setEffectProperty(x); b.setEffectProperty(nullptr);
    EXPECT_EQ(cursor.Current(),x);
    EXPECT_FALSE(cursor.MoveNext());
    cursor.Reset(); ASSERT_TRUE(cursor.MoveNext()); EXPECT_EQ(cursor.Current(),x);
    auto changed = mesh.getEffectsProperty().GetEnumerator();
    a.setEffectProperty(nullptr);
    EXPECT_THROW((void)changed.MoveNext(),System::InvalidOperationException);
    EXPECT_THROW(changed.Reset(),System::InvalidOperationException);
    EXPECT_TRUE(Snapshot(mesh).empty());
}
TEST(ModelRuntimeEffectsTest, MeshesKeepIndependentEffectMembership)
{
    ModelMeshPart a,b;
    ModelMesh left(nullptr,{&a}), right(nullptr,{&b});
    auto* x = Token<Effect>(1);
    a.setEffectProperty(x); b.setEffectProperty(x); a.setEffectProperty(nullptr);
    // Refresh setter scans only this part's parent.MeshParts (IL_000f..0036).
    EXPECT_TRUE(Snapshot(left).empty());
    EXPECT_EQ(Snapshot(right),(std::vector<Effect*>{x}));
}
TEST(ModelRuntimeEffectsTest, PartGeometryPropertiesDoNotAliasAndCanBeCleared)
{
    auto* vb = Token<VertexBuffer>(1); auto* ib = Token<IndexBuffer>(2);
    ModelMeshPart part(vb,ib,11,13,17,19), other;
    // Refresh ModelMeshPart ctor IL_0007..0026 and get_* IL_0000..0006 are field stores/loads.
    EXPECT_EQ(part.getVertexBufferProperty(),vb); EXPECT_EQ(part.getIndexBufferProperty(),ib);
    EXPECT_EQ(part.getNumVerticesProperty(),11); EXPECT_EQ(part.getPrimitiveCountProperty(),13);
    EXPECT_EQ(part.getStartIndexProperty(),17); EXPECT_EQ(part.getVertexOffsetProperty(),19);
    // CNA construction setters supply the internal XNA fields without requiring an XNB reader.
    part.SetVertexOffset(23); part.SetNumVertices(29); part.SetStartIndex(31);
    part.SetPrimitiveCount(37); part.SetVertexBuffer(nullptr); part.SetIndexBuffer(nullptr);
    EXPECT_EQ(part.getVertexOffsetProperty(),23); EXPECT_EQ(part.getNumVerticesProperty(),29);
    EXPECT_EQ(part.getStartIndexProperty(),31); EXPECT_EQ(part.getPrimitiveCountProperty(),37);
    EXPECT_EQ(part.getVertexBufferProperty(),nullptr); EXPECT_EQ(part.getIndexBufferProperty(),nullptr);
    EXPECT_EQ(other.getNumVerticesProperty(),0); EXPECT_EQ(other.getVertexOffsetProperty(),0);
}
