// SPDX-License-Identifier: MS-PL
// Museum native C/C++ duplication, in the cna-engine UP-006 runtime family.
// No device/window/service is needed to observe two copies of process-wide state.
#include "CNA/Logger.hpp"
#include "CNA/AssemblyInfo.hpp"
#include "CNA/C/core_ext.h"
#include <cstdio>
#include <string_view>

namespace
{
const CNA::AssemblyTitleAttributeEXT title{"mixed C/C++ runtime"};
}

int main()
{
    int failures = 0;
    const auto check = [&failures](bool passed, const char* message) {
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", message);
        if (!passed) ++failures;
    };
    char titleBuffer[64]{};
    uint64_t titleBytes = 0;
    check(cna_assembly_copy_title_ext(titleBuffer, sizeof(titleBuffer), &titleBytes) ==
              CNA_RESULT_SUCCESS &&
          std::string_view(titleBuffer, titleBytes) == "mixed C/C++ runtime",
          "C reads the title declared by C++ static initialization (museum reproducer)");
    constexpr char cTitle[] = "title set through C";
    check(cna_assembly_set_title_ext(CNA_StringView{cTitle, sizeof(cTitle) - 1}) ==
              CNA_RESULT_SUCCESS && CNA::GetAssemblyTitleEXT() == cTitle,
          "C++ reads the title assigned through C");
    check(cna_logger_set_minimum_level(CNA_LOG_LEVEL_TRACE) == CNA_RESULT_SUCCESS,
          "C establishes a known initial level");
    CNA::Logger::SetMinimumLevel(CNA::LogLevel::WARN);
    CNA_LogLevel level = CNA_LOG_LEVEL_FATAL;
    check(cna_logger_get_minimum_level(&level) == CNA_RESULT_SUCCESS &&
          level == CNA_LOG_LEVEL_WARN, "C reads the C++ process-wide log level");
    check(cna_logger_set_minimum_level(CNA_LOG_LEVEL_ERROR) == CNA_RESULT_SUCCESS &&
          CNA::Logger::GetMinimumLevel() == CNA::LogLevel::ERROR,
          "C++ reads the C process-wide log level");

    int cCalls = 0;
    const auto callback = [](CNA_LogLevel, CNA_LogCategory, CNA_StringView, void* context) {
        ++*static_cast<int*>(context);
    };
    check(cna_logger_set_sink_ext(callback, &cCalls) == CNA_RESULT_SUCCESS,
          "C installs the process-wide sink");
    CNA::Logger::Error("mixed-runtime C++ to C sink");
    check(cCalls == 1, "C++ logging reaches the sink installed through C");
    check(cna_logger_reset_sink_ext() == CNA_RESULT_SUCCESS, "C releases its sink");

    int cppCalls = 0;
    CNA::Logger::SetSink([&cppCalls](CNA::LogLevel, CNA::LogCategory, std::string_view) {
        ++cppCalls;
    });
    constexpr char message[] = "mixed-runtime C to C++ sink";
    check(cna_logger_error(CNA_StringView{message, sizeof(message) - 1},
                          CNA_LOG_CATEGORY_APPLICATION) == CNA_RESULT_SUCCESS,
          "C logging succeeds");
    check(cppCalls == 1, "C logging reaches the sink installed through C++");
    CNA::Logger::ResetSink();
    return failures == 0 ? 0 : 1;
}
