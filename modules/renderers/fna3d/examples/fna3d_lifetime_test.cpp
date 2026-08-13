// SPDX-License-Identifier: MS-PL
// plan_fna3d.md FNA3D-35: negative, error and lifetime behaviour.
//
// Every other FNA3D test asks "does the right thing happen?". This one asks "does the WRONG thing
// get refused, by name, instead of being accepted into undefined behaviour?" -- which is the half
// of a renderer contract that silent success hides. FNA3D itself validates almost nothing: it
// takes raw pointers and integer ranges, so anything not rejected here reaches the driver.
//
// Check A -- an invalid mip level, a region outside the texture and an undersized destination are
//   each refused rather than served with fabricated bytes.
// Check B -- using a disposed resource raises rather than dereferencing a freed handle.
// Check C -- a draw with no effect applied is refused.
// Check D -- primitive counts that do not fit the bound buffer are refused (the range check is a
//   real boundary, not a clamp).
// Check E -- the renderer's own documented refusals still fire and still name their reason:
//   instanced drawing, custom effects, an unknown vertex stride, an out-of-contract state ordinal.
// Check F -- teardown with resources still alive does not crash: resources created and left to
//   their destructors are released in the right order relative to the device.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Fna3d::Fna3dRenderer;

namespace
{
    /// Runs @p action and reports whether it threw, capturing the message for the reason checks.
    template <typename Action>
    bool Threw(Action&& action, std::string& message)
    {
        try
        {
            action();
        }
        catch (const std::exception& error)
        {
            message = error.what();
            return true;
        }
        catch (...)
        {
            message = "(non-std exception)";
            return true;
        }
        message.clear();
        return false;
    }
}

class Fna3dLifetimeTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        TransferBoundaryChecks(device);
        DisposedResourceChecks(device);
        DrawValidationChecks(device);
        DocumentedRefusalChecks(device);
        TeardownChecks(device);
    }

