/* Boundary tests for the memory ownership check used before SEC dereferences FDT.
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdint.h>
#include <stdio.h>

#define STATIC static
#define IN
typedef uint64_t UINT64;
typedef int BOOLEAN;
#include "../uefi-lavender/Silicon/Silicium/SiliciumPkg/Sec/BootHandoffRange.h"

int main(void)
{
  struct test {
    uint64_t base, size, low, high;
    int expected;
  } cases[] = {
    {0x80000000, 40, 0x80000000, 0x85800000, 1},
    {0x81000000, 4 * 1024 * 1024, 0x80000000, 0x85800000, 1},
    {0x857ff000, 4096, 0x80000000, 0x85800000, 1},
    {0x857ff000, 4097, 0x80000000, 0x85800000, 0},
    {0x80000000, 0, 0x80000000, 0x85800000, 0},
    {0x7fffffff, 40, 0x80000000, 0x85800000, 0},
    {0x85800000, 1, 0x80000000, 0x85800000, 0},
    {0x94000000, 4096, 0x80000000, 0x85800000, 0},
    {0x80000000, UINT64_MAX, 0x80000000, 0x85800000, 0},
    {UINT64_MAX - 7, 16, 0, UINT64_MAX, 0},
    {1, 1, 2, 2, 0},
    {1, 1, 2, 1, 0},
  };

  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    struct test *t = &cases[i];
    if (BootHandoffRangeWithin(t->base, t->size, t->low, t->high) != t->expected) {
      fprintf(stderr, "boot handoff range case %u failed\n", i);
      return 1;
    }
  }

  struct copy_test {
    uint64_t fdt, fdt_size, initrd, initrd_size, low, high;
    int expected;
  } copies[] = {
    {0x85400000, 0x80000, 0x84000000, 0x1400000, 0x80000000, 0x85800000, 1},
    {0x85400000, 0x100000, 0x84000000, 0x400000, 0x80000000, 0x85800000, 1},
    {0x85400000, 0x80000, 0x84000000, 0x1400200, 0x80000000, 0x85800000, 0},
    {0x85400000, 0x80000, 0x84000000, 0x1400001, 0x80000000, 0x85800000, 0},
    {0x85400000, 0x80000, 0x84000000, 0, 0x80000000, 0x85800000, 0},
    {0x85400000, 0, 0x84000000, 0x400000, 0x80000000, 0x85800000, 0},
    {0x85800000, 40, 0x84000000, 0x400000, 0x80000000, 0x85800000, 0},
    {0x85400000, 0x400001, 0x84000000, 0x400000, 0x80000000, 0x85800000, 0},
    {0x85400000, 40, 0x7ffffe00, 0x200, 0x80000000, 0x85800000, 0},
    {0x85400000, 40, 0x84000001, 0x200, 0x80000000, 0x85800000, 0},
    {0x84000000, 40, 0x84000000, 0x200, 0x80000000, 0x85800000, 0},
    {UINT64_MAX - 31, 64, 0x84000000, 0x200, 0, UINT64_MAX, 0},
  };
  for (unsigned i = 0; i < sizeof(copies) / sizeof(copies[0]); i++) {
    struct copy_test *t = &copies[i];
    if (BootHandoffCopiesWithin(t->fdt, t->fdt_size, t->initrd,
        t->initrd_size, t->low, t->high) != t->expected) {
      fprintf(stderr, "boot handoff copy layout case %u failed\n", i);
      return 1;
    }
  }
  struct window_test {
    uint64_t fdt, fdt_size, fdt_low, fdt_high, initrd, initrd_size, low, high;
    int expected;
  } windows[] = {
    {0x85400000, 0x80000, 0x80000000, 0x85800000, 0xc0000000, 0x10000000, 0xc0000000, 0xd0000000, 1},
    {0x85400000, 0x100000, 0x80000000, 0x85800000, 0xc0000000, 0x400000, 0xc0000000, 0xd0000000, 1},
    {0x85400000, 40, 0x80000000, 0x85800000, 0xcffffe00, 512, 0xc0000000, 0xd0000000, 1},
    {0x85400000, 40, 0x80000000, 0x85800000, 0xc0000000, 0x10000200, 0xc0000000, 0xd0000000, 0},
    {0x85400000, 40, 0x80000000, 0x85800000, 0xbffffe00, 512, 0xc0000000, 0xd0000000, 0},
    {0x85400000, 40, 0x80000000, 0x85800000, 0xd0000000, 512, 0xc0000000, 0xd0000000, 0},
    {0x85400000, 40, 0x80000000, 0x85800000, 0xc0000001, 512, 0xc0000000, 0xd0000000, 0},
    {0x85400000, 40, 0x80000000, 0x85800000, 0xc0000000, 513, 0xc0000000, 0xd0000000, 0},
    {0x85400000, 40, 0x80000000, 0x85800000, 0xc0000000, 0, 0xc0000000, 0xd0000000, 0},
    {0x85400000, 0, 0x80000000, 0x85800000, 0xc0000000, 512, 0xc0000000, 0xd0000000, 0},
    {0x94000000, 40, 0x80000000, 0x85800000, 0xc0000000, 512, 0xc0000000, 0xd0000000, 0},
    {0x85400000, 40, 0x80000000, 0x85800000, 0xc0000000, 512, 0xd0000000, 0xc0000000, 0},
    {0xc0000000, 40, 0xc0000000, 0xd0000000, 0xc0000000, 512, 0xc0000000, 0xd0000000, 0},
    {0xc0000100, 40, 0xc0000000, 0xd0000000, 0xc0000000, 512, 0xc0000000, 0xd0000000, 0},
    {0x85400000, 512, 0x80000000, 0x85800000, 0x85400200, 512, 0x80000000, 0x85800000, 1},
    {UINT64_MAX - 31, 64, 0, UINT64_MAX, 0xc0000000, 512, 0xc0000000, 0xd0000000, 0},
  };
  for (unsigned i = 0; i < sizeof(windows) / sizeof(windows[0]); i++) {
    struct window_test *t = &windows[i];
    if (BootHandoffCopiesAcrossWindows(t->fdt, t->fdt_size, t->fdt_low,
        t->fdt_high, t->initrd, t->initrd_size, t->low, t->high) != t->expected) {
      fprintf(stderr, "boot handoff separate-window case %u failed\n", i);
      return 1;
    }
  }
  puts("boot handoff: 12 range, 12 small-payload and 16 WinPE separate-window cases PASS");
  return 0;
}
