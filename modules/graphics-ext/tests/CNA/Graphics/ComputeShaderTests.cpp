// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-1510..MOD-1525: compute shaders and storage buffers, end to end.
//
// Every test here starts by asking the device whether it can do this at all, because the answer
// genuinely varies -- GL ES 3.1 and desktop GL 4.3 can, and the ES 2.0/3.0 profiles several CNA
// renderers target cannot. Where it can, the assertions are exact: a shader that doubles a
// thousand floats produces exactly those floats back, and a shader that writes a gradient into a
// texture produces exactly that gradient.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/GraphicsMemoryBarrier.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/NotSupportedException.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using CNA::GraphicsCapability;
using CNA::GraphicsImageAccess;
using CNA::GraphicsMemoryBarrier;
using CNA::Graphics::ComputeShader;
using CNA::Graphics::StorageBuffer;
using CNA::Graphics::StorageBufferT;

namespace {

    class ComputeTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;

        [[nodiscard]] bool supported() const
        {
            return gd.SupportsCapability(GraphicsCapability::ComputeShaders);
        }
    };

    /// Doubles every element of a float buffer. `local_size_x` is 64, so the dispatch count is
    /// the element count divided by 64.
    const char* const kDoubler = R"(#version 310 es
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Values { float values[]; };
uniform int uCount;
void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index < uint(uCount)) values[index] = values[index] * 2.0;
}
)";

} // namespace

TEST_F(ComputeTest, TheCapabilityAndTheLimitsAgreeWithEachOther)
{
    // MOD-1505/MOD-1510: whatever the answer is, the two halves must not disagree -- a device that
    // claims compute must be able to say how large a dispatch it takes, and one that does not must
    // report zero rather than a plausible-looking number.
    if (supported())
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            EXPECT_GT(gd.GetMaxComputeWorkGroupCountEXT(axis), 0) << "axis " << axis;
            EXPECT_GT(gd.GetMaxComputeWorkGroupSizeEXT(axis), 0) << "axis " << axis;
        }
        EXPECT_GT(gd.GetMaxComputeWorkGroupInvocationsEXT(), 0);
    }
    else
    {
        EXPECT_EQ(gd.GetMaxComputeWorkGroupCountEXT(0), 0);
        EXPECT_EQ(gd.GetMaxComputeWorkGroupSizeEXT(0), 0);
        EXPECT_EQ(gd.GetMaxComputeWorkGroupInvocationsEXT(), 0);
    }
    // An out-of-range axis is 0 on every device, supported or not.
    EXPECT_EQ(gd.GetMaxComputeWorkGroupCountEXT(-1), 0);
    EXPECT_EQ(gd.GetMaxComputeWorkGroupCountEXT(3), 0);
}

TEST_F(ComputeTest, WithoutSupportBothWrappersRefuseByName)
{
    // MOD-1522: the refusal names the renderer, and it happens at construction -- an object whose
    // every method silently did nothing would be the worst of the available behaviours.
    if (supported()) GTEST_SKIP() << "this renderer supports compute; the refusal path is elsewhere";
    EXPECT_THROW(StorageBuffer(gd, 1024), System::NotSupportedException);
    EXPECT_THROW(ComputeShader(gd, kDoubler), System::NotSupportedException);
}

TEST_F(ComputeTest, AStorageBufferRoundTripsAMegabyteExactly)
{
    // MOD-1512.
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    constexpr std::size_t kBytes = 1024u * 1024u;
    StorageBuffer buffer(gd, kBytes);
    EXPECT_EQ(buffer.getByteSize(), kBytes);

    std::vector<std::uint8_t> source(kBytes);
    for (std::size_t i = 0; i < kBytes; ++i)
        source[i] = static_cast<std::uint8_t>((i * 31u + 7u) & 0xFFu);
    buffer.setBytes(source.data(), source.size());

    std::vector<std::uint8_t> read(kBytes, 0);
    buffer.getBytes(read.data(), read.size());
    EXPECT_EQ(read, source);
}

TEST_F(ComputeTest, TheBufferRefusesOversizedAndNullTransfers)
{
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    StorageBuffer buffer(gd, 64);
    std::vector<std::uint8_t> data(128, 1);
    EXPECT_THROW(buffer.setBytes(data.data(), data.size()), std::invalid_argument);
    EXPECT_THROW(buffer.setBytes(nullptr, 4), std::invalid_argument);
    EXPECT_THROW(buffer.getBytes(data.data(), 128), std::invalid_argument);
    EXPECT_THROW(buffer.getBytes(nullptr, 4), std::invalid_argument);
    EXPECT_THROW(StorageBuffer(gd, 0), std::invalid_argument);
}

TEST_F(ComputeTest, ABrokenShaderThrowsWithItsCompilerLog)
{
    // MOD-1511: the log is the whole value of the failure, so it must reach the caller.
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    try
    {
        ComputeShader broken(gd, "#version 310 es\nthis is not a shader\n");
        FAIL() << "a shader that cannot compile was accepted";
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("did not compile"), std::string::npos) << message;
        EXPECT_GT(message.size(), std::string("CNA::Graphics::ComputeShader: the program did not "
                                              "compile: ").size())
            << "the compiler log was empty: " << message;
    }
}

