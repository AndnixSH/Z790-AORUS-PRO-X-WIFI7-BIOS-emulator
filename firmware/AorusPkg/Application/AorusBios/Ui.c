/** @file
  Shared setup UI: background, header, hardware monitor, buttons, global
  hot keys and question editing.

  SPDX-License-Identifier: MIT
**/

#include "Ui.h"

UINT8  gUiMode = MODE_ADVANCED;

STATIC UINT32  *mBg;                    // cached background
STATIC UINT8   mLastSecond = 0xFF;

VOID
UiInit (
  VOID
  )
{
  INT32  I;

  if (mBg != NULL) {
    return;
  }

  //
  // Light background with faint diagonal strokes.
  //
  GfxGradientV (0, 0, SCREEN_W, SCREEN_H, RGB (0xF7, 0xF7, 0xF7), RGB (0xD6, 0xD6, 0xD6));
  for (I = -SCREEN_H; I < SCREEN_W; I += 46) {
    GfxLine (I, SCREEN_H, I + SCREEN_H / 2, 0, C_WHITE, 70);
    GfxLine (I + 1, SCREEN_H, I + 1 + SCREEN_H / 2, 0, RGB (0xC8, 0xC8, 0xC8), 30);
  }

  //
  // Dark metallic header.
  //
  GfxGradientV (0, 0, SCREEN_W, 70, RGB (0x30, 0x30, 0x30), RGB (0x0C, 0x0C, 0x0C));
  GfxParallelogram (430, 6, 380, 28, 26, RGB (0x5A, 0x5A, 0x5A), RGB (0x22, 0x22, 0x22));
  GfxParallelogram (470, 40, 330, 18, 18, RGB (0x44, 0x44, 0x44), RGB (0x1C, 0x1C, 0x1C));
  for (I = 0; I < 14; I++) {
    GfxLine (300 + I * 9, 66, 340 + I * 9, 4, RGB (0x70, 0x70, 0x70), 60);
  }

  GfxGradientV (0, 70, SCREEN_W, 12, RGB (0x9A, 0x9A, 0x9A), RGB (0xEC, 0xEC, 0xEC));
  GfxFill (0, 70, SCREEN_W, 1, RGB (0x3A, 0x3A, 0x3A));
  GfxDrawImage (DataImage (IMG_GUI (GUI_AORUS_LOGO)), 46, 14);

  //
  // Chrome strip at the very bottom.
  //
  GfxGradientV (0, SCREEN_H - 14, SCREEN_W, 14, RGB (0xBA, 0xBA, 0xBA), RGB (0x36, 0x36, 0x36));

  mBg = GfxSave ();
}

VOID
UiDrawBackground (
  VOID
  )
{
  if (mBg != NULL) {
    CopyMem (gBack, mBg, SCREEN_W * SCREEN_H * sizeof (UINT32));
  }
}

VOID
UiRestoreBackground (
  INT32  X,
  INT32  Y,
  INT32  W,
  INT32  H
  )
{
  INT32  Row;

  if (mBg == NULL) {
    return;
  }

  X = MAX (X, 0);
  Y = MAX (Y, 0);
  W = MIN (X + W, SCREEN_W) - X;
  H = MIN (Y + H, SCREEN_H) - Y;
  for (Row = 0; Row < H; Row++) {
    CopyMem (gBack + (UINTN)(Y + Row) * SCREEN_W + X, mBg + (UINTN)(Y + Row) * SCREEN_W + X, (UINTN)W * 4);
  }
}

VOID
UiDrawClock (
  BOOLEAN  Present
  )
{
  EFI_TIME      Now;
  CHAR16        Buf[32];
  STATIC CONST CHAR16  *Days[] = {
    L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"
  };
  UINTN         Y;
  UINTN         M;
  UINTN         Dow;

  ZeroMem (&Now, sizeof (Now));
  gRT->GetTime (&Now, NULL);
  mLastSecond = Now.Second;

  UiRestoreBackground (1630, 0, 290, 70);
  UnicodeSPrint (Buf, sizeof (Buf), L"%02d/%02d/%04d", Now.Month, Now.Day, Now.Year);
  FontDraw (1640, 10, Buf, RGB (0xE6, 0xE6, 0xE6), 200);

  // Day of week (Zeller-style, Gregorian)
  Y   = Now.Year;
  M   = Now.Month;
  if (M < 3) {
    M += 12;
    Y -= 1;
  }

  Dow = (Now.Day + (13 * (M + 1)) / 5 + Y + Y / 4 - Y / 100 + Y / 400 + 6) % 7;
  FontDraw (1640, 34, Days[Dow], RGB (0xB4, 0xB4, 0xB4), 200);

  UnicodeSPrint (Buf, sizeof (Buf), L"%02d:%02d", Now.Hour, Now.Minute);
  FontDraw (1772, 10, Buf, C_WHITE, 440);
  if (Present) {
    GfxPresentRect (1630, 0, 290, 70);
  }
}

VOID
UiDrawHeader (
  VOID
  )
{
  UiRestoreBackground (880, 0, 750, 82);
  if (gUiMode == MODE_EASY) {
    GfxDrawImage (DataImage (IMG_GUI (GUI_EASY_ON)), 880, 4);
    GfxDrawImage (DataImage (IMG_GUI (GUI_ADV_OFF)), 1226, 6);
  } else {
    GfxDrawImage (DataImage (IMG_GUI (GUI_EASY_OFF)), 880, 6);
    GfxDrawImage (DataImage (IMG_GUI (GUI_ADV_ON)), 1226, 4);
  }

  UiDrawClock (FALSE);
}

