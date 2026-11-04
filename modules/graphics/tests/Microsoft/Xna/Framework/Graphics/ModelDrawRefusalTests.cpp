// SPDX-License-Identifier: MS-PL
// RRC-047: Model::Draw over a mesh part with no index buffer refuses the draw -- exactly
// InvalidOperationException with the shared DrawIndexedPrimitives message, with a valid vertex
// buffer so XNA Refresh reaches the index-buffer check -- and the device stays
// usable. The retired SDL_RENDERER suite held this (Task 728,
// sdlrenderer_model_draw_throws_test.cpp); nothing renderer-neutral did after its retirement.

#include <gtest/gtest.h>

#include <string>
#include <typeinfo>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/InvalidOperationException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using namespace Microsoft::Xna::Framework::Graphics;

TEST(ModelDrawRefusalTest, APartWithNoIndexBufferRefusesWithTheSharedMessage)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    BasicEffect effect(device);
    effect.setVertexColorEnabledProperty(true);

    ModelBone root(0, "root");
    ModelBone child(1, "child");
    root.AddChild(&child);
    // Refresh ModelMeshPart.Draw reads the vertex buffer device before binding indices.
    // Supply a real vertex buffer so this still isolates the missing-index-buffer refusal.
    VertexBuffer vertices(device, 4);
    ModelMeshPart part(&vertices, nullptr, 4, 2, 0, 0);
    ModelMesh mesh(&device, {&part});
    mesh.setParentBoneProperty(&root);
    part.setEffectProperty(&effect);
    Model model(&device, {&root, &child}, {&mesh});

    std::string message = "the draw was accepted";
    try
    {
        model.Draw(Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                   Matrix::getIdentityProperty());
    }
    catch (const System::InvalidOperationException& e)
    {
        message = typeid(e) == typeid(System::InvalidOperationException)
            ? std::string(e.what())
            : std::string("a subclass of InvalidOperationException: ") + e.what();
    }
    catch (const std::exception& e)
    {
        message = std::string("another exception: ") + e.what();
    }
    EXPECT_EQ(message, // Refresh VerifyCanDraw IL_01ee-01f8: CannotDrawNoData.
        "A valid vertex buffer (and a valid index buffer if you are using indexed primitives) must be set on the device before any draw operations may be performed.");

    // The refusal leaves nothing half-bound: the device still clears.
    EXPECT_NO_THROW(device.Clear(Color::CornflowerBlue));
}
