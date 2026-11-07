# Lavender firmware device tree

`lavender.dtb` is a sanitized static board tree from the supplied recovery
image whose full hash matched the installed recovery partition:

- Recovery SHA-256: `34b60abdb37167cd2381e38f8f3e741bed90910e931263f9c24225fcc294a709`
- Original extracted DTB SHA-256: `8613a5aa219b4d6ddc1ef9a493fb8b16e98d071d89e282d2cf589c8dffafac44`

The static `/chosen/bootargs` and `/chosen/rng-seed` properties are removed.
ABL supplies the runtime boot arguments/initrd bounds; the XNU ADT generator
supplies fresh entropy. Hardware nodes, board IDs and reservations are kept.
Sanitized DTB SHA-256:
`e31c4dbc5844e8aea919f0f30c1b655b19c75caa6a2dad7479623eeed78e398a`.

Board IDs remain `qcom,msm-id = <0x13d 0>` and
`qcom,board-id = <0x20008 0>`. This is the firmware/ABL FDT, not the Apple
product ADT or a live device dump.
