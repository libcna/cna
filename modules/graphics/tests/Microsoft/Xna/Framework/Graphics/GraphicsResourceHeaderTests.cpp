// SPDX-License-Identifier: MS-PL
// CBIND-133: GraphicsResource.hpp is complete for its own templates.
//
// GraphicsResource::WithRendererContext holds a std::unique_ptr<IRendererThreadContextLease>.
// libc++'s C++23 unique_ptr is constexpr, so its deleter is instantiated with the template, and
// the header used to forward-declare the lease: clang with libc++ (and so every Emscripten build)
// failed to compile it, while GCC's later instantiation hid the gap. This translation unit
// includes the header first and alone, and asks for what that instantiation needs.

#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"

#include <memory>
#include <type_traits>

#include <gtest/gtest.h>

namespace
{
    using Lease = CNA::Internal::Renderers::IRendererThreadContextLease;

    static_assert(sizeof(Lease) > 0, "GraphicsResource.hpp must define the lease it holds");
    static_assert(std::is_destructible_v<std::unique_ptr<Lease>>);
}

TEST(GraphicsResourceHeader, DefinesTheRendererContextLeaseItsTemplatesHold)
{
    std::unique_ptr<Lease> lease;
    EXPECT_EQ(lease, nullptr);
}
