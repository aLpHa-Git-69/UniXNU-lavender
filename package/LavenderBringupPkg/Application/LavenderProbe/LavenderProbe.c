/** @file
  Read-only stage-1 checks for the Redmi Note 7 UniXNU firmware.

  Executing this application from the FAT initrd checks the actual firmware
  interfaces without requiring an XNU kernel or changing a storage partition.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/UniXNULoader.h>

#define READ_SYSREG(Name)  ({ UINT64 Value; __asm__ volatile ("mrs %0, " #Name : "=r" (Value)); Value; })

STATIC CONST EFI_GUID mEntryArgHobGuid = {
  0xDAEE82E6, 0x8090, 0x4E72, { 0xBF, 0xB9, 0x78, 0xC1, 0x3C, 0xD4, 0xE3, 0x59 }
};

STATIC UINTN mPassed;
STATIC UINTN mFailed;

STATIC
VOID
Check (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Ok
  )
{
  Print (L"LAVENDER-PROBE: %s %s\n", Name, Ok ? L"PASS" : L"FAIL");
  if (Ok) {
    mPassed++;
  } else {
    mFailed++;
  }
}

STATIC
BOOLEAN
InitrdIsReserved (
  IN UINT64  Start,
  IN UINT64  End
  )
{
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Descriptor;
  UINTN                  Size;
  UINTN                  Capacity;
  UINTN                  Key;
  UINTN                  DescriptorSize;
  UINT32                 Version;
  UINTN                  Count;
  UINTN                  Index;
  UINTN                  Step;
  UINT64                 Cursor;
  UINT64                 RegionEnd;
  BOOLEAN                Found;
  BOOLEAN                Ok;

  if (End <= Start) {
    return FALSE;
  }

  Size = 0;
  Status = gBS->GetMemoryMap (&Size, NULL, &Key, &DescriptorSize, &Version);
  if ((Status != EFI_BUFFER_TOO_SMALL) || (DescriptorSize < sizeof (EFI_MEMORY_DESCRIPTOR))) {
    return FALSE;
  }

  // Leave room for descriptors introduced by allocating the map buffer.
  Capacity = Size + 8 * DescriptorSize;
  Map = AllocatePool (Capacity);
  if (Map == NULL) {
    return FALSE;
  }

  Size = Capacity;
  Status = gBS->GetMemoryMap (&Size, Map, &Key, &DescriptorSize, &Version);
  if (EFI_ERROR (Status)) {
    FreePool (Map);
    return FALSE;
  }

  Count  = Size / DescriptorSize;
  Cursor = Start;
  Ok     = TRUE;
  for (Step = 0; (Cursor < End) && (Step < Count); Step++) {
    Found = FALSE;
    for (Index = 0; Index < Count; Index++) {
      Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Index * DescriptorSize);
      RegionEnd  = Descriptor->PhysicalStart + EFI_PAGES_TO_SIZE (Descriptor->NumberOfPages);
      if ((Descriptor->PhysicalStart <= Cursor) && (Cursor < RegionEnd)) {
        Found = TRUE;
        if (Descriptor->Type != EfiReservedMemoryType) {
          Ok = FALSE;
        }

        Cursor = MIN (End, RegionEnd);
        break;
      }
    }

    if (!Found || !Ok) {
      Ok = FALSE;
      break;
    }
  }

  Ok = Ok && (Cursor == End);
  FreePool (Map);
  return Ok;
}

STATIC
VOID
CheckHandoff (VOID)
{
  EFI_HOB_GUID_TYPE  *Hob;
  CONST VOID         *Fdt;
  CONST VOID         *StartProp;
  CONST VOID         *EndProp;
  UINT64             Dtb;
  UINT64             Start;
  UINT64             End;
  INT32              Chosen;
  INT32              StartLength;
  INT32              EndLength;

  Hob = GetFirstGuidHob (&mEntryArgHobGuid);
  if ((Hob == NULL) || (GET_GUID_HOB_DATA_SIZE (Hob) != sizeof (UINT64))) {
    Check (L"FDT handoff HOB", FALSE);
    return;
  }

  Dtb = ReadUnaligned64 (GET_GUID_HOB_DATA (Hob));
  Fdt = (CONST VOID *)(UINTN)Dtb;
  if (FdtCheckHeader (Fdt) != 0) {
    Check (L"FDT handoff HOB", FALSE);
    return;
  }

  Check (L"FDT handoff HOB", TRUE);
  Print (L"LAVENDER-PROBE: FDT 0x%lx, %u bytes\n", Dtb, (UINT32)FdtTotalSize (Fdt));
  Chosen = FdtPathOffset (Fdt, "/chosen");
  if (Chosen < 0) {
    Check (L"initrd reserved in live memory map", FALSE);
    return;
  }

  StartProp = FdtGetProp (Fdt, Chosen, "linux,initrd-start", &StartLength);
  EndProp   = FdtGetProp (Fdt, Chosen, "linux,initrd-end", &EndLength);
  if ((StartProp == NULL) || (EndProp == NULL) ||
      ((StartLength != 4) && (StartLength != 8)) ||
      ((EndLength != 4) && (EndLength != 8)))
  {
    Check (L"initrd reserved in live memory map", FALSE);
    return;
  }

  Start = (StartLength == 8) ? Fdt64ToCpu (ReadUnaligned64 (StartProp)) : Fdt32ToCpu (ReadUnaligned32 (StartProp));
  End   = (EndLength == 8) ? Fdt64ToCpu (ReadUnaligned64 (EndProp)) : Fdt32ToCpu (ReadUnaligned32 (EndProp));
  Print (L"LAVENDER-PROBE: initrd 0x%lx-0x%lx\n", Start, End);
  Check (L"initrd reserved in live memory map", InitrdIsReserved (Start, End));
}

EFI_STATUS
EFIAPI
LavenderProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                       Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL     *Gop;
  EFI_LOADED_IMAGE_PROTOCOL        *Loaded;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_BLOCK_IO_PROTOCOL            *Block;
  UNIXNU_LOADER_PROTOCOL           *Loader;
  EFI_INPUT_KEY                    Key;
  UINT64                           Mmfr0;
  UINTN                            Tick;

  Print (L"\nLAVENDER-PROBE: Redmi Note 7 / SDM660 stage 1\n");
  Print (L"LAVENDER-PROBE: MIDR 0x%lx, MPIDR 0x%lx, EL%lu\n",
         READ_SYSREG (midr_el1), READ_SYSREG (mpidr_el1), READ_SYSREG (CurrentEL) >> 2);
  Mmfr0 = READ_SYSREG (id_aa64mmfr0_el1);
  Print (L"LAVENDER-PROBE: MMFR0 0x%lx, CNTFRQ %lu Hz\n", Mmfr0, READ_SYSREG (cntfrq_el0));
  Check (L"CPU supports 4 KiB tables", ((Mmfr0 >> 28) & 0xF) != 0xF);

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (!EFI_ERROR (Status) && (Gop->Mode != NULL) && (Gop->Mode->Info != NULL)) {
    Print (L"LAVENDER-PROBE: GOP 0x%lx, %u x %u, stride %u, format %u\n",
           Gop->Mode->FrameBufferBase, Gop->Mode->Info->HorizontalResolution,
           Gop->Mode->Info->VerticalResolution, Gop->Mode->Info->PixelsPerScanLine,
           Gop->Mode->Info->PixelFormat);
    Check (L"lavender framebuffer handoff",
           (Gop->Mode->FrameBufferBase == 0x9D400000) &&
           (Gop->Mode->Info->HorizontalResolution == 1080) &&
           (Gop->Mode->Info->VerticalResolution == 2340) &&
           (Gop->Mode->Info->PixelsPerScanLine == 1080) &&
           (Gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor));
  } else {
    Check (L"lavender framebuffer handoff", FALSE);
  }

  Status = gBS->LocateProtocol (&gUniXNULoaderProtocolGuid, NULL, (VOID **)&Loader);
  Check (L"DarwinBoot loader present", !EFI_ERROR (Status));

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (!EFI_ERROR (Status)) {
    Status = gBS->HandleProtocol (Loaded->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    Check (L"application loaded from payload filesystem", !EFI_ERROR (Status));
    Status = gBS->HandleProtocol (Loaded->DeviceHandle, &gEfiBlockIoProtocolGuid, (VOID **)&Block);
    Check (L"payload block device is read-only",
           !EFI_ERROR (Status) && (Block->Media != NULL) && Block->Media->ReadOnly);
  } else {
    Check (L"application loaded from payload filesystem", FALSE);
  }

  CheckHandoff ();
  Print (L"LAVENDER-PROBE: RESULT %s (%u passed, %u failed)\n",
         (mFailed == 0) ? L"PASS" : L"FAIL", (UINT32)mPassed, (UINT32)mFailed);
  Print (L"Photograph this screen. A key returns to the menu; timeout is 60 s.\n");
  for (Tick = 0; Tick < 600; Tick++) {
    if ((gST->ConIn != NULL) && !EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      Print (L"LAVENDER-PROBE: input scan 0x%x, unicode 0x%x\n", Key.ScanCode, Key.UnicodeChar);
      gBS->Stall (1000000);
      break;
    }

    gBS->Stall (100000);
  }

  return (mFailed == 0) ? EFI_SUCCESS : EFI_ABORTED;
}
