// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_graphics_shared_cleanup.md GSC-0006: which Direct3D debug-layer messages fail the test that
// produced them.
//
// Pure C++ with no Direct3D include, so the verdicts are unit-tested on every platform
// (D3DDebugLayerPolicyTests.cpp); D3DDebugLayerListener.cpp applies them on Windows.
//
//   * CORRUPTION and ERROR always fail. Nothing can allowlist them.
//   * WARNING fails unless one entry of kAllowedWarnings names its message id and the test it
//     was investigated in. An entry is added only for a warning whose behaviour was measured to be
//     valid, with the reason written next to it -- matched by id, never by description text.
//   * INFO and MESSAGE never fail; the renderer also denies them at the info queue's storage filter.
//   * An unknown severity fails: a message the policy cannot place is not a pass.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Testing::D3DDebugLayer
{
    /** @brief D3D11_MESSAGE_SEVERITY. */
    enum Severity : int
    {
        /** @brief Corruption. */
        Corruption = 0,
        /** @brief Error. */
        Error = 1,
        /** @brief Warning. */
        Warning = 2,
        /** @brief Info. */
        Info = 3,
        /** @brief Message. */
        Message = 4,
    };

    /** @brief What a message means for the test that produced it. */
    enum class Verdict
    {
        /** @brief The test fails. */
        Fatal,
        /** @brief A warning investigated and allowlisted for this test. */
        Allowed,
        /** @brief Carries no verdict (INFO, MESSAGE). */
        Ignored,
    };

    /** @brief One investigated warning that is valid behaviour in the test it names. */
    struct AllowedWarning
    {
        /** @brief The D3D11_MESSAGE_ID. */
        int id;
        /** @brief The id's enumerator name, for the report. */
        const char* idName;
        /** @brief "Suite.Test", or "Suite." for every test of one suite. */
        const char* testScope;
        /** @brief Why the behaviour is valid, and where it was measured. */
        const char* reason;
    };

    /** @brief The complete allowlist. Every entry is a WARNING; errors cannot be listed. */
    inline constexpr AllowedWarning kAllowedWarnings[] = {
        {391, "CREATEINPUTLAYOUT_TYPE_MISMATCH",
         "DrawRouteValidation.EveryVertexElementFormatIsBoundOrRefusedByName",
         "The test binds every XNA VertexElementFormat, including Byte4, Short2 and Short4 read by a "
         "float TEXCOORD input, which XNA's declarations allow. The layer says \"this is not an error, "
         "since behavior is well defined\" (plans/plan_graphics_shared_cleanup.md GSC-0006, first "
         "DirectX11 debug-layer round)."},
        {408, "QUERY_BEGIN_ABANDONING_PREVIOUS_RESULTS",
         "OcclusionQueryPixelCountPrecisionTest.XnaLifecycleRejectsUnavailableAndInvalidSequences",
         "The test reads IsComplete before the result is ready and then calls Begin again -- the sequence "
         "Microsoft XNA's OcclusionQuery state machine permits (SOFTWARE-199). The layer calls abandoning the "
         "previous result \"valid; but unusual\" (GSC-0006, first DirectX11 debug-layer round)."},
    };

    /**
     * @brief Whether @p testName is inside an allowlist entry's scope.
     *
     * @param scope     "Suite.Test" for one test, or "Suite." for a whole suite.
     * @param testName  The full "Suite.Test" name.
     * @return True when the entry applies to the test.
     */
    [[nodiscard]] inline bool InScope(std::string_view scope, std::string_view testName)
    {
        if (!scope.empty() && scope.back() == '.')
            return testName.substr(0, scope.size()) == scope;
        return testName == scope;
    }

    /**
     * @brief Classifies one message.
     *
     * @param severity  Its severity ordinal.
     * @param id        Its message id.
     * @param testName  The running test's "Suite.Test" name; empty outside a test.
     * @return The verdict.
     */
    [[nodiscard]] inline Verdict Classify(int severity, int id, std::string_view testName)
    {
        switch (severity)
        {
            case Corruption:
            case Error:
                return Verdict::Fatal;
            case Warning:
                for (const AllowedWarning& entry : kAllowedWarnings)
                {
                    if (entry.id == id && InScope(entry.testScope, testName))
                        return Verdict::Allowed;
                }
                return Verdict::Fatal;
            case Info:
            case Message:
                return Verdict::Ignored;
            default:
                return Verdict::Fatal;
        }
    }

    /**
     * @brief Accumulates one test's messages and decides whether it passed validation.
     *
     * The listener feeds every recorded message through Observe() between BeginTest() and EndTest();
     * messages seen outside a test are kept separately, since no test can own them.
     */
    class Gate
    {
    public:
        /** @brief What one test (or the time outside every test) produced. */
        struct Outcome
        {
            /** @brief "Suite.Test", or empty for messages outside a test. */
            std::string testName;
            /** @brief Fatal messages, formatted; at most kRetainedFatal are kept. */
            std::vector<std::string> fatal;
            /** @brief Every fatal message, including those not retained. */
            std::size_t fatalCount = 0;
            /** @brief Allowlisted warnings. */
            std::size_t allowedCount = 0;
        };

        /** @brief Fatal messages kept verbatim per test; the count keeps going. */
        static constexpr std::size_t kRetainedFatal = 32;

        /**
         * @brief Starts attributing messages to @p testName.
         *
         * @param testName The "Suite.Test" name.
         */
        void BeginTest(std::string testName)
        {
            current_ = Outcome{};
            current_.testName = std::move(testName);
            inTest_ = true;
        }

        /**
         * @brief Classifies and records one message.
         *
         * @param severity    Its severity ordinal.
         * @param id          Its message id.
         * @param description The layer's text.
         */
        void Observe(int severity, int id, const std::string& description)
        {
            Outcome& outcome = inTest_ ? current_ : outside_;
            switch (Classify(severity, id, outcome.testName))
            {
                case Verdict::Fatal:
                    if (outcome.fatal.size() < kRetainedFatal)
                        outcome.fatal.push_back(Format(severity, id, description));
                    ++outcome.fatalCount;
                    break;
                case Verdict::Allowed:
                    ++outcome.allowedCount;
                    break;
                case Verdict::Ignored:
                    break;
            }
        }

        /**
         * @brief Ends the running test.
         *
         * @return Everything it produced.
         */
        Outcome EndTest()
        {
            inTest_ = false;
            Outcome finished = std::move(current_);
            current_ = Outcome{};
            return finished;
        }

        /**
         * @brief Takes what was produced outside every test so far.
         *
         * @return The outside outcome; the gate starts a fresh one.
         */
        Outcome TakeOutside()
        {
            Outcome taken = std::move(outside_);
            outside_ = Outcome{};
            return taken;
        }

        /**
         * @brief One report line per message: severity, id and text.
         *
         * @param severity    Its severity ordinal.
         * @param id          Its message id.
         * @param description The layer's text.
         * @return The formatted line.
         */
        [[nodiscard]] static std::string Format(int severity, int id, const std::string& description)
        {
            static constexpr const char* kSeverities[] = {"CORRUPTION", "ERROR", "WARNING", "INFO",
                                                          "MESSAGE"};
            const std::string severityName = severity >= 0 && severity <= Message
                ? kSeverities[severity]
                : "severity " + std::to_string(severity);
            return "D3D11 " + severityName +
                   " id " + std::to_string(id) + ": " + description;
        }

    private:
        Outcome current_;
        Outcome outside_;
        bool inTest_ = false;
    };

    /**
     * @brief The failure text for an outcome with fatal messages.
     *
     * @param outcome A finished test's outcome.
     * @return The count followed by one line per retained message.
     */
    [[nodiscard]] inline std::string DescribeFatal(const Gate::Outcome& outcome)
    {
        std::string text = std::to_string(outcome.fatalCount) +
                           " fatal Direct3D debug-layer message(s) (plans/plan_graphics_shared_cleanup.md GSC-0006):";
        for (const std::string& line : outcome.fatal)
            text += "\n  " + line;
        if (outcome.fatalCount > outcome.fatal.size())
            text += "\n  ... " + std::to_string(outcome.fatalCount - outcome.fatal.size()) + " more";
        return text;
    }
}
