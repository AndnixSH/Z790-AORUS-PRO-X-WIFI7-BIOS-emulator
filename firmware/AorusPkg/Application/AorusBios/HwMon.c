/** @file
  The emulated board's hardware: CPU/memory description from the (virtual)
  machine, plus simulated sensors for the Super I/O hardware monitor that
  QEMU does not have (temperatures, fan speeds, voltage rails).

  SPDX-License-Identifier: MIT
**/

#include "AorusBios.h"

HW_STATE  gHw;

STATIC UINT32  mRand = 0x2545F491;

STATIC
UINT32
Rand (
  VOID
  )
{
  mRand ^= mRand << 13;
  mRand ^= mRand >> 17;
  mRand ^= mRand << 5;
  return mRand;
}

STATIC
INT32
Jitter (
  INT32  Range
  )
{
  return (INT32)(Rand () % (UINT32)(2 * Range + 1)) - Range;
}

typedef struct {
  CONST CHAR16    *Model;
  UINT8           PRatio;   // all-core turbo, x100 MHz
  UINT8           ERatio;
} CPU_TABLE;

STATIC CONST CPU_TABLE  mCpus[] = {
  { L"14900KS", 59, 45 }, { L"14900K", 57, 44 }, { L"14900", 54, 42 },
  { L"14700K",  55, 43 }, { L"14700",  53, 41 }, { L"14600K", 53, 40 },
  { L"13900KS", 57, 43 }, { L"13900K", 55, 43 }, { L"13900", 54, 41 },
  { L"13700K",  53, 42 }, { L"13600K", 51, 39 }, { L"12900K", 49, 37 },
};

VOID
HwInit (
  VOID
  )
{
  UINT32                 Regs[12];
  UINT32                 Eax;
  UINTN                  Index;
  CHAR8                  Brand[49];
  CHAR8                  *P;
  UINTN                  MapSize;
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *D;
  UINTN                  Key;
  UINTN                  DescSize;
  UINT32                 DescVer;
  UINT64                 Bytes;
  UINT32                 Crc;

  ZeroMem (&gHw, sizeof (gHw));

  //
  // CPU name and signature: whatever QEMU presents (run.sh passes the
  // i9-14900K identity by default, or the host CPU with --host-cpu).
  //
  AsmCpuid (0x80000000, &Eax, NULL, NULL, NULL);
  ZeroMem (Brand, sizeof (Brand));
  if (Eax >= 0x80000004) {
    for (Index = 0; Index < 3; Index++) {
      AsmCpuid (0x80000002 + (UINT32)Index, &Regs[Index * 4], &Regs[Index * 4 + 1], &Regs[Index * 4 + 2], &Regs[Index * 4 + 3]);
    }

    CopyMem (Brand, Regs, 48);
  }

  P = Brand;
  while (*P == ' ') {
    P++;
  }

  AsciiStrToUnicodeStrS (P, gHw.CpuBrand, ARRAY_SIZE (gHw.CpuBrand));
  AsmCpuid (1, &gHw.CpuSignature, NULL, NULL, NULL);

  gHw.PCoreRatio = 50;
  gHw.ECoreRatio = 39;
  for (Index = 0; Index < ARRAY_SIZE (mCpus); Index++) {
    if (StrStr (gHw.CpuBrand, mCpus[Index].Model) != NULL) {
      gHw.PCoreRatio = mCpus[Index].PRatio;
      gHw.ECoreRatio = mCpus[Index].ERatio;
      break;
    }
  }

  //
  // Installed memory: everything in the memory map that is RAM.
  //
  MapSize = 0;
  Map     = NULL;
  Bytes   = 0;
  if (gBS->GetMemoryMap (&MapSize, Map, &Key, &DescSize, &DescVer) == EFI_BUFFER_TOO_SMALL) {
    MapSize += 4 * EFI_PAGE_SIZE;
    Map      = AllocatePool (MapSize);
    if ((Map != NULL) && !EFI_ERROR (gBS->GetMemoryMap (&MapSize, Map, &Key, &DescSize, &DescVer))) {
      for (D = Map; (UINT8 *)D < (UINT8 *)Map + MapSize; D = NEXT_MEMORY_DESCRIPTOR (D, DescSize)) {
        switch (D->Type) {
          case EfiReservedMemoryType:
          case EfiMemoryMappedIO:
          case EfiMemoryMappedIOPortSpace:
          case EfiUnusableMemory:
            break;
          default:
            Bytes += EFI_PAGES_TO_SIZE (D->NumberOfPages);
            break;
        }
      }
    }

    if (Map != NULL) {
      FreePool (Map);
    }
  }

  // Round up to whole GiB like a DIMM population would be.
  gHw.MemSizeMb = (UINT32)(((Bytes + SIZE_1GB - 1) / SIZE_1GB) * 1024);

  gHw.BclkKhz10   = 10004;
  gHw.CpuTempDeci = 340;
  gHw.SysTempDeci = 300;
  gHw.PchTempDeci = 430;
  gHw.VrmTempDeci = 380;
  gHw.VcoreMv     = 945;
  gHw.CpuFanRpm   = 1036;
  gHw.SysFanRpm   = 812;
  gHw.PumpRpm     = 0;

  // A GIGABYTE OUI with a board-unique tail.
  Crc = 0;
  gBS->CalculateCrc32 (gHw.CpuBrand, sizeof (gHw.CpuBrand), &Crc);
  gHw.Mac[0] = 0x74;
  gHw.Mac[1] = 0x56;
  gHw.Mac[2] = 0x3C;
  gHw.Mac[3] = (UINT8)Crc;
  gHw.Mac[4] = (UINT8)(Crc >> 8);
  gHw.Mac[5] = (UINT8)(Crc >> 16);
  mRand     ^= Crc | 1;

  HwTick ();
}

//
// Called about once per second while the setup UI is shown.
//
VOID
HwTick (
  VOID
  )
{
  UINT64  V;

  gHw.Tick++;

  V = SdbSpecialValue (SDB_SPEC_BCLK, 0);
  gHw.BclkKhz10 = (V != 0 ? (UINT32)V : 10000) + 2 + Jitter (2);

  V = SdbSpecialValue (SDB_SPEC_P_RATIO, 0);
  if ((V != 0) && (V < 120)) {
    gHw.PCoreRatio = (UINT32)V;
  }

  V = SdbSpecialValue (SDB_SPEC_E_RATIO, 0);
  if ((V != 0) && (V < 120)) {
    gHw.ECoreRatio = (UINT32)V;
  }

  //
  // Memory speed: JEDEC DDR5-4800 unless XMP or a manual multiplier is set
  // (the emulated kit carries a DDR5-6000 XMP profile).
  //
  V = SdbSpecialValue (SDB_SPEC_MEM_MULTIPLIER, 0);
  if ((V >= 8) && (V <= 120)) {
    gHw.MemMts = (UINT32)V * 100;
  } else if (SdbSpecialValue (SDB_SPEC_XMP, 0) != 0) {
    gHw.MemMts = 6000;
  } else {
    gHw.MemMts = 4800;
  }

  V            = SdbSpecialValue (SDB_SPEC_VCORE, 0);
  gHw.VcoreMv  = (V != 0 && V < 2000) ? (UINT32)V + Jitter (4) : 940 + Jitter (6);
  gHw.CpuTempDeci = (UINT32)MAX (280, MIN (420, (INT32)gHw.CpuTempDeci + Jitter (6) + (gHw.CpuTempDeci < 350 ? 2 : -2)));
  gHw.SysTempDeci = 300 + (UINT32)Jitter (3) + 3;
  gHw.PchTempDeci = 430 + (UINT32)(Jitter (5) + 5);
  gHw.VrmTempDeci = 380 + (UINT32)(Jitter (4) + 4);
  gHw.CpuFanRpm   = 1036 + (UINT32)(Jitter (12) + 12) - 12;
  gHw.SysFanRpm   = 812 + (UINT32)(Jitter (10) + 10) - 10;
}

VOID
HwFormatMv (
  UINT32  Mv,
  CHAR16  *Buf,
  UINTN   BufSize
  )
{
  UnicodeSPrint (Buf, BufSize, L"%d.%03d V", Mv / 1000, Mv % 1000);
}

