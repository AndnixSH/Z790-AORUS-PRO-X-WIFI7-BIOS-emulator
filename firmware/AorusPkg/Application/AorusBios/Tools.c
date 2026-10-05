/** @file
  Q-Flash (BIOS update utility) and Smart Fan 6.

  Q-Flash really browses the attached FAT drives and checks the image you
  pick, but the "flash" is simulated: there is no SPI chip to write.

  SPDX-License-Identifier: MIT
**/

#include "Ui.h"

STATIC
VOID
DarkScreen (
  CONST CHAR16  *Title
  )
{
  INT32  I;

  GfxGradientV (0, 0, SCREEN_W, SCREEN_H, RGB (0x26, 0x26, 0x26), RGB (0x0A, 0x0A, 0x0A));
  for (I = -SCREEN_H; I < SCREEN_W; I += 60) {
    GfxLine (I, SCREEN_H, I + SCREEN_H / 2, 0, RGB (0x40, 0x40, 0x40), 50);
  }

  GfxDrawImage (DataImage (IMG_GUI (0)), 46, 24);
  FontDraw (250, 34, Title, C_ORANGE, 320);
  GfxGradientH (0, 86, SCREEN_W, 2, C_ORANGE, RGB (0x30, 0x30, 0x30));
}

//
// ---------------------------------------------------------------- Q-Flash
//
typedef struct {
  EFI_FILE_PROTOCOL  *Root;
  CHAR16             Name[64];
  UINT64             Size;
} QF_FILE;

STATIC
UINTN
ListFiles (
  QF_FILE  *Files,
  UINTN    Max
  )
{
  EFI_HANDLE                       *Handles;
  UINTN                            Count;
  UINTN                            Index;
  UINTN                            N;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_INFO                    *Info;
  UINTN                            Size;
  UINT8                            Buf[SIZE_OF_EFI_FILE_INFO + 512];

  N = 0;
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles))) {
    return 0;
  }

  for (Index = 0; Index < Count && N < Max; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs)) ||
        EFI_ERROR (Fs->OpenVolume (Fs, &Root)))
    {
      continue;
    }

    for ( ; N < Max;) {
      Size = sizeof (Buf);
      if (EFI_ERROR (Root->Read (Root, &Size, Buf)) || (Size == 0)) {
        break;
      }

      Info = (EFI_FILE_INFO *)Buf;
      if ((Info->Attribute & EFI_FILE_DIRECTORY) != 0) {
        continue;
      }

      Files[N].Root = Root;
      Files[N].Size = Info->FileSize;
      UnicodeSPrint (Files[N].Name, sizeof (Files[N].Name), L"%s", Info->FileName);
      N++;
    }
  }

  FreePool (Handles);
  return N;
}

STATIC
VOID
Progress (
  CONST CHAR16  *Label,
  UINTN         Percent
  )
{
  CHAR16  Buf[32];

  GfxFill (460, 760, 1000, 90, RGB (0x18, 0x18, 0x18));
  FontDraw (460, 762, Label, C_WHITE, FONT_SCALE);
  GfxFill (460, 800, 1000, 22, RGB (0x44, 0x44, 0x44));
  GfxGradientH (460, 800, (INT32)(1000 * Percent / 100), 22, C_ORANGE_DARK, C_ORANGE);
  UnicodeSPrint (Buf, sizeof (Buf), L"%d%%", Percent);
  FontDrawRight (1460, 762, Buf, C_WHITE, FONT_SCALE);
  GfxPresentRect (460, 760, 1000, 90);
}

