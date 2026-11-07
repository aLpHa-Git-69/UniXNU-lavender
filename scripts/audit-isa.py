#!/usr/bin/env python3
"""Scan executable images in the actual FD for SDM660-incompatible LSE/RCpc."""
import argparse
import hashlib
import importlib.util
import json
import lzma
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("stage1_build", ROOT / "scripts/build-stage1.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--fd", type=Path, default=ROOT / "out/debug/SILICIUM_UEFI.fd")
args = parser.parse_args()
outer = build.volume_files(args.fd.read_bytes())
container = outer["9E21FD93-9C72-4C15-8C4B-E77F1DB2D792"]
inner = None
for kind, section, header_size in build.sections(container):
    if kind != 2:
        continue
    guid = uuid.UUID(bytes_le=section[header_size:header_size + 16])
    if str(guid).upper() != "EE4E5898-3914-4259-9D6E-DC7BD79403CF":
        continue
    offset = struct.unpack_from("<H", section, header_size + 16)[0]
    for decoded_kind, data, decoded_header in build.sections(lzma.decompress(section[offset:], format=lzma.FORMAT_ALONE)):
        if decoded_kind == 0x17:
            inner = build.volume_files(data[decoded_header:])
            break
if inner is None:
    raise SystemExit("FVMAIN extraction failed")

prefixes = ("cas", "ldadd", "ldclr", "ldeor", "ldset", "ldsmax", "ldsmin", "ldumax", "ldumin",
            "swp", "stadd", "stclr", "steor", "stset", "stsmax", "stsmin", "stumax", "stumin", "ldapr")
line_pattern = re.compile(r"^\s*[0-9a-f]+:\s+([a-z0-9.]+)\b", re.I)
images = []
hits = {}
te_count = 0
literal_hits = {}
table_hits = {}


def executable_code(data):
    """Read code section bounds without consuming vendor debug directories.

    Qualcomm's stock EnvDxe has an uneven debug-directory size, which LLVM
    rejects even though the UEFI loader does not use that directory. Reading
    the executable section table avoids modifying the supplied firmware.
    """
    if data[:2] == b"VZ":
        if len(data) < 40 or struct.unpack_from("<H", data, 2)[0] != 0xAA64:
            raise ValueError("invalid AARCH64 TE header")
        count = data[4]
        table = 40
        adjustment = struct.unpack_from("<H", data, 6)[0] - 40
        is_te = True
    else:
        if len(data) < 64:
            raise ValueError("truncated PE header")
        pe = struct.unpack_from("<I", data, 0x3C)[0] if data[:2] == b"MZ" else 0
        if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("invalid PE signature")
        machine, count = struct.unpack_from("<HH", data, pe + 4)
        if machine != 0xAA64:
            raise ValueError("executable image is not AARCH64")
        table = pe + 24 + struct.unpack_from("<H", data, pe + 20)[0]
        adjustment = 0
        is_te = False
    if count == 0 or table + count * 40 > len(data):
        raise ValueError("invalid executable section table")
    code = bytearray()
    for index in range(count):
        offset = table + index * 40
        flags = struct.unpack_from("<I", data, offset + 36)[0]
        if not flags & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
            continue
        size, pointer = struct.unpack_from("<II", data, offset + 16)
        pointer -= adjustment
        if pointer < 0 or pointer + size > len(data) or size % 4:
            raise ValueError("invalid executable code section bounds")
        code.extend(data[pointer:pointer + size])
    if not code:
        raise ValueError("image has no executable section")
    return code, is_te


# Old Qualcomm ELF-to-PE conversion combines read-only literals with .text.
# Only classify a match as a literal when the executable bytes exactly match
# a pinned stock binary and the entire word lies inside a terminated string.
stock_code = {}
for folder in (ROOT / "uefi-lavender/Binaries/lavender", ROOT / "uefi-lavender/Binaries/generic"):
    for inf in folder.rglob("*.inf"):
        text = inf.read_text()
        guid = re.search(r"(?mi)^\s*FILE_GUID\s*=\s*([0-9a-f-]{36})", text)
        binary = re.search(r"(?mi)^\s*PE32\|([^|]+)\|", text)
        if not guid or not binary:
            continue
        path = inf.parent / binary[1].strip()
        if not path.is_file():
            continue
        code, _ = executable_code(path.read_bytes())
        stock_code.setdefault(guid[1].upper(), set()).add(hashlib.sha256(code).hexdigest())

