# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded, offline readers for the audited NVN2 201.11 shader containers.

These readers describe input bytes. They do not admit a shader to the Metal
runtime, lower native instructions, or establish GPU execution compatibility.
Control versions and BNSH program extensions outside the audited layout fail
explicitly. In particular, the NVN1 control offsets are never used here.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import struct

from .errors import DumpError
from .formats import region


CONTROL_MAGIC = 0x98761234
CONTROL_HEADER_BYTES = 0xD8
CONTROL_VERSION = (201, 11)
MAX_CONTROL_BYTES = 16 * 1024 * 1024
MAX_CODE_BYTES = 64 * 1024 * 1024
MAX_VARIATIONS = 1_000_000
MAX_BNSH_BYTES = 512 * 1024 * 1024
MAX_BNSH_SHADER_BYTES = 256 * 1024 * 1024
STAGES = ("vs", "ps", "gs", "hs", "ds", "cs")
BNSH_STAGES = ("vs", "hs", "ds", "gs", "ps", "cs")
SPH_STAGES = {1: "vs", 2: "hs", 3: "ds", 4: "gs", 5: "ps"}
SPIRV_STAGES = {0: "vs", 1: "hs", 2: "ds", 3: "gs", 4: "ps", 5: "cs"}


def _unsupported(message: str) -> DumpError:
    return DumpError(message, code="unsupported-shader-format")


@dataclass(frozen=True)
class SpirvEntryPoint:
    stage: str
    name: str


@dataclass(frozen=True)
class SpirvModule:
    data: bytes
    version: tuple[int, int]
    entry_points: tuple[SpirvEntryPoint, ...]


