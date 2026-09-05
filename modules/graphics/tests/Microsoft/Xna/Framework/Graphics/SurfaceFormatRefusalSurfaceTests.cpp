// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-183: the `SurfaceFormat` refusal surface, per resource kind.
//
// Every `SurfaceFormat` against every resource kind -- `Texture2D`, `TextureCube`, `Texture3D`,
// `RenderTarget2D`, `RenderTargetCube` -- with one rule and no per-renderer expectation table:
//
//   a resource either CONSTRUCTS, and then reports the format it was asked for,
//   or it REFUSES BY NAME, throwing System::NotSupportedException or std::runtime_error.
//
// There is deliberately no list of which formats a renderer must accept. A list would have to be
// rewritten every time a renderer gains one -- `WEBGPU-198`..`202` added eight render-target
// formats to WebGPU alone since this row was written, and `WEBGPU-206` added the compressed cube --
// and a test that needs editing to stay true is not locking anything down. What is invariant, and
// what this test asserts, is the SHAPE of the answer: never a silent substitution, never an
// unnamed exception, never a resource that says it is one format while holding another.
//
// The silent-substitution half is the one with teeth, and it is not hypothetical: `WEBGPU-163` was
// exactly a format query promising something the resource did not hold, and XNA's own
// `RenderTarget2D` substitutes `Color` for an unsupported format rather than throwing (recorded in
// `plans/plan_graphics.md`; CNA deliberately diverges and refuses instead). A renderer drifting
// back toward substitution is what this catches.
//
// The test also PRINTS the surface it found, so the diff `WEBGPU-184` produces when it adds a
// format is visible in the log rather than only in a pass/fail bit.

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdio>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>

using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

namespace
{
    struct NamedFormat { SurfaceFormat format; const char* name; };

    /// Every SurfaceFormat the enum defines, named so the printed surface is readable.
    constexpr std::array<NamedFormat, 26> kAllFormats{{
        {SurfaceFormat::Color, "Color"},
        {SurfaceFormat::Bgr565, "Bgr565"},
        {SurfaceFormat::Bgra5551, "Bgra5551"},
        {SurfaceFormat::Bgra4444, "Bgra4444"},
        {SurfaceFormat::Dxt1, "Dxt1"},
        {SurfaceFormat::Dxt3, "Dxt3"},
        {SurfaceFormat::Dxt5, "Dxt5"},
        {SurfaceFormat::NormalizedByte2, "NormalizedByte2"},
        {SurfaceFormat::NormalizedByte4, "NormalizedByte4"},
        {SurfaceFormat::Rgba1010102, "Rgba1010102"},
        {SurfaceFormat::Rg32, "Rg32"},
        {SurfaceFormat::Rgba64, "Rgba64"},
        {SurfaceFormat::Alpha8, "Alpha8"},
        {SurfaceFormat::Single, "Single"},
        {SurfaceFormat::Vector2, "Vector2"},
        {SurfaceFormat::Vector4, "Vector4"},
        {SurfaceFormat::HalfSingle, "HalfSingle"},
        {SurfaceFormat::HalfVector2, "HalfVector2"},
        {SurfaceFormat::HalfVector4, "HalfVector4"},
        {SurfaceFormat::HdrBlendable, "HdrBlendable"},
        {SurfaceFormat::ColorBgraEXT, "ColorBgraEXT"},
        {SurfaceFormat::ColorSrgbEXT, "ColorSrgbEXT"},
        {SurfaceFormat::Dxt5SrgbEXT, "Dxt5SrgbEXT"},
        {SurfaceFormat::Bc7EXT, "Bc7EXT"},
        {SurfaceFormat::Bc7SrgbEXT, "Bc7SrgbEXT"},
        {SurfaceFormat::ByteEXT, "ByteEXT"},
    }};

    /// What happened when a resource of one kind was asked for in one format.
    struct Outcome
    {
        bool constructed = false;
        /// Set only when `constructed`: the format the resource says it holds.
        std::optional<SurfaceFormat> reported;
        /// Set only when refused: which named exception type carried the refusal.
        const char* refusalKind = nullptr;
        std::string message;
    };

