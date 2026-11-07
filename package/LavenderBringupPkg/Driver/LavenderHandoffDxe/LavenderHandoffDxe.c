/** @file
  Visible diagnostic for a missing boot-image initrd, before the boot menu.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <PiDxe.h>
#include <Guid/UniXNUBootHandoff.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

STATIC CONST EFI_GUID mDiagnosticGuid = UNIXNU_BOOT_HANDOFF_DIAGNOSTIC_GUID;

EFI_STATUS
EFIAPI
LavenderHandoffDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HOB_GUID_TYPE                 *Hob;
  UNIXNU_BOOT_HANDOFF_DIAGNOSTIC     *Record;

  Hob = GetFirstGuidHob (&mDiagnosticGuid);
  if ((gST == NULL) || (gST->ConOut == NULL)) {
    DEBUG ((DEBUG_ERROR, "LAVENDER-DIAG: no console; diagnostic HOB %a\n", (Hob != NULL) ? "present" : "missing"));
    return EFI_SUCCESS;
  }

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"\nLAVENDER HANDOFF DIAGNOSTIC\n\n");
  if ((Hob == NULL) || (GET_GUID_HOB_DATA_SIZE (Hob) != sizeof (*Record))) {
    Print (L"Diagnostic HOB missing\n");
  } else {
    Record = GET_GUID_HOB_DATA (Hob);
    Print (L"Bootloader x0: 0x%lx\n", Record->EntryArg);
    Print (L"Original x0:   0x%lx\n", Record->RawEntryArg);
    Print (L"Entry SCTLR:   0x%lx  PAR: 0x%lx\n", Record->EntrySctlr, Record->EntryPar);
    Print (L"Handoff status: %u\n", Record->Status);
    Print (L"FDT bytes: %u\n", Record->FdtBytes);
    Print (L"Initrd start: 0x%lx\n", Record->InitrdStart);
    Print (L"Initrd end:   0x%lx\n\n", Record->InitrdEnd);
    Print (L"0=published, 1=unmapped arg, 2=bad FDT, 3=FDT range\n");
    Print (L"4=no chosen, 5=no initrd, 6=bad cells, 7=initrd range\n");
  }

  Print (L"\nPhotograph this screen. Menu starts in 25 seconds.\n");
  gBS->Stall (25 * 1000 * 1000);
  return EFI_SUCCESS;
}
