#!/usr/bin/env python3
"""Host-test actual DarwinBoot sources and cross-compile/audit ARM64 handoff code."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "UniXNU/drivers/DarwinBoot/DarwinBootPkg/Driver"


def run(argv, commands, env=None):
    argv = [str(item) for item in argv]
    result = subprocess.run(argv, capture_output=True, text=True, env=env, timeout=120)
    commands.append({"argv": argv, "returncode": result.returncode,
                     "stdout": result.stdout, "stderr": result.stderr})
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {argv!r}\n{result.stdout}{result.stderr}")
    return result.stdout


def coff_audit(path):
    """Use real COFF section/symbol tables, not a source-string relocation check."""
    data = path.read_bytes()
    machine, count, _, symoff, symcount, optional, _ = struct.unpack_from("<HHIIIHH", data)
    assert machine == 0xAA64
    strings = data[symoff + 18 * symcount:]

    def name(raw):
        if raw[:4] == b"\0" * 4:
            offset = struct.unpack_from("<I", raw, 4)[0]
            return strings[offset:].split(b"\0", 1)[0].decode()
        if raw.startswith(b"/"):
            offset = int(raw[1:].rstrip(b"\0"))
            return strings[offset:].split(b"\0", 1)[0].decode()
        return raw.rstrip(b"\0").decode()

    sections = []
    for index in range(count):
        offset = 20 + optional + 40 * index
        raw, _, _, size, ptr, _, _, relocs, _, _ = struct.unpack_from("<8sIIIIIIHHI", data, offset)
        sections.append({"name": name(raw), "size": size, "bytes": data[ptr:ptr + size], "relocations": relocs})
    symbols, undefined = {}, []
    index = 0
    while index < symcount:
        raw, value, section, _, storage, aux = struct.unpack_from("<8sIhHBB", data, symoff + index * 18)
        symbol = name(raw)
        if storage == 2 and section == 0:
            undefined.append(symbol)
        symbols[symbol] = (section, value)
        index += 1 + aux
    assert not undefined, undefined
    section, start = symbols["DarwinColdEntry"]
    end_section, end = symbols["DarwinColdEntryEnd"]
    cold = sections[section - 1]
    assert section == end_section and cold["name"] == ".text$DarwinColdEntry"
    assert start == 0 and end == cold["size"] and cold["relocations"] == 0
    assert end > 0 and end % 4 == 0
    return {"cold_bytes": end, "cold_relocations": cold["relocations"], "assembly_undefined_symbols": undefined}


def instruction_audit(disassembly, cold=False):
    instructions = []
    lse = ("cas", "ldadd", "ldclr", "ldeor", "ldset", "ldsmax", "ldsmin", "ldumax", "ldumin",
           "swp", "stadd", "stclr", "steor", "stset", "stsmax", "stsmin", "stumax", "stumin", "ldapr")
    for line in disassembly.splitlines():
        match = re.match(r"\s*[0-9a-f]+:\s+([a-z0-9.]+)\s*(.*)", line, re.I)
        if match:
            opcode, operands = match[1].lower(), match[2].split("//", 1)[0]
            assert not opcode.startswith(lse), line
            if cold:
                assert opcode not in ("bl", "blr", "adr", "adrp", "ret"), line
                assert not re.search(r"\b(?:sp|wsp|x29|x30)\b", operands), line
                if opcode in ("ldr", "str"):
                    assert re.search(r"\[x19(?:,|\])", operands), line
            instructions.append((opcode, operands))
    assert instructions
    if cold:
        assert any(op == "smc" for op, _ in instructions) and any(op == "hvc" for op, _ in instructions)
        assert instructions[-1][0] == "br" and instructions[-1][1].strip() == "x1"
    return len(instructions)


def handoff_order_audit(disassembly):
    """Check the actual decoded origin fork and publication ordering."""
    code = []
    for line in disassembly.splitlines():
        match = re.match(r"\s*([0-9a-f]+):\s+([a-z0-9.]+)\s*(.*)", line, re.I)
        if match:
            code.append((int(match[1], 16), match[2].lower(), match[3].split("//", 1)[0].strip().lower()))

    def locate(opcode, prefix):
        return next(i for i, (_, op, operands) in enumerate(code) if op == opcode and operands.startswith(prefix))

    mask = locate("msr", "daifset")
    saved_context = locate("mov", "x12, x6")
    clean = locate("dc", "civac")
    shutdown = locate("msr", "sctlr_el1")
    fork = locate("cbz", "x12,")
    start = locate("str", "x4, [x12, #0x1000]")
    event = locate("sev", "")
    park = locate("wfe", "")
    direct = locate("br", "x1")
    assert mask < saved_context < clean < shutdown < fork < start < event < park < direct
    # BIC-immediate is an assembler alias for AND with a complemented mask.
    for (_, op, args), bit in zip(code[shutdown - 3:shutdown], (1, 4, 0x1000)):
        assert args.startswith("x4, x4, #")
        immediate = int(args.rsplit("#", 1)[1], 0)
        assert (op == "bic" and immediate == bit) or (op == "and" and immediate == (0xffffffffffffffff ^ bit))
    assert code[start + 1][1:] == ("dsb", "sy")
    assert int(re.search(r"0x([0-9a-f]+)", code[fork][2])[1], 16) == code[direct][0]
    assert code[park + 1][1] == "b"
    assert int(re.search(r"0x([0-9a-f]+)", code[park + 1][2])[1], 16) == code[park][0]


def verify(output, mde, sanitize):
    commands = []
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TMPDIR=str(output), PYTHONDONTWRITEBYTECODE="1")
    includes = [f"-I{mde}", f"-I{mde / 'AArch64'}",
                f"-I{ROOT / 'UniXNU/package/UniXNUPkg/Include'}"]
    try:
        host = output / "darwin-boot-test"
        host_flags = ["-O1", "-g", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                      "-fno-builtin", "-fshort-wchar", "-DMDEPKG_NDEBUG"]
        if sanitize:
            host_flags += ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
        run(["clang", *host_flags, *includes, ROOT / "tests/darwin-boot.c",
             DRIVER / "CpuHandoff.c", "-o", host], commands, env)
        host_result = run([host], commands, env)
        print(host_result, end="")
        target = ["--target=aarch64-unknown-windows", "-mcpu=cortex-a53", "-march=armv8-a+crypto",
                  "-mno-outline-atomics", "-mgeneral-regs-only", "-ffreestanding", "-fshort-wchar",
                  "-fno-builtin", "-fno-stack-protector", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
                  "-O2", "-Wall", "-Wextra", "-Werror"]
        for source in ("DarwinBoot.c", "CpuHandoff.c"):
            obj = output / (Path(source).stem + ".obj")
            run(["clang", *target, *includes, "-c", DRIVER / source, "-o", obj], commands, env)
            text = run(["llvm-objdump", "-d", "--no-show-raw-insn", "--mattr=+lse,+rcpc", obj], commands, env)
            instruction_audit(text)
        entry = output / "Entry.obj"
        # EDK II normally supplies these Base/ProcessorBind macros through
        # AutoGen. Use the actual header macros for an isolated assembler check.
        run(["clang", *target, "-D__GNUC__", "-imacros", mde / "Base.h", *includes,
             "-c", DRIVER / "Entry.S", "-o", entry], commands, env)
        audit = coff_audit(entry)
        relocations = run(["llvm-readobj", "--relocations", entry], commands, env)
        assert "IMAGE_REL_" not in relocations
        cold = run(["llvm-objdump", "-d", "--no-show-raw-insn", "--section=.text$DarwinColdEntry", entry], commands, env)
        audit["cold_instructions"] = instruction_audit(cold, cold=True)
        (output / "cold-entry-disassembly.txt").write_text(cold)
        enter = run(["llvm-objdump", "-d", "--no-show-raw-insn", "--section=.text$DarwinEnter", entry], commands, env)
        instruction_audit(enter)
        handoff_order_audit(enter)
        audit["origin_clean_shutdown_start_park_order"] = True
        # Link the real assembly as a native PE test executable, not a staged
        # EFI driver. It checks PE branch resolution without mock UEFI libraries.
        pe = output / "entry-link.exe"
        run(["lld-link", "/subsystem:native", "/entry:DarwinColdEntry", "/machine:arm64",
             "/nodefaultlib", "/timestamp:0", "/base:0", f"/out:{pe}", entry], commands, env)
        data = pe.read_bytes()
        header = struct.unpack_from("<I", data, 0x3c)[0]
        optional = header + 24
        assert data[:2] == b"MZ" and data[header:header + 4] == b"PE\0\0"
        assert struct.unpack_from("<H", data, header + 4)[0] == 0xAA64
        assert struct.unpack_from("<H", data, optional)[0] == 0x20B
        assert not any(struct.unpack_from("<II", data, optional + 112 + 8)) # imports
        elf = output / "Entry.o"
        run(["clang", "--target=aarch64-none-elf", "-march=armv8-a", "-imacros", mde / "Base.h",
             *includes, "-c", DRIVER / "Entry.S", "-o", elf], commands, env)
        elf_relocs = run(["llvm-readobj", "--relocations", elf], commands, env)
        assert "R_AARCH64_" not in elf_relocs
        print(f"PASS: actual C ARM64/COFF objects; {audit['cold_bytes']}-byte cold entry, zero relocations/undefined symbols, stack/global/call-free")
        print("PASS: assembly PE link and ELF cross-compile; zero LSE/RCpc instruction hits")
        for script in ("test-kernel-parser.py", "test-packed-ramdisk.py"):
            print(run(["python3", DRIVER.parents[1] / "Test" / script], commands, env), end="")
        window = output / "window-size-test"
        run(["clang", "-Wall", "-Wextra", "-Werror", DRIVER.parents[1] / "Test/window-size.c", "-o", window], commands, env)
        run([window], commands, env)
        sources = [DRIVER / name for name in ("BootArgs.h", "BootPolicy.h", "CpuHandoff.h", "CpuHandoff.c", "DarwinBoot.c", "Entry.S", "DarwinBoot.inf")]
        report = {"host_result": host_result, "ubsan": sanitize, "compiler": run(["clang", "--version"], commands, env).splitlines()[0],
                  "mde_include": str(mde), "arm64_audit": audit, "pe_link_scope": "production Entry.S only, native PE test link",
                  "full_firmware_built": False, "hardware_boot_tested": False,
                  "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    finally:
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mde", type=Path, default=ROOT / "uefi-lavender/Mu_Basecore/MdePkg/Include")
    parser.add_argument("--output", type=Path, help="Keep objects, disassembly and command/report JSON here")
    parser.add_argument("--sanitize", action="store_true", help="Run the host C test with UBSan")
    args = parser.parse_args()
    if args.output:
        verify(args.output.resolve(), args.mde.resolve(), args.sanitize)
        print(f"Evidence: {args.output.resolve()}")
    else:
        approved = Path("/data/data/com.termux/files/usr/tmp/opencode")
        parent = approved if approved.is_dir() else Path(tempfile.gettempdir())
        with tempfile.TemporaryDirectory(prefix="darwin-checks-", dir=parent) as tmp:
            verify(Path(tmp), args.mde.resolve(), args.sanitize)


if __name__ == "__main__":
    main()
