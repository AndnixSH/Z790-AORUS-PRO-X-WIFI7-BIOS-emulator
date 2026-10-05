/** @file
  Easy Mode: the dashboard GIGABYTE shows by default.

  SPDX-License-Identifier: MIT
**/

#include "Ui.h"

#define W_BUTTON   1
#define W_STMT     2
#define W_PERF     3
#define W_BOOT     4

typedef struct {
  RECT        R;
  UINT8       Type;
  UINT32      Arg;              // button id / option index / boot index
  SDB_STMT    *S;
} WIDGET;

#define BTN_HELP      0
#define BTN_LANG      1
#define BTN_DEFAULTS  2
#define BTN_SAVE      3
#define BTN_FAV       4
#define BTN_SEARCH    5
#define BTN_SPD       6

STATIC WIDGET  mW[64];
STATIC UINTN   mWCount;
STATIC UINTN   mFocus = 6;

STATIC
VOID
AddWidget (
  INT32     X,
  INT32     Y,
  INT32     W,
  INT32     H,
  UINT8     Type,
  UINT32    Arg,
  SDB_STMT  *S
  )
{
  if (mWCount < ARRAY_SIZE (mW)) {
    mW[mWCount].R.X  = X;
    mW[mWCount].R.Y  = Y;
    mW[mWCount].R.W  = W;
    mW[mWCount].R.H  = H;
    mW[mWCount].Type = Type;
    mW[mWCount].Arg  = Arg;
    mW[mWCount].S    = S;
    mWCount++;
  }
}

STATIC
VOID
PanelBox (
  INT32         X,
  INT32         Y,
  INT32         W,
  INT32         H,
  CONST CHAR16  *Title
  )
{
  FontDraw (X + 4, Y, Title, C_TEXT, 240);
  GfxBlend (X, Y + 30, W, H - 30, C_WHITE, 150);
  GfxRect (X, Y + 30, W, H - 30, RGB (0xC2, 0xC2, 0xC2));
}

STATIC
VOID
InfoLine (
  INT32         X,
  INT32         Y,
  CONST CHAR16  *Label,
  CONST CHAR16  *Value
  )
{
  FontDraw (X, Y, Label, C_TEXT_LIGHT, 220);
  FontDrawWrapped (X + 140, Y, Value, C_TEXT, 220, 330, 24, 2);
}

STATIC
VOID
DrawDropValue (
  INT32         X,
  INT32         Y,
  INT32         W,
  CONST CHAR16  *Value,
  BOOLEAN       Focus,
  BOOLEAN       Gray
  )
{
  GfxFill (X, Y, W, 28, Focus ? RGB (0x3A, 0x3A, 0x3A) : RGB (0xFA, 0xFA, 0xFA));
  GfxRect (X, Y, W, 28, Focus ? C_ORANGE : RGB (0x88, 0x88, 0x88));
  FontDrawFit (X + 8, Y + 3, Value, Focus ? C_WHITE : (Gray ? C_TEXT_GRAY : C_TEXT), 210, W - 40);
  GfxFill (X + W - 26, Y + 2, 1, 24, RGB (0x88, 0x88, 0x88));
  // little "v" arrow
  GfxLine (X + W - 19, Y + 11, X + W - 13, Y + 17, Focus ? C_WHITE : C_TEXT, 255);
  GfxLine (X + W - 13, Y + 17, X + W - 7, Y + 11, Focus ? C_WHITE : C_TEXT, 255);
}

STATIC
VOID
DrawToggle (
  INT32    X,
  INT32    Y,
  BOOLEAN  On
  )
{
  GfxDrawImage (DataImage (IMG_GUI (On ? 53 : 52)), X, Y);
}

