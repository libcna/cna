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
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::Effect;
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