    /// Runs one construction and classifies it. Anything not derived from the two named exception
    /// types leaves `refusalKind` null, which is the failure this test exists to find.
    template <typename Fn>
    [[nodiscard]] Outcome Attempt(Fn&& construct)
    {
        Outcome outcome;
        try
        {
            outcome.reported = construct();
            outcome.constructed = true;
        }
        catch (const System::NotSupportedException& exception)
        {
            outcome.refusalKind = "NotSupportedException";
            outcome.message = exception.what();
        }
        catch (const std::runtime_error& exception)
        {
            outcome.refusalKind = "runtime_error";
            outcome.message = exception.what();
        }
        catch (const std::exception& exception)
        {
            outcome.message = std::string("UNNAMED: ") + exception.what();
        }
        catch (...)
        {
            outcome.message = "UNNAMED: a non-std exception";
        }
        return outcome;
    }

    void ExpectAcceptOrNamedRefusal(const Outcome& outcome, const char* kind, const char* formatName,
                                    SurfaceFormat requested)
    {
        if (outcome.constructed)
        {
            ASSERT_TRUE(outcome.reported.has_value());
            EXPECT_EQ(static_cast<int>(*outcome.reported), static_cast<int>(requested))
                << kind << " accepted " << formatName << " but reports format ordinal "
                << static_cast<int>(*outcome.reported)
                << " -- a resource must hold the format it was asked for, or refuse. A silent "
                   "substitution is the defect WEBGPU-163 was";
            return;
        }
        EXPECT_NE(outcome.refusalKind, nullptr)
            << kind << " refused " << formatName << " without a named exception: "
            << outcome.message
            << " -- a refusal must be System::NotSupportedException or std::runtime_error so a "
               "caller can tell 'not on this profile' from 'not on this renderer'";
    }
}

// One test per resource kind, so a failure names the kind without needing the message parsed.
#define CNA_FORMAT_SURFACE_TEST(TestName, KindLabel, Construct)                                    \
    TEST(SurfaceFormatRefusalSurface, TestName)                                                    \
    {                                                                                              \
        GraphicsDevice device;                                                                     \
        std::string accepted;                                                                      \
        for (const NamedFormat& entry : kAllFormats)                                               \
        {                                                                                          \
            const SurfaceFormat format = entry.format;                                             \
            const Outcome outcome = Attempt([&]() -> SurfaceFormat Construct);                     \
            ExpectAcceptOrNamedRefusal(outcome, KindLabel, entry.name, format);                    \
            if (outcome.constructed) accepted += std::string(accepted.empty() ? "" : ", ")          \
                                              + entry.name;                                        \
        }                                                                                          \
        std::printf("[surface] %-16s accepts: %s\n", KindLabel,                                    \
                    accepted.empty() ? "(nothing)" : accepted.c_str());                            \
    }

CNA_FORMAT_SURFACE_TEST(Texture2DAcceptsOrRefusesByName, "Texture2D",
                        { Texture2D resource(device, 4, 4, false, format);
                          return resource.getFormatProperty(); })

CNA_FORMAT_SURFACE_TEST(TextureCubeAcceptsOrRefusesByName, "TextureCube",
                        { TextureCube resource(device, 4, false, format);
                          return resource.getFormatProperty(); })

CNA_FORMAT_SURFACE_TEST(Texture3DAcceptsOrRefusesByName, "Texture3D",
                        { Texture3D resource(device, 4, 4, 2, false, format);
                          return resource.getFormatProperty(); })

CNA_FORMAT_SURFACE_TEST(RenderTarget2DAcceptsOrRefusesByName, "RenderTarget2D",
                        { RenderTarget2D resource(device, 4, 4, false, format, DepthFormat::None, 0,
                                                  RenderTargetUsage::DiscardContents);
                          return resource.getFormatProperty(); })

CNA_FORMAT_SURFACE_TEST(RenderTargetCubeAcceptsOrRefusesByName, "RenderTargetCube",
                        { RenderTargetCube resource(device, 4, false, format, DepthFormat::None, 0,
                                                    RenderTargetUsage::DiscardContents);
                          return resource.getFormatProperty(); })

#undef CNA_FORMAT_SURFACE_TEST
