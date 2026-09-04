// SPDX-License-Identifier: MS-PL

#include <filesystem>
#include <memory>
#include <vector>

#include "CNA/Content/Pipeline/ContentCompiler.hpp"

namespace Pipeline = CNA::Content::Pipeline;

namespace
{
    template<typename Character>
    int Run(int argc, Character** argv)
    {
        std::vector<std::filesystem::path> arguments;
        arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0u);
        for (int index = 1; index < argc; ++index) { arguments.emplace_back(argv[index]); }

        // The registry is built by the coordinator, after it has parsed the command line, so that
        // options selecting a build-time service (--fx-compiler) reach registration.
        return Pipeline::RunContentCompiler(
            arguments, [](const Pipeline::ContentCompilerOptions& options)
        {
            auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
            Pipeline::RegisterBuiltInContentPipeline(*registry, options);
            return registry;
        });
    }
}

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
{
    return Run(argc, argv);
}
#else
int main(int argc, char** argv)
{
    return Run(argc, argv);
}
#endif
