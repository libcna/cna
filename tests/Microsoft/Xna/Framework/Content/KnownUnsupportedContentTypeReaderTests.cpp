// SPDX-License-Identifier: MS-PL
//
// plan_xnb.md XNB-32A: verifies the general Microsoft.Xna.Framework.Content.EffectReader
// (compiled platform shader bytecode) actually throws its documented, precise
// ContentLoadException when dispatched through a real ContentReader -- ContentTypeReaderManagerTests.cpp
// deliberately only covers registration, not ReadUntyped()'s own throw behavior.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/KnownUnsupportedContentTypeReader.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Content::RegisterKnownUnsupportedXnbReaders;

namespace
{
    class KnownUnsupportedContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            RegisterKnownUnsupportedXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };
}

TEST_F(KnownUnsupportedContentTypeReaderTest, GeneralEffectReaderThrowsPreciseContentLoadException)
{
    ContentManager cm;
    System::IO::MemoryStream ms; // no bytes needed: ReadUntyped() throws before reading anything
    ContentReader reader(&cm, &ms, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.EffectReader");
    ASSERT_NE(typeReader, nullptr);

    try
    {
        typeReader->ReadUntyped(reader, std::any{});
        FAIL() << "expected ContentLoadException";
    }
    catch (const ContentLoadException& ex)
    {
        const std::string message = ex.what();
        EXPECT_NE(message.find("Microsoft.Xna.Framework.Graphics.Effect"), std::string::npos);
        EXPECT_NE(message.find("compiled platform shader bytecode"), std::string::npos);
    }
}
