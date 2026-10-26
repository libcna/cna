// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <string>

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

// The free functions name the build's compile-time DEFAULT renderer; the device names the one it
// was created with (docs/runtime-renderer-selection.md, "Which accessor answers which question").
// The two coincide whenever the default is the selected renderer -- always in a single-renderer
// build. A multi-renderer build may be run with CNA_GRAPHICS_RENDERER naming another one, and the
// device must then report that renderer, not the default.
TEST(GraphicsDeviceRendererTest, GetGraphicsRendererTypeMatchesFreeFunction)
{
    GraphicsDevice gd;
    const CNA::GraphicsRendererType selected = CNA::GraphicsRendererSelection::GetSelected();
    EXPECT_EQ(gd.GetGraphicsRendererType(), selected);
    if (selected == CNA::getCurrentGraphicsRendererType())
        EXPECT_EQ(gd.GetGraphicsRendererType(), CNA::getCurrentGraphicsRendererType());
}

TEST(GraphicsDeviceRendererTest, GetGraphicsRendererNameMatchesFreeFunction)
{
    GraphicsDevice gd;
    const CNA::GraphicsRendererType selected = CNA::GraphicsRendererSelection::GetSelected();
    EXPECT_EQ(gd.GetGraphicsRendererName(), CNA::getGraphicsRendererName(selected));
    if (selected == CNA::getCurrentGraphicsRendererType())
        EXPECT_EQ(gd.GetGraphicsRendererName(), CNA::getCurrentGraphicsRendererName());
}

TEST(GraphicsDeviceRendererTest, GetGraphicsRendererNameIsNotEmpty)
{
    GraphicsDevice gd;
    EXPECT_FALSE(gd.GetGraphicsRendererName().empty());
}

TEST(GraphicsDeviceRendererTest, StartupDiagnosticNeverWritesToStdout)
{
    const CNA::LogLevel previousLevel = CNA::Logger::GetMinimumLevel();
    CNA::Logger::ResetSink();
    CNA::Logger::SetMinimumLevel(CNA::LogLevel::INFO);

    // GoogleTest's capturers are process-global and are released only by GetCaptured*(). An
    // exception that escapes this block leaves both installed, and the next test in the process to
    // capture anything dies on "Only one stdout capturer can exist at a time" -- a CHECK, so it
    // calls abort() and takes the whole run with it. That is not hypothetical: on native Windows,
    // where the retired DIRECTX11 renderer's GraphicsDevice could fail to construct once the D3D11
    // device budget was spent, this test ended CnaTests with exit 3 and no results file at all,
    // losing every test after it.
    std::string constructionFailure;
    ::testing::internal::CaptureStdout();
    ::testing::internal::CaptureStderr();
    try
    {
        GraphicsDevice gd;
        EXPECT_FALSE(gd.GetGraphicsRendererName().empty());
    }
    catch (const std::exception& error)
    {
        constructionFailure = error.what();
    }
    catch (...)
    {
        constructionFailure = "unknown exception";
    }
    const std::string standardError = ::testing::internal::GetCapturedStderr();
    const std::string standardOutput = ::testing::internal::GetCapturedStdout();
    CNA::Logger::SetMinimumLevel(previousLevel);

    if (!constructionFailure.empty())
    {
        GTEST_SKIP() << "no graphics device here: " << constructionFailure;
    }

    EXPECT_TRUE(standardOutput.empty()) << standardOutput;
    EXPECT_NE(standardError.find("CNA: graphics renderer:"), std::string::npos)
        << standardError;
}
