// SPDX-License-Identifier: MS-PL
// Task 351: Effect base class audit — constructors, parameters, techniques,
// Apply()/OnApply() dispatch, GetTypeName(), and disposal, against FNA's
// Graphics/Effect/Effect.cs. CNA's Effect has no MojoShader/.fx-bytecode
// pipeline (OnApply() is pure virtual), so these tests exercise the
// construction-time single-"Default"-technique contract CNA actually uses,
// not FNA's byte[]-blob constructor (that policy is Task 352's scope).

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/NotImplementedException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EffectPass;
using Microsoft::Xna::Framework::Graphics::EffectTechnique;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    // Minimal concrete Effect: OnApply() is pure virtual in CNA (no base-class
    // bytecode/MojoShader pipeline exists), so every test needs a subclass.
    class TestEffect : public Effect
    {
    public:
        explicit TestEffect(GraphicsDevice& device) : Effect(device) {}
        TestEffect(GraphicsDevice& device, const std::vector<SharpRuntime::bytecs>& effectCode)
            : Effect(device, effectCode) {}

        int applyCount = 0;

    protected:
        void OnApply() override { ++applyCount; }
    };
}

// -----------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------

TEST(EffectTest, ConstructorCreatesExactlyOneDefaultTechnique)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    ASSERT_EQ(fx.getTechniquesProperty().getCountProperty(), 1);
    EXPECT_EQ(fx.getTechniquesProperty()[0].getNameProperty(), "Default");
}

TEST(EffectTest, ConstructorSelectsFirstTechniqueAsCurrent)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    ASSERT_NE(fx.getCurrentTechniqueProperty(), nullptr);
    EXPECT_EQ(fx.getCurrentTechniqueProperty(), &fx.getTechniquesProperty()[0]);
}

TEST(EffectTest, ConstructorLeavesParametersEmpty)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    EXPECT_EQ(fx.getParametersProperty().getCountProperty(), 0);
    EXPECT_EQ(std::as_const(fx).getParametersProperty().getCountProperty(), 0);
}

TEST(EffectTest, GetTechniquesConstOverloadMatchesMutable)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    const Effect& constFx = fx;
    EXPECT_EQ(&constFx.getTechniquesProperty()[0], &fx.getTechniquesProperty()[0]);
}

TEST(EffectTest, GraphicsDeviceInternalReturnsOwningDevice)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    EXPECT_EQ(&fx.getGraphicsDeviceInternal(), &gd);
}

// -----------------------------------------------------------------------
// Task 353: interim safety net — the bytecode constructor exists (matching
// FNA's public API surface) but must throw until Phase 74's real MojoShader-
// equivalent bytecode pipeline lands, rather than silently building a
// broken/fake Effect.
// -----------------------------------------------------------------------

TEST(EffectTest, BytecodeConstructorThrowsNotImplementedException)
{
    GraphicsDevice gd;
    const std::vector<SharpRuntime::bytecs> fakeBytecode{ 1, 2, 3, 4 };

    EXPECT_THROW(TestEffect(gd, fakeBytecode), System::NotImplementedException);
}

TEST(EffectTest, BytecodeConstructorMessageMentionsPhase74Roadmap)
{
    GraphicsDevice gd;
    const std::vector<SharpRuntime::bytecs> fakeBytecode{ 1, 2, 3, 4 };

    try
    {
        TestEffect fx(gd, fakeBytecode);
        FAIL() << "Expected System::NotImplementedException";
    }
    catch (const System::NotImplementedException& e)
    {
        EXPECT_NE(std::string(e.what()).find("Phase 74"), std::string::npos);
    }
}

// -----------------------------------------------------------------------
// CurrentTechnique — FNA's setter performs zero validation (any
// EffectTechnique* is accepted, even one not owned by this Effect).
// -----------------------------------------------------------------------

TEST(EffectTest, SetCurrentTechniqueAcceptsAnyPointerWithoutValidation)
{
    GraphicsDevice gd;
    TestEffect fx(gd);
    EffectTechnique unrelated(nullptr, "Unrelated");

    fx.setCurrentTechniqueProperty(&unrelated);

    EXPECT_EQ(fx.getCurrentTechniqueProperty(), &unrelated);
}

TEST(EffectTest, SetCurrentTechniqueAcceptsNull)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    fx.setCurrentTechniqueProperty(nullptr);

    EXPECT_EQ(fx.getCurrentTechniqueProperty(), nullptr);
}

