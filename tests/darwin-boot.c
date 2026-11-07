// SPDX-License-Identifier: BSD-2-Clause-Patent
// Compile the complete production loader, parser and CPU state machine against
// real MdePkg headers. Only firmware/assembly boundaries are mocked.
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#undef NULL // MdePkg defines its own NULL after the host's standard headers.
#include "../UniXNU/drivers/DarwinBoot/DarwinBootPkg/Driver/DarwinBoot.c"
#include "darwin-mocks.h"

typedef struct { CONST CHAR8 *Name; CONST VOID *Data; UINTN Size; } TEST_PROPERTY;
typedef struct TEST_NODE TEST_NODE;
struct TEST_NODE {
  CONST CHAR8 *Name;
  CONST TEST_PROPERTY *Properties;
  UINTN PropertyCount;
  CONST TEST_NODE *CONST *Children;
  UINTN ChildCount;
};

STATIC UINTN Cases;

STATIC VOID Property (UINT8 *Data, UINTN *Offset, CONST CHAR8 *Name, CONST VOID *Value, UINTN Size) {
  UINTN Length = AsciiStrLen (Name);
  assert (Length < 32 && *Offset + 36 + Size + 4 < sizeof (Mock.Tree));
  ZeroMem (Data + *Offset, 36);
  CopyMem (Data + *Offset, Name, Length);
  WriteUnaligned32 ((UINT32 *)(Data + *Offset + 32), (UINT32)Size);
  *Offset += 36;
  if (Size != 0) { CopyMem (Data + *Offset, Value, Size); }
  *Offset = ALIGN_VALUE (*Offset + Size, 4);
}

STATIC VOID Node (UINT8 *Data, UINTN *Offset, CONST TEST_NODE *N) {
  UINTN I;
  WriteUnaligned32 ((UINT32 *)(Data + *Offset), (UINT32)N->PropertyCount + 1);
  WriteUnaligned32 ((UINT32 *)(Data + *Offset + 4), (UINT32)N->ChildCount);
  *Offset += 8;
  for (I = 0; I < N->PropertyCount; I++) {
    Property (Data, Offset, N->Properties[I].Name, N->Properties[I].Data, N->Properties[I].Size);
  }
  // Name last exercises the parser's path resolution independently of order.
  Property (Data, Offset, "name", N->Name, AsciiStrLen (N->Name) + 1);
  for (I = 0; I < N->ChildCount; I++) { Node (Data, Offset, N->Children[I]); }
}

STATIC UINTN Encode (UINT8 *Data, CONST TEST_NODE *N) {
  UINTN Offset = 0;
  ZeroMem (Data, sizeof (Mock.Tree));
  Node (Data, &Offset, N);
  return Offset;
}

STATIC BOOLEAN ParseTree (CONST UINT8 *Data, UINTN Size, DARWIN_BOOT_POLICY *Policy, UINT64 *Window) {
  UINTN Offset = 0, Ram = MAX_UINTN, Trust = MAX_UINTN, Addon = MAX_UINTN, RootPolicy = MAX_UINTN;
  BOOLEAN Packed = FALSE;
  DarwinBootPolicyInit (Policy, 0x80000000);
  *Window = 0;
  return AdtNode (Data, Size, &Offset, 0, 3, &Ram, &Trust, &Addon, &RootPolicy, Window, &Packed, Policy) && (Offset == Size);
}

STATIC VOID CheckBytes (CONST UINT8 *Data, UINTN Begin, UINTN End, UINT8 Byte) {
  for (; Begin < End; Begin++) { assert (Data[Begin] == Byte); }
}

