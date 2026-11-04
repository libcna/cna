// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/IO/BinaryWriter.hpp"
using namespace Microsoft::Xna::Framework::Content;
namespace {
struct Tq045Item final : System::IDisposable {
    int label, disposals = 0;
    std::vector<int>* trace;
    Tq045Item(int value, std::vector<int>* log) : label(value), trace(log) {}
    void Dispose() override { ++disposals; trace->push_back(label); }
};
using Item = std::shared_ptr<Tq045Item>;
struct Tq045Graph { Item direct, external, externalAgain, shared, sharedAgain; };
using Graph = std::shared_ptr<Tq045Graph>;
class Tq045ItemReader final : public ContentTypeReader<Item> {
public:
    std::vector<int>* trace;
    explicit Tq045ItemReader(std::vector<int>* log)
        : ContentTypeReader<Item>("Tq045.Item"), trace(log) {}
protected:
    Item Read(ContentReader& input, std::optional<Item>) override {
        return std::make_shared<Tq045Item>(input.ReadInt32(), trace);
    }
};
class Tq045GraphReader final : public ContentTypeReader<Graph> {
public:
    Tq045GraphReader() : ContentTypeReader<Graph>("Tq045.Graph") {}
protected:
    Graph Read(ContentReader& input, std::optional<Graph>) override {
        auto graph = std::make_shared<Tq045Graph>();
        graph->direct = input.ReadObject<Item>();
        graph->external = input.ReadExternalReference<Item>().value();
        graph->externalAgain = input.ReadExternalReference<Item>().value();
        input.ReadSharedResource<Item>([graph](Item item) { graph->shared = item; });
        input.ReadSharedResource<Item>([graph](Item item) { graph->sharedAgain = item; });
        return graph;
    }
};
std::vector<std::uint8_t> GraphBytes(bool child)
{
    // Hand-authored XNB v5: two reader names/version zero; child has root item 20.
    // Parent root: direct item 10, two references to child, two fixups to shared #1;
    // shared table: one materialized item 30. Container length = body length + 10.
    System::IO::MemoryStream body;
    System::IO::BinaryWriter writer(&body, true);
    writer.Write7BitEncodedInt(2);
    for (const char* name : {"Tq045.GraphReader", "Tq045.ItemReader"}) {
        writer.Write(std::string(name)); writer.Write(std::int32_t{0});
    }
    writer.Write7BitEncodedInt(child ? 0 : 1);
    writer.Write7BitEncodedInt(child ? 2 : 1);
    if (!child) writer.Write7BitEncodedInt(2);
    writer.Write(std::int32_t{child ? 20 : 10});
    if (!child) {
        writer.Write(std::string("child")); writer.Write(std::string("child"));
        writer.Write7BitEncodedInt(1); writer.Write7BitEncodedInt(1);
        writer.Write7BitEncodedInt(2); writer.Write(std::int32_t{30});
    }
    writer.Flush();
    const auto payload = body.ToArray();
    System::IO::MemoryStream file;
    System::IO::BinaryWriter header(&file, true);
    for (const auto byte : std::array<std::uint8_t,6>{'X','N','B','w',5,0}) header.Write(byte);
    header.Write(static_cast<std::int32_t>(10 + payload.size()));
    for (auto byte : payload) header.Write(byte);
    header.Flush();
    const auto result = file.ToArray();
    return {result.begin(), result.end()};
}
class Tq045GraphManager final : public ContentManager {
public:
    using ContentManager::ReadAsset;
    int opens = 0;
protected:
    std::unique_ptr<System::IO::Stream> OpenStream(const std::string& name) override {
        ++opens;
        const auto bytes = GraphBytes(name == "child");
        return std::make_unique<System::IO::MemoryStream>(bytes.data(), static_cast<std::int32_t>(bytes.size()), false);
    }
};
class ContentManagerGraphTq045Test : public ::testing::Test {
protected:
    std::vector<int> trace;
    void SetUp() override {
        ContentTypeReaderManager::ClearTypeCreators();
        ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
        ContentTypeReaderManager::AddTypeCreator("Tq045.ItemReader", [this] { return std::make_unique<Tq045ItemReader>(&trace); });
        ContentTypeReaderManager::AddTypeCreator("Tq045.GraphReader", [] { return std::make_unique<Tq045GraphReader>(); });
    }
    void TearDown() override {
        ContentTypeReaderManager::ClearTypeCreators();
        ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
    }
    static void Check(const Graph& graph) {
        ASSERT_NE(graph, nullptr);
        ASSERT_NE(graph->direct, nullptr); ASSERT_NE(graph->external, nullptr); ASSERT_NE(graph->shared, nullptr);
        EXPECT_EQ(graph->direct->label, 10); EXPECT_EQ(graph->external->label, 20); EXPECT_EQ(graph->shared->label, 30);
        EXPECT_EQ(graph->externalAgain, graph->external); EXPECT_EQ(graph->sharedAgain, graph->shared);
    }
};
TEST_F(ContentManagerGraphTq045Test, SharedAndExternalAliasesDisposeOnceInMaterializationOrder)
{
    // Refresh ContentReader.ReadAsset IL_0007-0010: root then shared table;
    // InvokeReader IL_00c1-00ed records new disposables; external Load caches the child;
    // ContentManager.Unload IL_0015-0038 traverses that recording order.
    Tq045GraphManager manager;
    auto graph = manager.Load<Graph>("parent");
    Check(graph);
    ASSERT_NE(graph->direct, nullptr); ASSERT_NE(graph->external, nullptr); ASSERT_NE(graph->shared, nullptr);
    EXPECT_EQ(manager.opens, 2);
    EXPECT_EQ(manager.Load<Graph>("PARENT"), graph);
    EXPECT_TRUE(trace.empty());
    manager.Unload();
    EXPECT_EQ(trace, (std::vector<int>{10,20,30}));
    EXPECT_EQ(graph->direct->disposals, 1); EXPECT_EQ(graph->external->disposals, 1); EXPECT_EQ(graph->shared->disposals, 1);
    manager.Unload();
    EXPECT_EQ(trace, (std::vector<int>{10,20,30}));
}
TEST_F(ContentManagerGraphTq045Test, CallerRecorderOwnsSharedObjectsButExternalLoadUsesManager)
{
    // Refresh InvokeReader IL_00d0-00ed chooses callback OR manager, never both.
    // ReadExternalReference IL_0028-0033 calls Load<T>, which supplies null callback.
    Tq045GraphManager manager;
    std::vector<std::shared_ptr<System::IDisposable>> recorded;
    const auto graph = manager.ReadAsset<Graph>("parent", [&](auto item) { recorded.push_back(item); });
    Check(graph);
    ASSERT_NE(graph->direct, nullptr); ASSERT_NE(graph->external, nullptr); ASSERT_NE(graph->shared, nullptr);
    ASSERT_EQ(recorded.size(), 2U);
    EXPECT_EQ(recorded[0].get(), graph->direct.get());
    EXPECT_EQ(recorded[1].get(), graph->shared.get());
    EXPECT_EQ(manager.opens, 2);
    manager.Unload();
    EXPECT_EQ(trace, (std::vector<int>{20}));
    EXPECT_EQ(graph->direct->disposals, 0); EXPECT_EQ(graph->shared->disposals, 0);
    for (const auto& item : recorded) item->Dispose();
    EXPECT_EQ(trace, (std::vector<int>{20,10,30}));
    manager.Dispose();
    EXPECT_EQ(graph->direct->disposals, 1); EXPECT_EQ(graph->external->disposals, 1); EXPECT_EQ(graph->shared->disposals, 1);
}
}
