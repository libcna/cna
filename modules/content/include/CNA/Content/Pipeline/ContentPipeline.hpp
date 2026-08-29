// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <utility>
#include <variant>
#include <vector>

namespace CNA::Content::Pipeline
{
    /** @brief Stability marker for the initial custom C++ pipeline component API. */
    inline constexpr bool ContentPipelineExtensionApiIsExperimental = true;

    /** @brief Stable, author-controlled identity used for diagnostics and build invalidation. */
    struct ContentComponentIdentity
    {
        /** @brief Stable component name, independent of a C++ ABI or RTTI spelling. */
        std::string name;

        /** @brief Stable build version changed whenever content-affecting behavior changes. */
        std::string version;

        /** @brief Compares both the stable component name and version. */
        bool operator==(const ContentComponentIdentity&) const = default;
    };

    /** @brief Pipeline stages reported by diagnostics and build logging. */
    enum class ContentPipelineStage
    {
        Selection,
        Import,
        Process,
        Write,
        Publish,
    };

    /**
     * @brief Returns the stable diagnostic spelling of a pipeline stage.
     *
     * @param stage The stage to name.
     * @return A process-lifetime string literal.
     */
    [[nodiscard]] const char* ContentPipelineStageName(ContentPipelineStage stage) noexcept;

    /** @brief Severity of one build log message. */
    enum class ContentLogLevel
    {
        Info,
        Warning,
        Error,
    };

    /** @brief One context-rich build log message. */
    struct ContentLogMessage
    {
        /** @brief Message severity. */
        ContentLogLevel level = ContentLogLevel::Info;

        /** @brief Primary native source path. */
        std::filesystem::path source;

        /** @brief Logical ContentManager asset name. */
        std::string logicalName;

        /** @brief Pipeline stage that emitted the message. */
        ContentPipelineStage stage = ContentPipelineStage::Selection;

        /** @brief Stable component name, or empty before a component is selected. */
        std::string component;

        /** @brief Human-readable message text. */
        std::string text;

        /** @brief Compares every contextual message field. */
        bool operator==(const ContentLogMessage&) const = default;
    };

    /** @brief Scoped logging sink for content builds. */
    class ContentBuildLogger
    {
    public:
        /** @brief Enables correct destruction through a logging interface pointer. */
        virtual ~ContentBuildLogger() = default;

        /**
         * @brief Receives one fully contextualized build message.
         *
         * @param message The message to consume before this call returns.
         */
        virtual void Log(const ContentLogMessage& message) = 0;
    };

    /** @brief Categories of build-time dependencies, excluding runtime CNB XREFs. */
    enum class ContentDependencyKind
    {
        PrimarySource,
        SourceFile,
        ContentBuild,
        Generated,
    };

    /** @brief One normalized build-time dependency. */
    struct ContentDependency
    {
        /** @brief Semantic category of the dependency. */
        ContentDependencyKind kind = ContentDependencyKind::SourceFile;

        /** @brief Canonical generic UTF-8 path, or logical name for a ContentBuild dependency. */
        std::string identity;

        /** @brief Orders records deterministically by category and identity. */
        bool operator<(const ContentDependency& other) const noexcept;

        /** @brief Compares dependency category and identity. */
        bool operator==(const ContentDependency&) const = default;
    };

    /** @brief One runtime content reference that may become a CNB XREF entry. */
    struct RuntimeContentReference
    {
        /** @brief ContentManager logical asset name. */
        std::string logicalName;

        /** @brief Expected CNB asset type identifier, or zero when unconstrained. */
        std::uint32_t expectedAssetTypeId = 0u;

        /** @brief Orders references deterministically by logical name and type. */
        bool operator<(const RuntimeContentReference& other) const noexcept;

        /** @brief Compares logical name and expected asset type. */
        bool operator==(const RuntimeContentReference&) const = default;
    };

    /** @brief Per-build collector that keeps build dependencies distinct from runtime XREFs. */
    class ContentDependencyCollector
    {
    public:
        /**
         * @brief Adds a normalized build-time dependency if it is not already present.
         *
         * @param dependency The dependency to add.
         */
        void Add(ContentDependency dependency);

        /**
         * @brief Adds a validated runtime content reference if it is not already present.
         *
         * @param reference The runtime reference to add.
         * @throws std::invalid_argument if the logical name is not a safe CNB content name.
         */
        void AddRuntimeReference(RuntimeContentReference reference);

