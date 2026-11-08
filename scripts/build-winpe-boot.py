#!/usr/bin/env python3
"""Assemble a checked ARM64 WinPE FAT payload and temporary Lavender boot image."""
import argparse
import importlib.util
import json
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import sys
import tempfile
import tomllib
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]
PAYLOAD_BYTES = 256 * 1024 * 1024
FASTBOOT_LIMIT = 384 * 1024 * 1024
RAMDISK_GUID = "75CA63AD-FBAE-4A06-92A0-F937C671251D"
SEC_GUID = "9AFFB503-E643-4141-8B90-17E8588B1D35"


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


INPUTS = module("winpe_inputs", ROOT / "scripts/fetch-winpe-inputs.py")
STAGE1 = module("lavender_stage1", ROOT / "scripts/build-stage1.py")


def verify_inputs(inputs):
    report = json.loads((inputs / "input-report.json").read_text())
    media = inputs / "media"
    if report["bootstrap_sha256"] != INPUTS.BOOTSTRAP_SHA256:
        raise ValueError("WinPE inputs are not from the pinned Microsoft SDK")
    actual = {path.relative_to(media).as_posix() for path in media.rglob("*") if path.is_file()}
    if actual != set(report["artifacts"]):
        raise ValueError("WinPE media file set differs from its manifest")
    for relative, item in report["artifacts"].items():
        if PurePosixPath(relative).is_absolute() or ".." in PurePosixPath(relative).parts:
            raise ValueError("invalid WinPE manifest path")
        path = media / relative
        if path.stat().st_size != item["bytes"] or INPUTS.digest(path) != item["sha256"]:
            raise ValueError(f"WinPE input changed: {relative}")
    INPUTS.check_efi(media / "EFI/Boot/bootaa64.efi")
    if INPUTS.wim_profile(media / "sources/boot.wim") != report["wim"]:
        raise ValueError("WinPE WIM metadata differs from its manifest")
    if report["media_bytes"] >= PAYLOAD_BYTES - 8 * 1024 * 1024:
        raise ValueError("WinPE media exceeds the reserved FAT payload budget")
    return report


def verify_firmware(firmware):
    report = json.loads((firmware / "build-report.json").read_text())
    defines = report.get("firmware_defines", {})
    if not all(name in defines for name in ("UNIXNU_BUILD", "WINDOWS_BUILD")):
        raise ValueError("firmware was not built with the Windows RAM-boot profile")
    if report.get("ramdisk_copy_window") != {"base": "0xc0000000", "end": "0xd0000000"}:
        raise ValueError("firmware has a different reserved WinPE payload window")
    if report["source_commits"] != STAGE1.PINS:
        raise ValueError("firmware source revisions differ from the board pins")
    for relative, item in report["artifacts"].items():
        path = firmware / relative
        if path.stat().st_size != item["bytes"] or INPUTS.digest(path) != item["sha256"]:
            raise ValueError(f"firmware artifact changed: {relative}")
    fd = firmware / "SILICIUM_UEFI.fd"
    audit = json.loads((firmware / "isa-audit.json").read_text())
    if audit["fd_sha256"] != INPUTS.digest(fd) or audit["lse_rcpc_hits"]:
        raise ValueError("firmware has no matching successful SDM660 ISA audit")
    files = STAGE1.firmware_files(fd)
    if RAMDISK_GUID not in files:
        raise ValueError("the actual FD has no initrd block driver")
    template = (b"\x04\x09\x26\x00" + bytes(16) +
                uuid.UUID("77ab535a-45fc-624b-5560-f7b281d1f96e").bytes_le +
                b"\0\0\x7f\xff\x04\0")
    executables = b"".join(section[header:] for kind, section, header
                           in STAGE1.sections(files[RAMDISK_GUID]) if kind in (0x10, 0x12))
    if template not in executables:
        raise ValueError("the actual FD lacks the Windows MEDIA_RAM_DISK_DP node")
    outer = STAGE1.volume_files(fd.read_bytes())
    sec = b"".join(section[header:] for kind, section, header
                   in STAGE1.sections(outer[SEC_GUID]) if kind in (0x10, 0x12))
    if b"WinPE Payload\0" not in sec:
        raise ValueError("the actual SEC image lacks the WinPE memory reservation")
    return report


