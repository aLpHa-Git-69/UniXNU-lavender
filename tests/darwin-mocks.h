// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host boundaries for the actual DarwinBoot.c and CpuHandoff.c. Cache-visible
// reports/commands are separate from the target's physical copies.
#ifndef DARWIN_TEST_MOCKS_H_
#define DARWIN_TEST_MOCKS_H_

typedef struct {
  EFI_MEMORY_DESCRIPTOR Map[4];
  UINTN MapCount;
  VOID *Memory;
  UINTN MemoryBytes;
  VOID *ContextMemory;
  VOID *Pools[64];
  UINTN PoolCount;
  UINT64 Window;
  UINTN WindowPages;
  UINTN WindowAllocations;
  UINTN WindowFrees;
  UINTN ContextAllocations;
  UINTN ContextFrees;
  UINTN OnCalls;
  UINTN AffinityCalls;
  UINTN Polls;
  UINTN Stalls;
  UINTN AbortPolls;
  UINTN ReadyAfter;
  UINTN OffAfter;
  UINTN ExitAttempts;
  UINTN InvalidExits;
  UINTN MapCalls;
  UINTN Uninstalls;
  UINTN Events;
  UINTN ImageCleans;
  UINTN ContextCleans;
  UINTN CommandCleans;
  UINTN Enters;
  UINTN Parks;
  UINT64 CurrentCpu;
  UINT64 CurrentEl;
  UINT64 Mmfr0;
  UINT64 Sctlr;
  UINT64 LineSize;
  UINT64 BadTranslation;
  UINT64 Key;
  UINT32 ExpectedAbi;
  UINT32 ExpectedPage;
  UINT64 ExpectedWindow;
  UINT64 ExpectedCpu;
  UINT32 ExpectedMethod;
  INT32 InitialAffinity;
  INT32 OnReturn;
  INT32 RunningAffinity;
  EFI_STATUS ContextAllocateStatus;
  EFI_STATUS ExitStatus;
  BOOLEAN OnDespiteError;
  BOOLEAN NeverReady;
  BOOLEAN NeverOff;
  BOOLEAN SawOff;
  BOOLEAN BadContextTranslation;
  BOOLEAN TranslationFault;
  BOOLEAN FailMapAfterOn;
  BOOLEAN FailMapAfterExit;
  BOOLEAN FailPoolAfterOn;
  BOOLEAN FailContextFree;
  BOOLEAN FailStall;
  BOOLEAN Exited;
  BOOLEAN OriginParked;
  DARWIN_CPU_CONTEXT *ActiveContext;
  DARWIN_CPU_FEATURES TargetFeatures;
  DARWIN_CPU_FEATURES PhysicalReport;
  UINT64 PhysicalCommand;
  UINT64 PhysicalArgs;
  UINT64 PhysicalEntry;
  UINT8 Kernel[4096];
  UINT8 Tree[8192];
  UINTN TreeSize;
  CHAR16 Command[1100];
} DARWIN_MOCK;

STATIC DARWIN_MOCK Mock;
STATIC jmp_buf BootJump;
STATIC EFI_BOOT_SERVICES MockServices;
STATIC EFI_SYSTEM_TABLE MockSystem;
STATIC EFI_LOADED_IMAGE_PROTOCOL MockLoaded;
EFI_BOOT_SERVICES *gBS = &MockServices;
EFI_SYSTEM_TABLE *gST = &MockSystem;
EFI_GUID gEfiFileInfoGuid;
EFI_GUID gEfiLoadedImageProtocolGuid;
EFI_GUID gEfiGraphicsOutputProtocolGuid;
EFI_GUID gEfiSimpleFileSystemProtocolGuid;
EFI_GUID gUniXNULoaderProtocolGuid = UNIXNU_LOADER_PROTOCOL_GUID;

