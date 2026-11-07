# Lavender UniXNU bring-up

Working directory for adapting Mu-Silicium's Redmi Note 7 (`lavender`,
Qualcomm SDM660) UEFI platform to UniXNU.

Published board workspace: <https://github.com/aLpHa-Git-69/UniXNU-lavender>.
The user requested regular publication in separate component repositories;
[REPOSITORIES.md](REPOSITORIES.md) lists their roles and pinned commits.
Target userland is the iOS 16.5 installation already on userdata.

The rebuilt native kernel and UEFI firmware now pass their full Actions
builds. A temporary kernel/ADT boot image has been assembled and unpacked
successfully; its kernel/root hardware result is the next checkpoint.

## Current result

**UEFI, the configured UniXNU menu and all seven Stage-1 hardware checks
pass on lavender.** The photograph `IMG_20261107_020240_926.jpg` explicitly
shows `LAVENDER-PROBE: RESULT PASS (7 passed, 0 failed)`. This phone's ABL
executes RAM boots, despite the older port's notes describing a stubbed
`fastboot boot` implementation on another firmware.

The image contains a small diagnostic FAT payload and DarwinBoot. Hardware
checks confirm 4 KiB CPU translation tables, the 1080 x 2340 framebuffer,
the DarwinBoot protocol, execution of an EFI application from the payload,
read-only payload access, the FDT handoff HOB and the initrd reservation in
the live UEFI memory map. The FDT is at `0x85400000`; the 4 MiB initrd is at
`0x84000000..0x84400000`.

The initial image under `out/debug/` reached only the built-in fallback menu.
The later `out/copied-handoff/` build preserves a copy of ABL's FDT before
firmware initialization and supplies the successful handoff path. Its five
artifact checksums have been verified locally. The photo records the hardware
result, but does not measure the boot image's hash; that distinction is kept
in `out/copied-handoff/device-boot.json`.

**XNU/iOS kernel handoff is next.** The probe verifies loader availability;
it does not load a kernel. The historical Lavender kernel requires 16 KiB
pages and Gold MPIDR `0x100`, whereas this UEFI probe ran on Silver CPU 0.

The actual earlier `kernel.development.lavender` is now downloaded and
verified under `out/kernel-inputs/`. The photograph
`AGC_20261107_095435409.jpg` confirms **`KERNEL-READY: PASS`** for the
standalone image under `out/kernel-readiness/`: Gold `0x100` wakes at EL1,
supports 16 KiB pages and powers off cleanly. The largest aligned conventional
RAM run is `0xa0004000..0xfeac0000`, `0x5eabc000` bytes (about 1.48 GiB),
which satisfies the historical kernel's 1 GiB budget. This is CPU/RAM readiness;
XNU kernel entry has not yet been tested through UniXNU.

## Artifacts

Built on the supplied Ubuntu 26.04.1 x86_64 server with LLVM 21.1.8 and
Python 3.14.4. Local output: `out/debug/`. Server output:
`/home/ayush/lavender-unixnu-20261106/out/debug/`.

| File under `out/debug/` | Bytes | Purpose |
| --- | ---: | --- |
| `payload/lavender-unixnu-stage1.img` | 5,902,336 | Tested Android boot image: UEFI + 4 MiB FAT probe payload |
| `empty/lavender-uefi-empty.img` | 1,708,032 | Firmware/menu isolation image |
| `SILICIUM_UEFI.fd` | 3,145,728 | Firmware volume at `0x9fc00000` |
| `LavenderProbe.efi` | 25,600 | Firmware/CPU/GOP/payload checks |

Use the packaged `.img` with fastboot. Full hashes, source commits and image
geometry are in `build-report.json`, `SHA256SUMS` and the per-image `boot.json`.
Hardware observations are in `device-boot.json`. The table and hash below
refer to the first menu-tested image; diagnostic revisions have their own
build reports and hashes in their output directories.

Current copied-handoff image: `out/copied-handoff/payload/lavender-unixnu-stage1.img`
(5,791,744 bytes), SHA-256
`93e5432770d08bbb0fda9dc5eddaaeeb68a77f75579c25ace01d50d7dd3d2ea4`.
The firmware, payload, reports and source overlay are preserved together in
`out/lavender-stage1-probe-pass.tar.gz`.

### Gold/RAM readiness image