STATIC
BOOLEAN
CheckImage (
  QF_FILE  *F,
  CHAR16   *Why,
  UINTN    WhySize
  )
{
  EFI_FILE_PROTOCOL  *File;
  UINT8              *Data;
  UINTN              Size;
  UINTN              Index;
  BOOLEAN            Match;
  CHAR8              Id[9];

  if ((F->Size != SIZE_32MB) && (F->Size != SIZE_16MB)) {
    UnicodeSPrint (Why, WhySize, L"Invalid BIOS image size (%Ld bytes).", F->Size);
    return FALSE;
  }

  if (EFI_ERROR (F->Root->Open (F->Root, &File, F->Name, EFI_FILE_MODE_READ, 0))) {
    StrCpyS (Why, WhySize / sizeof (CHAR16), L"Can't Read File");
    return FALSE;
  }

  Data = AllocatePool ((UINTN)F->Size);
  Size = (UINTN)F->Size;
  if ((Data == NULL) || EFI_ERROR (File->Read (File, &Size, Data))) {
    File->Close (File);
    StrCpyS (Why, WhySize / sizeof (CHAR16), L"Can't Read File");
    return FALSE;
  }

  File->Close (File);
  UnicodeStrToAsciiStrS (gSdb.Info->BiosId, Id, sizeof (Id));
  Match = FALSE;
  for (Index = 0; Index + 8 < Size; Index++) {
    if ((Data[Index] == (UINT8)Id[0]) && (CompareMem (Data + Index, Id, 8) == 0)) {
      Match = TRUE;
      break;
    }
  }

  FreePool (Data);
  if (!Match) {
    UnicodeSPrint (Why, WhySize, L"BIOS image is not for %s (BIOS ID %s).", gSdb.Info->Model, gSdb.Info->BiosId);
  }

  return Match;
}

VOID
QFlashRun (
  VOID
  )
{
  CONST CHAR16  *Items[3];
  CONST CHAR16  **Names;
  QF_FILE       *Files;
  UINTN         Count;
  UINTN         Index;
  INTN          Pick;
  CHAR16        Why[160];
  KEY           Key;

  for ( ; ;) {
    DarkScreen (SdbNamed (SDB_STR_QFLASH_TITLE));
    GfxDrawImage (DataImage (IMG_GUI (184)), 420, 300);
    GfxDrawImage (DataImage (IMG_GUI (185)), 850, 380);
    GfxDrawImage (DataImage (IMG_GUI (186)), 1250, 290);
    FontDrawCentered (560, 500, L"USB / HDD", RGB (0xC0, 0xC0, 0xC0), FONT_SCALE);
    FontDrawCentered (1357, 500, gSdb.Info->BiosVersion, RGB (0xC0, 0xC0, 0xC0), FONT_SCALE);
    GfxPresent ();

    Items[0] = SdbNamed (SDB_STR_UPDATE_BIOS);
    Items[1] = SdbNamed (SDB_STR_SAVE_BIOS);
    Items[2] = SdbNamed (SDB_STR_EXIT_NOSAVE);
    Pick     = DlgMenu (SdbNamed (SDB_STR_QFLASH_TITLE), Items, 3, 0, TRUE);
    if ((Pick < 0) || (Pick == 2)) {
      return;
    }

    if (Pick == 1) {
      DlgMessage (SdbNamed (SDB_STR_SAVE_BIOS), SdbNamed (SDB_STR_NOT_EMULATED));
      continue;
    }

    Files = AllocateZeroPool (128 * sizeof (QF_FILE));
    Names = AllocateZeroPool (128 * sizeof (CHAR16 *));
    if ((Files == NULL) || (Names == NULL)) {
      return;
    }

    Count = ListFiles (Files, 128);
    if (Count == 0) {
      DlgMessage (SdbNamed (SDB_STR_UPDATE_BIOS), SdbNamed (SDB_STR_QFLASH_NO_DRIVE));
      FreePool (Files);
      FreePool (Names);
      continue;
    }

    for (Index = 0; Index < Count; Index++) {
      Names[Index] = Files[Index].Name;
    }

    Pick = DlgMenu (SdbNamed (SDB_STR_UPDATE_BIOS), Names, Count, 0, FALSE);
    if (Pick >= 0) {
      if (!CheckImage (&Files[Pick], Why, sizeof (Why))) {
        DlgMessage (SdbNamed (SDB_STR_ERROR), Why);
      } else if (DlgConfirm (SdbNamed (SDB_STR_UPDATE_BIOS), L"Are you sure to update BIOS?")) {
        for (Index = 0; Index <= 100; Index += 4) {
          Progress (L"Verifying...", Index);
          gBS->Stall (40 * 1000);
        }

        for (Index = 0; Index <= 100; Index += 2) {
          Progress (L"Updating BIOS...", Index);
          gBS->Stall (60 * 1000);
        }

        GfxFill (460, 760, 1000, 90, RGB (0x18, 0x18, 0x18));
        FontDraw (460, 770, L"Update completed (simulated - the emulated BIOS is unchanged).", C_WHITE, FONT_SCALE);
        FontDraw (460, 806, L"Press any key to reboot.", RGB (0xC0, 0xC0, 0xC0), FONT_SCALE);
        GfxPresent ();
        InputFlush ();
        while (!InputRead (&Key, MAX_UINTN)) {
        }

        GfxFill (0, 0, SCREEN_W, SCREEN_H, C_BLACK);
        GfxPresent ();
        gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
      }
    }

    FreePool (Files);
    FreePool (Names);
  }
}