STATIC
VOID
PanelLabelValue (
  INT32         X,
  INT32         Y,
  CONST CHAR16  *Label,
  CONST CHAR16  *Value,
  CONST CHAR16  *Small
  )
{
  INT32  W;

  FontDraw (X, Y, Label, C_TEXT_LIGHT, 200);
  W = FontDraw (X + 2, Y + 20, Value, C_TEXT, 300);
  if (Small != NULL) {
    FontDraw (X + 8 + W, Y + 34, Small, C_TEXT, 170);
  }
}

VOID
UiDrawMonitorPanel (
  INT32    X,
  INT32    Y,
  INT32    W,
  INT32    H,
  BOOLEAN  Present
  )
{
  CHAR16  A[40];
  CHAR16  B[40];
  CHAR16  C[40];
  INT32   Col2;
  UINT32  Mhz100;
  INT32   Sy;

  UiRestoreBackground (X, Y, W, H);
  GfxBlend (X, Y, W, H, C_WHITE, 120);
  GfxFill (X, Y, 1, H, RGB (0xC4, 0xC4, 0xC4));
  Col2 = X + W / 2 + 20;

  // CPU
  Sy = Y + 12;
  FontDraw (X + 22, Sy, SdbNamed (SDB_STR_CPU), C_ORANGE, 230);
  Mhz100 = gHw.PCoreRatio * gHw.BclkKhz10;
  UnicodeSPrint (A, sizeof (A), L"%d.%02dMHz", Mhz100 / 100, Mhz100 % 100);
  UnicodeSPrint (C, sizeof (C), L"%d.%02d", (gHw.ECoreRatio * gHw.BclkKhz10) / 100, (gHw.ECoreRatio * gHw.BclkKhz10) % 100);
  PanelLabelValue (X + 22, Sy + 34, SdbNamed (SDB_STR_FREQUENCY), A, C);
  UnicodeSPrint (B, sizeof (B), L"%d.%02dMHz", gHw.BclkKhz10 / 100, gHw.BclkKhz10 % 100);
  PanelLabelValue (Col2, Sy + 34, SdbNamed (SDB_STR_BCLK), B, NULL);
  UnicodeSPrint (A, sizeof (A), L"%d.%d \x00B0" L"C", gHw.CpuTempDeci / 10, gHw.CpuTempDeci % 10);
  PanelLabelValue (X + 22, Sy + 96, SdbNamed (SDB_STR_TEMPERATURE), A, NULL);
  HwFormatMv (gHw.VcoreMv, B, sizeof (B));
  PanelLabelValue (Col2, Sy + 96, SdbNamed (SDB_STR_VOLTAGE), B, NULL);
  GfxFill (X + 16, Sy + 164, W - 32, 1, RGB (0xBE, 0xBE, 0xBE));

  // Memory
  Sy += 176;
  FontDraw (X + 22, Sy, SdbNamed (SDB_STR_MEMORY), C_ORANGE, 230);
  UnicodeSPrint (A, sizeof (A), L"%d.00MT/s", gHw.MemMts);
  PanelLabelValue (X + 22, Sy + 34, SdbNamed (SDB_STR_FREQUENCY), A, NULL);
  UnicodeSPrint (B, sizeof (B), L"%dMB", gHw.MemSizeMb);
  PanelLabelValue (Col2, Sy + 34, SdbNamed (SDB_STR_SIZE), B, NULL);
  PanelLabelValue (X + 22, Sy + 96, SdbNamed (SDB_STR_MODULE_MFG), L"QEMU", NULL);
  PanelLabelValue (Col2, Sy + 96, SdbNamed (SDB_STR_DRAM_MFG), L"Virtual", NULL);
  GfxFill (X + 16, Sy + 164, W - 32, 1, RGB (0xBE, 0xBE, 0xBE));

  // Voltage
  Sy += 176;
  FontDraw (X + 22, Sy, SdbNamed (SDB_STR_VOLTAGE), C_ORANGE, 230);
  HwDynText (SDB_DYN_HW_PCH082, A, sizeof (A));
  PanelLabelValue (X + 22, Sy + 34, L"PCH 0.82V", A, NULL);
  HwDynText (SDB_DYN_HW_5V, B, sizeof (B));
  PanelLabelValue (Col2, Sy + 34, L"+5V", B, NULL);
  HwDynText (SDB_DYN_HW_12V, A, sizeof (A));
  PanelLabelValue (X + 22, Sy + 96, L"+12V", A, NULL);
  HwDynText (SDB_DYN_HW_VCCSA, B, sizeof (B));
  PanelLabelValue (Col2, Sy + 96, L"VCCSA", B, NULL);
  UnicodeSPrint (A, sizeof (A), L"%d.%03d CP", 80 + (gHw.CpuSignature % 13), (gHw.CpuSignature * 37) % 1000);
  PanelLabelValue (X + 22, Sy + 158, L"Biscuits", A, NULL);

  if (Present) {
    GfxPresentRect (X, Y, W, H);
  }
}

