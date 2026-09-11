// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-81: an LZX encoder for .xnb output.
//
// LZX is the compression Microsoft's own XNA 4.0 Content Pipeline produced, and it was the one
// gap in CNA's XNB writer: CNA has *read* LZX since plans/plan_xnb.md XNB-28. An encoder is a
// different program from a decoder, and this one is written against the decoder's own observable
// contract rather than derived from any encoder implementation:
//
//   * `LzxDecoder::Decompress` (src/Xnb/LzxDecoder.cpp) is the specification for the bit order,
//     the block header layout, the pretree-coded tree transmission, the position-slot tables and
//     the repeated-offset queue. Everything below is that function read backwards.
//   * `DecompressXnbPayload` (src/Xnb/XnbDecompression.cpp) is the specification for the
//     container's own block framing.
//   * The two committed externally produced LZX `.xnb` fixtures confirmed the framing
//     independently before a line of this was written: one carries a single explicit-size frame,
//     the other a full 0x8000 frame followed by an explicit-size one.
//
// Three design decisions are worth stating, because each is a trade rather than an oversight.
//
// **One verbatim block per frame.** LZX blocks and output frames are independent in the format: a
// block may span frames, and a frame may contain several blocks. Making them coincide costs a
// little ratio (the trees are retransmitted every 32 KiB, and a match may not reach across a
// frame boundary) and buys two things. First, a block's symbols then produce exactly the frame's
// byte count, so the decoder's `this_run` accounting can never overrun -- FNA's port, which CNA's
// decoder follows line by line, omits libmspack's own guard against a match that overshoots the
// requested run, and a stream that relies on that guard would decode into shifted output here.
// Second, it makes the encoder a single pass per frame with bounded memory.
//
// **Verbatim blocks only.** Aligned-offset blocks would save a few bits on offset-heavy data and
// need a fourth Huffman tree; uncompressed blocks would save nothing and need the bitstream
// realignment dance the decoder performs around them. Neither is needed for a conforming stream.
// What is *not* done here, deliberately, is the shortcut of emitting an uncompressed LZX block and
// calling the file compressed: this encoder builds real Huffman trees over real literal/match
// symbols.
//
// **Code lengths are limited to the decoder's table width.** The main and length trees are limited
// to 12 bits and the pretree to 6, which is exactly `MAINTREE_TABLEBITS`, `LENGTH_TABLEBITS` and
// `PRETREE_TABLEBITS`. Every code therefore resolves in `MakeDecodeTable`'s fast direct-lookup
// path and never reaches its secondary tree-walking path, and every transmitted tree is *complete*
// (Kraft sum exactly one), so no decode-table entry is ever a zero-length placeholder.

#include "CNA/Internal/Xnb/LzxEncoder.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <queue>
#include <string>

#include "CNA/Internal/Xnb/XnbByteWriter.hpp"

namespace CNA::Internal::Xnb
{
    namespace
    {
        constexpr std::uint32_t kMinMatch = 2u;            // LZX's own MIN_MATCH
        constexpr std::uint32_t kSearchMinMatch = 3u;      // shortest match the hash finds
        constexpr std::uint32_t kNumChars = 256u;
        constexpr std::uint32_t kNumPrimaryLengths = 7u;
        constexpr std::uint32_t kNumSecondaryLengths = 249u;
        // The length tree transmits symbols 0..247 plus 248; symbol 249 exists in the
        // decoder's table but is never given a code length, so the longest expressible
        // match is MIN_MATCH + NUM_PRIMARY_LENGTHS + 248 = 257.
        constexpr std::uint32_t kMaxMatch =
            kMinMatch + kNumPrimaryLengths + (kNumSecondaryLengths - 1u);
        constexpr std::uint32_t kPretreeElements = 20u;
        constexpr unsigned kMainTreeLengthLimit = 12u;     // MAINTREE_TABLEBITS
        constexpr unsigned kLengthTreeLengthLimit = 12u;   // LENGTH_TABLEBITS
        constexpr unsigned kPretreeLengthLimit = 6u;       // PRETREE_TABLEBITS
        constexpr std::uint32_t kMainTreeSymbols = 656u;   // NUM_CHARS + 50 * 8
        constexpr std::size_t kMaxBlockSizeField = 0xFEFFu; // 0xFF00 would look like the 0xFF form
        constexpr std::uint32_t kHashBits = 16u;

