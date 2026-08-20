// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/LzxDecoder.hpp"

namespace CNA::Internal::Xnb
{
    namespace
    {
        constexpr uint16_t MIN_MATCH = 2;
        constexpr uint16_t NUM_CHARS = 256;
        constexpr uint16_t PRETREE_NUM_ELEMENTS = 20;
        constexpr uint16_t ALIGNED_NUM_ELEMENTS = 8;
        constexpr uint16_t NUM_PRIMARY_LENGTHS = 7;
        constexpr uint16_t NUM_SECONDARY_LENGTHS = 249;

        constexpr uint16_t PRETREE_MAXSYMBOLS = PRETREE_NUM_ELEMENTS;
        constexpr uint16_t PRETREE_TABLEBITS = 6;
        constexpr uint16_t MAINTREE_MAXSYMBOLS = NUM_CHARS + 50 * 8;
        constexpr uint16_t MAINTREE_TABLEBITS = 12;
        constexpr uint16_t LENGTH_MAXSYMBOLS = NUM_SECONDARY_LENGTHS + 1;
        constexpr uint16_t LENGTH_TABLEBITS = 12;
        constexpr uint16_t ALIGNED_MAXSYMBOLS = ALIGNED_NUM_ELEMENTS;
        constexpr uint16_t ALIGNED_TABLEBITS = 7;
        constexpr uint16_t LENTABLE_SAFETY = 64;

        // FNA's own static (lazily-initialized-once, shared across every LzxDecoder instance)
        // lookup tables -- C++ magic statics give the same "compute once" behavior, thread-safely.
        const std::vector<uint8_t>& ExtraBits()
        {
            static const std::vector<uint8_t> bits = [] {
                std::vector<uint8_t> b(52, 0);
                for (int i = 0, j = 0; i <= 50; i += 2)
                {
                    b[i] = b[i + 1] = static_cast<uint8_t>(j);
                    if (i != 0 && j < 17) j++;
                }
                return b;
            }();
            return bits;
        }

        const std::vector<uint32_t>& PositionBase()
        {
            static const std::vector<uint32_t> base = [] {
                std::vector<uint32_t> b(51, 0);
                const auto& eb = ExtraBits();
                for (int i = 0, j = 0; i <= 50; i++)
                {
                    b[i] = static_cast<uint32_t>(j);
                    j += 1 << eb[i];
                }
                return b;
            }();
            return base;
        }
    }

    // FNA's own nested BitBuffer class -- reads the compressed stream 16 bits at a time into a
    // 32-bit MSB-aligned buffer.
    class LzxDecoder::BitBuffer
    {
    public:
        explicit BitBuffer(System::IO::Stream& stream) : stream_(stream) { InitBitStream(); }

        void InitBitStream() { buffer_ = 0; bitsLeft_ = 0; }

        void EnsureBits(uint8_t bits)
        {
            while (bitsLeft_ < bits)
            {
                // Matches FNA's own (byte)stream.ReadByte(): at end-of-stream, ReadByte()
                // returns -1, which the (byte) cast wraps to 0xFF -- replicated verbatim here,
                // not treated as a special EOF case (hardening against this is XNB-30).
                const auto lo = static_cast<uint8_t>(stream_.ReadByte());
                const auto hi = static_cast<uint8_t>(stream_.ReadByte());
                buffer_ |= (static_cast<uint32_t>((hi << 8) | lo)) << (32 - 16 - bitsLeft_);
                bitsLeft_ = static_cast<uint8_t>(bitsLeft_ + 16);
            }
        }

        [[nodiscard]] uint32_t PeekBits(uint8_t bits) const { return buffer_ >> (32 - bits); }

        void RemoveBits(uint8_t bits)
        {
            buffer_ <<= bits;
            bitsLeft_ = static_cast<uint8_t>(bitsLeft_ - bits);
        }

        uint32_t ReadBits(uint8_t bits)
        {
            uint32_t ret = 0;
            if (bits > 0)
            {
                EnsureBits(bits);
                ret = PeekBits(bits);
                RemoveBits(bits);
            }
            return ret;
        }

