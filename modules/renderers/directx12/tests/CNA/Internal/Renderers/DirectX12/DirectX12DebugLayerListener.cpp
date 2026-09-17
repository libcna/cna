// SPDX-License-Identifier: MS-PL
// plans/plan_directx12_parity.md DX12-0004: attribute every D3D12 debug-layer message to the test
// that produced it.
//
// Installed only when CNA_D3D12_DEBUG_LAYER=1 (or CNA_D3D12_GPU_VALIDATION=1) is set for the test
// process, so an ordinary run is untouched. A message cannot fail the test after it has ended, so the
// listener reports instead: one "[D3D12 DEBUG LAYER]" line per affected test on stdout and, when
// CNA_D3D12_DEBUG_REPORT names a file, the same lines appended there for the shard runner to collect.
//
// The vectored exception handler exists because of what DX12-0003 measured: an object released while
// queued GPU work still references it (ID 921) makes the layer raise exception 0x87D, and the process
// dies with exit code 2173 and no output. Printing the live queues at the raise is what names it.

#if defined(_WIN32) && defined(CNA_RENDERER_DIRECTX12)

#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <windows.h>

namespace
{
    using CNA::Internal::Renderers::DirectX12::D3D12DebugMessageTotalsEXT;
    using CNA::Internal::Renderers::DirectX12::DirectX12Renderer;

    char g_currentTest[512] = "<outside any test>";

    bool SwitchIsOn(const char* name)
    {
        const char* value = std::getenv(name);
        return value != nullptr && std::strcmp(value, "1") == 0;
    }

    void Report(const std::string& line)
    {
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
        if (const char* path = std::getenv("CNA_D3D12_DEBUG_REPORT"); path != nullptr && *path != '\0')
        {
            if (FILE* file = std::fopen(path, "a"))
            {
                std::fprintf(file, "%s\n", line.c_str());
                std::fclose(file);
            }
        }
    }

    class D3D12DebugLayerListener final : public ::testing::EmptyTestEventListener
    {
    public:
        void OnTestStart(const ::testing::TestInfo& info) override
        {
            std::snprintf(g_currentTest, sizeof(g_currentTest), "%s.%s", info.test_suite_name(),
                          info.name());
            before_ = DirectX12Renderer::GetProcessDebugMessageTotalsEXT();
        }

        void OnTestEnd(const ::testing::TestInfo& info) override
        {
            const D3D12DebugMessageTotalsEXT after = DirectX12Renderer::GetProcessDebugMessageTotalsEXT();
            const std::uint64_t corruption = after.corruption - before_.corruption;
            const std::uint64_t error = after.error - before_.error;
            const std::uint64_t warning = after.warning - before_.warning;
            const std::uint64_t total = corruption + error + warning;
            if (total != 0)
            {
                const std::string name = std::string(info.test_suite_name()) + "." + info.name();
                Report("[D3D12 DEBUG LAYER] " + name + ": corruption " + std::to_string(corruption) +
                       ", error " + std::to_string(error) + ", warning " + std::to_string(warning));
                const auto recent = DirectX12Renderer::GetRecentProcessDebugMessagesEXT();
                const std::size_t shown = total < recent.size() ? static_cast<std::size_t>(total) : recent.size();
                for (std::size_t i = recent.size() - shown; i < recent.size(); ++i)
                    Report("[D3D12 DEBUG LAYER]   " + name + " [severity " +
                           std::to_string(recent[i].severity) + ", id " + std::to_string(recent[i].id) +
                           "] " + recent[i].description);
            }
            std::snprintf(g_currentTest, sizeof(g_currentTest), "%s", "<between tests>");
        }

        void OnTestProgramEnd(const ::testing::UnitTest&) override
        {
            const D3D12DebugMessageTotalsEXT totals = DirectX12Renderer::GetProcessDebugMessageTotalsEXT();
            Report("[D3D12 DEBUG LAYER] process totals: corruption " + std::to_string(totals.corruption) +
                   ", error " + std::to_string(totals.error) + ", warning " +
                   std::to_string(totals.warning));
        }

    private:
        D3D12DebugMessageTotalsEXT before_{};
    };

    LONG CALLBACK ReportFatalDebugLayerException(EXCEPTION_POINTERS* pointers)
    {
        constexpr DWORD kDebugLayerTermination = 0x87D;
        if (pointers->ExceptionRecord->ExceptionCode != kDebugLayerTermination)
            return EXCEPTION_CONTINUE_SEARCH;
        Report(std::string("[D3D12 DEBUG LAYER] FATAL exception 0x87D during ") + g_currentTest);
        for (const auto& message : DirectX12Renderer::PeekLiveDebugQueuesEXT())
            Report("[D3D12 DEBUG LAYER]   FATAL [severity " + std::to_string(message.severity) + ", id " +
                   std::to_string(message.id) + "] " + message.description);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    struct D3D12DebugLayerListenerRegistrar
    {
        D3D12DebugLayerListenerRegistrar()
        {
            if (!SwitchIsOn("CNA_D3D12_DEBUG_LAYER") && !SwitchIsOn("CNA_D3D12_GPU_VALIDATION"))
                return;
            ::testing::UnitTest::GetInstance()->listeners().Append(new D3D12DebugLayerListener());
            AddVectoredExceptionHandler(1, ReportFatalDebugLayerException);
        }
    };

    const D3D12DebugLayerListenerRegistrar g_registrar;
}

#endif