        /**
         * @brief The decoder's own extra-bit and position-base tables, computed identically.
         *
         * Recomputed here rather than shared with the decoder on purpose: a round trip against a
         * table the encoder and decoder both got wrong would prove nothing. These two loops are
         * the same shape as `LzxDecoder`'s, and the position-slot behaviour they describe is
         * asserted directly by `LzxEncoderTest`.
         */
        struct SlotTables
        {
            std::array<std::uint8_t, 52> extraBits{};
            std::array<std::uint32_t, 52> positionBase{};
        };

        [[nodiscard]] const SlotTables& Slots()
        {
            static const SlotTables tables = []
            {
                SlotTables built;
                for (int index = 0, bits = 0; index <= 50; index += 2)
                {
                    built.extraBits[static_cast<std::size_t>(index)] =
                        built.extraBits[static_cast<std::size_t>(index) + 1u] =
                            static_cast<std::uint8_t>(bits);
                    if (index != 0 && bits < 17) { ++bits; }
                }
                std::uint32_t base = 0u;
                for (int index = 0; index <= 51; ++index)
                {
                    built.positionBase[static_cast<std::size_t>(index)] = base;
                    if (index <= 50)
                    {
                        base += 1u << built.extraBits[static_cast<std::size_t>(index)];
                    }
                }
                return built;
            }();
            return tables;
        }

        [[nodiscard]] std::uint32_t PositionSlotCount(const int windowBits)
        {
            if (windowBits == 20) { return 42u; }
            if (windowBits == 21) { return 50u; }
            return static_cast<std::uint32_t>(windowBits) << 1;
        }

        /**
         * @brief Writes bits MSB-first into 16-bit words, each emitted little-endian.
         *
         * `LzxDecoder::BitBuffer` reads two bytes at a time as `(second << 8) | first` and
         * consumes the resulting word from its top bit down, so a word is little-endian in the
         * byte stream and big-endian in bit order. Both halves of that sentence matter.
         */
        class LzxBitWriter
        {
        public:
            explicit LzxBitWriter(std::vector<std::uint8_t>& out) : out_(out) {}

            /** @brief Appends the low @p bits bits of @p value, most significant first. */
            void Write(const std::uint32_t value, const unsigned bits)
            {
                if (bits == 0u) { return; }
                const std::uint32_t mask =
                    bits >= 32u ? 0xFFFFFFFFu : ((1u << bits) - 1u);
                accumulator_ = (accumulator_ << bits) | (value & mask);
                count_ += bits;
                while (count_ >= 16u)
                {
                    const std::uint32_t word =
                        (accumulator_ >> (count_ - 16u)) & 0xFFFFu;
                    out_.push_back(static_cast<std::uint8_t>(word & 0xFFu));
                    out_.push_back(static_cast<std::uint8_t>(word >> 8));
                    count_ -= 16u;
                }
            }

            /** @brief Pads to the next 16-bit boundary with zero bits. */
            void Flush()
            {
                if (count_ == 0u) { return; }
                const std::uint32_t word =
                    (accumulator_ << (16u - count_)) & 0xFFFFu;
                out_.push_back(static_cast<std::uint8_t>(word & 0xFFu));
                out_.push_back(static_cast<std::uint8_t>(word >> 8));
                accumulator_ = 0u;
                count_ = 0u;
            }

        private:
            std::vector<std::uint8_t>& out_;
            std::uint32_t accumulator_ = 0u;
            unsigned count_ = 0u;
        };

