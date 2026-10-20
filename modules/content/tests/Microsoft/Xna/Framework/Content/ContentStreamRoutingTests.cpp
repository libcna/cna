// SPDX-License-Identifier: MS-PL
// ISSUE-012: no Game, no routing opt-in, and no manual built-in registration.
#include <gtest/gtest.h>
#include <array>
#include <functional>
#include <barrier>
#include <thread>
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ResourceContentManager.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Resources/ResourceManager.hpp"

using namespace Microsoft::Xna::Framework::Content;
namespace
{
    struct StreamPayload { int value = 0; };
    using Payload = std::shared_ptr<StreamPayload>;
    class StreamPayloadReader final : public ContentTypeReader<Payload>
    {
    public:
        StreamPayloadReader() : ContentTypeReader<Payload>("Repair.StreamPayload") { }
    protected:
        Payload Read(ContentReader& input, std::optional<Payload>) override
        {
            return std::make_shared<StreamPayload>(StreamPayload{input.ReadInt32()});
        }
    };
    std::vector<std::uint8_t> Xnb(const std::string& readerName,
                                const std::function<void(System::IO::BinaryWriter&)>& payload)
    {
        System::IO::MemoryStream body;
        System::IO::BinaryWriter writer(&body, true);
        writer.Write7BitEncodedInt(1);
        writer.Write(readerName); writer.Write(std::int32_t{0});
        writer.Write7BitEncodedInt(0);
        writer.Write7BitEncodedInt(1);
        payload(writer);
        writer.Flush();
        const auto data = body.ToArray();
        System::IO::MemoryStream file;
        System::IO::BinaryWriter header(&file, true);
        for (const auto byte : std::array<std::uint8_t, 6>{'X', 'N', 'B', 'w', 5, 0}) header.Write(byte);
        header.Write(static_cast<std::int32_t>(data.size() + 10));
        for (const auto byte : data) header.Write(byte);
        header.Flush();
        const auto result = file.ToArray();
        return {result.begin(), result.end()};
    }
    class StreamManager final : public ContentManager
    {
    public:
        using ContentManager::ReadAsset;
        int opens = 0;
        bool fail = false;
        StreamManager() : ContentManager(nullptr, "repair-no-disk-stream-assets"),
            bytes_(Xnb("Repair.StreamPayloadReader", [](auto& writer) { writer.Write(std::int32_t{42}); })) { }
    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string&) override
        {
            ++opens;
            if (fail) throw System::InvalidOperationException("stream override refusal");
            return std::make_unique<System::IO::MemoryStream>(bytes_.data(), static_cast<std::int32_t>(bytes_.size()), false);
        }
    private:
        std::vector<std::uint8_t> bytes_;
    };
    class ContentStreamRoutingTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
            ContentTypeReaderManager::AddTypeCreator("Repair.StreamPayloadReader", [] { return std::make_unique<StreamPayloadReader>(); });
        }
        void TearDown() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
        }
    };

TEST_F(ContentStreamRoutingTest, DirectReadAssetAlreadyUsesTheVirtualStream)
{
    StreamManager manager;
    const auto value = manager.ReadAsset<Payload>("asset", {});
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value, 42);
    EXPECT_EQ(manager.opens, 1);
    manager.fail = true;
    EXPECT_THROW((void)manager.ReadAsset<Payload>("other", {}), System::InvalidOperationException);
    EXPECT_EQ(manager.opens, 2);
}

TEST_F(ContentStreamRoutingTest, OrdinaryLoadUsesTheVirtualStreamAndCachesItsInstance)
{
    StreamManager manager;
    Payload value;
    ASSERT_NO_THROW(value = manager.Load<Payload>("asset"));
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value, 42);
    EXPECT_EQ(manager.opens, 1);
    EXPECT_EQ(manager.Load<Payload>("asset"), value);
    EXPECT_EQ(manager.opens, 1);
}