        [[nodiscard]] uint32_t GetBuffer() const { return buffer_; }
        [[nodiscard]] uint8_t GetBitsLeft() const { return bitsLeft_; }

    private:
        uint32_t buffer_ = 0;
        uint8_t bitsLeft_ = 0;
        System::IO::Stream& stream_;
    };

    LzxDecoder::LzxDecoder(int window)
    {
        if (window < 15 || window > 21)
        {
            throw UnsupportedLzxWindowSizeRange();
        }

        const uint32_t wndsize = uint32_t(1) << window;
        state_.window.assign(wndsize, uint8_t(0xDC));
        state_.window_size = wndsize;
        state_.window_posn = 0;

        int posn_slots;
        if (window == 20) posn_slots = 42;
        else if (window == 21) posn_slots = 50;
        else posn_slots = window << 1;

        state_.R0 = state_.R1 = state_.R2 = 1;
        state_.main_elements = static_cast<uint16_t>(NUM_CHARS + (posn_slots << 3));
        state_.header_read = false;
        state_.frames_read = 0;
        state_.block_remaining = 0;
        state_.block_type = BlockType::Invalid;
        state_.intel_curpos = 0;
        state_.intel_started = false;

        state_.PRETREE_table.assign((1u << PRETREE_TABLEBITS) + (uint32_t(PRETREE_MAXSYMBOLS) << 1), 0);
        state_.PRETREE_len.assign(PRETREE_MAXSYMBOLS + LENTABLE_SAFETY, 0);
        state_.MAINTREE_table.assign((1u << MAINTREE_TABLEBITS) + (uint32_t(MAINTREE_MAXSYMBOLS) << 1), 0);
        state_.MAINTREE_len.assign(MAINTREE_MAXSYMBOLS + LENTABLE_SAFETY, 0);
        state_.LENGTH_table.assign((1u << LENGTH_TABLEBITS) + (uint32_t(LENGTH_MAXSYMBOLS) << 1), 0);
        state_.LENGTH_len.assign(LENGTH_MAXSYMBOLS + LENTABLE_SAFETY, 0);
        state_.ALIGNED_table.assign((1u << ALIGNED_TABLEBITS) + (uint32_t(ALIGNED_MAXSYMBOLS) << 1), 0);
        state_.ALIGNED_len.assign(ALIGNED_MAXSYMBOLS + LENTABLE_SAFETY, 0);
        // MAINTREE_len/LENGTH_len are already zero from .assign(n, 0) above -- FNA's own explicit
        // zero-fill loop has no C++ equivalent needed.
    }

    int LzxDecoder::MakeDecodeTable(uint32_t nsyms, uint32_t nbits, const std::vector<uint8_t>& length,
                                     std::vector<uint16_t>& table)
    {
        uint16_t sym;
        uint32_t leaf;
        uint8_t bit_num = 1;
        uint32_t fill;
        uint32_t pos = 0;
        uint32_t table_mask = uint32_t(1) << nbits;
        uint32_t bit_mask = table_mask >> 1;
        uint32_t next_symbol = bit_mask;

        while (bit_num <= nbits)
        {
            for (sym = 0; sym < nsyms; sym++)
            {
                if (length[sym] == bit_num)
                {
                    leaf = pos;
                    if ((pos += bit_mask) > table_mask) return 1;
                    fill = bit_mask;
                    while (fill-- > 0) table[leaf++] = sym;
                }
            }
            bit_mask >>= 1;
            bit_num++;
        }

        if (pos != table_mask)
        {
            for (sym = static_cast<uint16_t>(pos); sym < table_mask; sym++) table[sym] = 0;

            pos <<= 16;
            table_mask <<= 16;
            bit_mask = 1u << 15;

            while (bit_num <= 16)
            {
                for (sym = 0; sym < nsyms; sym++)
                {
                    if (length[sym] == bit_num)
                    {
                        leaf = pos >> 16;
                        for (fill = 0; fill < static_cast<uint32_t>(bit_num) - nbits; fill++)
                        {
                            // plans/plan_xnb.md XNB-30A hardening: FNA's own C# port relies on the CLR's
                            // automatic bounds-checked array access here (an adversarial/corrupt
                            // Huffman code-length table can grow next_symbol/leaf past this
                            // table's allocated room); a direct std::vector::operator[] port has
                            // no equivalent safety net. Reject it explicitly instead of writing
                            // out of bounds (found by the XNB-30A fuzz test).
                            if (leaf >= table.size() || (static_cast<std::size_t>(next_symbol) << 1) + 1 >= table.size())
                            {
                                return 1;
                            }
                            if (table[leaf] == 0)
                            {
                                table[next_symbol << 1] = 0;
                                table[(next_symbol << 1) + 1] = 0;
                                table[leaf] = static_cast<uint16_t>(next_symbol++);
                            }
                            leaf = static_cast<uint32_t>(table[leaf]) << 1;
                            if (((pos >> (15 - fill)) & 1) == 1) leaf++;
                        }
                        if (leaf >= table.size()) return 1;
                        table[leaf] = sym;

                        if ((pos += bit_mask) > table_mask) return 1;
                    }
                }
                bit_mask >>= 1;
                bit_num++;
            }
        }

        if (pos == table_mask) return 0;

        for (sym = 0; sym < nsyms; sym++) if (length[sym] != 0) return 1;
        return 0;
    }

