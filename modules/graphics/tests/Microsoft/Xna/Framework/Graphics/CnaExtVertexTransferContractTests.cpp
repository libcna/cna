// SPDX-License-Identifier: MS-PL
// TQ-021: the CNAEXT typed VertexBuffer transfers (VertexPositionNormalTextureSkinned,
// VertexPositionNormalTangentTexture, VertexPositionNormalTangentTextureSkinned) follow the same
// classic copy contract as the XNA vertex types.
//
// Oracle: Microsoft XNA 4.0 VertexBuffer::CopyData<T>, the single implementation behind every
// SetData<T>/GetData<T>(T[] data, int startIndex, int elementCount) overload:
//  IL_0015-0023  null or empty data -> ArgumentNullException("data") (thrown at IL_02be);
//  IL_009f-00a4  Helpers::ValidateCopyParameters: dataIndex outside [0, data.Length] ->
//                ArgumentOutOfRangeException("dataIndex"); elementCount <= 0 ->
//                ArgumentOutOfRangeException("elementCount") (Framework Helpers IL_0000-0041);
//  IL_00eb-0100  more bytes than the buffer holds -> InvalidOperationException;
//  IL_01eb-01f0  element i of the transfer is data[startIndex + i] and buffer element i: the
//                start index addresses the caller's array, never the buffer.
// The header documents the same meaning ("Index of the first element to write in data").
// Before TQ-021 the tangent SetData overloads were unvalidated (a null source was dereferenced
// and an over-long upload overran the CPU shadow) and these GetData overloads applied startIndex
// to the buffer instead of the destination.
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <typeinfo>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"

using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTextureSkinned;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTextureSkinned;

namespace
{
    template<typename TException, typename TCallable>
    void ExpectNamed(TCallable&& callable, const char* parameterName, const char* what)
    {
        try
        {
            callable();
            ADD_FAILURE() << what << ": accepted";
        }
        catch (const TException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(TException)) << what;
            EXPECT_EQ(exception.getParamNameProperty(), parameterName) << what;
        }
        catch (...)
        {
            ADD_FAILURE() << what << ": unexpected exception type";
        }
    }

    template<typename TCallable>
    void ExpectExactInvalidOperation(TCallable&& callable, const char* what)
    {
        try
        {
            callable();
            ADD_FAILURE() << what << ": accepted";
        }
        catch (const System::InvalidOperationException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(System::InvalidOperationException)) << what;
        }
        catch (...)
        {
            ADD_FAILURE() << what << ": unexpected exception type";
        }
    }

    VertexPositionNormalTextureSkinned Skinned(float seed)
    {
        return VertexPositionNormalTextureSkinned(
            Vector3(seed, seed + 1.0f, seed + 2.0f), Vector3(0.0f, 0.0f, 1.0f),
            Vector2(seed / 8.0f, seed / 16.0f), Vector4(1.0f, 0.0f, 0.0f, 0.0f),
            std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(seed), 1, 2, 3});
    }

    VertexPositionNormalTangentTexture Tangent(float seed)
    {
        return VertexPositionNormalTangentTexture(
            Vector3(seed, seed + 1.0f, seed + 2.0f), Vector3(0.0f, 1.0f, 0.0f),
            Vector4(1.0f, 0.0f, 0.0f, seed), Vector2(seed / 4.0f, seed / 2.0f));
    }

    VertexPositionNormalTangentTextureSkinned TangentSkinned(float seed)
    {
        return VertexPositionNormalTangentTextureSkinned(
            Vector3(seed, seed + 1.0f, seed + 2.0f), Vector3(0.0f, 1.0f, 0.0f),
            Vector4(1.0f, 0.0f, 0.0f, seed), Vector2(seed / 4.0f, seed / 2.0f),
            Vector4(0.5f, 0.5f, 0.0f, 0.0f),
            std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(seed), 4, 5, 6});
    }

    class CnaExtVertexTransferContractTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        void SetUp() override
        {
            device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
            if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
                GTEST_SKIP() << "renderer has no vertex buffers";
        }

        // One shared body for the three types: A and B uploaded, then every refusal, then the
        // destination-indexed readback.
        template<typename TVertex>
        void ExpectClassicContract(TVertex a, TVertex b, TVertex sentinel)
        {
            VertexBuffer buffer(device, TVertex::getVertexDeclarationStatic(), 2,
                                BufferUsage::None);
            const std::array<TVertex, 2> source{a, b};
            buffer.SetData(source.data(), 2);

            const std::array<TVertex, 3> tooMany{sentinel, sentinel, sentinel};
            ExpectNamed<System::ArgumentNullException>(
                [&] { buffer.SetData(static_cast<const TVertex*>(nullptr), 1); }, "data",
                "SetData(null)");
            ExpectNamed<System::ArgumentOutOfRangeException>(
                [&] { buffer.SetData(source.data(), -1, 1); }, "dataIndex",
                "SetData negative start");
            ExpectNamed<System::ArgumentOutOfRangeException>(
                [&] { buffer.SetData(source.data(), 0, 0); }, "elementCount",
                "SetData zero count");
            ExpectExactInvalidOperation([&] { buffer.SetData(tooMany.data(), 3); },
                                        "SetData beyond the buffer");

            std::array<TVertex, 3> destination{sentinel, sentinel, sentinel};
            ExpectNamed<System::ArgumentNullException>(
                [&] { buffer.GetData(static_cast<TVertex*>(nullptr), 0, 1); }, "data",
                "GetData(null)");
            ExpectNamed<System::ArgumentOutOfRangeException>(
                [&] { buffer.GetData(destination.data(), -1, 1); }, "dataIndex",
                "GetData negative start");
            ExpectNamed<System::ArgumentOutOfRangeException>(
                [&] { buffer.GetData(destination.data(), 0, 0); }, "elementCount",
                "GetData zero count");
            ExpectExactInvalidOperation([&] { buffer.GetData(destination.data(), 0, 3); },
                                        "GetData beyond the buffer");
            EXPECT_EQ(destination[0], sentinel);
            EXPECT_EQ(destination[1], sentinel);
            EXPECT_EQ(destination[2], sentinel);

            // startIndex 1 addresses destination[1]; the buffer is read from its vertex 0.
            buffer.GetData(destination.data(), 1, 2);
            EXPECT_EQ(destination[0], sentinel);
            EXPECT_EQ(destination[1], a);
            EXPECT_EQ(destination[2], b);

            // The refused uploads changed nothing.
            std::array<TVertex, 2> whole{sentinel, sentinel};
            buffer.GetData(whole.data(), 2);
            EXPECT_EQ(whole[0], a);
            EXPECT_EQ(whole[1], b);
        }
    };
}

TEST_F(CnaExtVertexTransferContractTest, SkinnedVerticesFollowTheClassicCopyContract)
{
    ExpectClassicContract(Skinned(1.0f), Skinned(2.0f), Skinned(9.0f));
}

TEST_F(CnaExtVertexTransferContractTest, TangentVerticesFollowTheClassicCopyContract)
{
    ExpectClassicContract(Tangent(1.0f), Tangent(2.0f), Tangent(9.0f));
}

TEST_F(CnaExtVertexTransferContractTest, SkinnedTangentVerticesFollowTheClassicCopyContract)
{
    ExpectClassicContract(TangentSkinned(1.0f), TangentSkinned(2.0f), TangentSkinned(9.0f));
}