def read_spirv(data: bytes) -> SpirvModule:
    """Check framing, not SPIR-V semantics; use spirv-val before compilation."""
    if len(data) < 20 or len(data) % 4:
        raise DumpError("NVN2 embedded SPIR-V is truncated or not word aligned")
    magic, version, _, bound, schema = struct.unpack_from("<5I", data)
    if (magic != 0x07230203 or version & 0xFF0000FF or version >> 16 != 1 or
            version >> 8 & 0xFF > 6 or schema or not bound):
        raise DumpError("NVN2 embedded SPIR-V has an invalid or unsupported header")
    entry_points = []
    offset = 20
    while offset < len(data):
        instruction = struct.unpack_from("<I", data, offset)[0]
        count, opcode = instruction >> 16, instruction & 0xFFFF
        if not count:
            raise DumpError("NVN2 embedded SPIR-V has a zero-length instruction")
        instruction_data = region(data, offset, count * 4, "NVN2 SPIR-V instruction")
        if opcode == 15:  # OpEntryPoint: execution model, function ID, literal name, interfaces.
            if count < 4:
                raise DumpError("NVN2 embedded SPIR-V has a truncated entry point")
            model, function = struct.unpack_from("<II", instruction_data, 4)
            if model not in SPIRV_STAGES:
                raise _unsupported(f"NVN2 embedded SPIR-V execution model {model} is not audited")
            if not 0 < function < bound:
                raise DumpError("NVN2 embedded SPIR-V entry point ID exceeds its bound")
            raw_name = instruction_data[12:]
            end = raw_name.find(b"\0")
            if end < 0:
                raise DumpError("NVN2 embedded SPIR-V entry point name is unterminated")
            try:
                name = raw_name[:end].decode("utf-8")
            except UnicodeDecodeError as error:
                raise DumpError("NVN2 embedded SPIR-V entry point name is not UTF-8") from error
            if not name or any(raw_name[end:((end // 4) + 1) * 4]):
                raise DumpError("NVN2 embedded SPIR-V entry point has invalid string padding")
            entry_points.append(SpirvEntryPoint(SPIRV_STAGES[model], name))
        offset += count * 4
    if not entry_points:
        raise DumpError("NVN2 embedded SPIR-V has no entry point")
    return SpirvModule(data, (version >> 16, version >> 8 & 0xFF), tuple(entry_points))


@dataclass(frozen=True)
class ShaderControl:
    data: bytes
    version: tuple[int, int]
    stage: str
    code_bytes: int
    compiler_data: bytes
    source_spirv: SpirvModule | None


def read_control(data: bytes) -> ShaderControl:
    """Read exactly the declared section; permit outer container padding.

    Sizes at +0x30 and +0x38 are u32 fields, independently exposed by the
    original driver. Reading either as u64 incorrectly folds in the next field.
    The stage is the API stage byte at +0x56, not the native SPH stage enum.
    """
    region(data, 0, CONTROL_HEADER_BYTES, "NVN2 shader control header")
    magic, major, minor = struct.unpack_from("<III", data)
    if magic != CONTROL_MAGIC:
        raise DumpError("NVN2 shader control magic does not match")
    if (major, minor) != CONTROL_VERSION:
        raise _unsupported(f"NVN2 shader control version {major}.{minor} is not audited")
    size = struct.unpack_from("<I", data, 0x30)[0]
    code_bytes = struct.unpack_from("<I", data, 0x38)[0]
    if not CONTROL_HEADER_BYTES <= size <= MAX_CONTROL_BYTES:
        raise DumpError("NVN2 shader control declares an invalid section size")
    if not 0 < code_bytes <= MAX_CODE_BYTES:
        raise DumpError("NVN2 shader control declares an invalid native code size")
    control = region(data, 0, size, "NVN2 shader control")
    stage_number = control[0x56]
    if stage_number >= len(STAGES):
        raise _unsupported(f"NVN2 shader stage {stage_number} is not audited")
    stage = STAGES[stage_number]
    compiler_offset, compiler_bytes = struct.unpack_from("<II", control, 0xBC)
    if compiler_offset < CONTROL_HEADER_BYTES or compiler_offset > size:
        raise DumpError("NVN2 compiler data overlaps its header or exceeds the control section")
    compiler = region(control, compiler_offset, compiler_bytes, "NVN2 compiler data")
    spirv = None
    if compiler:
        region(compiler, 0, 0x50, "NVN2 compiler data header")
        if struct.unpack_from("<III", compiler) != (0x37F20897, major, minor):
            raise _unsupported("NVN2 compiler data layout is not audited")
        # Optional source module retained by the compiler; shipping BNSH controls
        # commonly omit this entire block. Its bindings are source API slots,
        # distinct from the translated shader's generated resource bindings.
        if struct.unpack_from("<I", compiler, 0x2C)[0] != 0x48:
            raise _unsupported("NVN2 compiler source-record layout is not audited")
        length, source_stage = struct.unpack_from("<II", compiler, 0x48)
        if source_stage != stage_number:
            raise DumpError("NVN2 compiler source stage disagrees with its control section")
        spirv = read_spirv(region(compiler, 0x50, length, "NVN2 compiler SPIR-V"))
        if any(entry.stage != stage for entry in spirv.entry_points):
            raise DumpError("NVN2 embedded SPIR-V stage disagrees with its control section")
    return ShaderControl(control, (major, minor), stage, code_bytes, compiler, spirv)


@dataclass(frozen=True)
class Shader:
    control: ShaderControl
    code: bytes
    storage_bytes: int

    @property
    def key(self) -> str:
        """Content identity over complete declared control and meaningful code.

        Allocation padding in a BNSH descriptor is excluded. Both offline and
        runtime consumers must apply these same declared-length rules.
        """
        return hashlib.sha256(self.control.data + self.code).hexdigest()[:16]


def read_shader(control: bytes, code: bytes, *, expected_stage: str | None = None) -> Shader:
    metadata = read_control(control)
    if expected_stage is not None and metadata.stage != expected_stage:
        raise DumpError("NVN2 shader control stage disagrees with its container slot")
    actual_code = region(code, 0, metadata.code_bytes, "NVN2 native shader code")
    if len(actual_code) % 16:
        raise DumpError("NVN2 native shader code is not 16-byte aligned")
    if metadata.stage != "cs":
        region(actual_code, 0, 128 + 16, "NVN2 graphics header and first instruction")
        if (len(actual_code) - 128) % 16:
            raise DumpError("NVN2 graphics instructions are not 16-byte aligned")
        sph_stage = struct.unpack_from("<I", actual_code)[0] >> 10 & 0xF
        if SPH_STAGES.get(sph_stage) != metadata.stage:
            raise DumpError("NVN2 native graphics header stage disagrees with its control section")
    return Shader(metadata, actual_code, len(code))


@dataclass(frozen=True)
class ShaderVariation:
    index: int
    shaders: tuple[Shader, ...]


def read_bnsh(data: bytes, offset: int = 0) -> tuple[ShaderVariation, ...]:
    """Read BNSH 2.4.0 classic stages; reject unknown program extensions.

    No vertex/fragment pairing is inferred: variations may contain an
    intermediate geometry stage. Native lowering currently accepts VS/PS only.
    Pointers in this serialized format are relative to the BNSH header.
    """
    header = region(data, offset, 0xC0, "NVN2 BNSH header")
    if header[:8] != b"BNSH\0\0\0\0" or header[12:14] != b"\xff\xfe":
        raise DumpError("NVN2 BNSH magic or little-endian marker does not match")
    if struct.unpack_from("<I", header, 8)[0] != 0x00020400:
        raise _unsupported("NVN2 BNSH version is not audited (expected 2.4.0)")
    size = struct.unpack_from("<I", header, 0x1C)[0]
    if size > MAX_BNSH_BYTES:
        raise DumpError("NVN2 BNSH file exceeds the reader size limit")
    blob = region(data, offset, size, "NVN2 BNSH file")
    if len(blob) < 0xC0 or blob[0x60:0x64] != b"grsc":
        raise DumpError("NVN2 BNSH is missing its graphics resource header")
    count, table = struct.unpack_from("<IQ", blob, 0x7C)
    if count > MAX_VARIATIONS:
        raise DumpError("NVN2 BNSH variation count exceeds the reader limit")
    if count and table < 0xC0:
        raise DumpError("NVN2 BNSH variations overlap its header")
    region(blob, table, count * 0x40, "NVN2 BNSH variation table")
    variations, stages, shader_bytes = [], {}, 0
    for index in range(count):
        program = struct.unpack_from("<Q", blob, table + 0x40 * index + 0x10)[0]
        shaders = []
        if program:
            if program < 0xC0:
                raise DumpError("NVN2 BNSH program overlaps its header")
            descriptor = region(blob, program, 0xA0, "NVN2 BNSH program")
            if any(descriptor[0x38:0x60]):
                raise _unsupported("NVN2 BNSH program has an unaudited stage extension")
            for slot, stage in enumerate(BNSH_STAGES):
                pointer = struct.unpack_from("<Q", descriptor, 8 + slot * 8)[0]
                if not pointer:
                    continue
                if pointer in stages:
                    shader = stages[pointer]
                    if shader.control.stage != stage:
                        raise DumpError("NVN2 BNSH reuses a stage descriptor in a different stage slot")
                    shaders.append(shader)
                    continue
                if pointer < 0xC0:
                    raise DumpError("NVN2 BNSH stage descriptor overlaps its header")
                native = region(blob, pointer, 0x20, "NVN2 BNSH stage descriptor")
                ctl, code, ncode, nctl = struct.unpack_from("<QQII", native, 8)
                if min(ctl, code) < 0xC0:
                    raise DumpError("NVN2 BNSH shader payload overlaps its header")
                if nctl > MAX_CONTROL_BYTES or ncode > MAX_CODE_BYTES:
                    raise DumpError("NVN2 BNSH shader exceeds the reader size limits")
                # A small serialized file can point many independent stage
                # descriptors at the same large payload. Bound the aggregate
                # copied bytes as well as each section; reused descriptors above
                # are charged only once.
                if nctl + ncode > MAX_BNSH_SHADER_BYTES - shader_bytes:
                    raise DumpError("NVN2 BNSH shader payloads exceed the reader memory limit")
                shader_bytes += nctl + ncode
                shader = read_shader(region(blob, ctl, nctl, "NVN2 BNSH control"),
                                     region(blob, code, ncode, "NVN2 BNSH code"),
                                     expected_stage=stage)
                stages[pointer] = shader
                shaders.append(shader)
        variations.append(ShaderVariation(index, tuple(shaders)))
    return tuple(variations)


def read_sead_primitive(data: bytes) -> tuple[Shader, Shader]:
    """Read the two-stage SEAD primitive container's explicit file offsets."""
    region(data, 0, 16, "NVN2 SEAD primitive offset table")
    ctls = struct.unpack_from("<2I", data)
    codes = struct.unpack_from("<2I", data, 8)
    shaders, ranges = [], []
    for stage, ctl, code in zip(("vs", "ps"), ctls, codes):
        if min(ctl, code) < 16:
            raise DumpError("NVN2 SEAD primitive data overlaps its offset table")
        metadata = read_control(region(data, ctl, len(data) - ctl, "NVN2 SEAD control"))
        native = region(data, code, metadata.code_bytes, "NVN2 SEAD code")
        for start, length in ((ctl, len(metadata.data)), (code, len(native))):
            for a, b in ranges:
                if max(start, a) < min(start + length, b):
                    raise DumpError("NVN2 SEAD primitive sections overlap")
            ranges.append((start, start + length))
        shaders.append(read_shader(metadata.data, native, expected_stage=stage))
    return tuple(shaders)
