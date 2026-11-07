/** @file
  Conventional-memory run measurement matching DarwinBoot's 2 MiB alignment.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef LAVENDER_MEMORY_RUNS_H
#define LAVENDER_MEMORY_RUNS_H

#include <Uefi.h>

STATIC
BOOLEAN
KernelProbeLargestRun (
  IN CONST VOID  *Map,
  IN UINTN       Size,
  IN UINTN       Stride,
  IN UINT64      AlignmentOffset,
  OUT UINT64     *BestBase,
  OUT UINT64     *BestBytes
  )
{
  CONST EFI_MEMORY_DESCRIPTOR  *Descriptor;
  CONST EFI_MEMORY_DESCRIPTOR  *Next;
  UINTN                        Offset;
  UINTN                        Other;
  UINT64                       End;
  UINT64                       Candidate;
  BOOLEAN                      Extended;

  *BestBase = 0;
  *BestBytes = 0;
  if ((Map == NULL) || (Stride < sizeof (*Descriptor)) || ((Stride & 7) != 0) ||
      ((Size % Stride) != 0) || (AlignmentOffset >= SIZE_2MB) ||
      ((AlignmentOffset & EFI_PAGE_MASK) != 0)) {
    return FALSE;
  }

  for (Offset = 0; Offset < Size; Offset += Stride) {
    Descriptor = (CONST EFI_MEMORY_DESCRIPTOR *)((CONST UINT8 *)Map + Offset);
    if ((Descriptor->Type == EfiConventionalMemory) &&
        (((Descriptor->PhysicalStart & EFI_PAGE_MASK) != 0) ||
         (Descriptor->NumberOfPages > (MAX_UINT64 - Descriptor->PhysicalStart) / EFI_PAGE_SIZE))) {
      return FALSE;
    }
  }

  for (Offset = 0; Offset < Size; Offset += Stride) {
    Descriptor = (CONST EFI_MEMORY_DESCRIPTOR *)((CONST UINT8 *)Map + Offset);
    if ((Descriptor->Type != EfiConventionalMemory) || (Descriptor->NumberOfPages == 0)) {
      continue;
    }
    End = Descriptor->PhysicalStart + EFI_PAGES_TO_SIZE (Descriptor->NumberOfPages);
    do {
      Extended = FALSE;
      for (Other = 0; Other < Size; Other += Stride) {
        Next = (CONST EFI_MEMORY_DESCRIPTOR *)((CONST UINT8 *)Map + Other);
        if ((Next->Type == EfiConventionalMemory) && (Next->PhysicalStart == End) &&
            (Next->NumberOfPages != 0)) {
          End += EFI_PAGES_TO_SIZE (Next->NumberOfPages);
          Extended = TRUE;
        }
      }
    } while (Extended);
    if (Descriptor->PhysicalStart > MAX_UINT64 - (SIZE_2MB - 1) - AlignmentOffset) {
      continue;
    }
    Candidate = ALIGN_VALUE (Descriptor->PhysicalStart, SIZE_2MB) + AlignmentOffset;
    if ((Candidate < End) && (End - Candidate > *BestBytes)) {
      *BestBase = Candidate;
      *BestBytes = End - Candidate;
    }
  }
  return TRUE;
}

#endif
