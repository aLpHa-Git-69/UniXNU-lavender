# Lavender board/build workspace

This repository owns board configuration, the bring-up probes, measured
payload policy, ADT/image assembly and pinned component references.

Read `README.md`, `REPOSITORIES.md` and the active handoff in `WINDOWS.md`
before changing the port. Each submodule has its own applicable instructions.

The user paused iOS and selected Windows ARM64. First target is Windows 10
ARM64 WinPE `10.0.19041.1`, temporary RAM boot on the measured 3 GiB Lavender.
The saved iOS checkpoint is `8335656`: Gold CPU init and IRQ/timer discovery,
with no observed root mount. Resume iOS only when the user requests it.

Keep device firmware, the boot manager, DarwinBoot and kernel patches in
their own repositories. The user requested regular pushes of verified
milestones. Update pinned links after publishing component commits.
Keep source/build results distinct from device observations.

Hardware Stage 1 and Gold/RAM probes are PASS. The measured aligned run is
`0xa0004000..0xfeac0000`. FDT copy is `0x85400000`; the Windows profile reserves
`0xc0000000..0xd0000000` (256 MiB) for its FAT payload. The saved small-payload
profile uses `0x84000000..0x85400000` (20 MiB). Use the matching Lavender DTB
and actual GPT-by-name geometry. Platina's board dimensions and eMMC IRQ are
reference inputs; Lavender's host/power IRQs are 142/144.

Use temporary `fastboot boot` device checkpoints. Keep partition dumps,
device identifiers, credentials and Apple inputs under ignored/local paths.
Do not overwrite userdata or GPT as part of this integration.

Checks:

```sh
python3 tests/windows-checks.py
python3 scripts/fetch-winpe-inputs.py
python3 scripts/build-stage1.py --config windows-firmware.toml --target DEBUG --out out/windows-firmware -j 4
python3 scripts/audit-isa.py --fd out/windows-firmware/SILICIUM_UEFI.fd
python3 scripts/build-winpe-boot.py
```

The board's manual `Lavender Windows PE` Actions workflow builds/verifies the
pinned firmware and temporary image. Keep Microsoft input binaries in ignored
local/artifact paths; publish build recipes and provenance in source repositories.