STATIC
VOID
Layout (
  VOID
  )
{
  STATIC CONST UINT32  Icons[] = { 120, 109, 114, 113, 111, 119 };
  UINTN                Index;
  SDB_STMT             *S;
  INT32                Y;
  STATIC CONST UINT16  MemKeys[] = { SDB_SPEC_XMP_BOOSTER, SDB_SPEC_XMP, SDB_SPEC_HIGH_BANDWIDTH, SDB_SPEC_LOW_LATENCY };

  mWCount = 0;
  for (Index = 0; Index < 6; Index++) {
    AddWidget (55 + (INT32)Index * 306, TABS_Y, TAB_W, TAB_H, W_BUTTON, (UINT32)Index, NULL);
    (VOID)Icons;
  }

  Y = 664;
  for (Index = 0; Index < ARRAY_SIZE (MemKeys); Index++) {
    S = SdbSpecial (MemKeys[Index]);
    if ((S != NULL) && !SdbSuppressed (S)) {
      AddWidget (330, Y, 250, 28, W_STMT, 0, S);
      Y += 40;
    }
  }

  S = SdbSpecial (SDB_SPEC_PERFDRIVE);
  if (S != NULL) {
    Y = 202;
    for (Index = 0; Index < S->OptCount && Y < 360; Index++) {
      if (SdbOptionVisible (&gSdb.Opts[S->OptFirst + Index])) {
        AddWidget (1340, Y, 520, 32, W_PERF, (UINT32)Index, S);
        Y += 36;
      }
    }
  }

  for (Index = 0; Index < gBootEntryCount && Index < 5; Index++) {
    AddWidget (1340, 438 + (INT32)Index * 56, 520, 50, W_BOOT, (UINT32)Index, NULL);
  }

  AddWidget (1340, 748, 520, 36, W_BUTTON, BTN_SPD, NULL);
  S = SdbSpecial (SDB_SPEC_MEM_BOOT_MODE);
  if (S != NULL) {
    AddWidget (1620, 800, 240, 28, W_STMT, 0, S);
  }

  S = SdbSpecial (SDB_SPEC_MEM_DETECT_MSG);
  if (S != NULL) {
    AddWidget (1620, 846, 240, 32, W_STMT, 1, S);
  }

  if (mFocus >= mWCount) {
    mFocus = 6;
  }
}

