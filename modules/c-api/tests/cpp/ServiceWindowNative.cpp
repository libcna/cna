// SPDX-License-Identifier: MS-PL
#include "CNA/C/cna.h"
#include "CNA/Input/Clipboard.hpp"
#include "Microsoft/Xna/Framework/FileDropEventArgsEXT.hpp"
#include "Microsoft/Xna/Framework/TextDropEventArgsEXT.hpp"
#include "../../../platform/tests/CNA/Platform/PlatformTestDecorator.hpp"
#include <thread>
#include <cstring>
namespace {
class MemoryClipboard final : public CNA::Platform::IPlatformClipboard {
public:
    std::string text;
    std::vector<CNA::Platform::ClipboardOffer> offers;
    bool HasText() const override { return !text.empty(); }
    std::string GetText() const override { return text; }
    void SetText(const std::string& value) override { text = value; }
    std::vector<std::string> GetMimeTypes() const override {
        std::vector<std::string> types;
        for (const auto& offer : offers) types.push_back(offer.mimeType);
        return types;
    }
    bool HasData(const std::string& type) const override {
        for (const auto& offer : offers) if (offer.mimeType == type) return true;
        return false;
    }
    std::vector<uint8_t> GetData(const std::string& type) const override {
        for (const auto& offer : offers) if (offer.mimeType == type) return offer.data;
        return {};
    }
    void SetData(const std::vector<CNA::Platform::ClipboardOffer>& value) override { offers = value; }
};
class MemoryPlatform final : public CNA::Platform::Testing::PlatformTestDecorator {
public:
    MemoryClipboard clipboard, primary;
    bool absent = false;
    CNA::Platform::IPlatformClipboard* GetClipboard() override { return absent ? nullptr : &clipboard; }
    CNA::Platform::IPlatformClipboard* GetPrimarySelection() override { return absent ? nullptr : &primary; }
};
std::unique_ptr<MemoryPlatform> platform;
std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> installed;
std::thread::id owner;
CNA::Platform::IPlatform* gamePlatform = nullptr;
}
extern "C" int cna_test_window_begin(void)
{
    owner = std::this_thread::get_id();
    gamePlatform = &CNA::Platform::GetCurrentPlatform();
    platform = std::make_unique<MemoryPlatform>();
    installed = std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(*platform);
    return &CNA::Platform::GetCurrentPlatform() == platform.get();
}
extern "C" int cna_test_window_end(void)
{
    if (!installed) return platform == nullptr;
    // Restore the borrowed game platform while the game still owns it.
    installed.reset();
    const bool restored = CNA::Platform::HasCurrentPlatform() &&
        &CNA::Platform::GetCurrentPlatform() == gamePlatform;
    platform.reset();
    gamePlatform = nullptr;
    return restored;
}
extern "C" int cna_test_window_absent(int absent)
{
    platform->absent = absent != 0;
    auto& current = CNA::Platform::GetCurrentPlatform();
    return &current == platform.get() &&
        (current.GetClipboard() == nullptr) == (absent != 0) &&
        (current.GetPrimarySelection() == nullptr) == (absent != 0);
}
extern "C" int cna_test_window_owner(void) { return owner == std::this_thread::get_id(); }
extern "C" int cna_test_window_worker(int (*test)(void))
{
    int result = 0; std::thread worker([&] { result = test(); }); worker.join(); return result;
}
extern "C" int cna_test_window_native(const char* text, const uint8_t* bytes, uint64_t count)
{
    return CNA::Input::Clipboard::GetPrimarySelectionTextEXT() == text &&
        CNA::Input::Clipboard::GetDataEXT("application/x-cna") == std::vector<uint8_t>(bytes, bytes + count);
}
extern "C" int cna_test_drop_native(int files, const CNA_StringView* items, uint64_t count)
{
    using namespace Microsoft::Xna::Framework;
    if (!files) {
        const TextDropEventArgsEXT args("drop-\xc3\xa9");
        return count == 1 && items[0].byte_length == args.getTextProperty().size() &&
            std::memcmp(items[0].data, args.getTextProperty().data(), items[0].byte_length) == 0;
    }
    const FileDropEventArgsEXT args({"first", "second"});
    if (count != args.getFilesProperty().size()) return 0;
    for (std::size_t i = 0; i < count; ++i)
        if (items[i].byte_length != args.getFilesProperty()[i].size() ||
            std::memcmp(items[i].data, args.getFilesProperty()[i].data(), items[i].byte_length) != 0) return 0;
    return 1;
}
