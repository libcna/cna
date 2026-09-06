// SPDX-License-Identifier: MS-PL
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeindex>
#include <utility>
#include <variant>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"

namespace CNA::Content::Pipeline
{
    class ContentPipeline;
    class ContentPipelineRegistry;
    struct ContentAdditionalWriteOutput;

    /** @brief Maximum number of named read-only external source roots in one build request. */
    inline constexpr std::size_t MaxContentSourceRoots = 32u;

    /**
     * @brief Returns why a source-root capability alias is invalid.
     *
     * Valid aliases contain 1-64 lowercase ASCII letters, digits, or hyphens, and begin with a
     * letter.
     *
     * @param alias Alias to validate.
     * @return Empty text when valid, otherwise a stable diagnostic fragment.
     */
    [[nodiscard]] std::string ContentSourceRootAliasProblem(const std::string& alias);

    /** @brief Explicit read-only external source-root capabilities keyed by stable alias. */
    class ContentSourceRootCapabilities
    {
    public:
        /**
         * @brief Adds one uniquely named native source root.
         *
         * @param alias Stable logical root alias.
         * @param root Native physical directory, canonicalized when a build starts.
         * @throws std::invalid_argument for an invalid/duplicate alias, empty path, or excess
         *         root count.
         */
        void Add(std::string alias, std::filesystem::path root);

        /**
         * @brief Finds one configured physical root.
         *
         * @param alias Stable root alias.
         * @return Pointer valid for this object's lifetime, or null when absent.
         */
        [[nodiscard]] const std::filesystem::path* Find(const std::string& alias) const;

        /**
         * @brief Returns all roots in deterministic alias order.
         *
         * @return Read-only ordered alias-to-native-path map.
         */
        [[nodiscard]] const std::map<std::string, std::filesystem::path>& Entries() const noexcept;

        /** @brief Returns true when no external source capability is configured. */
        [[nodiscard]] bool Empty() const noexcept;

        /** @brief Compares every alias and native mapping. */
        bool operator==(const ContentSourceRootCapabilities&) const = default;

    private:
        std::map<std::string, std::filesystem::path> entries_;
    };

    /**
     * @brief Canonicalizes and validates a request's read-only external source roots.
     *
     * Relative external roots are resolved against @p sourceRoot. Every root must exist as a
     * directory, and no source/external root may equal or contain another.
     *
     * @param sourceRoot Primary content source root.
     * @param configured Request-local physical capability mappings.
     * @return Canonical primary-independent capability mappings.
     * @throws std::invalid_argument for missing/file roots, duplicate physical roots, or overlap.
     */
    [[nodiscard]] ContentSourceRootCapabilities ResolveContentSourceRootCapabilities(
        const std::filesystem::path& sourceRoot,
        const ContentSourceRootCapabilities& configured);

    /** @brief Stability marker for the initial custom C++ pipeline component API. */
    inline constexpr bool ContentPipelineExtensionApiIsExperimental = true;

    /**
     * @brief The compiled container one build node emits
     *        (plans/plan_xnapipeline.md `XNAP-60`).
     *
     * The format is chosen at the writer boundary and nowhere else: importers, processors and the
     * canonical values between them are format-neutral, so a new source route reaches both outputs
     * at once and neither container constrains the other.
     */
    enum class ContentOutputFormat
    {
        /** @brief CNA's own native compiled container. The default. */
        Cnb,
        /** @brief The XNA-compatible `.xnb` container. */
        Xnb,
    };

    /**
     * @brief Returns the stable lowercase configuration/CLI spelling of an output format.
     *
     * @param format The output format.
     * @return A process-lifetime string literal, `"cnb"` or `"xnb"`.
     */
    [[nodiscard]] const char* ContentOutputFormatName(ContentOutputFormat format) noexcept;

    /**
     * @brief Returns the published artifact extension for an output format.
     *
     * @param format The output format.
     * @return A process-lifetime string literal including the leading dot.
     */
    [[nodiscard]] const char* ContentOutputFormatExtension(ContentOutputFormat format) noexcept;

    /**
     * @brief Parses the stable lowercase spelling of an output format.
     *
     * @param name Configuration or command-line spelling.
     * @param format Receives the parsed format when parsing succeeds.
     * @return True when @p name named a supported format.
     */
    [[nodiscard]] bool TryParseContentOutputFormat(const std::string& name,
                                                   ContentOutputFormat& format);

