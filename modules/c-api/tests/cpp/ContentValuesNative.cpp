// SPDX-License-Identifier: MS-PL
#include "CNA/C/content_values.h"
#include "CNA/Content/ForeignContentObjectEXT.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ReflectiveTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/MemoryStream.hpp"
#include <cstring>
#include <cstdio>
#include <thread>

using namespace Microsoft::Xna::Framework::Content;
namespace {
struct Node { int32_t number = 0; std::shared_ptr<Node> next; };
struct Abstract { virtual ~Abstract() = default; virtual void Marker() = 0; int32_t base = 0; };
struct Derived : Abstract { void Marker() override {} int32_t derived = 0; };
int disposals = 0;
struct Disposable : System::IDisposable {
    void Dispose() override { ++disposals; }
};
class DisposableReader final : public ContentTypeReader<std::shared_ptr<Disposable>> {
public:
    DisposableReader() : ContentTypeReader("Lane.Disposable") {}
    std::shared_ptr<Disposable> Read(ContentReader&, std::optional<std::shared_ptr<Disposable>>) override {
        return std::make_shared<Disposable>();
    }
};
class RecordRootReader final : public ContentTypeReader<CNA::Content::ForeignContentObjectEXT> {
public:
    RecordRootReader() : ContentTypeReader("Lane.RecordRoot") {}
    CNA::Content::ForeignContentObjectEXT Read(ContentReader& input,
        std::optional<CNA::Content::ForeignContentObjectEXT>) override {
        static int foreign;
        static_cast<void>(input.ReadObject<std::shared_ptr<Disposable>>());
        return {&foreign};
    }
};
void Var(std::vector<uint8_t>& bytes, unsigned n) {
    do { bytes.push_back(static_cast<uint8_t>((n & 127) | (n > 127 ? 128 : 0))); n >>= 7; } while (n);
}
void Text(std::vector<uint8_t>& bytes, const std::string& text) {
    Var(bytes, static_cast<unsigned>(text.size())); bytes.insert(bytes.end(), text.begin(), text.end());
}
void I32(std::vector<uint8_t>& bytes, int32_t n) {
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(static_cast<uint32_t>(n) >> (8*i)));
}
}
extern "C" void cna_test_content_values_begin(void) {
    CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
    ContentTypeReaderManager::AddTypeCreator("Lane.DisposableReader", [] { return std::make_unique<DisposableReader>(); });
    ContentTypeReaderManager::AddTypeCreator("Lane.RecordRootReader", [] { return std::make_unique<RecordRootReader>(); });
    disposals = 0;
}
extern "C" int cna_test_content_disposals(void) { return disposals; }
extern "C" int cna_test_content_worker(int (*test)(void)) {
    int result = 0; std::thread worker([&] { result = test(); }); worker.join(); return result;
}
extern "C" int cna_test_content_native_shared(void) try {
    ReflectiveTypeReaderBuilder<Node> builder("Oracle.Node");
    builder.Field(&Node::number).SharedResourceField(&Node::next).RegisterShared();
    std::vector<uint8_t> bytes{1};
    Text(bytes, "Microsoft.Xna.Framework.Content.ReflectiveReader`1[[Oracle.Node]]"); I32(bytes, 0);
    Var(bytes, 1); Var(bytes, 1); I32(bytes, 11); Var(bytes, 1);
    Var(bytes, 1); I32(bytes, 22); Var(bytes, 1);
    System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()), false);
    ContentReader input(nullptr, &stream, "graph", 5, 'w');
    auto root = input.ReadAsset<std::shared_ptr<Node>>();
    const int ok = root && root->number == 11 && root->next && root->next->number == 22 && root->next->next == root->next;
    if (root && root->next) root->next->next.reset();
    return ok;
} catch (const std::exception& error) {
    std::fprintf(stderr, "Native content oracle failed: %s\n", error.what());
    return 0;
} catch (...) { return 0; }