VOID *EFIAPI CopyMem (VOID *To, CONST VOID *From, UINTN Size) { return memmove (To, From, (size_t)Size); }
VOID *EFIAPI ZeroMem (VOID *To, UINTN Size) { return memset (To, 0, (size_t)Size); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN Size) { return memcmp (A, B, (size_t)Size); }
VOID *EFIAPI ScanMem8 (CONST VOID *P, UINTN Size, UINT8 Value) { return memchr (P, Value, (size_t)Size); }
UINT32 EFIAPI ReadUnaligned32 (CONST VOID *P) { UINT32 V; memcpy (&V, P, sizeof (V)); return V; }
UINT64 EFIAPI ReadUnaligned64 (CONST VOID *P) { UINT64 V; memcpy (&V, P, sizeof (V)); return V; }
UINT32 EFIAPI WriteUnaligned32 (VOID *P, UINT32 V) { memcpy (P, &V, sizeof (V)); return V; }
UINT64 EFIAPI WriteUnaligned64 (VOID *P, UINT64 V) { memcpy (P, &V, sizeof (V)); return V; }
UINTN EFIAPI AsciiStrLen (CONST CHAR8 *P) { return (UINTN)strlen (P); }
INTN EFIAPI AsciiStrCmp (CONST CHAR8 *A, CONST CHAR8 *B) { return strcmp (A, B); }
UINTN EFIAPI StrLen (CONST CHAR16 *P) { UINTN N = 0; while (P[N] != 0) { N++; } return N; }
UINTN EFIAPI StrSize (CONST CHAR16 *P) { return (StrLen (P) + 1) * sizeof (*P); }
RETURN_STATUS EFIAPI StrCatS (CHAR16 *To, UINTN Capacity, CONST CHAR16 *From) {
  UINTN N = StrLen (To), M = StrLen (From);
  if (N + M >= Capacity) { return RETURN_BUFFER_TOO_SMALL; }
  CopyMem (To + N, From, (M + 1) * sizeof (*From));
  return RETURN_SUCCESS;
}

VOID *EFIAPI AllocatePool (UINTN Size) {
  VOID *P;
  assert (Mock.ExitAttempts == 0);
  if (Mock.FailPoolAfterOn && (Mock.OnCalls != 0)) { return NULL; }
  assert (Mock.PoolCount < ARRAY_SIZE (Mock.Pools));
  P = malloc ((size_t)Size);
  assert (P != NULL);
  Mock.Pools[Mock.PoolCount++] = P;
  return P;
}

VOID EFIAPI FreePool (VOID *P) {
  UINTN I;
  assert (Mock.ExitAttempts == 0);
  for (I = 0; I < Mock.PoolCount; I++) {
    if (Mock.Pools[I] == P) {
      free (P);
      Mock.Pools[I] = Mock.Pools[--Mock.PoolCount];
      return;
    }
  }
  assert (!"unowned pool free");
}

UINTN EFIAPI Print (CONST CHAR16 *Format, ...) {
  assert (Format != NULL && Mock.ExitAttempts == 0);
  return 0;
}

UINTN EFIAPI UnicodeSPrint (CHAR16 *To, UINTN Size, CONST CHAR16 *Format, ...) {
  assert (Mock.ExitAttempts == 0 && Format != NULL);
  if (Size >= sizeof (*To)) { To[0] = 0; }
  return 0;
}

UINT64 EFIAPI DarwinCurrentEl (VOID) { return Mock.CurrentEl; }
UINT64 EFIAPI DarwinCurrentMpidr (VOID) { return Mock.CurrentCpu; }
UINT64 EFIAPI DarwinMmfr0 (VOID) { return Mock.Mmfr0; }
UINT64 EFIAPI DarwinSctlr (VOID) { return Mock.Sctlr; }
UINT64 EFIAPI DarwinTcr (VOID) { return 0; }
UINT64 EFIAPI DarwinPfr0 (VOID) { return 0; }
UINT64 EFIAPI DarwinCacheLineSize (VOID) { return Mock.LineSize; }
UINT64 EFIAPI DarwinTranslate (UINT64 Address) {
  if ((Address == Mock.BadTranslation) ||
      (Mock.BadContextTranslation && (Address == (UINT64)(UINTN)Mock.ContextMemory))) {
    return Mock.TranslationFault ? (Address | 1) : (Address ^ EFI_PAGE_SIZE);
  }
  return Address;
}