    /**
     * @brief The XNA 4.0 platform a build produces content for
     *        (plans/plan_xnapipeline_parity.md `XNAPP-040`).
     *
     * Format-neutral: a processor may consult it to take a platform-specific decision (texture
     * format, profile limits) whatever container the build writes. The XNB writer maps it onto
     * its own platform byte.
     */
    enum class ContentTargetPlatform
    {
        /** @brief Windows desktop. The default. */
        Windows,
        /** @brief Xbox 360. */
        Xbox360,
        /** @brief Windows Phone 7. */
        WindowsPhone,
    };

    /**
     * @brief Returns the XNA spelling of a target platform (`Windows`, `Xbox360`, `WindowsPhone`).
     *
     * @param platform The platform.
     * @return A process-lifetime string literal.
     */
    [[nodiscard]] const char* ContentTargetPlatformName(ContentTargetPlatform platform) noexcept;

    /**
     * @brief Host-level facts about a build that any component may consult
     *        (plans/plan_xnapipeline_parity.md `XNAPP-040`).
     *
     * These are the values XNA exposes on its processor context. None of them changes what a
     * built-in CNA component does today; they exist so a component written against the XNA shape
     * can ask the questions XNA lets it ask. Every field has the default XNA's own tooling uses
     * for a Windows build.
     */
    struct ContentBuildEnvironment
    {
        /** @brief Platform the content is built for. */
        ContentTargetPlatform targetPlatform = ContentTargetPlatform::Windows;

        /** @brief Graphics profile the content must respect. */
        Microsoft::Xna::Framework::Graphics::GraphicsProfile targetProfile =
            Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach;

        /** @brief Build configuration name, as MSBuild's `$(Configuration)`; `Release` by default. */
        std::string buildConfiguration = "Release";

        /** @brief Directory compiled artifacts are published to, or empty when not yet decided. */
        std::filesystem::path outputDirectory;

        /** @brief Directory a component may use for scratch files, or empty for none. */
        std::filesystem::path intermediateDirectory;

        /** @brief Compares every field. */
        bool operator==(const ContentBuildEnvironment&) const = default;
    };

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

    /** @brief Stable native asset/schema/codec identity declared by a content writer. */
    struct ContentWriterSchemaIdentity
    {
        /** @brief Stable nonzero CNB asset type identifier emitted by the writer. */
        std::uint32_t assetTypeId = 0u;

        /** @brief Stable nonzero CNB asset schema version emitted for this asset type. */
        std::uint32_t assetSchemaVersion = 0u;

        /** @brief Canonical runtime type name carried by CNB metadata. */
        std::string assetTypeName;

        /** @brief Stable codec name/version, changed when same-schema output semantics change. */
        ContentComponentIdentity codec;

        /**
         * @brief Compares the complete persistent writer schema identity.
         * @param other Identity to compare.
         * @return True when every asset, schema and codec field matches.
         */
        bool operator==(const ContentWriterSchemaIdentity& other) const = default;
    };

    /** @brief Pipeline stages reported by diagnostics and build logging. */
    enum class ContentPipelineStage
    {
        /** @brief Component or route selection. */
        Selection,

        /** @brief Source import and validation. */
        Import,

        /** @brief Imported-value processing. */
        Process,

        /** @brief CNB writer adaptation. */
        Write,

        /** @brief Build-graph scheduling and dependency resolution. */
        Graph,

        /** @brief Atomic artifact or manifest publication. */
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
        /** @brief Detail a build tool shows only when asked for it. */
        Info,
        /**
         * @brief Something the author asked to be told, shown at ordinary verbosity.
         *
         * XNA's `ContentBuildLogger` has two message levels and the distinction is the whole point
         * of the second: `LogImportantMessage` is documented as reaching the user even at low
         * verbosity, and a component uses it for what the author needs to see. Without a level
         * between `Info` and `Warning`, a user's important message either vanishes with the
         * chatter or is dressed up as a warning it is not
         * (plans/plan_xnapipeline_parity.md `XNAPP-260`).
         */
        Important,
        /** @brief Something the author lost, or is about to. */
        Warning,
        /** @brief Something that stopped the build. */
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

    /**
     * @brief Scoped logging sink for content builds.
     *
     * A sink reused by concurrent Build() calls may receive concurrent Log() calls and must
     * synchronize its own mutable state. The stock compiler does not share a downstream logger
     * between build calls; graph diagnostics remain coordinator-owned.
     */
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

        /** @brief External source-root alias, or empty for the primary source root/non-file edge. */
        std::string sourceRoot;