STATIC VOID TestAbi (VOID) {
  DARWIN_BOOT_ARGS23 Values;
  UINT8 Buffer[1280];
  CHAR8 Command[1025];
  CHAR16 Wide[1025];
  UINTN I;

  ZeroMem (&Values, sizeof (Values));
  Values.Revision = 2; Values.Version = 2;
  Values.VirtualBase = 0xfffffe0006000000ULL;
  Values.PhysicalBase = 0xa0000000;
  Values.MemorySize = SIZE_1GB;
  Values.TopOfKernelData = 0xa298c000;
  for (I = 0; I < 6; I++) { Values.Video[I] = 0x1122334455667700ULL + I; }
  Values.MachineType = 0xabcdef01;
  Values.DeviceTree = Values.VirtualBase + 0x2988000;
  Values.DeviceTreeLength = 0x9988;
  Values.BootFlags = 0x8877665544332211ULL;
  Values.MemorySizeActual = 0x123456789abcdef0ULL;
  memset (Command, 'x', sizeof (Command));
  Command[607] = 0;
  memset (Buffer, 0xa5, sizeof (Buffer));
  assert (DarwinBootArgsWrite (Buffer, sizeof (Buffer), 22, &Values, Command));
  assert (U32 (Buffer) == 0x00020002 && U64 (Buffer + 8) == Values.VirtualBase);
  assert (U64 (Buffer + 0x10) == Values.PhysicalBase && U64 (Buffer + 0x18) == Values.MemorySize);
  assert (U64 (Buffer + 0x20) == Values.TopOfKernelData);
  for (I = 0; I < 6; I++) { assert (U64 (Buffer + 0x28 + I * 8) == Values.Video[I]); }
  assert (U32 (Buffer + 0x58) == Values.MachineType && U64 (Buffer + 0x60) == Values.DeviceTree);
  assert (U32 (Buffer + 0x68) == Values.DeviceTreeLength && Buffer[0x6c + 607] == 0);
  assert (U32 (Buffer + 0x2cc) == 0 && U64 (Buffer + 0x2d0) == Values.BootFlags);
  assert (U64 (Buffer + 0x2d8) == Values.MemorySizeActual);
  CheckBytes (Buffer, 736, sizeof (Buffer), 0xa5); // Includes Darwin 23's 0x478.
  Command[607] = 'x'; Command[608] = 0;
  memset (Buffer, 0xa5, sizeof (Buffer));
  assert (!DarwinBootArgsWrite (Buffer, sizeof (Buffer), 22, &Values, Command));
  CheckBytes (Buffer, 0, sizeof (Buffer), 0xa5);
  memset (Command, 'x', sizeof (Command)); Command[1023] = 0;
  assert (DarwinBootArgsWrite (Buffer, sizeof (Buffer), 23, &Values, Command));
  assert (U64 (Buffer + 0x470) == Values.BootFlags && U64 (Buffer + 0x478) == Values.MemorySizeActual);
  assert (Buffer[0x6c + 1023] == 0);
  CheckBytes (Buffer, 1152, sizeof (Buffer), 0xa5);
  Command[1023] = 'x'; Command[1024] = 0;
  assert (!DarwinBootArgsWrite (Buffer, sizeof (Buffer), 23, &Values, Command));
  assert (!DarwinBootArgsWrite (Buffer, 735, 22, &Values, NULL));
  assert (!DarwinBootArgsWrite (Buffer, sizeof (Buffer), 24, &Values, NULL));
  assert (DarwinBootArgsSize (22) == 736 && DarwinBootArgsSize (23) == 1152 && DarwinBootArgsSize (24) == 0);
  assert (DarwinBootCommandCapacity (22) == 608 && DarwinBootCommandCapacity (23) == 1024);
  for (I = 0; I < ARRAY_SIZE (Wide); I++) { Wide[I] = 'a'; }
  Wide[607] = 0;
  assert (DarwinBootCommandLine (Command, 608, Wide) && strlen (Command) == 607);
  Wide[607] = 'a'; Wide[608] = 0;
  memset (Command, 0x55, sizeof (Command));
  assert (!DarwinBootCommandLine (Command, 608, Wide));
  CheckBytes ((UINT8 *)Command, 0, sizeof (Command), 0x55);
  Wide[0] = 0x100;
  assert (!DarwinBootCommandLine (Command, 1024, Wide));
  CheckBytes ((UINT8 *)Command, 0, sizeof (Command), 0x55);
  assert (DarwinBootCommandLine (Command, 608, NULL) && Command[0] == 0);
  Cases += 16;
  puts ("PASS: actual Darwin 22/23 ABI bytes, shared fields, sentinel tails and non-truncating command limits");
}

STATIC VOID TestGranules (VOID) {
  UINTN I;
  DARWIN_CPU_FEATURES F = { DARWIN_CPU_READY, 0x80000100, 4, 0x101122, 0xc00800, 0 };
  for (I = 0; I < 16; I++) {
    assert (DarwinBootGranuleSupported ((UINT64)I << 28, 4096) == (I <= 1));
    assert (DarwinBootGranuleSupported ((UINT64)I << 20, 16384) == ((I == 1) || (I == 2)));
  }
  assert (!DarwinBootGranuleSupported (0x101122, 8192));
  assert (DarwinCpuFeaturesValid (&F, 0x100, 16384));
  F.Mpidr = 0x80000101; assert (!DarwinCpuFeaturesValid (&F, 0x100, 16384)); F.Mpidr = 0x80000100;
  F.CurrentEl = 8; assert (!DarwinCpuFeaturesValid (&F, 0x100, 16384)); F.CurrentEl = 4;
  for (I = 0; I < 3; I++) {
    UINT64 Bits[] = { 1, 4, 0x1000 };
    F.Sctlr |= Bits[I]; assert (!DarwinCpuFeaturesValid (&F, 0x100, 16384)); F.Sctlr &= ~Bits[I];
  }
  F.State = DARWIN_CPU_OFF_FAILED; assert (!DarwinCpuFeaturesValid (&F, 0x100, 16384));
  Cases += 39;
  puts ("PASS: all MMFR0 granule encodings, raw MPIDR affinity, EL and M/C/I checks");
}

