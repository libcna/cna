// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <string>

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

TEST(GraphicsDeviceRendererTest, GetGraphicsRendererTypeMatchesFreeFunction)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.GetGraphicsRendererType(), CNA::getCurrentGraphicsRendererType());
}

TEST(GraphicsDeviceRendererTest, GetGraphicsRendererNameMatchesFreeFunction)
{
    GraphicsDevice gd;
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

    ::testing::internal::CaptureStdout();
    ::testing::internal::CaptureStderr();
    {
        GraphicsDevice gd;
        EXPECT_FALSE(gd.GetGraphicsRendererName().empty());
    }
    const std::string standardError = ::testing::internal::GetCapturedStderr();
    const std::string standardOutput = ::testing::internal::GetCapturedStdout();
    CNA::Logger::SetMinimumLevel(previousLevel);

    EXPECT_TRUE(standardOutput.empty()) << standardOutput;
    EXPECT_NE(standardError.find("CNA: graphics renderer:"), std::string::npos)
        << standardError;
}