        /** @brief Orders records deterministically by category, source root, and identity. */
        bool operator<(const ContentDependency& other) const noexcept;

        /** @brief Compares dependency category, source root, and identity. */
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

    /** @brief One source file copied as a non-CNB deployment artifact beside compiled content. */
    struct ContentDeploymentFile
    {
        /** @brief Canonical native source path contained by the build's source root. */
        std::filesystem::path source;

        /** @brief Generic UTF-8 destination path relative to the content output root. */
        std::string outputPath;

        /** @brief External source-root alias, or empty for a primary-root source. */
        std::string sourceRoot;

        /** @brief Compares the source and destination identities. */
        bool operator==(const ContentDeploymentFile&) const = default;
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
         * @brief Adds one validated deployment file under its unique output path.
         *
         * @param file Canonical source and contained output-relative destination.
         * @throws std::invalid_argument when the output path is already mapped to another source.
         */
        void AddDeploymentFile(ContentDeploymentFile file);

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

        /**
         * @brief Returns deployment files in deterministic output-path order.
         *
         * @return A sorted copy of the collected deployment files.
         */
        [[nodiscard]] std::vector<ContentDeploymentFile> DeploymentFiles() const;

    private:
        std::set<ContentDependency> dependencies_;
        std::set<RuntimeContentReference> runtimeReferences_;
        std::map<std::string, ContentDeploymentFile> deploymentFiles_;
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
         * @brief Returns the process-local C++ type of the stored value, for a component that
         *        dispatches on it without knowing @p T statically (the XNA-shaped
         *        `ContentCompiler`, plans/plan_xnapipeline_parity.md `XNAPP-062`).
         *
         * Never serialized: it identifies a type only within this process.
         *
         * @return The stored value's `std::type_index`, or `typeid(void)` when empty.
         */
        [[nodiscard]] std::type_index CppType() const noexcept;

        /**
         * @brief Returns the erased address of the stored value, valid while this object lives.
         *
         * The pointer is meaningful only together with CppType(); a caller casts it to that type
         * and to nothing else.
         *
         * @return The stored value, or null when empty.
         */
        [[nodiscard]] const void* RawData() const noexcept;

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
         * @param externalSourceRoots Canonical request-local read capabilities.
         * @param dependencies Per-build dependency collector.
         * @param logger Scoped logger.
         */
        ContentImporterContext(std::filesystem::path sourceRoot, std::filesystem::path source,
                               std::string logicalName, std::string component,
                               const ContentSourceRootCapabilities& externalSourceRoots,
                               ContentDependencyCollector& dependencies,
                               ContentBuildLogger& logger,
                               ContentBuildEnvironment environment = {});

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

        /** @brief Returns the host-level build facts (platform, profile, configuration, directories). */
        [[nodiscard]] const ContentBuildEnvironment& Environment() const noexcept;

        /**
         * @brief Resolves and records a file dependency relative to the primary source.
         *
         * `@alias/root-relative-path` explicitly selects one configured external source root;
         * unqualified paths remain relative to the primary source asset.
         *
         * @param authoredPath Relative path read from source content.
         * @return Canonical contained native path.
         * @throws std::invalid_argument for absolute paths, unknown aliases, or root escapes.
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
         * @brief Records a message the author asked to be told, at ordinary verbosity.
         *
         * @param text The message.
         */
        void LogImportant(std::string text) const;

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
        const ContentSourceRootCapabilities* externalSourceRoots_ = nullptr;
        ContentBuildEnvironment environment_;
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
         * @param externalSourceRoots Canonical request-local read capabilities.
         * @param dependencies Per-build dependency collector.
         * @param logger Scoped logger.
         * @param outputFormat Compiled container this build is producing.
         * @param environment Host-level build facts; defaults describe a Windows/Reach build.
         * @param pipeline The coordinator running this build, so a processor can request a
         *        nested in-process build (plans/plan_xnapipeline_parity.md `XNAPP-044`); null
         *        when the context is constructed outside a coordinator.
         */
        ContentProcessorContext(std::filesystem::path sourceRoot, std::filesystem::path source,
                                std::string logicalName, std::string component,
                                const ContentProcessorParameters& parameters,
                                const ContentSourceRootCapabilities& externalSourceRoots,
                                ContentDependencyCollector& dependencies,
                                ContentBuildLogger& logger,
                                ContentOutputFormat outputFormat = ContentOutputFormat::Cnb,
                                ContentBuildEnvironment environment = {},
                                const ContentPipeline* pipeline = nullptr);

