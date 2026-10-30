// SPDX-License-Identifier: GPL-3.0-or-later
// Offline developer tool. Its output is intermediate instructions, not executable host code.
#include <bit>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include "lower_to_maxwell.h"

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::cerr << "Usage: nx2-ampere-lower <sm86-code.bin> <new-maxwell-words.bin>\n";
            return 2;
        }
        if (std::endian::native != std::endian::little)
            throw std::runtime_error("This tool requires a little-endian host");
        const std::filesystem::path input(argv[1]), output(argv[2]);
        const auto size = std::filesystem::file_size(input);
        if (size == 0 || size % 16 != 0 || size > 65536 * 16)
            throw std::runtime_error("Expected 1..65536 aligned 16-byte Ampere instructions, without a shader header");
        std::ifstream source(input, std::ios::binary);
        std::vector<Shader::Ampere::Instruction> code(size / 16);
        if (!source.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size)))
            throw std::runtime_error("Could not read the complete input");
        // Eden resolves BRX/JMX through a custom source-address map. Ryujinx's
        // Maxwell translator does not implement that ABI, so do not publish an
        // apparently compatible output for computed control flow.
        for (const auto& insn : code) {
            const auto op = insn.Bits(0, 12);
            if (op == 0x949 || op == 0x958)
                throw std::runtime_error("Computed branches require native address-map integration");
        }
        const auto lowered = Shader::Ampere::LowerToMaxwell(code);
        if (!lowered) {
            std::cerr << "Unsupported shader at byte " << lowered.error_offset << ": " << lowered.error << '\n';
            return 3;
        }
        // Use O_EXCL through an output stream descriptor: never replace another
        // invocation's result, a source file, or a dangling symlink.
        const int fd = ::open(output.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) throw std::runtime_error("Output exists or cannot be created");
        bool good = true;
        const auto* bytes = reinterpret_cast<const char*>(lowered.instructions.data());
        size_t remaining = lowered.instructions.size() * sizeof(u64);
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
                  << ",\"format\":\"maxwell-raw-words-no-header-or-scheduling\",\"metal_ready\":false}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