def build_payload(inputs, firmware, output, command):
    payload = output / "winpe-payload.img"
    with payload.open("wb") as stream:
        stream.truncate(PAYLOAD_BYTES)
    command(["mformat", "-i", payload, "-F", "-c", "4", "-v", "WINPEARM64",
             "-N", "0x66007010", "::"])
    for path in sorted((inputs / "media").iterdir()):
        command(["mcopy", "-s", "-i", payload, path, "::/"])
    command(["mmd", "-i", payload, "::/EFI/UniXNU", "::/EFI/UniXNU/Payloads"])
    command(["mcopy", "-i", payload, ROOT / "windows.toml", "::/EFI/UniXNU/unixnu.toml"])
    command(["mcopy", "-i", payload, firmware / "LavenderProbe.efi",
             "::/EFI/UniXNU/Payloads/LavenderProbe.efi"])
    return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, default=ROOT / "out/windows-firmware")
    parser.add_argument("--inputs", type=Path, default=ROOT / "out/windows-inputs")
    parser.add_argument("--out", type=Path, default=ROOT / "out/windows-winpe")
    parser.add_argument("--check-inputs", action="store_true", help="verify the selected Microsoft media")
    args = parser.parse_args()
    inputs, firmware, output = args.inputs.resolve(), args.firmware.resolve(), args.out.resolve()
    input_report = verify_inputs(inputs)
    if args.check_inputs:
        print(f"Microsoft ARM64 WinPE inputs: PASS ({input_report['media_bytes']} bytes)")
        print(json.dumps(input_report["wim"], indent=2))
        return
    firmware_report = verify_firmware(firmware)
    output.mkdir(parents=True, exist_ok=True)
    commands = []

    def command(argv):
        argv = [str(item) for item in argv]
        result = subprocess.run(argv, capture_output=True, text=True)
        commands.append({"argv": argv, "returncode": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(f"command failed: {argv!r}\n{result.stdout}{result.stderr}")
        return result.stdout

    payload = build_payload(inputs, firmware, output, command)
    round_trips = []
    with tempfile.TemporaryDirectory(dir=output) as name:
        extracted = Path(name) / "file"
        files = {**input_report["artifacts"],
                 "EFI/UniXNU/unixnu.toml": {"sha256": INPUTS.digest(ROOT / "windows.toml")},
                 "EFI/UniXNU/Payloads/LavenderProbe.efi":
                 {"sha256": INPUTS.digest(firmware / "LavenderProbe.efi")}}
        for relative, item in files.items():
            if extracted.exists():
                extracted.unlink()
            command(["mcopy", "-i", payload, f"::/{relative}", extracted])
            if INPUTS.digest(extracted) != item["sha256"]:
                raise ValueError(f"FAT file did not round-trip: {relative}")
            round_trips.append(relative)

    original = (firmware / "payload/lavender-unixnu-stage1.img").read_bytes()
    if original[:8] != b"ANDROID!":
        raise ValueError("firmware baseline is not an Android boot image")
    page, size = struct.unpack_from("<I", original, 36)[0], struct.unpack_from("<I", original, 8)[0]
    firmware_kernel = original[page:page + size]
    unpacker = zlib.decompressobj(16 + zlib.MAX_WBITS)
    combined = unpacker.decompress(firmware_kernel)
    dtb = unpacker.unused_data
    fd = (firmware / "SILICIUM_UEFI.fd").read_bytes()
    if not unpacker.eof or combined[-len(fd):] != fd or not dtb:
        raise ValueError("baseline firmware kernel/DTB identity mismatch")
    kernel_path = output / "firmware-kernel.bin"
    kernel_path.write_bytes(firmware_kernel)
    image = output / "lavender-winpe.img"
    geometry = tomllib.loads((ROOT / "windows-firmware.toml").read_text())["bootimg"]["image"]
    argv = ["python3", ROOT / "uefi-lavender/Resources/Scripts/mkbootimg.py",
            "--kernel", kernel_path, "--ramdisk", payload, "-o", image]
    for key, value in geometry.items():
        argv += [f"--{key}", str(value)]
    command(argv)
    sys.path.insert(0, str(ROOT / "UniXNU/scripts"))
    from unixnu import bootimg
    boot_report = bootimg.verify(image, combined, dtb, payload, geometry)
    if image.stat().st_size > FASTBOOT_LIMIT:
        raise ValueError("WinPE boot image exceeds the measured 384 MiB fastboot limit")
    if boot_report["kernel_sha256"] != json.loads((firmware / "payload/boot.json").read_text())["kernel_sha256"]:
        raise ValueError("firmware kernel changed during WinPE image packaging")
    boot_report.update(image_sha256=INPUTS.digest(image), image_bytes=image.stat().st_size,
                       fd_sha256=INPUTS.digest(firmware / "SILICIUM_UEFI.fd"), hardware_tested=False)
    (output / "boot.json").write_text(json.dumps(boot_report, indent=2) + "\n")
    shutil.copyfile(firmware / "SILICIUM_UEFI.fd", output / "SILICIUM_UEFI.fd")
    artifacts = [image, payload, kernel_path, output / "SILICIUM_UEFI.fd"]
    report = {"milestone": "Windows 10 ARM64 WinPE temporary RAM boot",
              "hardware_tested": False, "windows_execution_observed": False,
              "drivers_injected": False, "windows_image_modified": False,
              "source_commits": firmware_report["source_commits"],
              "input_manifest_sha256": INPUTS.digest(inputs / "input-report.json"),
              "windows": input_report["wim"], "microsoft_inputs": input_report["inputs"],
              "fat_file_round_trips": round_trips,
              "ramdisk_copy_window": firmware_report["ramdisk_copy_window"],
              "payload_bytes": PAYLOAD_BYTES, "fastboot_max_bytes": FASTBOOT_LIMIT,
              "firmware_kernel_unchanged": True,
              "artifacts": {path.name: {"bytes": path.stat().st_size, "sha256": INPUTS.digest(path)}
                            for path in artifacts}}
    (output / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    (output / "SHA256SUMS").write_text("".join(f"{INPUTS.digest(path)}  {path.name}\n" for path in artifacts))
    print(f"FAT round trips: PASS ({len(round_trips)} files)")
    print("Android image round trip and unchanged firmware kernel: PASS")
    print(f"Windows PE device test: {image} ({image.stat().st_size} bytes)")
    print("Windows hardware boot result pending.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