STATIC
VOID
DrawEasy (
  VOID
  )
{
  STATIC CONST UINT32  Icons[] = { 120, 109, 114, 113, 111, 119 };
  CONST CHAR16         *Labels[6];
  CHAR16               Buf[128];
  CHAR16               Buf2[64];
  UINTN                Index;
  WIDGET               *W;
  INT32                Y;
  UINT32               PerSlot;

  UiDrawBackground ();
  UiDrawHeader ();

  Labels[0] = SdbNamed (SDB_STR_HELP_F1);
  Labels[1] = gSdb.Langs[gSdb.Lang].NativeName;
  Labels[2] = SdbNamed (SDB_STR_LOAD_DEFAULTS_F7);
  Labels[3] = SdbNamed (SDB_STR_SAVE_EXIT_F10);
  Labels[4] = SdbNamed (SDB_STR_FAVORITES_F11);
  Labels[5] = SdbNamed (SDB_STR_SEARCH_ALTF);
  for (Index = 0; Index < 6; Index++) {
    UiDrawTab (55 + (INT32)Index * 306, TABS_Y, Icons[Index], Labels[Index], mFocus == Index);
  }

  //
  // Information
  //
  PanelBox (50, 152, 520, 290, SdbNamed (SDB_STR_INFORMATION));
  InfoLine (70, 196, SdbNamed (SDB_STR_MB), gSdb.Info->Model);
  InfoLine (70, 230, SdbNamed (SDB_STR_BIOS_VER), gSdb.Info->BiosVersion);
  InfoLine (70, 264, SdbNamed (SDB_STR_CPU), gHw.CpuBrand);
  UnicodeSPrint (Buf, sizeof (Buf), L"%dGB", gHw.MemSizeMb / 1024);
  InfoLine (70, 326, SdbNamed (SDB_STR_RAM), Buf);
  UnicodeSPrint (Buf, sizeof (Buf), L"%X", (UINT32)RShiftU64 (AsmReadMsr64 (0x8B), 32));
  InfoLine (70, 360, SdbNamed (SDB_STR_MICROCODE), Buf);

  //
  // DRAM status: the emulated memory sits in the A2/B2 slots (the slots
  // GIGABYTE recommends for two DIMMs).
  //
  PanelBox (50, 456, 520, 190, SdbNamed (SDB_STR_DRAM_STATUS));
  PerSlot = gHw.MemSizeMb / 2048;
  for (Index = 0; Index < 4; Index++) {
    STATIC CONST CHAR16  *Slots[] = { L"DDR5_A1", L"DDR5_A2", L"DDR5_B1", L"DDR5_B2" };
    Y = 500 + (INT32)Index * 34;
    FontDraw (70, Y, Slots[Index], C_TEXT_LIGHT, 220);
    if ((Index & 1) == 1) {
      UnicodeSPrint (Buf, sizeof (Buf), L"QEMU %dGB %dMT/s", PerSlot, gHw.MemMts);
      FontDraw (210, Y, Buf, C_TEXT, 220);
    } else {
      FontDraw (210, Y, L"N/A", C_TEXT_GRAY, 220);
    }
  }

  //
  // PC Health (centre column)
  //
  PanelBox (600, 152, 700, 290, SdbNamed (SDB_STR_PC_HEALTH));
  {
    STATIC CONST CHAR16  *Names[] = { L"CPU", L"System", L"PCH", L"VRM MOS" };
    UINT32               Temps[4];
    Temps[0] = gHw.CpuTempDeci;
    Temps[1] = gHw.SysTempDeci;
    Temps[2] = gHw.PchTempDeci;
    Temps[3] = gHw.VrmTempDeci;
    for (Index = 0; Index < 4; Index++) {
      INT32  Cx = 680 + (INT32)Index * 165;
      GfxDrawImage (DataImage (IMG_GUI (104)), Cx - 62, 200);
      UnicodeSPrint (Buf, sizeof (Buf), L"%d.%d\x00B0" L"C", Temps[Index] / 10, Temps[Index] % 10);
      FontDrawCentered (Cx, 300, Buf, C_TEXT, 300);
      FontDrawCentered (Cx, 340, Names[Index], C_TEXT_LIGHT, 210);
    }

    HwDynText (SDB_DYN_HW_VCORE, Buf, sizeof (Buf));
    UnicodeSPrint (Buf2, sizeof (Buf2), L"CPU Vcore  %s", Buf);
    FontDraw (630, 390, Buf2, C_TEXT, 210);
    HwDynText (SDB_DYN_HW_12V, Buf, sizeof (Buf));
    UnicodeSPrint (Buf2, sizeof (Buf2), L"+12V  %s", Buf);
    FontDraw (980, 390, Buf2, C_TEXT, 210);
  }

  //
  // Smart Fan 6
  //
  PanelBox (600, 456, 700, 190, SdbNamed (SDB_STR_SMART_FAN));
  {
    STATIC CONST CHAR16  *Fans[] = { L"CPU_FAN", L"CPU_OPT", L"SYS_FAN1", L"SYS_FAN2", L"SYS_FAN3", L"SYS_FAN5_PUMP" };
    for (Index = 0; Index < ARRAY_SIZE (Fans); Index++) {
      INT32   Fx  = 630 + (INT32)(Index % 3) * 225;
      INT32   Fy  = 500 + (INT32)(Index / 3) * 64;
      UINT32  Rpm = Index == 0 ? gHw.CpuFanRpm : (Index == 2 ? gHw.SysFanRpm : 0);
      GfxDrawImageTinted (DataImage (IMG_GUI (112)), Fx, Fy + 4, Rpm ? C_ORANGE : C_TEXT_GRAY);
      FontDraw (Fx + 34, Fy, Fans[Index], C_TEXT_LIGHT, 200);
      if (Rpm != 0) {
        UnicodeSPrint (Buf, sizeof (Buf), L"%d RPM", Rpm);
      } else {
        StrCpyS (Buf, ARRAY_SIZE (Buf), L"N/A");
      }

      FontDraw (Fx + 34, Fy + 22, Buf, Rpm ? C_TEXT : C_TEXT_GRAY, 220);
    }
  }

  //
  // Memory settings
  //
  PanelBox (50, 620, 1250, 250, L"");
  FontDraw (54, 618, L"X.M.P.", C_TEXT, 240);

  //
  // GIGABYTE PerfDrive / Boot Sequence / memory (right column)
  //
  PanelBox (1320, 152, 560, 220, SdbNamed (SDB_STR_PERFDRIVE));
  PanelBox (1320, 392, 560, 340, SdbNamed (SDB_STR_BOOT_SEQUENCE));
  if (gBootEntryCount == 0) {
    FontDraw (1350, 450, L"N/A", C_TEXT_GRAY, 220);
  }

  GfxBlend (1320, 740, 560, 150, C_WHITE, 150);
  GfxRect (1320, 740, 560, 150, RGB (0xC2, 0xC2, 0xC2));

  for (Index = 0; Index < mWCount; Index++) {
    BOOLEAN  Focus = Index == mFocus;
    W = &mW[Index];
    switch (W->Type) {
      case W_STMT:
        if (W->Arg == 1) {
          FontDrawFit (1340, W->R.Y + 4, SdbStr (W->S->Prompt), C_TEXT, 210, 270);
          if (Focus) {
            GfxRect (W->R.X + 160, W->R.Y - 2, 70, 36, C_ORANGE);
          }

          DrawToggle (W->R.X + 162, W->R.Y, SdbGetValue (W->S) != 0);
        } else {
          FontDrawFit (W->R.X > 1000 ? 1340 : 70, W->R.Y + 3, SdbStr (W->S->Prompt), C_TEXT, 220, 270);
          UiFormatStatementValue (W->S, Buf, sizeof (Buf));
          DrawDropValue (W->R.X, W->R.Y, W->R.W, Buf, Focus, SdbGrayed (W->S));
        }

        break;

      case W_PERF:
        {
          SDB_OPT  *O = &gSdb.Opts[W->S->OptFirst + W->Arg];
          if (Focus) {
            GfxBlend (W->R.X - 6, W->R.Y - 2, W->R.W + 12, W->R.H + 4, C_ORANGE, 60);
          }

          FontDraw (W->R.X, W->R.Y + 4, SdbStr (O->Text), C_TEXT, 220);
          DrawToggle (W->R.X + W->R.W - 70, W->R.Y, SdbGetValue (W->S) == O->Value);
        }

        break;

      case W_BOOT:
        if (Focus) {
          GfxGradientV (W->R.X - 6, W->R.Y, W->R.W + 12, W->R.H, RGB (0x60, 0x60, 0x60), RGB (0x2C, 0x2C, 0x2C));
        }

        GfxDrawImageTinted (DataImage (IMG_GUI (103)), W->R.X, W->R.Y + 6, Focus ? C_WHITE : RGB (0x40, 0x40, 0x40));
        FontDrawWrapped (W->R.X + 52, W->R.Y + 2, gBootEntries[W->Arg].Name, Focus ? C_WHITE : C_TEXT, 210, W->R.W - 60, 22, 2);
        break;

      case W_BUTTON:
        if (W->Arg == BTN_SPD) {
          UiDrawButton (W->R.X, W->R.Y, W->R.W, W->R.H, 121, L"SPD Setup", Focus);
        }

        break;
    }
  }

  UiDrawBottomButtons ();
  GfxPresent ();
}

