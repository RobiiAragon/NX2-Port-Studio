// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <optional>
#include <span>
#include "lower_to_maxwell.h"

namespace NX2 {
struct BranchHints {
    std::size_t uniform_count = 0;
    std::optional<std::size_t> reserved_bit_instruction;
};

// SM86 BRA (opcode 0x947) places its uniform-branch hint at bit 32, separate
// from the relative word-address displacement at bits 34..81. The inherited
// lowerer reads bits 32..81 as a byte displacement, which already accounts for
// the word-to-byte shift but accidentally includes the hint. Clear only that
// hint in the in-memory copy passed to it; predicates and destinations remain
// unchanged. This does not change reconvergence instructions or model them.
//
// Encoding reference (OpBra, set_rel_offset, get_rel_offset):
// https://gitlab.freedesktop.org/mesa/mesa/-/blob/553844bf54c818c06a3a05e657fc0a31b6114a98/src/nouveau/compiler/nak/sm70_encode.rs
// No Mesa source is copied here. The vendored Eglantine snapshot is unchanged.
inline BranchHints normalize_bra_uniform_hints(std::span<Shader::Ampere::Instruction> code) {
    BranchHints result;
    for (std::size_t index = 0; index < code.size(); ++index) {
        auto& instruction = code[index];
        if (instruction.Bits(0, 12) != 0x947) continue;
        // Bit 33 is outside both fields; do not silently erase an unknown mode.
        if (instruction.words[1] & 2U) {
            result.reserved_bit_instruction = index;
            return result;
        }
        if (instruction.words[1] & 1U) {
            instruction.words[1] &= ~1U;
            ++result.uniform_count;
        }
    }
    return result;
}
}
