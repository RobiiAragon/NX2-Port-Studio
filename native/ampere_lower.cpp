// SPDX-License-Identifier: GPL-3.0-or-later
// Offline developer tool. Its output is intermediate instructions, not executable host code.
#include <bit>
#include <array>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include "lower_to_maxwell.h"
#include "ampere_branch_compat.h"

#ifndef NX2_AMPERE_SOURCE_SHA
#define NX2_AMPERE_SOURCE_SHA "unverified"
#endif
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--source-sha") {
        std::cout << NX2_AMPERE_SOURCE_SHA << '\n'; return 0;
    }
    try {
        const bool graphics = argc == 4 && std::string_view(argv[1]) == "--graphics";
        if (argc != 3 && !graphics) {
            std::cerr << "Usage: nx2-ampere-lower [--graphics] <sm86-code.bin> <new-maxwell.bin>\n";
            return 2;
        }
        if (std::endian::native != std::endian::little)
            throw std::runtime_error("This tool requires a little-endian host");
        const std::filesystem::path input(argv[graphics ? 2 : 1]), output(argv[graphics ? 3 : 2]);
        const auto size = std::filesystem::file_size(input);
        const size_t header_size = graphics ? 128 : 0;
        if (size <= header_size || size % 16 != 0 || size > 65536 * 16 + header_size)
            throw std::runtime_error(graphics ? "Expected a 128-byte graphics header and 1..65536 aligned 16-byte Ampere instructions" : "Expected 1..65536 aligned 16-byte Ampere instructions, without a shader header");
        std::ifstream source(input, std::ios::binary);
        std::array<u64, 16> header{};
        if (graphics && !source.read(reinterpret_cast<char*>(header.data()), header_size))
            throw std::runtime_error("Truncated native graphics header");
        const u32 stage = (static_cast<u32>(header[0]) >> 10) & 15;
        if (graphics && stage != 1 && stage != 5)
            throw std::runtime_error("Graphics adapter currently accepts only vertex and fragment stages");
        std::vector<Shader::Ampere::Instruction> code((size - header_size) / 16);
        if (!source.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size - header_size)))
            throw std::runtime_error("Could not read the complete input");
        const auto branch_hints = NX2::normalize_bra_uniform_hints(code);
        if (branch_hints.reserved_bit_instruction) {
            std::cerr << "Unsupported shader at byte " << *branch_hints.reserved_bit_instruction * 16
                      << ": reserved BRA bit 33\n";
            return 3;
        }
        // Eden resolves BRX/JMX through a custom source-address map. Ryujinx's
        // Maxwell translator does not implement that ABI, so do not publish an
        // apparently compatible output for computed control flow.
        for (const auto& insn : code) {
            const auto op = insn.Bits(0, 12);
            if (op == 0x949 || op == 0x958)
                throw std::runtime_error("Computed branches require native address-map integration");
            // The vendored HMMA path is controlled by EDEN_NX2_TENSOR and
            // includes a D=C diagnostic stub. Neither that stub nor the opt-in
            // expansion is admitted by this adapter. Reject before consulting
            // the vendor code, regardless of the invoking process's environment.
            if (op == 0x23c) {
                std::cerr << "Unsupported native matrix multiply: HMMA semantics are not admitted\n";
                return 3;
            }
        }
        const auto lowered = Shader::Ampere::LowerToMaxwell(code);
        if (!lowered) {
            std::cerr << "Unsupported shader at byte " << lowered.error_offset << ": " << lowered.error << '\n';
            return 3;
        }
        if (graphics) {
            for (const u64 instruction : lowered.instructions) {
                // The pinned Ryujinx decoder currently treats unconditional
                // CAL as ending the caller instead of decoding its continuation.
                // Reject real calls/returns, not native CALL idioms which the
                // lowerer proves are ordinary predicated branches.
                const u64 opcode = instruction >> 52;
                if (opcode == 0xe26 || opcode == 0xe32)
                    throw std::runtime_error("Graphics calls/returns require translator continuation integration");
            }
        }
        std::vector<u64> packed;
        if (graphics) {
            // The lowerer's direct branch relocations already use this layout:
            // 80-byte Maxwell SPH, then a scheduling word at each 32-byte boundary.
            packed.assign(header.begin(), header.begin() + 10);
            for (const u64 instruction : lowered.instructions) {
                if (packed.size() % 4 == 0) packed.push_back(0);
                packed.push_back(instruction);
            }
            packed.push_back(0);
            // Ryujinx Decoder.FillBlock counts scheduling slots relative to
            // the post-SPH code start, unlike Eden's absolute-file convention.
            // Prefix one scheduling word and one NOP, shifting the complete
            // Eden body by 16 bytes. Its scheduling slots now land at relative
            // 32,64,...; all relative BRA/CAL source/target distances survive.
            // Calls/returns and computed native-PC branches remain rejected
            // above; no return continuation or address map is faked.
            packed.insert(packed.begin() + 10, {0, 0x50b0000000070f00ULL});
        }
        const auto& words = graphics ? packed : lowered.instructions;
        // Use O_EXCL through an output stream descriptor: never replace another
        // invocation's result, a source file, or a dangling symlink.
        const int fd = ::open(output.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) throw std::runtime_error("Output exists or cannot be created");
        bool good = true;
        const auto* bytes = reinterpret_cast<const char*>(words.data());
        size_t remaining = words.size() * sizeof(u64);
        while (remaining) {
            const auto written = ::write(fd, bytes, remaining);
            if (written <= 0) { good = false; break; }
            bytes += written; remaining -= static_cast<size_t>(written);
        }
        if (::close(fd) != 0) good = false;
        if (!good) {
            std::filesystem::remove(output);
            throw std::runtime_error("Could not write the output");
        }
        std::cout << "{\"schema_version\":1,\"source_bytes\":" << lowered.source_size
                  << ",\"maxwell_words\":" << lowered.instructions.size()
                  << ",\"required_texture_buffer\":" << lowered.required_texture_buffer
                  << ",\"normalized_uniform_branches\":" << branch_hints.uniform_count
                  << ",\"format\":\"" << (graphics ? "maxwell-graphics-ryujinx-prefixed-scheduling" : "maxwell-raw-words-no-header-or-scheduling")
                  << "\",\"metal_ready\":false}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
