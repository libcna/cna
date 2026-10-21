// SPDX-License-Identifier: MS-PL
// ISSUE-055: structural clone state; real shader/pixel coverage belongs to clone_state.cpp.
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <typeinfo>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "System/ObjectDisposedException.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class ShaderCloneStateTest : public testing::Test
{
protected:
    GraphicsDevice device;
    void SetUp() override
    {
        RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
    }
    std::unique_ptr<ShaderEffect> MakeSource()
    {
        // Match the existing native structural test's deliberate placeholders.
        // No claim about compilation, a supported shader dialect, or GPU pixels.
        return std::make_unique<ShaderEffect>(device,
            "// vertex placeholder\n", "// fragment placeholder\n");
    }
};

TEST_F(ShaderCloneStateTest, AuthoredMatricesSurviveSourceDestruction)
{
    auto source = MakeSource();
    const auto world = Matrix::CreateScale(0.5f) * Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    const auto view = Matrix::CreateTranslation(-4.0f, 5.0f, -6.0f);
    const auto projection = Matrix::CreateOrthographic(4.0f, 8.0f, 0.1f, 10.0f);
    source->setWorldProperty(world);
    source->setViewProperty(view);
    source->setProjectionProperty(projection);
    std::unique_ptr<Effect> owner(source->Clone());
    auto* clone = dynamic_cast<ShaderEffect*>(owner.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->getVertexSourceProperty(), source->getVertexSourceProperty());
    EXPECT_EQ(clone->getFragmentSourceProperty(), source->getFragmentSourceProperty());
    source.reset();
    EXPECT_EQ(clone->getWorldProperty(), world);
    EXPECT_EQ(clone->getViewProperty(), view);
    EXPECT_EQ(clone->getProjectionProperty(), projection);
}

TEST_F(ShaderCloneStateTest, SourceAndCloneMatrixMutationsAreIndependent)
{
    auto source = MakeSource();
    const auto world = Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    source->setWorldProperty(world);
    std::unique_ptr<Effect> owner(source->Clone());
    auto* clone = dynamic_cast<ShaderEffect*>(owner.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->getWorldProperty(), world);
    clone->setWorldProperty(Matrix::CreateScale(2.0f));
    source->setViewProperty(Matrix::CreateTranslation(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(source->getWorldProperty(), world);
    EXPECT_EQ(clone->getViewProperty(), Matrix::getIdentityProperty());
}

TEST_F(ShaderCloneStateTest, DisposedSourceIsRefusedWithExactException)
{
    auto source = MakeSource();
    static_cast<GraphicsResource&>(*source).Dispose();
    ASSERT_TRUE(source->getIsDisposedProperty());
    bool exactRefusal = false;
    try { std::unique_ptr<Effect> unexpected(source->Clone()); }
    catch (const System::ObjectDisposedException& error)
    { exactRefusal = typeid(error) == typeid(System::ObjectDisposedException); }
    catch (...) { }
    EXPECT_TRUE(exactRefusal);
}

TEST_F(ShaderCloneStateTest, DefaultMatricesAndSeparateIdentityArePreserved)
{
    auto source = MakeSource();
    std::unique_ptr<Effect> owner(source->Clone());
    auto* clone = dynamic_cast<ShaderEffect*>(owner.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone, source.get());
    EXPECT_EQ(clone->getWorldProperty(), Matrix::getIdentityProperty());
    EXPECT_EQ(clone->getViewProperty(), Matrix::getIdentityProperty());
    EXPECT_EQ(clone->getProjectionProperty(), Matrix::getIdentityProperty());
}
} // namespace
