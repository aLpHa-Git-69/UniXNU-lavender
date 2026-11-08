/* Exercise the actual initrd driver's publication, device-path ABI and reads.
 * Compile both with and without WINDOWS_BUILD using MdePkg's AArch64 headers.
 * SPDX-License-Identifier: BSD-2-Clause
 */
#define MDEPKG_NDEBUG
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#undef NULL
#include "../UniXNU/package/UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.c"

EFI_BOOT_SERVICES *gBS;
EFI_GUID gEfiDevicePathProtocolGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
EFI_GUID gEfiBlockIoProtocolGuid = EFI_BLOCK_IO_PROTOCOL_GUID;
static unsigned installs;
static UINT64 start_cell, end_cell;
static INT32 start_length = 8, end_length = 8;
static BOOLEAN chosen_present = TRUE;
static UINT8 disk[1024];

#define CHECK(value) do { if (!(value)) { \
  fprintf(stderr, "initrd driver check failed at line %d: %s\n", __LINE__, #value); \
  return 1; } } while (0)

UINT32 EFIAPI ReadUnaligned32(const VOID *p) { UINT32 v; memcpy(&v, p, 4); return v; }
UINT64 EFIAPI ReadUnaligned64(const VOID *p) { UINT64 v; memcpy(&v, p, 8); return v; }
UINT64 EFIAPI WriteUnaligned64(VOID *p, UINT64 v) { memcpy(p, &v, 8); return v; }
VOID *EFIAPI CopyMem(VOID *destination, const VOID *source, UINTN bytes) {
  return memcpy(destination, source, bytes);
}
UINT32 EFIAPI Fdt32ToCpu(UINT32 v) { return __builtin_bswap32(v); }
UINT64 EFIAPI Fdt64ToCpu(UINT64 v) { return __builtin_bswap64(v); }
INT32 EFIAPI FdtCheckHeader(const VOID *fdt) { (void)fdt; return 0; }
INT32 EFIAPI FdtPathOffset(const VOID *fdt, const CHAR8 *name) {
  (void)fdt;
  return chosen_present && strcmp(name, "/chosen") == 0 ? 1 : -1;
}
const VOID *EFIAPI FdtGetProp(const VOID *fdt, INT32 node, const CHAR8 *name, INT32 *length) {
  (void)fdt; (void)node;
  if (strcmp(name, "linux,initrd-start") == 0) { *length = start_length; return &start_cell; }
  if (strcmp(name, "linux,initrd-end") == 0) { *length = end_length; return &end_cell; }
  *length = -1;
  return NULL;
}
VOID *EFIAPI GetFirstGuidHob(const EFI_GUID *guid) { (void)guid; return NULL; }

static EFI_STATUS EFIAPI install(EFI_HANDLE *handle, ...) {
  va_list args;
  const EFI_GUID *guid;
  va_start(args, handle);
  for (unsigned i = 0; (guid = va_arg(args, const EFI_GUID *)) != NULL; i++) {
    (void)guid;
    if (va_arg(args, VOID *) == NULL || i > 2) { va_end(args); return EFI_INVALID_PARAMETER; }
  }
  va_end(args);
  *handle = (EFI_HANDLE)(UINTN)1;
  installs++;
  return EFI_SUCCESS;
}