TEST_F(ContentStreamRoutingTest, OrdinaryLoadPreservesTheOverrideException)
{
    StreamManager manager;
    manager.fail = true;
    EXPECT_THROW((void)manager.Load<Payload>("asset"), System::InvalidOperationException);
    EXPECT_EQ(manager.opens, 1);
}

TEST_F(ContentStreamRoutingTest, ColdResourceCurveLoadsAndExplicitWithdrawalRemainsEffective)
{
    using Microsoft::Xna::Framework::Curve;
    const auto bytes = Xnb("Microsoft.Xna.Framework.Content.CurveReader", [](auto& writer) {
        writer.Write(std::int32_t{0}); writer.Write(std::int32_t{0}); // Constant loops.
        writer.Write(std::int32_t{1});
        writer.Write(0.0f); writer.Write(42.0f); writer.Write(0.0f); writer.Write(0.0f);
        writer.Write(std::int32_t{0}); // Smooth continuity.
    });
    int lookups = 0;
    System::Resources::ResourceManager resources("Repair.EmbeddedCurve",
        [](std::string_view, std::string_view, std::string_view) -> std::optional<std::string> { return std::nullopt; },
        [&](std::string_view, std::string_view, std::string_view name) -> std::optional<std::any> {
            ++lookups;
            return name == "curve" ? std::optional<std::any>(bytes) : std::nullopt;
        });
    ResourceContentManager manager(nullptr, &resources);
    std::optional<Curve> curve;
    ASSERT_NO_THROW(curve.emplace(manager.Load<Curve>("curve")));
    ASSERT_TRUE(curve.has_value());
    EXPECT_EQ(curve->getKeysProperty().getCountProperty(), 1);
    EXPECT_EQ(curve->Evaluate(0.0f), 42.0f);
    EXPECT_EQ(manager.Load<Curve>("curve").Evaluate(0.0f), 42.0f);
    EXPECT_EQ(lookups, 1);
    ASSERT_TRUE(ContentTypeReaderManager::RemoveTypeCreatorEXT("Microsoft.Xna.Framework.Content.CurveReader"));
    // A later custom graph can need all built-ins, but must not resurrect an explicitly
    // withdrawn reader from the earlier, standalone family registration.
    detail::RegisterEveryBuiltinXnbReader();
    EXPECT_FALSE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.CurveReader"));
    manager.Unload();
    EXPECT_THROW((void)manager.Load<Curve>("curve"), ContentLoadException);
    EXPECT_THROW((void)manager.Load<Curve>("missing"), ContentLoadException);
}

constexpr const char* CurveReaderName = "Microsoft.Xna.Framework.Content.CurveReader";
constexpr const char* EffectReaderName = "Microsoft.Xna.Framework.Content.EffectReader";
void RegisterColdCurve()
{
    ContentManager manager(nullptr);
    detail::BuiltinContent<Microsoft::Xna::Framework::Curve>::Register(manager);
}

TEST_F(ContentStreamRoutingTest, ColdFamilyRegistersOnlyItsClosureWithoutDeclaringEveryReader)
{
    RegisterColdCurve();
    EXPECT_NE(detail::CreateRegisteredReader(CurveReaderName), nullptr);
    EXPECT_EQ(detail::CreateRegisteredReader(EffectReaderName), nullptr);
    EXPECT_FALSE(detail::BuiltinXnbReadersDeclared());
}

TEST_F(ContentStreamRoutingTest, ColdFamilyPreservesAnExplicitEarlierFactory)
{
    ContentTypeReaderManager::AddTypeCreator(CurveReaderName, [] { return std::make_unique<StreamPayloadReader>(); });
    RegisterColdCurve();
    EXPECT_NE(dynamic_cast<StreamPayloadReader*>(detail::CreateRegisteredReader(CurveReaderName).get()), nullptr);
}