// -----------------------------------------------------------------------
// Apply() — dispatches to OnApply() and makes the effect the device's
// current effect (verified indirectly: DrawPrimitives requires a
// previously-applied effect, matching GraphicsDevice::DrawPrimitives's
// "no effect has been applied" guard).
// -----------------------------------------------------------------------

class EffectApplyTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
    TestEffect fx{gd};

    VertexDeclaration MakeDecl()
    {
        return VertexDeclaration({
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)
        });
    }
};

TEST_F(EffectApplyTest, ApplyInvokesOnApply)
{
    EXPECT_EQ(fx.applyCount, 0);
    fx.Apply();
    EXPECT_EQ(fx.applyCount, 1);
    fx.Apply();
    EXPECT_EQ(fx.applyCount, 2);
}

TEST_F(EffectApplyTest, DrawPrimitivesThrowsWithoutPriorApply)
{
    std::vector<VertexPositionColor> vpc {
        { Vector3(0.f, 0.f, 0.f), Color(255, 0, 0, 255) },
        { Vector3(1.f, 0.f, 0.f), Color(0, 255, 0, 255) },
        { Vector3(0.f, 1.f, 0.f), Color(0, 0, 255, 255) }
    };
    VertexBuffer vb(gd, MakeDecl(), 3, BufferUsage::None);
    vb.SetData(vpc.data(), 3);
    gd.SetVertexBuffer(&vb);

    EXPECT_THROW(gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1), std::runtime_error);
}

TEST_F(EffectApplyTest, ApplyMakesEffectCurrentSoDrawPrimitivesNoLongerThrowsForMissingEffect)
{
    std::vector<VertexPositionColor> vpc {
        { Vector3(0.f, 0.f, 0.f), Color(255, 0, 0, 255) },
        { Vector3(1.f, 0.f, 0.f), Color(0, 255, 0, 255) },
        { Vector3(0.f, 1.f, 0.f), Color(0, 0, 255, 255) }
    };
    VertexBuffer vb(gd, MakeDecl(), 3, BufferUsage::None);
    vb.SetData(vpc.data(), 3);
    gd.SetVertexBuffer(&vb);

    fx.Apply();

    // "no effect has been applied" is exactly the guard fx.Apply() must clear;
    // any other exception (or none) means the guard was satisfied.
    try
    {
        gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_STRNE(e.what(), std::string("GraphicsDevice::DrawPrimitives: no effect has been applied.").c_str());
    }
}

TEST_F(EffectApplyTest, ApplyAfterDisposeThrowsObjectDisposedException)
{
    fx.Dispose();
    EXPECT_THROW(fx.Apply(), System::ObjectDisposedException);
}

// -----------------------------------------------------------------------
// Task 355: EffectPass::Apply() must throw System::InvalidOperationException
// ("Applied a pass not in the current technique!") when applied while it is
// not part of the effect's CurrentTechnique, matching FNA's own
// EffectPass.Apply() guard (Graphics/Effect/EffectPass.cs). Uses a mock
// TestEffect with a manually-added second technique since no real CNA stock
// effect creates more than one technique today.
// -----------------------------------------------------------------------

TEST_F(EffectApplyTest, ApplyOnPassOfCurrentTechniqueSucceedsAndInvokesOnApply)
{
    EffectPass& p0 = fx.getTechniquesProperty()[0].getPassesProperty()[0];

    EXPECT_NO_THROW(p0.Apply());
    EXPECT_EQ(fx.applyCount, 1);
}

TEST_F(EffectApplyTest, ApplyOnPassNotInCurrentTechniqueThrowsInvalidOperationException)
{
    EffectPass& originalPass = fx.getTechniquesProperty()[0].getPassesProperty()[0];
    EffectTechnique unrelated(nullptr, "Unrelated");

    fx.setCurrentTechniqueProperty(&unrelated);

    EXPECT_THROW(originalPass.Apply(), System::InvalidOperationException);
    EXPECT_EQ(fx.applyCount, 0);
}

TEST_F(EffectApplyTest, ApplyWithNullCurrentTechniqueThrowsInvalidOperationException)
{
    EffectPass& p0 = fx.getTechniquesProperty()[0].getPassesProperty()[0];

    fx.setCurrentTechniqueProperty(nullptr);

    // FNA dereferences CurrentTechnique.TechniquePointer unconditionally here and would
    // crash with a NullReferenceException; CNA maps that to this same, defined exception
    // instead of undefined behavior (an intentional, documented deviation).
    EXPECT_THROW(p0.Apply(), System::InvalidOperationException);
}

