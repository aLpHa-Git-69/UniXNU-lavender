#!/usr/bin/env python3
"""Fetch and extract Microsoft's pinned Windows 10 ARM64 WinPE boot inputs.

Uses cabextract, msitools and 7-Zip on Linux/Termux. The MSI File, Component,
Directory and Media tables select the ARM64 media and WIM cabinets. Optional
components and the ARM32/x86/x64 images are not downloaded.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import sys
import urllib.parse
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
BOOTSTRAP_URL = ("https://download.microsoft.com/download/"
                 "058b9477-7235-48ec-a700-73c5ccf9c286/"
                 "adkwinpeaddons/adkwinpesetup.exe")
BOOTSTRAP_SHA256 = "39bb281ea5d631c7e0f989c53dbcd317bd5a6c4eeff82b41b40256744d2c5d5d"
DOC_URL = "https://learn.microsoft.com/en-us/windows-hardware/get-started/adk-install"
PACKAGES = ("package_WindowsPEARMARM64_x86_en_us",
            "package_WindowsPEARMARM64wims_x86_en_us")


def digest(path, algorithm="sha256"):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()


def command(argv, *, binary=False):
    result = subprocess.run([str(arg) for arg in argv], capture_output=True,
                            text=not binary, check=False)
    if result.returncode:
        raise RuntimeError(f"command failed: {argv!r}\n{result.stderr!r}")
    return result.stdout


def download(url, path, algorithm, expected, size=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file():
        partial = path.with_name(path.name + ".partial")
        print(f"Downloading {path.name}", flush=True)
        command(["curl", "--fail", "--location", "--silent", "--show-error",
                 "--retry", "3", "--connect-timeout", "30", "--max-time", "1800",
                 "--continue-at", "-", "--output", partial, url])
        if ((size is not None and partial.stat().st_size != size) or
                digest(partial, algorithm) != expected.lower()):
            raise ValueError(f"download identity mismatch: {partial}")
        partial.replace(path)
    if ((size is not None and path.stat().st_size != size) or
            digest(path, algorithm) != expected.lower()):
        raise ValueError(f"cached input identity mismatch: {path}")
    return path


def msi_table(msi, name):
    lines = command(["msiinfo", "export", msi, name]).splitlines()
    if len(lines) < 3:
        raise ValueError(f"missing MSI {name} table")
    header = next(csv.reader([lines[0]], delimiter="\t"))
    return [dict(zip(header, row)) for row in csv.reader(lines[3:], delimiter="\t")
            if row]


def long_name(value):
    # MSI target:source and short|long name encodings.
    return value.split(":", 1)[0].split("|")[-1]


def selected_files(msi):
    directories = {row["Directory"]: row for row in msi_table(msi, "Directory")}
    components = {row["Component"]: row for row in msi_table(msi, "Component")}
    media = sorted(msi_table(msi, "Media"), key=lambda row: int(row["LastSequence"]))

    def directory_path(key):
        parts, seen = [], set()
        while key:
            if key in seen or key not in directories:
                raise ValueError("invalid MSI directory ancestry")
            seen.add(key)
            row = directories[key]
            name = long_name(row["DefaultDir"])
            if key != "TARGETDIR" and name != ".":
                parts.append(name)
            key = row["Directory_Parent"]
        return tuple(reversed(parts))

    selected = []
    for row in msi_table(msi, "File"):
        parts = directory_path(components[row["Component_"]]["Directory_"])
        try:
            index = parts.index("Windows Preinstallation Environment")
        except ValueError:
            continue
        relative = parts[index + 1:] + (long_name(row["FileName"]),)
        if relative[:2] == ("arm64", "Media"):
            destination = relative[2:]
        elif relative == ("arm64", "en-us", "winpe.wim"):
            destination = ("sources", "boot.wim")
        else:
            continue
        if not destination or any(part in ("", ".", "..") or "/" in part or "\\" in part
                                  for part in destination):
            raise ValueError("invalid MSI output path")
        sequence = int(row["Sequence"])
        cabinet = next((entry["Cabinet"] for entry in media
                        if sequence <= int(entry["LastSequence"])), None)
        if not cabinet or cabinet.startswith("#"):
            raise ValueError("selected file has no external cabinet")
        selected.append({"key": row["File"], "bytes": int(row["FileSize"]),
                         "path": PurePosixPath(*destination).as_posix(),
                         "cabinet": cabinet})
    if not selected:
        raise ValueError(f"MSI has no selected ARM64 files: {msi}")
    return selected


def wim_profile(path):
    with path.open("rb") as stream:
        header = stream.read(208)
        if len(header) != 208 or header[:8] != b"MSWIM\0\0\0":
            raise ValueError("WinPE input is not a WIM")
        packed_size, offset, original_size = struct.unpack_from("<QQQ", header, 72)
        size, flags = packed_size & ((1 << 56) - 1), packed_size >> 56
        if (flags & 4 or not 0 < size == original_size <= 1024 * 1024 or
                offset < 208 or size > path.stat().st_size - offset):
            raise ValueError("unsupported or out-of-bounds WIM XML resource")
        stream.seek(offset)
        xml = stream.read(size)
    root = ET.fromstring(xml.decode("utf-16"))
    images = root.findall("IMAGE")
    if len(images) != 1:
        raise ValueError("expected a single-image WinPE WIM")
    windows = images[0].find("WINDOWS")
    if windows is None:
        raise ValueError("missing WinPE Windows metadata")
    architecture = int(windows.findtext("ARCH", "-1"))
    version = windows.find("VERSION")
    if version is None:
        raise ValueError("missing WinPE version")
    values = {name.lower(): int(version.findtext(name, "-1"))
              for name in ("MAJOR", "MINOR", "BUILD", "SPBUILD")}
    if architecture != 12 or values["major"] != 10 or values["build"] != 19041:
        raise ValueError(f"expected Windows 10 build 19041 ARM64, found {architecture}/{values}")
    profile = {"architecture": "ARM64", "architecture_id": architecture,
               "name": images[0].findtext("NAME"), "version": values,
               "boot_index": struct.unpack_from("<I", header, 120)[0],
               "image_count": struct.unpack_from("<I", header, 44)[0]}
    if profile["image_count"] != 1 or profile["boot_index"] != 1:
        raise ValueError("WinPE WIM is not bootable at index 1")
    return profile


def check_efi(path):
    with path.open("rb") as stream:
        header = stream.read(4096)
    if len(header) < 64 or header[:2] != b"MZ":
        raise ValueError(f"not an EFI PE executable: {path}")
    offset = struct.unpack_from("<I", header, 0x3c)[0]
    if (offset + 24 + 70 > len(header) or header[offset:offset + 4] != b"PE\0\0" or
            struct.unpack_from("<H", header, offset + 4)[0] != 0xaa64 or
            struct.unpack_from("<H", header, offset + 24)[0] != 0x20b or
            struct.unpack_from("<H", header, offset + 24 + 68)[0] != 10):
        raise ValueError(f"not an ARM64 EFI application: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "out/windows-inputs")
    parser.add_argument("--inventory-only", action="store_true",
                        help="verify bootstrap/MSIs and list selected files/cabinets")
    args = parser.parse_args()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cache, media_root = output / "cache", output / "media"
    bootstrap = download(BOOTSTRAP_URL, cache / "adkwinpesetup.exe", "sha256", BOOTSTRAP_SHA256)
    manifest = command(["cabextract", "--pipe", "--filter=0", bootstrap], binary=True)
    (output / "BurnManifest.xml").write_bytes(manifest)
    root = ET.fromstring(manifest)
    payloads = {item.attrib["Id"]: item.attrib for item in root.iter()
                if item.tag.rsplit("}", 1)[-1] == "Payload"}
    by_name = {PurePosixPath(item["FilePath"].replace("\\", "/")).name: item
               for item in payloads.values() if item.get("Packaging") == "external"}
    inputs, files = [], []

    def fetch_payload(item):
        relative = item["FilePath"].replace("\\", "/")
        url = urllib.parse.urljoin(BOOTSTRAP_URL, urllib.parse.quote(relative))
        path = download(url, cache / PurePosixPath(relative).name, "sha1",
                        item["Hash"], int(item["FileSize"]))
        inputs.append({"url": url, "file": path.name, "bytes": path.stat().st_size,
                       "manifest_sha1": item["Hash"].lower(), "sha256": digest(path)})
        return path

    for key in PACKAGES:
        if key not in payloads:
            raise ValueError(f"pinned SDK package missing: {key}")
        files.extend(selected_files(fetch_payload(payloads[key])))
    inventory = {"bootstrap_url": BOOTSTRAP_URL, "bootstrap_sha256": BOOTSTRAP_SHA256,
                 "documentation_url": DOC_URL, "selected_files": files,
                 "cabinets": sorted({item["cabinet"] for item in files})}
    (output / "inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
    if args.inventory_only:
        print(json.dumps(inventory, indent=2))
        return

    seven_zip = shutil.which("7z") or shutil.which("7zz")
    if seven_zip is None:
        raise RuntimeError("7-Zip is required")
    media_root.mkdir(parents=True, exist_ok=True)
    for cabinet in inventory["cabinets"]:
        chosen = [item for item in files if item["cabinet"] == cabinet]
        if cabinet not in by_name:
            raise ValueError(f"cabinet missing from pinned manifest: {cabinet}")
        archive = fetch_payload(by_name[cabinet])
        extracted = output / "extracted" / cabinet
        extracted.mkdir(parents=True, exist_ok=True)
        print(f"Extracting {len(chosen)} ARM64 files from {cabinet}", flush=True)
        command([seven_zip, "e", "-y", "-bsp0", "-bso0", f"-o{extracted}",
                 archive, *(item["key"] for item in chosen)])
        for item in chosen:
            source = extracted / item["key"]
            if not source.is_file() or source.stat().st_size != item["bytes"]:
                raise ValueError(f"extracted MSI file mismatch: {item['key']}")
            destination = media_root / item["path"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)

    boot = media_root / "EFI/Boot/bootaa64.efi"
    if not boot.is_file():
        source = media_root / "bootmgr.efi"
        if not source.is_file():
            raise ValueError("ARM64 SDK media has no boot manager")
        boot.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, boot)
    check_efi(boot)
    for relative in ("Boot/boot.sdi", "EFI/Microsoft/Boot/BCD", "sources/boot.wim"):
        if not (media_root / relative).is_file():
            raise ValueError(f"required WinPE boot input missing: {relative}")
    profile = wim_profile(media_root / "sources/boot.wim")
    eula = command(["cabextract", "--pipe", "--filter=u1", bootstrap], binary=True)
    (output / "Microsoft-EULA.rtf").write_bytes(eula)
    artifacts = {path.relative_to(media_root).as_posix():
                 {"bytes": path.stat().st_size, "sha256": digest(path)}
                 for path in sorted(media_root.rglob("*")) if path.is_file()}
    report = {**inventory, "inputs": inputs, "wim": profile,
              "media_bytes": sum(item["bytes"] for item in artifacts.values()),
              "artifacts": artifacts, "hardware_tested": False,
              "windows_image_modified": False, "drivers_injected": False}
    (output / "input-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Verified Microsoft ARM64 WinPE media: {media_root}")
    print(f"{profile['version']}; {report['media_bytes']} bytes in {len(artifacts)} files")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