TEST_F(ContentStreamRoutingTest, WithdrawnColdReaderSurvivesFamilyAndLazyAllRegistration)
{
    RegisterColdCurve();
    ASSERT_TRUE(ContentTypeReaderManager::RemoveTypeCreatorEXT(CurveReaderName));
    RegisterColdCurve();
    EXPECT_EQ(detail::CreateRegisteredReader(CurveReaderName), nullptr);
    detail::RegisterEveryBuiltinXnbReader();
    EXPECT_FALSE(ContentTypeReaderManager::IsRegistered(CurveReaderName));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(EffectReaderName));
    // Explicit registration remains an intentional restoration, not a lazy load.
    CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(CurveReaderName));
}

TEST_F(ContentStreamRoutingTest, NewGameDeclarationRestoresWithdrawnColdReader)
{
    RegisterColdCurve();
    ASSERT_TRUE(ContentTypeReaderManager::RemoveTypeCreatorEXT(CurveReaderName));
    detail::DeclareBuiltinXnbReaders();
    RegisterColdCurve();
    EXPECT_NE(detail::CreateRegisteredReader(CurveReaderName), nullptr);
}

class CallbackStreamManager final : public ContentManager
{
public:
    CallbackStreamManager() : ContentManager(nullptr, "repair-no-disk-stream-assets") { }
    using ContentManager::RouteLoadsThroughOpenStreamEXT;
    std::function<std::unique_ptr<System::IO::Stream>(const std::string&)> open;
    int opens = 0;
    std::unique_ptr<System::IO::Stream> BaseOpen(const std::string& name) { return ContentManager::OpenStream(name); }
protected:
    std::unique_ptr<System::IO::Stream> OpenStream(const std::string& name) override
    {
        ++opens;
        return open ? open(name) : BaseOpen(name);
    }
};
class CountingLooseReader final : public LooseFileContentTypeReader<Payload>
{
public:
    explicit CountingLooseReader(int& calls) : calls_(calls) { }
    Payload Read(const std::string&, ContentManager&) override
    {
        ++calls_;
        return std::make_shared<StreamPayload>(StreamPayload{9});
    }
private:
    int& calls_;
};
using StreamRoutingBoundaryTest = ContentStreamRoutingTest;

TEST_F(StreamRoutingBoundaryTest, BaseMissingCompiledAssetStillUsesLooseFallback)
{
    CallbackStreamManager manager;
    int reads = 0;
    manager.RegisterTypeReader<Payload>(std::make_unique<CountingLooseReader>(reads));
    const auto value = manager.Load<Payload>("asset");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value, 9);
    EXPECT_EQ(reads, 1);
    EXPECT_EQ(manager.Load<Payload>("asset"), value);
    EXPECT_EQ(reads, 1);
}

TEST_F(StreamRoutingBoundaryTest, CustomContentLoadExceptionDoesNotFallBack)
{
    CallbackStreamManager manager;
    int reads = 0;
    manager.RegisterTypeReader<Payload>(std::make_unique<CountingLooseReader>(reads));
    manager.open = [](const auto&) -> std::unique_ptr<System::IO::Stream> { throw ContentLoadException("custom refusal"); };
    try { (void)manager.Load<Payload>("asset"); FAIL() << "override refusal swallowed"; }
    catch (const ContentLoadException& error) { EXPECT_EQ(error.getMessageProperty(), "custom refusal"); EXPECT_EQ(typeid(error), typeid(ContentLoadException)); }
    EXPECT_EQ(manager.opens, 1);
    EXPECT_EQ(reads, 0);
}