int main(void) {
  EFI_BOOT_SERVICES services = {0};
  UINT8 buffer[512];
  services.InstallMultipleProtocolInterfaces = install;
  gBS = &services;

  /* A real 32-bit Lavender window, followed by a 64-bit host pointer. */
  start_length = end_length = 4;
  start_cell = __builtin_bswap32(0xc0000000U);
  end_cell = __builtin_bswap32(0xc0000800U);
  PublishInitrd(NULL);
  CHECK(installs == 1 && mMedia.LastBlock == 3);
  CHECK(mMedia.ReadOnly && mMedia.BlockSize == 512 && !mMedia.RemovableMedia);
#ifdef WINDOWS_BUILD
  CHECK(sizeof(MEDIA_RAM_DISK_DEVICE_PATH) == 38 && sizeof(mDevicePath) == 42);
  CHECK(mDevicePath.RamDisk.Header.Type == 4 && mDevicePath.RamDisk.Header.SubType == 9);
  CHECK(mDevicePath.RamDisk.Header.Length[0] == 38 && mDevicePath.RamDisk.Header.Length[1] == 0);
  CHECK(ReadUnaligned64((UINT64 *)mDevicePath.RamDisk.StartingAddr) == 0xc0000000ULL);
  CHECK(ReadUnaligned64((UINT64 *)mDevicePath.RamDisk.EndingAddr) == 0xc00007ffULL);
  CHECK(mDevicePath.RamDisk.TypeGuid.Data1 == 0x77ab535a &&
        mDevicePath.RamDisk.TypeGuid.Data2 == 0x45fc &&
        mDevicePath.RamDisk.TypeGuid.Data3 == 0x624b &&
        memcmp(mDevicePath.RamDisk.TypeGuid.Data4, "\x55\x60\xf7\xb2\x81\xd1\xf9\x6e", 8) == 0);
  CHECK(mDevicePath.RamDisk.Instance == 0);
#else
  CHECK(sizeof(mDevicePath) == 24 && mDevicePath.Vendor.Header.Type == 1);
  CHECK(mDevicePath.Vendor.Guid.Data1 == 0xa9c6827c);
#endif
  CHECK(mDevicePath.End.Type == 0x7f && mDevicePath.End.SubType == 0xff);

  for (unsigned i = 0; i < sizeof(disk); i++) { disk[i] = (UINT8)(i * 7 + i / 512); }
  start_length = end_length = 8;
  start_cell = __builtin_bswap64((UINT64)(UINTN)disk);
  end_cell = __builtin_bswap64((UINT64)(UINTN)disk + sizeof(disk));
  PublishInitrd(NULL);
  CHECK(installs == 2 && mMedia.LastBlock == 1);
#ifdef WINDOWS_BUILD
  CHECK(ReadUnaligned64((UINT64 *)mDevicePath.RamDisk.StartingAddr) == (UINT64)(UINTN)disk);
  CHECK(ReadUnaligned64((UINT64 *)mDevicePath.RamDisk.EndingAddr) == (UINT64)(UINTN)disk + 1023);
#endif
  CHECK(RamBlockRead(&mBlockIo, 1, 1, sizeof(buffer), buffer) == EFI_SUCCESS);
  CHECK(memcmp(buffer, disk + 512, sizeof(buffer)) == 0);
  CHECK(RamBlockRead(&mBlockIo, 2, 0, sizeof(buffer), buffer) == EFI_MEDIA_CHANGED);
  CHECK(RamBlockRead(&mBlockIo, 1, 0, 513, buffer) == EFI_BAD_BUFFER_SIZE);
  CHECK(RamBlockRead(&mBlockIo, 1, 2, sizeof(buffer), buffer) == EFI_INVALID_PARAMETER);
  CHECK(RamBlockRead(&mBlockIo, 1, 1, 1024, buffer) == EFI_INVALID_PARAMETER);
  CHECK(RamBlockRead(&mBlockIo, 1, 0, sizeof(buffer), NULL) == EFI_INVALID_PARAMETER);
  CHECK(RamBlockWrite(&mBlockIo, 1, 0, sizeof(buffer), buffer) == EFI_WRITE_PROTECTED);
  CHECK(RamBlockFlush(&mBlockIo) == EFI_SUCCESS && RamBlockReset(&mBlockIo, FALSE) == EFI_SUCCESS);

  start_length = 3;
  PublishInitrd(NULL);
  CHECK(installs == 2);
  start_length = 8;
  end_cell = start_cell;
  PublishInitrd(NULL);
  CHECK(installs == 2);
  chosen_present = FALSE;
  PublishInitrd(NULL);
  CHECK(installs == 2);
#ifdef WINDOWS_BUILD
  puts("actual initrd driver: Windows 38-byte RAM node, 32/64-bit spans, exact reads and rejection cases PASS");
#else
  puts("actual initrd driver: legacy vendor node, 32/64-bit spans, exact reads and rejection cases PASS");
#endif
  return 0;
}