        /**
         * @brief Computes complete, length-limited canonical Huffman code lengths.
         *
         * Two properties are guaranteed, and both are load-bearing for the decoder:
         *
         *  * **No length exceeds @p limit**, so `MakeDecodeTable` resolves every code in its
         *    direct-lookup table.
         *  * **The code is complete** -- the Kraft sum is exactly one -- unless no symbol is used
         *    at all, in which case every length is zero, which is the one incomplete table LZX
         *    itself defines (an unused tree).
         *
         * Ordinary Huffman gives the optimal lengths and can exceed the limit; the repair below
         * then lengthens the least frequent symbols until the Kraft sum is no longer
         * over-subscribed and shortens the most frequent ones until it is not under-subscribed
         * either. Working in units of `2^(limit - length)` makes both loops exact integer
         * arithmetic, and the final re-assignment (shortest lengths to the most frequent symbols)
         * keeps the result optimal for the length multiset it arrived at.
         *
         * @param frequencies One count per symbol; a zero means the symbol is unused.
         * @param limit Maximum code length in bits, at most 16.
         * @return One length per symbol, zero for an unused symbol.
         */
        [[nodiscard]] std::vector<std::uint8_t> ComputeCodeLengths(
            const std::vector<std::uint64_t>& frequencies, const unsigned limit)
        {
            const std::size_t symbolCount = frequencies.size();
            std::vector<std::uint8_t> lengths(symbolCount, 0u);

            std::vector<std::size_t> used;
            for (std::size_t symbol = 0u; symbol < symbolCount; ++symbol)
            {
                if (frequencies[symbol] != 0u) { used.push_back(symbol); }
            }
            if (used.empty()) { return lengths; }
            if (used.size() == 1u)
            {
                // A single symbol cannot form a complete code on its own, and an incomplete table
                // leaves half the decode entries pointing at a zero-length symbol. Give an unused
                // symbol a code nobody will ever emit so the table is complete either way.
                const std::size_t only = used.front();
                lengths[only] = 1u;
                lengths[only == 0u ? 1u : 0u] = 1u;
                return lengths;
            }

            // Ordinary Huffman over (frequency, symbol) order, which makes every merge decision
            // deterministic.
            struct Node
            {
                std::uint64_t weight = 0u;
                std::int32_t left = -1;
                std::int32_t right = -1;
                std::int32_t symbol = -1;
            };
            std::vector<Node> nodes;
            nodes.reserve(used.size() * 2u);
            std::vector<std::size_t> order = used;
            std::sort(order.begin(), order.end(),
                      [&frequencies](const std::size_t left, const std::size_t right)
                      {
                          if (frequencies[left] != frequencies[right])
                          {
                              return frequencies[left] < frequencies[right];
                          }
                          return left < right;
                      });
            for (const std::size_t symbol : order)
            {
                nodes.push_back({frequencies[symbol], -1, -1,
                                 static_cast<std::int32_t>(symbol)});
            }

            struct HeapEntry
            {
                std::uint64_t weight;
                std::uint32_t sequence;
                std::uint32_t node;
            };
            const auto worse = [](const HeapEntry& left, const HeapEntry& right)
            {
                if (left.weight != right.weight) { return left.weight > right.weight; }
                return left.sequence > right.sequence;
            };
            std::priority_queue<HeapEntry, std::vector<HeapEntry>, decltype(worse)> heap(worse);
            std::uint32_t sequence = 0u;
            for (std::uint32_t index = 0u; index < nodes.size(); ++index)
            {
                heap.push({nodes[index].weight, sequence++, index});
            }
            while (heap.size() > 1u)
            {
                const HeapEntry first = heap.top();
                heap.pop();
                const HeapEntry second = heap.top();
                heap.pop();
                Node parent;
                parent.weight = first.weight + second.weight;
                parent.left = static_cast<std::int32_t>(first.node);
                parent.right = static_cast<std::int32_t>(second.node);
                nodes.push_back(parent);
                heap.push({parent.weight, sequence++,
                           static_cast<std::uint32_t>(nodes.size() - 1u)});
            }

            // Leaf depths, iteratively -- a pathological frequency distribution makes this tree
            // as deep as it has symbols, and recursion there is a stack overflow waiting for the
            // right input.
            std::vector<std::uint32_t> depth(nodes.size(), 0u);
            std::vector<std::uint32_t> stack{static_cast<std::uint32_t>(nodes.size() - 1u)};
            std::vector<std::uint32_t> leafDepth(symbolCount, 0u);
            while (!stack.empty())
            {
                const std::uint32_t node = stack.back();
                stack.pop_back();
                const Node& current = nodes[node];
                if (current.symbol >= 0)
                {
                    leafDepth[static_cast<std::size_t>(current.symbol)] =
                        std::max<std::uint32_t>(depth[node], 1u);
                    continue;
                }
                depth[static_cast<std::size_t>(current.left)] = depth[node] + 1u;
                depth[static_cast<std::size_t>(current.right)] = depth[node] + 1u;
                stack.push_back(static_cast<std::uint32_t>(current.left));
                stack.push_back(static_cast<std::uint32_t>(current.right));
            }

            // Clamp, then repair the Kraft sum in units of 2^(limit - length).
            std::vector<std::uint32_t> working(used.size(), 0u);
            for (std::size_t index = 0u; index < order.size(); ++index)
            {
                working[index] = std::min<std::uint32_t>(leafDepth[order[index]], limit);
            }
            const std::uint64_t target = std::uint64_t{1} << limit;
            const auto kraft = [&working, limit]
            {
                std::uint64_t sum = 0u;
                for (const std::uint32_t length : working)
                {
                    sum += std::uint64_t{1} << (limit - length);
                }
                return sum;
            };

            // `order` is ascending by frequency, so lengthening from the front penalizes the
            // least frequent symbol available and shortening from the back rewards the most
            // frequent one.
            std::uint64_t sum = kraft();
            while (sum > target)
            {
                bool changed = false;
                for (std::size_t index = 0u; index < working.size(); ++index)
                {
                    if (working[index] < limit)
                    {
                        sum -= std::uint64_t{1} << (limit - working[index] - 1u);
                        ++working[index];
                        changed = true;
                        break;
                    }
                }
                // Every code is already at the limit, so the sum is the symbol count, which
                // cannot exceed 2^limit for any tree in this format.
                if (!changed) { break; }
            }
            while (sum < target)
            {
                bool changed = false;
                for (std::size_t index = working.size(); index-- > 0u;)
                {
                    if (working[index] <= 1u) { continue; }
                    const std::uint64_t gain = std::uint64_t{1} << (limit - working[index]);
                    if (sum + gain > target) { continue; }
                    sum += gain;
                    --working[index];
                    changed = true;
                    break;
                }
                if (!changed) { break; }
            }

            // Optimal assignment for the multiset arrived at: shortest codes to the most frequent
            // symbols. `order` is ascending by frequency and `working` is sorted ascending, so the
            // two are paired end to end.
            std::sort(working.begin(), working.end());
            for (std::size_t index = 0u; index < order.size(); ++index)
            {
                lengths[order[index]] =
                    static_cast<std::uint8_t>(working[working.size() - 1u - index]);
            }
            return lengths;
        }