TEST_F(StreamRoutingBoundaryTest, ReplacedBaseMissingExceptionDoesNotFallBack)
{
    CallbackStreamManager manager;
    int reads = 0;
    manager.RegisterTypeReader<Payload>(std::make_unique<CountingLooseReader>(reads));
    manager.open = [&](const auto& name) -> std::unique_ptr<System::IO::Stream> {
        try { return manager.BaseOpen(name); }
        catch (const ContentLoadException&) { throw ContentLoadException("translated refusal"); }
    };
    try { (void)manager.Load<Payload>("asset"); FAIL() << "replacement exception swallowed"; }
    catch (const ContentLoadException& error) { EXPECT_EQ(error.getMessageProperty(), "translated refusal"); }
    EXPECT_EQ(manager.opens, 1);
    EXPECT_EQ(reads, 0);
}

TEST_F(StreamRoutingBoundaryTest, NullOverrideStreamDoesNotFallBack)
{
    CallbackStreamManager manager;
    int reads = 0;
    manager.RegisterTypeReader<Payload>(std::make_unique<CountingLooseReader>(reads));
    manager.open = [](const auto&) { return std::unique_ptr<System::IO::Stream>{}; };
    EXPECT_THROW((void)manager.Load<Payload>("asset"), ContentLoadException);
    EXPECT_EQ(manager.opens, 1);
    EXPECT_EQ(reads, 0);
}

TEST_F(StreamRoutingBoundaryTest, InvalidCompiledStreamDoesNotFallBack)
{
    CallbackStreamManager manager;
    int reads = 0;
    manager.RegisterTypeReader<Payload>(std::make_unique<CountingLooseReader>(reads));
    manager.open = [](const auto&) { return std::make_unique<System::IO::MemoryStream>(); };
    EXPECT_THROW((void)manager.Load<Payload>("asset"), ContentLoadException);
    EXPECT_EQ(manager.opens, 1);
    EXPECT_EQ(reads, 0);
}

TEST_F(StreamRoutingBoundaryTest, StrictStreamExtensionStillDisablesBaseFallback)
{
    CallbackStreamManager manager;
    int reads = 0;
    manager.RegisterTypeReader<Payload>(std::make_unique<CountingLooseReader>(reads));
    manager.RouteLoadsThroughOpenStreamEXT(true);
    EXPECT_THROW((void)manager.Load<Payload>("asset"), ContentLoadException);
    EXPECT_EQ(manager.opens, 1);
    EXPECT_EQ(reads, 0);
}

TEST_F(StreamRoutingBoundaryTest, SoundEffectSpecializationUsesOverrideAndPreservesItsException)
{
    StreamManager manager;
    manager.fail = true;
    EXPECT_THROW((void)manager.Load<Microsoft::Xna::Framework::Audio::SoundEffect>("asset"), System::InvalidOperationException);
    EXPECT_EQ(manager.opens, 1);
}

TEST_F(StreamRoutingBoundaryTest, UntypedExternalReferenceUsesOverrideAndCachesItsInstance)
{
    StreamManager manager;
    System::IO::MemoryStream stream;
    System::IO::BinaryWriter writer(&stream, true);
    writer.Write(std::string("asset")); writer.Write(std::string("asset")); writer.Flush();
    stream.setPositionProperty(0);
    ContentReader reader(&manager, &stream, "parent", 5, 'w');
    std::any first;
    ASSERT_NO_THROW(first = reader.ReadExternalReference());
    const auto value = std::any_cast<Payload>(first);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value, 42);
    EXPECT_EQ(std::any_cast<Payload>(reader.ReadExternalReference()), value);
    EXPECT_EQ(manager.opens, 1);
}

TEST_F(StreamRoutingBoundaryTest, MissingDifferentAssetDoesNotTriggerFallbackForRequestedAsset)
{
    CallbackStreamManager manager;
    int reads = 0;
    manager.RegisterTypeReader<Payload>(std::make_unique<CountingLooseReader>(reads));
    manager.open = [&](const auto&) { return manager.BaseOpen("different"); };
    EXPECT_THROW((void)manager.Load<Payload>("asset"), ContentLoadException);
    EXPECT_EQ(manager.opens, 1);
    EXPECT_EQ(reads, 0);
}

