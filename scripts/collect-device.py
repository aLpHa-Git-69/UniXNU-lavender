#!/usr/bin/env python3
"""Read-only recovery inventory and partition backups for a confirmed lavender.

The only device commands are queries, reads and adb pull. No mounts, flashes,
formats or writes to device partitions are performed.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    adb = ["adb", "-s", args.serial]

    def text(*command):
        return subprocess.run(adb + list(command), check=True, capture_output=True,
                              text=True, timeout=60).stdout.strip()

    if text("shell", "getprop", "ro.product.device") != "lavender":
        raise ValueError("the selected device is not lavender")
    identity = text("shell", "id")
    if not identity.startswith("uid=0("):
        raise ValueError("a recovery root shell is required for complete backups")

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = args.out or ROOT / "out/device" / stamp
    output.mkdir(parents=True, exist_ok=False, mode=0o700)
    print(f"Confirmed lavender recovery root shell. Private capture: {output}", flush=True)
    manifest = {"device": "lavender", "serial": args.serial, "capture_utc": stamp,
                "device_writes": False, "partitions": {}, "errors": []}

    def exists(path):
        return subprocess.run(adb + ["shell", "test", "-e", path],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                              timeout=30).returncode == 0

    def record(path):
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        return {"file": path.name, "bytes": path.stat().st_size, "sha256": digest.hexdigest()}

    try:
        by_name = next((path for path in ("/dev/block/bootdevice/by-name", "/dev/block/by-name")
                        if exists(path + "/boot")), None)
        if by_name is None:
            raise ValueError("no recognized by-name boot partition directory")
        manifest["by_name"] = by_name
        for name, command in {
            "properties.txt": ("shell", "getprop"),
            "uname.txt": ("shell", "uname", "-a"),
            "partition-links.txt": ("shell", "ls", "-l", by_name),
            "mounts.txt": ("shell", "mount"),
            "dmesg.txt": ("shell", "dmesg"),
        }.items():
            (output / name).write_text(text(*command) + "\n")
        for source, name in (
            ("/proc/meminfo", "meminfo.txt"),
            ("/proc/cpuinfo", "cpuinfo.txt"),
            ("/proc/cmdline", "cmdline.txt"),
            ("/proc/partitions", "partitions.txt"),
        ):
            subprocess.run(adb + ["pull", source, str(output / name)], check=True, timeout=90)

        for name in ("boot", "recovery", "dtbo", "xbl", "abl", "tz", "rpm", "persist"):
            source = by_name + "/" + name
            if not exists(source):
                if name in ("boot", "recovery"):
                    raise ValueError(f"required partition missing: {name}")
                continue
            size = int(text("shell", "blockdev", "--getsize64", source))
            path = output / f"{name}-backup.img"
            with path.open("xb") as stream:
                # Bound each raw shell-v2 response. On this OrangeFox/host
                # combination, legacy exec-out disconnects during binary
                # reads. Verify each chunk before appending it locally.
                for offset in range(0, size, 1024 * 1024):
                    expected = min(1024 * 1024, size - offset)
                    result = subprocess.run(adb + ["shell", "-T", "-n", "dd", f"if={source}", "bs=4096",
                                                   f"skip={offset // 4096}", f"count={(expected + 4095) // 4096}"],
                                            capture_output=True, timeout=90)
                    if result.returncode or len(result.stdout) != expected:
                        raise ValueError(f"{name} chunk at {offset:#x} incomplete: {len(result.stdout)} of {expected} bytes; {result.stderr.decode(errors='replace')}")
                    stream.write(result.stdout)
                    if (offset + expected) % (16 * 1024 * 1024) == 0:
                        print(f"{name}: read {offset + expected} of {size} bytes", flush=True)
            if path.stat().st_size != size:
                raise ValueError(f"{name} backup incomplete: {path.stat().st_size} of {size} bytes")
            info = record(path)
            info["source"] = text("shell", "readlink", "-f", source)
            manifest["partitions"][name] = info
            print(f"{name}: complete, {size} bytes, sha256 {info['sha256']}", flush=True)

        # Verify the existing root format without mounting or copying userdata.
        source = by_name + "/userdata"
        manifest["userdata_bytes"] = int(text("shell", "blockdev", "--getsize64", source))
        prefix = output / "userdata-prefix.bin"
        with prefix.open("xb") as stream:
            subprocess.run(adb + ["shell", "-T", "-n", "dd", f"if={source}", "bs=4096", "count=1"],
                           stdout=stream, stderr=subprocess.PIPE, check=True, timeout=30)
        data = prefix.read_bytes()
        signature = data[1024:1026]
        manifest["userdata_signature_at_1024"] = signature.hex()
        manifest["userdata_format"] = {b"HX": "HFSX", b"H+": "HFS+"}.get(signature, "not an HFS+/HFSX header")
        manifest["userdata_prefix"] = record(prefix)
        print(f"Existing userdata format: {manifest['userdata_format']}", flush=True)

        # Do this after the critical backups. OrangeFox's shell can read the
        # root-only sysfs node even when the adbd sync/pull service cannot.
        source = "/sys/firmware/fdt"
        path = output / "recovery-fdt.dtb"
        expected = int(text("shell", "stat", "-c", "%s", source))
        with path.open("xb") as stream:
            subprocess.run(adb + ["shell", "-T", "-n", "dd", f"if={source}", "bs=4096"],
                           stdout=stream, stderr=subprocess.PIPE, check=True, timeout=90)
        if path.stat().st_size != expected:
            raise ValueError("the recovery FDT read was incomplete")
    except Exception as error:
        manifest["errors"].append(str(error))
        raise
    finally:
        (output / "capture.json").write_text(json.dumps(manifest, indent=2) + "\n")
        (output / "SHA256SUMS").write_text("".join(f"{entry['sha256']}  {entry['file']}\n"
                                                     for entry in manifest["partitions"].values()))

    print("Capture complete. No device partition writes were performed.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
