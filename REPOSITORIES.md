# Published source repositories

| Repository | Responsibility | Current component pin |
| --- | --- | --- |
| [UniXNU-lavender](https://github.com/aLpHa-Git-69/UniXNU-lavender) | This board/build workspace, probes and image assembly | Board commit |
| [uefi-lavender](https://github.com/aLpHa-Git-69/uefi-lavender/tree/lavender) | Mu-Silicium platform, FDT/initrd preservation and Windows PE RAM reservation | `107bdbcb5d4c2821fdf7e16c330084056d6a6a8f` |
| [UniXNU](https://github.com/aLpHa-Git-69/UniXNU/tree/lavender) | Boot manager, Windows RAM-disk path, RAM filesystem and loader submodule | `27ed66768fba420db0fc9919f777d0a2666ed7c9` |
| [DarwinBoot](https://github.com/aLpHa-Git-69/DarwinBoot/tree/lavender) | Darwin 22/23 ABI, PSCI handoff and coherent 16 KiB bootstrap banks | `66b68e00ef1a4bb15aaa35a8177b54b72850d921` |
| [ios-lavender](https://github.com/aLpHa-Git-69/ios-lavender/tree/lavender) | Paused Darwin 22 kernel/ADT checkpoint with native Gold CPU initialization and IRQ/timer discovery | `83356567124670f50a320b8ecc7c04f7ccf8f8e0` |

The top-level submodule links and `scripts/build-stage1.py` pin the inputs.
The UniXNU submodule pins DarwinBoot separately. Component repositories
are published before updating board links; generated images/logs are build
artifacts rather than source inputs.

## Active Windows milestone

The user paused iOS and selected Windows ARM64. Published firmware `b01cc99`
adds a checked 256 MiB WinPE copy reservation inside measured RAM; boot manager
`27ed667` adds the standard volatile RAM-disk device path for Windows builds.
Firmware `107bdbc` fixes the Windows-only unused-header-helper compilation error.
Actual host/UBSan driver, memory-map and handoff checks pass, as does changed
source compilation for Cortex-A53 ARM64. Full Windows FD/image and hardware
results are tracked in [WINDOWS.md](WINDOWS.md). Microsoft's boot inputs are
local/artifact data, checked by the board recipe.

## Saved iOS build record

Kernel Actions run [42359466404](https://github.com/aLpHa-Git-69/ios-lavender/actions/runs/42359466404)
compiled and linked the updated `LAVENDER` target successfully. Its artifact
still needs image and hardware checks. Userland iOS 16.5 is user-reported;
the installation's `SystemVersion.plist` has not yet been read.

Board firmware Actions run
[42363317189](https://github.com/aLpHa-Git-69/UniXNU-lavender/actions/runs/42363317189)
passed the full FD build and ISA audit. Kernel/image assembly now uses
`scripts/build-ios16-boot.py`; the manual image workflow publishes the checked
temporary test artifact. Both source and generated reports identify the
kernel, loader, FD, ADT, DTB and image hashes. Hardware kernel/root boot is pending.