VOID
UiDrawButton (
  INT32         X,
  INT32         Y,
  INT32         W,
  INT32         H,
  UINT32        IconId,
  CONST CHAR16  *Text,
  BOOLEAN       Active
  )
{
  INT32  Tw;
  INT32  Tx;

  GfxGradientV (X, Y, W, H, Active ? RGB (0x52, 0x52, 0x52) : RGB (0xFA, 0xFA, 0xFA), Active ? RGB (0x22, 0x22, 0x22) : RGB (0xC2, 0xC2, 0xC2));
  GfxRect (X, Y, W, H, RGB (0x70, 0x70, 0x70));
  GfxFill (X + 1, Y + 1, W - 2, 1, Active ? RGB (0x80, 0x80, 0x80) : C_WHITE);
  Tw = FontWidth (Text, 210) + (IconId != 0 ? 30 : 0);
  Tx = X + (W - Tw) / 2;
  if (IconId != 0) {
    GfxDrawImageTinted (DataImage (IMG_GUI (IconId)), Tx, Y + (H - 26) / 2, Active ? C_WHITE : RGB (0x30, 0x30, 0x30));
    Tx += 30;
  }

  FontDraw (Tx, Y + (H - 20) / 2 - 1, Text, Active ? C_WHITE : C_TEXT, 210);
}

VOID
UiDrawTab (
  INT32         X,
  INT32         Y,
  UINT32        IconId,
  CONST CHAR16  *Text,
  BOOLEAN       Active
  )
{
  INT32  Tw;
  INT32  Tx;

  if (Active) {
    GfxParallelogram (X + 6, Y + 2, TAB_W - 12, TAB_H - 6, 10, RGB (0x5C, 0x5C, 0x5C), RGB (0x26, 0x26, 0x26));
    GfxFill (X + 16, Y + 2, TAB_W - 22, 1, RGB (0x8C, 0x8C, 0x8C));
    GfxParallelogram (X + TAB_W - 30, Y + 2, 20, 6, 4, C_ORANGE, C_ORANGE_DARK);
    GfxGradientH (X + 10, Y + TAB_H - 5, TAB_W - 20, 3, C_ORANGE_DARK, C_ORANGE);
  } else {
    GfxDrawImage (DataImage (IMG_GUI (GUI_TAB)), X, Y);
  }

  Tw = FontWidth (Text, 220) + 32;
  Tx = X + (TAB_W - Tw) / 2;
  GfxDrawImageTinted (DataImage (IMG_GUI (IconId)), Tx, Y + 9, Active ? C_WHITE : RGB (0x2C, 0x2C, 0x2C));
  FontDraw (Tx + 32, Y + 11, Text, Active ? C_WHITE : C_TEXT, 220);
}

VOID
UiDrawBottomButtons (
  VOID
  )
{
  INT32  X;

  X = SCREEN_W - 40 - 70;
  UiDrawButton (X, BUTTONS_Y, 70, 36, GUI_ICON_SEARCH, L"", FALSE);
  X -= 190;
  UiDrawButton (X, BUTTONS_Y, 180, 36, GUI_ICON_HELP, SdbNamed (SDB_STR_HELP_F1), FALSE);
  X -= 190;
  UiDrawButton (X, BUTTONS_Y, 180, 36, GUI_ICON_CHIP, SdbNamed (SDB_STR_QFLASH_F8), FALSE);
  X -= 230;
  UiDrawButton (X, BUTTONS_Y, 220, 36, GUI_ICON_FAN, SdbNamed (SDB_STR_SMART_FAN_F6), FALSE);
}

//
// Returns TRUE once per second (time to refresh clock / sensors).
//
BOOLEAN
UiTick (
  VOID
  )
{
  EFI_TIME  Now;

  ZeroMem (&Now, sizeof (Now));
  gRT->GetTime (&Now, NULL);
  if (Now.Second != mLastSecond) {
    mLastSecond = Now.Second;
    return TRUE;
  }

  return FALSE;
}

BOOLEAN
UiIsSpecial (
  CONST SDB_STMT  *S,
  UINT16          Key
  )
{
  SDB_STMT  *Sp;

  Sp = SdbSpecial (Key);
  if ((Sp == NULL) || (S->QuestionId == 0)) {
    return FALSE;
  }

  // The IFR often defines the same setting more than once (different
  // question ids, same prompt and storage).
  return Sp->QuestionId == S->QuestionId ||
         (Sp->Prompt == S->Prompt && Sp->Kind == S->Kind && Sp->VarIndex == S->VarIndex && Sp->VarOffset == S->VarOffset);
}

BOOLEAN
UiIsLanguage (
  CONST SDB_STMT  *S
  )
{
  return UiIsSpecial (S, SDB_SPEC_LANGUAGE);
}

STATIC
CONST CHAR16 *
DayName (
  UINT16  Year,
  UINT8   Month,
  UINT8   Day
  )
{
  UINTN  Y;
  UINTN  M;

  Y = Year;
  M = Month;
  if (M < 3) {
    M += 12;
    Y -= 1;
  }

  return SdbStr ((UINT16)(0x4000 + 64 + (Day + (13 * (M + 1)) / 5 + Y + Y / 4 - Y / 100 + Y / 400 + 6) % 7));
}

VOID
UiFormatStatementValue (
  CONST SDB_STMT  *S,
  CHAR16          *Buf,
  UINTN           BufSize
  )
{
  EFI_TIME  Now;

  Buf[0] = 0;
  if (S->Dyn != 0) {
    HwDynText (S->Dyn, Buf, BufSize);
    return;
  }

  switch (S->Kind) {
    case SDB_K_TEXT:
      StrCpyS (Buf, BufSize / sizeof (CHAR16), SdbStr (S->Text2));
      break;

    case SDB_K_ONEOF:
      if (UiIsLanguage (S)) {
        StrCpyS (Buf, BufSize / sizeof (CHAR16), gSdb.Langs[gSdb.Lang].NativeName);
        break;
      }

      SdbFormatValue (S, Buf, BufSize);
      break;

    case SDB_K_CHECKBOX:
    case SDB_K_NUMERIC:
      SdbFormatValue (S, Buf, BufSize);
      break;

    case SDB_K_STRING:
      if ((S->VarIndex != SDB_NO_VAR) && (S->VarOffset + 2 <= gSdb.Vars[S->VarIndex].Size)) {
        UINTN  Max = MIN ((UINTN)(S->Max != 0 ? S->Max : 64), (gSdb.Vars[S->VarIndex].Size - S->VarOffset) / 2);
        UINTN  I;
        for (I = 0; I < Max && I < BufSize / 2 - 1; I++) {
          CHAR16  Ch;
          CopyMem (&Ch, gSdb.Cur[S->VarIndex] + S->VarOffset + 2 * I, 2);
          if (Ch == 0) {
            break;
          }

          Buf[I] = Ch;
        }

        Buf[I] = 0;
      }

      break;

    case SDB_K_PASSWORD:
      if (UiIsSpecial (S, SDB_SPEC_ADMIN_PASSWORD)) {
        StrCpyS (Buf, BufSize / sizeof (CHAR16), gEmu.AdminPassword[0] ? L"Installed" : L"Not Installed");
      } else if (UiIsSpecial (S, SDB_SPEC_USER_PASSWORD)) {
        StrCpyS (Buf, BufSize / sizeof (CHAR16), gEmu.UserPassword[0] ? L"Installed" : L"Not Installed");
      }

      break;

    case SDB_K_DATE:
      ZeroMem (&Now, sizeof (Now));
      gRT->GetTime (&Now, NULL);
      UnicodeSPrint (Buf, BufSize, L"[ %02d / %02d / %04d ]  %s", Now.Month, Now.Day, Now.Year, DayName (Now.Year, Now.Month, Now.Day));
      break;

    case SDB_K_TIME:
      ZeroMem (&Now, sizeof (Now));
      gRT->GetTime (&Now, NULL);
      UnicodeSPrint (Buf, BufSize, L"[ %02d : %02d : %02d ]", Now.Hour, Now.Minute, Now.Second);
      break;

    default:
      break;
  }
}

//
// Parse what the user typed for a numeric question.
//
STATIC
BOOLEAN
ParseNumber (
  CONST SDB_STMT  *S,
  CONST CHAR16    *Text,
  UINT64          *Value
  )
{
  UINT64   Int;
  UINT64   Frac;
  UINTN    FracDigits;
  UINTN    Scale;
  BOOLEAN  Neg;
  BOOLEAN  Dot;
  BOOLEAN  Any;

  while (*Text == L' ') {
    Text++;
  }

  if ((*Text == 0) || (*Text == L'a') || (*Text == L'A')) {
    if (S->NumFmt >= SDB_NF_AUTO) {
      *Value = 0;
      return TRUE;
    }

    return FALSE;
  }

  if (S->NumFmt == SDB_NF_HEX) {
    if ((Text[0] == L'0') && ((Text[1] == L'x') || (Text[1] == L'X'))) {
      Text += 2;
    }

    return !EFI_ERROR (StrHexToUint64S (Text, NULL, Value));
  }

  Neg = FALSE;
  if ((*Text == L'-') || (*Text == L'+')) {
    Neg = *Text == L'-';
    Text++;
  }

  Int        = 0;
  Frac       = 0;
  FracDigits = 0;
  Dot        = FALSE;
  Any        = FALSE;
  for ( ; *Text != 0; Text++) {
    if (*Text == L'.') {
      if (Dot) {
        return FALSE;
      }

      Dot = TRUE;
    } else if ((*Text >= L'0') && (*Text <= L'9')) {
      Any = TRUE;
      if (Dot) {
        if (FracDigits < 6) {
          Frac = Frac * 10 + (*Text - L'0');
          FracDigits++;
        }
      } else {
        Int = Int * 10 + (*Text - L'0');
      }
    } else if ((*Text == L'V') || (*Text == L'v') || (*Text == L'M') || (*Text == L'H') || (*Text == L'z')) {
      break;
    } else {
      return FALSE;
    }
  }

  if (!Any) {
    return FALSE;
  }

  //
  // Volts -> millivolts, MHz -> 10 kHz units; plain numbers are taken as-is
  // when no decimal point was typed (so "1250" means 1.250 V).
  //
  Scale = 1;
  if ((S->NumFmt == SDB_NF_AUTO_MV) || (S->NumFmt == SDB_NF_AUTO_MV_OFS)) {
    Scale = Dot ? 1000 : 1;
  } else if (S->NumFmt == SDB_NF_AUTO_CLK) {
    Scale = 100;
  }

  if (Scale > 1) {
    while (FracDigits > 0 && (Scale == 1000 ? FracDigits > 3 : FracDigits > 2)) {
      Frac /= 10;
      FracDigits--;
    }

    while ((Scale == 1000 ? FracDigits < 3 : FracDigits < 2)) {
      Frac *= 10;
      FracDigits++;
    }

    Int = Int * Scale + Frac;
  }

  if (Neg) {
    if (S->NumFmt != SDB_NF_AUTO_MV_OFS) {
      return FALSE;
    }

    Int = (UINT64)(-(INT64)Int) & ((S->Width >= 8) ? MAX_UINT64 : (LShiftU64 (1, S->Width * 8) - 1));
  }

  *Value = Int;
  return TRUE;
}

