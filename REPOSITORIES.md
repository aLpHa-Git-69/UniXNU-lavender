# Published source repositories

| Repository | Responsibility | Current component pin |
| --- | --- | --- |
| [UniXNU-lavender](https://github.com/aLpHa-Git-69/UniXNU-lavender) | This board/build workspace, probes and image assembly | Board commit |
| [uefi-lavender](https://github.com/aLpHa-Git-69/uefi-lavender/tree/lavender) | Mu-Silicium platform, FDT/initrd preservation and reservations | `3b2e7d8ce4dc02bd1cdf66db6421d919410c3a50` |
| [UniXNU](https://github.com/aLpHa-Git-69/UniXNU/tree/lavender) | Boot manager, RAM filesystem and loader submodule | `17aaf5262336a6e4bd7f722d168730d22b750725` |
| [DarwinBoot](https://github.com/aLpHa-Git-69/DarwinBoot/tree/lavender) | Darwin 22/23 ABI and PSCI CPU handoff | `6e8900cdca6e69d5426aeecbb6b62ec4e803a327` |
| [ios-lavender](https://github.com/aLpHa-Git-69/ios-lavender/tree/lavender) | Native Darwin 22 kernel, Clover-inspired SDCC/GIC patches and explicit product-ADT input | `f9707f0af3c5b3b4f22e7978ef4bc89583886c97` |

The top-level submodule links and `scripts/build-stage1.py` pin the inputs.
The UniXNU submodule pins DarwinBoot separately. Component repositories
are published before updating board links; generated images/logs are build
artifacts rather than source inputs.

Kernel Actions run [42359466404](https://github.com/aLpHa-Git-69/ios-lavender/actions/runs/42359466404)
compiled and linked the updated `LAVENDER` target successfully. Its artifact
still needs image and hardware checks. Userland iOS 16.5 is user-reported;
the installation's `SystemVersion.plist` has not yet been read.