`out/kernel-readiness/lavender-unixnu-kernel-readiness.img`, 5,791,744 bytes,
SHA-256 `f62f1e7aa123043dde0194b220bc2866abf10bc524182ffba04a920d35511d9d`.
The EFI probe is 8,192 bytes, within a 4 MiB FAT payload. This was built
locally in Termux with Clang 21.1.8 and mtools 4.0.50, using the saved
copied-handoff firmware kernel. Its firmware-kernel hash is unchanged:
`1af02edcef130d0f85c6da4b9aef65264b75aa3b5f04042b6a096d98bb8c7629`.

The image remains in the workspace; the requested copy to Downloads was
denied. Build reports, command logs, font license and checksums are beside it.

### Reviewed kernel input

`out/kernel-inputs/kernel.development.lavender`, 17,336,784 bytes,
SHA-256 `06cc7407d47abfae429cbcbb165231dccbdee5228efc389fb91842cff2fa0435`.
This is XNU 8796.141.3 / Darwin 22.6.0, arm64 MH_EXECUTE, from CI run
`40085821240` / source commit `8cdd49252fa820a6d62bd809e7089d432022921c`.
The artifact ZIP and kernel both match their published hashes. The actual
Mach-O has no chained-fixup command. `kernel-profile.json` records the
entry, alignment, source identity and remaining handoff requirements.

Stage-1 image SHA-256:
`f2237ab3ce0da91f4a181728495db2680ceffa19b6340b2d39dfca68af2b7488`.

## Changes

- Integrate UniXNU, UniXNUDxe, DarwinBoot and UniXNURamDiskDxe into the
  lavender DSC/FDF, using the existing DiskIo/FAT/collation services.
- Build Starlord/SDM660 code for ARMv8.0 + crypto, without RCpc or outlined
  atomics.
- Describe the complete shim + firmware size in BootShim's ARM64 header.
- Preserve ABL's original FDT argument across SEC initialization, then publish
  the shared entry-argument HOB consumed by UniXNURamDiskDxe.
- Accept FDT/initrd handoff only within lavender's existing reserved Kernel
  window (`0x80000000..0x85800000`). The initial 4 MiB payload fits this policy;
  larger OS payloads need their own measured memory/reservation policy.
- Add the PI type include needed by the shared RAM-disk driver.
- Build `LavenderProbe.efi` to check real firmware interfaces before XNU work.
- Build `LavenderKernelProbe.efi` independently of the firmware volume. Its
  position-independent callback wakes Gold `0x100`, reads feature registers,
  calls CPU_OFF and confirms the core is off before releasing its pages.
  The callback uses no stack, UEFI calls or GIC programming.
- Measure the largest live conventional-memory run with the selected kernel's
  2 MiB alignment and `0x4000` offset, checking its historical 1 GiB budget.

## Verification

- Firmware build passed using UniXNU's existing firmware builder.
- Decode the **actual FD's compressed FVMAIN**, parse its FFS headers and
  verify all seven required boot/payload modules are embedded.
- FAT configuration and probe executable round-trip checks passed.
- Both boot images round-trip through UniXNU's packer/unpacker; shim header,
  gzip, firmware, DTB and ramdisk match their inputs.
- Twelve reserved-range boundary/overflow cases passed.
- ISA audit: 81 embedded executables + the probe, zero remaining LSE/RCpc
  instruction matches. Pinned Qualcomm blobs contain literals in `.text`;
  298 terminated-string matches and two inspected data-table matches are
  recorded separately in `isa-audit.json`.
- Hardware: fastboot download/boot accepted; the configured menu and EFI
  payload application are visible. The supplied photograph confirms all seven
  probe checks pass. XNU boot has not yet been tested through this firmware.
- Preserved copied-handoff artifacts: `sha256sum -c SHA256SUMS` passes for
  all five files. Hardware evidence and artifact identity are recorded separately.
- Readiness build: twelve real gap/alignment/stride/overflow cases pass; the
  Gold callback has zero relocations; the AARCH64 EFI has no imports or LSE/RCpc
  instructions. FAT and Android-image round trips pass with identical saved
  firmware-kernel bytes. The supplied device photograph confirms Gold CPU_ON,
  16 KiB support, EL1, CPU_OFF and the 1 GiB aligned-memory-map check all pass.

## Rebuild

The checked-out commits are pinned by `scripts/build-stage1.py`. Prepare the
Mu-Silicium, UniXNU and `ios-lavender` checkouts at those commits, and apply
`out/lavender-uefi-overlay.zip` to their common parent directory.

