/** @file
  Advanced Mode: tabbed list of the BIOS setup forms.

  SPDX-License-Identifier: MIT
**/

#include "Ui.h"

#define LIST_X      40
#define LIST_Y      BODY_Y
#define LIST_W      1340
#define LIST_H      (BODY_BOTTOM - BODY_Y)
#define ROW_H       30
#define ROWS        ((LIST_H - 12) / ROW_H)
#define COL_PROMPT  (LIST_X + 44)
#define COL_STAR    (LIST_X + 690)
#define COL_VALUE   (LIST_X + 716)
#define COL_EXTRA   (LIST_X + 1000)
#define PANEL_X     1398
#define PANEL_W     482

#define FAVORITES_FORM  0xFFF0
#define SPD_INFO_FORM   0x28F6

#define ROW_STMT      0
#define ROW_BOOT      1             // "Boot Option #n" priority entry
#define ROW_BLANK     2
#define ROW_BOOTNOW   3             // "Boot Override" entry: boot it now

typedef struct {
  SDB_STMT    *S;
  UINT16      Boot;
  UINT8       Type;
  BOOLEAN     Selectable;
  BOOLEAN     Grayed;
} ROW;

typedef struct {
  UINT16    FormId;
  INTN      Sel;
  INTN      Top;
} NAV;

STATIC ROW    mRows[1536];
STATIC UINTN  mRowCount;
STATIC NAV    mNav[24];
STATIC UINTN  mDepth;
STATIC UINTN  mTab = 1;              // start on "Tweaker" like the real BIOS

STATIC
BOOLEAN
IsInteractive (
  CONST SDB_STMT  *S
  )
{
  switch (S->Kind) {
    case SDB_K_REF:
    case SDB_K_ONEOF:
    case SDB_K_CHECKBOX:
    case SDB_K_NUMERIC:
    case SDB_K_STRING:
    case SDB_K_PASSWORD:
    case SDB_K_DATE:
    case SDB_K_TIME:
      return TRUE;
    case SDB_K_ACTION:
      return (S->Flags & SDB_SF_ACTION) != 0;
    default:
      return FALSE;
  }
}

STATIC
VOID
AddRow (
  SDB_STMT  *S,
  UINT8     Type,
  UINT16    Boot
  )
{
  ROW  *R;

  if (mRowCount >= ARRAY_SIZE (mRows)) {
    return;
  }

  R             = &mRows[mRowCount++];
  R->S          = S;
  R->Type       = Type;
  R->Boot       = Boot;
  R->Grayed     = (S != NULL) && SdbGrayed (S) && (S->Kind != SDB_K_REF);
  R->Selectable = (Type == ROW_BOOT) || (Type == ROW_BOOTNOW) || ((S != NULL) && IsInteractive (S) && !R->Grayed);
}

STATIC
VOID
AddStatement (
  SDB_STMT  *S
  )
{
  UINTN  Index;

  if (SdbSuppressed (S)) {
    return;
  }

  switch (S->Kind) {
    case SDB_K_SUBTITLE:
      if (*SdbStr (S->Prompt) == 0) {
        // collapse blank lines
        if ((mRowCount == 0) || (mRows[mRowCount - 1].Type == ROW_BLANK)) {
          return;
        }

        AddRow (NULL, ROW_BLANK, 0);
        return;
      }

      break;

    case SDB_K_TEXT:
    case SDB_K_ACTION:
      if ((*SdbStr (S->Prompt) == 0) && (S->Dyn == 0)) {
        return;
      }

      break;

    case SDB_K_ONEOF:
      if (UiIsSpecial (S, SDB_SPEC_BOOT_OPTION)) {
        for (Index = 0; Index < gBootEntryCount; Index++) {
          AddRow (S, ROW_BOOT, (UINT16)Index);
        }

        return;
      }

      if (UiIsSpecial (S, SDB_SPEC_DRIVER_OPTION)) {
        return;
      }

      break;

    case SDB_K_REF:
      if (S->Action == SDB_ACT_BOOT_OVERRIDE) {
        for (Index = 0; Index < gBootEntryCount; Index++) {
          AddRow (S, ROW_BOOTNOW, (UINT16)Index);
        }

        return;
      }

      // links that AMI fills in at runtime (driver forms etc.)
      if (*SdbStr (S->Prompt) == 0) {
        return;
      }

      break;

    default:
      break;
  }

  AddRow (S, ROW_STMT, 0);
}

