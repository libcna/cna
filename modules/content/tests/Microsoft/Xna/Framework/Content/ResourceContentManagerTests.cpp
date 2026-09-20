// SPDX-License-Identifier: MS-PL
//
// XNA-MISSING-013: ResourceContentManager(IServiceProvider, ResourceManager).
//
// XNA's constructor rejects a null resource manager and stores it; its OpenStream override asks that
// manager for the asset, requires a byte[], and returns a memory stream over those bytes -- with a
// separate ContentLoadException for "not found" and for "not binary"
// (xna4-decomp/.../Microsoft.Xna.Framework.Content/ResourceContentManager.cs). CNA's OpenStream
// previously threw unconditionally, so the whole type was unusable.
//
// The resource-backed route is asserted end to end: a real .xnb built in memory, held as a binary
// resource, loaded through ReadAsset<T> -- which is what proves OpenStream is genuinely the seam
// ContentManager reads through rather than a method that merely exists.

#include <gtest/gtest.h>

#include <any>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/ResourceContentManager.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/Globalization/CultureInfo.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/Resources/ResourceManager.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Content::ResourceContentManager;

namespace
{
    struct ResourceValue
    {
        std::int32_t value = 0;
    };

    class ResourceValueReader final : public ContentTypeReader<ResourceValue>
    {
    public:
        ResourceValueReader() : ContentTypeReader<ResourceValue>("CNA.Test.ResourceValue") {}

    protected:
        ResourceValue Read(ContentReader& input, std::optional<ResourceValue> existing) override
        {
            ResourceValue result = existing.value_or(ResourceValue{});
            result.value = input.ReadInt32();
            return result;
        }
    };

    std::vector<std::uint8_t> BuildResourceXnb(std::int32_t value)
    {
        System::IO::MemoryStream body;
        {
            System::IO::BinaryWriter writer(&body, true);
            writer.Write7BitEncodedInt(1);
            writer.Write(std::string("CNA.Test.ResourceValue"));
            writer.Write(static_cast<std::int32_t>(0));
            writer.Write7BitEncodedInt(0);
            writer.Write7BitEncodedInt(1);
            writer.Write(value);
        }
        const auto bodyArray = body.ToArray();

        System::IO::MemoryStream file;
        {
            System::IO::BinaryWriter writer(&file, true);
            writer.Write(static_cast<std::uint8_t>('X'));
            writer.Write(static_cast<std::uint8_t>('N'));
            writer.Write(static_cast<std::uint8_t>('B'));
            writer.Write(static_cast<std::uint8_t>('w'));
            writer.Write(static_cast<std::uint8_t>(5));
            writer.Write(static_cast<std::uint8_t>(0));
            writer.Write(static_cast<std::int32_t>(10 + bodyArray.size()));
            for (const auto byteValue : bodyArray)
            {
                writer.Write(static_cast<std::uint8_t>(byteValue));
            }
        }
        const auto fileArray = file.ToArray();
        return std::vector<std::uint8_t>(fileArray.begin(), fileArray.end());
    }

    /// A resource family holding one binary asset, one string resource and nothing else.
    class ResourceFixture
    {
    public:
        explicit ResourceFixture(std::vector<std::uint8_t> assetBytes)
            : assetBytes_(std::move(assetBytes)),
              manager_("CNA.Test.Resources",
                       [this](std::string_view, std::string_view culture,
                              std::string_view name) -> std::optional<std::string> {
                           if (culture.empty() && name == "readme")
                           {
                               return std::string("a string resource, not an asset");
                           }
                           return std::nullopt;
                       },
                       [this](std::string_view, std::string_view culture,
                              std::string_view name) -> std::optional<std::any> {
                           if (culture.empty() && name == "fixture")
                           {
                               return std::any(assetBytes_);
                           }
                           return std::nullopt;
                       })
        {
        }

        [[nodiscard]] System::Resources::ResourceManager* manager() { return &manager_; }

    private:
        std::vector<std::uint8_t> assetBytes_;
        System::Resources::ResourceManager manager_;
    };

    /// Reaches the protected ReadAsset<T> and OpenStream, as a deriving manager would.
    class ProbeResourceContentManager final : public ResourceContentManager
    {
    public:
        using ResourceContentManager::OpenStream;
        using ResourceContentManager::ReadAsset;

        ProbeResourceContentManager(System::IServiceProvider* services,
                                    System::Resources::ResourceManager* resources)
            : ResourceContentManager(services, resources)
        {
        }

        explicit ProbeResourceContentManager(System::IServiceProvider* services)
            : ResourceContentManager(services)
        {
        }
    };

    class ResourceContentManagerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::AddTypeCreator(
                "CNA.Test.ResourceValue", [] { return std::make_unique<ResourceValueReader>(); });
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };
}