On a glibc Linux host, install build-essential, clang, lld, llvm, python3-venv,
uuid-dev, acpica-tools, nasm, device-tree-compiler and mtools. Initialize the
Mu-Silicium submodules `Mu_Basecore`, `Common/Mu`, `Common/Mu_OEM_Sample`,
`Binaries` and `Silicium-ACPI`, plus UniXNU's `drivers/DarwinBoot`.

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r uefi-lavender/pip-requirements.txt
PATH="$PWD/.venv/bin:$PATH" .venv/bin/python scripts/build-stage1.py --target DEBUG -j 4
python3 scripts/audit-isa.py --fd out/debug/SILICIUM_UEFI.fd
```

The builder initializes Basecore's direct submodules, applies the four
reviewed Mu patches for the build, then reverses those patches. To verify
and repack an existing FD, use `--skip-build`. Generate the reviewed source
overlay with `python3 scripts/make-overlay.py`. Use `--output <path>` to save
a checkpoint overlay without replacing the default archive.

## Completed device checkpoint: Gold CPU and kernel RAM

Rebuild the standalone probe with the reviewed input files in place:

```sh
python3 scripts/build-kernel-probe.py
```

For Termux, the extra payload-tool dependency is `pkg install mtools`.
The builder uses the existing Clang/LLD, MdePkg headers, DarwinBoot diagnostic
font and saved firmware; it does not need the firmware build server.

```sh
fastboot -s <serial> boot out/kernel-readiness/lavender-unixnu-kernel-readiness.img
```

The default menu entry is `Stage 2: Gold CPU and kernel memory`; it starts
after 15 seconds. The supplied photograph captures these successful checks:

- `Gold CPU_ON`, acknowledgement, MPIDR, MMFR0, CurrentEL and final state;
- `GOLD 16 KiB + CPU_OFF PASS`;
- the largest aligned RAM base/size and `Lavender 1 GiB map PASS`;
- `KERNEL-READY: PASS`.

The result remains visible for 90 seconds or until a key is pressed. This
checkpoint measures CPU/RAM readiness before kernel entry. If Gold is not
initially confirmed OFF, the probe reports that and skips the wake-up.

The old `Stage 1: firmware and RAM payload checks` entry remains available
in this image; its captured result is `PASS (7 passed, 0 failed)`.

Device observations, exact registers, RAM bounds and the evidence hash are
in `out/kernel-readiness/device-boot.json`. Build-time reports remain separate
from this hardware record.

## Next checkpoint: XNU kernel handoff

The current test image is
`out/ios16-boot-public/lavender-unixnu-ios16.img` (22,577,152 bytes), SHA-256
`d83796db90cd2ada5ad14c66ba8a8ca901811147d9d9fd705daf92a3d42494e7`.
It contains the rebuilt Darwin 22 kernel, the new loader/firmware, a 20 MiB
FAT payload and the iOS 16.5 D22AP product identity. It selects Gold `0x100`,
16 KiB pages, one CPU and `rd=emmcroot0`. The initial root path is read-only;
persistent kernel logging and userdata/GPT rewriting are not selected.

```sh
fastboot boot out/ios16-boot-public/lavender-unixnu-ios16.img
```

Photograph the last visible `DARWINBOOT`, Darwin kernel or SDHCI/root lines.
The image build/round trips pass; a running iOS root has not yet been observed.
The manual `Lavender iOS 16 test image` Actions workflow publishes the same
assembly recipe with checked kernel/firmware artifact inputs. Its generated
ADT entropy means each build has its own image hash.

On the connected phone, Root Fastboot confirmed `product: lavender`, sent
the 22,577,152-byte local image and returned `Booting OKAY` (0.947 s total).
The firmware/kernel/root screen is awaiting a photograph. The artifact recipe
also passed Actions run
[42364915071](https://github.com/aLpHa-Git-69/UniXNU-lavender/actions/runs/42364915071).
Fastboot acceptance is recorded separately from kernel/root execution.

Reproduce with local checked inputs:

```sh
python3 scripts/build-ios16-boot.py --firmware out/ios16-firmware \
  --kernel out/ios16-kernel/kernel.development.lavender \
  --product-adt out/ios16-apple/d22ap.adt --dtb resources/lavender.dtb \
  --out out/ios16-boot-public