STATIC
VOID
BuildRows (
  VOID
  )
{
  SDB_FORM  *F;
  UINT32    Index;
  SDB_STMT  *S;

  mRowCount = 0;
  F         = SdbFindForm (mNav[mDepth].FormId);
  if (F == NULL) {
    return;
  }

  for (Index = 0; Index < F->Count; Index++) {
    AddStatement (&gSdb.Stmts[F->First + Index]);
  }

  if (mNav[mDepth].FormId == FAVORITES_FORM) {
    AddRow (NULL, ROW_BLANK, 0);
    for (Index = 0; Index < gEmu.FavCount; Index++) {
      S = SdbStmtByQuestion (gEmu.Favorites[Index]);
      if (S != NULL) {
        AddStatement (SdbVisibleVariant (S));
      }
    }
  }

  while (mRowCount > 0 && mRows[mRowCount - 1].Type == ROW_BLANK) {
    mRowCount--;
  }
}

STATIC
INTN
NextSelectable (
  INTN  From,
  INTN  Dir
  )
{
  INTN  I;
  INTN  N;

  N = (INTN)mRowCount;
  if (N == 0) {
    return -1;
  }

  I = From;
  do {
    I = (I + Dir + N) % N;
    if (mRows[I].Selectable) {
      return I;
    }
  } while (I != From);

  return mRows[From].Selectable ? From : -1;
}

STATIC
VOID
FixSelection (
  VOID
  )
{
  NAV  *N;

  N = &mNav[mDepth];
  if ((N->Sel < 0) || (N->Sel >= (INTN)mRowCount) || !mRows[N->Sel].Selectable) {
    N->Sel = NextSelectable (N->Sel < 0 ? (INTN)mRowCount - 1 : MIN (N->Sel, (INTN)mRowCount - 1), 1);
  }

  if (N->Sel < 0) {
    N->Top = 0;
    return;
  }

  if (N->Sel < N->Top) {
    N->Top = N->Sel;
  }

  if (N->Sel >= N->Top + ROWS) {
    N->Top = N->Sel - ROWS + 1;
  }

  //
  // Keep a section title visible above the first selectable item.
  //
  while (N->Top > 0 && N->Top == N->Sel && !mRows[N->Top - 1].Selectable && mRows[N->Top - 1].Type != ROW_BLANK) {
    N->Top--;
  }

  if (N->Top > (INTN)mRowCount - ROWS) {
    N->Top = MAX (0, (INTN)mRowCount - ROWS);
  }
}

STATIC
VOID
LiveValue (
  CONST SDB_STMT  *S,
  CHAR16          *Buf,
  UINTN           Size
  )
{
  Buf[0] = 0;
  if (UiIsSpecial (S, SDB_SPEC_BCLK)) {
    UnicodeSPrint (Buf, Size, L"%d.%02dMHz", gHw.BclkKhz10 / 100, gHw.BclkKhz10 % 100);
  } else if (UiIsSpecial (S, SDB_SPEC_P_RATIO)) {
    UnicodeSPrint (Buf, Size, L"%d", gHw.PCoreRatio);
  } else if (UiIsSpecial (S, SDB_SPEC_E_RATIO)) {
    UnicodeSPrint (Buf, Size, L"%d", gHw.ECoreRatio);
  } else if (UiIsSpecial (S, SDB_SPEC_MEM_MULTIPLIER)) {
    UnicodeSPrint (Buf, Size, L"%d", gHw.MemMts);
  } else if (UiIsSpecial (S, SDB_SPEC_VCORE)) {
    UnicodeSPrint (Buf, Size, L"%d.%03dV", gHw.VcoreMv / 1000, gHw.VcoreMv % 1000);
  }
}