STATIC VOID TestPolicyParser (VOID) {
  UINT32 Abi = 22, Page = 16384;
  UINT64 Cpu = 0x100, Memory = SIZE_1GB, Window;
  DARWIN_BOOT_POLICY Policy;
  UINT8 Bytes[8192], Value[16] = { 0 };
  TEST_PROPERTY Fields[] = {
    { "darwinboot-args-abi", &Abi, 4 }, { "darwinboot-page-size", &Page, 4 },
    { "darwinboot-boot-cpu", &Cpu, 8 }, { "darwinboot-memory-size", &Memory, 8 }
  };
  TEST_PROPERTY Method = { "method", "smc", 4 };
  TEST_NODE Chosen = { "chosen", Fields, ARRAY_SIZE (Fields), NULL, 0 };
  TEST_NODE Psci = { "psci", &Method, 1, NULL, 0 };
  CONST TEST_NODE *Children[] = { &Chosen, &Psci };
  TEST_NODE Root = { "device-tree", NULL, 0, Children, 2 };
  UINTN Size, I, P;

  Size = Encode (Bytes, &Root);
  assert (ParseTree (Bytes, Size, &Policy, &Window));
  assert (Policy.ArgsAbi == 22 && Policy.PageSize == 16384 && Policy.BootCpu == 0x100);
  assert (Policy.PsciMethod == DARWIN_PSCI_SMC && Window == SIZE_1GB);
  Method.Data = "hvc";
  Size = Encode (Bytes, &Root);
  assert (ParseTree (Bytes, Size, &Policy, &Window) && Policy.PsciMethod == DARWIN_PSCI_HVC);
  for (I = 0; I < Size; I++) { assert (!ParseTree (Bytes, I, &Policy, &Window)); Cases++; }
  Bytes[Size] = 0; assert (!ParseTree (Bytes, Size + 1, &Policy, &Window));
  for (P = 0; P < 4; P++) {
    UINTN Correct = (P < 2) ? 4 : 8;
    for (I = 0; I <= Correct + 1; I++) {
      if (I == Correct) { continue; }
      Fields[P].Data = Value; Fields[P].Size = I;
      Size = Encode (Bytes, &Root);
      assert (!ParseTree (Bytes, Size, &Policy, &Window)); Cases++;
    }
    Fields[P].Size = Correct;
    Fields[P].Data = (P == 0) ? (CONST VOID *)&Abi : (P == 1) ? (CONST VOID *)&Page : (P == 2) ? (CONST VOID *)&Cpu : (CONST VOID *)&Memory;
  }
  {
    UINT32 BadAbi[] = { 0, 21, 24, MAX_UINT32 };
    UINT32 BadPage[] = { 0, 8192, 65536, MAX_UINT32 };
    UINT64 BadCpu[] = { 0x80000100, 1ULL << 24, 1ULL << 40, MAX_UINT64 };
    for (I = 0; I < 4; I++) {
      Fields[0].Data = &BadAbi[I]; Size = Encode (Bytes, &Root); assert (!ParseTree (Bytes, Size, &Policy, &Window));
      Fields[0].Data = &Abi;
      Fields[1].Data = &BadPage[I]; Size = Encode (Bytes, &Root); assert (!ParseTree (Bytes, Size, &Policy, &Window));
      Fields[1].Data = &Page;
      Fields[2].Data = &BadCpu[I]; Size = Encode (Bytes, &Root); assert (!ParseTree (Bytes, Size, &Policy, &Window));
      Fields[2].Data = &Cpu; Cases += 3;
    }
  }
  for (P = 0; P < ARRAY_SIZE (Fields); P++) {
    TEST_PROPERTY Duplicate[] = { Fields[P], Fields[P] };
    Chosen.Properties = Duplicate; Chosen.PropertyCount = 2;
    Size = Encode (Bytes, &Root); assert (!ParseTree (Bytes, Size, &Policy, &Window)); Cases++;
  }
  Chosen.Properties = Fields; Chosen.PropertyCount = ARRAY_SIZE (Fields);
  Chosen.Name = "options"; Size = Encode (Bytes, &Root); assert (!ParseTree (Bytes, Size, &Policy, &Window)); Chosen.Name = "chosen";
  for (I = 0; I <= 5; I++) {
    if (I == 4) { continue; }
    Method.Data = Value; Method.Size = I;
    Size = Encode (Bytes, &Root); assert (!ParseTree (Bytes, Size, &Policy, &Window)); Cases++;
  }
  Method.Size = 4; Method.Data = "SMC";
  Size = Encode (Bytes, &Root); assert (!ParseTree (Bytes, Size, &Policy, &Window));
  Method.Data = "smc";
  {
    TEST_PROPERTY Duplicate[] = { Method, Method };
    Psci.Properties = Duplicate; Psci.PropertyCount = 2;
    Size = Encode (Bytes, &Root); assert (!ParseTree (Bytes, Size, &Policy, &Window));
    Psci.Properties = &Method; Psci.PropertyCount = 1;
  }
  Children[1] = &Chosen; Size = Encode (Bytes, &Root); assert (!ParseTree (Bytes, Size, &Policy, &Window));
  Children[0] = &Psci; Children[1] = &Psci; Size = Encode (Bytes, &Root); assert (!ParseTree (Bytes, Size, &Policy, &Window));
  Root.ChildCount = 0;
  Size = Encode (Bytes, &Root); assert (ParseTree (Bytes, Size, &Policy, &Window));
  assert (Policy.ArgsAbi == 23 && Policy.PageSize == 4096 && Policy.BootCpu == 0 && Policy.PsciMethod == 0 && Window == 0);
  Cases += 10;
  puts ("PASS: actual ADT parser defaults, typed lengths/values, misplaced/duplicate policy and nodes, all truncations");
}

STATIC VOID KernelFixture (UINT64 Base) {
  UINT8 *K = Mock.Kernel;
  ZeroMem (K, sizeof (Mock.Kernel));
  WriteUnaligned32 ((UINT32 *)K, 0xfeedfacf);
  WriteUnaligned32 ((UINT32 *)(K + 4), 0x100000c);
  WriteUnaligned32 ((UINT32 *)(K + 12), 2);
  WriteUnaligned32 ((UINT32 *)(K + 16), 2);
  WriteUnaligned32 ((UINT32 *)(K + 20), 360);
  WriteUnaligned32 ((UINT32 *)(K + 32), 0x19);
  WriteUnaligned32 ((UINT32 *)(K + 36), 72);
  WriteUnaligned64 ((UINT64 *)(K + 32 + 24), Base);
  WriteUnaligned64 ((UINT64 *)(K + 32 + 32), SIZE_16KB);
  WriteUnaligned64 ((UINT64 *)(K + 32 + 48), sizeof (Mock.Kernel));
  WriteUnaligned32 ((UINT32 *)(K + 32 + 56), 5);
  WriteUnaligned32 ((UINT32 *)(K + 32 + 60), 5);
  WriteUnaligned32 ((UINT32 *)(K + 104), 5);
  WriteUnaligned32 ((UINT32 *)(K + 108), 288);
  WriteUnaligned32 ((UINT32 *)(K + 112), 6);
  WriteUnaligned32 ((UINT32 *)(K + 116), 68);
  WriteUnaligned64 ((UINT64 *)(K + 104 + 272), Base + 0x800);
  Mock.KernelData = Mock.Kernel;
  Mock.KernelSize = sizeof (Mock.Kernel);
  assert (ParseKernel (Mock.KernelData, Mock.KernelSize, &Mock.ExpectedKernel) == EFI_SUCCESS);
  Mock.ExpectedVirtualBase = (Mock.ExpectedPage == 16384) ? (Base & ~0x01ffffffULL) : Base;
}

