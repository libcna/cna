// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-260: the `.xml` source route.
//
// The pieces were all here before this file was: XNA's `XmlImporter` reads a document and asks the
// intermediate serializer to build whatever its `Asset Type` names; `ContentCompiler` knows how to
// write any type registered with it; `RegisterXnaXnbOutput` already turns those into canonical
// `.xnb` writers. What no one had written was the route joining them, so `.xml` was the one
// declared source extension a build could not take.
#include "CNA/Content/Pipeline/XnaXmlSourceContentPipeline.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "CNA/Content/Pipeline/XnaPipelineBridge.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/XmlImporter.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;

        /**
         * @brief The set of type names one registry's `.xml` route can produce.
         *
         * A game registers its own content compiler after the built-in one, and both reach the
         * same single `.xml` importer -- there is one `.xml` extension, and an importer is
         * resolved by extension. So the declared set has to grow with the second registration
         * rather than be fixed by the first, which is why the importer holds it by handle.
         */
        struct XmlDeclaredTypes
        {
            /** @brief Guards @ref names against a second registration on another thread. */
            mutable std::mutex mutex;

            /** @brief Every type name declared so far, in registration order. */
            std::vector<std::string> names;
        };

        /**
         * @brief The `.xml` importer, declaring every type the registry's compilers can carry.
         *
         * The generic bridge adapter declares one output type -- the importer's `ImportResult` --
         * and for this importer that is `System.Object`, which is true and useless: the value it
         * answers carries the document's own type, and the graph resolves the processor from that.
         * Declaring the whole set instead is what lets an `.xml` asset take part in the incremental
         * build, whose up-to-date check walks an importer's declared output types looking for the
         * route it recorded last time.
         */
        class XnaXmlImporterComponent final : public ContentImporter, public XnaImporterMetadata
        {
        public:
            XnaXmlImporterComponent(Xna::ContentImporterAttribute attribute,
                                    std::shared_ptr<XmlDeclaredTypes> declared)
                : attribute_(std::move(attribute)), declared_(std::move(declared))
            {
            }

            /** @brief Returns the shared declared-type set, so a later registration can extend it. */
            [[nodiscard]] const std::shared_ptr<XmlDeclaredTypes>& Declared() const noexcept
            {
                return declared_;
            }

            [[nodiscard]] ContentComponentIdentity Identity() const override
            {
                return ContentComponentIdentity{"XmlImporter", "1"};
            }

            [[nodiscard]] std::vector<std::string> SourceExtensions() const override
            {
                return {".xml"};
            }

            [[nodiscard]] std::vector<std::string> OutputTypes() const override
            {
                const std::lock_guard<std::mutex> lock(declared_->mutex);
                return declared_->names;
            }

            [[nodiscard]] std::string DefaultProcessor() const override
            {
                // XNA's own descriptor names none, and naming one here would be worse than
                // useless: which processor applies depends on the type the document declares.
                return {};
            }

            [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override
            {
                Xna::XmlImporter importer{};
                XnaBridgeImporterContext xnaContext(context);
                Xna::IContentImporter& untyped = importer;
                return untyped.Import(context.SourcePath().string(), xnaContext);
            }

            [[nodiscard]] const std::string& XnaClassName() const noexcept override
            {
                static const std::string name("XmlImporter");
                return name;
            }

            [[nodiscard]] const std::string& Catalog() const noexcept override
            {
                static const std::string name("Microsoft.Xna.Framework.Content.Pipeline");
                return name;
            }

            [[nodiscard]] const Xna::ContentImporterAttribute& Attribute() const noexcept override
            {
                return attribute_;
            }

            [[nodiscard]] std::string OutputTypeName() const override { return "System.Object"; }

        private:
            Xna::ContentImporterAttribute attribute_;
            std::shared_ptr<XmlDeclaredTypes> declared_;
        };

        /**
         * @brief XNA's `PassThroughProcessor` for one type: the input, unchanged.
         *
         * One instance per type the compiler knows, because the registry keys a processor by name
         * and resolves it by input type. Nothing has to speak the names these carry: a project
         * naming `PassThroughProcessor` reaches them through the component mapping, which leaves
         * the processor unnamed and lets the imported type choose, and a project naming no
         * processor reaches them the same way.
         */
        class XnaPassThroughComponent final : public ContentProcessor
        {
        public:
            explicit XnaPassThroughComponent(std::string typeName)
                : typeName_(std::move(typeName)),
                  name_("PassThroughProcessor(" + typeName_ + ")")
            {
            }

            [[nodiscard]] ContentComponentIdentity Identity() const override
            {
                return ContentComponentIdentity{name_, "1"};
            }

            [[nodiscard]] std::string InputType() const override { return typeName_; }
            [[nodiscard]] std::string OutputType() const override { return typeName_; }

            void ValidateParameters(const ContentProcessorParameters& parameters) const override
            {
                for (const auto& [name, value] : parameters.Values())
                {
                    static_cast<void>(value);
                    // Typed, so a host that has chosen XNA's leniency drops the parameter with a
                    // warning rather than failing: XNA's own PassThroughProcessor has no
                    // properties either, and a `.contentproj` may still carry some.
                    throw ContentParameterError(
                        ContentParameterFault::UnknownName, name,
                        "PassThroughProcessor does not accept processor parameters, and was given "
                        "'" + name + "'.");
                }
            }

            [[nodiscard]] ContentValue Process(const ContentValue& input,
                                               ContentProcessorContext& context) const override
            {
                context.LogInfo("passed a " + typeName_ + " through unchanged.");
                return input;
            }

        private:
            std::string typeName_;
            std::string name_;
        };
    }

    void RegisterXnaXmlSourceContentPipeline(
        ContentPipelineRegistry& registry,
        std::shared_ptr<const Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler::ContentCompiler> compiler,
        const CNA::Internal::Xnb::XnbFileOptions& options)
    {
        if (compiler == nullptr)
        {
            throw std::invalid_argument(
                "RegisterXnaXmlSourceContentPipeline(): compiler must not be null.");
        }
        // A second call -- a game registering its own compiler after the built-in one -- extends
        // the route rather than duplicating it: one `.xml` extension, one importer, one
        // pass-through per type name, whichever compiler contributed it.
        std::shared_ptr<XmlDeclaredTypes> declared;
        for (const std::shared_ptr<const ContentImporter>& existing : registry.Importers())
        {
            if (const auto* xml = dynamic_cast<const XnaXmlImporterComponent*>(existing.get()))
            {
                declared = xml->Declared();
                break;
            }
        }
        const bool first = declared == nullptr;
        if (first) { declared = std::make_shared<XmlDeclaredTypes>(); }

        for (const std::string& typeName : compiler->KnownTypeNames())
        {
            {
                const std::lock_guard<std::mutex> lock(declared->mutex);
                if (std::find(declared->names.begin(), declared->names.end(), typeName) !=
                    declared->names.end())
                {
                    continue;
                }
            }
            registry.RegisterProcessor(std::make_shared<const XnaPassThroughComponent>(typeName));
            // The `.xml` route is XNB-only, and that is a decision rather than an omission: an
            // XNA object graph is what an `.xnb` expresses, and `.cnb` is CNA's own container with
            // its own schema per asset type -- one would have to be invented for every type a
            // game's document might name. Recording it is what makes the refusal say which of the
            // two it is (plans/plan_xnapipeline_parity.md XNAPP-260).
            registry.DocumentAbsentWriter(
                ContentOutputFormat::Cnb, typeName,
                "the `.xml` source route writes XNA object graphs, which is what an `.xnb` is; "
                "`.cnb` carries CNA's own per-asset-type schemas and has none for '" + typeName +
                "'. Build this asset with --format xnb.");
            const std::lock_guard<std::mutex> lock(declared->mutex);
            declared->names.push_back(typeName);
        }

        // Registered last, because an importer that declares no output type at all is refused --
        // and on the first call the set is only filled by the loop above.
        if (first)
        {
            Xna::ContentImporterAttribute attribute = Xna::XmlImporter::Attribute();
            registry.RegisterImporter(
                std::make_shared<const XnaXmlImporterComponent>(std::move(attribute), declared));
        }

        RegisterXnaXnbOutput(registry, std::move(compiler), options);
    }
}
