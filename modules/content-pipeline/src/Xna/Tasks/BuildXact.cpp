// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/BuildXact.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "CNA/Internal/HostProcess.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Tasks
{
    namespace
    {
        /** @brief The XACT compiler this host can reach, or empty. */
        [[nodiscard]] std::string DiscoverXactCompiler(const std::string& explicitPath)
        {
            if (!explicitPath.empty())
            {
                return explicitPath;
            }
            if (const char* const named = std::getenv("CNA_XACTBLD"); named != nullptr && *named != '\0')
            {
                return named;
            }
            return std::string();
        }

        /** @brief The launcher to run it through (`wine`), or empty. */
        [[nodiscard]] std::string DiscoverXactLauncher(const std::string& explicitPath)
        {
            if (!explicitPath.empty())
            {
                return explicitPath;
            }
            if (const char* const named = std::getenv("CNA_XACTBLD_LAUNCHER");
                named != nullptr && *named != '\0')
            {
                return named;
            }
            return std::string();
        }
    }

    const std::string& BuildXact::getBuildConfigurationProperty() const noexcept
    {
        return buildConfiguration_;
    }

    void BuildXact::setBuildConfigurationProperty(std::string value)
    {
        buildConfiguration_ = std::move(value);
    }

    const std::string& BuildXact::getContentProjectGUIDProperty() const noexcept
    {
        return contentProjectGuid_;
    }

    void BuildXact::setContentProjectGUIDProperty(std::string value)
    {
        contentProjectGuid_ = std::move(value);
    }

    const std::string& BuildXact::getIntermediateDirectoryProperty() const noexcept
    {
        return intermediateDirectory_;
    }

    void BuildXact::setIntermediateDirectoryProperty(std::string value)
    {
        intermediateDirectory_ = std::move(value);
    }

    const std::vector<TaskItem>& BuildXact::getIntermediateFilesProperty() const noexcept
    {
        return intermediateFiles_;
    }

    const std::string& BuildXact::getLoggerRootDirectoryProperty() const noexcept
    {
        return loggerRootDirectory_;
    }

    void BuildXact::setLoggerRootDirectoryProperty(std::string value)
    {
        loggerRootDirectory_ = std::move(value);
    }

    const std::string& BuildXact::getOutputDirectoryProperty() const noexcept { return outputDirectory_; }

    void BuildXact::setOutputDirectoryProperty(std::string value)
    {
        outputDirectory_ = std::move(value);
    }

    const std::vector<TaskItem>& BuildXact::getOutputXactFilesProperty() const noexcept
    {
        return outputXactFiles_;
    }

    bool BuildXact::getRebuildAllProperty() const noexcept { return rebuildAll_; }

    void BuildXact::setRebuildAllProperty(const bool value) noexcept { rebuildAll_ = value; }

    const std::vector<TaskItem>& BuildXact::getRebuiltXactFilesProperty() const noexcept
    {
        return rebuiltXactFiles_;
    }

    const std::string& BuildXact::getRootDirectoryProperty() const noexcept { return rootDirectory_; }

    void BuildXact::setRootDirectoryProperty(std::string value) { rootDirectory_ = std::move(value); }

    const std::string& BuildXact::getTargetPlatformProperty() const noexcept { return targetPlatform_; }

    void BuildXact::setTargetPlatformProperty(std::string value) { targetPlatform_ = std::move(value); }

    const std::string& BuildXact::getTargetProfileProperty() const noexcept { return targetProfile_; }

    void BuildXact::setTargetProfileProperty(std::string value) { targetProfile_ = std::move(value); }

    const std::vector<TaskItem>& BuildXact::getXactProjectsProperty() const noexcept
    {
        return xactProjects_;
    }

    void BuildXact::setXactProjectsProperty(std::vector<TaskItem> value)
    {
        xactProjects_ = std::move(value);
    }

    const std::string& BuildXact::getXnaFrameworkVersionProperty() const noexcept
    {
        return xnaFrameworkVersion_;
    }

    void BuildXact::setXnaFrameworkVersionProperty(std::string value)
    {
        xnaFrameworkVersion_ = std::move(value);
    }

    void BuildXact::SetXactCompilerEXT(std::string executable, std::string launcher)
    {
        xactExecutable_ = std::move(executable);
        xactLauncher_ = std::move(launcher);
    }

    bool BuildXact::HasXactCompilerEXT() const
    {
        return !DiscoverXactCompiler(xactExecutable_).empty();
    }

    bool BuildXact::Execute()
    {
        intermediateFiles_.clear();
        outputXactFiles_.clear();
        rebuiltXactFiles_.clear();

        if (outputDirectory_.empty())
        {
            LogError("BuildXact needs an OutputDirectory before it can run.");
            return false;
        }
        if (xactProjects_.empty())
        {
            LogMessage("BuildXact: no XACT projects, so nothing was built.");
            return true;
        }

        // Validation first, and unconditionally: a project with a missing or unreadable .xap must
        // be told that rather than being told the tool is missing, whichever is also true.
        const std::filesystem::path root(rootDirectory_.empty() ? "." : rootDirectory_);
        std::vector<std::filesystem::path> projects;
        for (const TaskItem& project : xactProjects_)
        {
            const std::filesystem::path spec(project.getItemSpecProperty());
            const std::filesystem::path absolute = spec.is_absolute() ? spec : (root / spec);
            std::error_code error;
            if (!std::filesystem::exists(absolute, error) || error)
            {
                LogError("BuildXact: the XACT project \"" + project.getItemSpecProperty() +
                         "\" does not exist.");
                return false;
            }
            std::ifstream file(absolute, std::ios::binary);
            if (!file)
            {
                LogError("BuildXact: the XACT project \"" + project.getItemSpecProperty() +
                         "\" could not be opened.");
                return false;
            }
            // An .xap is a text project file; its first line names the signature. Reading it is
            // what tells a mis-named file from a real one before any tool is invoked.
            std::string first;
            std::getline(file, first);
            // An .xap opens with `Signature = XACT2;`, so both words on the first line are what
            // tells a real project from a file that merely mentions XACT.
            if (first.find("Signature") == std::string::npos || first.find("XACT") == std::string::npos)
            {
                LogError("BuildXact: \"" + project.getItemSpecProperty() +
                         "\" does not look like an XACT project: its first line names neither a "
                         "signature nor XACT.");
                return false;
            }
            projects.push_back(absolute);
        }

        const std::string compiler = DiscoverXactCompiler(xactExecutable_);
        if (compiler.empty())
        {
            // The one genuinely external step in this plan. Everything above it ran.
            LogError("BuildXact validated " + std::to_string(projects.size()) +
                     " XACT project(s) and cannot compile them: an XACT project is compiled by "
                     "Microsoft's own XactBld3.exe, which ships with the XNA Game Studio tools and "
                     "is not something CNA can reimplement or redistribute. Name one with "
                     "SetXactCompilerEXT() or the CNA_XACTBLD environment variable, and a launcher "
                     "such as wine with CNA_XACTBLD_LAUNCHER, and this task will invoke it.");
            return false;
        }

        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(outputDirectory_), error);
        const std::string launcher = DiscoverXactLauncher(xactLauncher_);
        bool succeeded = true;
        for (const std::filesystem::path& project : projects)
        {
            // XactBld3's own command line: the project, the output directory, and the platform.
            std::vector<std::string> arguments;
            if (!launcher.empty())
            {
                arguments.push_back(compiler);
            }
            arguments.push_back(project.string());
            arguments.push_back(outputDirectory_);
            if (!targetPlatform_.empty())
            {
                arguments.push_back("/X:" + targetPlatform_);
            }
            if (rebuildAll_)
            {
                arguments.emplace_back("/F");
            }
            const CNA::Internal::HostProcessResult result =
                CNA::Internal::RunHostProcess(launcher.empty() ? compiler : launcher, arguments);
            if (!result.started)
            {
                LogError("BuildXact could not run the XACT compiler \"" + compiler +
                         "\": " + result.failure);
                return false;
            }
            if (result.exitCode != 0)
            {
                LogError("BuildXact: the XACT compiler refused \"" + project.string() +
                         "\" with status " + std::to_string(result.exitCode) + ": " +
                         (result.standardError.empty() ? result.standardOutput : result.standardError));
                succeeded = false;
                continue;
            }
            for (const std::filesystem::directory_entry& produced :
                 std::filesystem::directory_iterator(std::filesystem::path(outputDirectory_), error))
            {
                if (!produced.is_regular_file())
                {
                    continue;
                }
                const std::string extension = produced.path().extension().string();
                if (extension != ".xgs" && extension != ".xwb" && extension != ".xsb")
                {
                    continue;
                }
                TaskItem item(produced.path().string());
                item.SetMetadata("SourceAsset", project.string());
                outputXactFiles_.push_back(item);
                rebuiltXactFiles_.push_back(item);
            }
        }
        if (succeeded)
        {
            LogMessage("BuildXact: " + std::to_string(outputXactFiles_.size()) + " file(s) produced.");
        }
        return succeeded;
    }

    const std::string& BuildXact::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