        /**
         * @brief Returns build-time dependencies in deterministic order.
         *
         * @return A sorted copy of the collected build dependencies.
         */
        [[nodiscard]] std::vector<ContentDependency> Dependencies() const;

        /**
         * @brief Returns runtime content references in deterministic order.
         *
         * @return A sorted copy of the collected runtime references.
         */
        [[nodiscard]] std::vector<RuntimeContentReference> RuntimeReferences() const;

    private:
        std::set<ContentDependency> dependencies_;
        std::set<RuntimeContentReference> runtimeReferences_;
    };

    /** @brief Bounded value types accepted by dynamic processor configuration. */
    using ContentProcessorParameterValue =
        std::variant<bool, std::int64_t, std::uint64_t, double, std::string>;

    /** @brief Ordered, explicitly typed processor parameters. */
    class ContentProcessorParameters
    {
    public:
        /**
         * @brief Sets one parameter, replacing an earlier value under the same name.
         *
         * @param name Stable parameter name.
         * @param value Typed value.
         * @throws std::invalid_argument for an empty name or non-finite floating-point value.
         */
        void Set(std::string name, ContentProcessorParameterValue value);

        /**
         * @brief Finds one parameter without applying a default.
         *
         * @param name Parameter name to find.
         * @return Pointer valid for this object's lifetime, or null when absent.
         */
        [[nodiscard]] const ContentProcessorParameterValue* Find(const std::string& name) const;

        /**
         * @brief Returns every parameter in canonical key order.
         *
         * @return Read-only ordered parameter map.
         */
        [[nodiscard]] const std::map<std::string, ContentProcessorParameterValue>& Values() const
            noexcept;

        /** @brief Returns true when no parameters were configured. */
        [[nodiscard]] bool Empty() const noexcept;

        /** @brief Compares every parameter name, type, and value. */
        bool operator==(const ContentProcessorParameters&) const = default;

    private:
        std::map<std::string, ContentProcessorParameterValue> values_;
    };

    /**
     * @brief A checked in-memory value passed between heterogeneous pipeline components.
     *
     * Stable string identities drive registry selection and future persistent fingerprints.
     * `std::type_index` is retained only as a process-local defensive check before a component
     * casts the erased value; it is never serialized or exposed as persistent identity.
     */
    class ContentValue
    {
    public:
        /** @brief Constructs an empty value, used only before a component has produced output. */
        ContentValue() = default;

        /**
         * @brief Boxes one value under a stable pipeline type identity.
         *
         * @tparam T Concrete C++ type stored by shared ownership.
         * @param stableType Stable ABI-independent type identity.
         * @param value Value to move into the erased carrier.
         * @return The checked erased value.
         * @throws std::invalid_argument if @p stableType is empty.
         */
        template<typename T>
        [[nodiscard]] static ContentValue Create(std::string stableType, T value)
        {
            if (stableType.empty())
            {
                throw std::invalid_argument("ContentValue::Create(): stableType must not be empty.");
            }
            ContentValue result;
            result.stableType_ = std::move(stableType);
            result.cppType_ = std::type_index(typeid(T));
            result.value_ = std::make_shared<const T>(std::move(value));
            return result;
        }

        /**
         * @brief Returns the stable ABI-independent type identity.
         *
         * @return Stable pipeline type identity, or empty for an empty value.
         */
        [[nodiscard]] const std::string& StableType() const noexcept;

        /** @brief Returns true when this carrier contains no value. */
        [[nodiscard]] bool Empty() const noexcept;

        /**
         * @brief Accesses the concrete value after checking its process-local C++ type.
         *
         * @tparam T Concrete type expected by the component.
         * @return Read-only reference valid while this ContentValue remains alive.
         * @throws std::logic_error if the carrier is empty or contains another C++ type.
         */
        template<typename T>
        [[nodiscard]] const T& Get() const
        {
            if (value_ == nullptr)
            {
                throw std::logic_error("ContentValue::Get(): the value is empty.");
            }
            if (cppType_ != std::type_index(typeid(T)))
            {
                throw std::logic_error(
                    "ContentValue::Get(): the stable type '" + stableType_ +
                    "' does not contain the C++ type this component requested.");
            }
            return *static_cast<const T*>(value_.get());
        }

    private:
        std::string stableType_;
        std::type_index cppType_{typeid(void)};
        std::shared_ptr<const void> value_;
    };