STATIC VOID KernelSpanFixture (UINT64 Base, UINT64 Span) {
  KernelFixture (Base);
  WriteUnaligned64 ((UINT64 *)(Mock.Kernel + 32 + 32), Span);
  assert (ParseKernel (Mock.Kernel, sizeof (Mock.Kernel), &Mock.ExpectedKernel) == EFI_SUCCESS);
}

STATIC VOID BootFixture (UINT32 Abi, UINT32 Page, UINT64 Cpu, UINT32 Method, UINT64 Memory, BOOLEAN Explicit) {
  TEST_PROPERTY Fields[] = {
    { "darwinboot-args-abi", &Abi, 4 }, { "darwinboot-page-size", &Page, 4 },
    { "darwinboot-boot-cpu", &Cpu, 8 }, { "darwinboot-memory-size", &Memory, 8 }
  };
  TEST_PROPERTY Conduit = { "method", (Method == DARWIN_PSCI_HVC) ? "hvc" : "smc", 4 };
  TEST_NODE Chosen = { "chosen", Fields, Explicit ? 4 : 0, NULL, 0 };
  TEST_NODE Psci = { "psci", &Conduit, 1, NULL, 0 };
  CONST TEST_NODE *Children[] = { &Chosen, &Psci };
  TEST_NODE Root = { "device-tree", NULL, 0, Children, (Method != 0) ? 2 : 1 };
  Mock.TreeSize = Encode (Mock.Tree, &Root);
  Mock.ExpectedAbi = Abi; Mock.ExpectedPage = Page; Mock.ExpectedCpu = Cpu;
  Mock.ExpectedMethod = Method; Mock.ExpectedWindow = Memory;
  KernelFixture (0xfffffe0007004000ULL);
}

STATIC EFI_STATUS RunBoot (VOID) {
  UNIXNU_LOADER_BOOT_REQUEST Request;
  ZeroMem (&Request, sizeof (Request));
  Request.Volume = (EFI_HANDLE)(UINTN)2;
  Request.KernelPath = L"\\kernel.macho";
  Request.BootArgs = Mock.Command;
  return Boot (&mLoader, &Request);
}

STATIC VOID ExpectFailure (EFI_STATUS Status) {
  assert (RunBoot () == Status);
  assert (Mock.ExitAttempts == 0 && Mock.Enters == 0 && Mock.PhysicalCommand != DARWIN_CPU_START);
  assert (Mock.PoolCount == 0 && Mock.WindowFrees == Mock.WindowAllocations);
  Cases++;
}

STATIC VOID ExpectEnter (VOID) {
  INTN Reason = setjmp (BootJump);
  if (Reason == 0) {
    EFI_STATUS Status = RunBoot ();
    fprintf (stderr, "unexpected boot refusal %#llx ABI=%u page=%u CPU=%#llx on=%llu affinity=%d\n",
             (unsigned long long)Status, Mock.ExpectedAbi, Mock.ExpectedPage,
             (unsigned long long)Mock.ExpectedCpu, (unsigned long long)Mock.OnCalls, mCpuHandoff.LastAffinity);
    assert (!"Boot returned on success");
  }
  assert (Reason == 1 && Mock.Enters == 1 && Mock.Exited);
  assert (Mock.ContextFrees == 0 && Mock.WindowFrees == 0);
  Cases++;
}

STATIC VOID ExpectPark (BOOLEAN Offline) {
  INTN Reason = setjmp (BootJump);
  if (Reason == 0) { RunBoot (); assert (!"Boot returned after ExitBootServices attempt"); }
  assert (Reason == 2 && Mock.Parks == 1 && Mock.Enters == 0);
  assert (Mock.PhysicalCommand == DARWIN_CPU_ABORT && Mock.SawOff == Offline);
  assert (mCpuHandoff.Launched == !Offline);
  assert (Mock.ContextFrees == 0 && Mock.WindowFrees == 0 && mCpuHandoff.Address != 0);
  assert (Unload (mImage) == EFI_ACCESS_DENIED);
  Cases++;
}

STATIC VOID GoldFixture (VOID) {
  ResetMock ();
  BootFixture (22, 16384, 0x100, DARWIN_PSCI_SMC, SIZE_1GB, TRUE);
}