STATIC
UINTN
MoveFocus (
  INT32  Dx,
  INT32  Dy
  )
{
  RECT    *C;
  UINTN   Index;
  UINTN   Best;
  INT64   BestScore;
  INT32   Cx;
  INT32   Cy;

  C         = &mW[mFocus].R;
  Cx        = C->X + C->W / 2;
  Cy        = C->Y + C->H / 2;
  Best      = mFocus;
  BestScore = MAX_INT64;
  for (Index = 0; Index < mWCount; Index++) {
    RECT   *R = &mW[Index].R;
    INT32  X  = R->X + R->W / 2;
    INT32  Y  = R->Y + R->H / 2;
    INT32  Along;
    INT32  Across;
    INT64  Score;

    if (Index == mFocus) {
      continue;
    }

    Along  = Dx != 0 ? (X - Cx) * Dx : (Y - Cy) * Dy;
    Across = Dx != 0 ? Y - Cy : X - Cx;
    if (Along <= 0) {
      continue;
    }

    Score = (INT64)Along + 3 * (INT64)(Across < 0 ? -Across : Across);
    if (Score < BestScore) {
      BestScore = Score;
      Best      = Index;
    }
  }

  return Best;
}

STATIC
UINTN
ActivateWidget (
  INT32  Direction
  )
{
  WIDGET  *W;
  BOOT_ENTRY  Tmp;
  UINTN   Target;
  CONST CHAR16  *Items[64];
  UINTN   Index;
  INTN    Pick;

  W = &mW[mFocus];
  switch (W->Type) {
    case W_BUTTON:
      if (Direction != 0) {
        break;
      }

      switch (W->Arg) {
        case BTN_HELP:
          DlgMessage (SdbNamed (SDB_STR_GENERAL_HELP), SdbNamed (SDB_STR_GENERAL_HELP_TEXT));
          break;
        case BTN_LANG:
          for (Index = 0; Index < gSdb.LangCount && Index < ARRAY_SIZE (Items); Index++) {
            Items[Index] = gSdb.Langs[Index].NativeName;
          }

          Pick = DlgMenu (L"Language", Items, gSdb.LangCount, gSdb.Lang, FALSE);
          if (Pick >= 0) {
            gSdb.Lang     = (UINT32)Pick;
            gEmu.Language = (UINT8)Pick;
            EmuStateSave ();
          }

          break;
        case BTN_DEFAULTS:
          return UiRunAction (SDB_ACT_LOAD_DEFAULTS);
        case BTN_SAVE:
          return UiRunAction (SDB_ACT_SAVE_EXIT);
        case BTN_FAV:
          return UI_GOTO_FAV;
        case BTN_SEARCH:
          return UI_SWITCH_MODE;
        case BTN_SPD:
          UiShowForm (0x28F6);
          return UI_SWITCH_MODE;
      }

      break;

    case W_STMT:
      UiEditStatement (W->S, Direction);
      break;

    case W_PERF:
      if (!SdbGrayed (W->S)) {
        SdbSetValue (W->S, gSdb.Opts[W->S->OptFirst + W->Arg].Value);
      }

      break;

    case W_BOOT:
      if (Direction != 0) {
        Target = (W->Arg + gBootEntryCount - (UINTN)(INTN)Direction) % gBootEntryCount;
        Tmp                   = gBootEntries[W->Arg];
        gBootEntries[W->Arg]  = gBootEntries[Target];
        gBootEntries[Target]  = Tmp;
        for (Index = 0; Index < mWCount; Index++) {
          if ((mW[Index].Type == W_BOOT) && (mW[Index].Arg == Target)) {
            mFocus = Index;
          }
        }
      }

      break;
  }

  return UI_CONTINUE;
}