        /**
         * @brief Assigns canonical Huffman codes from code lengths.
         *
         * `MakeDecodeTable` fills its table by walking lengths from 1 upwards and symbols from 0
         * upwards, advancing by `2^(tableBits - length)` per symbol, which makes the code of a
         * symbol exactly its rank in (length, symbol) order at that length. That is the ordinary
         * canonical assignment, and this is it.
         *
         * @param lengths One code length per symbol; zero means unused.
         * @return One code per symbol, right-aligned in the low `lengths[symbol]` bits.
         */
        [[nodiscard]] std::vector<std::uint16_t> BuildCanonicalCodes(
            const std::vector<std::uint8_t>& lengths)
        {
            std::vector<std::uint16_t> codes(lengths.size(), 0u);
            std::uint32_t code = 0u;
            for (unsigned bits = 1u; bits <= 16u; ++bits)
            {
                for (std::size_t symbol = 0u; symbol < lengths.size(); ++symbol)
                {
                    if (lengths[symbol] == bits)
                    {
                        codes[symbol] = static_cast<std::uint16_t>(code++);
                    }
                }
                code <<= 1;
            }
            return codes;
        }

        /** @brief One pretree instruction: a symbol plus however many raw bits follow it. */
        struct PretreeOp
        {
            std::uint32_t symbol = 0u;
            unsigned extraBits = 0u;
            std::uint32_t extraValue = 0u;
        };