        /** @brief Processor contexts are call-scoped and cannot be copied. */
        ContentProcessorContext(const ContentProcessorContext&) = delete;

        /** @brief Processor contexts are call-scoped and cannot be assigned. */
        ContentProcessorContext& operator=(const ContentProcessorContext&) = delete;

        /** @brief Returns the logical ContentManager asset name. */
        [[nodiscard]] const std::string& LogicalName() const noexcept;

        /** @brief Returns the canonical source root. */
        [[nodiscard]] const std::filesystem::path& SourceRoot() const noexcept;

        /** @brief Returns the canonical primary source path. */
        [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept;

        /** @brief Returns the host-level build facts (platform, profile, configuration, directories). */
        [[nodiscard]] const ContentBuildEnvironment& Environment() const noexcept;

        /** @brief Returns the external source-root capabilities this build was given. */
        [[nodiscard]] const ContentSourceRootCapabilities& ExternalSourceRoots() const noexcept;

        /** @brief Returns the dependency collector, so a nested build can merge its edges into this one. */
        [[nodiscard]] ContentDependencyCollector& Dependencies() const noexcept;

        /** @brief Returns the scoped logger this context reports through. */
        [[nodiscard]] ContentBuildLogger& Logger() const noexcept;

        /**
         * @brief Returns the coordinator running this build, or null outside a coordinator.
         *
         * A processor that needs another asset built in-process -- XNA's `BuildAsset`,
         * `BuildAndLoadAsset` and `Convert` -- goes through this rather than constructing a
         * second pipeline, so the nested build shares the registry, the components and their
         * frozen state.
         */
        [[nodiscard]] const ContentPipeline* Pipeline() const noexcept;

        /** @brief Returns the ordered processor parameters. */
        [[nodiscard]] const ContentProcessorParameters& Parameters() const noexcept;

        /**
         * @brief Returns the compiled container this build is producing.
         *
         * A processor's job is to produce one canonical value, and almost none of them need
         * this. It exists for the cases where the two containers genuinely cannot carry the same
         * thing -- a block-compressed texture has an XNB `SurfaceFormat` but no representation in
         * the frozen CNB texture schema -- so a processor can take the documented fallback and
         * say so, instead of failing late inside a codec.
         *
         * @return The requested output format.
         */
        [[nodiscard]] ContentOutputFormat OutputFormat() const noexcept;

        /**
         * @brief Resolves and records a processor-read source dependency.
         *
         * `@alias/root-relative-path` explicitly selects one configured external source root;
         * unqualified paths remain relative to the primary source asset.
         *
         * @param authoredPath Relative path resolved from the primary source's directory.
         * @return Canonical contained native path.
         * @throws std::invalid_argument for absolute paths, unknown aliases, or root escapes.
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
         * @brief Registers a contained source file for atomic deployment below the output root.
         *
         * The source is also recorded as a byte-hashed source dependency unless it is the primary
         * source itself. An external source is accepted only after this build explicitly resolved
         * that exact alias-qualified dependency.
         *
         * @param sourcePath Native absolute path, or a path relative to the source root.
         * @param outputPath Safe generic UTF-8 path relative to the content output root.
         * @throws std::invalid_argument if either path escapes its root, the source is not a
         * regular file, or the destination conflicts with an earlier deployment file.
         */
        void AddDeploymentFile(const std::filesystem::path& sourcePath,
                               std::string outputPath);

        /**
         * @brief Emits an informational processor message.
         *
         * @param text Message text.
         */
        void LogInfo(std::string text) const;

        /**
         * @brief Records a message the author asked to be told, at ordinary verbosity.
         *
         * @param text The message.
         */
        void LogImportant(std::string text) const;

        /**
         * @brief Emits a processor warning.
         *
         * @param text Message text.
         */
        void LogWarning(std::string text) const;

        /**
         * @brief Adds a compiled asset a nested build produced, to be published as an additional
         *        output of the current node (plans/plan_xnapipeline_parity.md `XNAPP-044`).
         *
         * This is how XNA's `ContentProcessorContext.BuildAsset` reaches the canonical
         * multi-output path: the nested build ran through the same pipeline, its bytes and
         * identity are complete, and `ContentPipeline::Build()` appends them after the current
         * writer's own outputs -- so they are owned, fingerprinted and cleaned like any other
         * artifact. A name that collides with the primary output or another additional output is
         * refused when the build completes.
         *
         * @param output Complete compiled output with a distinct logical name.
         * @throws std::invalid_argument for an empty logical name or empty bytes.
         */
        void AddNestedOutput(ContentAdditionalWriteOutput output);

        /**
         * @brief Returns the nested outputs added so far, in the order they were added.
         *
         * @return The outputs; the coordinator moves them into the build result.
         */
        [[nodiscard]] const std::vector<ContentAdditionalWriteOutput>& NestedOutputs() const noexcept;

        /**
         * @brief Records the writer schemas a nested build's own writer declared.
         *
         * A nested output was written by a different writer than this node's, so its schema is not
         * one this node's writer declares. Both the manifest and the incremental check read the
         * node's schema list -- the first to recognise every output it publishes, the second to
         * invalidate the node when a schema it depends on changes -- so a nested build's schemas
         * have to join that list or the node publishes an output it cannot describe and survives a
         * change to the codec that wrote it (plans/plan_xnapipeline_parity.md `XNAPP-021`).
         *
         * Duplicates are ignored, so repeating a schema across nested builds is harmless.
         *
         * @param schemas The nested build's declared writer schemas.
         */
        void AddNestedWriterSchemas(const std::vector<ContentWriterSchemaIdentity>& schemas);

        /**
         * @brief Returns the nested writer schemas added so far, in the order they were added.
         *
         * @return The schemas; the coordinator merges them into the node's own list.
         */
        [[nodiscard]] const std::vector<ContentWriterSchemaIdentity>& NestedWriterSchemas() const noexcept;

    private:
        friend class ContentPipeline;
        std::filesystem::path sourceRoot_;
        std::filesystem::path source_;
        std::string logicalName_;
        std::string component_;
        const ContentProcessorParameters* parameters_ = nullptr;
        ContentDependencyCollector* dependencies_ = nullptr;
        ContentBuildLogger* logger_ = nullptr;
        const ContentSourceRootCapabilities* externalSourceRoots_ = nullptr;
        ContentOutputFormat outputFormat_ = ContentOutputFormat::Cnb;
        ContentBuildEnvironment environment_;
        const ContentPipeline* pipeline_ = nullptr;
        std::vector<ContentAdditionalWriteOutput> nestedOutputs_;
        std::vector<ContentWriterSchemaIdentity> nestedWriterSchemas_;
    };

