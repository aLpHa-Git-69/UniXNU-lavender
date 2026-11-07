#!/usr/bin/env python3
"""Build and verify lavender's firmware-only UniXNU bring-up images.

Uses UniXNU's existing firmware builder and boot-image packer. No kernel,
IPSW, GitHub authentication or attached phone is needed for this milestone.
"""

import argparse
import hashlib
import json
import lzma
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import tomllib
from types import SimpleNamespace
import uuid


ROOT = Path(__file__).resolve().parents[1]
PINS = {
    "uefi-lavender": "3b2e7d8ce4dc02bd1cdf66db6421d919410c3a50",
    "UniXNU": "afca227881b00dd76b7b4b6ce4d16b8a60014483",
    "UniXNU/drivers/DarwinBoot": "66b68e00ef1a4bb15aaa35a8177b54b72850d921",
    "ios-lavender": "a14adba76ff52eaf38e24bc883db28266d78673f",
    "uefi-lavender/Mu_Basecore": "64f478939955e45faa632c160ae2a75a5ff7ef7f",
    "uefi-lavender/Common/Mu": "566cf5c8c2c8adf8e1a337ced1d147bc6246856f",
    "uefi-lavender/Common/Mu_OEM_Sample": "9292f9254adb94c5e5bb09a370b9662fcd66a953",
    "uefi-lavender/Binaries": "9f4dbfd7d61acdae69fd5bb48720ce7e53711e25",
    "uefi-lavender/Silicium-ACPI": "f50bc115dccdea12e44efb550fd783d1daafc739",
}

REQUIRED_INFS = (
    "UniXNUPkg/Application/UniXNU/UniXNU.inf",
    "UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf",
    "UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.inf",
    "DarwinBootPkg/Driver/DarwinBoot.inf",
    "MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf",
    "FatPkg/EnhancedFatDxe/Fat.inf",
    "MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf",
    "LavenderBringupPkg/Driver/LavenderHandoffDxe/LavenderHandoffDxe.inf",
)