UINTN
EasyRun (
  VOID
  )
{
  KEY      Key;
  UINTN    Result;
  BOOLEAN  Handled;

  Layout ();
  DrawEasy ();
  for ( ; ;) {
    if (!InputRead (&Key, 250)) {
      if (UiTick ()) {
        HwTick ();
        UiDrawClock (TRUE);
      }

      continue;
    }

    Result = UiHandleGlobalKey (&Key, &Handled);
    if (!Handled) {
      if ((Key.Shift & (EFI_LEFT_ALT_PRESSED | EFI_RIGHT_ALT_PRESSED)) && ((Key.Char == L'f') || (Key.Char == L'F'))) {
        Result = UI_SWITCH_MODE;
      } else if (Key.Scan == SCAN_UP) {
        mFocus = MoveFocus (0, -1);
      } else if (Key.Scan == SCAN_DOWN) {
        mFocus = MoveFocus (0, 1);
      } else if (Key.Scan == SCAN_LEFT) {
        mFocus = MoveFocus (-1, 0);
      } else if (Key.Scan == SCAN_RIGHT) {
        mFocus = MoveFocus (1, 0);
      } else if (Key.Char == CHAR_CARRIAGE_RETURN) {
        Result = ActivateWidget (0);
      } else if ((Key.Char == L'+') || (Key.Scan == SCAN_PAGE_UP)) {
        Result = ActivateWidget (1);
      } else if ((Key.Char == L'-') || (Key.Scan == SCAN_PAGE_DOWN)) {
        Result = ActivateWidget (-1);
      } else if (Key.Scan == SCAN_ESC) {
        Result = UiRunAction (SDB_ACT_EXIT_NOSAVE);
      }
    }

    if (Result != UI_CONTINUE) {
      return Result;
    }

    Layout ();
    DrawEasy ();
  }
}