        /**
         * @brief Encodes one tree's code lengths the way `LzxDecoder::ReadLengths` reads them.
         *
         * The wire form is a 20-symbol pretree (eight-bit-free, four raw bits per length) followed
         * by pretree-coded instructions covering `[first, last)`: symbols 0-16 carry
         * `(previous - new) mod 17` for one position, 17 a run of 4-19 zeros, 18 a run of 20-51
         * zeros. Symbol 19 (a run of one repeated delta) is not emitted -- it is an optional
         * saving, not part of the contract, and leaving it out keeps this function auditable
         * against the decoder's own branch table.
         *
         * @param writer Bit sink.
         * @param lengths The new code lengths.
         * @param previous The lengths the decoder currently holds; updated to @p lengths on return.
         * @param first First symbol index to transmit.
         * @param last One past the last symbol index to transmit.
         */
        void EmitTreeLengths(LzxBitWriter& writer, const std::vector<std::uint8_t>& lengths,
                             std::vector<std::uint8_t>& previous, const std::size_t first,
                             const std::size_t last)
        {
            std::vector<PretreeOp> operations;
            operations.reserve(last - first);
            std::vector<std::uint64_t> pretreeFrequencies(kPretreeElements, 0u);

            std::size_t position = first;
            while (position < last)
            {
                if (lengths[position] == 0u)
                {
                    std::size_t run = 0u;
                    while (position + run < last && lengths[position + run] == 0u) { ++run; }
                    if (run >= 20u)
                    {
                        const std::size_t emitted = std::min<std::size_t>(run, 51u);
                        operations.push_back(
                            {18u, 5u, static_cast<std::uint32_t>(emitted - 20u)});
                        pretreeFrequencies[18] += 1u;
                        position += emitted;
                        continue;
                    }
                    if (run >= 4u)
                    {
                        const std::size_t emitted = std::min<std::size_t>(run, 19u);
                        operations.push_back(
                            {17u, 4u, static_cast<std::uint32_t>(emitted - 4u)});
                        pretreeFrequencies[17] += 1u;
                        position += emitted;
                        continue;
                    }
                }
                const std::uint32_t delta =
                    (static_cast<std::uint32_t>(previous[position]) + 17u -
                     static_cast<std::uint32_t>(lengths[position])) %
                    17u;
                operations.push_back({delta, 0u, 0u});
                pretreeFrequencies[delta] += 1u;
                ++position;
            }

            const std::vector<std::uint8_t> pretreeLengths =
                ComputeCodeLengths(pretreeFrequencies, kPretreeLengthLimit);
            const std::vector<std::uint16_t> pretreeCodes = BuildCanonicalCodes(pretreeLengths);

            for (std::size_t symbol = 0u; symbol < kPretreeElements; ++symbol)
            {
                writer.Write(pretreeLengths[symbol], 4u);
            }
            for (const PretreeOp& operation : operations)
            {
                writer.Write(pretreeCodes[operation.symbol],
                             pretreeLengths[operation.symbol]);
                writer.Write(operation.extraValue, operation.extraBits);
            }

            for (std::size_t index = first; index < last; ++index)
            {
                previous[index] = lengths[index];
            }
        }

        /** @brief One emitted literal or match, resolved to its wire symbols. */
        struct EncodedSymbol
        {
            std::uint32_t mainSymbol = 0u;
            std::int32_t lengthSymbol = -1;
            unsigned extraBits = 0u;
            std::uint32_t extraValue = 0u;
        };

        /**
         * @brief Greedy longest-match finder over a bounded hash chain.
         *
         * The chain is indexed by position within the sliding window, so its memory is the window
         * rather than the payload: a 64 MiB asset costs the same 256 KiB as a 64 KiB one. Search
         * depth is fixed, which is what keeps the output reproducible -- an adaptive cut-off would
         * make the bytes depend on how the search happened to go.
         */
        class MatchFinder
        {
        public:
            MatchFinder(const std::span<const std::uint8_t> data, const std::uint32_t windowSize,
                        const std::uint32_t depth)
                : data_(data)
                , head_(std::size_t{1} << kHashBits, -1)
                , chain_(windowSize, -1)
                , windowMask_(windowSize - 1u)
                , depth_(depth)
            {
            }

            /** @brief Records @p position so later searches can find it. */
            void Insert(const std::size_t position)
            {
                if (position + kSearchMinMatch > data_.size()) { return; }
                const std::size_t slot = Hash(position);
                chain_[position & windowMask_] = head_[slot];
                head_[slot] = static_cast<std::int64_t>(position);
            }