STATIC
BOOLEAN
EditOneOf (
  SDB_STMT  *S,
  INT32     Direction
  )
{
  CONST CHAR16  *Items[256];
  UINT64        Values[256];
  UINTN         Count;
  UINTN         Current;
  UINTN         Index;
  INTN          Pick;
  UINT64        Value;

  if (UiIsLanguage (S)) {
    for (Index = 0; Index < gSdb.LangCount && Index < ARRAY_SIZE (Items); Index++) {
      Items[Index] = gSdb.Langs[Index].NativeName;
    }

    if (Direction != 0) {
      Pick = (INTN)((gSdb.Lang + gSdb.LangCount + Direction) % gSdb.LangCount);
    } else {
      Pick = DlgMenu (SdbStr (S->Prompt), Items, gSdb.LangCount, gSdb.Lang, FALSE);
    }

    if (Pick >= 0) {
      gSdb.Lang     = (UINT32)Pick;
      gEmu.Language = (UINT8)Pick;
      EmuStateSave ();
      return TRUE;
    }

    return FALSE;
  }

  Value   = SdbGetValue (S);
  Count   = 0;
  Current = 0;
  for (Index = 0; Index < S->OptCount && Count < ARRAY_SIZE (Items); Index++) {
    SDB_OPT  *O = &gSdb.Opts[S->OptFirst + Index];
    if (!SdbOptionVisible (O)) {
      continue;
    }

    if (O->Value == Value) {
      Current = Count;
    }

    Items[Count]  = SdbStr (O->Text);
    Values[Count] = O->Value;
    Count++;
  }

  if (Count == 0) {
    return FALSE;
  }

  if (Direction != 0) {
    Pick = (INTN)((Current + Count + Direction) % Count);
  } else {
    Pick = DlgMenu (SdbStr (S->Prompt), Items, Count, Current, TRUE);
  }

  if (Pick < 0) {
    return FALSE;
  }

  SdbSetValue (S, Values[Pick]);
  return TRUE;
}

STATIC
BOOLEAN
EditNumeric (
  SDB_STMT  *S,
  INT32     Direction,
  CHAR16    FirstChar
  )
{
  CHAR16  Buf[40];
  CHAR16  Prompt[96];
  UINT64  Value;
  UINT64  Step;

  Value = SdbGetValue (S);
  Step  = S->Step != 0 ? S->Step : 1;
  if (Direction > 0) {
    Value = (Value + Step > S->Max) ? S->Max : Value + Step;
    SdbSetValue (S, Value);
    return TRUE;
  }

  if (Direction < 0) {
    Value = (Value < S->Min + Step) ? S->Min : Value - Step;
    SdbSetValue (S, Value);
    return TRUE;
  }

  Buf[0] = 0;
  if (FirstChar != 0) {
    Buf[0] = FirstChar;
    Buf[1] = 0;
  } else if (!((S->NumFmt >= SDB_NF_AUTO) && (Value == 0))) {
    SdbFormatValue (S, Buf, sizeof (Buf));
    if (S->NumFmt == SDB_NF_AUTO_MV || S->NumFmt == SDB_NF_AUTO_MV_OFS) {
      Buf[StrLen (Buf) - 1] = 0;     // drop the "V"
    } else if (S->NumFmt == SDB_NF_AUTO_CLK) {
      Buf[StrLen (Buf) - 3] = 0;     // drop "MHz"
    }
  }

  if (S->NumFmt >= SDB_NF_AUTO) {
    UnicodeSPrint (Prompt, sizeof (Prompt), L"%s (Auto = empty)", SdbStr (S->Prompt));
  } else {
    UnicodeSPrint (Prompt, sizeof (Prompt), L"Min: %Ld   Max: %Ld", S->Min, S->Max);
  }

  if (!DlgInput (SdbStr (S->Prompt), Prompt, Buf, 20, FALSE, S->NumFmt != SDB_NF_HEX)) {
    return FALSE;
  }

  if (!ParseNumber (S, Buf, &Value) ||
      ((S->NumFmt != SDB_NF_AUTO_MV_OFS) && ((Value < S->Min) || (Value > S->Max))))
  {
    DlgMessage (SdbNamed (SDB_STR_WARNING), SdbNamed (SDB_STR_INVALID_RANGE));
    return FALSE;
  }

  SdbSetValue (S, Value);
  return TRUE;
}

STATIC
BOOLEAN
EditDateTime (
  SDB_STMT  *S
  )
{
  EFI_TIME  Now;
  CHAR16    Buf[32];
  UINTN     A;
  UINTN     B;
  UINTN     C;
  CHAR16    *P;

  ZeroMem (&Now, sizeof (Now));
  gRT->GetTime (&Now, NULL);
  if (S->Kind == SDB_K_DATE) {
    UnicodeSPrint (Buf, sizeof (Buf), L"%02d/%02d/%04d", Now.Month, Now.Day, Now.Year);
  } else {
    UnicodeSPrint (Buf, sizeof (Buf), L"%02d:%02d:%02d", Now.Hour, Now.Minute, Now.Second);
  }

  if (!DlgInput (SdbStr (S->Prompt), S->Kind == SDB_K_DATE ? L"MM/DD/YYYY" : L"HH:MM:SS", Buf, 10, FALSE, FALSE)) {
    return FALSE;
  }

  P = Buf;
  A = StrDecimalToUintn (P);
  while (*P >= L'0' && *P <= L'9') {
    P++;
  }

  if (*P != 0) {
    P++;
  }

  B = StrDecimalToUintn (P);
  while (*P >= L'0' && *P <= L'9') {
    P++;
  }

  if (*P != 0) {
    P++;
  }

  C = StrDecimalToUintn (P);
  if (S->Kind == SDB_K_DATE) {
    if ((A < 1) || (A > 12) || (B < 1) || (B > 31) || (C < 1998) || (C > 2099)) {
      DlgMessage (SdbNamed (SDB_STR_WARNING), SdbNamed (SDB_STR_INVALID_RANGE));
      return FALSE;
    }

    Now.Month = (UINT8)A;
    Now.Day   = (UINT8)B;
    Now.Year  = (UINT16)C;
  } else {
    if ((A > 23) || (B > 59) || (C > 59)) {
      DlgMessage (SdbNamed (SDB_STR_WARNING), SdbNamed (SDB_STR_INVALID_RANGE));
      return FALSE;
    }

    Now.Hour   = (UINT8)A;
    Now.Minute = (UINT8)B;
    Now.Second = (UINT8)C;
  }

  gRT->SetTime (&Now);
  return TRUE;
}