```

The iOS milestone needs a compatible kernel/ADT and the SDM660 storage,
display and input integration for the selected OS ABI. The historical
Lavender XNU path uses 16 KiB pages on Gold CPUs; the required Gold CPU and
RAM budget are now measured on this firmware. UniXNU's
SM8550-specific GPU/storage/touch drivers are not SDM660 implementations.

The published loader implements Gold handoff and the 736-byte Darwin 22
boot-args ABI; the board ADT explicitly selects it. The rebuilt SEC accepts
the disjoint 20 MiB FAT copy slot. The earlier readiness image used the
4 MiB payload; hardware kernel/root verification now applies to this new image.

## Reference repositories

- <https://github.com/UniXNU/UniXNU>
- <https://github.com/Project-Silicium/Mu-Silicium>
- <https://github.com/aLpHa-Git-69/ios-lavender> (`lavender` branch)
- <https://github.com/qine233/UniXNU-clover> (Mi Pad 4, same SDM660)

The Clover reference is checked out at `refs/UniXNU-clover`, pinned to
`b9ad635986998de400a7c1dec84f3e40a767e779`. It supplies relevant 4 KiB
CPU/GIC/timer, SDCC/eMMC and IOKit-storage work, with eight-core and APFS
device readings. It targets iPadOS 17.7.11; our reviewed kernel is Darwin 22.
Its touch and desktop are still unfinished, and its QUP3/Goodix/FocalTech
wiring differs from the QUP1/Novatek/Synaptics-TCM candidates in Lavender's
matching recovery DT. See [CLOVER-REFERENCE.md](CLOVER-REFERENCE.md) for the
measured controller matches, board differences and reuse entry points.

The historical iOS repository currently has Whyred-oriented recent notes.
Use its Lavender-specific evidence rather than transplanting Whyred's panel
geometry, DTB identifiers or boot-image limits.

## Initial hardware evidence

From the Lavender section of `ios-lavender/NOTES.md`:

- Working framebuffer: `0x9d400000`, 1080 x 2340, 4320-byte stride,
  little-endian BGRA pixels.
- GICv3: distributor `0x17a00000`, redistributors `0x17b00000`, stride
  `0x20000`.
- eMMC: Qualcomm SDCC v5 at `0x0c0c4000`, GIC SPI 110.
- ABL enters the historical shim on Silver CPU 0; its 16 KiB XNU path moves
  to Gold MPIDR `0x100`. UEFI CPU/page-size requirements must be checked
  against the actual firmware and DarwinBoot.
- Matching DTB identifiers recorded there: `qcom,msm-id = <0x13d 0>` and
  `qcom,board-id = <0x20008 0>`.
- The historical Lavender ABL reportedly acknowledges `fastboot boot`
  without executing the image. Recovery boot was used successfully.

## Device observations in this session

- Redmi Note 7, `lavender`, 3 GiB RAM / 32 GB eMMC variant. Recovery reports
  `MemTotal = 2801856 KiB`; the complete handoff RAM map is still to be reviewed.
- OrangeFox kernel: `4.19.325-cip124-st8-OrangeFox-lavender-southwest`.
- Boot and recovery: 64 MiB each. Fastboot maximum download: 384 MiB.
- Fastboot reports `product: lavender`, `unlocked: yes`.
- The installed iOS userdata has an HFSX (`HX`) volume header.
- Active panel in recovery's command line:
  `qcom,mdss_dsi_nt36672a_shenchao_fhdplus_video`.
- The supplied `/storage/emulated/0/Download/recovery.img` is 64 MiB and its
  full SHA-256 **matches the installed recovery partition**:
  `34b60abdb37167cd2381e38f8f3e741bed90910e931263f9c24225fcc294a709`.
- That supplied recovery carries a 497,795-byte matching appended DTB.
  Its Android v0 geometry matches the stage-1 image: base 0, kernel `0x8000`,
  ramdisk `0x1000000`, tags `0x100`, 4096-byte pages.

Private device facts and partial backup attempts are under `out/device/`.
The bulk backup attempts were interrupted and are **not verified restore
images**. A complete recovery restore source was instead verified as above.
No device partition has been flashed in this session.

For this Termux host, direct-root Android platform tools are extracted under
`/data/data/com.termux/files/usr/tmp/opencode/root-adb-tools/`. Its wrappers
use KernelSU, ADB port 5038 and the legacy USB backend (`ADB_LIBUSB=0`).
The older termux-adb/Termux:API path required repeated USB grants; large
binary reads also caused USB resets on this host. Record transfer results
before relying on a dump. `scripts/collect-device.py` reads in bounded raw
shell-v2 chunks and creates checksummed private captures.