TEST_F(EffectApplyTest, ApplyIsConsistentAcrossInterleavedTechniqueSwitches)
{
    fx.getTechniquesProperty().Add(EffectTechnique(&fx, "Second"));
    EffectPass& firstPass = fx.getTechniquesProperty()[0].getPassesProperty()[0];
    EffectPass& secondPass = fx.getTechniquesProperty()[1].getPassesProperty()[0];

    // CurrentTechnique still points at [0] (set at construction) — applying [1]'s pass
    // must fail, and applying [0]'s pass must keep succeeding.
    EXPECT_THROW(secondPass.Apply(), System::InvalidOperationException);
    EXPECT_NO_THROW(firstPass.Apply());
    EXPECT_EQ(fx.applyCount, 1);

    // Switch CurrentTechnique to [1]: now [1]'s pass succeeds and [0]'s pass fails —
    // no stale state lingers from the previous technique being current.
    fx.setCurrentTechniqueProperty(&fx.getTechniquesProperty()[1]);
    EXPECT_THROW(firstPass.Apply(), System::InvalidOperationException);
    EXPECT_NO_THROW(secondPass.Apply());
    EXPECT_EQ(fx.applyCount, 2);
}

// -----------------------------------------------------------------------
// Task 356: verify that switching CurrentTechnique genuinely changes which
// EffectPassCollection is "the applied one" — accessed *through*
// CurrentTechnique itself (getCurrentTechniqueProperty()->getPassesProperty()),
// not via a directly-held technique index as Task 355's test above does.
// Confirms FNA's CurrentTechnique.set (Effect.cs) has no additional hidden
// state beyond the plain pointer swap CNA already performs: FNA's setter also
// calls FNA3D_SetEffectTechnique, a native call into the compiled-effect
// backend with no C#-observable side effect beyond what INTERNAL_applyEffect
// later reads — CNA has no compiled-technique GPU representation yet
// (Phase 74), so the plain pointer swap already provides the complete
// C#-visible contract.
// -----------------------------------------------------------------------

TEST_F(EffectApplyTest, CurrentTechniquePropertyPassCollectionTracksSelectedTechnique)
{
    fx.getTechniquesProperty().Add(EffectTechnique(&fx, "Second"));

    // Immediately after construction, CurrentTechnique's own Passes collection
    // must be technique [0]'s, not technique [1]'s.
    EXPECT_EQ(&fx.getCurrentTechniqueProperty()->getPassesProperty()[0],
              &fx.getTechniquesProperty()[0].getPassesProperty()[0]);
    EXPECT_NO_THROW(fx.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());

    fx.setCurrentTechniqueProperty(&fx.getTechniquesProperty()[1]);

    // After switching, CurrentTechnique's own Passes collection must now be
    // technique [1]'s — a real toggle, not a one-directional/stale snapshot.
    EXPECT_EQ(&fx.getCurrentTechniqueProperty()->getPassesProperty()[0],
              &fx.getTechniquesProperty()[1].getPassesProperty()[0]);
    EXPECT_NO_THROW(fx.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());

    fx.setCurrentTechniqueProperty(&fx.getTechniquesProperty()[0]);

    // Switching back restores [0]'s pass collection as current — bidirectional.
    EXPECT_EQ(&fx.getCurrentTechniqueProperty()->getPassesProperty()[0],
              &fx.getTechniquesProperty()[0].getPassesProperty()[0]);
    EXPECT_NO_THROW(fx.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
}

// -----------------------------------------------------------------------
// GetTypeName() — must be the fully-qualified .NET name per CLAUDE.md,
// matching every other GraphicsResource subclass's convention
// (RenderTarget2D, Texture3D, BasicEffect, ...).
// -----------------------------------------------------------------------

TEST(EffectTest, GetTypeNameIsFullyQualified)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    EXPECT_EQ(fx.GetTypeName(), "Microsoft.Xna.Framework.Graphics.Effect");
}

// -----------------------------------------------------------------------
// Disposal
// -----------------------------------------------------------------------

TEST(EffectTest, DisposeSetsIsDisposed)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    EXPECT_FALSE(fx.getIsDisposedProperty());
    fx.Dispose();
    EXPECT_TRUE(fx.getIsDisposedProperty());
}
