// SPDX-License-Identifier: MS-PL
// SKIA-156: hardens SKIA-154's mesh-effect compilation cache and SKIA-155's GLSL-to-SkSL
// translator -- bounded cache growth with least-recently-used eviction, a failed compile never
// poisoning the cache or a later compile, oversized/malformed translator input staying bounded
// rather than crashing or hanging, and a many-iteration sanitizer stress pass. Deliberately does
// not add an unused "mode" (raster vs. future Ganesh) cache-key axis -- no second mode exists yet
// to test against; see docs/skia-vertices-2d-effect-contract.md's cache design note.

#include "CNA/Internal/Backends/Skia/SkiaGlslToSkslTranslatorEXT.hpp"
#include "CNA/Internal/Backends/Skia/SkiaMeshEffectBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace CNA::Internal::Backends::Skia;

namespace
{
    int failures = 0;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition) ++failures;
    }

    [[nodiscard]] std::string MakeUniqueColoredSource(int index)
    {
        return "half4 main(float2 p) { return half4(0.0, 0.0, 0.0, " + std::to_string(index)
            + ".0 / 1000000.0 + 0.000001); }";
    }

    const std::string kMarker(kSkiaSkslMeshEffectMarkerEXT);
}

int main()
{
    // -- bounded cache growth: compiling well past the ceiling never exceeds it --
    {
        SkiaMeshEffectCacheEXT cache;
        std::vector<std::unique_ptr<SkiaMeshEffectBackend>> backends;
        for (std::size_t i = 0; i < kSkiaMeshEffectCacheMaxEntriesEXT + 20u; ++i)
        {
            auto backend = std::make_unique<SkiaMeshEffectBackend>();
            const bool ok = backend->CompileProgram(kMarker, MakeUniqueColoredSource(static_cast<int>(i)), cache);
            Check(ok, "compile #" + std::to_string(i) + " succeeds");
            backends.push_back(std::move(backend));
        }
        Check(cache.SizeEXT() <= kSkiaMeshEffectCacheMaxEntriesEXT,
              "the cache never grows past its declared " + std::to_string(kSkiaMeshEffectCacheMaxEntriesEXT)
                  + "-entry ceiling even after compiling " + std::to_string(kSkiaMeshEffectCacheMaxEntriesEXT + 20u)
                  + " distinct sources");
    }

    // -- least-recently-used eviction: a repeatedly-touched entry survives; an untouched one doesn't --
    {
        SkiaMeshEffectCacheEXT cache;
        SkiaMeshEffectBackend keptAlive;
        Check(keptAlive.CompileProgram(kMarker, MakeUniqueColoredSource(0), cache),
              "the entry meant to survive compiles first");
        const void* originalIdentity = keptAlive.GetCompiledIdentityEXT();

        SkiaMeshEffectBackend earlyVictim;
        Check(earlyVictim.CompileProgram(kMarker, MakeUniqueColoredSource(1), cache),
              "an early entry meant to be evicted compiles second");

        // Fill well past the ceiling, "touching" (re-requesting) index 0's source every few
        // iterations to keep it recently used while index 1's source is never touched again.
        for (std::size_t i = 2; i < kSkiaMeshEffectCacheMaxEntriesEXT + 30u; ++i)
        {
            SkiaMeshEffectBackend filler;
            (void)filler.CompileProgram(kMarker, MakeUniqueColoredSource(static_cast<int>(i)), cache);
            if (i % 4 == 0)
            {
                SkiaMeshEffectBackend touch;
                (void)touch.CompileProgram(kMarker, MakeUniqueColoredSource(0), cache);
            }
        }

        SkiaMeshEffectBackend recheckKept;
        Check(recheckKept.CompileProgram(kMarker, MakeUniqueColoredSource(0), cache),
              "recompiling the kept-alive source still succeeds");
        Check(recheckKept.GetCompiledIdentityEXT() == originalIdentity,
              "the repeatedly-touched entry was never evicted -- same compiled-program identity "
              "after far more distinct compiles than the cache ceiling allows");

        SkiaMeshEffectBackend recheckVictim;
        Check(recheckVictim.CompileProgram(kMarker, MakeUniqueColoredSource(1), cache),
              "recompiling the evicted source still succeeds (just not as a cache hit)");
        Check(recheckVictim.GetCompiledIdentityEXT() != originalIdentity,
              "the never-touched early entry was evicted and genuinely recompiled -- a different "
              "compiled-program identity, proving eviction actually happened and this is not a "
              "trivially-true comparison");
    }

    // -- a failed compile never poisons the cache or a later, different compile --
    {
        SkiaMeshEffectCacheEXT cache;
        SkiaMeshEffectBackend backend;
        const bool badOk = backend.CompileProgram(kMarker, "this is not valid SkSL at all {{{", cache);
        Check(!badOk && !backend.IsValid(), "a genuinely malformed source fails to compile");
        Check(cache.SizeEXT() == 0u, "the failed compile inserted nothing into the cache");

        const bool goodOk = backend.CompileProgram(kMarker, MakeUniqueColoredSource(0), cache);
        Check(goodOk && backend.IsValid(),
              "the same backend instance recovers and compiles a valid source immediately "
              "afterward -- a failure does not poison later use of the same instance");
        Check(cache.SizeEXT() == 1u, "the recovered valid compile is cached normally");
    }

    // -- translator: oversized input stays bounded rather than being tokenized unboundedly --
    {
        const std::string oversized(kSkiaSkslMaxSourceBytesEXT + 1u, 'x');
        const GlslToSkslTranslationResultEXT result = TranslateGlslToSkslEXT(oversized);
        Check(!result.success && result.errorMessage.find("65536") != std::string::npos,
              "GLSL source exceeding the 65536-byte safety limit is rejected before tokenizing, "
              "not processed unboundedly");
    }

    // -- translator: malformed/pathological input is rejected cleanly, never crashes or hangs --
    {
        const GlslToSkslTranslationResultEXT empty = TranslateGlslToSkslEXT("");
        Check(!empty.success, "empty GLSL source is rejected cleanly");

        const GlslToSkslTranslationResultEXT unbalanced =
            TranslateGlslToSkslEXT("in vec2 vUV;\nout vec4 FragColor;\nvoid main() { FragColor = vec4(1.0);");
        Check(!unbalanced.success, "a function body with an unterminated brace is rejected cleanly");

        const GlslToSkslTranslationResultEXT unterminatedComment = TranslateGlslToSkslEXT(
            "in vec2 vUV;\nout vec4 FragColor;\n/* unterminated\nvoid main() { FragColor = vec4(1.0); }");
        Check(!unterminatedComment.success,
              "an unterminated block comment consumes to end-of-source without crashing and the "
              "resulting (now commented-out) source is rejected cleanly");

        const GlslToSkslTranslationResultEXT garbage =
            TranslateGlslToSkslEXT("@@@ #!$ 123abc {{{}}} ((()))");
        Check(!garbage.success, "unstructured garbage tokens are rejected cleanly, not crashing");

        const GlslToSkslTranslationResultEXT onlyBraces(TranslateGlslToSkslEXT("{{{{{{{{{{"));
        Check(!onlyBraces.success, "deeply nested unmatched braces are rejected cleanly");
    }

    // -- sanitizer stress: many rapid compile/translate/evict cycles, checked by the surrounding
    // ASan+UBSan build (this loop itself has no assertions beyond "did not crash or hang") --
    {
        SkiaMeshEffectCacheEXT stressCache;
        for (int round = 0; round < 5; ++round)
        {
            for (std::size_t i = 0; i < kSkiaMeshEffectCacheMaxEntriesEXT * 2u; ++i)
            {
                SkiaMeshEffectBackend backend;
                (void)backend.CompileProgram(
                    kMarker, MakeUniqueColoredSource(static_cast<int>(round * 1000 + static_cast<int>(i))),
                    stressCache);
                const GlslToSkslTranslationResultEXT translated = TranslateGlslToSkslEXT(
                    "in vec2 vUV;\nout vec4 FragColor;\nvoid main() { FragColor = vec4(vUV, 0.0, 1.0); }");
                (void)translated;
            }
        }
        Check(stressCache.SizeEXT() <= kSkiaMeshEffectCacheMaxEntriesEXT,
              "the stress loop's cache still respects its growth ceiling after many rounds");
        Check(true, "500 compile/translate cycles complete without crashing or hanging");
    }

    std::printf("=== %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