STATIC
VOID
DrawRow (
  UINTN    Index,
  INT32    Y,
  BOOLEAN  Selected
  )
{
  ROW           *R;
  SDB_STMT      *S;
  CHAR16        Value[160];
  CHAR16        Prompt[64];
  UINT32        Fg;
  UINT32        ValueFg;
  INT32         X;
  CONST CHAR16  *P;

  R = &mRows[Index];
  S = R->S;
  if (Selected) {
    GfxGradientV (LIST_X + 6, Y, LIST_W - 34, ROW_H, RGB (0x74, 0x74, 0x74), RGB (0x2C, 0x2C, 0x2C));
    GfxFill (LIST_X + 6, Y, LIST_W - 34, 1, RGB (0xC8, 0xC8, 0xC8));
    GfxFill (LIST_X + 6, Y + ROW_H - 1, LIST_W - 34, 1, RGB (0x10, 0x10, 0x10));
    GfxParallelogram (LIST_X + 6, Y, 8, ROW_H, 4, C_ORANGE, C_ORANGE_DARK);
  } else if ((Index & 1) != 0) {
    GfxBlend (LIST_X + 6, Y, LIST_W - 34, ROW_H, RGB (0xC4, 0xC4, 0xC4), 70);
  }

  if (R->Type == ROW_BLANK) {
    return;
  }

  Fg      = Selected ? C_WHITE : (R->Grayed ? C_TEXT_GRAY : C_TEXT);
  ValueFg = Selected ? C_WHITE : (R->Grayed ? C_TEXT_GRAY : C_TEXT);
  X       = COL_PROMPT + ((S->Flags & SDB_SF_INDENT) ? 16 : 0);

  if (R->Type == ROW_BOOTNOW) {
    GfxFill (LIST_X + 22, Y + 11, 9, 9, C_ORANGE);
    FontDrawFit (X, Y + 4, gBootEntries[R->Boot].Name, Fg, 230, LIST_W - 80);
    return;
  }

  if (R->Type == ROW_BOOT) {
    UnicodeSPrint (Prompt, sizeof (Prompt), L"Boot Option #%d", R->Boot + 1);
    FontDraw (X, Y + 4, Prompt, Fg, 230);
    FontDrawFit (COL_VALUE, Y + 4, gBootEntries[R->Boot].Name, ValueFg, 230, LIST_X + LIST_W - 40 - COL_VALUE);
    return;
  }

  P = SdbStr (S->Prompt);
  while (*P == L' ') {
    P++;
  }

  if (S->Kind == SDB_K_SUBTITLE) {
    FontDrawFit (COL_PROMPT, Y + 4, P, C_ORANGE, 230, LIST_W - 80);
    return;
  }

  if (S->Kind == SDB_K_REF) {
    GfxFill (LIST_X + 22, Y + 11, 9, 9, C_ORANGE);
  }

  if ((S->Kind == SDB_K_TEXT) && (S->Text2 == 0) && (S->Dyn == 0)) {
    // free text line (notes inside forms)
    FontDrawFit (X, Y + 4, P, R->Grayed ? C_TEXT_GRAY : C_TEXT_LIGHT, 230, LIST_W - 80);
    return;
  }

  FontDrawFit (X, Y + 4, P, Fg, 230, COL_STAR - X - 10);
  if ((S->QuestionId != 0) && FavoriteHas (S->QuestionId) &&
      ((S->Kind == SDB_K_ONEOF) || (S->Kind == SDB_K_NUMERIC) || (S->Kind == SDB_K_CHECKBOX)))
  {
    GfxDrawImageTinted (DataImage (IMG_GUI (GUI_STAR)), COL_STAR, Y + 6, C_ORANGE);
  }

  UiFormatStatementValue (S, Value, sizeof (Value));
  if (Value[0] != 0) {
    FontDrawFit (COL_VALUE, Y + 4, Value, ValueFg, 230, (S->Kind == SDB_K_TEXT || S->Dyn) ? LIST_X + LIST_W - 40 - COL_VALUE : COL_EXTRA - COL_VALUE - 16);
  }

  LiveValue (S, Value, sizeof (Value));
  if (Value[0] != 0) {
    FontDraw (COL_EXTRA, Y + 4, Value, ValueFg, 230);
  }
}

