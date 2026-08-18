// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-9: the engine layer's exception type.
//
// The message shape is the point. A caller that catches this is usually writing it to a log a
// human will read weeks later, so the sentence has to name all three parts of the failure without
// the reader having to know which subsystem was running. That is what is pinned here, along with
// the two properties every exception type has to get right: it is catchable as its base, and the
// message survives being thrown.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/EngineException.hpp"
#include "System/Exception.hpp"

#include <exception>
#include <string>

namespace {

using CNA::Graphics::EngineException;

TEST(EngineExceptionTest, TheStandardMessageNamesSubsystemRequirementAndRenderer)
{
    const EngineException exception =
        EngineException::NotSupported("BloomPass", "float render targets", "Headless");

    EXPECT_EQ(exception.getMessageProperty(),
              "BloomPass: float render targets is not supported by the Headless renderer");
}

TEST(EngineExceptionTest, TheStandardMessageKeepsItsThreeParts)
{
    // Recorded separately from the sentence so a caller can act on the parts -- a log line is not
    // the only consumer, and re-parsing the message would tie every caller to its wording.
    const EngineException exception =
        EngineException::NotSupported("ShadowMap", "custom effects", "SdlRenderer");

    EXPECT_EQ(exception.getSubsystemProperty(), "ShadowMap");
    EXPECT_EQ(exception.getRequirementProperty(), "custom effects");
    EXPECT_EQ(exception.getRendererNameProperty(), "SdlRenderer");
}

TEST(EngineExceptionTest, AFreeFormMessageIsKeptVerbatimAndLeavesThePartsEmpty)
{
    const EngineException exception("RenderPipeline: the frame was never begun");

    EXPECT_EQ(exception.getMessageProperty(), "RenderPipeline: the frame was never begun");
    EXPECT_TRUE(exception.getSubsystemProperty().empty());
    EXPECT_TRUE(exception.getRequirementProperty().empty());
    EXPECT_TRUE(exception.getRendererNameProperty().empty());
}

TEST(EngineExceptionTest, ItIsCatchableAsItsBasesAndKeepsItsMessage)
{
    // Both bases matter: a game catching System::Exception should see this, and code that only
    // knows std::exception -- a test harness, a top-level handler -- should still get the text.
    try
    {
        throw EngineException::NotSupported("SsaoPass", "a depth prepass", "Stub");
    }
    catch (const System::Exception& sharp)
    {
        EXPECT_EQ(sharp.getMessageProperty(),
                  "SsaoPass: a depth prepass is not supported by the Stub renderer");
    }

    try
    {
        throw EngineException::NotSupported("SsaoPass", "a depth prepass", "Stub");
    }
    catch (const std::exception& standard)
    {
        EXPECT_EQ(std::string(standard.what()),
                  "SsaoPass: a depth prepass is not supported by the Stub renderer");
    }
}

TEST(EngineExceptionTest, TheDerivedTypeSurvivesBeingThrown)
{
    // Throwing by value slices anything that is caught by value; caught by reference it must still
    // be an EngineException, or the properties above are unreachable at every real throw site.
    try
    {
        throw EngineException::NotSupported("Skybox", "cube maps", "Gdi");
    }
    catch (const EngineException& engine)
    {
        EXPECT_EQ(engine.getRendererNameProperty(), "Gdi");
    }
    catch (...)
    {
        ADD_FAILURE() << "the exception did not arrive as an EngineException";
    }
}

} // namespace

#endif // CNA_CNAEXT