    /** @brief Focused, call-scoped services available to a Content Importer. */
    class ContentImporterContext
    {
    public:
        /**
         * @brief Creates one importer invocation context.
         *
         * @param sourceRoot Canonical content source root.
         * @param source Canonical primary source path within @p sourceRoot.
         * @param logicalName Logical ContentManager asset name.
         * @param component Stable importer name for logs.
         * @param dependencies Per-build dependency collector.
         * @param logger Scoped logger.
         */
        ContentImporterContext(std::filesystem::path sourceRoot, std::filesystem::path source,
                               std::string logicalName, std::string component,
                               ContentDependencyCollector& dependencies,
                               ContentBuildLogger& logger);

        /** @brief Importer contexts are call-scoped and cannot be copied. */
        ContentImporterContext(const ContentImporterContext&) = delete;

        /** @brief Importer contexts are call-scoped and cannot be assigned. */
        ContentImporterContext& operator=(const ContentImporterContext&) = delete;

        /** @brief Returns the canonical source root. */
        [[nodiscard]] const std::filesystem::path& SourceRoot() const noexcept;

        /** @brief Returns the canonical primary source path. */
        [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept;

        /** @brief Returns the logical ContentManager asset name. */
        [[nodiscard]] const std::string& LogicalName() const noexcept;

        /**
         * @brief Resolves and records a file dependency relative to the primary source.
         *
         * @param authoredPath Relative path read from source content.
         * @return Canonical contained native path.
         * @throws std::invalid_argument for absolute paths or source-root escapes.
         */
        [[nodiscard]] std::filesystem::path ResolveSourceDependency(
            const std::filesystem::path& authoredPath);

        /**
         * @brief Emits an informational importer message.
         *
         * @param text Message text.
         */
        void LogInfo(std::string text) const;

        /**
         * @brief Emits an importer warning.
         *
         * @param text Message text.
         */
        void LogWarning(std::string text) const;

    private:
        std::filesystem::path sourceRoot_;
        std::filesystem::path source_;
        std::string logicalName_;
        std::string component_;
        ContentDependencyCollector* dependencies_ = nullptr;
        ContentBuildLogger* logger_ = nullptr;
    };

    /** @brief Focused, call-scoped services available to a Content Processor. */
    class ContentProcessorContext
    {
    public:
        /**
         * @brief Creates one processor invocation context.
         *
         * @param sourceRoot Canonical content source root.
         * @param source Canonical primary source path.
         * @param logicalName Logical ContentManager asset name.
         * @param component Stable processor name for logs.
         * @param parameters Ordered processor parameters.
         * @param dependencies Per-build dependency collector.
         * @param logger Scoped logger.
         */
        ContentProcessorContext(std::filesystem::path sourceRoot, std::filesystem::path source,
                                std::string logicalName, std::string component,
                                const ContentProcessorParameters& parameters,
                                ContentDependencyCollector& dependencies,
                                ContentBuildLogger& logger);

        /** @brief Processor contexts are call-scoped and cannot be copied. */
        ContentProcessorContext(const ContentProcessorContext&) = delete;

        /** @brief Processor contexts are call-scoped and cannot be assigned. */
        ContentProcessorContext& operator=(const ContentProcessorContext&) = delete;

        /** @brief Returns the logical ContentManager asset name. */
        [[nodiscard]] const std::string& LogicalName() const noexcept;

        /** @brief Returns the ordered processor parameters. */
        [[nodiscard]] const ContentProcessorParameters& Parameters() const noexcept;

        /**
         * @brief Resolves and records a processor-read source dependency.
         *
         * @param authoredPath Relative path resolved from the primary source's directory.
         * @return Canonical contained native path.
         * @throws std::invalid_argument for absolute paths or source-root escapes.
         */
        [[nodiscard]] std::filesystem::path ResolveSourceDependency(
            const std::filesystem::path& authoredPath);

        /**
         * @brief Records a logical content-to-content build dependency.
         *
         * @param logicalName Safe logical content name.
         * @throws std::invalid_argument if the name is unsafe.
         */
        void AddContentBuildDependency(std::string logicalName);

        /**
         * @brief Records a generated file dependency contained by the source root.
         *
         * @param generatedPath Native generated path.
         * @throws std::invalid_argument if the path escapes the source root.
         */
        void AddGeneratedDependency(const std::filesystem::path& generatedPath);