            /**
             * @brief Finds the longest match for @p position.
             *
             * @param position Absolute position to match at.
             * @param matchLimit One past the last byte a match may consume.
             * @param maxOffset Largest offset the position-slot tables can express.
             * @param length Receives the match length when one is found.
             * @param offset Receives the match distance when one is found.
             * @return True when a match of at least @ref kSearchMinMatch bytes was found.
             */
            [[nodiscard]] bool Find(const std::size_t position, const std::size_t matchLimit,
                                    const std::uint32_t maxOffset, std::size_t& length,
                                    std::size_t& offset) const
            {
                if (position + kSearchMinMatch > matchLimit) { return false; }
                std::int64_t candidate = head_[Hash(position)];
                std::size_t bestLength = 0u;
                std::size_t bestOffset = 0u;
                std::uint32_t remaining = depth_;
                const std::size_t ceiling =
                    std::min<std::size_t>(matchLimit - position, kMaxMatch);
                while (candidate >= 0 && remaining-- > 0u)
                {
                    const std::size_t candidatePosition = static_cast<std::size_t>(candidate);
                    const std::size_t distance = position - candidatePosition;
                    if (distance == 0u || distance > maxOffset) { break; }
                    std::size_t matched = 0u;
                    while (matched < ceiling &&
                           data_[candidatePosition + matched] == data_[position + matched])
                    {
                        ++matched;
                    }
                    if (matched > bestLength)
                    {
                        bestLength = matched;
                        bestOffset = distance;
                        if (bestLength == ceiling) { break; }
                    }
                    candidate = chain_[candidatePosition & windowMask_];
                }
                if (bestLength < kSearchMinMatch) { return false; }
                length = bestLength;
                offset = bestOffset;
                return true;
            }

        private:
            [[nodiscard]] std::size_t Hash(const std::size_t position) const
            {
                const std::uint32_t sequence =
                    (static_cast<std::uint32_t>(data_[position]) << 16) |
                    (static_cast<std::uint32_t>(data_[position + 1u]) << 8) |
                    static_cast<std::uint32_t>(data_[position + 2u]);
                return static_cast<std::size_t>((sequence * 2654435761u) >> (32u - kHashBits));
            }

            std::span<const std::uint8_t> data_;
            std::vector<std::int64_t> head_;
            std::vector<std::int64_t> chain_;
            std::uint32_t windowMask_ = 0u;
            std::uint32_t depth_ = 0u;
        };

        /** @brief The repeated-offset LRU queue, kept in step with the decoder's own. */
        struct RepeatedOffsets
        {
            std::uint32_t r0 = 1u;
            std::uint32_t r1 = 1u;
            std::uint32_t r2 = 1u;
        };
    }

