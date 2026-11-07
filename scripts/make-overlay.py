#!/usr/bin/env python3
"""Export the reviewed source changes without firmware blobs or credentials."""
import argparse
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FILES = [
    "README.md", "CLOVER-REFERENCE.md", "REPOSITORIES.md", "AGENTS.md", ".gitignore", ".gitmodules", "firmware.toml", "stage1.toml", "stage2.toml",
    "UniXNU/drivers/DarwinBoot/DarwinBootPkg/Driver/BootArgs.h",
    "UniXNU/drivers/DarwinBoot/DarwinBootPkg/Driver/BootPolicy.h",
    "UniXNU/drivers/DarwinBoot/DarwinBootPkg/Driver/CpuHandoff.c",
    "UniXNU/drivers/DarwinBoot/DarwinBootPkg/Driver/CpuHandoff.h",
    "UniXNU/drivers/DarwinBoot/DarwinBootPkg/Driver/DarwinBoot.c",
    "UniXNU/drivers/DarwinBoot/DarwinBootPkg/Driver/DarwinBoot.inf",
    "UniXNU/drivers/DarwinBoot/DarwinBootPkg/Driver/Entry.S",
    "UniXNU/package/UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.c",
    "UniXNU/package/UniXNUPkg/Library/UniXNULib/Config.c",
    "UniXNU/package/UniXNUPkg/Library/UniXNULib/Main.c",
    "UniXNU/package/UniXNUPkg/Library/UniXNULib/UniXNULib.inf",
    "uefi-lavender/BootShim/BootShim.S",
    "uefi-lavender/Resources/Configs/lavender.toml",
    "uefi-lavender/Platforms/Xiaomi/lavenderPkg/lavender.dsc",
    "uefi-lavender/Platforms/Xiaomi/lavenderPkg/lavender.fdf",
    "uefi-lavender/Platforms/Xiaomi/lavenderPkg/Include/APRIORI.inc",
    "uefi-lavender/Silicon/Qualcomm/StarlordPkg/StarlordPkg.dsc.inc",
    "uefi-lavender/Silicon/Silicium/SiliciumPkg/Sec/Sec.c",
    "uefi-lavender/Silicon/Silicium/SiliciumPkg/Sec/Sec.inf",
    "uefi-lavender/Silicon/Silicium/SiliciumPkg/Sec/BootHandoffRange.h",
    "uefi-lavender/Silicon/Silicium/SiliciumPkg/Include/Guid/UniXNUBootHandoff.h",
    "uefi-lavender/Silicon/Silicium/SiliciumPkg/Sec/AArch64/ModuleEntryPoint.S",
]
FILES += [str(path.relative_to(ROOT)) for folder in ("scripts", "tests", "package")
          for path in (ROOT / folder).rglob("*") if path.is_file() and "__pycache__" not in path.parts]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, default=ROOT / "out/lavender-uefi-overlay.zip",
                    help="source overlay destination")
output = parser.parse_args().output.resolve()
output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for name in sorted(set(FILES)):
        entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        entry.compress_type = zipfile.ZIP_DEFLATED
        entry.external_attr = 0o100644 << 16
        archive.writestr(entry, (ROOT / name).read_bytes())
print(output)