STATIC VOID TestBootPaths (VOID) {
  UINTN I;
  ResetMock (); BootFixture (23, 4096, 0, 0, WINDOW_SIZE, FALSE); ExpectEnter ();
  ResetMock (); BootFixture (23, 4096, 0, 0, WINDOW_SIZE, TRUE);
  for (I = 0; I < 1023; I++) { Mock.Command[I] = 'a'; } ExpectEnter ();
  GoldFixture ();
  for (I = 0; I < 607; I++) { Mock.Command[I] = 'g'; } ExpectEnter ();
  assert (Mock.OriginParked && Mock.PhysicalCommand == DARWIN_CPU_START && !DarwinBootGranuleSupported (Mock.Mmfr0, 16384));
  ResetMock (); Mock.CurrentCpu = 0x80000100; Mock.Mmfr0 = 0x101122;
  BootFixture (22, 16384, 0x100, 0, WINDOW_SIZE, TRUE); ExpectEnter ();
  ResetMock (); BootFixture (22, 4096, 0x100, DARWIN_PSCI_HVC, WINDOW_SIZE, TRUE); ExpectEnter ();
  GoldFixture (); Mock.InvalidExits = 1; ExpectEnter (); assert (Mock.ExitAttempts == 2);

  GoldFixture (); for (I = 0; I < 608; I++) { Mock.Command[I] = 'x'; } ExpectFailure (EFI_BAD_BUFFER_SIZE);
  assert (Mock.OnCalls == 0 && Mock.WindowAllocations == 0);
  ResetMock (); BootFixture (23, 4096, 0, 0, WINDOW_SIZE, FALSE);
  for (I = 0; I < 1024; I++) { Mock.Command[I] = 'x'; } ExpectFailure (EFI_BAD_BUFFER_SIZE);
  ResetMock (); BootFixture (23, 4096, 0, 0, WINDOW_SIZE, FALSE); Mock.Command[0] = 0x100; ExpectFailure (EFI_BAD_BUFFER_SIZE);
  ResetMock (); BootFixture (24, 4096, 0, 0, WINDOW_SIZE, TRUE); ExpectFailure (EFI_COMPROMISED_DATA);
  ResetMock (); BootFixture (23, 8192, 0, 0, WINDOW_SIZE, TRUE); ExpectFailure (EFI_COMPROMISED_DATA);
  ResetMock (); BootFixture (23, 4096, 0x80000100, 0, WINDOW_SIZE, TRUE); ExpectFailure (EFI_COMPROMISED_DATA);
  ResetMock (); BootFixture (23, 4096, 0, 0, WINDOW_SIZE, FALSE); Mock.CurrentEl = 8; ExpectFailure (EFI_UNSUPPORTED);
  ResetMock (); BootFixture (23, 4096, 0, 0, WINDOW_SIZE, FALSE); Mock.Mmfr0 = 15ULL << 28; ExpectFailure (EFI_UNSUPPORTED);
  ResetMock (); BootFixture (22, 16384, 0, 0, WINDOW_SIZE, TRUE); ExpectFailure (EFI_UNSUPPORTED);
  ResetMock (); BootFixture (22, 16384, 0x100, 0, WINDOW_SIZE, TRUE); ExpectFailure (EFI_UNSUPPORTED);
  assert (Mock.AffinityCalls == 0 && Mock.WindowAllocations == 0);
  GoldFixture (); KernelFixture (0xfffffe0007005000ULL); ExpectFailure (EFI_LOAD_ERROR);
  GoldFixture (); WriteUnaligned32 ((UINT32 *)Mock.Kernel, 0); ExpectFailure (EFI_UNSUPPORTED);
  GoldFixture (); Mock.Tree[Mock.TreeSize++] = 0; ExpectFailure (EFI_COMPROMISED_DATA);
  GoldFixture (); Mock.Map[1].PhysicalStart += EFI_PAGE_SIZE; ExpectFailure (EFI_OUT_OF_RESOURCES);
  assert (Mock.WindowAllocations == 0);
  ResetMock (); BootFixture (23, 4096, 0, 0, SIZE_2GB, TRUE); ExpectFailure (EFI_OUT_OF_RESOURCES);

  for (I = 0; I < 3; I++) {
    INT32 States[] = { DARWIN_PSCI_AFFINITY_ON, DARWIN_PSCI_AFFINITY_PENDING, -1 };
    GoldFixture (); Mock.InitialAffinity = States[I]; ExpectFailure (EFI_NOT_READY);
    assert (Mock.ContextAllocations == 0 && Mock.OnCalls == 0 && Mock.Events == 0);
  }
  GoldFixture (); Mock.BadTranslation = mImageBase; ExpectFailure (EFI_UNSUPPORTED); assert (Mock.OnCalls == 0);
  GoldFixture (); Mock.BadTranslation = mImageBase; Mock.TranslationFault = TRUE; ExpectFailure (EFI_UNSUPPORTED); assert (Mock.OnCalls == 0);
  GoldFixture (); mImageBase = 0; mImageSize = 1024; ExpectFailure (EFI_UNSUPPORTED); assert (Mock.AffinityCalls == 0);
  GoldFixture (); Mock.BadContextTranslation = TRUE; ExpectFailure (EFI_UNSUPPORTED);
  assert (Mock.ContextFrees == 1 && Mock.OnCalls == 0 && !Mock.SawOff);
  GoldFixture (); Mock.LineSize = 4096; ExpectFailure (EFI_UNSUPPORTED);
  GoldFixture (); Mock.ContextAllocateStatus = EFI_OUT_OF_RESOURCES; ExpectFailure (EFI_OUT_OF_RESOURCES);
  assert (Mock.ContextFrees == 0 && mCpuHandoff.Address == 0);

  GoldFixture (); Mock.OnReturn = -3; Mock.OffAfter = 1; ExpectFailure (EFI_DEVICE_ERROR);
  assert (Mock.SawOff && Mock.ContextFrees == 1 && Mock.CommandCleans == 1);
  GoldFixture (); Mock.OnReturn = -3; Mock.OnDespiteError = TRUE; ExpectFailure (EFI_DEVICE_ERROR);
  assert (Mock.AbortPolls == 3 && Mock.ContextFrees == 1);
  GoldFixture (); Mock.NeverReady = TRUE; ExpectFailure (EFI_TIMEOUT);
  assert (Mock.Polls == 2000 && Mock.ContextFrees == 1 && Mock.SawOff);
  for (I = 0; I < 7; I++) {
    GoldFixture ();
    switch (I) {
      case 0: Mock.TargetFeatures.Mpidr = 0x80000101; break;
      case 1: Mock.TargetFeatures.CurrentEl = 8; break;
      case 2: Mock.TargetFeatures.Mmfr0 = 0x1122; break;
      case 3: Mock.TargetFeatures.Sctlr |= 1; break;
      case 4: Mock.TargetFeatures.Sctlr |= 4; break;
      case 5: Mock.TargetFeatures.Sctlr |= 0x1000; break;
      case 6: Mock.TargetFeatures.State = DARWIN_CPU_OFF_FAILED; break;
    }
    ExpectFailure ((I == 6) ? EFI_DEVICE_ERROR : EFI_UNSUPPORTED);
    assert (Mock.ContextFrees == 1 && Mock.SawOff && mCpuHandoff.Address == 0);
  }
  GoldFixture (); Mock.FailStall = TRUE; ExpectFailure (EFI_DEVICE_ERROR); assert (Mock.ContextFrees == 1);
  GoldFixture (); Mock.FailMapAfterOn = TRUE; ExpectFailure (EFI_DEVICE_ERROR); assert (Mock.ContextFrees == 1);
  GoldFixture (); Mock.FailPoolAfterOn = TRUE; ExpectFailure (EFI_OUT_OF_RESOURCES); assert (Mock.ContextFrees == 1);
  GoldFixture (); Mock.TargetFeatures.Mmfr0 = 0x1122; Mock.NeverOff = TRUE; ExpectFailure (EFI_TIMEOUT);
  assert (Mock.ContextFrees == 0 && mCpuHandoff.Address != 0 && Mock.PhysicalCommand == DARWIN_CPU_ABORT);
  assert (Mock.ActiveContext->Report.State == DARWIN_CPU_OFF_FAILED && Mock.ActiveContext->Report.OffReturn == (UINT64)(INT64)-3);
  assert (Unload (mImage) == EFI_ACCESS_DENIED && Mock.Uninstalls == 0);
  assert (RunBoot () == EFI_NOT_READY && Mock.OnCalls == 1 && Mock.WindowAllocations == 1);
  GoldFixture (); Mock.OnReturn = -3; Mock.FailContextFree = TRUE; ExpectFailure (EFI_DEVICE_ERROR);
  assert (Mock.SawOff && mCpuHandoff.Address != 0 && Unload (mImage) == EFI_ACCESS_DENIED);
  GoldFixture (); Mock.ExitStatus = EFI_DEVICE_ERROR; ExpectPark (TRUE);
  GoldFixture (); Mock.InvalidExits = 3; ExpectPark (TRUE); assert (Mock.ExitAttempts == 3);
  GoldFixture (); Mock.InvalidExits = 1; Mock.FailMapAfterExit = TRUE; ExpectPark (TRUE); assert (Mock.ExitAttempts == 1);
  GoldFixture (); Mock.ExitStatus = EFI_DEVICE_ERROR; Mock.NeverOff = TRUE; ExpectPark (FALSE);
  assert (Mock.ActiveContext->Report.State == DARWIN_CPU_OFF_FAILED && Mock.ActiveContext->Report.OffReturn == (UINT64)(INT64)-3);
  ResetMock (); assert (Unload (mImage) == EFI_SUCCESS && Mock.Uninstalls == 1);
  puts ("PASS: complete loader direct/Gold/HVC success, 1 GiB conventional allocation, prepare/abort errors, OFF-only frees, unload/retry guards and EBS retries/parking");
}