        /**
         * @brief Records a runtime content reference separately from build dependencies.
         *
         * @param logicalName Safe ContentManager logical name.
         * @param expectedAssetTypeId Expected CNB type, or zero when unconstrained.
         */
        void AddRuntimeReference(std::string logicalName,
                                 std::uint32_t expectedAssetTypeId = 0u);

        /**
         * @brief Emits an informational processor message.
         *
         * @param text Message text.
         */
        void LogInfo(std::string text) const;

        /**
         * @brief Emits a processor warning.
         *
         * @param text Message text.
         */
        void LogWarning(std::string text) const;

    private:
        std::filesystem::path sourceRoot_;
        std::filesystem::path source_;
        std::string logicalName_;
        std::string component_;
        const ContentProcessorParameters* parameters_ = nullptr;
        ContentDependencyCollector* dependencies_ = nullptr;
        ContentBuildLogger* logger_ = nullptr;
    };

    /** @brief Experimental build-time source importer contract. */
    class ContentImporter
    {
    public:
        /** @brief Enables correct destruction through the importer interface. */
        virtual ~ContentImporter() = default;

        /** @brief Returns the importer's stable name and build version. */
        [[nodiscard]] virtual ContentComponentIdentity Identity() const = 0;

        /** @brief Returns supported lowercase source extensions including the leading dot. */
        [[nodiscard]] virtual std::vector<std::string> SourceExtensions() const = 0;

        /**
         * @brief Returns the bounded stable type identities this importer may produce.
         *
         * Most source formats return one type. A self-describing container such as CNJ may return
         * one of several explicitly declared types after validating its envelope. The list is a
         * capability declaration, not a selection priority, and must contain no duplicates.
         *
         * @return Non-empty stable ABI-independent imported type identities.
         */
        [[nodiscard]] virtual std::vector<std::string> OutputTypes() const = 0;

        /**
         * @brief Imports the context's primary source into a source-oriented value.
         *
         * @param context Call-scoped importer services.
         * @return Imported value whose stable type is declared by OutputTypes().
         */
        [[nodiscard]] virtual ContentValue Import(ContentImporterContext& context) const = 0;
    };

    /** @brief Experimental build-time content processor contract. */
    class ContentProcessor
    {
    public:
        /** @brief Enables correct destruction through the processor interface. */
        virtual ~ContentProcessor() = default;

        /** @brief Returns the processor's stable name and build version. */
        [[nodiscard]] virtual ContentComponentIdentity Identity() const = 0;

        /** @brief Returns the stable imported type accepted by this processor. */
        [[nodiscard]] virtual std::string InputType() const = 0;

        /** @brief Returns the stable processed type produced by this processor. */
        [[nodiscard]] virtual std::string OutputType() const = 0;

        /**
         * @brief Validates configured parameters before any content transformation occurs.
         *
         * @param parameters Parameters to validate.
         * @throws std::invalid_argument for unknown, mistyped or invalid values.
         */
        virtual void ValidateParameters(const ContentProcessorParameters& parameters) const = 0;

        /**
         * @brief Transforms imported data into runtime-oriented processed content.
         *
         * @param input Imported value whose stable type equals InputType().
         * @param context Call-scoped processor services and validated parameters.
         * @return Processed value whose stable type equals OutputType().
         */
        [[nodiscard]] virtual ContentValue Process(const ContentValue& input,
                                                   ContentProcessorContext& context) const = 0;
    };

    /** @brief Maximum number of primary and additional CNB outputs from one build node. */
    inline constexpr std::size_t MaxContentBuildOutputs = 256u;

    /** @brief One explicitly named additional CNB output produced beside a primary output. */
    struct ContentAdditionalWriteOutput
    {
        /** @brief Complete logical ContentManager name and stable output identity. */
        std::string logicalName;

        /** @brief Complete CNB file image. */
        std::vector<std::uint8_t> bytes;

        /** @brief CNB asset type identifier written by the authoritative encoder. */
        std::uint32_t assetTypeId = 0u;

        /** @brief Canonical runtime type name used in diagnostics. */
        std::string assetTypeName;
    };

    /** @brief Primary CNB output and any bounded, explicitly named additional outputs. */
    struct ContentWriteResult
    {
        /** @brief Complete primary CNB file image. */
        std::vector<std::uint8_t> bytes;

        /** @brief Primary CNB asset type identifier written by the authoritative encoder. */
        std::uint32_t assetTypeId = 0u;

        /** @brief Primary canonical runtime type name used in diagnostics. */
        std::string assetTypeName;