    std::vector<std::uint8_t> CompressXnbLzxPayload(const std::span<const std::uint8_t> payload,
                                                     const LzxEncodeOptions& options)
    {
        if (options.windowBits != 16)
        {
            throw XnbWriteException(
                "LZX: the .xnb container always decompresses with a 64 KiB window, so window "
                "exponent " + std::to_string(options.windowBits) +
                " would produce a file CNA's own reader could not load.");
        }
        if (options.frameSize == 0u || options.frameSize > 0x8000u)
        {
            throw XnbWriteException(
                "LZX: an .xnb frame carries at most 0x8000 uncompressed bytes, not " +
                std::to_string(options.frameSize) + ".");
        }
        if (options.matchSearchDepth == 0u)
        {
            throw XnbWriteException("LZX: the match search depth must be at least one.");
        }

        std::vector<std::uint8_t> output;
        if (payload.empty()) { return output; }

        const SlotTables& slots = Slots();
        const std::uint32_t slotCount = PositionSlotCount(options.windowBits);
        const std::uint32_t windowSize = 1u << options.windowBits;
        // The largest distance the position-slot tables can express: the formatted offset is the
        // distance plus two, and it must fall inside the last slot's range.
        const std::uint32_t maxOffset =
            slots.positionBase[slotCount] - 2u - 1u;
        const std::uint32_t mainSymbolCount = kNumChars + (slotCount << 3);

        MatchFinder finder(payload, windowSize, options.matchSearchDepth);
        RepeatedOffsets repeats;
        std::vector<std::uint8_t> previousMain(kMainTreeSymbols, 0u);
        std::vector<std::uint8_t> previousLength(kNumSecondaryLengths, 0u);

        std::size_t position = 0u;
        bool wroteHeaderBit = false;
        while (position < payload.size())
        {
            const std::size_t frameStart = position;
            const std::size_t frameEnd =
                std::min<std::size_t>(payload.size(), frameStart + options.frameSize);

            std::vector<EncodedSymbol> symbols;
            symbols.reserve((frameEnd - frameStart) / 2u + 8u);
            std::vector<std::uint64_t> mainFrequencies(mainSymbolCount, 0u);
            std::vector<std::uint64_t> lengthFrequencies(kNumSecondaryLengths, 0u);

            while (position < frameEnd)
            {
                std::size_t matchLength = 0u;
                std::size_t matchOffset = 0u;
                const std::uint32_t reachable =
                    static_cast<std::uint32_t>(std::min<std::size_t>(position, maxOffset));
                const bool found = reachable > 0u &&
                    finder.Find(position, frameEnd, reachable, matchLength, matchOffset);

                if (!found)
                {
                    EncodedSymbol literal;
                    literal.mainSymbol = payload[position];
                    mainFrequencies[literal.mainSymbol] += 1u;
                    symbols.push_back(literal);
                    finder.Insert(position);
                    ++position;
                    continue;
                }

                // Prefer a repeated offset over an explicit slot at equal length: it costs no
                // extra bits at all and keeps the LRU queue useful for what follows.
                for (const std::uint32_t repeat : {repeats.r0, repeats.r1, repeats.r2})
                {
                    if (repeat == 0u || repeat > reachable || repeat == matchOffset) { continue; }
                    const std::size_t ceiling =
                        std::min<std::size_t>(frameEnd - position, kMaxMatch);
                    std::size_t matched = 0u;
                    while (matched < ceiling &&
                           payload[position - repeat + matched] == payload[position + matched])
                    {
                        ++matched;
                    }
                    if (matched >= matchLength && matched >= kMinMatch)
                    {
                        matchLength = matched;
                        matchOffset = repeat;
                        break;
                    }
                }

                EncodedSymbol match;
                std::uint32_t slot = 0u;
                if (matchOffset == repeats.r0)
                {
                    slot = 0u;
                }
                else if (matchOffset == repeats.r1)
                {
                    slot = 1u;
                    repeats.r1 = repeats.r0;
                    repeats.r0 = static_cast<std::uint32_t>(matchOffset);
                }
                else if (matchOffset == repeats.r2)
                {
                    slot = 2u;
                    repeats.r2 = repeats.r0;
                    repeats.r0 = static_cast<std::uint32_t>(matchOffset);
                }
                else
                {
                    const std::uint32_t formatted =
                        static_cast<std::uint32_t>(matchOffset) + 2u;
                    slot = slotCount - 1u;
                    while (slot > 3u && slots.positionBase[slot] > formatted) { --slot; }
                    match.extraBits = slots.extraBits[slot];
                    match.extraValue = formatted - slots.positionBase[slot];
                    repeats.r2 = repeats.r1;
                    repeats.r1 = repeats.r0;
                    repeats.r0 = static_cast<std::uint32_t>(matchOffset);
                }

                const std::uint32_t excess =
                    static_cast<std::uint32_t>(matchLength) - kMinMatch;
                const std::uint32_t lengthHeader = std::min(excess, kNumPrimaryLengths);
                if (lengthHeader == kNumPrimaryLengths)
                {
                    match.lengthSymbol =
                        static_cast<std::int32_t>(excess - kNumPrimaryLengths);
                    lengthFrequencies[static_cast<std::size_t>(match.lengthSymbol)] += 1u;
                }
                match.mainSymbol = kNumChars + (slot << 3) + lengthHeader;
                mainFrequencies[match.mainSymbol] += 1u;
                symbols.push_back(match);

                for (std::size_t inserted = 0u; inserted < matchLength; ++inserted)
                {
                    finder.Insert(position + inserted);
                }
                position += matchLength;
            }

            const std::vector<std::uint8_t> mainLengths =
                ComputeCodeLengths(mainFrequencies, kMainTreeLengthLimit);
            const std::vector<std::uint16_t> mainCodes = BuildCanonicalCodes(mainLengths);
            const std::vector<std::uint8_t> lengthLengths =
                ComputeCodeLengths(lengthFrequencies, kLengthTreeLengthLimit);
            const std::vector<std::uint16_t> lengthCodes = BuildCanonicalCodes(lengthLengths);

            std::vector<std::uint8_t> frame;
            frame.reserve((frameEnd - frameStart) + 1024u);
            LzxBitWriter writer(frame);

            if (!wroteHeaderBit)
            {
                // No Intel E8 call translation. CNA's decoder -- like FNA's, which never finished
                // that transform -- fails outright on a non-zero filesize here, and no .xnb uses
                // it.
                writer.Write(0u, 1u);
                wroteHeaderBit = true;
            }

            const std::uint32_t blockLength =
                static_cast<std::uint32_t>(frameEnd - frameStart);
            writer.Write(1u, 3u);                       // verbatim block
            writer.Write(blockLength >> 8, 16u);
            writer.Write(blockLength & 0xFFu, 8u);

            EmitTreeLengths(writer, mainLengths, previousMain, 0u, kNumChars);
            EmitTreeLengths(writer, mainLengths, previousMain, kNumChars, mainSymbolCount);
            EmitTreeLengths(writer, lengthLengths, previousLength, 0u, kNumSecondaryLengths);

            for (const EncodedSymbol& symbol : symbols)
            {
                writer.Write(mainCodes[symbol.mainSymbol], mainLengths[symbol.mainSymbol]);
                if (symbol.lengthSymbol >= 0)
                {
                    const std::size_t index = static_cast<std::size_t>(symbol.lengthSymbol);
                    writer.Write(lengthCodes[index], lengthLengths[index]);
                }
                writer.Write(symbol.extraValue, symbol.extraBits);
            }
            writer.Flush();

            if (frame.size() > kMaxBlockSizeField)
            {
                // Unreachable with a 12-bit code-length ceiling: 0x8000 symbols at 12 bits is
                // under 50 KiB including every tree. Checked rather than assumed, because the
                // alternative is a block-size field that silently aliases the 0xFF frame marker.
                throw XnbWriteException(
                    "LZX: a frame compressed to " + std::to_string(frame.size()) +
                    " bytes, which the container's 16-bit block-size field cannot carry.");
            }

            const bool explicitFrameSize = blockLength != 0x8000u;
            if (explicitFrameSize)
            {
                output.push_back(0xFFu);
                output.push_back(static_cast<std::uint8_t>((blockLength >> 8) & 0xFFu));
                output.push_back(static_cast<std::uint8_t>(blockLength & 0xFFu));
            }
            output.push_back(static_cast<std::uint8_t>((frame.size() >> 8) & 0xFFu));
            output.push_back(static_cast<std::uint8_t>(frame.size() & 0xFFu));
            output.insert(output.end(), frame.begin(), frame.end());
        }

        // Five zero bytes after the last block, and this is not padding for tidiness: a genuine
        // Microsoft XNA 4.0 runtime REFUSES a compressed .xnb without them, with
        // `InvalidOperationException: Error decompressing content data.`
        //
        // Measured, not reasoned about (tests/interop/xna40, 2026-09-06). The same asset was
        // handed to the real runtime with 0, 1, 2, 3, 4, 5 and 6 trailing bytes: everything up to
        // four failed and five loaded. Two of them are the chunk-size field of the next chunk,
        // which the reader consumes before it notices the stream has ended -- a zero size is what
        // stops it -- and the other three are slack for the LZX bit buffer, which fills itself a
        // sixteen-bit word at a time and reads past the last byte it actually consumes. A
        // Microsoft-written file ends the same way: `Explosion.xnb` in the MonoGame corpus has
        // exactly five zero bytes after its final block.
        //
        // Nothing in CNA noticed for the same reason nothing could: CNA's own decoder stops when
        // it has the declared number of decompressed bytes and never reads ahead, and the
        // independent Python parser does the same. Two implementations agreeing is not the same
        // as the one that matters agreeing.
        output.insert(output.end(), 5u, 0u);

        return output;
    }
}