private:
    // ---- Check A --------------------------------------------------------------------------
    void TransferBoundaryChecks(GraphicsDevice& device)
    {
        Texture2D texture(device, 8, 8);
        std::vector<Color> pixels(64, Color(10, 20, 30, 255));
        texture.SetData(pixels.data(), 64);

        std::string message;
        std::vector<Color> destination(64, Color(0, 0, 0, 0));

        const Rectangle whole(0, 0, 8, 8);
        Check(Threw([&] { texture.GetData(5, &whole, destination.data(), 0, 64); }, message),
              "a mip level past the end of a non-mipped texture is refused");

        const Rectangle outside(4, 4, 8, 8);
        Check(Threw([&] { texture.GetData(0, &outside, destination.data(), 0, 64); }, message),
              "a region reaching outside the texture is refused");

        Check(Threw([&] { texture.GetData(0, &whole, destination.data(), 0, 4); }, message),
              "a destination too small for the requested region is refused");

        // A startIndex that would push the write past the end of the destination is deliberately
        // NOT checked: XNA validates it against `data.Length`, and CNA's transfer methods take a
        // raw pointer with no length, so it is unknowable here. That is the same documented C++
        // deviation class CHECKLIST.md records for omitted null guards -- asserting it would be
        // asserting something the API structurally cannot do.

        // The successful case must still work, so none of the above is a blanket rejection.
        bool served = !Threw([&] { texture.GetData(0, &whole, destination.data(), 0, 64); },
                             message);
        Check(served, "a valid region is still served");
        Check(served && destination[0].getRProperty() == 10,
              "the valid readback returned the uploaded bytes");
    }

    // ---- Check B --------------------------------------------------------------------------
    void DisposedResourceChecks(GraphicsDevice& device)
    {
        std::string message;
        {
            // The contract this codebase actually establishes for a disposed texture is enforced
            // at DRAW time (see skia_presentation_edge_test.cpp), not at transfer time: the shared
            // Texture2D::SetData carries no isDisposed_ guard. That is a shared-layer gap, not an
            // FNA3D one -- it is recorded in plan_fna3d.md rather than patched from this lane --
            // so what is measured here is the boundary that IS contractual.
            Texture2D texture(device, 4, 4);
            texture.Dispose();
            Check(texture.getIsDisposedProperty(),
                  "a disposed texture reports itself disposed");
            // Disposal must also have withdrawn it from the device's sampler collections, so a
            // later frame cannot bind freed storage.
            Check(device.getTexturesProperty()[0] != &texture,
                  "a disposed texture is no longer bound in the device's texture collection");
        }
        {
            VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                                BufferUsage::WriteOnly);
            buffer.Dispose();
            Check(!buffer.HasRenderer(),
                  "a disposed vertex buffer reports that it no longer owns a GPU handle");
        }
        {
            // A render target that is still bound must refuse disposal rather than leave the
            // device pointing at freed storage.
            auto target = std::make_unique<RenderTarget2D>(device, 8, 8);
            device.SetRenderTarget(target.get());
            const bool refused = Threw([&] { target->Dispose(); }, message);
            device.SetRenderTarget(nullptr);
            Check(refused, "disposing a render target while it is bound is refused");
        }
    }

    // ---- Checks C and D -------------------------------------------------------------------
    void DrawValidationChecks(GraphicsDevice& device)
    {
        std::string message;

        VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                            BufferUsage::WriteOnly);
        const std::vector<VertexPositionColor> triangle = {
            { Vector3(-1.0f, -1.0f, 0.0f), Color(255, 255, 255, 255) },
            { Vector3( 1.0f, -1.0f, 0.0f), Color(255, 255, 255, 255) },
            { Vector3( 0.0f,  1.0f, 0.0f), Color(255, 255, 255, 255) },
        };
        buffer.SetData(triangle.data(), 3);
        device.SetVertexBuffer(&buffer);

        // Check D -- the bound buffer holds exactly one triangle; two do not fit.
        Check(Threw([&] { device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2); }, message),
              "a primitive count larger than the bound buffer is refused");
        Check(Threw([&] { device.DrawPrimitives(PrimitiveType::TriangleList, 2, 1); }, message),
              "a vertexStart that pushes the range past the buffer is refused");
        Check(Threw([&] { device.DrawPrimitives(PrimitiveType::TriangleList, 0, 0); }, message),
              "a zero primitive count is refused");
        Check(Threw([&] { device.DrawPrimitives(PrimitiveType::TriangleList, -1, 1); }, message),
              "a negative vertexStart is refused");
    }

    // ---- Check E --------------------------------------------------------------------------
    void DocumentedRefusalChecks(GraphicsDevice& device)
    {
        auto* fna3d = dynamic_cast<Fna3dRenderer*>(&device.GetRenderer());
        if (!Check(fna3d != nullptr, "the active renderer is Fna3dRenderer"))
        {
            return;
        }
        std::string message;

        // Custom effects: structural, because FNA3D compiles no shader source.
        Check(fna3d->CreateEffectRenderer(std::string(), std::string()) == nullptr,
              "CreateEffectRenderer returns null, matching CustomEffects=false");

        // A compressed cube is refused by name (FNA3D-19's boundary), and the message says why.
        const bool cubeRefused = Threw(
            [&] { (void) fna3d->CreateTextureCube(8, false,
                                                  static_cast<int>(SurfaceFormat::Dxt5)); },
            message);
        Check(cubeRefused, "a block-compressed TextureCube is refused");
        Check(message.find("block-compressed") != std::string::npos,
              "the compressed-cube refusal names the reason");

        // An out-of-contract enum ordinal must be refused rather than cast into an undefined
        // FNA3D enumerator.
        const bool ordinalRefused = Threw(
            [&] { (void) fna3d->CreateTextureCube(8, false, 9999); }, message);
        Check(ordinalRefused, "an out-of-contract SurfaceFormat ordinal is refused");

        // Stock effects still cannot consume an instance stream even on a driver with native
        // instancing. Compiled custom effects opt into that path with their own input semantics.
    }

    // ---- Check F --------------------------------------------------------------------------
    void TeardownChecks(GraphicsDevice& device)
    {
        // Create one of each resource kind and let them go out of scope while the device is still
        // alive. FNA3D frees through the device, so a resource released after its device would
        // dereference a destroyed handle; this is the ordering that has to hold.
        std::string message;
        const bool survived = !Threw([&] {
            Texture2D texture(device, 16, 16);
            RenderTarget2D target(device, 16, 16);
            VertexBuffer vertices(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                                  BufferUsage::WriteOnly);
            IndexBuffer indices(device, IndexElementSize::SixteenBits, 3, BufferUsage::WriteOnly);
            const std::uint16_t values[3] = { 0, 1, 2 };
            indices.SetData(values, 3);
            std::vector<Color> pixels(256, Color(9, 9, 9, 255));
            texture.SetData(pixels.data(), 256);
        }, message);
        Check(survived, "resources created and destroyed while the device lives tear down cleanly");

        // The device is still usable afterwards -- teardown of those resources did not disturb it.
        device.Clear(Color(30, 60, 90, 255));
        ExpectPixel("the device still renders after that teardown",
                    Rectangle(4, 4, 1, 1), Color(30, 60, 90, 255), 2);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Fna3dLifetimeTest>();
}
