// SPDX-License-Identifier: MS-PL
// plans/plan_graphics_shared_cleanup.md GSC-0006: a Direct3D debug-layer message that D3DDebugLayerPolicy
// calls fatal fails the test that produced it.
//
// Installed only when validation is explicitly enabled for the test process -- CNA_D3D11_DEBUG_LAYER=1 --
// so an ordinary run is untouched. Every message the renderer drains goes through
// D3DCommon::D3DDebugLayerLog to this listener's observer; at the end
// of each test the listener drains every live device first (a device that outlives the test's last
// Present still has its messages attributed to it) and then adds a gtest failure listing the fatal
// messages. That failure is recorded while gtest still attributes results to the finished test, so the
// XML report, the shard runner and the exit code all see it.
//
// Report lines also go to stdout and to the file named by CNA_D3D_DEBUG_REPORT for the shard runner.

#if defined(_WIN32) && defined(CNA_RENDERER_DIRECTX11)

#include "D3DDebugLayerPolicy.hpp"

#include "CNA/Internal/Renderers/D3DCommon/D3DDebugLayerLog.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace
{
    using CNA::Internal::Renderers::D3DCommon::D3DDebugLayerLog;
    using CNA::Internal::Renderers::D3DCommon::D3DDebugLayerMessage;
    namespace Policy = CNA::Testing::D3DDebugLayer;

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
                gate_.Observe(message.severity, message.id, message.description);
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

    struct D3DDebugLayerListenerRegistrar
    {
        D3DDebugLayerListenerRegistrar()
        {
            if (!SwitchIsOn("CNA_D3D11_DEBUG_LAYER"))
                return;
            ::testing::UnitTest::GetInstance()->listeners().Append(new D3DDebugLayerListener());
        }
    };

    const D3DDebugLayerListenerRegistrar g_registrar;
}

#endif