STATIC VOID TestMeasuredWindow (VOID) {
  KERNEL K;
  EFI_PHYSICAL_ADDRESS Address;
  ResetMock ();
  Mock.Map[0].PhysicalStart = 0xa0000000;
  Mock.Map[0].NumberOfPages = EFI_SIZE_TO_PAGES (SIZE_256MB);
  Mock.Map[1].PhysicalStart = 0xb0000000;
  Mock.Map[1].NumberOfPages = EFI_SIZE_TO_PAGES (0xfeac0000ULL - 0xb0000000);
  Mock.MapCount = 2;
  BootFixture (22, 16384, 0x100, DARWIN_PSCI_SMC, SIZE_1GB, TRUE);
  K = Mock.ExpectedKernel;
  assert (PrepareKernelBank (&K, 16384, SIZE_1GB) == EFI_SUCCESS && K.Base == 0xfffffe0006000000ULL);
  assert (AllocateWindow (&K, 16384, SIZE_1GB, &Address) == EFI_SUCCESS && Address == 0xa0000000);
  assert (Address + SIZE_1GB <= 0xfeac0000 && Mock.WindowPages == 262144);
  Cases++;
  // The probe's a0004000 was an aligned analysis candidate, not descriptor start.
  K = Mock.ExpectedKernel;
  assert (AllocateWindow (&K, 4096, SIZE_1GB, &Address) == EFI_SUCCESS && Address == 0xa0004000);
  assert (Address + SIZE_1GB <= 0xfeac0000 && Mock.WindowPages == 262144);
  assert (Mock.PoolCount == 0);
  Cases++;
  Mock.Map[0].PhysicalStart = 0xa0004000;
  Mock.Map[0].NumberOfPages = EFI_SIZE_TO_PAGES (0xb0000000ULL - 0xa0004000);
  K = Mock.ExpectedKernel;
  assert (PrepareKernelBank (&K, 16384, SIZE_1GB) == EFI_SUCCESS);
  assert (AllocateWindow (&K, 16384, SIZE_1GB, &Address) == EFI_SUCCESS && Address == 0xa2000000);
  assert (Address + SIZE_1GB <= 0xfeac0000 && Mock.PoolCount == 0);
  Cases++;
  puts ("PASS: real descriptor a0000000..feac0000 bank and 4 KiB offsets; descriptor a0004000 forces next 32 MiB bank a2000000");
}

