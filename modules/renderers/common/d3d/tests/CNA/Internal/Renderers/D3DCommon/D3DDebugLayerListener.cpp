// SPDX-License-Identifier: MS-PL
// plans/plan_graphics_shared_cleanup.md GSC-0006: a Direct3D debug-layer message that D3DDebugLayerPolicy
// calls fatal fails the test that produced it.
//
// Installed only when validation is explicitly enabled for the test process -- CNA_D3D11_DEBUG_LAYER=1,
// CNA_D3D12_DEBUG_LAYER=1 or CNA_D3D12_GPU_VALIDATION=1 -- so an ordinary run is untouched. Every message
// either renderer drains goes through D3DCommon::D3DDebugLayerLog to this listener's observer; at the end
// of each test the listener drains every live device first (a device that outlives the test's last
// Present still has its messages attributed to it) and then adds a gtest failure listing the fatal
// messages. That failure is recorded while gtest still attributes results to the finished test, so the
// XML report, the shard runner and the exit code all see it.
//
// Superseded DX12-0004's listener, which only printed the messages: a validation run could come back
// green with the debug layer reporting real API misuse. Report lines still go to stdout and to the file
// named by CNA_D3D_DEBUG_REPORT (or the older CNA_D3D12_DEBUG_REPORT) for the shard runner.
//
// The vectored exception handler remains for DirectX12, because of what DX12-0003 measured: an object
// released while queued GPU work still references it (ID 921) makes the layer raise exception 0x87D and the
// process dies with exit code 2173 and no output.

#if defined(_WIN32) && (defined(CNA_RENDERER_DIRECTX11) || defined(CNA_RENDERER_DIRECTX12))

#include "D3DDebugLayerPolicy.hpp"

#include "CNA/Internal/Renderers/D3DCommon/D3DDebugLayerLog.hpp"
#if defined(CNA_RENDERER_DIRECTX12)
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#endif

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include <windows.h>

namespace
{
    using CNA::Internal::Renderers::D3DCommon::D3DDebugLayerApi;
    using CNA::Internal::Renderers::D3DCommon::D3DDebugLayerLog;
    using CNA::Internal::Renderers::D3DCommon::D3DDebugLayerMessage;
    namespace Policy = CNA::Testing::D3DDebugLayer;

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
        const char* path = std::getenv("CNA_D3D_DEBUG_REPORT");
        if (path == nullptr || *path == '\0')
            path = std::getenv("CNA_D3D12_DEBUG_REPORT");
        if (path != nullptr && *path != '\0')
        {
            if (FILE* file = std::fopen(path, "a"))
            {
                std::fprintf(file, "%s\n", line.c_str());
                std::fclose(file);
            }
        }
    }

    class D3DDebugLayerListener final : public ::testing::EmptyTestEventListener
    {
    public:
        D3DDebugLayerListener()
        {
            D3DDebugLayerLog::SetObserver([this](const D3DDebugLayerMessage& message) {
                std::lock_guard<std::mutex> lock(mutex_);
                gate_.Observe(message.api == D3DDebugLayerApi::Direct3D11 ? Policy::Api::Direct3D11
                                                                          : Policy::Api::Direct3D12,
                              message.severity, message.id, message.description);
            });
        }

        ~D3DDebugLayerListener() override
        {
            // A renderer destroyed during static destruction must not call back into a freed listener.
            D3DDebugLayerLog::SetObserver({});
        }

        void OnTestStart(const ::testing::TestInfo& info) override
        {
            // Whatever a previous device left in its queue belongs to no test; take it out first.
            D3DDebugLayerLog::DrainLiveQueues();
            const std::string name = std::string(info.test_suite_name()) + "." + info.name();
            std::snprintf(g_currentTest, sizeof(g_currentTest), "%s", name.c_str());
            std::lock_guard<std::mutex> lock(mutex_);
            gate_.BeginTest(name);
        }

        void OnTestEnd(const ::testing::TestInfo&) override
        {
            D3DDebugLayerLog::DrainLiveQueues();
            Policy::Gate::Outcome outcome;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                outcome = gate_.EndTest();
            }
            std::snprintf(g_currentTest, sizeof(g_currentTest), "%s", "<between tests>");
            if (outcome.allowedCount != 0)
                Report("[D3D DEBUG LAYER] " + outcome.testName + ": " +
                       std::to_string(outcome.allowedCount) + " allowlisted warning(s)");
            if (outcome.fatalCount == 0)
                return;
            Report("[D3D DEBUG LAYER] " + outcome.testName + ": FAIL, " +
                   std::to_string(outcome.fatalCount) + " fatal message(s)");
            for (const std::string& line : outcome.fatal)
                Report("[D3D DEBUG LAYER]   " + outcome.testName + " " + line);
            // gtest still attributes results to the finished test while its end is being announced.
            ADD_FAILURE() << Policy::DescribeFatal(outcome);
        }

        void OnTestProgramEnd(const ::testing::UnitTest&) override
        {
            D3DDebugLayerLog::DrainLiveQueues();
            Policy::Gate::Outcome outside;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                outside = gate_.TakeOutside();
            }
            for (const std::string& line : outside.fatal)
                Report("[D3D DEBUG LAYER] outside any test: " + line);
            const auto totals = D3DDebugLayerLog::Totals();
            Report("[D3D DEBUG LAYER] process totals: corruption " + std::to_string(totals.corruption) +
                   ", error " + std::to_string(totals.error) + ", warning " +
                   std::to_string(totals.warning) + "; outside any test, fatal " +
                   std::to_string(outside.fatalCount));
        }

    private:
        std::mutex mutex_;
        Policy::Gate gate_;
    };

#if defined(CNA_RENDERER_DIRECTX12)
    LONG CALLBACK ReportFatalDebugLayerException(EXCEPTION_POINTERS* pointers)
    {
        constexpr DWORD kDebugLayerTermination = 0x87D;
        if (pointers->ExceptionRecord->ExceptionCode != kDebugLayerTermination)
            return EXCEPTION_CONTINUE_SEARCH;
        using CNA::Internal::Renderers::DirectX12::DirectX12Renderer;
        Report(std::string("[D3D DEBUG LAYER] FATAL exception 0x87D during ") + g_currentTest);
        for (const auto& message : DirectX12Renderer::PeekLiveDebugQueuesEXT())
            Report("[D3D DEBUG LAYER]   FATAL [severity " + std::to_string(message.severity) + ", id " +
                   std::to_string(message.id) + "] " + message.description);
        return EXCEPTION_CONTINUE_SEARCH;
    }
#endif

    struct D3DDebugLayerListenerRegistrar
    {
        D3DDebugLayerListenerRegistrar()
        {
            if (!SwitchIsOn("CNA_D3D11_DEBUG_LAYER") && !SwitchIsOn("CNA_D3D12_DEBUG_LAYER") &&
                !SwitchIsOn("CNA_D3D12_GPU_VALIDATION"))
                return;
            ::testing::UnitTest::GetInstance()->listeners().Append(new D3DDebugLayerListener());
#if defined(CNA_RENDERER_DIRECTX12)
            AddVectoredExceptionHandler(1, ReportFatalDebugLayerException);
#endif
        }
    };

    const D3DDebugLayerListenerRegistrar g_registrar;
}

#endif
