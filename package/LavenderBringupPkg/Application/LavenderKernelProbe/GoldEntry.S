// Position-independent PSCI callback, copied to owned physical pages.
// x0 = mailbox PA. No stack, global references, UEFI calls or GIC changes.
// SPDX-License-Identifier: BSD-2-Clause-Patent
  .text
  .balign 16
  .globl LavenderGoldCodeStart
LavenderGoldCodeStart:
  msr daifset, #15
  mov x19, x0
  ic iallu
  dsb sy
  isb
  mrs x1, midr_el1
  str x1, [x19, #0]
  mrs x1, mpidr_el1
  str x1, [x19, #8]
  mrs x1, id_aa64mmfr0_el1
  str x1, [x19, #16]
  mrs x1, CurrentEL
  str x1, [x19, #24]
  mrs x1, sctlr_el1
  str x1, [x19, #48]
  mov x1, #1
  str x1, [x19, #32]
  dsb sy
  mov w0, #2
  movk w0, #0x8400, lsl #16
  smc #0                         // PSCI_CPU_OFF: success does not return.
  str x0, [x19, #40]
  mov x1, #2
  str x1, [x19, #32]
  dsb sy
1:
  wfe
  b 1b                          // Owned pages remain reserved on this failure.
  .globl LavenderGoldCodeEnd
LavenderGoldCodeEnd:
