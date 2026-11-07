/** @file
  Lavender kernel-readiness diagnostic: Gold 16 KiB support and live RAM map.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include "MemoryRuns.h"
#include "DiagFont.h"

#ifndef KERNEL_ALIGNMENT_OFFSET
#define KERNEL_ALIGNMENT_OFFSET  0x4000
#endif

#define GOLD_AFFINITY        0x100ULL
#define MPIDR_AFFINITY_MASK  0xFF00FFFFFFULL
#define PSCI_VERSION        0x84000000ULL
#define PSCI_CPU_ON         0xC4000003ULL
#define PSCI_AFFINITY_INFO   0xC4000004ULL
#define KERNEL_MEMORY_SIZE  SIZE_1GB

typedef struct {
  UINT64  Midr;
  UINT64  Mpidr;
  UINT64  Mmfr0;
  UINT64  CurrentEl;
  UINT64  State;
  UINT64  OffReturn;
  UINT64  Sctlr;
} GOLD_MAILBOX;

STATIC_ASSERT (OFFSET_OF (GOLD_MAILBOX, State) == 32, "Gold callback state ABI");
STATIC_ASSERT (OFFSET_OF (GOLD_MAILBOX, OffReturn) == 40, "Gold callback result ABI");
STATIC_ASSERT (OFFSET_OF (GOLD_MAILBOX, Sctlr) == 48, "Gold callback SCTLR ABI");
STATIC_ASSERT (sizeof (GOLD_MAILBOX) == 56, "Gold callback mailbox ABI");

extern CONST UINT8 LavenderGoldCodeStart[];
extern CONST UINT8 LavenderGoldCodeEnd[];

STATIC EFI_SYSTEM_TABLE              *mSystemTable;
STATIC EFI_BOOT_SERVICES             *mBootServices;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *mGop;
STATIC UINTN                        mX;
STATIC UINTN                        mY;
STATIC CONST UINT64                  mFont[] = GLYPH_FONT;

#define READ_REGISTER(Name)  ({ UINT64 Value; __asm__ volatile ("mrs %0, " #Name : "=r" (Value)); Value; })

STATIC
UINT64
Smc (UINT64 Function, UINT64 Arg1, UINT64 Arg2, UINT64 Arg3)
{
  register UINT64 X0 __asm__ ("x0") = Function;
  register UINT64 X1 __asm__ ("x1") = Arg1;
  register UINT64 X2 __asm__ ("x2") = Arg2;
  register UINT64 X3 __asm__ ("x3") = Arg3;
  __asm__ volatile ("smc #0" : "+r" (X0), "+r" (X1), "+r" (X2), "+r" (X3) :
                    : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
                      "x12", "x13", "x14", "x15", "x16", "x17", "memory");
  return X0;
}

STATIC
VOID
PrintText (CONST CHAR16 *Text)
{
  UINTN   Index;
  UINTN   Gx;
  UINTN   Gy;
  UINTN   Dx;
  UINTN   Dy;
  UINT64  Glyph;
  UINT64  Bits;
  UINT32  *Framebuffer;
  CHAR16  Ch;

  if (mGop == NULL) {
    if (mSystemTable->ConOut != NULL) {
      mSystemTable->ConOut->OutputString (mSystemTable->ConOut, (CHAR16 *)Text);
    }
    return;
  }
  Framebuffer = (UINT32 *)(UINTN)mGop->Mode->FrameBufferBase;
  for (Index = 0; Text[Index] != 0; Index++) {
    Ch = Text[Index];
    if (Ch == L'\r') { continue; }
    if ((Ch == L'\n') || (mX + 24 > mGop->Mode->Info->HorizontalResolution)) {
      mX = 24;
      mY += 56;
      if (Ch == L'\n') { continue; }
    }
    if ((Ch < 32) || (Ch > 126) || (mY + 48 > mGop->Mode->Info->VerticalResolution)) {
      continue;
    }
    Glyph = mFont[Ch - 32];
    for (Gy = 0; Gy < 12; Gy++) {
      Bits = (Gy < 6) ? ((Glyph >> 32) >> (Gy * 5)) :
             ((Glyph & 0xFFFFFFFFULL) >> ((Gy - 6) * 5));
      for (Gx = 0; Gx < 5; Gx++) {
        for (Dy = 0; Dy < 4; Dy++) {
          for (Dx = 0; Dx < 4; Dx++) {
            Framebuffer[(mY + Gy * 4 + Dy) * mGop->Mode->Info->PixelsPerScanLine + mX + Gx * 4 + Dx] =
              ((Bits >> Gx) & 1) ? 0xFFFFFFFF : 0;
          }
        }
      }
    }
    mX += 24;
  }
}

STATIC
VOID
PrintValue (CONST CHAR16 *Name, UINT64 Value)
{
  STATIC CONST CHAR16 Digits[] = L"0123456789abcdef";
  CHAR16             Number[23];
  UINTN              Index;

  Number[0] = L' ';
  Number[1] = L'0';
  Number[2] = L'x';
  for (Index = 0; Index < 16; Index++) {
    Number[3 + Index] = Digits[(Value >> (60 - Index * 4)) & 15];
  }
  Number[19] = L'\r';
  Number[20] = L'\n';
  Number[21] = 0;
  PrintText (Name);
  PrintText (Number);
}

STATIC
VOID
CleanRange (UINTN Address, UINTN Bytes)
{
  UINTN Line = 4ULL << ((READ_REGISTER (ctr_el0) >> 16) & 15);
  UINTN End = Address + Bytes;
  for (Address &= ~(Line - 1); Address < End; Address += Line) {
    __asm__ volatile ("dc cvac, %0" : : "r" (Address) : "memory");
  }
  __asm__ volatile ("dsb sy" ::: "memory");
}

STATIC
VOID
InvalidateMailbox (UINTN Address)
{
  UINTN Line = 4ULL << ((READ_REGISTER (ctr_el0) >> 16) & 15);
  UINTN End = Address + sizeof (GOLD_MAILBOX);
  for (Address &= ~(Line - 1); Address < End; Address += Line) {
    __asm__ volatile ("dc ivac, %0" : : "r" (Address) : "memory");
  }
  __asm__ volatile ("dsb sy" ::: "memory");
}

STATIC
BOOLEAN
ProbeGold (VOID)
{
  EFI_PHYSICAL_ADDRESS    Pages;
  EFI_STATUS              Status;
  volatile GOLD_MAILBOX   *Mailbox;
  UINT64                  Result;
  UINT64                  Affinity;
  UINT64                  Granule;
  UINTN                   Attempt;
  UINTN                   CodeBytes;
  BOOLEAN                 Offline;
  BOOLEAN                 Passed;

  Affinity = READ_REGISTER (mpidr_el1) & MPIDR_AFFINITY_MASK;
  PrintValue (L"Boot affinity", Affinity);
  PrintValue (L"PSCI version", Smc (PSCI_VERSION, 0, 0, 0));
  if (Affinity != 0) {
    PrintText (L"Gold test needs Silver CPU 0\r\n");
    return FALSE;
  }
  Result = Smc (PSCI_AFFINITY_INFO, GOLD_AFFINITY, 0, 0);
  PrintValue (L"Gold initial state", Result);
  if (Result != 1) {
    PrintText (L"Gold is not confirmed OFF\r\n");
    return FALSE;
  }

  CodeBytes = (UINTN)LavenderGoldCodeEnd - (UINTN)LavenderGoldCodeStart;
  if ((CodeBytes == 0) || (CodeBytes > EFI_PAGE_SIZE)) {
    return FALSE;
  }
  Pages = 0;
  Status = mBootServices->AllocatePages (AllocateAnyPages, EfiReservedMemoryType, 2, &Pages);
  if (EFI_ERROR (Status)) {
    PrintValue (L"Gold allocation error", Status);
    return FALSE;
  }
  mBootServices->SetMem ((VOID *)(UINTN)Pages, 2 * EFI_PAGE_SIZE, 0);
  mBootServices->CopyMem ((VOID *)(UINTN)Pages, (VOID *)LavenderGoldCodeStart, CodeBytes);
  Mailbox = (volatile GOLD_MAILBOX *)(UINTN)(Pages + EFI_PAGE_SIZE);
  CleanRange ((UINTN)Pages, 2 * EFI_PAGE_SIZE);
  Result = Smc (PSCI_CPU_ON, GOLD_AFFINITY, Pages, Pages + EFI_PAGE_SIZE);
  PrintValue (L"Gold CPU_ON", Result);
  if (Result != 0) {
    mBootServices->FreePages (Pages, 2);
    return FALSE;
  }

  Offline = FALSE;
  for (Attempt = 0; Attempt < 2000; Attempt++) {
    InvalidateMailbox ((UINTN)Mailbox);
    Result = Smc (PSCI_AFFINITY_INFO, GOLD_AFFINITY, 0, 0);
    if ((Mailbox->State != 0) && (Result == 1)) {
      Offline = TRUE;
      break;
    }
    mBootServices->Stall (1000);
  }
  InvalidateMailbox ((UINTN)Mailbox);
  PrintValue (L"Gold acknowledgement", Mailbox->State);
  PrintValue (L"Gold MIDR", Mailbox->Midr);
  PrintValue (L"Gold MPIDR", Mailbox->Mpidr);
  PrintValue (L"Gold MMFR0", Mailbox->Mmfr0);
  PrintValue (L"Gold CurrentEL", Mailbox->CurrentEl);
  PrintValue (L"Gold SCTLR", Mailbox->Sctlr);
  PrintValue (L"Gold final state", Result);
  Granule = (Mailbox->Mmfr0 >> 20) & 15;
  Passed = Offline && (Mailbox->State == 1) &&
           ((Mailbox->Mpidr & MPIDR_AFFINITY_MASK) == GOLD_AFFINITY) &&
           (Mailbox->CurrentEl == 4) && ((Mailbox->Sctlr & 1) == 0) &&
           ((Granule == 1) || (Granule == 2));
  if (Offline) {
    mBootServices->FreePages (Pages, 2);
  } else {
    // The callback and mailbox cannot be freed while Gold might use them.
    PrintValue (L"Gold CPU_OFF return", Mailbox->OffReturn);
    PrintText (L"Gold pages retained; reboot\r\n");
  }
  PrintText (Passed ? L"GOLD 16 KiB + CPU_OFF PASS\r\n" : L"GOLD readiness FAIL\r\n");
  return Passed;
}

STATIC
BOOLEAN
ProbeMemory (VOID)
{
  EFI_STATUS  Status;
  VOID        *Map;
  UINTN       Size;
  UINTN       Capacity;
  UINTN       Key;
  UINTN       Stride;
  UINTN       Attempt;
  UINT32      Version;
  UINT64      Base;
  UINT64      Bytes;
  BOOLEAN     Valid;

  Map = NULL;
  Size = 0;
  Status = mBootServices->GetMemoryMap (&Size, NULL, &Key, &Stride, &Version);
  if ((Status != EFI_BUFFER_TOO_SMALL) || (Size > SIZE_64KB)) {
    PrintValue (L"Memory-map size error", Status);
    return FALSE;
  }
  for (Attempt = 0; Attempt < 3; Attempt++) {
    Capacity = Size + SIZE_4KB;
    Status = mBootServices->AllocatePool (EfiLoaderData, Capacity, &Map);
    if (EFI_ERROR (Status)) { return FALSE; }
    Size = Capacity;
    Status = mBootServices->GetMemoryMap (&Size, Map, &Key, &Stride, &Version);
    if (Status != EFI_BUFFER_TOO_SMALL) { break; }
    mBootServices->FreePool (Map);
    Map = NULL;
    if (Size > SIZE_64KB) { return FALSE; }
  }
  if (EFI_ERROR (Status)) {
    if (Map != NULL) { mBootServices->FreePool (Map); }
    PrintValue (L"Memory-map error", Status);
    return FALSE;
  }
  Valid = KernelProbeLargestRun (Map, Size, Stride, KERNEL_ALIGNMENT_OFFSET, &Base, &Bytes);
  mBootServices->FreePool (Map);
  PrintValue (L"Kernel alignment offset", KERNEL_ALIGNMENT_OFFSET);
  PrintValue (L"Largest aligned base", Base);
  PrintValue (L"Largest aligned bytes", Bytes);
  PrintText (Valid && (Bytes >= SIZE_256MB) ? L"DarwinBoot 256 MiB map PASS\r\n" : L"DarwinBoot 256 MiB map FAIL\r\n");
  PrintText (Valid && (Bytes >= KERNEL_MEMORY_SIZE) ? L"Lavender 1 GiB map PASS\r\n" : L"Lavender 1 GiB map FAIL\r\n");
  return Valid && (Bytes >= KERNEL_MEMORY_SIZE);
}

EFI_STATUS
EFIAPI
LavenderKernelProbeEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  STATIC EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
  EFI_STATUS      Status;
  EFI_INPUT_KEY   Key;
  UINTN           Attempt;
  BOOLEAN         Gold;
  BOOLEAN         Memory;

  (VOID)ImageHandle;
  mSystemTable = SystemTable;
  mBootServices = SystemTable->BootServices;
  mGop = NULL;
  mX = 24;
  mY = 64;
  if (SystemTable->ConOut != NULL) {
    SystemTable->ConOut->ClearScreen (SystemTable->ConOut);
  }
  Status = mBootServices->LocateProtocol (&GopGuid, NULL, (VOID **)&mGop);
  if (EFI_ERROR (Status) || (mGop->Mode == NULL) || (mGop->Mode->Info == NULL) ||
      (mGop->Mode->FrameBufferBase != 0x9D400000) ||
      (mGop->Mode->Info->HorizontalResolution != 1080) ||
      (mGop->Mode->Info->VerticalResolution != 2340) ||
      (mGop->Mode->Info->PixelsPerScanLine < 1080) ||
      (mGop->Mode->Info->PixelsPerScanLine > mGop->Mode->FrameBufferSize / 4 / 2340) ||
      (mGop->Mode->Info->PixelFormat > PixelBlueGreenRedReserved8BitPerColor)) {
    mGop = NULL;
  } else {
    mBootServices->SetMem ((VOID *)(UINTN)mGop->Mode->FrameBufferBase, mGop->Mode->FrameBufferSize, 0);
  }
  PrintText (L"LAVENDER KERNEL READINESS\r\n\r\n");
  if (READ_REGISTER (CurrentEL) != 4) {
    PrintText (L"Probe requires EL1\r\n");
  } else {
    Gold = ProbeGold ();
    PrintText (L"\r\n");
    Memory = ProbeMemory ();
    PrintText (Gold && Memory ? L"\r\nKERNEL-READY: PASS\r\n" : L"\r\nKERNEL-READY: FAIL\r\n");
  }
  PrintText (L"CPU/RAM only; no XNU loaded\r\n");
  PrintText (L"Photo this screen. 90 seconds.\r\n");
  for (Attempt = 0; Attempt < 900; Attempt++) {
    if ((SystemTable->ConIn != NULL) &&
        !EFI_ERROR (SystemTable->ConIn->ReadKeyStroke (SystemTable->ConIn, &Key))) {
      break;
    }
    mBootServices->Stall (100000);
  }
  return EFI_SUCCESS;
}