    /**
     * @brief Experimental build-time source importer contract.
     *
     * One registered instance may serve concurrent build nodes after the registry is frozen.
     * Implementations must therefore be reentrant or internally synchronize mutable state.
     */
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
         * @brief Returns the stable name of the processor used when a build names none, or
         *        empty to keep the registry's own default resolution
         *        (plans/plan_xnapipeline_parity.md `XNAPP-038`).
         *
         * XNA's `ContentImporterAttribute.DefaultProcessor`: the importer, not the registry,
         * knows which processor its output is meant for when several accept the same type. Every
         * built-in CNA importer returns empty, so their resolution is unchanged.
         *
         * @return A registered processor name, or empty.
         */
        [[nodiscard]] virtual std::string DefaultProcessor() const { return {}; }

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

    /**
     * @brief Experimental build-time content processor contract.
     *
     * One registered instance may serve concurrent build nodes after the registry is frozen.
     * Implementations must therefore be reentrant or internally synchronize mutable state.
     */
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

        /**
         * @brief Whether this processor is chosen only when a build names it.
         *
         * A processor that answers true is left out of default resolution: it still runs when a
         * build, or a component starting a nested build, asks for it by name, and it never becomes
         * the answer to "which processor handles this imported type" on its own.
         *
         * This exists for a processor registered under a name some other component reaches it by
         * rather than as a route of its own -- the XNA-named `TextureProcessor` that `XNA`'s
         * `MaterialProcessor` builds a model's textures through, which accepts imported images and
         * must not therefore start competing with the built-in texture route for every `.png` in
         * the tree (plans/plan_xnapipeline_parity.md `XNAPP-021`).
         *
         * @return false for every ordinary processor.
         */
        [[nodiscard]] virtual bool SelectedByNameOnly() const { return false; }
    };

    /** @brief Maximum number of primary and additional CNB outputs from one build node. */
    inline constexpr std::size_t MaxContentBuildOutputs = 256u;

    /** @brief Maximum number of non-CNB deployment files owned by one build node. */
    inline constexpr std::size_t MaxContentDeploymentFiles = 256u;

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

        /** @brief Asset schema version emitted in the CNB header. */
        std::uint32_t assetSchemaVersion = 0u;

