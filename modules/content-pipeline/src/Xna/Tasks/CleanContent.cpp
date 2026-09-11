// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/CleanContent.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/ContentCompiler.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Tasks
{
    namespace Canon = CNA::Content::Pipeline;

    const std::string& CleanContent::getBuildConfigurationProperty() const noexcept
    {
        return buildConfiguration_;
    }

    void CleanContent::setBuildConfigurationProperty(std::string value)
    {
        buildConfiguration_ = std::move(value);
    }

    const std::string& CleanContent::getContentProjectGUIDProperty() const noexcept
    {
        return contentProjectGuid_;
    }

    void CleanContent::setContentProjectGUIDProperty(std::string value)
    {
        contentProjectGuid_ = std::move(value);
    }

    const std::string& CleanContent::getIntermediateDirectoryProperty() const noexcept
    {
        return intermediateDirectory_;
    }

    void CleanContent::setIntermediateDirectoryProperty(std::string value)
    {
        intermediateDirectory_ = std::move(value);
    }

    const std::string& CleanContent::getOutputDirectoryProperty() const noexcept
    {
        return outputDirectory_;
    }

    void CleanContent::setOutputDirectoryProperty(std::string value)
    {
        outputDirectory_ = std::move(value);
    }

    const std::string& CleanContent::getRootDirectoryProperty() const noexcept { return rootDirectory_; }

    void CleanContent::setRootDirectoryProperty(std::string value) { rootDirectory_ = std::move(value); }

    const std::string& CleanContent::getTargetPlatformProperty() const noexcept { return targetPlatform_; }

    void CleanContent::setTargetPlatformProperty(std::string value)
    {
        targetPlatform_ = std::move(value);
    }

    const std::string& CleanContent::getTargetProfileProperty() const noexcept { return targetProfile_; }

    void CleanContent::setTargetProfileProperty(std::string value) { targetProfile_ = std::move(value); }

    bool CleanContent::Execute()
    {
        if (outputDirectory_.empty())
        {
            LogError("CleanContent needs an OutputDirectory before it can run.");
            return false;
        }
        std::error_code error;
        if (!std::filesystem::exists(std::filesystem::path(outputDirectory_), error) || error)
        {
            // Nothing to clean is not a failure; a project cleaned twice must not fail the second
            // time.
            LogMessage("CleanContent: \"" + outputDirectory_ + "\" does not exist, so nothing was "
                       "removed.");
            return true;
        }
        int status = 1;
        try
        {
            status = Canon::RunContentCompiler(
                {"clean", std::filesystem::path(outputDirectory_), "--quiet"},
                [](const Canon::ContentCompilerOptions& options)
                {
                    auto registry = std::make_shared<Canon::ContentPipelineRegistry>();
                    Canon::RegisterBuiltInContentPipeline(*registry, options);
                    return registry;
                });
        }
        catch (const std::exception& failure)
        {
            LogError(std::string("CleanContent failed: ") + failure.what());
            return false;
        }
        if (status != 0)
        {
            LogError("CleanContent failed: the clean reported status " + std::to_string(status) + ".");
            return false;
        }
        // The build configuration this project's own BuildContent wrote is intermediate state and
        // is this task's to remove; nothing else under the intermediate directory is.
        if (!intermediateDirectory_.empty())
        {
            std::filesystem::remove(
                std::filesystem::path(intermediateDirectory_) / "cna-buildcontent.json", error);
        }
        LogMessage("CleanContent: removed the pipeline-owned files under \"" + outputDirectory_ + "\".");
        return true;
    }

    const std::string& CleanContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    const std::string& GetLastOutputs::getContentProjectGUIDProperty() const noexcept
    {
        return contentProjectGuid_;
    }

    void GetLastOutputs::setContentProjectGUIDProperty(std::string value)
    {
        contentProjectGuid_ = std::move(value);
    }

    const std::string& GetLastOutputs::getIntermediateDirectoryProperty() const noexcept
    {
        return intermediateDirectory_;
    }

    void GetLastOutputs::setIntermediateDirectoryProperty(std::string value)
    {
        intermediateDirectory_ = std::move(value);
    }

    const std::vector<TaskItem>& GetLastOutputs::getOutputContentFilesProperty() const noexcept
    {
        return outputContentFiles_;
    }

    void GetLastOutputs::setOutputDirectoryEXT(std::string value)
    {
        outputDirectory_ = std::move(value);
    }

    bool GetLastOutputs::Execute()
    {
        outputContentFiles_.clear();
        // XNA keeps its incremental state under the intermediate directory; CNA's manifest lives
        // beside the compiled output, so both are looked in and the explicit one wins.
        std::vector<std::filesystem::path> roots;
        if (!outputDirectory_.empty())
        {
            roots.emplace_back(outputDirectory_);
        }
        if (!intermediateDirectory_.empty())
        {
            roots.emplace_back(intermediateDirectory_);
        }
        if (roots.empty())
        {
            LogError("GetLastOutputs needs an IntermediateDirectory, or an output directory set "
                     "with SetOutputDirectoryEXT, before it can run.");
            return false;
        }
        for (const std::filesystem::path& root : roots)
        {
            const std::filesystem::path file = root / Canon::ContentBuildManifestFileName;
            std::ifstream stream(file, std::ios::binary);
            if (!stream)
            {
                continue;
            }
            const std::string json((std::istreambuf_iterator<char>(stream)),
                                   std::istreambuf_iterator<char>());
            Canon::ContentBuildManifest manifest;
            try
            {
                manifest = Canon::ContentBuildManifest::Parse(json);
            }
            catch (const std::exception& failure)
            {
                LogError(std::string("GetLastOutputs could not read \"") + file.string() +
                         "\": " + failure.what());
                return false;
            }
            for (const auto& [nodeId, entry] : manifest.Entries())
            {
                for (const Canon::ContentBuildManifestOutput& produced : entry.outputs)
                {
                    TaskItem item((root / produced.path).string());
                    item.SetMetadata("Name", nodeId);
                    item.SetMetadata("SourceAsset", entry.source);
                    outputContentFiles_.push_back(item);
                }
                for (const Canon::ContentBuildManifestDeploymentFile& deployed : entry.deploymentFiles)
                {
                    TaskItem item((root / deployed.path).string());
                    item.SetMetadata("Name", nodeId);
                    item.SetMetadata("SourceAsset", entry.source);
                    outputContentFiles_.push_back(item);
                }
            }
            LogMessage("GetLastOutputs: " + std::to_string(outputContentFiles_.size()) +
                       " file(s) from the previous build.");
            return true;
        }
        // No previous build is not a failure: a project asking what is there is told nothing is.
        LogMessage("GetLastOutputs: no previous build was found.");
        return true;
    }

    const std::string& GetLastOutputs::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