        /** @brief Additional outputs whose logical names are distinct from the primary asset. */
        std::vector<ContentAdditionalWriteOutput> additionalOutputs;
    };

    /** @brief Experimental pipeline writer contract above the low-level CNB codecs. */
    class ContentTypeWriter
    {
    public:
        /** @brief Enables correct destruction through the writer interface. */
        virtual ~ContentTypeWriter() = default;

        /** @brief Returns the writer's stable name and build version. */
        [[nodiscard]] virtual ContentComponentIdentity Identity() const = 0;

        /** @brief Returns the stable processed type accepted by this writer. */
        [[nodiscard]] virtual std::string InputType() const = 0;

        /**
         * @brief Delegates processed content to its authoritative typed CNB encoder.
         *
         * @param input Processed value whose stable type equals InputType().
         * @param logicalName Logical name recorded in CNB metadata.
         * @return Primary CNB bytes/identity and at most MaxContentBuildOutputs minus one
         *         explicitly named additional outputs.
         */
        [[nodiscard]] virtual ContentWriteResult Write(const ContentValue& input,
                                                       const std::string& logicalName) const = 0;
    };

    /**
     * @brief Explicit, deterministic registry of importers, processors and writers.
     *
     * Registration order never resolves an ambiguity. Components are selected by stable names and
     * stable type identities; C++ RTTI does not participate in lookup.
     */
    class ContentPipelineRegistry
    {
    public:
        /**
         * @brief Registers one importer owned by this registry.
         *
         * @param importer Non-null component.
         * @throws std::invalid_argument for malformed identity/type/extension declarations.
         * @throws std::logic_error when the stable importer name is already registered.
         */
        void RegisterImporter(std::shared_ptr<const ContentImporter> importer);

        /**
         * @brief Registers one processor owned by this registry.
         *
         * @param processor Non-null component.
         * @throws std::invalid_argument for malformed identity/type declarations.
         * @throws std::logic_error when the stable processor name is already registered.
         */
        void RegisterProcessor(std::shared_ptr<const ContentProcessor> processor);

        /**
         * @brief Registers one writer owned by this registry.
         *
         * @param writer Non-null component.
         * @throws std::invalid_argument for malformed identity/type declarations.
         * @throws std::logic_error when the stable writer name is already registered.
         */
        void RegisterWriter(std::shared_ptr<const ContentTypeWriter> writer);

        /**
         * @brief Resolves an importer by source extension and optional explicit stable name.
         *
         * @param source Source path whose extension drives default selection.
         * @param explicitName Stable importer override, or empty for default selection.
         * @return Selected importer.
         * @throws std::logic_error for unknown, incompatible or ambiguous selection.
         */
        [[nodiscard]] std::shared_ptr<const ContentImporter> ResolveImporter(
            const std::filesystem::path& source, const std::string& explicitName = {}) const;

        /**
         * @brief Returns whether any importer declares the source extension.
         *
         * This is used by convention-based directory discovery to ignore support files such as
         * glTF `.bin` buffers. It does not promise unambiguous resolution; ResolveImporter()
         * remains authoritative and diagnoses duplicate routes.
         *
         * @param source Source path whose extension is queried case-insensitively.
         * @return True when at least one importer declares the extension.
         */
        [[nodiscard]] bool HasImporterForSource(const std::filesystem::path& source) const;

        /**
         * @brief Resolves a processor by stable imported type and optional explicit name.
         *
         * @param inputType Stable imported type identity.
         * @param explicitName Stable processor override, or empty for default selection.
         * @return Selected processor.
         * @throws std::logic_error for unknown, incompatible or ambiguous selection.
         */
        [[nodiscard]] std::shared_ptr<const ContentProcessor> ResolveProcessor(
            const std::string& inputType, const std::string& explicitName = {}) const;

        /**
         * @brief Resolves a writer by stable processed type and optional explicit name.
         *
         * @param inputType Stable processed type identity.
         * @param explicitName Stable writer override, or empty for default selection.
         * @return Selected writer.
         * @throws std::logic_error for unknown, incompatible or ambiguous selection.
         */
        [[nodiscard]] std::shared_ptr<const ContentTypeWriter> ResolveWriter(
            const std::string& inputType, const std::string& explicitName = {}) const;