        /**
         * @brief Root `ContentTypeReader` name for an `.xnb` output; empty for a CNB one.
         *
         * A compiled `.xnb` carries no asset type id and no schema version, so the reader its root
         * object dispatches to is its compatibility identity and what the build manifest records
         * (plans/plan_xnapipeline.md `XNAP-99`). A CNB writer leaves this empty.
         */
        std::string rootReaderName;

        /**
         * @brief Whether a nested build produced this output rather than this node's own writer.
         *
         * The two are published the same way and differ in one respect that matters: a writer's
         * additional output is this node's product and its name is this node's to claim, while a
         * nested build's output is a copy of an asset another node may already own -- a model's
         * materials build the textures they name, and a project usually lists those textures as
         * items of its own. In-memory only; the manifest records what was published, not who asked
         * for it (plans/plan_xnapipeline_parity.md `XNAPP-021`).
         */
        bool fromNestedBuild = false;
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

        /** @brief Primary asset schema version emitted in the CNB header. */
        std::uint32_t assetSchemaVersion = 0u;

        /**
         * @brief Root `ContentTypeReader` name for an `.xnb` output; empty for a CNB one.
         *
         * A compiled `.xnb` carries no asset type id and no schema version, so the reader its root
         * object dispatches to is its compatibility identity and what the build manifest records
         * (plans/plan_xnapipeline.md `XNAP-99`). A CNB writer leaves this empty.
         */
        std::string rootReaderName;

        /** @brief Additional outputs whose logical names are distinct from the primary asset. */
        std::vector<ContentAdditionalWriteOutput> additionalOutputs;

        /**
         * @brief Documented losses the writer took, in the order it took them.
         *
         * A writer that cannot represent something the processed value carries has three honest
         * options: refuse, represent it exactly, or state precisely what it dropped. This field is
         * the third. `ContentPipeline::Build()` forwards each entry to the build log as a warning
         * against the writer's own component name, so an author is told what an output format cost
         * them rather than discovering it at run time.
         */
        std::vector<std::string> warnings;
    };

    /**
     * @brief Experimental pipeline writer contract above the low-level CNB codecs.
     *
     * One registered instance may serve concurrent build nodes after the registry is frozen.
     * Implementations must therefore be reentrant or internally synchronize mutable state.
     */
    class ContentTypeWriter
    {
    public:
        /** @brief Enables correct destruction through the writer interface. */
        virtual ~ContentTypeWriter() = default;

        /** @brief Returns the writer's stable name and build version. */
        [[nodiscard]] virtual ContentComponentIdentity Identity() const = 0;

        /**
         * @brief Returns the compiled container this writer emits.
         *
         * Defaulted to @ref ContentOutputFormat::Cnb so an existing custom writer keeps working
         * unchanged; a writer that emits `.xnb` overrides it. Writer selection is keyed by the
         * pair (format, input type), so one processed type may legitimately have one writer per
         * format.
         *
         * @return The container this writer produces.
         */
        [[nodiscard]] virtual ContentOutputFormat OutputFormat() const
        {
            return ContentOutputFormat::Cnb;
        }

        /**
         * @brief Declares every stable asset/schema/codec identity this writer can emit.
         *
         * The result must be nonempty, strictly ordered by asset type ID, canonical type name,
         * then schema version, and contain at most one entry for each such tuple. The build cache
         * records this declaration before invoking the writer, so schema or codec evolution
         * invalidates old output even when the writer component version was accidentally left
         * unchanged.
         *
         * @return Immutable author-controlled identities independent of C++ RTTI.
         */
        [[nodiscard]] virtual std::vector<ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const = 0;

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

        /**
         * @brief Writes with the build's host-level facts available
         *        (plans/plan_xnapipeline_parity.md `XNAPP-063`).
         *
         * The coordinator calls this form; the default forwards to Write() so every existing
         * writer is unchanged. A writer whose bytes depend on the environment -- an XNB object
         * writer spelling external references relative to the asset's output location -- overrides
         * it.
         *
         * @param input Processed value whose stable type equals InputType().
         * @param logicalName Logical name recorded in the output.
         * @param environment The build's platform, profile, configuration and directories.
         * @return The same result Write() returns.
         */
        [[nodiscard]] virtual ContentWriteResult Write(const ContentValue& input,
                                                       const std::string& logicalName,
                                                       const ContentBuildEnvironment& environment) const
        {
            (void)environment;
            return Write(input, logicalName);
        }
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
         * @brief Permanently seals this registry for concurrent read-only build use.
         *
         * The operation is idempotent. ContentPipeline and RunContentCompiler call it before
         * build work begins, so callers normally only need it when they want an explicit
         * configure-then-freeze boundary before constructing either coordinator.
         */
        void Freeze() const;

