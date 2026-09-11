// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-163 -- construction and use must agree about a block-compressed
// `TextureCube`.
//
// THE DEFECT THIS MEASURES, stated as a renderer-neutral invariant rather than as one renderer's
// behaviour: `WebGPURenderer::ClassifySurfaceFormatEXT()` answered `Supported` for BC formats
// whenever the device had the feature, and ONE classifier served both `Texture2D` and `TextureCube`
// -- while `CreateTextureCube` discarded the format and the cube renderer stored RGBA8 only. So
// `TextureCube(device, size, mipMap, SurfaceFormat::Dxt1)` CONSTRUCTED happily and then threw from
// every `SetData`. A capability answer that a later call contradicts is the one shape such an
// answer exists to prevent.
//
// The invariant holds for every renderer and is not a statement about which of them supports
// compressed cubes:
//
//     if a TextureCube CONSTRUCTS with a block-compressed format,
//     then a compressed SetData on it must not be refused for lack of a route.
//
// Both outcomes are legal. A renderer with a compressed cube path constructs and uploads; one
// without refuses at construction. What is not legal is promising at construction and refusing at
// use -- and no renderer needs a special case here, which is what makes this the right shape.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

namespace
{
    using Microsoft::Xna::Framework::Graphics::CubeMapFace;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::TextureCube;

    [[nodiscard]] std::string RendererName()
    {
        return std::string(CNA::getGraphicsRendererName(
            CNA::GraphicsRendererSelection::GetSelected()));
    }

    struct CompressedFormat
    {
        const char* name;
        SurfaceFormat format;
        /// Bytes per 4x4 block: 8 for DXT1, 16 for the rest.
        int blockBytes;
    };

    /// The formats a renderer might store as blocks. Listed rather than derived, so a new one has
    /// to be considered here deliberately.
    const std::vector<CompressedFormat> kCompressedFormats{
        {"Dxt1", SurfaceFormat::Dxt1, 8},
        {"Dxt3", SurfaceFormat::Dxt3, 16},
        {"Dxt5", SurfaceFormat::Dxt5, 16},
        {"Dxt5SrgbEXT", SurfaceFormat::Dxt5SrgbEXT, 16},
        {"Bc7EXT", SurfaceFormat::Bc7EXT, 16},
        {"Bc7SrgbEXT", SurfaceFormat::Bc7SrgbEXT, 16},
    };

    /// A cube edge that is a clean multiple of the 4-texel block, so a refusal can never be about
    /// the size.
    constexpr int kCubeSize = 8;
}

TEST(TextureCubeCompressedFormatAgreement, ConstructionAndUseAgreeForEveryCompressedFormat)
{
    GraphicsDevice device;

    for (const CompressedFormat& candidate : kCompressedFormats)
    {
        SCOPED_TRACE(candidate.name);

        std::string constructionRefusal;
        bool constructed = false;
        try
        {
            TextureCube cube(device, kCubeSize, false, candidate.format);
            constructed = true;

            // One face's worth of blocks at level 0: (8/4) * (8/4) = 4 blocks.
            const std::size_t blocks = static_cast<std::size_t>((kCubeSize / 4) * (kCubeSize / 4));
            std::vector<std::uint8_t> payload(
                blocks * static_cast<std::size_t>(candidate.blockBytes), 0u);

            std::string useRefusal;
            try
            {
                cube.SetData(CubeMapFace::PositiveX, 0, nullptr, payload.data(), 0,
                             static_cast<int>(payload.size()));
            }
            catch (const std::exception& e)
            {
                useRefusal = e.what();
            }

            std::cout << "[WEBGPU-163] " << RendererName() << ' ' << candidate.name
                      << ": constructed, SetData "
                      << (useRefusal.empty() ? std::string("accepted") : '"' + useRefusal + '"')
                      << std::endl;

            // THE INVARIANT. A renderer that accepted the format at construction has promised it
            // can store it; refusing the matching upload for lack of a route contradicts that.
            // Other refusals (a bad size, a disposed device) are not what this measures, so the
            // check names the specific contradiction rather than banning every exception.
            EXPECT_EQ(std::string::npos, useRefusal.find("no compressed cube transfer route"))
                << RendererName() << " constructed a " << candidate.name
                << " TextureCube and then refused its compressed upload: \"" << useRefusal
                << "\". Construction promised a format the cube path cannot store.";
        }
        catch (const std::exception& e)
        {
            // Deliberately every exception type, not just `System::NotSupportedException`. CNA has
            // two refusal sites here and they do not agree on the type: the renderer-verdict gate
            // this row added throws `NotSupportedException`, while a format the renderer defers on
            // falls through to `Texture::ValidateFormat`, which throws `std::runtime_error`
            // (EasyGL takes that second path for `Dxt5SrgbEXT`/`Bc7EXT`/`Bc7SrgbEXT`, which it does
            // not classify at all). Which type a refusal uses is worth settling, but it is not what
            // THIS test measures -- narrowing the catch here would only turn one renderer's
            // legitimate refusal into an error and hide the agreement the test is for.
            constructionRefusal = e.what();
        }

        if (!constructed)
        {
            std::cout << "[WEBGPU-163] " << RendererName() << ' ' << candidate.name
                      << ": refused at construction -- \"" << constructionRefusal << '"'
                      << std::endl;
            // A refusal is a legitimate answer, but it has to be legible: a caller choosing a
            // fallback format needs to know which format was refused and for what.
            EXPECT_FALSE(constructionRefusal.empty())
                << RendererName() << " refused a " << candidate.name
                << " TextureCube without saying why";
            EXPECT_NE(std::string::npos, constructionRefusal.find("SurfaceFormat"))
                << RendererName() << " refused a " << candidate.name
                << " TextureCube without naming the format: \"" << constructionRefusal << '"';
        }
    }
}
