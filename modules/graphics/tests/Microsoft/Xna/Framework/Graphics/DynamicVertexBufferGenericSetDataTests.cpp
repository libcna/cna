// SPDX-License-Identifier: MS-PL
// XNA's DynamicVertexBuffer.SetData<T>(T[], int, int, SetDataOptions) for an application-defined
// vertex type. CNA carried the four built-in vertex types only, so a game with its own per-instance
// stream -- the shape hardware instancing needs, where each element is a plain Matrix -- had no way
// to upload it with streaming semantics at all.

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/ArgumentException.hpp"

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace
{
    static_assert(sizeof(Matrix) == 64);
    static_assert(std::is_trivially_copyable_v<Matrix>);

    // The instance stream every hardware-instancing sample declares: a 4x4 matrix as four
    // Vector4 values carrying the BLENDWEIGHT semantic the vertex shader reads them through.
    VertexDeclaration InstanceDeclaration()
    {
        return VertexDeclaration({
            VertexElement(0,  VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 1),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 2),
            VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 3),
        });
    }

    Matrix Numbered(float first)
    {
        Matrix m;
        float* values = &m.M11;
        for (int i = 0; i < 16; ++i)
            values[i] = first + (float)i;
        return m;
    }

    class DynamicVertexBufferGenericSetDataTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support vertex buffers";
        }
    };

    TEST_F(DynamicVertexBufferGenericSetDataTest, UploadsAnApplicationDefinedVertexType)
    {
        // BufferUsage::None rather than the WriteOnly a game would use: XNA refuses GetData on a
        // write-only resource, and reading the bytes back is the whole point of this test.
        DynamicVertexBuffer buffer(device, InstanceDeclaration(), 3, BufferUsage::None);

        const std::array<Matrix, 3> source{Numbered(0), Numbered(100), Numbered(200)};
        buffer.SetData(source.data(), 0, 3, SetDataOptions::Discard);

        std::array<Matrix, 3> read{};
        buffer.GetDataRawEXT(0, read.data(), 3, (int)sizeof(Matrix));

        EXPECT_EQ(0, std::memcmp(source.data(), read.data(), sizeof(source)));
    }

    // The behavioural half of the contract: startIndex selects where READING from the source
    // begins, while the destination write always starts at the buffer's own beginning. An
    // implementation that forgets to advance the source pointer still compiles and still uploads
    // the right number of bytes -- it just uploads the wrong ones.
    TEST_F(DynamicVertexBufferGenericSetDataTest, StartIndexSelectsWhereReadingBegins)
    {
        DynamicVertexBuffer buffer(device, InstanceDeclaration(), 2, BufferUsage::None);

        const std::array<Matrix, 3> source{Numbered(0), Numbered(100), Numbered(200)};
        buffer.SetData(source.data(), 1, 2, SetDataOptions::Discard);

        std::array<Matrix, 2> read{};
        buffer.GetDataRawEXT(0, read.data(), 2, (int)sizeof(Matrix));

        EXPECT_EQ(0, std::memcmp(&source[1], read.data(), sizeof(read)));
        EXPECT_FLOAT_EQ(100.0f, read[0].M11);
        EXPECT_FLOAT_EQ(200.0f, read[1].M11);
    }

    // XNA's windowed overload, the shape a particle system uses: write only the newly created
    // particles at the position the circular queue has reached, leaving the rest of the buffer
    // alone. An implementation that ignored offsetInBytes would still upload the right bytes --
    // to the wrong place.
    TEST_F(DynamicVertexBufferGenericSetDataTest, WindowedUploadWritesAtTheDestinationOffset)
    {
        DynamicVertexBuffer buffer(device, InstanceDeclaration(), 4, BufferUsage::None);

        const std::array<Matrix, 4> initial{Numbered(0), Numbered(10), Numbered(20), Numbered(30)};
        buffer.SetData(initial.data(), 0, 4, SetDataOptions::Discard);

        // Replace elements 1 and 2 only, reading them from the middle of a longer source.
        const std::array<Matrix, 3> source{Numbered(500), Numbered(600), Numbered(700)};
        buffer.SetData((int)sizeof(Matrix), source.data(), 1, 2,
                       (int)sizeof(Matrix), SetDataOptions::NoOverwrite);

        std::array<Matrix, 4> read{};
        buffer.GetDataRawEXT(0, read.data(), 4, (int)sizeof(Matrix));

        EXPECT_FLOAT_EQ(0.0f, read[0].M11) << "the window must not touch what came before it";
        EXPECT_FLOAT_EQ(600.0f, read[1].M11) << "startIndex selects where reading begins";
        EXPECT_FLOAT_EQ(700.0f, read[2].M11);
        EXPECT_FLOAT_EQ(30.0f, read[3].M11) << "the window must not touch what comes after it";
    }

    // A windowed write is composed in a CPU shadow and uploaded whole, so a buffer that has never
    // had a full upload must still read back as zero outside the window rather than as garbage.
    TEST_F(DynamicVertexBufferGenericSetDataTest, WindowedUploadIntoAnUntouchedBufferZeroesTheRest)
    {
        DynamicVertexBuffer buffer(device, InstanceDeclaration(), 3, BufferUsage::None);

        const std::array<Matrix, 1> source{Numbered(42)};
        buffer.SetData((int)sizeof(Matrix), source.data(), 0, 1,
                       (int)sizeof(Matrix), SetDataOptions::NoOverwrite);

        std::array<Matrix, 3> read{};
        buffer.GetDataRawEXT(0, read.data(), 3, (int)sizeof(Matrix));

        EXPECT_FLOAT_EQ(0.0f, read[0].M11);
        EXPECT_FLOAT_EQ(42.0f, read[1].M11);
        EXPECT_FLOAT_EQ(0.0f, read[2].M11);
    }

    // DynamicVertexBuffer declares SetData overloads of its own, which without a using-declaration
    // hide every VertexBuffer::SetData. XNA's does not: a game restoring a lost buffer calls the
    // whole-array form on the dynamic object.
    TEST_F(DynamicVertexBufferGenericSetDataTest, InheritedWholeBufferSetDataIsVisible)
    {
        DynamicVertexBuffer buffer(device, InstanceDeclaration(), 2, BufferUsage::None);

        const std::array<Matrix, 2> source{Numbered(1), Numbered(2)};
        buffer.SetData(source.data(), 2);

        std::array<Matrix, 2> read{};
        buffer.GetDataRawEXT(0, read.data(), 2, (int)sizeof(Matrix));

        EXPECT_EQ(0, std::memcmp(source.data(), read.data(), sizeof(source)));
    }

    // A raw upload carries no packing step, so the declaration has to describe exactly the bytes
    // the type occupies. A mismatch is a caller error, not something to silently reinterpret.
    TEST_F(DynamicVertexBufferGenericSetDataTest, RejectsADeclarationOfADifferentStride)
    {
        const VertexDeclaration halfWidth({
            VertexElement(0,  VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 1),
        });
        DynamicVertexBuffer buffer(device, halfWidth, 2, BufferUsage::WriteOnly);

        const std::array<Matrix, 2> source{Numbered(0), Numbered(100)};
        EXPECT_THROW(buffer.SetData(source.data(), 0, 2, SetDataOptions::Discard),
                     System::ArgumentException);
    }
}