STATIC VOID TestBankBounds (VOID) {
  UINT64 Base, Address, Cursor, End;
  KERNEL K;

  assert (DarwinBootstrapBlockSize (4096) == 0x200000 && DarwinBootstrapBlockSize (16384) == 0x2000000);
  assert (!DarwinBootstrapPhaseMatches (0xfffffe0007004000ULL, 0xa0004000, 16384));
  assert (BootstrapTranslation (0xfffffe0007004000ULL, 0xfffffe0007004000ULL, 0xa0004000, 16384) == 0xa1004000);
  assert (DarwinBootstrapPhaseMatches (0xfffffe0007004000ULL, 0xa0004000, 4096));
  assert (DarwinBootstrapPhaseMatches (0xfffffe0007004000ULL, 0xa1004000, 16384));
  Base = 0x1122;
  assert (!DarwinBootstrapBankBase (0xfffffe0007005000ULL, 0xfffffe0007009000ULL, 16384, SIZE_1GB, &Base) && Base == 0x1122);
  assert (!DarwinBootstrapBankBase (0xfffffe0007004000ULL, 0xfffffe0007004000ULL, 16384, SIZE_1GB, &Base));
  assert (!DarwinBootstrapBankBase (0xfffffe0007004000ULL, 0xfffffe0007008000ULL, 8192, SIZE_1GB, &Base));
  Address = 0x1122;
  assert (!DarwinBootstrapCandidate (MAX_UINT64 - 4095, MAX_UINT64, 0xfffffe0006000000ULL, 16384, SIZE_1GB, &Address) && Address == 0x1122);
  assert (!DarwinBootstrapCandidate (0xa0000000, 0xfeac0000, 0xfffffe0007004000ULL, 16384, SIZE_1GB, &Address));
  Cursor = 0;
  assert (DarwinBankReserve (0xa0001000, SIZE_64KB, &Cursor, SIZE_16KB, SIZE_4KB, &Address));
  assert (Address == 0xa0004000 && Cursor == 0x4000); // Absolute alignment for legacy 4 KiB.
  Cursor = SIZE_32MB - SIZE_16KB;
  assert (DarwinBankReserve (0xa0000000, SIZE_32MB, &Cursor, SIZE_16KB, SIZE_16KB, &Address) && Cursor == SIZE_32MB);
  assert (!DarwinBankReserve (0xa0000000, SIZE_32MB, &Cursor, 1, 1, &Address) && Cursor == SIZE_32MB);
  Cursor = 0; Address = 0x1122;
  assert (!DarwinBankReserve (MAX_UINT64 - 15, 32, &Cursor, 16, 8, &Address) && Cursor == 0 && Address == 0x1122);
  assert (!DarwinBankReserve (0xa0000000, SIZE_32MB, &Cursor, 3, 1, &Address));
  assert (!DarwinConventionalRangeEnd (0xa0000000, MAX_UINT64, &End));
  assert (!DarwinConventionalRangeEnd (MAX_UINT64 - 4095, 1, &End));
  Cases += 17;

  ResetMock (); BootFixture (22, 16384, 0x100, DARWIN_PSCI_SMC, WINDOW_SIZE, TRUE);
  KernelSpanFixture (0xfffffe0007ffc000ULL, WINDOW_SIZE - SIZE_2MB);
  K = Mock.ExpectedKernel;
  assert (PrepareKernelBank (&K, 16384, WINDOW_SIZE) == EFI_LOAD_ERROR && K.Base == Mock.ExpectedKernel.Base);
  assert (PrepareKernelBank (&K, 16384, SIZE_1GB) == EFI_SUCCESS); // Prefix fits the larger existing budget.
  ExpectFailure (EFI_LOAD_ERROR);
  assert (Mock.WindowAttempts == 0 && Mock.OnCalls == 0);

  ResetMock (); BootFixture (22, 16384, 0x100, DARWIN_PSCI_SMC, WINDOW_SIZE, TRUE);
  KernelSpanFixture (0xfffffe0007004000ULL, WINDOW_SIZE - SIZE_2MB - 0x1004000);
  ExpectFailure (EFI_BAD_BUFFER_SIZE); // Kernel ends at admitted limit: no argument/ADT space.
  assert (Mock.WindowAllocations == 1 && Mock.OnCalls == 0);
  KernelSpanFixture (0xfffffe0007004000ULL, WINDOW_SIZE - SIZE_2MB - 0x1004000 + SIZE_16KB);
  ExpectFailure (EFI_LOAD_ERROR); // One granule more fails before another allocation.
  assert (Mock.WindowAllocations == 1);

  ResetMock (); BootFixture (23, 4096, 0, 0, WINDOW_SIZE, TRUE);
  KernelSpanFixture (0xfffffe0007004000ULL, WINDOW_SIZE - SIZE_2MB);
  ExpectFailure (EFI_BAD_BUFFER_SIZE); assert (Mock.WindowAllocations == 1);
  ResetMock (); BootFixture (23, 4096, 0, 0, SIZE_1GB, TRUE);
  KernelFixture (0xffffffffe1004000ULL); ExpectFailure (EFI_LOAD_ERROR); assert (Mock.WindowAttempts == 0);
  ResetMock (); BootFixture (22, 16384, 0x100, DARWIN_PSCI_SMC, SIZE_1GB, TRUE);
  KernelFixture (0xffffffffe1004000ULL); ExpectFailure (EFI_LOAD_ERROR); assert (Mock.WindowAttempts == 0);

  GoldFixture ();
  Mock.Map[0].NumberOfPages = MAX_UINT64;
  Mock.Map[1].Type = EfiReservedMemoryType;
  ExpectFailure (EFI_OUT_OF_RESOURCES); assert (Mock.WindowAttempts == 0);
  GoldFixture (); Mock.WindowAllocateStatus = EFI_OUT_OF_RESOURCES;
  ExpectFailure (EFI_OUT_OF_RESOURCES); assert (Mock.ContextAllocations == 0);
  GoldFixture ();
  Mock.Map[0].PhysicalStart = 0xa0004000;
  Mock.Map[0].NumberOfPages = EFI_SIZE_TO_PAGES (SIZE_1GB);
  Mock.Map[1].Type = EfiReservedMemoryType;
  Mock.MapCount = 2;
  ExpectFailure (EFI_OUT_OF_RESOURCES); assert (Mock.WindowAttempts == 0);
  puts ("PASS: bank-prefix/span admission, tight payload reserve, VA/PA arithmetic overflow, malformed map and allocation failures");
}