        /**
         * @brief Returns whether this registry has been permanently sealed.
         *
         * @return True after Freeze() or after a coordinator has accepted this registry.
         */
        [[nodiscard]] bool IsFrozen() const noexcept;

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
         * @brief Resolves a writer by output format, stable processed type and optional name.
         *
         * @param inputType Stable processed type identity.
         * @param explicitName Stable writer override, or empty for default selection.
         * @param format Compiled container the selected writer must emit.
         * @return Selected writer.
         * @throws std::logic_error for unknown, incompatible or ambiguous selection.
         */
        [[nodiscard]] std::shared_ptr<const ContentTypeWriter> ResolveWriter(
            const std::string& inputType, const std::string& explicitName = {},
            ContentOutputFormat format = ContentOutputFormat::Cnb) const;

        /**
         * @brief Returns every registered importer, ordered by stable name.
         *
         * The pipeline's own routing never needs this: it resolves by extension. It exists so
         * that the *inventory* of what a configuration can build can be derived from the registry
         * rather than restated by hand somewhere it can go stale
         * (plans/plan_xnapipeline.md `XNAP-61`).
         *
         * @return Every importer, ordered by identity name.
         */
        [[nodiscard]] std::vector<std::shared_ptr<const ContentImporter>> Importers() const;

        /**
         * @brief Returns every registered processor, ordered by stable name.
         *
         * @return Every processor, ordered by identity name.
         */
        [[nodiscard]] std::vector<std::shared_ptr<const ContentProcessor>> Processors() const;

        /**
         * @brief Returns every registered writer, ordered by stable name.
         *
         * @return Every writer, ordered by identity name.
         */
        [[nodiscard]] std::vector<std::shared_ptr<const ContentTypeWriter>> Writers() const;

        /**
         * @brief Records why one container deliberately has no writer for a processed type
         *        (plans/plan_xnapipeline.md `XNAP-61`).
         *
         * A missing writer and a *deliberately absent* one produce the same failure otherwise --
         * "no writer is registered" -- which tells a user nothing about whether they hit an
         * omission or a decision. Registering the reason makes ResolveWriter() say which, and
         * makes the decision enumerable rather than something a reader has to find in a plan.
         *
         * @param format Container that has no writer for @p inputType.
         * @param inputType Stable processed type identity.
         * @param reason Why the combination cannot exist. Must not be empty.
         * @throws std::invalid_argument if @p inputType or @p reason is empty.
         * @throws std::logic_error if the registry is frozen, if a writer for the same route is
         *         already registered, or if the same absence is documented twice.
         */
        void DocumentAbsentWriter(ContentOutputFormat format, const std::string& inputType,
                                  const std::string& reason);

        /**
         * @brief Returns the documented reason a route has no writer, or an empty string.
         *
         * @param format Container to ask about.
         * @param inputType Stable processed type identity.
         * @return The recorded reason, or an empty string when none was recorded.
         */
        [[nodiscard]] std::string AbsentWriterReason(ContentOutputFormat format,
                                                     const std::string& inputType) const;

        /**
         * @brief Returns every documented writer absence, ordered by container then type.
         *
         * @return One (format, processed type, reason) triple per documented absence.
         */
        [[nodiscard]] std::vector<std::tuple<ContentOutputFormat, std::string, std::string>>
        AbsentWriters() const;

    private:
        void RequireMutable() const;

        mutable std::shared_mutex configurationMutex_;
        mutable std::atomic_bool frozen_{false};
        std::map<std::string, std::shared_ptr<const ContentImporter>> importers_;
        std::map<std::string, std::shared_ptr<const ContentProcessor>> processors_;
        std::map<std::string, std::shared_ptr<const ContentTypeWriter>> writers_;
        std::map<std::pair<ContentOutputFormat, std::string>, std::string> absentWriters_;
        std::map<std::string, std::set<std::string>> importersByExtension_;
        std::map<std::string, std::set<std::string>> processorsByInputType_;
        std::map<std::pair<ContentOutputFormat, std::string>, std::set<std::string>>
            writersByInputType_;
    };

    /** @brief One request to run Importer -> Processor -> Writer without publishing a file. */
    struct ContentBuildRequest
    {
        /** @brief Root all source reads must remain inside. */
        std::filesystem::path sourceRoot;

