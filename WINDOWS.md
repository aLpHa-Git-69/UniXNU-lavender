# Lavender Windows ARM64 bring-up

## Active goal

The user paused iOS and selected Windows for the existing Redmi Note 7 / SDM660
UEFI. The first hardware checkpoint is **Windows 10 ARM64 WinPE from a temporary
RAM boot**. Target device: the measured 3 GiB RAM / 32 GB eMMC Lavender phone.

The native iOS checkpoint is saved at `ios-lavender` commit
`83356567124670f50a320b8ecc7c04f7ccf8f8e0` on the published `lavender` branch.
Photo `AGC_20261107_191649282.jpg` confirms interrupt-controller and timer
discovery, followed by Gold CPU initialization; no root-mount output is visible.
Its SHA-256 is `476ea946ff8027ca74ec3f1601171629b59f19be216c6d697edf6d24ccc4ace3`.

## Starting point and reference review

Existing Mu-Silicium firmware at `uefi-lavender` is the starting point. Its
upstream `Status.md` (Redmi Note 7, lines 5471–5534 at baseline `8471e47`) reports
Windows PE boot with CPU/display working, and no Windows internal-storage or
input support. UEFI storage support is already present. This upstream report
does not establish Windows execution on the current phone.

Existing Starlord ACPI describes eight CPU affinities (`0..3`, `0x100..0x103`),
GICv3 at `0x17a00000` / `0x17b00000`, and timer PPIs 17/18/19. FADT revision 6
sets hardware-reduced ACPI and PSCI-over-SMC. The DSDT is CPU-only; it does not
publish an eMMC device to Windows. The current package includes APIC, DSDT,
FACP, GTDT and the common SSDT.

User reference: <https://github.com/hiratazx-playground/edk2-msm-platina/tree/master>,
checked out read-only at `refs/edk2-msm-platina`, revision
`a23a067299b9a319a0d994d8365c8b04290fecbe`. It explicitly describes itself as a
broken Mi 8 Lite port. Useful shared-SoC inputs have these board differences:

| Input | Platina reference | Lavender source/evidence |
| --- | --- | --- |
| Framebuffer | `0x9d400000`, 1080×2280 | Same base, 1080×2340, stride 4320 bytes |
| Firmware placement | `0xce000000`, 32 MiB | `0x9fc00000`, 3 MiB |
| eMMC | `0x0c0c4000`, `_HID=QCOM24BF`, IRQ `0x9d` (157), range `0x200` | Recovery DT host/CQE/ICE ranges; host IRQ 142 and power IRQ 144 |
| Power dependencies | MSM8998-derived PEP/PMIC code, with TODOs | Requires the phone's actual controller/regulator resources |

A closer storage-driver candidate is
<https://github.com/AistopGit/windows_oem_xiaomi_nitrogen>, revision
`da5826848482700805f70c7a5b9480a32c8e7f4b` (Mi Max 3 / SDM636). Its ARM64
`components/eMMC/sdhc.inf` matches `ACPI\QCOMF294`, rather than Platina's
`QCOM24BF`; the pack records MSM8998 reference-driver origins. Its use on
Lavender requires a matching, locally verified ACPI storage description and
driver test. No donor storage driver has been injected into this WinPE image.

## Microsoft boot inputs

`scripts/fetch-winpe-inputs.py` downloads the fixed Windows 10 ADK WinPE add-on
from Microsoft's download server. The bootstrap is pinned by SHA-256; selected
MSIs/cabinets are checked against its Burn manifest. MSI directory/component/
file/media tables select the ARM64 boot media and WIM rather than another CPU
architecture or the optional-component cabinets.

Microsoft documentation: <https://learn.microsoft.com/en-us/windows-hardware/get-started/adk-install>.
The verified WIM is Windows PE ARM64 `10.0.19041.1`, architecture ID 12, one
image, boot index 1. Boot inputs contain 57 files / 228,865,340 bytes, including
`EFI/Boot/bootaa64.efi`, `EFI/Microsoft/Boot/BCD`, `Boot/boot.sdi` and
`sources/boot.wim`. Inputs, extracted binaries and the Microsoft EULA are local
under ignored `out/windows-inputs/`. Source repositories carry the recipe.

## RAM-boot integration

`windows-firmware.toml` selects `UNIXNU_BUILD=1` and `WINDOWS_BUILD=1` for the
existing Lavender DSC. The Windows-specific memory policy is:

| Item | Address/range |
| --- | --- |
| FDT copy | `0x85400000`, inside the existing `0x80000000..0x85800000` reservation |
| WinPE FAT copy | `0xc0000000..0xd0000000` (256 MiB) |
| Measured conventional run | `0xa0004000..0xfeac0000` |
| Fastboot maximum image | 384 MiB |

The platform reserves the high payload with a system-memory allocation HOB
before DXE. RamManager maps gaps around existing descriptors, retaining this
reservation. SEC validates the two independent windows and their disjointness
before copying and validates the final initrd against its high-RAM reservation.

For Windows builds, UniXNURamDiskDxe publishes a standard UEFI 2.6 volatile
`MEDIA_RAM_DISK_DP` node (38 bytes plus the end node), with the actual start and
inclusive end. Its Block I/O stays read-only. `windows.toml` uses the existing
EFI chainloader to start Microsoft's ARM64 boot manager after ten seconds.

