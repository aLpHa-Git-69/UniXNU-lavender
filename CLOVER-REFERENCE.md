# Clover SDM660 reference review

Reviewed repository: <https://github.com/qine233/UniXNU-clover>

Pinned reference checkout: `refs/UniXNU-clover`, commit
`b9ad635986998de400a7c1dec84f3e40a767e779`.

Companion firmware: <https://github.com/qine233/uefi-clover>. It is a
Mu-Silicium overlay using `Silicon/Qualcomm/StarlordPkg`, like Lavender.

## Finding

This is a relevant SDM660 XNU implementation. Its CPU/GIC, eMMC, ADT and
framebuffer work provide useful starting points for Lavender integration.
The source records hardware success on Clover; those results still require
Lavender verification after adaptation.

Clover targets iPad7,1 / iPadOS 17.7.11 (`21H461`), 4 KiB pages and all eight
Kryo 260 cores. Our reviewed Lavender kernel is XNU 8796.141.3 / Darwin 22,
with a historical 16 KiB, Gold-only profile. Clover's retained display,
HID and storage families are tied to the iPadOS 17 ABI and kernelcache.
Source can be adapted and rebuilt for a selected matching profile; the
published binaries are not an ABI match for the reviewed Darwin 22 kernel.

## Hardware comparison

| Item | Clover reference | Lavender evidence | Reuse implication |
| --- | --- | --- | --- |
| SoC | SDM660, Kryo 260, ARMv8.0 | SDM660; boot Silver 0, Gold `0x100` measured | Same baseline architecture; retain Cortex-A53-compatible ISA and disable unsupported PAN |
| CPU topology | `0..3`, `0x100..0x103`; eight-core 4 KiB SMP reported | Same SoC topology; Gold wake/16 KiB/CPU_OFF confirmed here | Adapt `ArmCpu` startup, cache maintenance, GIC and timer code; all-core kernel startup remains a separate Lavender test |
| GIC | `0x17a00000` / `0x17b00000`, stride `0x20000` | Same registers in matching recovery DT | Strong shared-controller match |
| Virtual timer | INTID 19, 19.2 MHz | Recovery DT PPI 3; Stage 1 measured 19.2 MHz | Keep timer source numbering, not another platform's default INTID 27 |
| eMMC | SDCC at `0x0c0c4000`, HC INTID 142, power INTID 144 | Recovery DT matches host/CQE/ICE ranges and SPI 110/112 | `CloverSdhci` is a useful SDCC source; transfer state, clocks, DMA and card geometry need local validation |
| Framebuffer | `0x9d400000`, 1200 x 1920, stride 4800 | `0x9d400000`, 1080 x 2340, stride 4320, measured by GOP | Reuse handoff/presentation approach with Lavender geometry and orientation |
| Owned kernel memory | 1 GiB; Clover board describes a low 2 GiB bank of its 4 GiB unit | Aligned run `0xa0004000..0xfeac0000`, about 1.48 GiB | The 1 GiB allocation budget fits; total-bank descriptions and carve-outs remain board-specific |
| Touch bus | QUP3 `0x0c177000`, GPIO 10/11, INTID 129 | Recovery DT QUP1 `0x0c175000`, GPIO 2/3, INTID 127 | Reuse QUP v2 transport logic after remapping resources |
| Touch candidates | Goodix `0x5d`, FocalTech/EDT `0x38` | Recovery DT Novatek `0x62` and Synaptics-TCM `0x20` | Candidate-specific register and contact protocols differ; DT candidates do not identify the fitted controller |
| Board ID | `0x0001000b` | `0x00020008` in matching Lavender DT | Keep Lavender's board DTB |
| Image geometry | Android v0, 4096-byte pages, base-relative `0x8000` / `0x1000000` / `0x100` | Same tested image geometry | Packer structure is reusable |
| Payload transport | 56 MiB FAT volume on the Clover `cache` partition; no RAM filesystem in its firmware | Our RAM FAT payload works; current SEC copy cap is 8 MiB | A full OS payload needs Lavender's own RAM/reservation or storage-loading policy |

The Lavender DTB used for comparison is `out/device/supplied-recovery.dtb`,
extracted from the recovery image that matched the installed recovery hash.

