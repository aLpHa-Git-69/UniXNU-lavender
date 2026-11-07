# Lavender board/build workspace

This repository owns board configuration, the bring-up probes, measured
payload policy, ADT/image assembly and pinned component references.

Read `README.md`, `REPOSITORIES.md`, `CLOVER-REFERENCE.md` and the active
handoff in `ios-lavender/NOTES.md` before changing the port. Each submodule
has its own applicable instructions.

Target userland is the user's already-installed iOS 16.5 HFSX userdata.
The first native kernel checkpoint uses Darwin 22, Gold `0x100`, 16 KiB,
`cpus=1`; eight-core/4 KiB work follows reliable root boot.

Keep device firmware, the boot manager, DarwinBoot and kernel patches in
their own repositories. The user requested regular pushes of verified
milestones. Update pinned links after publishing component commits.
Keep source/build results distinct from device observations.

Hardware Stage 1 and Gold/RAM probes are PASS. The measured aligned run is
`0xa0004000..0xfeac0000`; the native ownership request is 1 GiB. FDT copy
is `0x85400000`; the reserved FAT slot is `0x84000000..0x85400000` (20 MiB).
Use the matching Lavender DTB and actual GPT-by-name geometry. Clover's
tablet GPIOs, partition LBAs, panel dimensions and iPadOS 17 ABI are references.

Use temporary `fastboot boot` device checkpoints. Keep partition dumps,
device identifiers, credentials and Apple inputs under ignored/local paths.
Do not overwrite userdata or GPT as part of this integration.

Checks:

```sh
python3 tests/darwin-checks.py --sanitize --output out/darwinboot-checks
clang -Wall -Wextra -Werror tests/boot-handoff-range.c -o out/range-test
out/range-test
python3 scripts/build-stage1.py --target DEBUG --out out/firmware
python3 scripts/audit-isa.py --fd out/firmware/SILICIUM_UEFI.fd
```

Kernel compilation is in the `ios-lavender` Actions workflow. This board's
firmware workflow builds the pinned Mu-Silicium/UniXNU/DarwinBoot integration.