TEST_F(ResourceContentManagerTest, TheConstructorRejectsANullResourceManager)
{
    EXPECT_THROW((void)ResourceContentManager(nullptr, nullptr), System::ArgumentNullException);
}

TEST_F(ResourceContentManagerTest, OpenStreamServesABinaryResource)
{
    ResourceFixture fixture(BuildResourceXnb(1234));
    ProbeResourceContentManager manager(nullptr, fixture.manager());

    const std::unique_ptr<System::IO::Stream> stream = manager.OpenStream("fixture");
    ASSERT_NE(stream, nullptr);

    // The stream holds the resource's bytes from the start, container header included.
    std::array<std::uint8_t, 4> signature{};
    ASSERT_EQ(stream->Read(signature.data(), 0, 4), 4);
    EXPECT_EQ(signature[0], 'X');
    EXPECT_EQ(signature[1], 'N');
    EXPECT_EQ(signature[2], 'B');
    EXPECT_EQ(signature[3], 'w');
}

TEST_F(ResourceContentManagerTest, AnAssetLoadsEndToEndThroughTheResourceFamily)
{
    // The point of the type: a compiled asset held as a resource, read with no content directory
    // anywhere. This only works if OpenStream is genuinely the seam ReadAsset reads through.
    ResourceFixture fixture(BuildResourceXnb(4321));
    ProbeResourceContentManager manager(nullptr, fixture.manager());

    const ResourceValue loaded = manager.ReadAsset<ResourceValue>("fixture", {});
    EXPECT_EQ(loaded.value, 4321);
}

TEST_F(ResourceContentManagerTest, AMissingResourceIsReportedAsSuch)
{
    ResourceFixture fixture(BuildResourceXnb(1));
    ProbeResourceContentManager manager(nullptr, fixture.manager());

    EXPECT_THROW((void)manager.OpenStream("absent"), ContentLoadException);
    try
    {
        (void)manager.OpenStream("absent");
        FAIL() << "a missing resource must be refused";
    }
    catch (const ContentLoadException& error)
    {
        EXPECT_NE(std::string(error.what()).find("could not be found"), std::string::npos);
    }
}

TEST_F(ResourceContentManagerTest, ANonBinaryResourceIsReportedSeparatelyFromAMissingOne)
{
    // XNA distinguishes the two, because they mean different mistakes: a resource that is not there
    // and a resource authored as the wrong kind.
    ResourceFixture fixture(BuildResourceXnb(1));
    ProbeResourceContentManager manager(nullptr, fixture.manager());

    try
    {
        (void)manager.OpenStream("readme");
        FAIL() << "a string resource is not an asset";
    }
    catch (const ContentLoadException& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("not a binary resource"), std::string::npos);
        EXPECT_EQ(message.find("could not be found"), std::string::npos)
            << "the two failures must not report the same thing";
    }
}

TEST_F(ResourceContentManagerTest, AManagerWithNoResourceFamilyRefusesToOpenAnything)
{
    // CNA's own service-provider-only constructor. There is nothing to read from, and saying so is
    // the honest answer -- the previous unconditional throw said it for the resource-backed case too.
    ProbeResourceContentManager manager(nullptr);
    EXPECT_THROW((void)manager.OpenStream("fixture"), ContentLoadException);
    try
    {
        (void)manager.OpenStream("fixture");
        FAIL() << "a manager with no resource family must refuse";
    }
    catch (const ContentLoadException& error)
    {
        EXPECT_NE(std::string(error.what()).find("without a ResourceManager"), std::string::npos);
    }
}

TEST_F(ResourceContentManagerTest, TheResourceFamilysCultureFallbackIsWhatResolvesTheAsset)
{
    // The asset is an invariant resource, so a manager running under any culture still finds it --
    // which is the resource manager's fallback doing the work, not the content manager's.
    ResourceFixture fixture(BuildResourceXnb(7));
    ProbeResourceContentManager manager(nullptr, fixture.manager());

    const auto asObject = fixture.manager()->GetObject(
        "fixture", System::Globalization::CultureInfo("fr-FR"));
    ASSERT_TRUE(asObject.has_value());

    EXPECT_EQ(manager.ReadAsset<ResourceValue>("fixture", {}).value, 7);
}

TEST_F(ResourceContentManagerTest, ItIsStillAContentManager)
{
    ResourceFixture fixture(BuildResourceXnb(1));
    ProbeResourceContentManager manager(nullptr, fixture.manager());

    // Disposal, the cache and every other ContentManager contract still apply.
    EXPECT_NE(dynamic_cast<Microsoft::Xna::Framework::Content::ContentManager*>(&manager), nullptr);
    manager.Dispose();
    EXPECT_ANY_THROW((void)manager.ReadAsset<ResourceValue>("fixture", {}));
}
