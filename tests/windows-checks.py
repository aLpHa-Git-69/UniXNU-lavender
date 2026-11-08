#!/usr/bin/env python3
"""Run real handoff, initrd-driver and platform-memory checks for WinPE firmware."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MDE = ROOT / "uefi-lavender/Mu_Basecore/MdePkg/Include"
SILICIUM = ROOT / "uefi-lavender/Silicon/Silicium/SiliciumPkg/Include"
MAP = ROOT / "uefi-lavender/Platforms/Xiaomi/lavenderPkg/Library/MemoryMapLib/MemoryMapLib.c"
DRIVER = ROOT / "UniXNU/package/UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.c"
DEFINES = ["-DWINDOWS_BUILD", "-DUNIXNU_INITRD_COPY_BASE=0xC0000000ULL",
           "-DUNIXNU_INITRD_COPY_END=0xD0000000ULL"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "out/windows-checks")
    args = parser.parse_args()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    commands, results = [], []

    def run(argv):
        argv = [str(item) for item in argv]
        result = subprocess.run(argv, capture_output=True, text=True)
        commands.append({"argv": argv, "returncode": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(f"command failed: {argv!r}\n{result.stdout}{result.stderr}")
        return result.stdout

    host = ["clang", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
            "-fshort-wchar", "-fsanitize=undefined", f"-I{MDE}", f"-I{MDE / 'AArch64'}"]
    handoff = output / "handoff"
    run([*host, ROOT / "tests/boot-handoff-range.c", "-o", handoff])
    results.append(run([handoff]).strip())
    for name, defines in (("legacy", []), ("winpe", DEFINES)):
        driver_test = output / f"driver-{name}"
        run([*host, *defines, ROOT / "tests/windows-ramdisk.c", "-o", driver_test])
        results.append(run([driver_test]).strip())
        map_test = output / f"map-{name}"
        run([*host, f"-I{SILICIUM}", *defines, ROOT / "tests/windows-memory-map.c",
             MAP, "-o", map_test])
        results.append(run([map_test]).strip())

    target = ["clang", "--target=aarch64-unknown-windows", "-O2", "-Wall", "-Wextra",
              "-Werror", "-Wno-unused-parameter", "-mcpu=cortex-a53", "-march=armv8-a+crypto",
              "-mno-outline-atomics", "-ffreestanding", "-fshort-wchar", "-DMDEPKG_NDEBUG",
              f"-I{MDE}", f"-I{MDE / 'AArch64'}", f"-I{SILICIUM}", *DEFINES]
    prefixes = ("cas", "ldadd", "ldclr", "ldeor", "ldset", "ldsmax", "ldsmin", "ldumax",
                "ldumin", "swp", "stadd", "stclr", "steor", "stset", "stsmax", "stsmin",
                "stumax", "stumin", "ldapr")
    for name, source in (("memory-map", MAP), ("ramdisk", DRIVER)):
        obj = output / f"{name}.obj"
        run([*target, "-c", source, "-o", obj])
        disassembly = run(["llvm-objdump", "--disassemble", "--no-show-raw-insn", "--mattr=+lse,+rcpc", obj])
        (output / f"{name}-disassembly.txt").write_text(disassembly)
        hits = [line for line in disassembly.splitlines()
                if (match := re.match(r"\s*[0-9a-f]+:\s+([a-z0-9.]+)", line, re.I))
                and match[1].lower().startswith(prefixes)]
        if hits:
            raise ValueError(f"unsupported SDM660 instructions in {name}: {hits}")
    results.append("actual memory-map/initrd sources compile for Cortex-A53 ARM64 with zero LSE/RCpc hits")
    sources = [MAP, DRIVER, ROOT / "tests/boot-handoff-range.c", ROOT / "tests/windows-ramdisk.c",
               ROOT / "tests/windows-memory-map.c", Path(__file__).resolve()]
    report = {"results": results, "hardware_tested": False,
              "source_files_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                      for path in sources}}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print("\n".join(results))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