TEST_F(ComputeTest, ADispatchDoublesEveryElementOfABuffer)
{
    // MOD-1513, the row's own example: 1024 floats, doubled.
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    constexpr int kCount = 1024;
    StorageBufferT<float> values(gd, kCount);
    std::vector<float> source(kCount);
    for (int i = 0; i < kCount; ++i) source[static_cast<std::size_t>(i)] = static_cast<float>(i);
    values.setData(source);

    ComputeShader doubler(gd, kDoubler);
    EXPECT_TRUE(doubler.isValid());
    EXPECT_TRUE(doubler.getCompileError().empty());
    doubler.bindStorageBuffer(0, values.getBuffer());
    doubler.setUniform("uCount", kCount);
    doubler.dispatch(kCount / 64);

    const std::vector<float> result = values.getData();
    ASSERT_EQ(result.size(), static_cast<std::size_t>(kCount));
    for (int i = 0; i < kCount; ++i)
        EXPECT_FLOAT_EQ(result[static_cast<std::size_t>(i)], static_cast<float>(i) * 2.0f)
            << "element " << i;
}

TEST_F(ComputeTest, AVectorBufferRoundTripsThroughTheTypedView)
{
    // MOD-1520's own acceptance case: StorageBufferT<Vector4> with 1024 elements.
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    constexpr std::size_t kCount = 1024;
    StorageBufferT<Vector4> buffer(gd, kCount);
    EXPECT_EQ(buffer.getElementCount(), kCount);
    EXPECT_EQ(buffer.getBuffer().getByteSize(), kCount * sizeof(Vector4));

    std::vector<Vector4> source;
    source.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i)
    {
        const auto f = static_cast<float>(i);
        source.emplace_back(f, f + 0.5f, -f, 1.0f);
    }
    buffer.setData(source);
    EXPECT_EQ(buffer.getData(), source);

    std::vector<Vector4> tooMany(kCount + 1);
    EXPECT_THROW(buffer.setData(tooMany), std::invalid_argument);
}

TEST_F(ComputeTest, ImageBindingEitherWorksOrRefusesWithItsReason)
{
    // MOD-1514/MOD-1504, written to assert something on both kinds of context rather than to skip
    // on one. GL ES 3.1 requires an immutable texture for glBindImageTexture and CNA allocates its
    // textures mutably, so an ES context with full compute support cannot bind one -- and rather
    // than let the driver reject the binding silently, the wrapper refuses it and says why. On
    // desktop GL the same code binds and the gradient below is asserted exactly.
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    constexpr int kSize = 16;
    Texture2D texture(gd, kSize, kSize);
    const std::vector<Color> initial(kSize * kSize, Color::Black);
    texture.SetData(initial.data(), static_cast<int>(initial.size()));

    ComputeShader painter(gd, R"(#version 310 es
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba8, binding = 0) writeonly uniform highp image2D uOutput;
uniform int uSize;
void main() {
    ivec2 at = ivec2(gl_GlobalInvocationID.xy);
    if (at.x >= uSize || at.y >= uSize) return;
    imageStore(uOutput, at, vec4(float(at.x) / float(uSize - 1),
                                 float(at.y) / float(uSize - 1), 0.0, 1.0));
}
)");

    if (!painter.isImageBindingSupported())
    {
        try
        {
            painter.bindImage(0, texture, GraphicsImageAccess::WriteOnly);
            FAIL() << "an image binding this renderer cannot honour was accepted";
        }
        catch (const System::NotSupportedException& error)
        {
            const std::string message = error.what();
            EXPECT_NE(message.find("immutable"), std::string::npos) << message;
            EXPECT_NE(message.find("StorageBuffer"), std::string::npos)
                << "the refusal does not say what to do instead: " << message;
        }
        return;
    }

    painter.bindImage(0, texture, GraphicsImageAccess::WriteOnly);
    painter.setUniform("uSize", kSize);
    painter.dispatch(kSize / 8, kSize / 8);
    painter.barrier(GraphicsMemoryBarrier::ShaderImageAccess);

    // Read back through a second dispatch, not through Texture2D::GetData: that answers from the
    // CPU shadow copy the texture was uploaded with, which a GPU-side write never touches.
    StorageBufferT<float> readBack(gd, static_cast<std::size_t>(kSize) * kSize * 4);
    ComputeShader reader(gd, R"(#version 310 es
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba8, binding = 0) readonly uniform highp image2D uInput;
layout(std430, binding = 1) buffer Output { float texels[]; };
uniform int uSize;
void main() {
    ivec2 at = ivec2(gl_GlobalInvocationID.xy);
    if (at.x >= uSize || at.y >= uSize) return;
    vec4 texel = imageLoad(uInput, at);
    int base = (at.y * uSize + at.x) * 4;
    texels[base + 0] = texel.r;
    texels[base + 1] = texel.g;
    texels[base + 2] = texel.b;
    texels[base + 3] = texel.a;
}
)");
    reader.bindImage(0, texture, GraphicsImageAccess::ReadOnly);
    reader.bindStorageBuffer(1, readBack.getBuffer());
    reader.setUniform("uSize", kSize);
    reader.dispatch(kSize / 8, kSize / 8);

    const std::vector<float> texels = readBack.getData();
    ASSERT_EQ(texels.size(), static_cast<std::size_t>(kSize) * kSize * 4);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
        {
            const std::size_t base = (static_cast<std::size_t>(y) * kSize + x) * 4;
            const float expectedR = static_cast<float>(x) / static_cast<float>(kSize - 1);
            const float expectedG = static_cast<float>(y) / static_cast<float>(kSize - 1);
            // Within one 8-bit step: the image is RGBA8, so the float the shader wrote was
            // quantised on the way in and dequantised on the way out.
            EXPECT_NEAR(texels[base + 0], expectedR, 1.0f / 255.0f) << "at " << x << "," << y;
            EXPECT_NEAR(texels[base + 1], expectedG, 1.0f / 255.0f) << "at " << x << "," << y;
            EXPECT_NEAR(texels[base + 2], 0.0f, 1.0f / 255.0f) << "at " << x << "," << y;
            EXPECT_NEAR(texels[base + 3], 1.0f, 1.0f / 255.0f) << "at " << x << "," << y;
        }
}

