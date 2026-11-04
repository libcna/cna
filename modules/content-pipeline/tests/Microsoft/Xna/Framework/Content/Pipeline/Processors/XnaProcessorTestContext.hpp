// SPDX-License-Identifier: MS-PL
//
// TQ-034: the build context the processor tests hand a processor, outside XNA's internal build
// engine -- the same answers tools/xna-pipeline-oracle's drivers give the genuine processors: a
// conversion is answered with its input, a nested build with a reference to `<source>.xnb`, and
// both are recorded.
#pragma once

#include <string>
#include <vector>

#include "CNA/Internal/PathUtf8.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "System/NotSupportedException.hpp"

namespace CNA::Tests::XnaProcessors
{
    namespace Pipeline = Microsoft::Xna::Framework::Content::Pipeline;

    /** @brief A logger that drops everything. */
    class SilentLogger final : public Pipeline::ContentBuildLogger
    {
    protected:
        void LogMessage(const std::string&) override {}
        void LogImportantMessage(const std::string&) override {}
        void LogWarning(const std::string&, const Pipeline::ContentIdentity&, const std::string&) override {}
    };

    /** @brief Windows/HiDef, Debug; records every conversion and nested build it is asked for. */
    class RecordingContext final : public Pipeline::ContentProcessorContext
    {
    public:
        [[nodiscard]] std::string getBuildConfigurationProperty() const override { return "Debug"; }
        [[nodiscard]] std::string getIntermediateDirectoryProperty() const override { return "obj"; }
        [[nodiscard]] Pipeline::ContentBuildLogger& getLoggerProperty() const override { return logger_; }
        [[nodiscard]] std::string getOutputDirectoryProperty() const override { return "bin"; }
        [[nodiscard]] std::string getOutputFilenameProperty() const override { return "asset.xnb"; }
        [[nodiscard]] const Pipeline::OpaqueDataDictionary& getParametersProperty() const override
        {
            return parameters_;
        }
        [[nodiscard]] Pipeline::TargetPlatform getTargetPlatformProperty() const override
        {
            return Pipeline::TargetPlatform::Windows;
        }
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::GraphicsProfile getTargetProfileProperty() const override
        {
            return Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef;
        }
        void AddDependency(const std::string&) override {}
        void AddOutputFile(const std::string&) override {}

        /** @brief What was asked for, in order: `convert->Processor` or `file->Processor`. */
        std::vector<std::string> requests;

    protected:
        [[nodiscard]] Pipeline::ContentObject BuildAndLoadAssetCore(
            const std::string&, const Pipeline::ContentIdentity&, const std::string&,
            const Pipeline::OpaqueDataDictionary&, const std::string&, const std::string&, const std::string&) override
        {
            throw System::NotSupportedException("BuildAndLoadAsset");
        }

        [[nodiscard]] std::string BuildAssetCore(const std::string& sourceFilename, const Pipeline::ContentIdentity&,
                                                 const std::string& processorName,
                                                 const Pipeline::OpaqueDataDictionary&, const std::string&,
                                                 const std::string&, const std::string&, const std::string&) override
        {
            requests.push_back(CNA::Internal::PathToUtf8(CNA::Internal::PathFromUtf8(sourceFilename).filename()) +
                               "->" + processorName);
            return sourceFilename + ".xnb";
        }

        [[nodiscard]] Pipeline::ContentObject ConvertCore(const Pipeline::ContentObject& input,
                                                          const std::string& processorName,
                                                          const Pipeline::OpaqueDataDictionary&, const std::string&,
                                                          const std::string&) override
        {
            requests.push_back("convert->" + processorName);
            return input;
        }

    private:
        mutable SilentLogger logger_;
        Pipeline::OpaqueDataDictionary parameters_;
    };

    /** @brief The refusal a call ends in: its message and the identity it carries. */
    struct Refusal
    {
        /** @brief "built" when nothing was thrown. */
        std::string message = "built";
        /** @brief The exception's identity; empty when nothing was thrown. */
        Pipeline::ContentIdentity identity;
    };

    /** @brief Runs @p body and records the InvalidContentException it throws, if any. */
    template<typename Body>
    [[nodiscard]] Refusal RefusalOf(Body&& body)
    {
        Refusal refusal;
        try
        {
            body();
        }
        catch (const Pipeline::InvalidContentException& error)
        {
            refusal.message = error.getMessageProperty();
            refusal.identity = error.getContentIdentityProperty();
        }
        return refusal;
    }

    /** @brief The identity as the lane oracle prints it: {file|tool|fragment}. */
    [[nodiscard]] inline std::string Spell(const Pipeline::ContentIdentity& identity)
    {
        return "{" + identity.getSourceFilenameProperty() + "|" + identity.getSourceToolProperty() + "|" +
               identity.getFragmentIdentifierProperty() + "}";
    }
}
