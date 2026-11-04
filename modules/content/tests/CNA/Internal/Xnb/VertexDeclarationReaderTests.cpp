// SPDX-License-Identifier: MS-PL
//
// TQ-033: VertexDeclarationReader on malformed declarations refuses with the exceptions XNA's
// own construction throws, in XNA's order.
//
// Oracle (Microsoft.Xna.Framework.Graphics.dll 4.0):
//   VertexDeclarationReader.Read IL_0000-0061: stride, count, `newarr VertexElement[count]`
//     (IL_000f-0010: OverflowException for a negative count), every element, then
//     new VertexDeclaration(stride, elements);
//   VertexDeclaration .ctor(int32, VertexElement[]) IL_0006-0011 / IL_003e-004e: no elements is
//     ArgumentNullException("elements"), checked before
//   VertexElementValidator.Validate IL_0000-000e: a stride <= 0 is
//     ArgumentOutOfRangeException("vertexStride"); an element outside the stride is
//     ArgumentException.

#include <any>
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/ModelContentTypeReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/OverflowException.hpp"

using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderBase;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace
{
    struct Element { std::int32_t offset; std::int32_t format; std::int32_t usage; std::int32_t usageIndex; };

    class VertexDeclarationReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterModelXnbReaders();
            reader_ = ContentTypeReaderManager::CreateReader(
                "Microsoft.Xna.Framework.Content.VertexDeclarationReader");
            ASSERT_NE(reader_, nullptr);
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        std::any Read(const std::int32_t stride, const std::int32_t count,
                      const std::vector<Element>& elements)
        {
            auto stream = std::make_unique<System::IO::MemoryStream>();
            System::IO::BinaryWriter writer(stream.get(), true);
            writer.Write(stride);
            writer.Write(count);
            for (const Element& element : elements)
            {
                writer.Write(element.offset);
                writer.Write(element.format);
                writer.Write(element.usage);
                writer.Write(element.usageIndex);
            }
            writer.Flush();
            const auto bytes = stream->ToArray();
            streams_.push_back(std::make_unique<System::IO::MemoryStream>(
                bytes.data(), static_cast<std::int32_t>(bytes.size())));
            readers_.push_back(std::make_unique<ContentReader>(nullptr, streams_.back().get(), "decl", 5, 'w'));
            return reader_->ReadUntyped(*readers_.back(), std::any{});
        }

        std::unique_ptr<ContentTypeReaderBase> reader_;
        std::vector<std::unique_ptr<System::IO::MemoryStream>> streams_;
        std::vector<std::unique_ptr<ContentReader>> readers_;
    };

    constexpr Element kPosition{0, 2, 0, 0};  // (0, Vector3, Position, 0)
}

TEST_F(VertexDeclarationReaderTest, AWellFormedDeclarationKeepsItsStrideAndElements)
{
    const auto declaration = std::any_cast<VertexDeclaration>(
        Read(16, 2, {kPosition, {12, 4, 1, 0}}));  // + (12, Color, Color, 0)
    EXPECT_EQ(declaration.getVertexStrideProperty(), 16);
    EXPECT_EQ(declaration.GetVertexElements(),
              (std::vector<VertexElement>{
                  VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                  VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)}));
}

TEST_F(VertexDeclarationReaderTest, AStrideOfZeroOrLessIsArgumentOutOfRange)
{
    EXPECT_THROW((void)Read(0, 1, {kPosition}), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)Read(-12, 1, {kPosition}), System::ArgumentOutOfRangeException);
}

TEST_F(VertexDeclarationReaderTest, NoElementsIsArgumentNullBeforeTheStrideIsChecked)
{
    EXPECT_THROW((void)Read(12, 0, {}), System::ArgumentNullException);
    // The empty-array check comes first, so a bad stride is not what is reported.
    EXPECT_THROW((void)Read(0, 0, {}), System::ArgumentNullException);
}

TEST_F(VertexDeclarationReaderTest, ANegativeElementCountIsOverflow)
{
    EXPECT_THROW((void)Read(12, -1, {}), System::OverflowException);
}

TEST_F(VertexDeclarationReaderTest, AnElementOutsideTheStrideIsArgumentException)
{
    // A Vector3 at offset 0 needs 12 bytes; the stride gives 8.
    EXPECT_THROW((void)Read(8, 1, {kPosition}), System::ArgumentException);
}