        /** @brief Primary source, absolute or relative to sourceRoot. */
        std::filesystem::path source;

        /** @brief Explicit request-local read-only external source-root capabilities. */
        ContentSourceRootCapabilities externalSourceRoots;

        /** @brief Logical ContentManager asset name written into CNB metadata. */
        std::string logicalName;

        /** @brief Optional stable importer override. */
        std::string importer;

        /** @brief Optional stable processor override. */
        std::string processor;

        /** @brief Optional stable writer override. */
        std::string writer;

        /** @brief Compiled container this build node must emit. */
        ContentOutputFormat outputFormat = ContentOutputFormat::Cnb;

        /** @brief Processor parameters included in the effective build identity. */
        ContentProcessorParameters parameters;

        /** @brief Host-level build facts handed to every context of this build. */
        ContentBuildEnvironment environment;

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

        /** @brief Compiled container the selected writer emitted. */
        ContentOutputFormat outputFormat = ContentOutputFormat::Cnb;

        /** @brief Stable asset/schema/codec declarations selected before writing. */
        std::vector<ContentWriterSchemaIdentity> writerSchemas;

        /** @brief Effective processor parameters. */
        ContentProcessorParameters parameters;

        /** @brief Sorted build-time dependencies. */
        std::vector<ContentDependency> dependencies;

        /** @brief Sorted runtime content references, distinct from dependencies. */
        std::vector<RuntimeContentReference> runtimeReferences;

        /** @brief Sorted non-CNB files that must be deployed beside compiled content. */
        std::vector<ContentDeploymentFile> deploymentFiles;

        /** @brief Ordered informational and warning messages emitted by the successful build. */
        std::vector<ContentLogMessage> messages;

        /** @brief Complete primary and additional compiled CNB bytes, not yet published. */
        ContentWriteResult output;

        /** @brief True for the current non-incremental coordinator; future manifests may skip. */
        bool built = true;
    };

    /** @brief Result of ContentPipeline::ImportAndProcess(): a processed value that was not written. */
    struct ContentProcessResult
    {
        /** @brief Canonical primary source path of the nested stages. */
        std::filesystem::path source;

        /** @brief Logical name the request carried. */
        std::string logicalName;

        /** @brief Importer identity used. */
        ContentComponentIdentity importer;

        /** @brief Processor identity used. */
        ContentComponentIdentity processor;

        /** @brief The processed value, boxed under the processor's declared output type. */
        ContentValue processed;

        /** @brief Compiled assets the nested processor itself produced through further nested builds. */
        std::vector<ContentAdditionalWriteOutput> nestedOutputs;

        /** @brief Ordered messages the nested stages emitted. */
        std::vector<ContentLogMessage> messages;
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

    /**
     * @brief Build-to-bytes coordinator over an explicitly configured, frozen registry.
     *
     * Separate Build() calls may run concurrently when registered components and any shared
     * logging sink obey their documented reentrancy contracts.
     */
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

        /**
         * @brief Runs Importer -> Processor for a source and returns the processed value without
         *        writing it (plans/plan_xnapipeline_parity.md `XNAPP-044`).
         *
         * The in-process half of a build, for a processor that needs another asset's *processed
         * object* rather than its compiled bytes -- XNA's `BuildAndLoadAsset`. Dependencies the
         * nested import and processing record go into @p dependencies, which a nesting processor
         * passes its own collector so the outer node is rebuilt when the nested source changes;
         * the nested source itself is recorded as a source-file dependency there, never as a
         * second primary source.
         *
         * @param request Source identity, optional component overrides and processor parameters.
         * @param dependencies Collector that receives every dependency of the nested stages.
         * @return The processed value and the identities that produced it.
         * @throws ContentPipelineError with source/stage/component context on any failure.
         */
        [[nodiscard]] ContentProcessResult ImportAndProcess(const ContentBuildRequest& request,
                                                            ContentDependencyCollector& dependencies) const;

        /**
         * @brief Returns the frozen registry this coordinator builds with.
         *
         * @return The registry; never null.
         */
        [[nodiscard]] const ContentPipelineRegistry& Registry() const noexcept;

    private:
        struct StagedBuild;
        [[nodiscard]] StagedBuild RunImportAndProcess(const ContentBuildRequest& request,
                                                      ContentDependencyCollector& dependencies,
                                                      ContentBuildLogger& logger,
                                                      bool nested) const;

        std::shared_ptr<const ContentPipelineRegistry> registry_;
    };
}
