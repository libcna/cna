#pragma once

// plans/plan_runtimerenderer.md RTR-P9-1/P9-2: the conversion idiom for renderer-gated tests.
//
// The corpus has ~892 `#ifdef CNA_RENDERER_<X>` sites. That was the only mechanism available while
// a build could hold exactly one renderer, and it has two costs now that a build can hold several:
//
//   1. A compile-time gate can only ever describe ONE renderer -- in a multi-renderer build, the
//      default. A test that means "this is how SOFTWARE behaves" simply does not run when SOFTWARE
//      is compiled in but is not the default.
//   2. A gated test that is compiled OUT reports nothing at all. It does not appear as skipped, so
//      a renderer's coverage silently drops to zero without any signal.
//
// The runtime gates below fix both. They are for tests whose BODY is renderer-agnostic and merely
// needs the right renderer active. A test that genuinely needs renderer-specific headers or types
// still needs a compile-time guard -- on that family's own private macro, which in a multi-renderer
// build is exactly what its target has.
//
// Usage:
//
//     TEST(SoftwareRasterTest, RejectsPointLists)
//     {
//         CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Software);
//         ...
//     }

#include <gtest/gtest.h>

#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <initializer_list>
#include <string>

namespace CNA::Testing
{
    /**
     * @brief Whether the renderer CNA will use for the next device is @p type.
     *
     * Reads the SELECTION rather than the compile-time identity, so it stays correct in a
     * multi-renderer build and when a test has chosen a renderer for itself.
     *
     * @param type The renderer identity to test for.
     * @return true when that renderer is the one CNA will use.
     */
    [[nodiscard]] inline bool ActiveRendererIs(GraphicsRendererType type)
    {
        return GraphicsRendererSelection::GetSelected() == type;
    }

    /**
     * @brief Whether the active renderer is any one of @p types.
     *
     * @param types The renderer identities to test for.
     * @return true when the active renderer is one of them.
     */
    [[nodiscard]] inline bool ActiveRendererIsAnyOf(std::initializer_list<GraphicsRendererType> types)
    {
        for (const GraphicsRendererType type : types)
        {
            if (ActiveRendererIs(type))
                return true;
        }
        return false;
    }

    /**
     * @brief A skip message naming the renderer a test wanted and the one it got.
     *
     * Spelled out rather than left to GTEST_SKIP's default so a skipped run says which renderer's
     * coverage was not exercised -- the whole point of skipping at runtime instead of compiling out.
     *
     * @param expected The renderer the test requires.
     * @return The message.
     */
    [[nodiscard]] inline std::string SkipMessageFor(GraphicsRendererType expected)
    {
        return std::string("needs the ") + std::string(getGraphicsRendererName(expected)) +
               " renderer; the active renderer is " +
               std::string(getGraphicsRendererName(GraphicsRendererSelection::GetSelected()));
    }
}

/**
 * @brief Whether the active renderer is one of the named identities.
 *
 * The runtime form of `#if defined(CNA_RENDERER_A) || defined(CNA_RENDERER_B)`. Written as a macro
 * taking bare identity names so a converted guard reads almost exactly like the one it replaces:
 *
 *     #if defined(CNA_RENDERER_STUB)   ->   CNA_RENDERER_IS(Stub)
 *
 * Worth doing beyond tidiness: these guards select the EXPECTED OUTCOME of a test, not whether it
 * compiles. A compile-time guard bakes in the build default's expectation, so in a multi-renderer
 * build a test asserts the wrong renderer's contract as soon as another one is selected. Evaluated
 * at runtime, the same table asserts the right contract for whichever renderer is active.
 */
#define CNA_RENDERER_IS(...)                                                                     \
    (::CNA::Testing::ActiveRendererIsAnyOf({__VA_ARGS__}))

/** @brief Convenience for naming identities inside CNA_RENDERER_IS without the enum prefix. */
namespace CNA::Testing::Renderers
{
    using enum ::CNA::GraphicsRendererType;
}

/** @brief Skips the current test unless @p type is the active renderer. */
#define CNA_SKIP_IF_RENDERER_IS_NOT(type)                                                        \
    do {                                                                                         \
        if (!::CNA::Testing::ActiveRendererIs(type))                                             \
            GTEST_SKIP() << ::CNA::Testing::SkipMessageFor(type);                                \
    } while (false)

/** @brief Skips the current test unless the active renderer is one of the listed identities. */
#define CNA_SKIP_IF_RENDERER_IS_NONE_OF(...)                                                     \
    do {                                                                                         \
        if (!::CNA::Testing::ActiveRendererIsAnyOf({__VA_ARGS__}))                               \
            GTEST_SKIP() << "needs one of the listed renderers; the active renderer is "         \
                         << ::CNA::getGraphicsRendererName(                                      \
                                ::CNA::GraphicsRendererSelection::GetSelected());                \
    } while (false)
