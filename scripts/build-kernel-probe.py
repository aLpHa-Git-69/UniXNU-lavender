#!/usr/bin/env python3
"""Build a standalone Lavender Gold/RAM probe and reuse the saved Stage-1 firmware."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import tomllib
import zlib

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "package/LavenderBringupPkg/Application/LavenderKernelProbe"
MDE = ROOT / "uefi-lavender/Mu_Basecore/MdePkg/Include"
FONT = ROOT / "UniXNU/drivers/DarwinBoot/DarwinBootPkg/Driver"


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, default=ROOT / "out/copied-handoff")
    parser.add_argument("--kernel", type=Path, default=ROOT / "out/kernel-inputs/kernel.development.lavender")
    parser.add_argument("--output", type=Path, default=ROOT / "out/kernel-readiness")
    args = parser.parse_args()
    baseline, kernel, output = args.baseline.resolve(), args.kernel.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    commands = []

    def command(argv):
        argv = [str(value) for value in argv]
        result = subprocess.run(argv, capture_output=True, text=True)
        commands.append({"argv": argv, "returncode": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(f"command failed: {argv!r}\n{result.stdout}{result.stderr}")
        return result.stdout

    baseline_report = json.loads((baseline / "build-report.json").read_text())
    for name, item in baseline_report["artifacts"].items():
        path = baseline / name
        if path.stat().st_size != item["bytes"] or sha256(path) != item["sha256"]:
            raise ValueError(f"baseline artifact changed: {name}")
    profile = json.loads((kernel.parent / "kernel-profile.json").read_text())
    if sha256(kernel) != profile["kernel_sha256"]:
        raise ValueError("selected kernel does not match the reviewed Lavender kernel")
    sys.path.insert(0, str(ROOT / "ios-lavender/tools"))
    import mkpayload
    macho = mkpayload.parse_macho(kernel.read_bytes())
    alignment = min(s["vmaddr"] for s in macho["segs"] if s["vmsize"]) & 0x1fffff
    if alignment != int(profile["darwinboot_alignment_offset"], 0):
        raise ValueError("kernel alignment differs from the reviewed profile")

    includes = [f"-I{MDE}", f"-I{MDE / 'AArch64'}"]
    test = output / "memory-map-test"
    command(["clang", "-Wall", "-Wextra", "-Werror", "-fshort-wchar", *includes,
             ROOT / "tests/kernel-memory-map.c", "-o", test])
    test_result = command([test]).strip()

    probe = output / "LavenderKernelProbe.efi"
    obj = output / "LavenderKernelProbe.obj"
    gold_obj = output / "GoldEntry.obj"
    target = ["--target=aarch64-unknown-windows", "-mcpu=cortex-a53", "-march=armv8-a+crypto",
              "-mno-outline-atomics", "-ffreestanding", "-fshort-wchar", "-fno-builtin",
              "-fno-stack-protector", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables"]
    command(["clang", *target, "-O2", "-Wall", "-Wextra", "-Werror", *includes,
             f"-I{FONT}", f"-DKERNEL_ALIGNMENT_OFFSET={alignment:#x}",
             "-c", SOURCE / "LavenderKernelProbe.c", "-o", obj])
    command(["clang", *target, "-c", SOURCE / "GoldEntry.S", "-o", gold_obj])
    relocations = command(["llvm-readobj", "--relocations", gold_obj])
    if "IMAGE_REL_" in relocations:
        raise ValueError("copied Gold callback contains relocations")
    command(["lld-link", "/subsystem:efi_application", "/entry:LavenderKernelProbeEntry",
             "/machine:arm64", "/nodefaultlib", "/timestamp:0", "/base:0",
             f"/out:{probe}", obj, gold_obj])
    data = probe.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    optional = pe + 24
    if (data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0" or
            struct.unpack_from("<H", data, pe + 4)[0] != 0xaa64 or
            (struct.unpack_from("<H", data, pe + 22)[0] & 1) or
            struct.unpack_from("<H", data, optional)[0] != 0x20b or
            struct.unpack_from("<H", data, optional + 68)[0] != 10 or
            any(struct.unpack_from("<II", data, optional + 112 + 8))):
        raise ValueError("probe is not a dependency-free AARCH64 EFI application")
    disassembly = command(["llvm-objdump", "--disassemble", "--no-show-raw-insn", "--mattr=+lse,+rcpc", probe])
    prefixes = ("cas", "ldadd", "ldclr", "ldeor", "ldset", "ldsmax", "ldsmin", "ldumax", "ldumin",
                "swp", "stadd", "stclr", "steor", "stset", "stsmax", "stsmin", "stumax", "stumin", "ldapr")
    hits = [line.strip() for line in disassembly.splitlines()
            if (match := re.match(r"\s*[0-9a-f]+:\s+([a-z0-9.]+)\b", line, re.I)) and
            match[1].lower().startswith(prefixes)]
    if hits:
        raise ValueError(f"unsupported SDM660 LSE/RCpc instructions: {hits}")
    (output / "probe-disassembly.txt").write_text(disassembly)
    (output / "LICENSES.txt").write_text("Framebuffer font from DarwinBoot's DiagFont.h:\n\n" +
                                         (FONT / "DiagFont.h").read_text().split("#ifndef", 1)[0])

    payload = output / "stage2-payload.img"
    payload.write_bytes((baseline / "stage1-payload.img").read_bytes())
    command(["mcopy", "-o", "-i", payload, ROOT / "stage2.toml", "::/EFI/UniXNU/unixnu.toml"])
    command(["mcopy", "-o", "-i", payload, probe, "::/EFI/UniXNU/Payloads/LavenderKernelProbe.efi"])
    config = command(["mtype", "-i", payload, "::/EFI/UniXNU/unixnu.toml"])
    if config != (ROOT / "stage2.toml").read_text():
        raise ValueError("Stage-2 configuration did not round-trip through FAT")
    with tempfile.TemporaryDirectory(dir=output) as scratch:
        extracted = Path(scratch) / "probe.efi"
        command(["mcopy", "-i", payload, "::/EFI/UniXNU/Payloads/LavenderKernelProbe.efi", extracted])
        if sha256(extracted) != sha256(probe):
            raise ValueError("kernel-readiness probe did not round-trip through FAT")

    baseline_image = baseline / "payload/lavender-unixnu-stage1.img"
    original = baseline_image.read_bytes()
    page = struct.unpack_from("<I", original, 36)[0]
    kernel_size = struct.unpack_from("<I", original, 8)[0]
    firmware_kernel = original[page:page + kernel_size]
    unpacker = zlib.decompressobj(16 + zlib.MAX_WBITS)
    combined = unpacker.decompress(firmware_kernel)
    dtb = unpacker.unused_data
    if not unpacker.eof or not dtb or combined[-(baseline / "SILICIUM_UEFI.fd").stat().st_size:] != (baseline / "SILICIUM_UEFI.fd").read_bytes():
        raise ValueError("baseline firmware kernel/DTB did not unpack correctly")
    firmware_kernel_path = output / "firmware-kernel.bin"
    firmware_kernel_path.write_bytes(firmware_kernel)
    image = output / "lavender-unixnu-kernel-readiness.img"
    geometry = tomllib.loads((ROOT / "firmware.toml").read_text())["bootimg"]["image"]
    argv = ["python3", ROOT / "uefi-lavender/Resources/Scripts/mkbootimg.py",
            "--kernel", firmware_kernel_path, "--ramdisk", payload, "-o", image]
    for key in ("header_version", "base", "kernel_offset", "ramdisk_offset", "tags_offset", "pagesize"):
        argv += [f"--{key}", str(geometry[key])]
    command(argv)
    sys.path.insert(0, str(ROOT / "UniXNU/scripts"))
    from unixnu import bootimg
    boot_report = bootimg.verify(image, combined, dtb, payload, geometry)
    old_boot_report = json.loads((baseline / "payload/boot.json").read_text())
    if boot_report["kernel_sha256"] != old_boot_report["kernel_sha256"]:
        raise ValueError("the saved firmware kernel changed during Stage-2 packaging")
    boot_report.update(image_sha256=sha256(image), image_bytes=image.stat().st_size,
                       fd_sha256=sha256(baseline / "SILICIUM_UEFI.fd"), hardware_tested=False)
    (output / "boot.json").write_text(json.dumps(boot_report, indent=2) + "\n")
    files = [probe, payload, image, firmware_kernel_path]
    report = {"milestone": "Gold 16 KiB and 1 GiB conventional-memory readiness diagnostic",
              "hardware_tested": False, "kernel_included": False,
              "baseline_image_sha256": sha256(baseline_image),
              "baseline_firmware_kernel_unchanged": True, "baseline_fd_sha256": sha256(baseline / "SILICIUM_UEFI.fd"),
              "kernel_reference": profile, "alignment_offset": hex(alignment),
              "source_files_sha256": {str(p.relative_to(ROOT)): sha256(p)
                                      for p in [SOURCE / "LavenderKernelProbe.c", SOURCE / "GoldEntry.S",
                                                SOURCE / "MemoryRuns.h", SOURCE / "LavenderKernelProbe.inf",
                                                ROOT / "stage2.toml", ROOT / "scripts/build-kernel-probe.py",
                                                FONT / "DiagFont.h"]},
              "host_test": test_result, "gold_callback_relocations": 0,
              "probe_lse_rcpc_hits": hits, "compiler": command(["clang", "--version"]).splitlines()[0],
              "artifacts": {p.name: {"bytes": p.stat().st_size, "sha256": sha256(p)} for p in files}}
    (output / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    (output / "SHA256SUMS").write_text("".join(f"{sha256(p)}  {p.name}\n" for p in files))
    print(test_result)
    print("AARCH64 EFI, relocation-free Gold callback and zero LSE/RCpc instructions: PASS")
    print("FAT payload and Android boot-image round trips: PASS; saved firmware kernel unchanged")
    print(f"Device test image: {image} ({image.stat().st_size} bytes)")
    print("Hardware result pending; this image does not contain an XNU kernel.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