Exact input-source facts:

- `/soc/i2c@c175000/novatek@62`: `compatible = "novatek,NVT-ts"`.
- `/soc/i2c@c175000/synaptics_tcm@20`: `compatible = "synaptics,tcm-i2c"`.
- Parent IRQ `<0 0x5f 4>`: SPI 95, INTID 127.
- Parent GPIO properties contain pins 2 and 3.
- `/soc/sdhci@c0c4000/reg`: host `0xc0c4000/0x1000`, CQE
  `0xc0c5000/0x1000`, ICE `0xc0c8000/0x8000`.
- Host IRQs `<0 0x6e 4>` and `<0 0x70 4>`: INTIDs 142 and 144.

## Useful source entry points

- `drivers/ArmCpu/ArmCpu.cpp:255`: GIC resources read from the ADT;
  `:335`: redistributor affinity selection; `:882`: secondary startup.
  `startCPU()` cleans the physical reset-vector inputs before PSCI CPU_ON.
  `drivers/ArmCpu/README.md:15` documents timer/SMP/self-IPI controls.
- `scripts/make-clover-adt.py:170`: CPU nodes; the BCM2837 configuration uses
  a flat SMP cluster rather than a two-cluster E/P description. `:243` emits
  `darwinboot-memory-size`; `:265` emits framebuffer properties.
- `drivers/CloverSdhci/SdhciRegs.h:23`: SDCC resources and sourced register
  definitions. `drivers/CloverSdhci/README.md:93` describes the progress from
  register inspection through PIO, high-speed operation, ADMA2 and recovery.
- `drivers/CloverBlock/`: the bridge to the retained IOStorageFamily, GPT
  media discovery and APFS OS/SystemOS/Data windows. Its absolute LBAs, BSD
  aliases and logging/BCB destinations are Clover-specific.
- `drivers/CloverInput/InputCore.h`: QUP v2 tag/FIFO/state/error helpers.
  Its Goodix/FocalTech identity processing must be replaced for Lavender's
  actually identified controller.
- `drivers/NativeMobileDisplay/`, `drivers/NativeHidInput/`, and their
  archive/audit builders: iPadOS 17 display and HID plumbing. Every retained
  family binding must be re-audited for the selected kernel/OS ABI.

## What the reference currently achieves

The current device README and porting record report:

- Eight registered/running CPUs, seven secondary starts, per-core timer
  counts and IPI delivery reproduced across multiple boots/images.
- Working eMMC reads/writes and APFS OS, SystemOS and Data publication.
- `launchd`, real system daemons and `Early boot complete`.
- Accepted display swaps and copied scanout frames.

The same current README still reports a `backboardd`/SpringBoard desktop
blocker, touch candidates returning NACK, and no Adreno 512/Metal renderer.
The input `implemented` status covers transport and diagnostic/HID work,
not verified physical touch. Older paragraphs in the firmware README and
porting history retain earlier milestones; use the later device readings
when evaluating current progress.

## Integration constraints and recommendation

1. Start with the shared SDM660 CPU/GIC/timer and SDCC controller code.
2. Keep Lavender's measured framebuffer, board DTB, RAM bounds, card/GPT
   geometry and actual touch wiring. Replace Clover's fixed LBAs and log/BCB
   destinations before any storage-path use.
3. Select a matching kernel/OS ABI before integrating Clover's prelinked
   IOStorageFamily, HID or display objects. A full Clover-style build selects
   the iPadOS 17.7.11 profile; our Darwin 22 kernel requires corresponding
   source adaptation/rebuild instead.
4. Review its patch/build ordering: device-local patches live under
   `patches/xnu-clover/`, with copies integrated into the upstream XNU series.
   Several builders and references still use legacy `Native*` names. The
   device `board.toml` uses the format-1 layout.
5. Keep Lavender's verified temporary RAM-boot procedure. Clover's
   cache-partition workflow is evidence about that tablet's firmware and
   payload layout, not a Lavender requirement.

This review selects a reference, not a new OS profile or a bootable Clover
image for Lavender. No Clover code has been integrated into the running
Lavender build yet.