//
// ------------------------------------------------------------ Smart Fan 6
//
#define MAX_FANS  12

STATIC
UINTN
FindFanControls (
  SDB_STMT  **Out,
  UINTN     Max
  )
{
  UINT32        Index;
  UINTN         N;
  CONST CHAR16  *P;
  UINTN         Len;

  N = 0;
  for (Index = 0; Index < gSdb.StmtCount && N < Max; Index++) {
    SDB_STMT  *S = &gSdb.Stmts[Index];
    if ((S->Kind != SDB_K_ONEOF) || ((S->Flags & SDB_SF_VIRTUAL) != 0)) {
      continue;
    }

    P   = SdbStr (S->Prompt);
    Len = StrLen (P);
    if ((Len > 14) && (StrCmp (P + Len - 14, L" Speed Control") == 0) && ((P[0] == L'C') || (P[0] == L'S')) &&
        (StrnCmp (P, L"CPU_", 4) == 0 || StrnCmp (P, L"SYS_", 4) == 0))
    {
      UINTN  J;
      for (J = 0; J < N; J++) {
        if (Out[J]->QuestionId == S->QuestionId) {
          break;
        }
      }

      if (J == N) {
        Out[N++] = S;
      }
    }
  }

  return N;
}

STATIC
VOID
DrawCurve (
  INT32         X,
  INT32         Y,
  INT32         W,
  INT32         H,
  CONST CHAR16  *Mode
  )
{
  // (temperature degC, duty %) points per preset
  STATIC CONST UINT8  Silent[5][2] = { { 0, 20 }, { 40, 20 }, { 60, 40 }, { 75, 70 }, { 90, 100 } };
  STATIC CONST UINT8  Normal[5][2] = { { 0, 30 }, { 40, 30 }, { 55, 50 }, { 70, 80 }, { 85, 100 } };
  STATIC CONST UINT8  Full[5][2]   = { { 0, 100 }, { 25, 100 }, { 50, 100 }, { 75, 100 }, { 100, 100 } };
  CONST UINT8         (*P)[2];
  UINTN               I;
  CHAR16              Buf[16];
  INT32               Px[5];
  INT32               Py[5];
  INT32               T;

  P = Normal;
  if (StrStr (Mode, L"Silent") != NULL) {
    P = Silent;
  } else if (StrStr (Mode, L"Full") != NULL) {
    P = Full;
  }

  GfxFill (X, Y, W, H, RGB (0x16, 0x16, 0x16));
  for (I = 0; I <= 10; I++) {
    GfxFill (X + (W * (INT32)I) / 10, Y, 1, H, RGB (0x34, 0x34, 0x34));
    GfxFill (X, Y + (H * (INT32)I) / 10, W, 1, RGB (0x34, 0x34, 0x34));
    UnicodeSPrint (Buf, sizeof (Buf), L"%d", I * 10);
    FontDraw (X + (W * (INT32)I) / 10 - 8, Y + H + 6, Buf, RGB (0x90, 0x90, 0x90), 180);
    FontDrawRight (X - 8, Y + H - (H * (INT32)I) / 10 - 10, Buf, RGB (0x90, 0x90, 0x90), 180);
  }

  FontDraw (X + W - 140, Y + H + 30, L"Temperature (\x00B0" L"C)", RGB (0xB0, 0xB0, 0xB0), 200);
  FontDraw (X - 70, Y - 34, L"PWM (%)", RGB (0xB0, 0xB0, 0xB0), 200);
  for (I = 0; I < 5; I++) {
    Px[I] = X + (W * P[I][0]) / 100;
    Py[I] = Y + H - (H * P[I][1]) / 100;
  }

  for (I = 0; I + 1 < 5; I++) {
    for (T = -1; T <= 1; T++) {
      GfxLine (Px[I], Py[I] + T, Px[I + 1], Py[I + 1] + T, C_ORANGE, 255);
    }
  }

  GfxLine (Px[4], Py[4], X + W, Py[4], C_ORANGE, 255);
  for (I = 0; I < 5; I++) {
    GfxDrawImage (DataImage (IMG_GUI (150)), Px[I] - 10, Py[I] - 10);
  }

  // current CPU temperature marker
  T = X + (W * (INT32)gHw.CpuTempDeci) / 1000;
  GfxBlend (T - 1, Y, 3, H, RGB (0x40, 0xA0, 0xFF), 180);
}

