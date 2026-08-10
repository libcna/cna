// SPDX-License-Identifier: MS-PL
// Task 883: ShaderEffect::Clone() — a CNAEXT CNA extension, not part of XNA's
// stock-effect set, gained a Clone() override as a mechanical consequence of
// Effect::Clone() becoming a pure virtual base-class contract. Unlike the
// other 7 concrete Effect subclasses, ShaderEffect deliberately recompiles a
// fresh renderer program from the same GLSL source rather than sharing the
// original's compiled program object (see ShaderEffect.hpp's Clone() doc
// comment for the ownership-model rationale). No prior test file exercised
// ShaderEffect at all before this task; source strings here are deliberately
// trivial/unvalidated GLSL — only Clone()'s structural contract (independent
// object, source strings preserved, no crash regardless of compile outcome)
// is in scope here, not shader compilation correctness.

#include <gtest/gtest.h>

#include <memory>

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;

TEST(ShaderEffectTest, CloneReturnsIndependentShaderEffectWithSameSource)
{
    GraphicsDevice gd;
    ShaderEffect fx(gd, "// vertex placeholder\n", "// fragment placeholder\n");

    std::unique_ptr<Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<ShaderEffect*>(cloned.get());

    ASSERT_NE(clone, nullptr);
    EXPECT_NE(static_cast<Effect*>(clone), static_cast<Effect*>(&fx));
    EXPECT_EQ(clone->getVertexSourceProperty(), fx.getVertexSourceProperty());
    EXPECT_EQ(clone->getFragmentSourceProperty(), fx.getFragmentSourceProperty());
    EXPECT_EQ(clone->IsEffectValid(), fx.IsEffectValid());
}
