// SPDX-License-Identifier: MS-PL
// Headless renderer: closes the remainder of HEADLESS-40 (trace log coverage for the last
// untraced state-toggle/Clear-variant methods and the two remaining untraced Create* factories)
// and HEADLESS-43 (trace-log comparison tooling, previously flagged aspirational/unimplemented).
//
// Needs no window, no Game, no GraphicsDeviceManager at all -- HeadlessRenderer and the
// new CompareTraceLogs()/FormatTraceLogDiff() free functions are fully self-contained, so this is
// a plain standalone main(), matching how HEADLESS-5's env-var parsing check needed no game loop
// either.
//
// Check 1 -- ClearDepth/ClearStencil/ClearDepthAndStencil/SetDepthTestEnabled/
//   SetDepthWriteEnabled all now appear in FormatTraceLog()'s output after a HeadlessTrace-mode
//   run (previously only Clear/Present/draws/resource creation/state-blend-depth-rasterizer-
//   sampler Apply* calls were logged).
// Check 2 -- CreateSpriteBatch and CreateOcclusionQuery now also appear (the only two Create*
//   paths that were still silently untraced).
// Check 3 -- CompareTraceLogs() reports two logs built from an identical call sequence as
//   identical.
// Check 4 -- CompareTraceLogs() reports the correct diverging index when one call in the middle
//   of an otherwise-identical sequence differs (a different SetViewport size). This also caught a
//   real pre-existing bug: SetViewport itself had never actually been wired into RecordTrace()
//   despite an earlier commit message claiming it was (only SetScissorRect was) -- fixed here.
// Check 5 -- FormatTraceLogDiff() renders a human-readable divergence report containing "diverge"
//   for the pair from Check 4.
// Check 6 -- CompareTraceLogs() reports non-identical when one log is a strict prefix of the
//   other (same calls, but one run stopped early) -- divergence index is the length of the
//   shorter log.
// Check 7 -- FormatTraceLogDiff() renders a one-line "identical" summary for a matching pair.
// Check 8 -- virtual-resolution, presentation-mode, blend-factor and reference-stencil changes
//   have structured trace entries.
// Check 9 -- direct 2D/cube/MRT binds and an unbind have type, face, size and count details.
// Check 10 -- an instanced draw is identified as such and records primitive/instance counts.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdint>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Headless;

namespace
{
    int g_passCount = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++g_passCount;
    }

    bool HasTrace(const HeadlessRenderer& renderer, const char* method, const char* args)
    {
        for (const HeadlessTraceEntry& entry : renderer.TraceLog())
        {
            if (entry.method == method && entry.argsSummary == args)
                return true;
        }
        return false;
    }

    // A small, deterministic sequence exercising the newly-traced methods plus a couple of
    // already-traced ones, so the resulting log is non-trivial for the diff checks below.
    void RunDeterministicSequence(HeadlessRenderer& renderer, int viewportSize)
    {
        renderer.SetMode(HeadlessMode::Trace);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.SetViewport(0, 0, viewportSize, viewportSize, 0.0f, 1.0f);
        renderer.ClearDepth(1.0f);
        renderer.ClearStencil(0);
        renderer.ClearDepthAndStencil(1.0f, 0);
        renderer.SetDepthTestEnabled(true);
        renderer.SetDepthWriteEnabled(false);
        renderer.Present();
    }
}