STATIC
BOOLEAN
EditPassword (
  SDB_STMT  *S
  )
{
  CHAR16  *Target;
  CHAR16  New[24];
  CHAR16  Confirm[24];
  CHAR16  Old[24];

  Target = UiIsSpecial (S, SDB_SPEC_USER_PASSWORD) ? gEmu.UserPassword : gEmu.AdminPassword;
  if (Target[0] != 0) {
    Old[0] = 0;
    if (!DlgInput (SdbStr (S->Prompt), SdbNamed (SDB_STR_ENTER_CURRENT_PASSWORD), Old, 20, TRUE, FALSE)) {
      return FALSE;
    }

    if (StrCmp (Old, Target) != 0) {
      DlgMessage (SdbNamed (SDB_STR_ERROR), SdbNamed (SDB_STR_INVALID_PASSWORD));
      return FALSE;
    }
  }

  New[0] = 0;
  if (!DlgInput (SdbStr (S->Prompt), SdbNamed (SDB_STR_CREATE_PASSWORD), New, 20, TRUE, FALSE)) {
    return FALSE;
  }

  if (New[0] == 0) {
    if ((Target[0] != 0) && DlgConfirm (SdbStr (S->Prompt), SdbNamed (SDB_STR_CLEAR_PASSWORD_Q))) {
      Target[0] = 0;
      EmuStateSave ();
      return TRUE;
    }

    return FALSE;
  }

  Confirm[0] = 0;
  if (!DlgInput (SdbStr (S->Prompt), SdbNamed (SDB_STR_CONFIRM_PASSWORD), Confirm, 20, TRUE, FALSE)) {
    return FALSE;
  }

  if (StrCmp (New, Confirm) != 0) {
    DlgMessage (SdbNamed (SDB_STR_ERROR), SdbNamed (SDB_STR_INVALID_PASSWORD));
    return FALSE;
  }

  StrCpyS (Target, 24, New);
  EmuStateSave ();
  return TRUE;
}

STATIC
BOOLEAN
EditString (
  SDB_STMT  *S
  )
{
  CHAR16  Buf[80];
  UINTN   Max;
  UINTN   I;

  if (S->VarIndex == SDB_NO_VAR) {
    return FALSE;
  }

  UiFormatStatementValue (S, Buf, sizeof (Buf));
  Max = MIN ((UINTN)(S->Max != 0 ? S->Max : 64), (gSdb.Vars[S->VarIndex].Size - S->VarOffset) / 2 - 1);
  Max = MIN (Max, ARRAY_SIZE (Buf) - 1);
  if (!DlgInput (SdbStr (S->Prompt), SdbStr (S->Help), Buf, Max, FALSE, FALSE)) {
    return FALSE;
  }

  for (I = 0; I <= Max; I++) {
    CHAR16  Ch = I < StrLen (Buf) ? Buf[I] : 0;
    CopyMem (gSdb.Cur[S->VarIndex] + S->VarOffset + 2 * I, &Ch, 2);
  }

  return TRUE;
}

//
// Change a question: Direction 0 = Enter (pop-up), +1/-1 = +/- keys.
// Returns TRUE if something changed.
//
BOOLEAN
UiEditStatement (
  SDB_STMT  *S,
  INT32     Direction
  )
{
  if (SdbGrayed (S)) {
    return FALSE;
  }

  switch (S->Kind) {
    case SDB_K_ONEOF:
      return EditOneOf (S, Direction);

    case SDB_K_CHECKBOX:
      if (Direction != 0) {
        SdbSetValue (S, SdbGetValue (S) ? 0 : 1);
        return TRUE;
      } else {
        CONST CHAR16  *Items[2];
        INTN          Pick;
        Items[0] = SdbNamed (SDB_STR_DISABLED);
        Items[1] = SdbNamed (SDB_STR_ENABLED);
        Pick     = DlgMenu (SdbStr (S->Prompt), Items, 2, SdbGetValue (S) ? 1 : 0, TRUE);
        if (Pick < 0) {
          return FALSE;
        }

        SdbSetValue (S, (UINT64)Pick);
        return TRUE;
      }

    case SDB_K_NUMERIC:
      return EditNumeric (S, Direction, 0);

    case SDB_K_DATE:
    case SDB_K_TIME:
      return Direction == 0 ? EditDateTime (S) : FALSE;

    case SDB_K_PASSWORD:
      return Direction == 0 ? EditPassword (S) : FALSE;

    case SDB_K_STRING:
      return Direction == 0 ? EditString (S) : FALSE;

    default:
      return FALSE;
  }
}

