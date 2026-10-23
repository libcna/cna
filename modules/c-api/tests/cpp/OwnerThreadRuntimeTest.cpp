// SPDX-License-Identifier: MS-PL
// CNA-DEMOS-ISSUE-050: driving, destroying and servicing dispatch belong to the owner.
// Never pump a forbidden call. A bounded wait detects accidental queuing; cancellation
// releases the worker for diagnostics but can never turn a queued call into a passing test.

#include <CNA/C/cna.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

namespace
{
    struct Operation
    {
        const char* name;
        CNA_Result (*call)(CNA_Handle);
    };

    CNA_Result RunFrame(const CNA_Handle game)
    {
        CNA_Bool running = CNA_TRUE;
        return cna_game_run_frame_ext(game, &running);
    }

    CNA_Result DisableDispatch(const CNA_Handle game)
    {
        return cna_game_set_foreign_thread_calls_ext(game, CNA_FALSE);
    }

    bool Check(const Operation& operation, const CNA_Bool dispatch)
    {
        CNA_GameCreateInfo info{};
        info.struct_size = sizeof(info);
        info.struct_version = 1;
        info.is_fixed_time_step = CNA_TRUE;
        info.target_elapsed_time_ticks = 166667;
        CNA_Handle game = CNA_INVALID_HANDLE;
        if (cna_game_create(&info, &game) != CNA_RESULT_SUCCESS)
        {
            std::fprintf(stderr, "%s: game creation failed\n", operation.name);
            return false;
        }
        if (cna_game_set_foreign_thread_calls_ext(game, dispatch) != CNA_RESULT_SUCCESS)
        {
            (void)cna_game_destroy(game);
            return false;
        }

        std::mutex mutex;
        std::condition_variable finished;
        bool done = false;
        CNA_Result result = CNA_RESULT_SUCCESS;
        CNA_Result diagnostic = CNA_RESULT_INTERNAL;
        CNA_ErrorInfo error{};
        error.struct_size = sizeof(error);
        error.struct_version = 1;
        std::thread worker([&]
        {
            result = operation.call(game);
            diagnostic = cna_error_get_last_info(&error);
            {
                const std::lock_guard lock(mutex);
                done = true;
            }
            finished.notify_one();
        });

        bool completed;
        {
            std::unique_lock lock(mutex);
            completed = finished.wait_for(lock, std::chrono::seconds(2), [&] { return done; });
        }
        const CNA_Result disabled = cna_game_set_foreign_thread_calls_ext(game, CNA_FALSE);
        worker.join();
        int64_t ticks = 0;
        const CNA_Result ownerRead = cna_game_get_target_elapsed_time_ticks(game, &ticks);
        const CNA_Result destroyed = cna_game_destroy(game);
        const bool passed = completed && result == CNA_RESULT_THREAD &&
            diagnostic == CNA_RESULT_SUCCESS && error.result == CNA_RESULT_THREAD &&
            error.category == CNA_ERROR_CATEGORY_THREAD && disabled == CNA_RESULT_SUCCESS &&
            ownerRead == CNA_RESULT_SUCCESS && ticks == 166667 && destroyed == CNA_RESULT_SUCCESS;
        std::printf("%s dispatch=%u: completed=%d result=%u category=%u owner=%u cleanup=%u %s\n",
            operation.name, static_cast<unsigned>(dispatch), completed ? 1 : 0,
            static_cast<unsigned>(result), static_cast<unsigned>(error.category),
            static_cast<unsigned>(ownerRead), static_cast<unsigned>(destroyed),
            passed ? "PASS" : "FAIL");
        return passed;
    }
}

int main()
{
    const Operation operations[] = {
        {"run", cna_game_run},
        {"run_one_frame", cna_game_run_one_frame},
        {"run_frame_ext", RunFrame},
        {"tick", cna_game_tick},
        {"destroy", cna_game_destroy},
        {"set_foreign_thread_calls", DisableDispatch},
        {"run_foreign_thread_calls", cna_game_run_foreign_thread_calls_ext},
    };
    int failures = 0;
    for (const auto& operation : operations)
    {
        failures += Check(operation, CNA_FALSE) ? 0 : 1;
        failures += Check(operation, CNA_TRUE) ? 0 : 1;
    }
    return failures == 0 ? 0 : 1;
}