__attribute__((aligned(16), noinline))
VOID EFIAPI DarwinColdEntry (UINT64 Context) { (VOID)Context; }
__asm__ (".globl DarwinColdEntryEnd\n.set DarwinColdEntryEnd, DarwinColdEntry + 4");

VOID EFIAPI DarwinCacheClean (UINT64 Address, UINT64 Size) {
  if ((Address == mImageBase) && (Size == mImageSize)) {
    assert (Mock.OnCalls == 0);
    Mock.ImageCleans++;
  } else if ((Address == (UINT64)(UINTN)Mock.ContextMemory) && (Size == sizeof (DARWIN_CPU_CONTEXT))) {
    DARWIN_CPU_CONTEXT *C = Mock.ContextMemory;
    assert (Mock.OnCalls == 0 && C->Command == DARWIN_CPU_WAIT && C->Report.State == 0);
    Mock.PhysicalArgs = C->Args;
    Mock.PhysicalEntry = C->Entry;
    Mock.PhysicalCommand = C->Command;
    Mock.ContextCleans++;
  } else {
    assert (Address == (UINT64)(UINTN)Mock.ContextMemory + DARWIN_CPU_COMMAND && Size == DARWIN_CPU_PAGE_SIZE);
    assert (Mock.OnCalls != 0);
    Mock.PhysicalCommand = ((DARWIN_CPU_CONTEXT *)Mock.ContextMemory)->Command;
    assert (Mock.PhysicalCommand == DARWIN_CPU_ABORT);
    Mock.CommandCleans++;
  }
}

VOID EFIAPI DarwinCacheInvalidate (UINT64 Address, UINT64 Size) {
  assert (Mock.OnCalls != 0);
  assert (Address == (UINT64)(UINTN)Mock.ContextMemory + DARWIN_CPU_REPORT && Size == sizeof (DARWIN_CPU_FEATURES));
  if (Mock.PhysicalCommand == DARWIN_CPU_WAIT) {
    assert (Mock.ExitAttempts == 0);
    Mock.Polls++;
    if (!Mock.NeverReady && (Mock.Polls >= Mock.ReadyAfter)) { Mock.PhysicalReport = Mock.TargetFeatures; }
  }
  CopyMem ((VOID *)&Mock.ActiveContext->Report, &Mock.PhysicalReport, sizeof (Mock.PhysicalReport));
}

VOID EFIAPI DarwinSendEvent (VOID) {
  assert (Mock.PhysicalCommand == DARWIN_CPU_ABORT);
  Mock.Events++;
}

INT32 EFIAPI DarwinPsciCall (UINT64 Method, UINT64 Function, UINT64 A, UINT64 B, UINT64 C) {
  assert (Method == Mock.ExpectedMethod);
  assert (A == Mock.ExpectedCpu);
  if (Function == DARWIN_PSCI_CPU_ON) {
    assert (Mock.ExitAttempts == 0);
    assert (Mock.ImageCleans == 1 && Mock.ContextCleans == 1);
    assert (B == (UINT64)(UINTN)DarwinColdEntry && C == (UINT64)(UINTN)Mock.ContextMemory);
    assert (Mock.PhysicalCommand == DARWIN_CPU_WAIT && mCpuHandoff.Launched);
    Mock.OnCalls++;
    Mock.ActiveContext = (DARWIN_CPU_CONTEXT *)(UINTN)C;
    return Mock.OnReturn;
  }
  assert (Function == DARWIN_PSCI_AFFINITY_INFO && B == 0 && C == 0);
  Mock.AffinityCalls++;
  if (Mock.OnCalls == 0) { return Mock.InitialAffinity; }
  if (Mock.PhysicalCommand == DARWIN_CPU_ABORT) {
    assert (Mock.Events != 0);
    Mock.AbortPolls++;
    if ((Mock.OnReturn != 0) && !Mock.OnDespiteError) { Mock.SawOff = TRUE; return DARWIN_PSCI_AFFINITY_OFF; }
    if (!Mock.NeverOff && (Mock.AbortPolls >= Mock.OffAfter)) { Mock.SawOff = TRUE; return DARWIN_PSCI_AFFINITY_OFF; }
    if (Mock.NeverOff) {
      Mock.PhysicalReport.State = DARWIN_CPU_OFF_FAILED;
      Mock.PhysicalReport.OffReturn = (UINT64)(INT64)-3;
    }
    return DARWIN_PSCI_AFFINITY_ON;
  }
  if ((Mock.OnReturn != 0) && !Mock.OnDespiteError) { Mock.SawOff = TRUE; return DARWIN_PSCI_AFFINITY_OFF; }
  return Mock.RunningAffinity;
}

