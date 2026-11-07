#!/usr/bin/env python3
"""Assemble Lavender's Darwin 22 kernel/ADT into the bounded UniXNU RAM payload."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import tomllib
from types import SimpleNamespace
import lzma

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "UniXNU/scripts"))
sys.path.insert(0, str(ROOT / "ios-lavender/tools"))
from unixnu import bootimg
import adt
import mkadt
import mkpayload


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def loader_policy_present(fd):
    spec = importlib.util.spec_from_file_location("stage1", ROOT / "scripts/build-stage1.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    outer = build.volume_files(fd.read_bytes())
    container = outer["9E21FD93-9C72-4C15-8C4B-E77F1DB2D792"]
    inner = None
    for kind, section, header in build.sections(container):
        if kind != 2:
            continue
        offset = struct.unpack_from("<H", section, header + 16)[0]
        decoded = lzma.decompress(section[offset:], format=lzma.FORMAT_ALONE)
        for child_kind, child, child_header in build.sections(decoded):
            if child_kind == 0x17:
                inner = build.volume_files(child[child_header:])
                break
    if inner is None:
        raise ValueError("cannot read compressed FVMAIN")
    for kind, section, header in build.sections(inner["1B64AF87-8415-4B57-9B16-680D70913B9F"]):
        if kind == 0x10:
            image = section[header:]
            bank_marker = "DARWINBOOT: block=0x%lx linked header va=0x%lx".encode("utf-16-le")
            if (all(name in image for name in (b"darwinboot-args-abi", b"darwinboot-page-size", b"darwinboot-boot-cpu"))
                    and bank_marker in image):
                return hashlib.sha256(image).hexdigest()
    raise ValueError("actual FD has no coherent Darwin 22/16 KiB bootstrap-bank loader")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, required=True, help="directory containing the new FD and build-report.json")
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--product-adt", type=Path, required=True, help="decoded Apple iOS 16.5 D22AP ADT")
    parser.add_argument("--dtb", type=Path, required=True, help="matching Lavender firmware/recovery DTB")
    parser.add_argument("--out", type=Path, default=ROOT / "out/ios16-boot")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    fd = args.firmware.resolve() / "SILICIUM_UEFI.fd"
    fw_report = json.loads((args.firmware / "build-report.json").read_text())
    expected = fw_report["artifacts"]["SILICIUM_UEFI.fd"]
    if digest(fd) != expected["sha256"] or fd.stat().st_size != expected["bytes"]:
        raise ValueError("FD differs from its build report")
    loader_hash = loader_policy_present(fd)
    kernel = args.kernel.resolve()
    manifest = (kernel.parent / "SHA256SUMS").read_text().splitlines()
    expected_kernel = next(line.split()[0] for line in manifest if line.split()[-1] == kernel.name)
    if digest(kernel) != expected_kernel:
        raise ValueError("kernel differs from its CI manifest")
    kernel_data = kernel.read_bytes()
    header = struct.unpack_from("<8I", kernel_data)
    if header[:4] != (0xfeedfacf, 0x100000c, 0, 2):
        raise ValueError("expected the plain arm64 executable Lavender kernel")
    macho = mkpayload.parse_macho(kernel_data)
    if macho["fixups"] is not None:
        raise ValueError("selected kernel needs a reviewed fixup recipe before packaging")
    for property_name, expected_cells in (("qcom,msm-id", [0x13d, 0]), ("qcom,board-id", [0x20008, 0])):
        value = subprocess.check_output(["fdtget", "-t", "x", str(args.dtb), "/", property_name], text=True)
        if [int(cell, 16) for cell in value.split()] != expected_cells:
            raise ValueError(f"wrong Lavender DTB {property_name}")

    tree = mkadt.build(native=True, board="lavender", product_adt=args.product_adt)
    chosen = next(node for node in tree.children if node.name == "chosen")
    chosen.props += [
        ("darwinboot-args-abi", 0, struct.pack("<I", 22)),
        ("darwinboot-page-size", 0, struct.pack("<I", 16384)),
        ("darwinboot-boot-cpu", 0, struct.pack("<Q", 0x100)),
        ("darwinboot-memory-size", 0, struct.pack("<Q", 0x40000000)),
    ]
    tree.children.append(mkadt.node("psci", [("compatible", mkadt.cstr("arm,psci-1.0")), ("method", mkadt.cstr("smc"))]))
    staging = out / "staging"
    staging.mkdir(exist_ok=True)
    staged_kernel = staging / "kernel.macho"
    staged_kernel.write_bytes(kernel_data)
    staged_adt = staging / "kernel.macho.adt"
    staged_adt.write_bytes(adt.serialize(tree))
    parsed, used = adt.parse(staged_adt.read_bytes())
    if used != staged_adt.stat().st_size:
        raise ValueError("ADT did not round-trip")
    # Read-only first root checkpoint; persistent kernel logging is not selected.
    command_line = ("-v serial=0 debug=0x14e cpus=1 keepsyms=1 "
                    "sweetfb_native=1 sweetcpufreq=0 sweetbattery=0 rd=emmcroot0 lavender "
                    "cs_enforcement_disable=1 unentitled_ios_sim_launch=1 launchd_unsecure_cache=1")
    if len(command_line.encode("ascii")) >= 608:
        raise ValueError("Darwin 22 command line is too long")
    config = ("version = 1\n[boot]\ndefault = \"ios16\"\ntimeout = 15\nrotate = 0\n"
              "[ui]\ntitle = \"UniXNU - Redmi Note 7 / iOS 16.5\"\n"
              "[[entry]]\nid = \"ios16\"\ntitle = \"iOS 16.5: Gold kernel and existing HFSX root\"\n"
              "type = \"xnu\"\npath = \"Payloads/kernel.macho\"\nargs = " + json.dumps(command_line) + "\n"
              "[[entry]]\nid = \"reset\"\ntitle = \"Restart\"\ntype = \"action\"\naction = \"reset\"\n")
    config_path = staging / "unixnu.toml"
    config_path.write_text(config)
    payload = out / "payload.img"
    with payload.open("wb") as stream:
        stream.truncate(20 * 1024 * 1024)
    subprocess.run(["mformat", "-i", str(payload), "-v", "LAVIOS16", "-N", "0x66016005", "::"], check=True)
    subprocess.run(["mmd", "-i", str(payload), "::/EFI", "::/EFI/UniXNU", "::/EFI/UniXNU/Payloads"], check=True)
    for path, destination in ((config_path, "::/EFI/UniXNU/unixnu.toml"), (staged_kernel, "::/EFI/UniXNU/Payloads/kernel.macho"),
                              (staged_adt, "::/EFI/UniXNU/Payloads/kernel.macho.adt")):
        os.utime(path, (315532800, 315532800))
        subprocess.run(["mcopy", "-m", "-i", str(payload), str(path), destination], check=True)
    if subprocess.check_output(["mtype", "-i", str(payload), "::/EFI/UniXNU/unixnu.toml"]) != config.encode():
        raise ValueError("FAT configuration round-trip failed")
    with tempfile.TemporaryDirectory(dir=out) as temporary:
        for source in (staged_kernel, staged_adt):
            copy = Path(temporary) / source.name
            subprocess.run(["mcopy", "-i", str(payload), "::/EFI/UniXNU/Payloads/" + source.name, str(copy)], check=True)
            if digest(copy) != digest(source):
                raise ValueError(f"FAT round-trip failed: {source.name}")
    board = tomllib.loads((ROOT / "firmware.toml").read_text())
    ctx = SimpleNamespace(board=board, work=out, tree=ROOT / "UniXNU", device=ROOT, logs=out / "logs")
    image = bootimg.build(ctx, ROOT / "uefi-lavender", fd, args.dtb.resolve(), ramdisk=payload,
                          output=out / "lavender-unixnu-ios16.img")
    report = {"target_userland": "iOS 16.5 (user-reported, already installed)", "kernel_included": True,
              "hardware_tested": False, "command_line": command_line, "kernel_sha256": digest(kernel),
              "fd_sha256": digest(fd), "actual_loader_sha256": loader_hash, "adt_sha256": digest(staged_adt),
              "product_adt_sha256": digest(args.product_adt), "dtb_sha256": digest(args.dtb),
              "payload_bytes": payload.stat().st_size, "payload_sha256": digest(payload),
              "image_bytes": image.stat().st_size, "image_sha256": digest(image),
              "cpu_affinity": "0x100", "page_bytes": 16384, "owned_memory_bytes": 0x40000000,
              "bootstrap_block_bytes": 0x2000000,
              "virtual_bank_base": hex(min(s["vmaddr"] for s in macho["segs"] if s["vmsize"]) & ~0x1ffffff),
              "linked_header_bank_offset": hex(min(s["vmaddr"] for s in macho["segs"] if s["vmsize"]) & 0x1ffffff),
              "payload_copy_range": ["0x84000000", "0x85400000"], "firmware_sources": fw_report["source_commits"]}
    (out / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    (out / "SHA256SUMS").write_text(f"{digest(image)}  {image.name}\n{digest(payload)}  {payload.name}\n")
    print(f"iOS 16 kernel payload and boot-image round trips PASS: {image}")
    print("Hardware kernel/root checkpoint pending.")


if __name__ == "__main__":
    main()