    void LzxDecoder::ReadLengths(std::vector<uint8_t>& lens, uint32_t first, uint32_t last, BitBuffer& bitbuf)
    {
        for (uint32_t x = 0; x < 20; x++)
        {
            const uint32_t y = bitbuf.ReadBits(4);
            state_.PRETREE_len[x] = static_cast<uint8_t>(y);
        }
        MakeDecodeTable(PRETREE_MAXSYMBOLS, PRETREE_TABLEBITS, state_.PRETREE_len, state_.PRETREE_table);

        for (uint32_t x = first; x < last;)
        {
            int z = static_cast<int>(
                ReadHuffSym(state_.PRETREE_table, state_.PRETREE_len, PRETREE_MAXSYMBOLS, PRETREE_TABLEBITS, bitbuf));
            if (z == 17)
            {
                uint32_t y = bitbuf.ReadBits(4);
                y += 4;
                while (y-- != 0) lens[x++] = 0;
            }
            else if (z == 18)
            {
                uint32_t y = bitbuf.ReadBits(5);
                y += 20;
                while (y-- != 0) lens[x++] = 0;
            }
            else if (z == 19)
            {
                uint32_t y = bitbuf.ReadBits(1);
                y += 4;
                z = static_cast<int>(ReadHuffSym(
                    state_.PRETREE_table, state_.PRETREE_len, PRETREE_MAXSYMBOLS, PRETREE_TABLEBITS, bitbuf));
                z = lens[x] - z;
                if (z < 0) z += 17;
                while (y-- != 0) lens[x++] = static_cast<uint8_t>(z);
            }
            else
            {
                z = lens[x] - z;
                if (z < 0) z += 17;
                lens[x++] = static_cast<uint8_t>(z);
            }
        }
    }

    uint32_t LzxDecoder::ReadHuffSym(const std::vector<uint16_t>& table, const std::vector<uint8_t>& lengths,
                                     uint32_t nsyms, uint32_t nbits, BitBuffer& bitbuf)
    {
        bitbuf.EnsureBits(16);
        uint32_t i;
        if ((i = table[bitbuf.PeekBits(static_cast<uint8_t>(nbits))]) >= nsyms)
        {
            uint32_t j = uint32_t(1) << (32 - nbits);
            do
            {
                j >>= 1;
                i <<= 1;
                i |= (bitbuf.GetBuffer() & j) != 0 ? 1u : 0u;
                if (j == 0) return 0; // matches FNA's own "TODO: throw proper exception"
            } while ((i = table[i]) >= nsyms);
        }
        const uint32_t j = lengths[i];
        bitbuf.RemoveBits(static_cast<uint8_t>(j));

        return i;
    }