    private:
        std::map<std::string, std::shared_ptr<const ContentImporter>> importers_;
        std::map<std::string, std::shared_ptr<const ContentProcessor>> processors_;
        std::map<std::string, std::shared_ptr<const ContentTypeWriter>> writers_;
        std::map<std::string, std::set<std::string>> importersByExtension_;
        std::map<std::string, std::set<std::string>> processorsByInputType_;
        std::map<std::string, std::set<std::string>> writersByInputType_;
    };

    /** @brief One request to run Importer -> Processor -> Writer without publishing a file. */
    struct ContentBuildRequest
    {
        /** @brief Root all source reads must remain inside. */
        std::filesystem::path sourceRoot;

        /** @brief Primary source, absolute or relative to sourceRoot. */
        std::filesystem::path source;

        /** @brief Logical ContentManager asset name written into CNB metadata. */
        std::string logicalName;

        /** @brief Optional stable importer override. */
        std::string importer;

        /** @brief Optional stable processor override. */
        std::string processor;

        /** @brief Optional stable writer override. */
        std::string writer;

        /** @brief Processor parameters included in the effective build identity. */
        ContentProcessorParameters parameters;

        /** @brief Optional scoped logger; null selects a no-op logger. */
        ContentBuildLogger* logger = nullptr;
    };

    /** @brief Complete observable result of one in-memory content build. */
    struct ContentBuildResult
    {
        /** @brief Canonical primary source path. */
        std::filesystem::path source;

        /** @brief Logical ContentManager asset name. */
        std::string logicalName;

        /** @brief Importer identity used for this build. */
        ContentComponentIdentity importer;

        /** @brief Processor identity used for this build. */
        ContentComponentIdentity processor;

        /** @brief Writer identity used for this build. */
        ContentComponentIdentity writer;

        /** @brief Effective processor parameters. */
        ContentProcessorParameters parameters;

        /** @brief Sorted build-time dependencies. */
        std::vector<ContentDependency> dependencies;

        /** @brief Sorted runtime content references, distinct from dependencies. */
        std::vector<RuntimeContentReference> runtimeReferences;

        /** @brief Ordered informational and warning messages emitted by the successful build. */
        std::vector<ContentLogMessage> messages;

        /** @brief Complete primary and additional compiled CNB bytes, not yet published. */
        ContentWriteResult output;

        /** @brief True for the current non-incremental coordinator; future manifests may skip. */
        bool built = true;
    };

    /** @brief Context-rich failure at a specific pipeline boundary. */
    class ContentPipelineError : public std::runtime_error
    {
    public:
        /**
         * @brief Creates a pipeline error while retaining the underlying reason in its message.
         *
         * @param source Primary source path.
         * @param logicalName Logical content name.
         * @param stage Failing pipeline stage.
         * @param component Stable component name, or empty during selection.
         * @param reason Underlying diagnostic text.
         */
        ContentPipelineError(std::filesystem::path source, std::string logicalName,
                             ContentPipelineStage stage, std::string component,
                             std::string reason);

        /** @brief Returns the primary source path associated with the failure. */
        [[nodiscard]] const std::filesystem::path& Source() const noexcept;

        /** @brief Returns the logical content name associated with the failure. */
        [[nodiscard]] const std::string& LogicalName() const noexcept;

        /** @brief Returns the stage where the failure crossed into orchestration. */
        [[nodiscard]] ContentPipelineStage Stage() const noexcept;

        /** @brief Returns the stable component name, or empty for a selection failure. */
        [[nodiscard]] const std::string& Component() const noexcept;

    private:
        std::filesystem::path source_;
        std::string logicalName_;
        ContentPipelineStage stage_ = ContentPipelineStage::Selection;
        std::string component_;
    };

    /** @brief Serial build-to-bytes coordinator over an explicitly configured registry. */
    class ContentPipeline
    {
    public:
        /**
         * @brief Creates a coordinator sharing an explicitly owned registry.
         *
         * @param registry Configured registry that must remain alive for this coordinator.
         */
        explicit ContentPipeline(std::shared_ptr<const ContentPipelineRegistry> registry);

        /**
         * @brief Runs Importer -> Processor -> Writer and returns bytes plus build metadata.
         *
         * @param request Source identity, optional component overrides and processor parameters.
         * @return Observable build result; publication is deliberately a later orchestration step.
         * @throws ContentPipelineError with source/stage/component context on any failure.
         */
        [[nodiscard]] ContentBuildResult Build(const ContentBuildRequest& request) const;

    private:
        std::shared_ptr<const ContentPipelineRegistry> registry_;
    };
}