extern "C" int cna_test_content_native_inheritance(void) try {
    ReflectiveTypeReaderBuilder<Abstract> base("Oracle.Base"); base.Field(&Abstract::base).RegisterAbstract();
    ReflectiveTypeReaderBuilder<Derived>("Oracle.Derived").Base(base).Field(&Derived::derived).RegisterShared<Abstract>();
    std::vector<uint8_t> bytes{2};
    Text(bytes, "Microsoft.Xna.Framework.Content.ReflectiveReader`1[[Oracle.Base]]"); I32(bytes, 0);
    Text(bytes, "Microsoft.Xna.Framework.Content.ReflectiveReader`1[[Oracle.Derived]]"); I32(bytes, 0);
    Var(bytes, 0); Var(bytes, 2); I32(bytes, 41); I32(bytes, 42);
    System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()), false);
    ContentReader input(nullptr, &stream, "derived", 5, 'w');
    auto root = input.ReadAsset<std::shared_ptr<Abstract>>();
    auto derived = std::dynamic_pointer_cast<Derived>(root);
    if (!derived || derived->base != 41 || derived->derived != 42) return 0;
    auto abstract = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.ReflectiveReader`1[[Oracle.Base]]");
    try { static_cast<void>(abstract->ReadUntyped(input, {})); return 0; } catch (const ContentLoadException&) { return 1; }
} catch (const std::exception& error) {
    std::fprintf(stderr, "Native content oracle failed: %s\n", error.what());
    return 0;
} catch (...) { return 0; }

extern "C" int cna_test_content_native_value(const uint8_t* bytes, uint64_t count, unsigned kind, const void* expected, uint64_t size) {
    try {
        System::IO::MemoryStream stream(bytes, static_cast<int32_t>(count), false);
        ContentReader input(nullptr, &stream, "value", 5, 'w');
        const auto result = input.ReadAsset();
        if (kind == CNA_CONTENT_VALUE_INT32) return size == sizeof(int32_t) && std::any_cast<int32_t>(result) == *static_cast<const int32_t*>(expected);
        if (kind == CNA_CONTENT_VALUE_STRING) return std::any_cast<std::string>(result) == std::string(static_cast<const char*>(expected), size);
        return !result.has_value() && kind == CNA_CONTENT_VALUE_EMPTY;
    } catch (...) { return 0; }
}
extern "C" int cna_test_content_native_scalar_existing(void) {
    std::vector<uint8_t> bytes; I32(bytes, 456);
    System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()), false);
    ContentReader input(nullptr, &stream, "raw", 5, 'w');
    ContentTypeReaderManager::AssociateTargetType(System::Type::From<int32_t>(), "Microsoft.Xna.Framework.Content.Int32Reader");
    try { static_cast<void>(input.ReadRawObject<int32_t>(123)); return 0; }
    catch (const System::InvalidOperationException&) { return 1; }
}
extern "C" int cna_test_content_native_stream(const uint8_t* bytes, uint64_t count) try {
    struct Manager final : ContentManager {
        std::vector<uint8_t> bytes;
        unsigned opens = 0;
        bool fail = false;
        Manager(const uint8_t* data, uint64_t size) : bytes(data, data + size) { RouteLoadsThroughOpenStreamEXT(true); }
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string&) override {
            ++opens;
            if (fail) throw ContentLoadException("source refused");
            return std::make_unique<System::IO::MemoryStream>(bytes.data(), static_cast<int32_t>(bytes.size()), false);
        }
        std::string Uncached() { return ReadAsset<std::string>("fresh", {}); }
    } manager(bytes, count);
    if (manager.Load<std::string>("dir/asset") != "cached" || manager.opens != 1) return 0;
    manager.fail = true;
    if (manager.Load<std::string>("dir/asset") != "cached" || manager.opens != 1) return 0;
    try { static_cast<void>(manager.Load<int32_t>("dir/asset")); return 0; } catch (const ContentLoadException&) {}
    if (manager.opens != 1) return 0;
    manager.Unload();
    try { static_cast<void>(manager.Load<std::string>("dir/asset")); return 0; } catch (const ContentLoadException&) {}
    if (manager.opens != 2) return 0;
    manager.fail = false;
    if (manager.Uncached() != "cached" || manager.Uncached() != "cached" || manager.opens != 4) return 0;
    manager.Dispose();
    try { static_cast<void>(manager.Load<std::string>("")); return 0; } catch (const System::ObjectDisposedException&) {}
    return 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "Native content oracle failed: %s\n", error.what());
    return 0;
} catch (...) { return 0; }