    int LzxDecoder::Decompress(System::IO::Stream& inData, int inLen, System::IO::Stream& outData, int outLen)
    {
        BitBuffer bitbuf(inData);
        const int64_t startpos = inData.getPositionProperty();

        std::vector<uint8_t>& window = state_.window;

        uint32_t window_posn = state_.window_posn;
        const uint32_t window_size = state_.window_size;
        uint32_t R0 = state_.R0;
        uint32_t R1 = state_.R1;
        uint32_t R2 = state_.R2;
        uint32_t i, j;

        int togo = outLen, this_run, main_element, match_length, match_offset, length_footer, extra, verbatim_bits;
        int rundest, runsrc, copy_length, aligned_bits;

        bitbuf.InitBitStream();

        // Read header if necessary.
        if (!state_.header_read)
        {
            const uint32_t intel = bitbuf.ReadBits(1);
            if (intel != 0)
            {
                i = bitbuf.ReadBits(16);
                j = bitbuf.ReadBits(16);
                state_.intel_filesize = static_cast<int32_t>((i << 16) | j);
            }
            state_.header_read = true;
        }

        // Main decoding loop.
        while (togo > 0)
        {
            // Last block finished, new block expected.
            if (state_.block_remaining == 0)
            {
                if (state_.block_type == BlockType::Uncompressed)
                {
                    if ((state_.block_length & 1) == 1) inData.ReadByte(); // realign bitstream to word
                    bitbuf.InitBitStream();
                }

                state_.block_type = static_cast<BlockType>(bitbuf.ReadBits(3));
                i = bitbuf.ReadBits(16);
                j = bitbuf.ReadBits(8);
                state_.block_remaining = state_.block_length = (i << 8) | j;

                switch (state_.block_type)
                {
                    case BlockType::Aligned:
                        for (i = 0, j = 0; i < 8; i++)
                        {
                            j = bitbuf.ReadBits(3);
                            state_.ALIGNED_len[i] = static_cast<uint8_t>(j);
                        }
                        MakeDecodeTable(ALIGNED_MAXSYMBOLS, ALIGNED_TABLEBITS, state_.ALIGNED_len, state_.ALIGNED_table);
                        // Rest of aligned header is same as verbatim.
                        [[fallthrough]];

                    case BlockType::Verbatim:
                        ReadLengths(state_.MAINTREE_len, 0, 256, bitbuf);
                        ReadLengths(state_.MAINTREE_len, 256, state_.main_elements, bitbuf);
                        MakeDecodeTable(MAINTREE_MAXSYMBOLS, MAINTREE_TABLEBITS, state_.MAINTREE_len, state_.MAINTREE_table);
                        if (state_.MAINTREE_len[0xE8] != 0) state_.intel_started = true;

                        ReadLengths(state_.LENGTH_len, 0, NUM_SECONDARY_LENGTHS, bitbuf);
                        MakeDecodeTable(LENGTH_MAXSYMBOLS, LENGTH_TABLEBITS, state_.LENGTH_len, state_.LENGTH_table);
                        break;

                    case BlockType::Uncompressed:
                    {
                        state_.intel_started = true; // Because we can't assume otherwise.
                        bitbuf.EnsureBits(16); // Get up to 16 pad bits into the buffer.
                        if (bitbuf.GetBitsLeft() > 16) inData.Seek(-2, System::IO::SeekOrigin::Current);
                        uint8_t hi, mh, ml, lo;
                        lo = static_cast<uint8_t>(inData.ReadByte()); ml = static_cast<uint8_t>(inData.ReadByte());
                        mh = static_cast<uint8_t>(inData.ReadByte()); hi = static_cast<uint8_t>(inData.ReadByte());
                        R0 = static_cast<uint32_t>(lo | (ml << 8) | (mh << 16) | (hi << 24));
                        lo = static_cast<uint8_t>(inData.ReadByte()); ml = static_cast<uint8_t>(inData.ReadByte());
                        mh = static_cast<uint8_t>(inData.ReadByte()); hi = static_cast<uint8_t>(inData.ReadByte());
                        R1 = static_cast<uint32_t>(lo | (ml << 8) | (mh << 16) | (hi << 24));
                        lo = static_cast<uint8_t>(inData.ReadByte()); ml = static_cast<uint8_t>(inData.ReadByte());
                        mh = static_cast<uint8_t>(inData.ReadByte()); hi = static_cast<uint8_t>(inData.ReadByte());
                        R2 = static_cast<uint32_t>(lo | (ml << 8) | (mh << 16) | (hi << 24));
                        break;
                    }

                    default:
                        return -1;
                }
            }

            // Buffer exhaustion check.
            if (inData.getPositionProperty() > (startpos + inLen))
            {
                /* It's possible to have a file where the next run is less than
                 * 16 bits in size. In this case, the READ_HUFFSYM() macro used
                 * in building the tables will exhaust the buffer, so we should
                 * allow for this, but not allow those accidentally read bits to
                 * be used (so we check that there are at least 16 bits
                 * remaining - in this boundary case they aren't really part of
                 * the compressed data).
                 */
                if (inData.getPositionProperty() > (startpos + inLen + 2) || bitbuf.GetBitsLeft() < 16) return -1;
            }

            while ((this_run = static_cast<int>(state_.block_remaining)) > 0 && togo > 0)
            {
                if (this_run > togo) this_run = togo;
                togo -= this_run;
                state_.block_remaining -= static_cast<uint32_t>(this_run);

                // Apply 2^x-1 mask.
                window_posn &= window_size - 1;
                // Runs can't straddle the window wraparound.
                if ((window_posn + static_cast<uint32_t>(this_run)) > window_size) return -1;

                switch (state_.block_type)
                {
                    case BlockType::Verbatim:
                        while (this_run > 0)
                        {
                            main_element = static_cast<int>(ReadHuffSym(
                                state_.MAINTREE_table, state_.MAINTREE_len, MAINTREE_MAXSYMBOLS, MAINTREE_TABLEBITS, bitbuf));
                            if (main_element < NUM_CHARS)
                            {
                                // Literal: 0 to NUM_CHARS-1.
                                window[window_posn++] = static_cast<uint8_t>(main_element);
                                this_run--;
                            }
                            else
                            {
                                // Match: NUM_CHARS + ((slot<<3) | length_header (3 bits)).
                                main_element -= NUM_CHARS;

                                match_length = main_element & NUM_PRIMARY_LENGTHS;
                                if (match_length == NUM_PRIMARY_LENGTHS)
                                {
                                    length_footer = static_cast<int>(ReadHuffSym(
                                        state_.LENGTH_table, state_.LENGTH_len, LENGTH_MAXSYMBOLS, LENGTH_TABLEBITS, bitbuf));
                                    match_length += length_footer;
                                }
                                match_length += MIN_MATCH;

                                match_offset = main_element >> 3;

                                if (match_offset > 2)
                                {
                                    // Not repeated offset.
                                    if (match_offset != 3)
                                    {
                                        extra = ExtraBits()[static_cast<std::size_t>(match_offset)];
                                        verbatim_bits = static_cast<int>(bitbuf.ReadBits(static_cast<uint8_t>(extra)));
                                        match_offset = static_cast<int>(PositionBase()[static_cast<std::size_t>(match_offset)]) - 2 + verbatim_bits;
                                    }
                                    else
                                    {
                                        match_offset = 1;
                                    }

                                    // Update repeated offset LRU queue.
                                    R2 = R1; R1 = R0; R0 = static_cast<uint32_t>(match_offset);
                                }
                                else if (match_offset == 0)
                                {
                                    match_offset = static_cast<int>(R0);
                                }
                                else if (match_offset == 1)
                                {
                                    match_offset = static_cast<int>(R1);
                                    R1 = R0; R0 = static_cast<uint32_t>(match_offset);
                                }
                                else // match_offset == 2
                                {
                                    match_offset = static_cast<int>(R2);
                                    R2 = R0; R0 = static_cast<uint32_t>(match_offset);
                                }

                                // plans/plan_xnb.md XNB-30 hardening: FNA's own C# port relies on the
                                // CLR's automatic bounds-checked array access to turn a corrupt
                                // match_offset into a catchable IndexOutOfRangeException; a
                                // direct C++ port using std::vector::operator[] would instead be
                                // undefined behavior (a potential out-of-bounds read) on the same
                                // malicious/corrupt input. Reject it explicitly instead.
                                if (match_offset <= 0 || match_offset > static_cast<int>(window_size)) return -1;

                                rundest = static_cast<int>(window_posn);
                                this_run -= match_length;

                                // Copy any wrapped around source data.
                                if (window_posn >= static_cast<uint32_t>(match_offset))
                                {
                                    // No wrap.
                                    runsrc = rundest - match_offset;
                                }
                                else
                                {
                                    runsrc = rundest + (static_cast<int>(window_size) - match_offset);
                                    copy_length = match_offset - static_cast<int>(window_posn);
                                    if (copy_length < match_length)
                                    {
                                        match_length -= copy_length;
                                        window_posn += static_cast<uint32_t>(copy_length);
                                        while (copy_length-- > 0) window[rundest++] = window[runsrc++];
                                        runsrc = 0;
                                    }
                                }
                                window_posn += static_cast<uint32_t>(match_length);

                                // Copy match data - no worries about destination wraps.
                                while (match_length-- > 0) window[rundest++] = window[runsrc++];
                            }
                        }
                        break;

                    case BlockType::Aligned:
                        while (this_run > 0)
                        {
                            main_element = static_cast<int>(ReadHuffSym(
                                state_.MAINTREE_table, state_.MAINTREE_len, MAINTREE_MAXSYMBOLS, MAINTREE_TABLEBITS, bitbuf));

                            if (main_element < NUM_CHARS)
                            {
                                // Literal 0 to NUM_CHARS-1.
                                window[window_posn++] = static_cast<uint8_t>(main_element);
                                this_run -= 1;
                            }
                            else
                            {
                                // Match: NUM_CHARS + ((slot<<3) | length_header (3 bits)).
                                main_element -= NUM_CHARS;

                                match_length = main_element & NUM_PRIMARY_LENGTHS;
                                if (match_length == NUM_PRIMARY_LENGTHS)
                                {
                                    length_footer = static_cast<int>(ReadHuffSym(
                                        state_.LENGTH_table, state_.LENGTH_len, LENGTH_MAXSYMBOLS, LENGTH_TABLEBITS, bitbuf));
                                    match_length += length_footer;
                                }
                                match_length += MIN_MATCH;

                                match_offset = main_element >> 3;

                                if (match_offset > 2)
                                {
                                    // Not repeated offset.
                                    extra = ExtraBits()[static_cast<std::size_t>(match_offset)];
                                    match_offset = static_cast<int>(PositionBase()[static_cast<std::size_t>(match_offset)]) - 2;
                                    if (extra > 3)
                                    {
                                        // Verbatim and aligned bits.
                                        extra -= 3;
                                        verbatim_bits = static_cast<int>(bitbuf.ReadBits(static_cast<uint8_t>(extra)));
                                        match_offset += (verbatim_bits << 3);
                                        aligned_bits = static_cast<int>(ReadHuffSym(
                                            state_.ALIGNED_table, state_.ALIGNED_len, ALIGNED_MAXSYMBOLS, ALIGNED_TABLEBITS, bitbuf));
                                        match_offset += aligned_bits;
                                    }
                                    else if (extra == 3)
                                    {
                                        // Aligned bits only.
                                        aligned_bits = static_cast<int>(ReadHuffSym(
                                            state_.ALIGNED_table, state_.ALIGNED_len, ALIGNED_MAXSYMBOLS, ALIGNED_TABLEBITS, bitbuf));
                                        match_offset += aligned_bits;
                                    }
                                    else if (extra > 0) // extra==1, extra==2
                                    {
                                        // Verbatim bits only.
                                        verbatim_bits = static_cast<int>(bitbuf.ReadBits(static_cast<uint8_t>(extra)));
                                        match_offset += verbatim_bits;
                                    }
                                    else // extra == 0
                                    {
                                        match_offset = 1;
                                    }

                                    // Update repeated offset LRU queue.
                                    R2 = R1; R1 = R0; R0 = static_cast<uint32_t>(match_offset);
                                }
                                else if (match_offset == 0)
                                {
                                    match_offset = static_cast<int>(R0);
                                }
                                else if (match_offset == 1)
                                {
                                    match_offset = static_cast<int>(R1);
                                    R1 = R0; R0 = static_cast<uint32_t>(match_offset);
                                }
                                else // match_offset == 2
                                {
                                    match_offset = static_cast<int>(R2);
                                    R2 = R0; R0 = static_cast<uint32_t>(match_offset);
                                }

                                // plans/plan_xnb.md XNB-30 hardening: FNA's own C# port relies on the
                                // CLR's automatic bounds-checked array access to turn a corrupt
                                // match_offset into a catchable IndexOutOfRangeException; a
                                // direct C++ port using std::vector::operator[] would instead be
                                // undefined behavior (a potential out-of-bounds read) on the same
                                // malicious/corrupt input. Reject it explicitly instead.
                                if (match_offset <= 0 || match_offset > static_cast<int>(window_size)) return -1;

                                rundest = static_cast<int>(window_posn);
                                this_run -= match_length;

                                // Copy any wrapped around source data.
                                if (window_posn >= static_cast<uint32_t>(match_offset))
                                {
                                    // No wrap.
                                    runsrc = rundest - match_offset;
                                }
                                else
                                {
                                    runsrc = rundest + (static_cast<int>(window_size) - match_offset);
                                    copy_length = match_offset - static_cast<int>(window_posn);
                                    if (copy_length < match_length)
                                    {
                                        match_length -= copy_length;
                                        window_posn += static_cast<uint32_t>(copy_length);
                                        while (copy_length-- > 0) window[rundest++] = window[runsrc++];
                                        runsrc = 0;
                                    }
                                }
                                window_posn += static_cast<uint32_t>(match_length);

                                // Copy match data - no worries about destination wraps.
                                while (match_length-- > 0) window[rundest++] = window[runsrc++];
                            }
                        }
                        break;

                    case BlockType::Uncompressed:
                    {
                        if ((inData.getPositionProperty() + this_run) > (startpos + inLen)) return -1;
                        std::vector<uint8_t> temp_buffer(static_cast<std::size_t>(this_run));
                        inData.Read(temp_buffer.data(), 0, this_run);
                        std::copy(temp_buffer.begin(), temp_buffer.end(), window.begin() + window_posn);
                        window_posn += static_cast<uint32_t>(this_run);
                        break;
                    }

                    default:
                        return -1;
                }
            }
        }

        if (togo != 0) return -1;
        int start_window_pos = static_cast<int>(window_posn);
        if (start_window_pos == 0) start_window_pos = static_cast<int>(window_size);
        start_window_pos -= outLen;
        outData.Write(window.data(), start_window_pos, outLen);

        state_.window_posn = window_posn;
        state_.R0 = R0;
        state_.R1 = R1;
        state_.R2 = R2;

        // Intel E8 decoding: FNA's own port never actually finished this (its loop condition
        // never advances outData's position, and it always returns -1 immediately below,
        // matching its own "TODO: Finish intel E8 decoding" comment) -- reproduced verbatim,
        // not silently "fixed", since real .xnb content encoded with this option would already
        // fail identically against real FNA.
        if ((state_.frames_read++ < 32768) && state_.intel_filesize != 0)
        {
            if (outLen <= 6 || !state_.intel_started)
            {
                state_.intel_curpos += outLen;
            }
            else
            {
                const int dataend = outLen - 10;
                auto curpos = static_cast<uint32_t>(state_.intel_curpos);
                state_.intel_curpos = static_cast<int32_t>(curpos) + outLen;

                while (outData.getPositionProperty() < dataend)
                {
                    if (outData.ReadByte() != 0xE8) { curpos++; continue; }
                }
            }
            return -1;
        }
        return 0;
    }
}
