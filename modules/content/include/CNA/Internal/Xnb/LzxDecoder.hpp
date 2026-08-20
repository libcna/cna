// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "System/IO/Stream.hpp"

// plans/plan_xnb.md XNB-28: a from-scratch C++ port of FNA's LzxDecoder.cs (itself a C# port of
// libmspack's lzxd.c, Copyright 2003-2004 Stuart Caie / 2011 Ali Scissons, dual MSPL/LGPL --
// used here under CNA's own MS-PL license, matching FNA's own use). This is a line-by-line
// port, not a reimplementation from the algorithm description -- preserves FNA's own variable
// names, control flow (including its `goto case ALIGNED -> VERBATIM` fallthrough, translated to
// C++'s native switch-fallthrough), and error-as-return-code style (0 = success, negative =
// error) so it can be verified against the original source directly. Malformed/adversarial-input
// hardening beyond FNA's own bounds checks is plans/plan_xnb.md XNB-30, a deliberately separate,
// subsequent pass -- not folded into this initial port.

namespace CNA::Internal::Xnb
{
    /** @brief Thrown when constructed with a window size FNA's own LzxDecoder also rejects (outside [15, 21]). */
    class UnsupportedLzxWindowSizeRange : public std::runtime_error
    {
    public:
        UnsupportedLzxWindowSizeRange() : std::runtime_error("Unsupported LZX window size (must be 15-21).") {}
    };

    /**
     * @brief LZX decompressor for `.xnb` container payloads (plans/plan_xnb.md XNB-28/29), matching
     *        FNA's `LzxDecoder` (`src/Content/LzxDecoder.cs`) exactly.
     *
     * One instance holds decoder *state* across multiple Decompress() calls within the same
     * `.xnb` file's block stream (the sliding window, repeated-offset LRU queue, and Huffman
     * tables all persist between blocks) -- construct a fresh instance per file, never share one
     * across files (matching plans/plan_xnb.md XNB-17G's no-cross-file-state requirement).
     */
    class LzxDecoder
    {
    public:
        /**
         * @brief Constructs a decoder with a 2^window-byte sliding window.
         *
         * @param window Window size exponent, 15-21 (`.xnb` always uses 16, i.e. 64KB).
         * @throws UnsupportedLzxWindowSizeRange if @p window is outside [15, 21].
         */
        explicit LzxDecoder(int window);

        /**
         * @brief Decompresses one block from @p inData into @p outData.
         *
         * @param inData  Compressed input stream, positioned at the start of this block.
         * @param inLen   Number of compressed bytes this block occupies in @p inData.
         * @param outData Output stream to append @p outLen decompressed bytes to.
         * @param outLen  Number of decompressed bytes this block should produce.
         * @return 0 on success, negative on any error (matches FNA's own error-as-return-code style).
         */
        int Decompress(System::IO::Stream& inData, int inLen, System::IO::Stream& outData, int outLen);

    private:
        class BitBuffer;

        enum class BlockType : uint8_t
        {
            Invalid = 0,
            Verbatim = 1,
            Aligned = 2,
            Uncompressed = 3
        };

        struct LzxState
        {
            uint32_t R0 = 1, R1 = 1, R2 = 1;
            uint16_t main_elements = 0;
            bool header_read = false;
            BlockType block_type = BlockType::Invalid;
            uint32_t block_length = 0;
            uint32_t block_remaining = 0;
            uint32_t frames_read = 0;
            int32_t intel_filesize = 0;
            int32_t intel_curpos = 0;
            bool intel_started = false;

            std::vector<uint16_t> PRETREE_table;
            std::vector<uint8_t> PRETREE_len;
            std::vector<uint16_t> MAINTREE_table;
            std::vector<uint8_t> MAINTREE_len;
            std::vector<uint16_t> LENGTH_table;
            std::vector<uint8_t> LENGTH_len;
            std::vector<uint16_t> ALIGNED_table;
            std::vector<uint8_t> ALIGNED_len;

            std::vector<uint8_t> window;
            uint32_t window_size = 0;
            uint32_t window_posn = 0;
        };

        int MakeDecodeTable(uint32_t nsyms, uint32_t nbits, const std::vector<uint8_t>& length,
                             std::vector<uint16_t>& table);
        void ReadLengths(std::vector<uint8_t>& lens, uint32_t first, uint32_t last, BitBuffer& bitbuf);
        uint32_t ReadHuffSym(const std::vector<uint16_t>& table, const std::vector<uint8_t>& lengths,
                             uint32_t nsyms, uint32_t nbits, BitBuffer& bitbuf);

        LzxState state_;
    };
}
