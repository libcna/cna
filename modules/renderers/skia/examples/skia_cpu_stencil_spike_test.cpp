// SPDX-License-Identifier: MS-PL
// SKIA-98: isolated CPU stencil-state feasibility spike.
//
// SKIA-97 selected a CPU-owned colour/depth raster bridge. This test extends only that model with
// the observable Depth24Stencil8 stencil contract used by EasyGL. It deliberately does not connect
// the model to public Draw*, advertise a depth capability, or claim format-accurate storage.

#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

using Microsoft::Xna::Framework::Graphics::ColorWriteChannels;
using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::StencilOperation;

namespace
{
    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }

    constexpr std::array<CompareFunction, 8> kCompareFunctions = {
        CompareFunction::Always, CompareFunction::Never, CompareFunction::Less,
        CompareFunction::LessEqual, CompareFunction::Equal, CompareFunction::GreaterEqual,
        CompareFunction::Greater, CompareFunction::NotEqual,
    };

    constexpr std::array<StencilOperation, 8> kStencilOperations = {
        StencilOperation::Keep, StencilOperation::Zero, StencilOperation::Replace,
        StencilOperation::Increment, StencilOperation::Decrement,
        StencilOperation::IncrementSaturation, StencilOperation::DecrementSaturation,
        StencilOperation::Invert,
    };

    constexpr std::array<std::uint8_t, 8> kMasks = {
        0x00u, 0x01u, 0x0fu, 0x33u, 0x55u, 0xaau, 0xf0u, 0xffu,
    };

    [[nodiscard]] bool Compare(CompareFunction function, float incoming, float stored)
    {
        switch (function)
        {
        case CompareFunction::Always:       return true;
        case CompareFunction::Never:        return false;
        case CompareFunction::Less:         return incoming < stored;
        case CompareFunction::LessEqual:    return incoming <= stored;
        case CompareFunction::Equal:        return incoming == stored;
        case CompareFunction::GreaterEqual: return incoming >= stored;
        case CompareFunction::Greater:      return incoming > stored;
        case CompareFunction::NotEqual:     return incoming != stored;
        }
        return false;
    }

    [[nodiscard]] std::uint8_t ApplyStencilOperation(
        StencilOperation operation, std::uint8_t stored, std::uint8_t reference)
    {
        switch (operation)
        {
        case StencilOperation::Keep:                return stored;
        case StencilOperation::Zero:                return 0u;
        case StencilOperation::Replace:             return reference;
        case StencilOperation::Increment:           return static_cast<std::uint8_t>(stored + 1u);
        case StencilOperation::Decrement:           return static_cast<std::uint8_t>(stored - 1u);
        case StencilOperation::IncrementSaturation: return stored == 0xffu ? 0xffu : stored + 1u;
        case StencilOperation::DecrementSaturation: return stored == 0u ? 0u : stored - 1u;
        case StencilOperation::Invert:              return static_cast<std::uint8_t>(~stored);
        }
        return stored;
    }

    [[nodiscard]] std::uint8_t MaskedStencilWrite(
        std::uint8_t stored, std::uint8_t result, std::uint8_t writeMask)
    {
        return static_cast<std::uint8_t>((stored & static_cast<std::uint8_t>(~writeMask)) |
                                         (result & writeMask));
    }

    struct FaceStencilState
    {
        CompareFunction function = CompareFunction::Always;
        StencilOperation fail = StencilOperation::Keep;
        StencilOperation depthFail = StencilOperation::Keep;
        StencilOperation pass = StencilOperation::Keep;
    };

    struct PipelineState
    {
        bool depthEnable = true;
        bool depthWriteEnable = true;
        CompareFunction depthFunction = CompareFunction::LessEqual;
        bool stencilEnable = false;
        bool twoSidedStencilMode = false;
        FaceStencilState front{};
        FaceStencilState counterClockwise{};
        std::uint8_t reference = 0u;
        std::uint8_t readMask = 0xffu;
        std::uint8_t writeMask = 0xffu;
        ColorWriteChannels colorWriteChannels = ColorWriteChannels::All;
    };

    struct Pixel
    {
        std::array<std::uint8_t, 4> color{};
        float depth = 1.0f;
        std::uint8_t stencil = 0u;
    };

    enum class FragmentResult
    {
        StencilFailed,
        DepthFailed,
        Passed,
    };

    void ApplyStencilWrite(Pixel& pixel, StencilOperation operation, const PipelineState& state)
    {
        const std::uint8_t result = ApplyStencilOperation(operation, pixel.stencil, state.reference);
        pixel.stencil = MaskedStencilWrite(pixel.stencil, result, state.writeMask);
    }

    [[nodiscard]] FragmentResult ProcessFragment(
        Pixel& pixel, float depth, const std::array<std::uint8_t, 4>& color,
        bool counterClockwiseFace, const PipelineState& state)
    {
        const FaceStencilState& face =
            state.twoSidedStencilMode && counterClockwiseFace
                ? state.counterClockwise
                : state.front;

        if (state.stencilEnable)
        {
            const std::uint8_t reference = state.reference & state.readMask;
            const std::uint8_t stored = pixel.stencil & state.readMask;
            if (!Compare(face.function, reference, stored))
            {
                ApplyStencilWrite(pixel, face.fail, state);
                return FragmentResult::StencilFailed;
            }
        }

        if (state.depthEnable && !Compare(state.depthFunction, depth, pixel.depth))
        {
            if (state.stencilEnable) ApplyStencilWrite(pixel, face.depthFail, state);
            return FragmentResult::DepthFailed;
        }

        if (state.stencilEnable) ApplyStencilWrite(pixel, face.pass, state);
        if (state.depthEnable && state.depthWriteEnable) pixel.depth = depth;

        const unsigned int writeMask = static_cast<unsigned int>(state.colorWriteChannels);
        for (unsigned int channel = 0; channel < color.size(); ++channel)
            if ((writeMask & (1u << channel)) != 0u) pixel.color[channel] = color[channel];
        return FragmentResult::Passed;
    }

    // Like glClear on EasyGL, clear values are target operations and ignore draw-state masks.
    void Clear(Pixel& pixel, const std::array<std::uint8_t, 4>& color, float depth,
               std::uint8_t stencil)
    {
        pixel.color = color;
        pixel.depth = depth;
        pixel.stencil = stencil;
    }

    [[nodiscard]] bool ExpectedCompare(
        CompareFunction function, std::uint8_t incoming, std::uint8_t stored)
    {
        if (function == CompareFunction::Always) return true;
        if (function == CompareFunction::Never) return false;
        if (function == CompareFunction::Less) return incoming < stored;
        if (function == CompareFunction::LessEqual) return incoming <= stored;
        if (function == CompareFunction::Equal) return incoming == stored;
        if (function == CompareFunction::GreaterEqual) return incoming >= stored;
        if (function == CompareFunction::Greater) return incoming > stored;
        return incoming != stored;
    }

    [[nodiscard]] std::uint8_t ExpectedOperation(
        StencilOperation operation, unsigned int stored, unsigned int reference)
    {
        constexpr unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
        if (operation == StencilOperation::Keep) return static_cast<std::uint8_t>(stored);
        if (operation == StencilOperation::Zero) return 0u;
        if (operation == StencilOperation::Replace) return static_cast<std::uint8_t>(reference);
        if (operation == StencilOperation::Increment)
            return static_cast<std::uint8_t>((stored + 1u) & maximum);
        if (operation == StencilOperation::Decrement)
            return static_cast<std::uint8_t>((stored + maximum) & maximum);
        if (operation == StencilOperation::IncrementSaturation)
            return static_cast<std::uint8_t>(stored < maximum ? stored + 1u : maximum);
        if (operation == StencilOperation::DecrementSaturation)
            return static_cast<std::uint8_t>(stored > 0u ? stored - 1u : 0u);
        return static_cast<std::uint8_t>(stored ^ maximum);
    }

    void TestPublicDefaults()
    {
        const DepthStencilState state;
        Check(state.getDepthBufferEnableProperty() && state.getDepthBufferWriteEnableProperty() &&
                  state.getDepthBufferFunctionProperty() == CompareFunction::LessEqual,
              "DepthStencilState default depth contract matches CPU model");
        Check(!state.getStencilEnableProperty() &&
                  state.getStencilFunctionProperty() == CompareFunction::Always &&
                  (state.getStencilMaskProperty() & 0xff) == 0xff &&
                  (state.getStencilWriteMaskProperty() & 0xff) == 0xff &&
                  state.getReferenceStencilProperty() == 0,
              "DepthStencilState default stencil values reduce to the 8-bit model");
        Check(!state.getTwoSidedStencilModeProperty() &&
                  state.getStencilFailProperty() == StencilOperation::Keep &&
                  state.getStencilDepthBufferFailProperty() == StencilOperation::Keep &&
                  state.getStencilPassProperty() == StencilOperation::Keep &&
                  state.getCounterClockwiseStencilFunctionProperty() == CompareFunction::Always &&
                  state.getCounterClockwiseStencilFailProperty() == StencilOperation::Keep &&
                  state.getCounterClockwiseStencilDepthBufferFailProperty() ==
                      StencilOperation::Keep &&
                  state.getCounterClockwiseStencilPassProperty() == StencilOperation::Keep,
              "DepthStencilState default face operations match CPU model");
    }

    void TestCompareMatrix()
    {
        bool matches = true;
        std::uint64_t cases = 0;
        for (const CompareFunction function : kCompareFunctions)
            for (const std::uint8_t mask : kMasks)
                for (unsigned int reference = 0; reference <= 0xffu; ++reference)
                    for (unsigned int stored = 0; stored <= 0xffu; ++stored)
                    {
                        const auto maskedReference = static_cast<std::uint8_t>(reference) & mask;
                        const auto maskedStored = static_cast<std::uint8_t>(stored) & mask;
                        matches = matches &&
                            Compare(function, maskedReference, maskedStored) ==
                                ExpectedCompare(function, maskedReference, maskedStored);
                        ++cases;
                    }
        Check(matches && cases == 4194304u,
              "all 8 compares match EasyGL reference-vs-stored semantics across 8-bit inputs/masks");
    }

    void TestOperationMatrix()
    {
        bool matches = true;
        std::uint64_t cases = 0;
        for (const StencilOperation operation : kStencilOperations)
            for (const std::uint8_t mask : kMasks)
                for (unsigned int stored = 0; stored <= 0xffu; ++stored)
                    for (unsigned int reference = 0; reference <= 0xffu; ++reference)
                    {
                        const std::uint8_t oldValue = static_cast<std::uint8_t>(stored);
                        const std::uint8_t result = ApplyStencilOperation(
                            operation, oldValue, static_cast<std::uint8_t>(reference));
                        const std::uint8_t actual = MaskedStencilWrite(oldValue, result, mask);
                        const std::uint8_t expectedResult =
                            ExpectedOperation(operation, stored, reference);
                        const std::uint8_t expected = static_cast<std::uint8_t>(
                            (oldValue & static_cast<std::uint8_t>(~mask)) |
                            (expectedResult & mask));
                        matches = matches && actual == expected;
                        ++cases;
                    }
        Check(matches && cases == 4194304u,
              "all 8 operations match EasyGL wrap/saturate/reference rules and write masks");
    }

    void TestBranchOrdering()
    {
        PipelineState state;
        state.stencilEnable = true;
        state.front.function = CompareFunction::Equal;
        state.front.fail = StencilOperation::Increment;
        state.front.depthFail = StencilOperation::Decrement;
        state.front.pass = StencilOperation::Invert;
        state.reference = 0x05u;

        Pixel stencilFail{{1u, 2u, 3u, 4u}, 0.5f, 0x04u};
        const auto failResult = ProcessFragment(
            stencilFail, 0.25f, {9u, 9u, 9u, 9u}, false, state);
        Check(failResult == FragmentResult::StencilFailed && stencilFail.stencil == 0x05u &&
                  stencilFail.depth == 0.5f && stencilFail.color ==
                      std::array<std::uint8_t, 4>{1u, 2u, 3u, 4u},
              "stencil-fail operation runs before depth and blocks depth/colour writes");

        Pixel depthFail{{1u, 2u, 3u, 4u}, 0.5f, 0x05u};
        const auto depthResult = ProcessFragment(
            depthFail, 0.75f, {9u, 9u, 9u, 9u}, false, state);
        Check(depthResult == FragmentResult::DepthFailed && depthFail.stencil == 0x04u &&
                  depthFail.depth == 0.5f && depthFail.color ==
                      std::array<std::uint8_t, 4>{1u, 2u, 3u, 4u},
              "depth-fail operation runs only after stencil pass and blocks depth/colour writes");

        Pixel pass{{1u, 2u, 3u, 4u}, 0.5f, 0x05u};
        const auto passResult = ProcessFragment(pass, 0.25f, {9u, 8u, 7u, 6u}, false, state);
        Check(passResult == FragmentResult::Passed && pass.stencil == 0xfau &&
                  pass.depth == 0.25f && pass.color ==
                      std::array<std::uint8_t, 4>{9u, 8u, 7u, 6u},
              "stencil-pass operation, depth write and colour write occur on full pass");
    }

    void TestMasksAndStencilDisable()
    {
        PipelineState state;
        state.stencilEnable = true;
        state.depthEnable = false;
        state.front.function = CompareFunction::Equal;
        state.front.pass = StencilOperation::Replace;
        state.reference = 0x01u;
        state.readMask = 0x01u;
        state.writeMask = 0x0fu;
        Pixel pixel{{0u, 0u, 0u, 0u}, 0.5f, 0xf5u};
        const auto result = ProcessFragment(pixel, 0.9f, {1u, 2u, 3u, 4u}, false, state);
        Check(result == FragmentResult::Passed && pixel.stencil == 0xf1u && pixel.depth == 0.5f,
              "read mask gates reference/stored compare and write mask preserves protected bits");

        state.stencilEnable = false;
        state.depthEnable = true;
        state.depthFunction = CompareFunction::Always;
        state.front.pass = StencilOperation::Zero;
        Pixel disabled{{0u, 0u, 0u, 0u}, 0.5f, 0xabu};
        const auto disabledResult =
            ProcessFragment(disabled, 0.25f, {1u, 2u, 3u, 4u}, false, state);
        Check(disabledResult == FragmentResult::Passed && disabled.stencil == 0xabu &&
                  disabled.depth == 0.25f,
              "disabled stencil bypasses compare and every stencil operation");
    }

    void TestTwoSidedState()
    {
        PipelineState state;
        state.stencilEnable = true;
        state.depthEnable = false;
        state.twoSidedStencilMode = true;
        state.reference = 0x05u;
        state.front.function = CompareFunction::Equal;
        state.front.pass = StencilOperation::Decrement;
        state.counterClockwise.function = CompareFunction::NotEqual;
        state.counterClockwise.fail = StencilOperation::Increment;

        Pixel twoSided{{}, 1.0f, 0x05u};
        const auto twoSidedResult = ProcessFragment(twoSided, 0.5f, {}, true, state);
        Check(twoSidedResult == FragmentResult::StencilFailed && twoSided.stencil == 0x06u,
              "two-sided counter-clockwise face selects its fail operation (EasyGL fixture 0x06)");

        state.twoSidedStencilMode = false;
        Pixel oneSided{{}, 1.0f, 0x05u};
        const auto oneSidedResult = ProcessFragment(oneSided, 0.5f, {}, true, state);
        Check(oneSidedResult == FragmentResult::Passed && oneSided.stencil == 0x04u,
              "one-sided mode applies front pass operation to the same face (EasyGL fixture 0x04)");
    }

    void TestColorWriteInteractionAndClear()
    {
        PipelineState state;
        state.stencilEnable = true;
        state.front.pass = StencilOperation::Increment;
        state.depthFunction = CompareFunction::Always;
        bool allMasksMatch = true;
        for (unsigned int mask = 0; mask <= 0x0fu; ++mask)
        {
            state.colorWriteChannels = static_cast<ColorWriteChannels>(mask);
            Pixel pixel{{10u, 20u, 30u, 40u}, 1.0f, 4u};
            const auto result =
                ProcessFragment(pixel, 0.25f, {50u, 60u, 70u, 80u}, false, state);
            const std::array<std::uint8_t, 4> expected = {
                static_cast<std::uint8_t>((mask & 1u) != 0u ? 50 : 10),
                static_cast<std::uint8_t>((mask & 2u) != 0u ? 60 : 20),
                static_cast<std::uint8_t>((mask & 4u) != 0u ? 70 : 30),
                static_cast<std::uint8_t>((mask & 8u) != 0u ? 80 : 40),
            };
            allMasksMatch = allMasksMatch && result == FragmentResult::Passed &&
                pixel.color == expected && pixel.depth == 0.25f && pixel.stencil == 5u;
        }
        Check(allMasksMatch,
              "all 16 colour-write masks leave depth/stencil writes independent");

        // Clear intentionally takes no PipelineState: EasyGL forces full write masks around clear.
        state.colorWriteChannels = ColorWriteChannels::None;
        state.writeMask = 0x00u;
        Pixel cleared{{10u, 20u, 30u, 40u}, 0.25f, 0xf0u};
        Clear(cleared, {1u, 2u, 3u, 4u}, 0.75f, 0x05u);
        Check(cleared.color == std::array<std::uint8_t, 4>{1u, 2u, 3u, 4u} &&
                  cleared.depth == 0.75f && cleared.stencil == 0x05u,
              "clear replaces all requested planes independently of draw-state masks");
    }
}

int main()
{
    TestPublicDefaults();
    TestCompareMatrix();
    TestOperationMatrix();
    TestBranchOrdering();
    TestMasksAndStencilDisable();
    TestTwoSidedState();
    TestColorWriteInteractionAndClear();
    return failures == 0 ? 0 : 1;
}