VOID
HwDynText (
  UINT8   Dyn,
  CHAR16  *Buf,
  UINTN   BufSize
  )
{
  UINT32  Mhz100;

  Buf[0] = 0;
  switch (Dyn) {
    case SDB_DYN_MODEL:
      StrCpyS (Buf, BufSize / sizeof (CHAR16), gSdb.Info->Model);
      break;
    case SDB_DYN_BIOS_VERSION:
      StrCpyS (Buf, BufSize / sizeof (CHAR16), gSdb.Info->BiosVersion);
      break;
    case SDB_DYN_BIOS_DATE:
      StrCpyS (Buf, BufSize / sizeof (CHAR16), gSdb.Info->BiosDate);
      break;
    case SDB_DYN_BIOS_ID:
      StrCpyS (Buf, BufSize / sizeof (CHAR16), gSdb.Info->BiosId);
      break;
    case SDB_DYN_CPU_TYPE:
      StrCpyS (Buf, BufSize / sizeof (CHAR16), gHw.CpuBrand);
      break;
    case SDB_DYN_CPU_ID:
      UnicodeSPrint (Buf, BufSize, L"%08X", gHw.CpuSignature);
      break;
    case SDB_DYN_CPU_SPEED:
      Mhz100 = gHw.PCoreRatio * gHw.BclkKhz10;
      UnicodeSPrint (
        Buf,
        BufSize,
        L"%d.%02dMHz|%d.%02d",
        Mhz100 / 100,
        Mhz100 % 100,
        (gHw.ECoreRatio * gHw.BclkKhz10) / 100,
        (gHw.ECoreRatio * gHw.BclkKhz10) % 100
        );
      break;
    case SDB_DYN_CPU_CLOCK:
      UnicodeSPrint (Buf, BufSize, L"%d.%02dMHz", gHw.BclkKhz10 / 100, gHw.BclkKhz10 % 100);
      break;
    case SDB_DYN_MEM_SIZE:
      UnicodeSPrint (Buf, BufSize, L"%dMB", gHw.MemSizeMb);
      break;
    case SDB_DYN_LAN_MAC:
      UnicodeSPrint (
        Buf,
        BufSize,
        L"%02X%02X%02X%02X%02X%02X",
        gHw.Mac[0],
        gHw.Mac[1],
        gHw.Mac[2],
        gHw.Mac[3],
        gHw.Mac[4],
        gHw.Mac[5]
        );
      break;
    case SDB_DYN_HW_CASE_OPEN:
      StrCpyS (Buf, BufSize / sizeof (CHAR16), SdbNamed (SDB_STR_NO));
      break;
    case SDB_DYN_HW_VCORE:
      HwFormatMv (gHw.VcoreMv, Buf, BufSize);
      break;
    case SDB_DYN_HW_VCCIN_AUX:
      HwFormatMv (1794 + Jitter (3), Buf, BufSize);
      break;
    case SDB_DYN_HW_VCCSA:
      HwFormatMv (836 + Jitter (2), Buf, BufSize);
      break;
    case SDB_DYN_HW_VDD2:
      HwFormatMv (gHw.MemMts > 4800 ? 1350 + Jitter (3) : 1133 + Jitter (3), Buf, BufSize);
      break;
    case SDB_DYN_HW_PCH18:
      HwFormatMv (1859 + Jitter (3), Buf, BufSize);
      break;
    case SDB_DYN_HW_3V3:
      HwFormatMv (3324 + Jitter (8), Buf, BufSize);
      break;
    case SDB_DYN_HW_5V:
      HwFormatMv (5055 + Jitter (8), Buf, BufSize);
      break;
    case SDB_DYN_HW_PCH082:
      HwFormatMv (847 + Jitter (2), Buf, BufSize);
      break;
    case SDB_DYN_HW_12V:
      HwFormatMv (12060 + Jitter (24), Buf, BufSize);
      break;
    case SDB_DYN_HW_VAXG:
      HwFormatMv (48 + Jitter (2), Buf, BufSize);
      break;
    default:
      break;
  }
}
