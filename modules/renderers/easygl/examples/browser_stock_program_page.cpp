// SPDX-License-Identifier: MS-PL
//
// CNA-ISLAND-002: runs the EasyGL stock-program GoogleTest suites (GlStockUniformBudgetTests.cpp,
// EasyGLStockProgramTests.cpp) inside a real browser WebGL 2 context, where Node -- which runs the
// wasm unit-test executables -- has none. The page first reports the context's own uniform limits,
// then prints one [PASS]/[FAIL] line per test and the `=== <passed>/<expected> PASS ===` summary
// scripts/run_browser_tests.mjs reads. A skipped test is not a pass, so a run on the wrong renderer
// or without a context cannot qualify.
//
//   tools/platform/run_gpu_tests_private.sh --exec node scripts/run_browser_tests.mjs \
//       cmake-build-wasm-webgl2 cna_browser_easygl_stock_program_page.html

#include <cstdio>

#include <emscripten.h>
#include <gtest/gtest.h>

#include "CNA/GraphicsRendererSelection.hpp"

EM_JS(void, CnaReportWebGl2UniformLimits, (), {
    const gl = document.createElement('canvas').getContext('webgl2');
    if (!gl) {
        console.log('[LIMITS] no WebGL 2 context');
        return;
    }
    const info = gl.getExtension('WEBGL_debug_renderer_info');
    console.log('[LIMITS] renderer=' +
                (info ? gl.getParameter(info.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER)) +
                ' MAX_VERTEX_UNIFORM_VECTORS=' + gl.getParameter(gl.MAX_VERTEX_UNIFORM_VECTORS) +
                ' MAX_FRAGMENT_UNIFORM_VECTORS=' + gl.getParameter(gl.MAX_FRAGMENT_UNIFORM_VECTORS));
});

namespace
{
    class PageReporter final : public ::testing::EmptyTestEventListener
    {
    public:
        void OnTestEnd(const ::testing::TestInfo& info) override
        {
            const ::testing::TestResult& result = *info.result();
            ++total_;
            const bool passed = result.Passed() && !result.Skipped();
            passed_ += passed ? 1 : 0;
            std::printf("[%s] %s.%s\n", passed ? "PASS" : "FAIL", info.test_suite_name(),
                        info.name());
            for (int i = 0; i < result.total_part_count(); ++i)
            {
                const ::testing::TestPartResult& part = result.GetTestPartResult(i);
                if (part.failed() || part.skipped())
                    std::printf("    %s:%d %s\n", part.file_name() ? part.file_name() : "?",
                                part.line_number(), part.message());
            }
        }

        void OnTestProgramEnd(const ::testing::UnitTest&) override
        {
            std::printf("=== %d/%d PASS ===\n", passed_, total_);
            std::fflush(stdout);
        }

    private:
        int total_ = 0;
        int passed_ = 0;
    };
}

int main(int argc, char** argv)
{
    CNA::GraphicsRendererSelection::SetPreferred("WEBGL2");
    CnaReportWebGl2UniformLimits();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::UnitTest::GetInstance()->listeners().Append(new PageReporter);
    return RUN_ALL_TESTS();
}
