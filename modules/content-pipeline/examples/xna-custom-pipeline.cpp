// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-260: a third party's content pipeline, written the way
// XNA's documentation tells a game to write one.
//
// Everything here is a *user's* code. It defines its own intermediate types, its own source
// extension, its own importer and processor derived from `ContentImporter<T>` and
// `ContentProcessor<TInput, TOutput>`, and its own `ContentTypeWriter<T>` naming a runtime reader
// that lives in the game's assembly rather than in XNA's. It reaches an `.xnb` through the same
// coordinator `cna-content` uses, registered through the documented bridge -- no CNA header is
// included except the two that a consumer is supposed to include, and nothing in CNA knows this
// file exists.
//
// The point is acceptance rather than illustration: a route like this has to carry dependencies,
// start nested builds, share a resource between two references, take processor parameters, log
// diagnostics and take part in incremental rebuilds, and every one of those is exercised here so
// that a change which quietly breaks one of them fails a test rather than a user's project.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentCompiler.hpp"
#include "CNA/Content/Pipeline/XnaPipelineBridge.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/PipelineException.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace
{
    namespace Canon = CNA::Content::Pipeline;
    namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
    namespace Compiler = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler;
    using Microsoft::Xna::Framework::Vector3;

    /** @brief One step of a quest: the game's own intermediate and runtime type. */
    class QuestStep final : public Xna::ContentItem
    {
    public:
        /** @brief .NET full name, as the game's own assembly spells it. */
        static constexpr std::string_view XnaTypeName = "QuestGame.QuestStep";

        /** @brief Where the step happens. */
        Vector3 position;

        /** @brief What the step tells the player. */
        std::string label;

        /** @brief Returns the type's stable name. */
        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name{XnaTypeName};
            return name;
        }
    };

    /** @brief A quest: steps, one step shared by two references, and a reward built beside it. */
    class Quest final : public Xna::ContentItem
    {
    public:
        /** @brief .NET full name, as the game's own assembly spells it. */
        static constexpr std::string_view XnaTypeName = "QuestGame.Quest";

        /** @brief The steps, in order. */
        std::vector<std::shared_ptr<QuestStep>> steps;

        /** @brief The step the quest both starts and ends at; written once, referenced twice. */
        std::shared_ptr<QuestStep> hub;

        /** @brief How many times the quest may be repeated; a processor parameter sets it. */
        std::int32_t repeats = 1;

        /** @brief The reward asset, built as its own `.xnb` beside this one. */
        Xna::ExternalReference<Quest> reward;

        /** @brief Returns the type's stable name. */
        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name{XnaTypeName};
            return name;
        }
    };

    /**
     * @brief Reads a `.quest` file.
     *
     * The format is deliberately trivial -- a name, then one `x y z label` line per step -- because
     * what is being proven is the shape of the route, not a parser. The importer records the
     * sidecar it reads as a build dependency, which is what makes a change to that file rebuild
     * the quest.
     */
    class QuestImporter final : public Xna::ContentImporter<Quest>
    {
    public:
        [[nodiscard]] std::shared_ptr<Quest> Import(const std::string& filename,
                                                    Xna::ContentImporterContext& context) override
        {
            std::ifstream stream(filename);
            if (!stream)
            {
                throw Xna::InvalidContentException("QuestImporter: '" + filename +
                                                   "' could not be opened.");
            }
            auto quest = std::make_shared<Quest>();
            quest->setIdentityProperty(Xna::ContentIdentity(filename, "QuestImporter"));

            std::string line;
            std::getline(stream, line);
            quest->setNameProperty(line);

            while (std::getline(stream, line))
            {
                if (line.empty()) { continue; }
                std::istringstream fields(line);
                auto step = std::make_shared<QuestStep>();
                fields >> step->position.X >> step->position.Y >> step->position.Z >> step->label;
                quest->steps.push_back(step);
            }
            if (quest->steps.empty())
            {
                throw Xna::InvalidContentException("QuestImporter: '" + filename +
                                                   "' declares no steps.");
            }
            // The first step is also the hub, so the writer has one object reached by two
            // references and the shared-resource path is really exercised.
            quest->hub = quest->steps.front();

            // A sidecar beside the source, read for its text and recorded so that editing it
            // rebuilds this asset even though it is not the primary source.
            const std::filesystem::path sidecar =
                std::filesystem::path(filename).replace_extension(".notes");
            std::error_code error;
            if (std::filesystem::exists(sidecar, error) && !error)
            {
                context.AddDependency(sidecar.string());
                std::ifstream notes(sidecar);
                std::string note;
                std::getline(notes, note);
                quest->setNameProperty(quest->getNameProperty() + " (" + note + ")");
            }
            context.getLoggerProperty().LogMessage("read {0} step(s)",
                                                   std::to_string(quest->steps.size()));
            return quest;
        }
    };

    /**
     * @brief Applies the game's own options and builds the quest's reward as its own asset.
     *
     * `Repeats` is an ordinary declared parameter, so a `.contentproj` or a build configuration can
     * set it by name. The reward is a nested build: the same importer and processor run over
     * another source file, and the result is published beside this asset and referenced by name.
     */
    class QuestProcessor final : public Xna::ContentProcessor<Quest, Quest>
    {
    public:
        /** @brief Gets how many times the quest may be repeated. */
        [[nodiscard]] std::int32_t getRepeatsProperty() const { return repeats_; }

        /** @brief Sets how many times the quest may be repeated. */
        void setRepeatsProperty(std::int32_t value) { repeats_ = value; }

        /** @brief Gets the reward source this quest builds beside itself. */
        [[nodiscard]] std::string getRewardProperty() const { return reward_; }

        /** @brief Sets the reward source this quest builds beside itself. */
        void setRewardProperty(std::string value) { reward_ = std::move(value); }

        /**
         * @brief Declares the properties a build may set by name.
         * @param bindings The bindings to add to.
         */
        static void DescribeParameters(Xna::ProcessorParameterBindings<QuestProcessor>& bindings)
        {
            bindings.Add<std::int32_t>("Repeats", &QuestProcessor::getRepeatsProperty,
                                       &QuestProcessor::setRepeatsProperty, "Repeats",
                                       "How many times the quest may be taken.");
            bindings.Add<std::string>("Reward", &QuestProcessor::getRewardProperty,
                                      &QuestProcessor::setRewardProperty);
        }

        [[nodiscard]] std::shared_ptr<Quest> Process(const std::shared_ptr<Quest>& input,
                                                     Xna::ContentProcessorContext& context) override
        {
            if (input == nullptr)
            {
                throw Xna::PipelineException("QuestProcessor: a null quest cannot be processed.");
            }
            input->repeats = repeats_;
            if (!reward_.empty())
            {
                const std::filesystem::path source =
                    std::filesystem::path(input->getIdentityProperty().getSourceFilenameProperty())
                        .parent_path() / reward_;
                input->reward = context.BuildAsset<Quest, Quest>(
                    Xna::ExternalReference<Quest>(source.string()), "QuestProcessor");
                context.getLoggerProperty().LogImportantMessage(
                    "built the reward asset {0}", input->reward.getFilenameProperty());
            }
            return input;
        }

    private:
        std::int32_t repeats_ = 1;
        std::string reward_;
    };

    /** @brief Writes a step; its runtime reader lives in the game's own assembly. */
    class QuestStepWriter final : public Compiler::ContentTypeWriter<QuestStep>
    {
    public:
        [[nodiscard]] std::string GetRuntimeReader(Xna::TargetPlatform) const override
        {
            return "QuestGame.QuestStepReader, QuestGame";
        }

        [[nodiscard]] std::int32_t getTypeVersionProperty() const override { return 2; }

    protected:
        void Write(Compiler::ContentWriter& output, const std::shared_ptr<QuestStep>& value) override
        {
            output.Write(value->position);
            output.Write(value->label);
        }
    };

    /** @brief Writes a quest, sharing the hub step and referencing the reward by name. */
    class QuestWriter final : public Compiler::ContentTypeWriter<Quest>
    {
    public:
        [[nodiscard]] std::string GetRuntimeReader(Xna::TargetPlatform) const override
        {
            return "QuestGame.QuestReader, QuestGame";
        }

    protected:
        void Write(Compiler::ContentWriter& output, const std::shared_ptr<Quest>& value) override
        {
            output.Write(value->getNameProperty());
            output.Write(value->repeats);
            output.Write(static_cast<std::int32_t>(value->steps.size()));
            for (const std::shared_ptr<QuestStep>& step : value->steps)
            {
                output.WriteObject<QuestStep>(step);
            }
            // The hub is one of the steps that was just written. A shared resource is written once
            // however many references name it, which is the whole reason XNA has the mechanism.
            output.WriteSharedResource<QuestStep>(value->hub);
            output.WriteExternalReference<Quest>(value->reward);
        }
    };

    template<typename Character>
    int Run(int argc, Character** argv)
    {
        std::vector<std::filesystem::path> arguments;
        arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0u);
        for (int index = 1; index < argc; ++index) { arguments.emplace_back(argv[index]); }

        return Canon::RunContentCompiler(
            arguments,
            [](const Canon::ContentCompilerOptions& options)
            {
                auto registry = std::make_shared<Canon::ContentPipelineRegistry>();
                Canon::RegisterBuiltInContentPipeline(*registry, options);

                Xna::ContentImporterAttribute attribute(".quest");
                attribute.setDefaultProcessorProperty("QuestProcessor");
                attribute.setDisplayNameProperty("Quest - QuestGame");
                Canon::RegisterXnaImporter<QuestImporter>(*registry, "QuestImporter", attribute, "1",
                                                          "QuestGame.Pipeline");
                Canon::RegisterXnaProcessor<QuestProcessor>(
                    *registry, "QuestProcessor", Xna::ContentProcessorAttribute{}, "1",
                    "QuestGame.Pipeline");

                auto compiler = std::make_shared<Compiler::ContentCompiler>();
                compiler->AddTypeWriter<QuestStepWriter>();
                compiler->AddTypeWriter<QuestWriter>();
                Canon::RegisterXnaXnbOutput(*registry, compiler, options.xnbContainer);
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