//
// Numeric quick entry: typing a digit on a numeric item.
//
BOOLEAN
UiEditNumericTyped (
  SDB_STMT  *S,
  CHAR16    Ch
  )
{
  if ((S->Kind != SDB_K_NUMERIC) || SdbGrayed (S)) {
    return FALSE;
  }

  return EditNumeric (S, 0, Ch);
}

STATIC
VOID
SaveProfile (
  VOID
  )
{
  CHAR16  Name[16];
  CHAR16  Items[8][48];
  CONST CHAR16  *Ptrs[8];
  UINTN   Index;
  UINTN   Size;
  UINT8   Dummy;
  INTN    Pick;
  UINT32  Var;

  for (Index = 0; Index < 8; Index++) {
    UnicodeSPrint (Name, sizeof (Name), L"AorusProfile%d", Index + 1);
    Size = sizeof (Dummy);
    UnicodeSPrint (
      Items[Index],
      sizeof (Items[Index]),
      L"%s %d : %s",
      SdbNamed (SDB_STR_SETUP_PROFILE),
      Index + 1,
      gRT->GetVariable (Name, &gAorusEmuVariableGuid, NULL, &Size, &Dummy) == EFI_BUFFER_TOO_SMALL ? L"Saved" : L"Empty"
      );
    Ptrs[Index] = Items[Index];
  }

  Pick = DlgMenu (SdbNamed (SDB_STR_SAVE_PROFILES), Ptrs, 8, 0, FALSE);
  if (Pick < 0) {
    return;
  }

  //
  // A profile is the concatenation of all persistent setup stores.
  //
  {
    UINTN  Total = 0;
    UINT8  *Blob;
    UINT8  *P;

    for (Var = 0; Var < gSdb.VarCount; Var++) {
      if (gSdb.Vars[Var].Flags & SDB_VAR_HAS_DEFAULTS) {
        Total += gSdb.Vars[Var].Size;
      }
    }

    Blob = AllocatePool (Total);
    if (Blob == NULL) {
      return;
    }

    P = Blob;
    for (Var = 0; Var < gSdb.VarCount; Var++) {
      if (gSdb.Vars[Var].Flags & SDB_VAR_HAS_DEFAULTS) {
        CopyMem (P, gSdb.Cur[Var], gSdb.Vars[Var].Size);
        P += gSdb.Vars[Var].Size;
      }
    }

    UnicodeSPrint (Name, sizeof (Name), L"AorusProfile%d", Pick + 1);
    gRT->SetVariable (Name, &gAorusEmuVariableGuid, EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS, Total, Blob);
    FreePool (Blob);
  }

  DlgMessage (SdbNamed (SDB_STR_SAVE_PROFILES), SdbNamed (SDB_STR_PROFILE_SAVED));
}

STATIC
BOOLEAN
LoadProfile (
  VOID
  )
{
  CHAR16        Name[16];
  CHAR16        Items[8][48];
  CONST CHAR16  *Ptrs[8];
  UINTN         Index;
  UINTN         Size;
  UINT8         Dummy;
  INTN          Pick;
  UINT32        Var;
  UINT8         *Blob;
  UINT8         *P;

  for (Index = 0; Index < 8; Index++) {
    UnicodeSPrint (Name, sizeof (Name), L"AorusProfile%d", Index + 1);
    Size = sizeof (Dummy);
    UnicodeSPrint (
      Items[Index],
      sizeof (Items[Index]),
      L"%s %d : %s",
      SdbNamed (SDB_STR_SETUP_PROFILE),
      Index + 1,
      gRT->GetVariable (Name, &gAorusEmuVariableGuid, NULL, &Size, &Dummy) == EFI_BUFFER_TOO_SMALL ? L"Saved" : L"Empty"
      );
    Ptrs[Index] = Items[Index];
  }

  Pick = DlgMenu (SdbNamed (SDB_STR_LOAD_PROFILES), Ptrs, 8, 0, FALSE);
  if (Pick < 0) {
    return FALSE;
  }

  UnicodeSPrint (Name, sizeof (Name), L"AorusProfile%d", Pick + 1);
  if (EFI_ERROR (GetVariable2 (Name, &gAorusEmuVariableGuid, (VOID **)&Blob, &Size)) || (Blob == NULL)) {
    DlgMessage (SdbNamed (SDB_STR_LOAD_PROFILES), SdbNamed (SDB_STR_PROFILE_NOT_FOUND));
    return FALSE;
  }

  P = Blob;
  for (Var = 0; Var < gSdb.VarCount; Var++) {
    if (gSdb.Vars[Var].Flags & SDB_VAR_HAS_DEFAULTS) {
      if ((UINTN)(P - Blob) + gSdb.Vars[Var].Size <= Size) {
        CopyMem (gSdb.Cur[Var], P, gSdb.Vars[Var].Size);
      }

      P += gSdb.Vars[Var].Size;
    }
  }

  FreePool (Blob);
  DlgMessage (SdbNamed (SDB_STR_LOAD_PROFILES), SdbNamed (SDB_STR_PROFILE_LOADED));
  return TRUE;
}

