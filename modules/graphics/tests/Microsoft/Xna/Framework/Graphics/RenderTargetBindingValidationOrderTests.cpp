// SPDX-License-Identifier: MS-PL
// TQ-021: GraphicsDevice.SetRenderTargets validates each binding completely, in slot order, before
// any state changes.
//
// Oracle: Microsoft XNA 4.0 GraphicsDevice::SetRenderTargets(RenderTargetBinding*, int).
//  IL_0015-0083  an unchanged binding set returns immediately;
//  IL_0088-00ac  more bindings than the profile's MaxRenderTargets -> NotSupportedException;
//  IL_00b1-0145  one loop over the slots; for slot i, in this order:
//                null target       -> ArgumentException(message only)   IL_0147-0151,
//                disposed target   -> ObjectDisposedException           IL_00d1-00e3,
//                foreign device    -> InvalidOperationException         IL_0152-015c,
//                (i > 0) the same texture in an earlier slot -> ArgumentException IL_015d-0167,
//                (i > 0) RenderTargetHelper::IsSameSize(slot i, slot 0) false (width, height,
//                multisample count, pixel size; RenderTargetHelper::IsSameSize IL_0059-008f)
//                                  -> ArgumentException                 IL_0168-0172;
//  IL_0173-      only then is any device state touched.
// So a defect in an earlier slot wins over a different defect in a later slot, and a refused call
// leaves the previous binding, viewport and scissor rectangle untouched.
#include <gtest/gtest.h>

#include <memory>
#include <typeinfo>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;

namespace
{
    template<typename TException, typename TCallable>
    void ExpectExact(TCallable&& callable, const char* what)
    {
        try
        {
            callable();
            ADD_FAILURE() << what << ": no exception";
        }
        catch (const TException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(TException)) << what;
        }
        catch (...)
        {
            ADD_FAILURE() << what << ": unexpected exception type";
        }
    }

    class RenderTargetBindingOrderTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;
        std::unique_ptr<RenderTarget2D> bound;

        // A HiDef device with one 4x4 target already bound and a non-default viewport/scissor, so
        // a refused call has observable state to preserve.
        void SetUp() override
        {
            device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
            bound = std::make_unique<RenderTarget2D>(device, 4, 4);
            try
            {
                device.SetRenderTarget(bound.get());
            }
            catch (const System::NotSupportedException&)
            {
                GTEST_SKIP() << "renderer has no RenderTarget2D";
            }
            device.setViewportProperty(
                Microsoft::Xna::Framework::Graphics::Viewport(1, 1, 2, 3));
            device.setScissorRectangleProperty(Rectangle(0, 1, 3, 2));
        }

        void ExpectPreviousStateKept()
        {
            const std::vector<RenderTargetBinding> current = device.GetRenderTargets();
            ASSERT_EQ(current.size(), 1u);
            EXPECT_EQ(current[0].getRenderTargetProperty(), bound.get());
            EXPECT_EQ(device.getViewportProperty().getXProperty(), 1);
            EXPECT_EQ(device.getViewportProperty().getYProperty(), 1);
            EXPECT_EQ(device.getViewportProperty().getWidthProperty(), 2);
            EXPECT_EQ(device.getViewportProperty().getHeightProperty(), 3);
            EXPECT_EQ(device.getScissorRectangleProperty(), Rectangle(0, 1, 3, 2));
        }
    };
}

TEST_F(RenderTargetBindingOrderTest, DisposedTargetIsRefusedWithoutTouchingTheCurrentBinding)
{
    RenderTarget2D disposed(device, 4, 4);
    disposed.Dispose();
    ExpectExact<System::ObjectDisposedException>(
        [&] { device.SetRenderTargets({RenderTargetBinding(&disposed)}); }, "disposed");
    ExpectPreviousStateKept();
}

TEST_F(RenderTargetBindingOrderTest, TheSameTextureInTwoSlotsIsRefused)
{
    RenderTarget2D target(device, 4, 4);
    ExpectExact<System::ArgumentException>(
        [&] {
            device.SetRenderTargets({RenderTargetBinding(&target), RenderTargetBinding(&target)});
        },
        "duplicate");
    ExpectPreviousStateKept();
}

// Slot 1 has the wrong size and slot 2 is disposed. XNA reaches slot 1's size check before slot
// 2's disposal check, so the size refusal (ArgumentException) is what the caller sees.
TEST_F(RenderTargetBindingOrderTest, AnEarlierSizeMismatchWinsOverALaterDisposedTarget)
{
    RenderTarget2D first(device, 4, 4);
    RenderTarget2D larger(device, 8, 8);
    RenderTarget2D disposed(device, 4, 4);
    disposed.Dispose();
    ExpectExact<System::ArgumentException>(
        [&] {
            device.SetRenderTargets({RenderTargetBinding(&first), RenderTargetBinding(&larger),
                                     RenderTargetBinding(&disposed)});
        },
        "size before later disposal");
    ExpectPreviousStateKept();
}

// Slot 1 repeats slot 0 and slot 2 belongs to another device: the duplicate wins.
TEST_F(RenderTargetBindingOrderTest, AnEarlierDuplicateWinsOverALaterForeignTarget)
{
    GraphicsDevice other;
    other.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    RenderTarget2D first(device, 4, 4);
    RenderTarget2D foreign(other, 4, 4);
    ExpectExact<System::ArgumentException>(
        [&] {
            device.SetRenderTargets({RenderTargetBinding(&first), RenderTargetBinding(&first),
                                     RenderTargetBinding(&foreign)});
        },
        "duplicate before later foreign target");
    ExpectPreviousStateKept();
}

// A default binding carries no target. XNA throws `new ArgumentException(NullNotAllowed)`
// (IL_0147-0151): exactly ArgumentException -- not ArgumentNullException -- with no ParamName.
TEST_F(RenderTargetBindingOrderTest, NullBindingIsAPlainArgumentExceptionWithoutParameterName)
{
    RenderTarget2D first(device, 4, 4);
    try
    {
        device.SetRenderTargets({RenderTargetBinding(&first), RenderTargetBinding()});
        ADD_FAILURE() << "a null binding was accepted";
    }
    catch (const System::ArgumentException& exception)
    {
        EXPECT_EQ(typeid(exception), typeid(System::ArgumentException));
        EXPECT_TRUE(exception.getParamNameProperty().empty())
            << "ParamName was '" << exception.getParamNameProperty() << "'";
    }
    ExpectPreviousStateKept();
}