STATIC
VOID
DrawList (
  BOOLEAN  Present
  )
{
  NAV    *N;
  UINTN  Index;
  INT32  Y;

  N = &mNav[mDepth];
  UiRestoreBackground (LIST_X - 4, LIST_Y - 4, LIST_W + 8, LIST_H + 8);
  GfxBlend (LIST_X, LIST_Y, LIST_W, LIST_H, C_WHITE, 140);
  GfxRect (LIST_X, LIST_Y, LIST_W, LIST_H, RGB (0xCC, 0xCC, 0xCC));

  for (Index = (UINTN)N->Top; Index < mRowCount && Index < (UINTN)N->Top + ROWS; Index++) {
    Y = LIST_Y + 6 + (INT32)(Index - N->Top) * ROW_H;
    DrawRow (Index, Y, (INTN)Index == N->Sel);
  }

  //
  // Scroll bar (thin orange track like the real GUI)
  //
  GfxFill (LIST_X + LIST_W - 18, LIST_Y + 8, 2, LIST_H - 16, RGB (0xE2, 0xA0, 0x80));
  if (mRowCount > ROWS) {
    INT32  Track = LIST_H - 16;
    INT32  Th    = MAX (30, (Track * ROWS) / (INT32)mRowCount);
    INT32  Ty    = LIST_Y + 8 + ((Track - Th) * (INT32)N->Top) / (INT32)(mRowCount - ROWS);
    GfxFill (LIST_X + LIST_W - 21, Ty, 8, Th, C_ORANGE);
  } else {
    GfxFill (LIST_X + LIST_W - 21, LIST_Y + 8, 8, 24, C_ORANGE);
  }

  if (Present) {
    GfxPresentRect (LIST_X - 4, LIST_Y - 4, LIST_W + 8, LIST_H + 8);
  }
}

STATIC
VOID
DrawHelp (
  BOOLEAN  Present
  )
{
  NAV           *N;
  CONST CHAR16  *Help;
  SDB_FORM      *F;

  N = &mNav[mDepth];
  UiRestoreBackground (LIST_X, HELP_Y - 6, LIST_W + 20, 66);
  Help = L"";
  if ((N->Sel >= 0) && (N->Sel < (INTN)mRowCount) && (mRows[N->Sel].S != NULL)) {
    SDB_STMT  *S = mRows[N->Sel].S;
    Help = SdbStr (S->Help);
    if (mRows[N->Sel].Type == ROW_BOOT) {
      Help = SdbStr (S->Help);
    } else if (*Help == 0) {
      Help = SdbStr (S->Prompt);
    }
  } else if ((F = SdbFindForm (N->FormId)) != NULL) {
    Help = SdbStr (F->Title);
  }

  while (*Help == L' ') {
    Help++;
  }

  FontDrawWrapped (LIST_X + 20, HELP_Y, Help, RGB (0x50, 0x50, 0x50), 220, LIST_W - 40, 24, 2);
  if (Present) {
    GfxPresentRect (LIST_X, HELP_Y - 6, LIST_W + 20, 66);
  }
}

STATIC
VOID
DrawTabs (
  VOID
  )
{
  UINT32  Index;

  UiRestoreBackground (0, TABS_Y - 2, SCREEN_W, TAB_H + 4);
  for (Index = 0; Index < gSdb.TabCount; Index++) {
    UiDrawTab (
      55 + (INT32)Index * 306,
      TABS_Y,
      GUI_TAB_ICON (gSdb.Tabs[Index].Icon),
      SdbStr (gSdb.Tabs[Index].Title),
      Index == mTab
      );
  }
}

STATIC
VOID
DrawAll (
  VOID
  )
{
  UiDrawBackground ();
  UiDrawHeader ();
  DrawTabs ();
  DrawList (FALSE);
  DrawHelp (FALSE);
  UiDrawMonitorPanel (PANEL_X, LIST_Y, PANEL_W, LIST_H, FALSE);
  UiDrawBottomButtons ();
  GfxPresent ();
}