The image recipe checks source/FD hashes, the actual FD's RAM-disk node and SEC
reservation, ISA audit identity, Microsoft input hashes/WIM architecture,
every FAT file round trip and every Android boot-image component. Final image
size is bounded by the measured fastboot maximum. The hardware procedure is a
temporary `fastboot boot`; userdata and GPT are outside this recipe.

## Build and verification

On Linux/Termux install Clang/LLD/LLVM, Python 3.11+, mtools, cabextract,
msitools and 7-Zip. A full firmware build also uses the host tools listed in
the board's existing build instructions.

```sh
python3 tests/windows-checks.py
python3 scripts/fetch-winpe-inputs.py
python3 scripts/build-stage1.py --config windows-firmware.toml --target DEBUG --out out/windows-firmware -j 4
python3 scripts/audit-isa.py --fd out/windows-firmware/SILICIUM_UEFI.fd
python3 scripts/build-winpe-boot.py
```

The manual `Lavender Windows PE` Actions workflow performs these checks,
builds the FD/image and publishes the temporary image and manifests.

Host checks exercise 40 real handoff boundary/layout cases, both actual
initrd-driver modes (32/64-bit address publication, reads, media errors and
write rejection), and both actual platform memory maps. Native changed sources
compile for Cortex-A53 ARM64 and are checked for LSE/RCpc instructions.

## Current handoff

Microsoft inputs are downloaded/verified. Windows RAM-profile source changes,
image recipe and manual CI workflow are implemented. Host/UBSan checks pass,
including actual driver/memory-map code and Cortex-A53 ARM64 compilation.
Published component commits: firmware `107bdbcb5d4c2821fdf7e16c330084056d6a6a8f`,
boot manager `27ed66768fba420db0fc9919f777d0a2666ed7c9`. Board pins now select
those commits and the saved iOS checkpoint. Windows-profile compilation, full
ISA and final image verification now pass; the actual Windows hardware screen
is next. Record its image identity and observations here before stopping.

First Windows Actions run `42451849485` at board `bbcdc3d` passed all host/UBSan
checks, then failed SEC compilation with `unused function 'BootHandoffCopiesWithin'
[-Werror,-Wunused-function]`. The original-profile control run `42451847767`
passed its full firmware build/ISA audit. Firmware `107bdbc` makes the three
pure header helpers static inline; all 40 handoff cases still pass.

Corrected Windows run
[42452410093](https://github.com/aLpHa-Git-69/UniXNU-lavender/actions/runs/42452410093)
at board `a8413c4d7336ec7a47b1fb2f525f6f65acc10a1b` passed in 2m52s: host/UBSan,
full Windows FD build, ISA audit, checked Microsoft ARM64 inputs, 59 FAT-file
round trips and all Android-image component checks.

Downloaded local files:

- Image: `out/windows-ci/windows-winpe/lavender-winpe.img`, 270,159,872 bytes,
  SHA-256 `4356b3222dea535a44c6afa2576eb1c6c87ad73dde72976dd2f06e084df6a66a`.
- FD: `out/windows-ci-logs/windows-firmware/SILICIUM_UEFI.fd`, 3,145,728 bytes,
  SHA-256 `d8e4f2a916589ccef1dc5e111d15445bc04e914785f09c6a927dc66150cf05de`.
- Image manifest and boot report are beside the image; source/ISA reports are
  beside the FD. Both downloaded file hashes match the CI manifests.
- Artifact `lavender-winpe-ramboot`: ID `13606142314`, GitHub-reported ZIP SHA-256
  `5b9f18aa5b9730b9c1262a466ff4e768a135da4ca9867bfd6cbfa01aded35f57`.
- Artifact `lavender-winpe-build-logs`: ID `13605224989`, GitHub-reported ZIP SHA-256
  `1547f913c74d0ec7d3e6a581f2436e190583e30e2b6d7e6becaf7409c3105fb3`.

After the user put the phone in Fastboot and connected USB, the stock/root
tool confirmed `product: lavender` and `max-download-size: 402653184`. Use:

```sh
fastboot boot out/windows-ci/windows-winpe/lavender-winpe.img
```

The menu defaults to `Windows 10 ARM64: WinPE RAM boot` after ten seconds.
Capture the final Windows/logo/error/console screen. A Windows command prompt
would establish PE userland execution; a successful Fastboot transfer alone
only establishes image acceptance.

**Temporary hardware boot accepted:** the exact image above sent as `263828 KB`
with download OKAY (8.190 s), boot OKAY (0.143 s), total 9.861 s. No partition
flash was issued. The private serial/command result record is
`out/windows-ci/windows-winpe/device-boot.json`. Windows kernel/userland execution
is awaiting the user's final screen photograph. Resize a supplied camera photo
to about 1100 pixels wide before reading it into the agent context.

On this Termux host, the stock ARM64 Android Tools 37 binary is extracted under
`/data/data/com.termux/files/usr/tmp/opencode/root-adb-tools/data/data/com.termux/files/usr/bin/`.
It runs with `LD_LIBRARY_PATH=/data/data/com.termux/files/usr/lib`; KernelSU root
provides USB-device access. The existing termux-fastboot frontend did not list
a target initially; the stock/root binary detected and RAM-booted Lavender after
the user entered Fastboot mode.

After a WinPE hardware result, the next Windows OS integration is Lavender's
SDCC ACPI/driver path. The upstream CPU-only DSDT and a RAM-booted PE image do
not establish an installable Windows system or working touch/storage drivers.