VOID
SmartFanRun (
  VOID
  )
{
  SDB_STMT  *Fans[MAX_FANS];
  UINTN     Count;
  UINTN     Sel;
  UINTN     Index;
  KEY       Key;
  CHAR16    Value[64];
  CHAR16    Name[64];
  UINT32    *Saved;

  Count = FindFanControls (Fans, MAX_FANS);
  Sel   = 0;
  Saved = GfxSave ();
  for ( ; ;) {
    DarkScreen (SdbNamed (SDB_STR_SMART_FAN));
    for (Index = 0; Index < Count; Index++) {
      INT32   Y   = 140 + (INT32)Index * 70;
      UINT32  Rpm = Index == 0 ? gHw.CpuFanRpm : (Index == 2 ? gHw.SysFanRpm : 0);
      CONST CHAR16  *P = SdbStr (Fans[Index]->Prompt);
      UINTN   L = StrLen (P) - 14;

      StrnCpyS (Name, ARRAY_SIZE (Name), P, MIN (L, ARRAY_SIZE (Name) - 1));
      if (Index == Sel) {
        GfxGradientV (40, Y - 6, 560, 62, RGB (0x50, 0x50, 0x50), RGB (0x26, 0x26, 0x26));
        GfxFill (40, Y - 6, 4, 62, C_ORANGE);
      }

      GfxDrawImageTinted (DataImage (IMG_GUI (112)), 60, Y + 8, Rpm ? C_ORANGE : RGB (0x70, 0x70, 0x70));
      FontDraw (100, Y, Name, C_WHITE, FONT_SCALE);
      if (Rpm != 0) {
        UnicodeSPrint (Value, sizeof (Value), L"%d RPM", Rpm);
      } else {
        StrCpyS (Value, ARRAY_SIZE (Value), L"N/A");
      }

      FontDraw (100, Y + 26, Value, RGB (0xA0, 0xA0, 0xA0), 210);
      UiFormatStatementValue (Fans[Index], Value, sizeof (Value));
      FontDrawRight (580, Y + 12, Value, C_ORANGE, FONT_SCALE);
    }

    if (Count > 0) {
      UiFormatStatementValue (Fans[Sel], Value, sizeof (Value));
      DrawCurve (760, 180, 1040, 620, Value);
      FontDrawWrapped (700, 880, SdbStr (Fans[Sel]->Help), RGB (0xB0, 0xB0, 0xB0), 210, 1140, 24, 4);
    }

    FontDraw (40, SCREEN_H - 50, L"\x2191\x2193: Select fan   Enter/+/-: Change mode   ESC/F6: Back", RGB (0x90, 0x90, 0x90), 210);
    GfxPresent ();

    if (!InputRead (&Key, 1000)) {
      HwTick ();
      continue;
    }

    if ((Key.Scan == SCAN_ESC) || (Key.Scan == SCAN_F6)) {
      break;
    }

    if ((Key.Scan == SCAN_UP) && (Sel > 0)) {
      Sel--;
    } else if ((Key.Scan == SCAN_DOWN) && (Sel + 1 < Count)) {
      Sel++;
    } else if ((Count > 0) && ((Key.Char == CHAR_CARRIAGE_RETURN) || (Key.Char == L'+') || (Key.Char == L'-'))) {
      UiEditStatement (Fans[Sel], Key.Char == L'+' ? 1 : (Key.Char == L'-' ? -1 : 0));
    }
  }

  GfxRestore (Saved);
  GfxPresent ();
}