# Verified data in the pinned stock blobs, not callable instructions. The
# HashDxe pattern is the complete standard SHA-256 K table; DisplayDxe's
# byte lookup table was inspected at code-section offset 0x30e40.
sha256_k = (
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,
)
data_tables = {
    "3ADF8DDA-1850-44C5-8C63-BB991849BC6F": struct.pack("<64I", *sha256_k),
    "4138022F-06C7-4F79-9C94-7E33B511A4E7": bytes.fromhex("070b0d0e131516191a1c232526292a2c313234381f2f373b"),
}

image_data = []
for guid, body in list(outer.items()) + list(inner.items()):
    for kind, section, header_size in build.sections(body):
        if kind in (0x10, 0x12):
            image_data.append((guid, section[header_size:]))
embedded_count = len(image_data)
probe = args.fd.parent / "LavenderProbe.efi"
if probe.is_file():
    image_data.append(("53C03A68-059A-45CE-9CB0-D953084A8094", probe.read_bytes()))

with tempfile.TemporaryDirectory(prefix="lavender-isa-", dir=args.fd.parent) as scratch:
    for guid, data in image_data:
        code, is_te = executable_code(data)
        is_stock = hashlib.sha256(code).hexdigest() in stock_code.get(guid, set())
        literals = []
        if is_stock:
            for pattern in (rb"[\x09\x0a\x0d\x20-\x7e]{8,}\x00", rb"(?:[\x20-\x7e]\x00){4,}\x00\x00"):
                literals.extend((match.start(), match.end() - 1) for match in re.finditer(pattern, code))
        table_range = None
        table = data_tables.get(guid) if is_stock else None
        if table is not None:
            position = code.find(table)
            if position >= 0:
                table_range = (position, position + len(table))
        # .inst emits the original instruction words, with executable mapping
        # symbols, so llvm-objdump can also analyze TE and vendor PE files.
        source = Path(scratch) / f"{guid}.S"
        source.write_text(".text\n" + "".join(f".inst 0x{word[0]:08x}\n" for word in struct.iter_unpack("<I", code)))
        path = Path(scratch) / f"{guid}.o"
        subprocess.run(["clang", "--target=aarch64-none-elf", "-c", str(source), "-o", str(path)], check=True)
        te_count += is_te
        result = subprocess.run(["llvm-objdump", "--disassemble", "--no-show-raw-insn",
                                 "--mattr=+lse,+rcpc", str(path)], check=True, capture_output=True, text=True)
        bad = []
        for line in result.stdout.splitlines():
            match = line_pattern.match(line)
            if match and match[1].lower().startswith(prefixes):
                address = int(line.split(":", 1)[0].strip(), 16)
                if any(address < end and address + 4 > start and
                       (address >= start or not any(code[address:start])) and
                       (address + 4 <= end or not any(code[end:address + 4]))
                       for start, end in literals):
                    literal_hits.setdefault(guid, []).append(line.strip())
                elif table_range and table_range[0] <= address and address + 4 <= table_range[1]:
                    table_hits.setdefault(guid, []).append(line.strip())
                else:
                    bad.append(line.strip())
        images.append(guid)
        if bad:
            hits[guid] = bad

report = {"fd_sha256": build.sha256(args.fd), "executable_images_scanned": len(images),
          "embedded_images_scanned": embedded_count,
          "te_images_scanned": te_count, "image_guids": images,
          "stock_string_literal_matches": literal_hits,
          "stock_data_table_matches": table_hits,
          "lse_rcpc_hits": hits, "hardware_tested": False}
output = args.fd.parent / "isa-audit.json"
output.write_text(json.dumps(report, indent=2) + "\n")
print(f"ISA audit: {embedded_count} embedded images + {len(images) - embedded_count} payload image, {sum(map(len, hits.values()))} LSE/RCpc hits")
print(f"Pinned stock string literals classified: {sum(map(len, literal_hits.values()))}")
print(f"Pinned stock data table words classified: {sum(map(len, table_hits.values()))}")
print(output)
if hits:
    print(json.dumps(hits, indent=2))
    raise SystemExit(1)
