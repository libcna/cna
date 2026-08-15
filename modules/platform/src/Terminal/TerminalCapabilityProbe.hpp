// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

namespace CNA::Platform::Terminal {

    /** @brief How many colours the attached terminal can show. */
    enum class TerminalColourDepth
    {
        /** @brief No colour at all; the frame degrades to a luminance ramp. */
        Monochrome,
        /** @brief The 16 ANSI colours. */
        Ansi16,
        /** @brief The 256-colour indexed palette. */
        Indexed256,
        /** @brief 24-bit direct colour. */
        TrueColour
    };

    /**
     * @brief What the attached terminal turned out to support.
     *
     * Filled once at startup. Every field here is a *measured* answer rather than an assumption,
     * which matters because the alternative — trusting `TERM` — is wrong often enough to be
     * useless: it is inherited across ssh, rewritten by multiplexers, and frequently set to
     * something conservative that bears no relation to the emulator actually running.
     */
    struct TerminalCapabilities
    {
        /** @brief True when stdout is a terminal. False for a pipe, a file or a CI log. */
        bool isTerminal = false;

        /**
         * @brief True when stdin is a terminal too.
         *
         * Separate from `isTerminal` because the two genuinely differ: output redirected while
         * input stays on the tty makes querying impossible, and detection must then fall back to
         * the environment alone.
         */
        bool canQuery = false;

        /** @brief True when the terminal answered Primary Device Attributes. */
        bool respondedToDeviceAttributes = false;

        /**
         * @brief True when the terminal implements the Kitty keyboard protocol.
         *
         * The single most consequential field. With it, key releases are real events and
         * `exactKeyboardState` is honestly true; without it, releases must be synthesised from a
         * timeout and the capability must be reported false.
         */
        bool hasKittyKeyboard = false;

        /** @brief How much colour the terminal can show. */
        TerminalColourDepth colourDepth = TerminalColourDepth::Monochrome;

        /** @brief The terminal's self-reported name, or empty when it did not answer. */
        std::string terminalName;
    };

    /**
     * @brief Detects what the terminal behind a descriptor pair supports.
     *
     * ### Why absence of a reply is the answer
     *
     * A terminal cannot say "no". A query it does not understand produces **silence**, so
     * detection is a write followed by a *timed* read, and the timeout is a correctness parameter
     * rather than a tuning knob — too short and a capable terminal at the end of a slow ssh link
     * is misread as incapable.
     *
     * Primary Device Attributes is therefore sent first as a control. Every real terminal answers
     * it; with it answered and a later query silent, the negative is real. Without it, silence
     * means only that nothing is listening.
     *
     * ### What it does not do
     *
     * Nothing here changes terminal state permanently. Raw mode is entered only if a query is
     * possible, and restored before returning — including on exception — so calling this is safe
     * in a test process whose terminal belongs to somebody else.
     *
     * ### Why the descriptors are parameters
     *
     * So that detection can be *tested* rather than trusted: a test drives a pipe to reach the
     * not-a-terminal path deterministically, and a pseudo-terminal to play a terminal that
     * answers some queries and not others. Probing the process's real stdin from a test would
     * write escape sequences into whatever terminal happened to be running the suite and eat the
     * developer's keystrokes waiting for a reply.
     *
     * @param outputDescriptor The descriptor frames would be written to; decides "is a terminal".
     * @param inputDescriptor The descriptor replies arrive on; decides whether querying is possible.
     * @param queryTimeoutMilliseconds How long to wait for the first byte of a reply.
     * @return What was detected. Every field is false or minimal when there is no terminal.
     */
    [[nodiscard]] TerminalCapabilities DetectTerminalCapabilities(int outputDescriptor,
                                                                 int inputDescriptor,
                                                                 int queryTimeoutMilliseconds = 250);

    /**
     * @brief Detects what the process's own terminal supports.
     *
     * Convenience over the descriptor-taking overload, using standard output and standard input.
     *
     * @param queryTimeoutMilliseconds How long to wait for the first byte of a reply.
     * @return What was detected. Every field is false or minimal when there is no terminal.
     */
    [[nodiscard]] TerminalCapabilities DetectTerminalCapabilities(int queryTimeoutMilliseconds = 250);

    /**
     * @brief Reports whether the process is attached to a terminal, without asking it anything.
     *
     * Separate from the full detection because it is *free* and changes no state at all: no raw
     * mode, no writes, no waiting. That is what lets a platform be constructed in any process —
     * including a test binary — and still answer honestly whether presenting frames is possible.
     *
     * @return True when standard output is a terminal.
     */
    [[nodiscard]] bool IsAttachedToTerminal();

    /**
     * @brief Returns the stable, human-readable name of a colour depth.
     *
     * @param depth The depth to name.
     * @return A stable name such as `"TrueColour"`.
     */
    [[nodiscard]] const std::string& ToString(TerminalColourDepth depth);

} // namespace CNA::Platform::Terminal
