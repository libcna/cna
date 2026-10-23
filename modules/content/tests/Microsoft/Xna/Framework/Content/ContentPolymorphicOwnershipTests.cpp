// SPDX-License-Identifier: MS-PL
// ISSUE-011: requesting a resource through Object must not lose value-wrapper ownership.
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::LooseFileContentTypeReader;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    template<class T>
    class ObjectTextureReader final : public LooseFileContentTypeReader<std::shared_ptr<T>>
    {
    public:
        std::vector<std::string> GetExtensions() const override { return {".png"}; }
        std::shared_ptr<T> Read(const std::string& path, ContentManager& manager) override
        {
            return std::make_shared<Texture2D>(path, manager.getGraphicsDeviceInternal());
        }
    };
    template<class T>
    void CheckOwnership()
    {
        GraphicsDevice device;
        ContentManager manager(nullptr, "tests/assets/xna40/texture");
        manager.setGraphicsDevice(device);
        manager.RegisterTypeReader<std::shared_ptr<T>>(std::make_unique<ObjectTextureReader<T>>());
        auto value = manager.Load<std::shared_ptr<T>>("probe_3x2.png");
        auto texture = std::dynamic_pointer_cast<const Texture2D>(value);
        ASSERT_NE(texture, nullptr);
        Texture2D copied(*texture);
        const auto renderer = texture->GetRendererWeak();
        std::vector<Microsoft::Xna::Framework::Color> pixels(texture->getWidthProperty() * texture->getHeightProperty());
        ASSERT_NO_THROW(copied.GetData(pixels.data(), static_cast<int>(pixels.size())));
        manager.Unload();
        EXPECT_TRUE(texture->getIsDisposedProperty());
        EXPECT_TRUE(copied.getIsDisposedProperty());
        EXPECT_TRUE(renderer.expired());
        EXPECT_THROW(copied.GetData(pixels.data(), static_cast<int>(pixels.size())), System::ObjectDisposedException);
    }
}

TEST(ContentPolymorphicOwnershipTest, ObjectTypedTextureLoadRetainsOwnershipOfLaterValueCopies)
{
    CheckOwnership<System::Object>();
}

TEST(ContentPolymorphicOwnershipTest, ConstObjectTypedTextureLoadRetainsOwnershipOfLaterValueCopies)
{
    CheckOwnership<const System::Object>();
}

TEST(ContentPolymorphicOwnershipTest, ConstTextureLoadRetainsOwnershipOfLaterValueCopies)
{
    CheckOwnership<const Texture2D>();
}