def command(args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_revisions():
    revisions = {}
    for relative, expected in PINS.items():
        revision = command(["git", "-C", ROOT / relative, "rev-parse", "HEAD"],
                           capture_output=True, text=True).stdout.strip()
        if revision != expected:
            raise ValueError(f"{relative}: expected {expected}, found {revision}")
        revisions[relative] = revision
    return revisions


def volume_files(data):
    """Parse FFS headers, so a GUID in application data cannot fake inclusion."""
    if len(data) < 64 or data[40:44] != b"_FVH":
        raise ValueError("firmware volume header is missing")
    length = struct.unpack_from("<Q", data, 32)[0]
    header_length = struct.unpack_from("<H", data, 48)[0]
    extension = struct.unpack_from("<H", data, 52)[0]
    if not 64 <= header_length <= length <= len(data):
        raise ValueError("invalid firmware volume bounds")
    if extension:
        if extension + 20 > length:
            raise ValueError("truncated extended volume header")
        extension_size = struct.unpack_from("<I", data, extension + 16)[0]
        header_length = max(header_length, extension + extension_size)
    offset = (header_length + 7) & ~7
    files = {}
    while offset + 24 <= length:
        header = data[offset:offset + 24]
        if header == b"\xff" * 24 or header == b"\0" * 24:
            break
        size = int.from_bytes(header[20:23], "little")
        header_size = 24
        if header[19] & 1:
            if offset + 32 > length:
                raise ValueError("truncated extended FFS header")
            size = struct.unpack_from("<Q", data, offset + 24)[0]
            header_size = 32
        if not header_size <= size <= length - offset:
            raise ValueError(f"invalid FFS file size at {offset:#x}")
        guid = str(uuid.UUID(bytes_le=header[:16])).upper()
        # RAW and PAD files have no PI section stream to decode.
        if header[18] not in (0x01, 0xF0):
            files[guid] = data[offset + header_size:offset + size]
        offset = (offset + size + 7) & ~7
    return files


def sections(data):
    offset = 0
    while offset + 4 <= len(data):
        size = int.from_bytes(data[offset:offset + 3], "little")
        header_size = 4
        if size == 0xFFFFFF:
            if offset + 8 > len(data):
                raise ValueError("truncated extended section header")
            size = struct.unpack_from("<I", data, offset + 4)[0]
            header_size = 8
        if not header_size <= size <= len(data) - offset:
            raise ValueError("invalid FFS section size")
        yield data[offset + 3], data[offset:offset + size], header_size
        offset = (offset + size + 3) & ~3


def check_firmware(fd, package_paths):
    outer = volume_files(fd.read_bytes())
    container = outer.get("9E21FD93-9C72-4C15-8C4B-E77F1DB2D792")
    if container is None:
        raise ValueError("compressed FVMAIN container is absent from the FD")
    inner = None
    for kind, section, header_size in sections(container):
        if kind != 2:  # EFI_SECTION_GUID_DEFINED
            continue
        guided = uuid.UUID(bytes_le=section[header_size:header_size + 16])
        if str(guided).upper() != "EE4E5898-3914-4259-9D6E-DC7BD79403CF":
            continue
        data_offset = struct.unpack_from("<H", section, header_size + 16)[0]
        decoded = lzma.decompress(section[data_offset:], format=lzma.FORMAT_ALONE)
        for decoded_kind, decoded_section, decoded_header in sections(decoded):
            if decoded_kind == 0x17:  # EFI_SECTION_FIRMWARE_VOLUME_IMAGE
                inner = volume_files(decoded_section[decoded_header:])
                break
    if inner is None:
        raise ValueError("cannot decode the FD's embedded FVMAIN")
    included = {}
    for name in REQUIRED_INFS:
        inf = next((path / name for path in package_paths if (path / name).is_file()), None)
        if inf is None:
            raise ValueError(f"missing required INF: {name}")
        match = re.search(r"(?mi)^\s*FILE_GUID\s*=\s*([0-9a-f-]{36})", inf.read_text())
        if match is None or match[1].upper() not in inner:
            raise ValueError(f"{name} was not embedded in the actual FD")
        included[name] = match[1].upper()
    print(f"FD verification: {len(included)} required modules present in decoded FVMAIN")
    return included


def build_payload(probe, work):
    payload = work / "stage1-payload.img"
    with payload.open("wb") as stream:
        stream.truncate(4 * 1024 * 1024)
    command(["mformat", "-i", payload, "-v", "LAVENDER", "-N", "0x66007001", "::"])
    command(["mmd", "-i", payload, "::/EFI", "::/EFI/UniXNU", "::/EFI/UniXNU/Payloads"])
    command(["mcopy", "-i", payload, ROOT / "stage1.toml", "::/EFI/UniXNU/unixnu.toml"])
    command(["mcopy", "-i", payload, probe, "::/EFI/UniXNU/Payloads/LavenderProbe.efi"])
    config = command(["mtype", "-i", payload, "::/EFI/UniXNU/unixnu.toml"], capture_output=True).stdout
    if config != (ROOT / "stage1.toml").read_bytes():
        raise ValueError("payload configuration did not round-trip")
    with tempfile.TemporaryDirectory(dir=work) as scratch:
        copy = Path(scratch) / "probe.efi"
        command(["mcopy", "-i", payload, "::/EFI/UniXNU/Payloads/LavenderProbe.efi", copy])
        if sha256(copy) != sha256(probe):
            raise ValueError("probe EFI did not round-trip through the FAT volume")
    print("FAT payload: configuration and EFI binary round-trip PASS")
    return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("DEBUG", "RELEASE"), default="DEBUG")
    parser.add_argument("-j", "--jobs", type=int, default=min(4, os.cpu_count() or 2))
    parser.add_argument("--skip-build", action="store_true", help="verify and repack the existing FD")
    parser.add_argument("--dtb", type=Path, help="matching device boot/recovery DTB")
    parser.add_argument("--out", type=Path, help="output directory (default: out/<target>)")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs must be positive")
    revisions = source_revisions()
    unixnu = ROOT / "UniXNU"
    uefi = ROOT / "uefi-lavender"
    edk2 = uefi / "Mu_Basecore"
    work = args.out.resolve() if args.out else ROOT / "out" / args.target.lower()
    work.mkdir(parents=True, exist_ok=True)

    command(["clang", "-Wall", "-Wextra", "-Werror", ROOT / "tests/boot-handoff-range.c", "-o", work / "range-test"])
    command([work / "range-test"])

    sys.path.insert(0, str(unixnu / "scripts"))
    from unixnu import bootimg, firmware
    board = tomllib.loads((ROOT / "firmware.toml").read_text())
    ctx = SimpleNamespace(board=board, work=work, tree=unixnu, device=ROOT,
                          logs=work / "logs", jobs=args.jobs,
                          args=SimpleNamespace(skip_preflight=False))
    if args.skip_build:
        fd = firmware.fd_path(ctx, uefi, args.target)
    else:
        # Package DEC include paths are checked even for unused libraries.
        # Initialize Basecore's direct submodules; their own optional test and
        # crypto submodules are not needed by this firmware configuration.
        command(["git", "-C", edk2, "submodule", "update", "--init", "--depth", "1", "--jobs", args.jobs])
        fd = firmware.build(ctx, {}, uefi, edk2, args.target)

    fields = {"uefi": str(uefi), "tree": str(unixnu), "edk2": str(edk2)}
    package_paths = [Path(item.format(**fields)).resolve() for item in board["firmware"]["packages"]]
    modules = check_firmware(fd, package_paths)
    probe = uefi / f"Build/lavenderPkg/{args.target}_CLANGPDB/AARCH64/LavenderProbe.efi"
    if not probe.is_file():
        raise ValueError(f"missing probe application: {probe}")
    probe_data = probe.read_bytes()
    if probe_data[:2] != b"MZ":
        raise ValueError("probe is not PE/COFF")
    pe_offset = struct.unpack_from("<I", probe_data, 0x3C)[0]
    if probe_data[pe_offset:pe_offset + 4] != b"PE\0\0" or struct.unpack_from("<H", probe_data, pe_offset + 4)[0] != 0xAA64:
        raise ValueError("probe is not an AARCH64 PE image")
    payload = build_payload(probe, work)

    dtb = args.dtb.resolve() if args.dtb else ROOT / "ios-lavender/resources/lavender_dtb.bin"
    for name, expected in (("qcom,msm-id", [0x13D, 0]), ("qcom,board-id", [0x20008, 0])):
        cells = command(["fdtget", "-t", "x", dtb, "/", name], capture_output=True, text=True).stdout.split()
        if [int(value, 16) for value in cells] != expected:
            raise ValueError(f"unexpected lavender DTB {name}")

    empty = bootimg.build(ctx, uefi, fd, dtb, output=work / "empty/lavender-uefi-empty.img")
    packed = bootimg.build(ctx, uefi, fd, dtb, ramdisk=payload, output=work / "payload/lavender-unixnu-stage1.img")
    shutil.copyfile(fd, work / "SILICIUM_UEFI.fd")
    shutil.copyfile(probe, work / "LavenderProbe.efi")
    artifacts = [empty, packed, payload, work / "SILICIUM_UEFI.fd", work / "LavenderProbe.efi"]
    report = {
        "milestone": "UEFI and UniXNU menu / protected small FAT initrd",
        "hardware_tested": False,
        "kernel_included": False,
        "target": args.target,
        "host_architecture": platform.machine(),
        "host_os": platform.freedesktop_os_release(),
        "python": platform.python_version(),
        "clang": command(["clang", "--version"], capture_output=True, text=True).stdout.splitlines()[0],
        "source_commits": revisions,
        "embedded_modules": modules,
        "artifacts": {str(path.relative_to(work)): {"bytes": path.stat().st_size, "sha256": sha256(path)} for path in artifacts},
        "dtb_sha256": sha256(dtb),
        "boot_geometry": board["bootimg"]["image"],
        "handoff_reserved_window": {"base": "0x80000000", "end": "0x85800000"},
    }
    (work / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    (work / "SHA256SUMS").write_text("".join(f"{sha256(path)}  {path.relative_to(work)}\n" for path in artifacts))
    print(f"Stage-1 build and image verification complete: {work}")
    print("Hardware boot has not yet been tested; no XNU kernel is included.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
