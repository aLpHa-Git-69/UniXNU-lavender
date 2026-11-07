/* Meaningful gaps, alignment, descriptor stride and overflow cases for the live probe.
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdio.h>
#undef NULL
#include <Uefi.h>
#include "../package/LavenderBringupPkg/Application/LavenderKernelProbe/MemoryRuns.h"

static EFI_MEMORY_DESCRIPTOR descriptor(UINT32 type, UINT64 base, UINT64 bytes)
{
  EFI_MEMORY_DESCRIPTOR d = {0};
  d.Type = type;
  d.PhysicalStart = base;
  d.NumberOfPages = bytes / EFI_PAGE_SIZE;
  return d;
}

int main(void)
{
  EFI_MEMORY_DESCRIPTOR map[3];
  UINT64 base, bytes;
  unsigned cases = 0;
#define CHECK(condition) do { cases++; if (!(condition)) { fprintf(stderr, "memory-map case %u failed\n", cases); return 1; } } while (0)

  map[0] = descriptor(EfiConventionalMemory, 0xc0000000, SIZE_512MB);
  map[1] = descriptor(EfiConventionalMemory, 0xa0000000, SIZE_512MB);
  CHECK(KernelProbeLargestRun(map, 2 * sizeof(map[0]), sizeof(map[0]), 0, &base, &bytes) && base == 0xa0000000 && bytes == SIZE_1GB);
  CHECK(KernelProbeLargestRun(map, 2 * sizeof(map[0]), sizeof(map[0]), 0x4000, &base, &bytes) && base == 0xa0004000 && bytes == SIZE_1GB - 0x4000);
  map[0].PhysicalStart += SIZE_2MB;
  CHECK(KernelProbeLargestRun(map, 2 * sizeof(map[0]), sizeof(map[0]), 0, &base, &bytes) && bytes == SIZE_512MB);
  map[0] = descriptor(EfiBootServicesData, 0xc0000000, SIZE_512MB);
  CHECK(KernelProbeLargestRun(map, 2 * sizeof(map[0]), sizeof(map[0]), 0, &base, &bytes) && bytes == SIZE_512MB);
  CHECK(!KernelProbeLargestRun(map, sizeof(map), sizeof(map[0]) - 8, 0, &base, &bytes));
  CHECK(!KernelProbeLargestRun(map, sizeof(map[0]) - 1, sizeof(map[0]), 0, &base, &bytes));
  map[0] = descriptor(EfiConventionalMemory, 0xfffffffffffff000ULL, 2 * EFI_PAGE_SIZE);
  CHECK(!KernelProbeLargestRun(map, sizeof(map[0]), sizeof(map[0]), 0, &base, &bytes));
  map[0] = descriptor(EfiConventionalMemory, 0xa0000000, 0);
  CHECK(KernelProbeLargestRun(map, sizeof(map[0]), sizeof(map[0]), 0, &base, &bytes) && bytes == 0);
  map[0] = descriptor(EfiConventionalMemory, 0xa0000001, EFI_PAGE_SIZE);
  CHECK(!KernelProbeLargestRun(map, sizeof(map[0]), sizeof(map[0]), 0, &base, &bytes));
  map[0] = descriptor(EfiConventionalMemory, 0xa0000000, SIZE_1GB + SIZE_2MB);
  CHECK(KernelProbeLargestRun(map, sizeof(map[0]), sizeof(map[0]), 0x4000, &base, &bytes) && bytes >= SIZE_1GB);
  CHECK(!KernelProbeLargestRun(map, sizeof(map[0]), sizeof(map[0]), EFI_PAGE_SIZE - 1, &base, &bytes));
  struct extended { EFI_MEMORY_DESCRIPTOR d; UINT64 extra[2]; } extended[2] = {0};
  extended[0].d = descriptor(EfiConventionalMemory, 0xc0000000, SIZE_512MB);
  extended[1].d = descriptor(EfiConventionalMemory, 0xa0000000, SIZE_512MB);
  CHECK(KernelProbeLargestRun(extended, sizeof(extended), sizeof(extended[0]), 0, &base, &bytes) && bytes == SIZE_1GB);
  printf("kernel memory map: %u gap/alignment/stride/overflow cases PASS\n", cases);
  return 0;
}