STATIC
VOID
ShowSystemInfo (
  VOID
  )
{
  CHAR16  Msg[600];
  CHAR16  Cpu[64];
  CHAR16  Speed[48];

  HwDynText (SDB_DYN_CPU_TYPE, Cpu, sizeof (Cpu));
  HwDynText (SDB_DYN_CPU_SPEED, Speed, sizeof (Speed));
  UnicodeSPrint (
    Msg,
    sizeof (Msg),
    L"%s\n%s %s (%s)\n%s\n%s\n%dMB DDR5 %dMT/s",
    gSdb.Info->Model,
    SdbNamed (SDB_STR_BIOS_VER),
    gSdb.Info->BiosVersion,
    gSdb.Info->BiosDate,
    Cpu,
    Speed,
    gHw.MemSizeMb,
    gHw.MemMts
    );
  DlgMessage (SdbNamed (SDB_STR_SYSTEM_INFO), Msg);
}

UINTN
UiRunAction (
  UINT8  Action
  )
{
  switch (Action) {
    case SDB_ACT_SAVE_EXIT:
      if (DlgConfirm (SdbNamed (SDB_STR_SAVE_EXIT), SdbNamed (SDB_STR_SAVE_EXIT_Q))) {
        SettingsSave ();
        BootApplyPriority ();
        EmuStateSave ();
        return UI_EXIT_RESET;
      }

      break;

    case SDB_ACT_EXIT_NOSAVE:
      if (DlgConfirm (SdbNamed (SDB_STR_EXIT_NOSAVE), SdbNamed (SDB_STR_QUIT_NOSAVE_Q))) {
        SettingsRestoreSnapshot ();
        return UI_EXIT_BOOT;
      }

      break;

    case SDB_ACT_LOAD_DEFAULTS:
      if (DlgConfirm (SdbNamed (SDB_STR_LOAD_DEFAULTS), SdbNamed (SDB_STR_LOAD_DEFAULTS_Q))) {
        SettingsLoadDefaults ();
      }

      break;

    case SDB_ACT_SAVE_PROFILE:
      SaveProfile ();
      break;

    case SDB_ACT_LOAD_PROFILE:
      LoadProfile ();
      break;

    case SDB_ACT_SMARTFAN:
      SmartFanRun ();
      break;

    case SDB_ACT_QFLASH:
      if (DlgConfirm (SdbNamed (SDB_STR_QFLASH), SdbNamed (SDB_STR_QFLASH_Q))) {
        QFlashRun ();
      }

      break;

    default:
      break;
  }

  return UI_CONTINUE;
}

//
// Hot keys common to Easy Mode and Advanced Mode.
//
UINTN
UiHandleGlobalKey (
  CONST KEY  *Key,
  BOOLEAN    *Handled
  )
{
  CHAR16  Name[32];
  CHAR16  Msg[96];

  *Handled = TRUE;
  switch (Key->Scan) {
    case SCAN_F1:
      DlgMessage (SdbNamed (SDB_STR_GENERAL_HELP), SdbNamed (SDB_STR_GENERAL_HELP_TEXT));
      return UI_CONTINUE;
    case SCAN_F2:
      return UI_SWITCH_MODE;
    case SCAN_F3:
      SaveProfile ();
      return UI_CONTINUE;
    case SCAN_F4:
      LoadProfile ();
      return UI_CONTINUE;
    case SCAN_F5:
      if (DlgConfirm (SdbNamed (SDB_STR_LOAD_PREV), SdbNamed (SDB_STR_LOAD_PREV_Q))) {
        SettingsRestoreSnapshot ();
      }

      return UI_CONTINUE;
    case SCAN_F6:
      SmartFanRun ();
      return UI_CONTINUE;
    case SCAN_F7:
      return UiRunAction (SDB_ACT_LOAD_DEFAULTS);
    case SCAN_F8:
      return UiRunAction (SDB_ACT_QFLASH);
    case SCAN_F9:
      ShowSystemInfo ();
      return UI_CONTINUE;
    case SCAN_F10:
      return UiRunAction (SDB_ACT_SAVE_EXIT);
    case SCAN_F11:
      return UI_GOTO_FAV;
    case SCAN_F12:
      if (!EFI_ERROR (GfxSaveScreenshot (Name, sizeof (Name)))) {
        UnicodeSPrint (Msg, sizeof (Msg), L"%s: %s", SdbNamed (SDB_STR_PRINT_SCREEN_SAVED), Name);
      } else {
        StrCpyS (Msg, ARRAY_SIZE (Msg), SdbNamed (SDB_STR_QFLASH_NO_DRIVE));
      }

      DlgMessage (L"F12", Msg);
      return UI_CONTINUE;
    default:
      break;
  }

  *Handled = FALSE;
  return UI_CONTINUE;
}

UINTN
SetupRun (
  VOID
  )
{
  UINTN    Result;
  BOOLEAN  Favorites;
  UINT64   Pref;

  UiInit ();
  HwTick ();
  BootRefresh ();
  SettingsSnapshot ();

  Pref    = SdbSpecialValue (SDB_SPEC_PREFERRED_MODE, 0);
  gUiMode = (Pref == 1) ? MODE_EASY : (Pref == 2) ? MODE_ADVANCED : (gEmu.LastMode == MODE_ADVANCED ? MODE_ADVANCED : MODE_EASY);
  Favorites = FALSE;
  for ( ; ;) {
    Result = (gUiMode == MODE_EASY) ? EasyRun () : AdvancedRun (Favorites);
    Favorites = FALSE;
    if (Result == UI_SWITCH_MODE) {
      gUiMode       = (gUiMode == MODE_EASY) ? MODE_ADVANCED : MODE_EASY;
      gEmu.LastMode = gUiMode;
      EmuStateSave ();
      continue;
    }

    if (Result == UI_GOTO_FAV) {
      gUiMode   = MODE_ADVANCED;
      Favorites = TRUE;
      continue;
    }

    return Result;
  }
}