TEST_F(ComputeTest, Texture2DGetDataDoesNotSeeComputeWrites)
{
    // The other half of the limitation above, and the reason a compute pass on CNA routes its
    // output through a storage buffer: Texture2D::GetData answers from the CPU pixels the texture
    // was uploaded with. If CNA ever gives Texture2D a real GPU read-back this test fails, and the
    // note beside it has to be rewritten.
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    constexpr int kSize = 8;
    Texture2D texture(gd, kSize, kSize);
    const std::vector<Color> initial(kSize * kSize, Color::Black);
    texture.SetData(initial.data(), static_cast<int>(initial.size()));

    ComputeShader painter(gd, R"(#version 310 es
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba8, binding = 0) writeonly uniform highp image2D uOutput;
void main() {
    imageStore(uOutput, ivec2(gl_GlobalInvocationID.xy), vec4(1.0, 1.0, 1.0, 1.0));
}
)");
    if (!painter.isImageBindingSupported())
        GTEST_SKIP() << "this renderer cannot bind an image at all; the point does not arise";
    painter.bindImage(0, texture, GraphicsImageAccess::WriteOnly);
    painter.dispatch(1, 1);
    painter.barrier(GraphicsMemoryBarrier::TextureFetch);

    std::vector<Color> pixels(kSize * kSize, Color::White);
    texture.GetData(pixels.data(), static_cast<int>(pixels.size()));
    EXPECT_EQ(pixels[0], Color::Black)
        << "Texture2D::GetData now reflects GPU writes; the note on the test above is out of date";
}

TEST_F(ComputeTest, UniformsReachTheProgram)
{
    // MOD-1515: an int and a float, checked by their effect on a buffer rather than by asking the
    // program back -- what matters is that the value the shader read is the value that was set.
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    StorageBufferT<float> values(gd, 64);
    values.setData(std::vector<float>(64, 1.0f));

    ComputeShader scaler(gd, R"(#version 310 es
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Values { float values[]; };
uniform int uOffset;
uniform float uScale;
void main() {
    uint index = gl_GlobalInvocationID.x;
    values[index] = values[index] * uScale + float(uOffset);
}
)");
    scaler.bindStorageBuffer(0, values.getBuffer());
    scaler.setUniform("uOffset", 7);
    scaler.setUniform("uScale", 3.0f);
    scaler.dispatch(1);

    for (const float value : values.getData())
        EXPECT_FLOAT_EQ(value, 10.0f);
}

TEST_F(ComputeTest, DispatchArgumentsAreValidatedBeforeSubmission)
{
    // MOD-1523.
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    StorageBufferT<float> values(gd, 64);
    ComputeShader doubler(gd, kDoubler);
    doubler.bindStorageBuffer(0, values.getBuffer());

    EXPECT_THROW(doubler.dispatch(0), std::invalid_argument);
    EXPECT_THROW(doubler.dispatch(1, -1), std::invalid_argument);
    EXPECT_THROW(doubler.dispatch(1, 1, 0), std::invalid_argument);
    EXPECT_THROW(doubler.bindStorageBuffer(-1, values.getBuffer()), std::invalid_argument);

    const int limit = gd.GetMaxComputeWorkGroupCountEXT(0);
    ASSERT_GT(limit, 0);
    try
    {
        doubler.dispatch(limit + 1);
        FAIL() << "a dispatch past the device limit was submitted";
    }
    catch (const std::invalid_argument& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("axis x"), std::string::npos) << message;
        EXPECT_NE(message.find(std::to_string(limit)), std::string::npos) << message;
    }
}

#endif // CNA_CNAEXT