TEST_F(StreamRoutingBoundaryTest, OtherManagersMissingAssetDoesNotTriggerFallback)
{
    CallbackStreamManager manager, other;
    int reads = 0;
    manager.RegisterTypeReader<Payload>(std::make_unique<CountingLooseReader>(reads));
    manager.open = [&](const auto& name) { return other.BaseOpen(name); };
    EXPECT_THROW((void)manager.Load<Payload>("asset"), ContentLoadException);
    EXPECT_EQ(manager.opens, 1);
    EXPECT_EQ(reads, 0);
}

TEST_F(StreamRoutingBoundaryTest, NestedLoadRestoresTheOuterFallbackContext)
{
    CallbackStreamManager manager;
    int reads = 0;
    Payload inner;
    manager.RegisterTypeReader<Payload>(std::make_unique<CountingLooseReader>(reads));
    manager.open = [&](const auto& name) {
        if (name == "outer") inner = manager.Load<Payload>("inner");
        return manager.BaseOpen(name);
    };
    const auto outer = manager.Load<Payload>("outer");
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(outer, nullptr);
    EXPECT_NE(inner, outer);
    EXPECT_EQ(inner->value, 9);
    EXPECT_EQ(outer->value, 9);
    EXPECT_EQ(manager.Load<Payload>("inner"), inner);
    EXPECT_EQ(manager.Load<Payload>("outer"), outer);
    EXPECT_EQ(manager.opens, 2);
    EXPECT_EQ(reads, 2);
}

TEST_F(StreamRoutingBoundaryTest, SimultaneousManagersKeepMissingExceptionIdentityThreadLocal)
{
    // This is a content-routing concurrency test, not a native thread-lifecycle test.
    // Use the existing platform seam so the owning edge supplies portable filesystem
    // behavior without native-backend cleanup calls leaking into content tests.
    auto platform = CNA::Platform::PlatformFactory::Create("Headless");
    const CNA::Platform::Testing::ScopedCurrentPlatform current(*platform);
    CallbackStreamManager first, second;
    int firstReads = 0, secondReads = 0;
    first.RegisterTypeReader<Payload>(std::make_unique<CountingLooseReader>(firstReads));
    second.RegisterTypeReader<Payload>(std::make_unique<CountingLooseReader>(secondReads));
    std::barrier rendezvous(2);
    first.open = [&](const auto& name) -> std::unique_ptr<System::IO::Stream> {
        std::exception_ptr missing;
        try { return first.BaseOpen(name); }
        catch (const ContentLoadException&) { missing = std::current_exception(); }
        rendezvous.arrive_and_wait();
        std::rethrow_exception(missing);
    };
    second.open = [&](const auto& name) -> std::unique_ptr<System::IO::Stream> {
        try { return second.BaseOpen(name); }
        catch (const ContentLoadException&) { }
        rendezvous.arrive_and_wait();
        throw ContentLoadException("second manager refusal");
    };
    Payload firstValue;
    std::exception_ptr firstError, secondError;
    std::jthread one([&] { try { firstValue = first.Load<Payload>("asset"); } catch (...) { firstError = std::current_exception(); } });
    std::jthread two([&] { try { (void)second.Load<Payload>("asset"); } catch (...) { secondError = std::current_exception(); } });
    one.join(); two.join();
    EXPECT_EQ(firstError, nullptr);
    ASSERT_NE(firstValue, nullptr);
    EXPECT_EQ(firstValue->value, 9);
    ASSERT_NE(secondError, nullptr);
    try { std::rethrow_exception(secondError); }
    catch (const ContentLoadException& error) { EXPECT_EQ(error.getMessageProperty(), "second manager refusal"); }
    EXPECT_EQ(firstReads, 1);
    EXPECT_EQ(secondReads, 0);
    EXPECT_EQ(first.opens, 1);
    EXPECT_EQ(second.opens, 1);
}
}