STATIC
VOID
OpenTab (
  UINTN  Tab
  )
{
  mTab              = Tab;
  mDepth            = 0;
  mNav[0].FormId    = gSdb.Tabs[Tab].FormId;
  mNav[0].Sel       = -1;
  mNav[0].Top       = 0;
  BuildRows ();
  FixSelection ();
}

STATIC
VOID
PushForm (
  UINT16  FormId,
  INTN    Sel
  )
{
  if ((mDepth + 1 >= ARRAY_SIZE (mNav)) || (SdbFindForm (FormId) == NULL)) {
    return;
  }

  mDepth++;
  mNav[mDepth].FormId = FormId;
  mNav[mDepth].Sel    = Sel;
  mNav[mDepth].Top    = 0;
  BuildRows ();
  FixSelection ();
}

VOID
UiShowForm (
  UINT16  FormId
  )
{
  if (mNav[0].FormId == 0) {
    OpenTab (mTab);
  }

  PushForm (FormId, -1);
}

STATIC
BOOLEAN
WordMatch (
  CONST CHAR16  *Text,
  CONST CHAR16  *Word,
  UINTN         Len
  )
{
  UINTN    I;
  UINTN    J;
  BOOLEAN  CaseSensitive;

  // AMI rule: case-sensitive if the key word starts with a capital letter.
  CaseSensitive = Word[0] >= L'A' && Word[0] <= L'Z';
  for (I = 0; Text[I] != 0; I++) {
    for (J = 0; J < Len; J++) {
      CHAR16  A = Text[I + J];
      CHAR16  B = Word[J];
      if (A == 0) {
        return FALSE;
      }

      if (!CaseSensitive) {
        A = (A >= L'A' && A <= L'Z') ? A + 32 : A;
        B = (B >= L'A' && B <= L'Z') ? B + 32 : B;
      }

      if (A != B) {
        break;
      }
    }

    if (J == Len) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
BOOLEAN
MatchesAll (
  CONST CHAR16  *Text,
  CONST CHAR16  *Query
  )
{
  CONST CHAR16  *W;
  UINTN         Len;

  W = Query;
  while (*W != 0) {
    while (*W == L' ') {
      W++;
    }

    Len = 0;
    while (W[Len] != 0 && W[Len] != L' ') {
      Len++;
    }

    if ((Len > 0) && !WordMatch (Text, W, Len)) {
      return FALSE;
    }

    W += Len;
  }

  return TRUE;
}

STATIC
VOID
OptionSearch (
  VOID
  )
{
  CHAR16         Query[40];
  CONST CHAR16   **Items;
  UINT32         *Where;
  UINTN          Count;
  UINT32         F;
  UINT32         I;
  INTN           Pick;
  SDB_FORM       *Form;

  Query[0] = 0;
  if (!DlgInput (SdbNamed (SDB_STR_OPTION_SEARCH), SdbNamed (SDB_STR_SEARCH_ALTF), Query, 30, FALSE, FALSE) || (Query[0] == 0)) {
    return;
  }

  Items = AllocateZeroPool (512 * sizeof (CHAR16 *));
  Where = AllocateZeroPool (512 * sizeof (UINT32));
  if ((Items == NULL) || (Where == NULL)) {
    return;
  }

  Count = 0;
  for (F = 0; F < gSdb.FormCount && Count < 512; F++) {
    Form = &gSdb.Forms[F];
    if (Form->FormId >= 0xF000) {
      continue;
    }

    for (I = 0; I < Form->Count && Count < 512; I++) {
      SDB_STMT  *S = &gSdb.Stmts[Form->First + I];
      if (!IsInteractive (S) || (S->Kind == SDB_K_REF) || SdbSuppressed (S) || (*SdbStr (S->Prompt) == 0)) {
        continue;
      }

      if (MatchesAll (SdbStr (S->Prompt), Query)) {
        Items[Count]   = SdbStr (S->Prompt);
        Where[Count++] = (F << 16) | I;
      }
    }
  }

  if (Count == 0) {
    DlgMessage (SdbNamed (SDB_STR_OPTION_SEARCH), SdbNamed (SDB_STR_NO_MATCH));
  } else {
    Pick = DlgMenu (SdbNamed (SDB_STR_OPTION_SEARCH), Items, Count, 0, FALSE);
    if (Pick >= 0) {
      Form   = &gSdb.Forms[Where[Pick] >> 16];
      mDepth = 0;
      PushForm (Form->FormId, -1);
      for (I = 0; I < mRowCount; I++) {
        if (mRows[I].S == &gSdb.Stmts[Form->First + (Where[Pick] & 0xFFFF)]) {
          mNav[mDepth].Sel = (INTN)I;
          FixSelection ();
          break;
        }
      }
    }
  }

  FreePool (Items);
  FreePool (Where);
}

STATIC
BOOLEAN
EditBootRow (
  ROW    *R,
  INT32  Direction
  )
{
  CONST CHAR16  *Items[64];
  UINTN         Index;
  INTN          Pick;
  BOOT_ENTRY    Tmp;
  UINTN         Target;

  if (gBootEntryCount == 0) {
    return FALSE;
  }

  if (Direction != 0) {
    Target = (R->Boot + gBootEntryCount + Direction) % gBootEntryCount;
  } else {
    for (Index = 0; Index < gBootEntryCount && Index < ARRAY_SIZE (Items); Index++) {
      Items[Index] = gBootEntries[Index].Name;
    }

    Pick = DlgMenu (SdbStr (R->S->Help), Items, gBootEntryCount, R->Boot, FALSE);
    if (Pick < 0) {
      return FALSE;
    }

    Target = (UINTN)Pick;
  }

  //
  // Swap the two positions, like AMI's boot priority pop-up.
  //
  Tmp                        = gBootEntries[R->Boot];
  gBootEntries[R->Boot]      = gBootEntries[Target];
  gBootEntries[Target]       = Tmp;
  return TRUE;
}

STATIC
UINTN
Activate (
  INT32  Direction
  )
{
  NAV       *N;
  ROW       *R;
  SDB_STMT  *S;

  N = &mNav[mDepth];
  if ((N->Sel < 0) || (N->Sel >= (INTN)mRowCount)) {
    return UI_CONTINUE;
  }

  R = &mRows[N->Sel];
  S = R->S;
  if (R->Type == ROW_BOOT) {
    if (EditBootRow (R, Direction)) {
      BuildRows ();
    }

    return UI_CONTINUE;
  }

  if (R->Type == ROW_BOOTNOW) {
    if (Direction == 0) {
      // Like AMI: boot the device right away; back here if it fails.
      BootTry (R->Boot);
      BootRefresh ();
      BuildRows ();
      FixSelection ();
    }

    return UI_CONTINUE;
  }

  if (S == NULL) {
    return UI_CONTINUE;
  }

  if ((S->Flags & SDB_SF_ACTION) != 0) {
    if (S->Action == SDB_ACT_SEARCH) {
      if (Direction == 0) {
        OptionSearch ();
      }

      return UI_CONTINUE;
    }

    return Direction == 0 ? UiRunAction (S->Action) : UI_CONTINUE;
  }

  if (S->Kind == SDB_K_REF) {
    if (Direction == 0) {
      PushForm (S->Ref, -1);
    }

    return UI_CONTINUE;
  }

  if (UiEditStatement (S, Direction)) {
    BuildRows ();
    FixSelection ();
  }

  return UI_CONTINUE;
}

UINTN
AdvancedRun (
  BOOLEAN  Favorites
  )
{
  KEY      Key;
  UINTN    Result;
  BOOLEAN  Handled;
  NAV      *N;

  if (Favorites || (mNav[0].FormId == 0)) {
    OpenTab (Favorites ? 0 : mTab);
  } else {
    BuildRows ();
    FixSelection ();
  }

  DrawAll ();
  for ( ; ;) {
    if (!InputRead (&Key, 250)) {
      if (UiTick ()) {
        HwTick ();
        UiDrawClock (TRUE);
        UiDrawMonitorPanel (PANEL_X, LIST_Y, PANEL_W, LIST_H, TRUE);
      }

      continue;
    }

    N = &mNav[mDepth];
    Result = UiHandleGlobalKey (&Key, &Handled);
    if (Handled) {
      if (Result != UI_CONTINUE) {
        return Result;
      }

      BuildRows ();
      FixSelection ();
      DrawAll ();
      continue;
    }

    if ((Key.Shift & (EFI_LEFT_ALT_PRESSED | EFI_RIGHT_ALT_PRESSED)) && ((Key.Char == L'f') || (Key.Char == L'F'))) {
      OptionSearch ();
      DrawAll ();
      continue;
    }

    if ((Key.Shift & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED)) && ((Key.Char == L's') || (Key.Char == L'S') || (Key.Char == 0x13))) {
      PushForm (SPD_INFO_FORM, -1);
      DrawAll ();
      continue;
    }

    switch (Key.Scan) {
      case SCAN_UP:
        N->Sel = NextSelectable (N->Sel < 0 ? 0 : N->Sel, -1);
        FixSelection ();
        DrawList (TRUE);
        DrawHelp (TRUE);
        continue;

      case SCAN_DOWN:
        N->Sel = NextSelectable (N->Sel < 0 ? (INTN)mRowCount - 1 : N->Sel, 1);
        FixSelection ();
        DrawList (TRUE);
        DrawHelp (TRUE);
        continue;

      case SCAN_HOME:
        N->Sel = NextSelectable ((INTN)mRowCount - 1, 1);
        FixSelection ();
        DrawList (TRUE);
        DrawHelp (TRUE);
        continue;

      case SCAN_END:
        N->Sel = NextSelectable (0, -1);
        FixSelection ();
        DrawList (TRUE);
        DrawHelp (TRUE);
        continue;

      case SCAN_LEFT:
      case SCAN_RIGHT:
        OpenTab ((mTab + gSdb.TabCount + (Key.Scan == SCAN_RIGHT ? 1 : -1)) % gSdb.TabCount);
        DrawAll ();
        continue;

      case SCAN_PAGE_UP:
      case SCAN_PAGE_DOWN:
        Result = Activate (Key.Scan == SCAN_PAGE_UP ? 1 : -1);
        if (Result != UI_CONTINUE) {
          return Result;
        }

        DrawList (TRUE);
        DrawHelp (TRUE);
        continue;

      case SCAN_INSERT:
        if ((N->Sel >= 0) && (mRows[N->Sel].S != NULL) && (mRows[N->Sel].Type == ROW_STMT)) {
          FavoriteToggle (mRows[N->Sel].S->QuestionId);
          if (N->FormId == FAVORITES_FORM) {
            BuildRows ();
            FixSelection ();
          }

          DrawList (TRUE);
        }

        continue;

      case SCAN_ESC:
        if (mDepth > 0) {
          mDepth--;
          BuildRows ();
          FixSelection ();
          DrawAll ();
          continue;
        }

        Result = UiRunAction (SDB_ACT_EXIT_NOSAVE);
        if (Result != UI_CONTINUE) {
          return Result;
        }

        DrawAll ();
        continue;

      default:
        break;
    }

    if ((Key.Char == CHAR_CARRIAGE_RETURN) || (Key.Char == L'+') || (Key.Char == L'-')) {
      Result = Activate (Key.Char == L'+' ? 1 : (Key.Char == L'-' ? -1 : 0));
      if (Result != UI_CONTINUE) {
        return Result;
      }

      DrawAll ();
      continue;
    }

    if ((Key.Char >= L'0') && (Key.Char <= L'9') && (N->Sel >= 0) && (mRows[N->Sel].S != NULL) &&
        (mRows[N->Sel].Type == ROW_STMT))
    {
      if (UiEditNumericTyped (mRows[N->Sel].S, Key.Char)) {
        BuildRows ();
        FixSelection ();
      }

      DrawAll ();
      continue;
    }
  }
}
