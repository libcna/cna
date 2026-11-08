// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Xnb/EffectContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/InvalidOperationException.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

namespace
{
    using Microsoft::Xna::Framework::Content::ContentManager;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
    using Microsoft::Xna::Framework::Graphics::Effect;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    std::vector<std::uint8_t> InvalidEffectCode()
    {
        // Hand-authored XNA wrapper and D3D9 Effect Framework container. All offsets
        // are relative to the pool after the two Framework header words. The pool
        // is 28 bytes: empty string at 0, pixel-shader object type at 4, value at 24.
        // One technique/pass binds object 0 as PixelShader (state 147). Its shader
        // version is ps_2_0, followed by an undefined opcode (0xdead) and END. Container
        // structure is complete, so rejection must come from shader translation.
        const std::uint32_t words[] = {
            0xbcf00bcf, 8, 0xfeff0901, 28,
            0, 15, 4, 0, 0, 0, 0,
            0, 1, 0, 1,          // parameters, techniques, unused, objects
            0, 0, 1,             // technique: name, annotations, passes
            0, 0, 1,             // pass: name, annotations, states
            147, 0, 4, 24,       // PixelShader state and type/value offsets
            0, 1,                // small and large object counts
            0, 0, 0xffffffff, 0, 0, 12, // technique, pass, unused, state, encoding, bytes
            0xffff0200, 0x0000dead, 0x0000ffff
        };
        std::vector<std::uint8_t> bytes;
        for (const auto word : words)
            for (int shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<std::uint8_t>(word >> shift));
        return bytes;
    }

    std::vector<std::uint8_t> EffectXnb(const std::vector<std::uint8_t>& code)
    {
        // XNB v5: one EffectReader, no shared resources, root reader index 1.
        System::IO::MemoryStream body;
        System::IO::BinaryWriter writer(&body, true);
        writer.Write7BitEncodedInt(1);
        writer.Write(std::string("Microsoft.Xna.Framework.Content.EffectReader"));
        writer.Write(std::int32_t{0});
        writer.Write7BitEncodedInt(0);
        writer.Write7BitEncodedInt(1);
        writer.Write(static_cast<std::int32_t>(code.size()));
        writer.Write(code.data(), 0, static_cast<std::int32_t>(code.size()));
        writer.Flush();
        const auto payload = body.ToArray();
        System::IO::MemoryStream file;
        System::IO::BinaryWriter header(&file, true);
        const std::uint8_t magic[] = {'X', 'N', 'B', 'w', 5, 0};
        for (const auto byte : magic) header.Write(byte);
        header.Write(static_cast<std::int32_t>(10 + payload.size()));
        header.Write(payload.data(), 0, static_cast<std::int32_t>(payload.size()));
        header.Flush();
        const auto result = file.ToArray();
        return {result.begin(), result.end()};
    }

    class EffectContentManager final : public ContentManager
    {
    public:
        EffectContentManager(GraphicsDevice& device, std::vector<std::uint8_t> bytes)
            : bytes_(std::move(bytes))
        {
            RouteLoadsThroughOpenStreamEXT(true);
            setGraphicsDevice(device);
        }
        int opens = 0;
    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string&) override
        {
            ++opens;
            return std::make_unique<System::IO::MemoryStream>(
                bytes_.data(), static_cast<std::int32_t>(bytes_.size()), false);
        }
    private:
        std::vector<std::uint8_t> bytes_;
    };

    template<class Action>
    void ExpectExactRejection(Action action, const std::string& nativeDiagnostic)
    {
        try
        {
            action();
            FAIL() << "invalid shader code was accepted";
        }
        catch (const System::InvalidOperationException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::InvalidOperationException));
            EXPECT_EQ(error.getMessageProperty(), nativeDiagnostic);
        }
        catch (const std::exception& error)
        {
            FAIL() << "wrong exception type " << typeid(error).name() << ": " << error.what();
        }
    }
}

TEST(ContentManagerEffectRejectionTest, InvalidShaderPreservesExactXnaExceptionAndDiagnostic)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not support compiled effects";

    const auto code = InvalidEffectCode();
    std::string nativeDiagnostic;
    try
    {
        auto runtime = device.GetRenderer().CreateCompiledEffect(code.data(), code.size());
        FAIL() << "the native translator must reject the undefined shader opcode";
    }
    catch (const std::runtime_error& error)
    {
        nativeDiagnostic = error.what();
    }
    ASSERT_FALSE(nativeDiagnostic.empty());

    // Independent type oracle: Refresh Effect::.ctor(GraphicsDevice, byte[]) IL_0009;
    // CreateEffectFromCode IL_0208-020d and failed native result IL_0128-012d ->
    // GraphicsHelpers.GetExceptionFromResult IL_0075-007b -> Helpers default IL_0252-025c.
    // EffectReader.Read IL_0016 does not catch; ReadAsset IL_0019-002c wraps IOException only.
    // The diagnostic comparison is a preservation contract, independent of native wording.
    ExpectExactRejection([&] { Effect effect(device, code); }, nativeDiagnostic);

    ContentTypeReaderManager::ClearTypeCreators();
    CNA::Internal::Xnb::RegisterEffectXnbReader();
    EffectContentManager manager(device, EffectXnb(code));
    ExpectExactRejection([&] { (void) manager.Load<std::shared_ptr<Effect>>("invalid"); },
                         nativeDiagnostic);
    EXPECT_EQ(manager.opens, 1);
    ContentTypeReaderManager::ClearTypeCreators();
}