int main()
{
    // Checks A/B: the previously-untraced methods now show up in FormatTraceLog().
    {
        HeadlessRenderer renderer(64, 64);
        RunDeterministicSequence(renderer, 64);
        {
            auto sb = renderer.CreateSpriteBatch();
            auto oq = renderer.CreateOcclusionQuery();
        }
        const std::string formatted = renderer.FormatTraceLog();
        Check(formatted.find("ClearDepth") != std::string::npos &&
              formatted.find("ClearStencil") != std::string::npos &&
              formatted.find("ClearDepthAndStencil") != std::string::npos &&
              formatted.find("SetDepthTestEnabled") != std::string::npos &&
              formatted.find("SetDepthWriteEnabled") != std::string::npos,
              "HeadlessTrace log now records ClearDepth/ClearStencil/ClearDepthAndStencil/"
              "SetDepthTestEnabled/SetDepthWriteEnabled");
        Check(formatted.find("CreateSpriteBatch") != std::string::npos &&
              formatted.find("CreateOcclusionQuery") != std::string::npos,
              "HeadlessTrace log now records CreateSpriteBatch/CreateOcclusionQuery");
    }

    // Check C: identical sequences -> identical logs.
    {
        HeadlessRenderer rendererA(64, 64);
        RunDeterministicSequence(rendererA, 64);
        HeadlessRenderer rendererB(64, 64);
        RunDeterministicSequence(rendererB, 64);

        const HeadlessTraceLogDiff diff = CompareTraceLogs(rendererA.TraceLog(), rendererB.TraceLog());
        Check(diff.identical, "CompareTraceLogs() reports two identical call sequences as identical");
    }

    // Check D: a genuine mid-sequence divergence (different SetViewport size) is located
    // precisely -- SetViewport is call index 1 (after Clear at index 0).
    {
        HeadlessRenderer rendererA(64, 64);
        RunDeterministicSequence(rendererA, 64);
        HeadlessRenderer rendererC(64, 64);
        RunDeterministicSequence(rendererC, 32);

        const HeadlessTraceLogDiff diff = CompareTraceLogs(rendererA.TraceLog(), rendererC.TraceLog());
        Check(!diff.identical && diff.firstDivergingIndex == 1,
              "CompareTraceLogs() locates a mid-sequence divergence at the correct index");

        const std::string formatted = FormatTraceLogDiff(rendererA.TraceLog(), rendererC.TraceLog());
        Check(formatted.find("diverge") != std::string::npos &&
              formatted.find("SetViewport") != std::string::npos,
              "FormatTraceLogDiff() renders a human-readable report naming the diverging call");
    }

    // Check E: one log is a strict prefix of the other (a run that stopped early).
    {
        HeadlessRenderer rendererA(64, 64);
        RunDeterministicSequence(rendererA, 64);

        HeadlessRenderer rendererD(64, 64);
        rendererD.SetMode(HeadlessMode::Trace);
        rendererD.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        rendererD.SetViewport(0, 0, 64, 64, 0.0f, 1.0f);
        // Stops here -- no ClearDepth/ClearStencil/.../Present.

        const HeadlessTraceLogDiff diff = CompareTraceLogs(rendererA.TraceLog(), rendererD.TraceLog());
        Check(!diff.identical && diff.firstDivergingIndex == rendererD.TraceLog().size(),
              "CompareTraceLogs() reports a prefix-only log as diverging at its own length");
    }

    // Check F: FormatTraceLogDiff()'s one-line summary for a genuinely identical pair.
    {
        HeadlessRenderer rendererA(64, 64);
        RunDeterministicSequence(rendererA, 64);
        HeadlessRenderer rendererB(64, 64);
        RunDeterministicSequence(rendererB, 64);

        const std::string formatted = FormatTraceLogDiff(rendererA.TraceLog(), rendererB.TraceLog());
        Check(formatted.find("identical") != std::string::npos,
              "FormatTraceLogDiff() renders a one-line 'identical' summary for matching logs");
    }


    // Check G: independently mutable device properties are no longer invisible in Trace mode.
    {
        HeadlessRenderer renderer(64, 64);
        renderer.SetMode(HeadlessMode::Trace);
        renderer.SetVirtualResolution(80, 60);
        renderer.SetPresentationMode(2);
        renderer.SetBlendFactor(0.25f, 0.5f, 0.75f, 1.0f);
        renderer.SetReferenceStencil(7);
        Check(HasTrace(renderer, "SetVirtualResolution", "size=80x60") &&
                  HasTrace(renderer, "SetPresentationMode", "mode=2") &&
                  HasTrace(renderer, "SetBlendFactor", "rgba=0.25,0.5,0.75,1") &&
                  HasTrace(renderer, "SetReferenceStencil", "value=7"),
              "HeadlessTrace records virtual resolution, presentation, blend factor and stencil reference");
    }

    // Check H: target changes preserve enough information to diagnose attachment-order bugs.
    {
        HeadlessRenderer renderer(64, 64);
        renderer.SetMode(HeadlessMode::Trace);
        auto target2D = renderer.CreateRenderTarget2D(16, 16, 0);
        auto targetCube = renderer.CreateRenderTargetCube(16, 0);
        renderer.SetRenderTarget2D(target2D.get());
        renderer.SetRenderTargetCubeFace(targetCube.get(), 4);
        const CNA::Internal::Renderers::RenderTargetBindingDescriptor bindings[] = {
            CNA::Internal::Renderers::RenderTargetBindingDescriptor::ForRenderTarget2D(
                target2D.get(), 0, 16, 16, 0),
            CNA::Internal::Renderers::RenderTargetBindingDescriptor::ForRenderTargetCubeFace(
                targetCube.get(), 2, 16, 0),
        };
        renderer.SetRenderTargets(bindings, 2);
        renderer.SetRenderTargets(nullptr, 0);
        Check(HasTrace(renderer, "SetRenderTarget2D", "target=2D size=16x16") &&
                  HasTrace(renderer, "SetRenderTargetCubeFace", "target=cube face=4 size=16x16") &&
                  HasTrace(renderer, "SetRenderTargets",
                           "count=2 [0:2D 16x16] [1:cube face=2 16x16]") &&
                  HasTrace(renderer, "SetRenderTargets", "count=0"),
              "HeadlessTrace records 2D, cube-face, MRT and backbuffer target transitions");
    }

    // Check I: instanced work has its own trace identity rather than masquerading as an indexed draw.
    {
        HeadlessRenderer renderer(64, 64);
        renderer.SetMode(HeadlessMode::Trace);
        auto vertices = renderer.CreateVertexBuffer(3);
        auto indices = renderer.CreateIndexBuffer16(3);
        const float vertexData[9] = {};
        const std::uint16_t indexData[3] = {0, 1, 2};
        vertices->SetData(vertexData, 3, sizeof(float) * 3);
        indices->SetData16(indexData, 3);
        const CNA::Internal::Renderers::GpuDrawParams params{};
        renderer.DrawInstancedPrimitivesEx(
            *vertices, *indices, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
            Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, 3, params);
        Check(HasTrace(renderer, "DrawInstancedPrimitivesEx",
                       "primitiveCount=1 instanceCount=3") &&
                  !HasTrace(renderer, "DrawIndexedColoredPrimitives", "primitiveCount=1"),
              "HeadlessTrace identifies instanced draws and records both logical counts");
    }

    std::printf("=== %d/%d PASS ===\n", g_passCount, 10);
    return (g_passCount == 10) ? 0 : 1;
}