STATIC BOOLEAN RangeFree (UINT64 Address, UINT64 Bytes) {
  UINT64 Cursor = Address, End = Address + Bytes;
  UINTN I;
  BOOLEAN Found;
  if (End < Address) { return FALSE; }
  while (Cursor < End) {
    Found = FALSE;
    for (I = 0; I < Mock.MapCount; I++) {
      UINT64 Start = Mock.Map[I].PhysicalStart;
      UINT64 Stop = Start + EFI_PAGES_TO_SIZE (Mock.Map[I].NumberOfPages);
      if ((Mock.Map[I].Type == EfiConventionalMemory) && (Cursor >= Start) && (Cursor < Stop)) {
        Cursor = MIN (End, Stop);
        Found = TRUE;
        break;
      }
    }
    if (!Found) { return FALSE; }
  }
  return TRUE;
}

STATIC EFI_STATUS EFIAPI MockAllocatePages (EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, UINTN Pages, EFI_PHYSICAL_ADDRESS *Address) {
  assert (Mock.ExitAttempts == 0);
  if (Type == AllocateAddress) {
    assert (MemoryType == EfiLoaderCode && RangeFree (*Address, EFI_PAGES_TO_SIZE (Pages)));
    Mock.Window = *Address;
    Mock.WindowPages = Pages;
    Mock.WindowAllocations++;
    return EFI_SUCCESS;
  }
  assert (Type == AllocateAnyPages && MemoryType == EfiReservedMemoryType && Pages == DARWIN_CPU_CONTEXT_PAGES);
  Mock.ContextAllocations++;
  if (EFI_ERROR (Mock.ContextAllocateStatus)) { return Mock.ContextAllocateStatus; }
  *Address = (UINT64)(UINTN)Mock.ContextMemory;
  assert (RangeFree (*Address, EFI_PAGES_TO_SIZE (Pages)));
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI MockFreePages (EFI_PHYSICAL_ADDRESS Address, UINTN Pages) {
  assert (Mock.ExitAttempts == 0);
  if (Address == (UINT64)(UINTN)Mock.ContextMemory) {
    assert (Pages == DARWIN_CPU_CONTEXT_PAGES && ((Mock.OnCalls == 0) || Mock.SawOff));
    if (Mock.FailContextFree) { return EFI_DEVICE_ERROR; }
    Mock.ContextFrees++;
  } else {
    assert (Address == Mock.Window && Pages == Mock.WindowPages && Mock.PhysicalCommand != DARWIN_CPU_START);
    Mock.WindowFrees++;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI MockStall (UINTN Microseconds) {
  assert (Mock.ExitAttempts == 0 && Microseconds == 1000);
  Mock.Stalls++;
  return Mock.FailStall ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI MockGetMap (UINTN *Size, EFI_MEMORY_DESCRIPTOR *Map, UINTN *Key, UINTN *Stride, UINT32 *Version) {
  UINTN I, Needed;
  Mock.MapCalls++;
  if ((Mock.FailMapAfterOn && (Mock.OnCalls != 0)) ||
      (Mock.FailMapAfterExit && (Mock.ExitAttempts != 0))) { return EFI_DEVICE_ERROR; }
  *Stride = sizeof (EFI_MEMORY_DESCRIPTOR) + 16;
  *Version = EFI_MEMORY_DESCRIPTOR_VERSION;
  Needed = Mock.MapCount * *Stride;
  if ((Map == NULL) || (*Size < Needed)) { *Size = Needed; return EFI_BUFFER_TOO_SMALL; }
  ZeroMem (Map, Needed);
  for (I = 0; I < Mock.MapCount; I++) { CopyMem ((UINT8 *)Map + I * *Stride, &Mock.Map[I], sizeof (Mock.Map[I])); }
  *Size = Needed;
  *Key = ++Mock.Key;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI MockExit (EFI_HANDLE Image, UINTN Key) {
  assert (Image == mImage && Key == Mock.Key && Mock.PhysicalCommand == DARWIN_CPU_WAIT);
  if (Mock.OnCalls != 0) {
    assert (DarwinCpuFeaturesValid (&Mock.ActiveContext->Report, Mock.ExpectedCpu, Mock.ExpectedPage));
  }
  Mock.ExitAttempts++;
  if (Mock.ExitAttempts <= Mock.InvalidExits) { return EFI_INVALID_PARAMETER; }
  if (EFI_ERROR (Mock.ExitStatus)) { return Mock.ExitStatus; }
  Mock.Exited = TRUE;
  return EFI_SUCCESS;
}

VOID EFIAPI DarwinEnter (UINT64 Args, UINT64 Entry, UINT64 Memory, UINT64 Size, UINT64 Video, UINT64 VideoBytes, UINT64 Context) {
  UINT64 TreeAddress;
  UINTN ActualOffset = (Mock.ExpectedAbi == 22) ? 0x2d8 : 0x478;
  assert (Mock.Exited && Video == 0 && VideoBytes == 0);
  assert (Size == Mock.ExpectedWindow && Memory == Mock.Window && Entry == Memory + 0x800);
  assert ((Args & (Mock.ExpectedPage - 1)) == 0);
  assert (U64 ((VOID *)(UINTN)(Args + 0x18)) == Size && U64 ((VOID *)(UINTN)(Args + ActualOffset)) == Size);
  assert (U64 ((VOID *)(UINTN)(Args + 0x08)) == 0xfffffe0007004000ULL);
  assert (U64 ((VOID *)(UINTN)(Args + 0x10)) == Memory);
  TreeAddress = U64 ((VOID *)(UINTN)(Args + 0x60)) - 0xfffffe0007004000ULL + Memory;
  assert (TreeAddress == Args + Mock.ExpectedPage && U32 ((VOID *)(UINTN)(Args + 0x68)) == Mock.TreeSize);
  assert (CompareMem ((VOID *)(UINTN)TreeAddress, Mock.Tree, Mock.TreeSize) == 0);
  assert (AsciiStrLen ((CHAR8 *)(UINTN)(Args + 0x6c)) == StrLen (Mock.Command));
  Mock.Enters++;
  if (Mock.ExpectedCpu == (Mock.CurrentCpu & DARWIN_MPIDR_AFFINITY_MASK)) {
    assert (Context == 0 && Mock.OnCalls == 0 && Mock.ContextAllocations == 0);
  } else {
    assert (Context == (UINT64)(UINTN)Mock.ContextMemory && Mock.OnCalls == 1);
    assert (Mock.PhysicalArgs == Args && Mock.PhysicalEntry == Entry);
    // Model the assembly boundary: physical START after MMU/cache shutdown.
    Mock.Sctlr &= ~0x1005ULL;
    Mock.ActiveContext->Command = DARWIN_CPU_START;
    Mock.PhysicalCommand = DARWIN_CPU_START;
    Mock.OriginParked = TRUE;
  }
  longjmp (BootJump, 1);
}

VOID EFIAPI CpuDeadLoop (VOID) { Mock.Parks++; longjmp (BootJump, 2); }

typedef struct { EFI_FILE_PROTOCOL Protocol; CONST UINT8 *Data; UINTN Size; } MOCK_FILE;
STATIC EFI_SIMPLE_FILE_SYSTEM_PROTOCOL MockFs;
STATIC EFI_FILE_PROTOCOL MockRoot;
STATIC MOCK_FILE MockFile;

STATIC BOOLEAN EndsWith (CONST CHAR16 *Path, CONST CHAR8 *Suffix) {
  UINTN N = StrLen (Path), M = AsciiStrLen (Suffix), I;
  if (M > N) { return FALSE; }
  for (I = 0; I < M; I++) { if (Path[N - M + I] != (UINT8)Suffix[I]) { return FALSE; } }
  return TRUE;
}

STATIC EFI_STATUS EFIAPI MockClose (EFI_FILE_PROTOCOL *File) { (VOID)File; return EFI_SUCCESS; }
STATIC EFI_STATUS EFIAPI MockRead (EFI_FILE_PROTOCOL *File, UINTN *Size, VOID *Data) {
  MOCK_FILE *F = (MOCK_FILE *)File;
  assert (*Size == F->Size);
  CopyMem (Data, F->Data, *Size);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI MockInfo (EFI_FILE_PROTOCOL *File, EFI_GUID *Guid, UINTN *Size, VOID *Data) {
  MOCK_FILE *F = (MOCK_FILE *)File;
  UINTN Needed = sizeof (EFI_FILE_INFO) + 32;
  assert (Guid == &gEfiFileInfoGuid);
  if ((Data == NULL) || (*Size < Needed)) { *Size = Needed; return EFI_BUFFER_TOO_SMALL; }
  ZeroMem (Data, Needed);
  ((EFI_FILE_INFO *)Data)->FileSize = F->Size;
  *Size = Needed;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI MockOpen (EFI_FILE_PROTOCOL *Root, EFI_FILE_PROTOCOL **File, CHAR16 *Path, UINT64 Mode, UINT64 Attributes) {
  assert (Root == &MockRoot && Mode == EFI_FILE_MODE_READ && Attributes == 0);
  if (EndsWith (Path, ".adt")) { MockFile.Data = Mock.Tree; MockFile.Size = Mock.TreeSize; }
  else if (EndsWith (Path, ".macho")) { MockFile.Data = Mock.Kernel; MockFile.Size = sizeof (Mock.Kernel); }
  else { return EFI_NOT_FOUND; }
  *File = &MockFile.Protocol;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI MockOpenVolume (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs, EFI_FILE_PROTOCOL **Root) {
  assert (Fs == &MockFs);
  *Root = &MockRoot;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI MockHandle (EFI_HANDLE Handle, EFI_GUID *Guid, VOID **Interface) {
  (VOID)Handle;
  if (Guid == &gEfiSimpleFileSystemProtocolGuid) { *Interface = &MockFs; return EFI_SUCCESS; }
  if (Guid == &gEfiLoadedImageProtocolGuid) { *Interface = &MockLoaded; return EFI_SUCCESS; }
  return EFI_NOT_FOUND;
}

STATIC EFI_STATUS EFIAPI MockLocate (EFI_GUID *Guid, VOID *Registration, VOID **Interface) {
  (VOID)Guid; (VOID)Registration; (VOID)Interface;
  return EFI_NOT_FOUND;
}

STATIC EFI_STATUS EFIAPI MockInstall (EFI_HANDLE *Handle, ...) { assert (Handle != NULL); return EFI_SUCCESS; }
STATIC EFI_STATUS EFIAPI MockUninstall (EFI_HANDLE Handle, ...) { (VOID)Handle; Mock.Uninstalls++; return EFI_SUCCESS; }

STATIC VOID ResetMock (VOID) {
  UINT64 Start;
  UINTN I;

  for (I = 0; I < Mock.PoolCount; I++) { free (Mock.Pools[I]); }
  if (Mock.Memory != NULL) { assert (munmap (Mock.Memory, (size_t)Mock.MemoryBytes) == 0); }
  if (Mock.ContextMemory != NULL) { assert (munmap (Mock.ContextMemory, sizeof (DARWIN_CPU_CONTEXT)) == 0); }
  ZeroMem (&Mock, sizeof (Mock));
  ZeroMem (&mCpuHandoff, sizeof (mCpuHandoff));
  Mock.MemoryBytes = SIZE_1GB + BLOCK_SIZE + SIZE_64KB;
  Mock.Memory = mmap (NULL, (size_t)Mock.MemoryBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  assert (Mock.Memory != MAP_FAILED);
  // mmap avoids Android's tagged malloc pointers in a physical-address fixture.
  Mock.ContextMemory = mmap (NULL, sizeof (DARWIN_CPU_CONTEXT), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  assert (Mock.ContextMemory != MAP_FAILED);
  Start = ALIGN_VALUE ((UINT64)(UINTN)Mock.Memory, BLOCK_SIZE);
  Mock.Map[0].Type = EfiConventionalMemory;
  Mock.Map[0].PhysicalStart = Start;
  Mock.Map[0].NumberOfPages = EFI_SIZE_TO_PAGES (SIZE_128MB);
  Mock.Map[1].Type = EfiConventionalMemory;
  Mock.Map[1].PhysicalStart = Start + SIZE_128MB;
  Mock.Map[1].NumberOfPages = EFI_SIZE_TO_PAGES (SIZE_1GB + SIZE_64KB - SIZE_128MB);
  Mock.Map[2].Type = EfiConventionalMemory;
  Mock.Map[2].PhysicalStart = (UINT64)(UINTN)Mock.ContextMemory;
  Mock.Map[2].NumberOfPages = DARWIN_CPU_CONTEXT_PAGES;
  Mock.MapCount = 3;
  Mock.CurrentCpu = 0x80000000;
  Mock.CurrentEl = 4;
  Mock.Mmfr0 = 0x1122; // Silver: 4 KiB supported, 16 KiB unsupported.
  Mock.Sctlr = 0x1005;
  Mock.LineSize = 64;
  Mock.ReadyAfter = 2;
  Mock.OffAfter = 3;
  Mock.InitialAffinity = DARWIN_PSCI_AFFINITY_OFF;
  Mock.RunningAffinity = DARWIN_PSCI_AFFINITY_ON;
  Mock.ExpectedAbi = 23;
  Mock.ExpectedPage = 4096;
  Mock.ExpectedWindow = WINDOW_SIZE;
  Mock.TargetFeatures.State = DARWIN_CPU_READY;
  Mock.TargetFeatures.Mpidr = 0x80000100;
  Mock.TargetFeatures.CurrentEl = 4;
  Mock.TargetFeatures.Mmfr0 = 0x101122;
  Mock.TargetFeatures.Sctlr = 0xc00800;
  MockServices.AllocatePages = MockAllocatePages;
  MockServices.FreePages = MockFreePages;
  MockServices.GetMemoryMap = MockGetMap;
  MockServices.Stall = MockStall;
  MockServices.ExitBootServices = MockExit;
  MockServices.HandleProtocol = MockHandle;
  MockServices.LocateProtocol = MockLocate;
  MockServices.InstallMultipleProtocolInterfaces = MockInstall;
  MockServices.UninstallMultipleProtocolInterfaces = MockUninstall;
  MockFs.OpenVolume = MockOpenVolume;
  MockRoot.Open = MockOpen;
  MockRoot.Close = MockClose;
  MockFile.Protocol.Close = MockClose;
  MockFile.Protocol.Read = MockRead;
  MockFile.Protocol.GetInfo = MockInfo;
  MockLoaded.ImageBase = (VOID *)(UINTN)((UINT64)(UINTN)DarwinColdEntry & ~(UINT64)EFI_PAGE_MASK);
  MockLoaded.ImageSize = EFI_PAGE_SIZE;
  assert (DarwinBootEntry ((EFI_HANDLE)(UINTN)1, &MockSystem) == EFI_SUCCESS);
  assert (MockLoaded.Unload == Unload && mLoader.Revision == UNIXNU_LOADER_REVISION);
}

#endif