STATIC VOID TestNativeBank (CONST UINT8 *Data, UINTN Size, UINT64 PhysicalBase) {
  ResetMockAt (0xa0000000);
  BootFixture (22, 16384, 0x100, DARWIN_PSCI_SMC, SIZE_1GB, TRUE);
  Mock.KernelData = Data;
  Mock.KernelSize = Size;
  assert (ParseKernel (Data, Size, &Mock.ExpectedKernel) == EFI_SUCCESS);
  assert (Mock.ExpectedKernel.Base == 0xfffffe0007004000ULL && Mock.ExpectedKernel.Entry == 0xfffffe00074ba488ULL);
  assert (Mock.ExpectedKernel.End == 0xfffffe0008984000ULL);
  Mock.ExpectedVirtualBase = 0xfffffe0006000000ULL;
  if (PhysicalBase != 0xa0000000) {
    assert (PhysicalBase == 0xa2000000);
    Mock.Map[0].PhysicalStart += SIZE_16KB;
    Mock.Map[0].NumberOfPages -= EFI_SIZE_TO_PAGES (SIZE_16KB);
  }
  // Anonymous RAM starts zero. Poison it so a missing bank-prefix/BSS clear
  // cannot pass, and compare every loaded segment against the actual Mach-O.
  memset ((VOID *)(UINTN)PhysicalBase, 0xa5, (size_t)(Mock.ExpectedKernel.End - Mock.ExpectedVirtualBase + SIZE_64KB));
  ExpectEnter ();
  assert (Mock.Window == PhysicalBase);
  assert (Mock.EnteredEntry == PhysicalBase + 0x014ba488 && Mock.EnteredArgs == PhysicalBase + 0x02984000);
  assert (Mock.EnteredTree == PhysicalBase + 0x02988000);
  assert (U64 ((VOID *)(UINTN)(Mock.EnteredArgs + 8)) == 0xfffffe0006000000ULL);
  assert (U64 ((VOID *)(UINTN)(Mock.EnteredArgs + 0x10)) == PhysicalBase);
  assert (U32 ((VOID *)(UINTN)(PhysicalBase + 0x01004000)) == 0xfeedfacf);
  printf ("PASS: native bank va=fffffe0006000000 pa=%llx header=%llx entry=%llx args=%llx tree=%llx; all copied segments/BSS/prefix and L2 phases\n",
          (unsigned long long)PhysicalBase, (unsigned long long)(PhysicalBase + 0x01004000),
          (unsigned long long)Mock.EnteredEntry, (unsigned long long)Mock.EnteredArgs, (unsigned long long)Mock.EnteredTree);
}

int main (int argc, char **argv) {
  UINT8 *Native = NULL;
  UINTN NativeSize = 0;
  setvbuf (stdout, NULL, _IOLBF, 0);
  TestAbi ();
  TestGranules ();
  TestPolicyParser ();
  TestBootPaths ();
  TestMeasuredWindow ();
  TestBankBounds ();
  if (argc == 2) {
    FILE *File = fopen (argv[1], "rb");
    assert (File != NULL && fseek (File, 0, SEEK_END) == 0);
    long Bytes = ftell (File);
    assert (Bytes > 0 && fseek (File, 0, SEEK_SET) == 0);
    NativeSize = (UINTN)Bytes;
    Native = malloc ((size_t)NativeSize);
    assert (Native != NULL && fread (Native, 1, (size_t)NativeSize, File) == (size_t)NativeSize);
    assert (fclose (File) == 0);
    TestNativeBank (Native, NativeSize, 0xa0000000);
    TestNativeBank (Native, NativeSize, 0xa2000000);
  } else { assert (argc == 1); }
  // Release host fixture resources; these are not firmware cleanup calls.
  for (UINTN I = 0; I < Mock.PoolCount; I++) { free (Mock.Pools[I]); }
  assert (munmap (Mock.Memory, (size_t)Mock.MemoryBytes) == 0);
  assert (munmap (Mock.ContextMemory, sizeof (DARWIN_CPU_CONTEXT)) == 0);
  free (Native);
  printf ("PASS: %llu ABI/policy/state/integration checks against production sources\n", (unsigned long long)Cases);
  return 0;
}
