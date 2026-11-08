/* Verify the actual Lavender platform descriptors used to build memory HOBs.
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdio.h>
#include <string.h>
#undef NULL
#include <Library/MemoryMapLib.h>

#define CHECK(value) do { if (!(value)) { \
  fprintf(stderr, "memory-map check failed at line %d: %s\n", __LINE__, #value); \
  return 1; } } while (0)

int main(void) {
  EFI_MEMORY_REGION_DESCRIPTOR *map;
  UINT8 count;
  unsigned payloads = 0, kernels = 0;
  GetMemoryMap(&map, &count);
  CHECK(map != NULL && count > 0);
  for (unsigned i = 0; i < count; i++) {
    CHECK(map[i].Length > 0 && map[i].Length <= MAX_UINT64 - map[i].Address);
    if (strcmp(map[i].Name, "Kernel") == 0) {
      kernels++;
      CHECK(map[i].Address == 0x80000000 && map[i].Length == 0x05800000);
      CHECK(map[i].MemoryType == EfiReservedMemoryType);
    }
    if (strcmp(map[i].Name, "WinPE Payload") == 0) {
      payloads++;
      CHECK(map[i].Address == 0xc0000000 && map[i].Length == 0x10000000);
      CHECK(map[i].Address >= 0xa0004000 && map[i].Address + map[i].Length <= 0xfeac0000);
      CHECK(map[i].HobOption == AddMem && map[i].ResourceType == EFI_RESOURCE_SYSTEM_MEMORY);
      CHECK(map[i].MemoryType == EfiReservedMemoryType && map[i].ArmAttributes == WRITE_BACK_XN);
    }
    /* MemoryPeim rejects overlapping DDR descriptors; test unsorted source too. */
    if (map[i].Address < 0x80000000) { continue; }
    for (unsigned j = i + 1; j < count; j++) {
      if (map[j].Address < 0x80000000) { continue; }
      CHECK(map[i].Address + map[i].Length <= map[j].Address ||
            map[j].Address + map[j].Length <= map[i].Address);
    }
  }
  CHECK(kernels == 1);
#ifdef WINDOWS_BUILD
  CHECK(payloads == 1);
  puts("actual Lavender map: reserved 256 MiB WinPE HOB within measured RAM, no DDR overlaps PASS");
#else
  CHECK(payloads == 0);
  puts("actual Lavender map: original Kernel reservation and no DDR overlaps PASS");
#endif
  return 0;
}
