/** @file
  Pop-up dialogs in the style of the GIGABYTE GUI (dark frame, orange title).

  SPDX-License-Identifier: MIT
**/

#include "AorusBios.h"

#define DLG_ROW_H     32
#define DLG_BTN_H     40
#define DLG_BTN_GAP   12
#define DLG_MAX_ROWS  14
#define DLG_PAD       28

STATIC UINT32  *mDlgBase;             // dimmed screen behind the dialog

STATIC
VOID
DlgBegin (
  OUT UINT32  **Original
  )
{
  *Original = GfxSave ();
  mDlgBase  = GfxSave ();
}

STATIC
VOID
DlgRedrawBase (
  VOID
  )
{
  if (mDlgBase != NULL) {
    CopyMem (gBack, mDlgBase, SCREEN_W * SCREEN_H * sizeof (UINT32));
  }
}

STATIC
VOID
DlgEnd (
  IN UINT32  *Original
  )
{
  if (mDlgBase != NULL) {
    FreePool (mDlgBase);
    mDlgBase = NULL;
  }

  GfxRestore (Original);
  GfxPresent ();
}

VOID
DlgDrawFrame (
  INT32         X,
  INT32         Y,
  INT32         W,
  INT32         H,
  CONST CHAR16  *Title
  )
{
  GfxBlend (X + 10, Y + 10, W, H, C_BLACK, 80);
  GfxGradientV (X, Y, W, H, RGB (0x46, 0x46, 0x46), RGB (0x22, 0x22, 0x22));
  GfxRect (X, Y, W, H, RGB (0x80, 0x80, 0x80));
  GfxRect (X + 1, Y + 1, W - 2, H - 2, RGB (0x18, 0x18, 0x18));
  GfxRect (X + 5, Y + 5, W - 10, H - 10, RGB (0x55, 0x55, 0x55));
  if ((Title != NULL) && (*Title != 0)) {
    FontDrawCentered (X + W / 2, Y + 14, Title, C_ORANGE, FONT_SCALE);
    GfxGradientH (X + 20, Y + 44, W / 2 - 20, 2, RGB (0x30, 0x30, 0x30), C_ORANGE);
    GfxGradientH (X + W / 2, Y + 44, W / 2 - 20, 2, C_ORANGE, RGB (0x30, 0x30, 0x30));
  }

  // close cross
  GfxDrawImage (DataImage (IMG_GUI (58)), X + W - 32, Y + 14);
}

STATIC
VOID
DrawButton (
  INT32         X,
  INT32         Y,
  INT32         W,
  INT32         H,
  CONST CHAR16  *Text,
  BOOLEAN       Selected
  )
{
  if (Selected) {
    GfxGradientV (X, Y, W, H, RGB (0x2A, 0x2A, 0x2A), RGB (0x14, 0x14, 0x14));
    GfxRect (X, Y, W, H, C_ORANGE);
    GfxRect (X + 1, Y + 1, W - 2, H - 2, C_ORANGE_DARK);
    FontDrawCentered (X + W / 2, Y + (H - FONT_CELL) / 2 + 1, Text, C_ORANGE, FONT_SCALE);
  } else {
    GfxGradientV (X, Y, W, H, RGB (0xF4, 0xF4, 0xF4), RGB (0xB8, 0xB8, 0xB8));
    GfxRect (X, Y, W, H, RGB (0x6A, 0x6A, 0x6A));
    GfxFill (X + 1, Y + 1, W - 2, 1, C_WHITE);
    FontDrawCentered (X + W / 2, Y + (H - FONT_CELL) / 2 + 1, Text, C_TEXT, FONT_SCALE);
  }
}

INTN
DlgMenu (
  CONST CHAR16  *Title,
  CONST CHAR16  **Items,
  UINTN         Count,
  UINTN         Selected,
  BOOLEAN       Buttons
  )
{
  UINT32  *Original;
  INT32   W;
  INT32   H;
  INT32   X;
  INT32   Y;
  UINTN   Index;
  UINTN   Top;
  UINTN   Rows;
  INT32   MaxText;
  KEY     Key;
  INTN    Result;
  BOOLEAN List;

  if (Count == 0) {
    return DLG_RESULT_CANCEL;
  }

  if (Selected >= Count) {
    Selected = 0;
  }

  MaxText = FontWidth (Title, FONT_SCALE) + 60;     // room for the close cross
  for (Index = 0; Index < Count; Index++) {
    MaxText = MAX (MaxText, FontWidth (Items[Index], FONT_SCALE));
  }

  List = !Buttons || Count > 8;
  Rows = List ? MIN (Count, DLG_MAX_ROWS) : Count;
  W    = MAX (List ? 460 : 380, MaxText + 2 * DLG_PAD + 40);
  W    = MIN (W, SCREEN_W - 200);
  H    = 64 + (List ? (INT32)Rows * DLG_ROW_H + 16 : (INT32)Count * (DLG_BTN_H + DLG_BTN_GAP) + 8);
  X    = (SCREEN_W - W) / 2;
  Y    = (SCREEN_H - H) / 2;
  Top  = 0;

  DlgBegin (&Original);
  Result = DLG_RESULT_CANCEL;
  for ( ; ;) {
    if (Selected < Top) {
      Top = Selected;
    } else if (Selected >= Top + Rows) {
      Top = Selected - Rows + 1;
    }

    DlgRedrawBase ();
    DlgDrawFrame (X, Y, W, H, Title);
    if (List) {
      INT32  Ly = Y + 60;
      GfxFill (X + 16, Ly, W - 32, (INT32)Rows * DLG_ROW_H + 4, RGB (0xF0, 0xF0, 0xF0));
      for (Index = Top; Index < Top + Rows; Index++) {
        INT32  Ry = Ly + 2 + (INT32)(Index - Top) * DLG_ROW_H;
        if (Index == Selected) {
          GfxGradientV (X + 18, Ry, W - 36, DLG_ROW_H, RGB (0x58, 0x58, 0x58), RGB (0x20, 0x20, 0x20));
          GfxFill (X + 18, Ry, W - 36, 1, RGB (0xC0, 0xC0, 0xC0));
          FontDrawCentered (X + W / 2, Ry + 4, Items[Index], C_WHITE, FONT_SCALE);
        } else {
          if ((Index & 1) != 0) {
            GfxFill (X + 18, Ry, W - 36, DLG_ROW_H, RGB (0xE4, 0xE4, 0xE4));
          }

          FontDrawCentered (X + W / 2, Ry + 4, Items[Index], C_TEXT, FONT_SCALE);
        }
      }

      if (Count > Rows) {
        INT32  Th = MAX (24, ((INT32)Rows * DLG_ROW_H * (INT32)Rows) / (INT32)Count);
        INT32  Ty = Ly + (INT32)((((INT32)Rows * DLG_ROW_H - Th) * (INT32)Top) / (INT32)(Count - Rows));
        GfxFill (X + W - 26, Ly, 3, (INT32)Rows * DLG_ROW_H, RGB (0x90, 0x90, 0x90));
        GfxFill (X + W - 27, Ty, 5, Th, C_ORANGE);
      }
    } else {
      for (Index = 0; Index < Count; Index++) {
        DrawButton (
          X + DLG_PAD,
          Y + 64 + (INT32)Index * (DLG_BTN_H + DLG_BTN_GAP),
          W - 2 * DLG_PAD,
          DLG_BTN_H,
          Items[Index],
          Index == Selected
          );
      }
    }

    GfxPresentRect (X, Y, W + 12, H + 12);

    if (!InputRead (&Key, MAX_UINTN)) {
      continue;
    }

    if ((Key.Scan == SCAN_UP) || (Key.Char == L'-')) {
      Selected = Selected > 0 ? Selected - 1 : Count - 1;
    } else if ((Key.Scan == SCAN_DOWN) || (Key.Char == L'+')) {
      Selected = Selected + 1 < Count ? Selected + 1 : 0;
    } else if (Key.Scan == SCAN_PAGE_UP) {
      Selected = Selected > Rows ? Selected - Rows : 0;
    } else if (Key.Scan == SCAN_PAGE_DOWN) {
      Selected = MIN (Selected + Rows, Count - 1);
    } else if (Key.Scan == SCAN_HOME) {
      Selected = 0;
    } else if (Key.Scan == SCAN_END) {
      Selected = Count - 1;
    } else if ((Key.Char == CHAR_CARRIAGE_RETURN) || (Key.Char == L' ')) {
      Result = (INTN)Selected;
      break;
    } else if (Key.Scan == SCAN_ESC) {
      break;
    }
  }

  DlgEnd (Original);
  return Result;
}

STATIC
INT32
WrappedHeight (
  CONST CHAR16  *Text,
  INT32         Width
  )
{
  // Measure by drawing off-screen (cheap enough for dialogs).
  INT32  Lines;

  GfxSetClip (0, 0, 0, 0);
  Lines = FontDrawWrapped (-SCREEN_W, 0, Text, 0, FONT_SCALE, Width, 28, 20);
  GfxResetClip ();
  return Lines * 28;
}

STATIC
INTN
DlgButtons (
  CONST CHAR16  *Title,
  CONST CHAR16  *Message,
  CONST CHAR16  **Buttons,
  UINTN         Count,
  UINTN         Selected
  )
{
  UINT32  *Original;
  INT32   W;
  INT32   H;
  INT32   X;
  INT32   Y;
  INT32   TextH;
  INT32   Bw;
  UINTN   Index;
  KEY     Key;
  INTN    Result;

  W     = MAX (560, FontWidth (Title, FONT_SCALE) + 120);
  W     = MIN (MAX (W, MIN (FontWidth (Message, FONT_SCALE) + 2 * DLG_PAD, 900)), 900);
  TextH = WrappedHeight (Message, W - 2 * DLG_PAD);
  H     = 64 + TextH + 30 + DLG_BTN_H + 26;
  X     = (SCREEN_W - W) / 2;
  Y     = (SCREEN_H - H) / 2;
  Bw    = 180;

  DlgBegin (&Original);
  Result = DLG_RESULT_CANCEL;
  for ( ; ;) {
    DlgRedrawBase ();
    DlgDrawFrame (X, Y, W, H, Title);
    FontDrawWrapped (X + DLG_PAD, Y + 62, Message, RGB (0xE8, 0xE8, 0xE8), FONT_SCALE, W - 2 * DLG_PAD, 28, 20);
    for (Index = 0; Index < Count; Index++) {
      INT32  Total = (INT32)Count * Bw + ((INT32)Count - 1) * 30;
      DrawButton (
        X + (W - Total) / 2 + (INT32)Index * (Bw + 30),
        Y + H - DLG_BTN_H - 24,
        Bw,
        DLG_BTN_H,
        Buttons[Index],
        Index == Selected
        );
    }

    GfxPresentRect (X, Y, W + 12, H + 12);
    if (!InputRead (&Key, MAX_UINTN)) {
      continue;
    }

    if ((Key.Scan == SCAN_LEFT) || (Key.Scan == SCAN_UP)) {
      Selected = Selected > 0 ? Selected - 1 : Count - 1;
    } else if ((Key.Scan == SCAN_RIGHT) || (Key.Scan == SCAN_DOWN) || (Key.Char == CHAR_TAB)) {
      Selected = Selected + 1 < Count ? Selected + 1 : 0;
    } else if (Key.Char == CHAR_CARRIAGE_RETURN) {
      Result = (INTN)Selected;
      break;
    } else if ((Count == 2) && ((Key.Char == L'y') || (Key.Char == L'Y'))) {
      Result = 0;
      break;
    } else if ((Count == 2) && ((Key.Char == L'n') || (Key.Char == L'N'))) {
      Result = 1;
      break;
    } else if (Key.Scan == SCAN_ESC) {
      Result = DLG_RESULT_CANCEL;
      break;
    }
  }

  DlgEnd (Original);
  return Result;
}

BOOLEAN
DlgConfirm (
  CONST CHAR16  *Title,
  CONST CHAR16  *Message
  )
{
  CONST CHAR16  *Buttons[2];

  Buttons[0] = SdbNamed (SDB_STR_YES);
  Buttons[1] = SdbNamed (SDB_STR_NO);
  return DlgButtons (Title, Message, Buttons, 2, 0) == 0;
}

VOID
DlgMessage (
  CONST CHAR16  *Title,
  CONST CHAR16  *Message
  )
{
  CONST CHAR16  *Buttons[1];

  Buttons[0] = SdbNamed (SDB_STR_OK);
  DlgButtons (Title, Message, Buttons, 1, 0);
}

BOOLEAN
DlgInput (
  CONST CHAR16  *Title,
  CONST CHAR16  *Prompt,
  CHAR16        *Buf,
  UINTN         MaxChars,
  BOOLEAN       Password,
  BOOLEAN       NumericOnly
  )
{
  UINT32   *Original;
  INT32    W;
  INT32    H;
  INT32    X;
  INT32    Y;
  UINTN    Len;
  UINTN    Index;
  KEY      Key;
  BOOLEAN  Ok;
  CHAR16   Shown[80];

  W = 620;
  H = 64 + 34 + 52 + 30;
  X = (SCREEN_W - W) / 2;
  Y = (SCREEN_H - H) / 2;
  if (MaxChars >= ARRAY_SIZE (Shown)) {
    MaxChars = ARRAY_SIZE (Shown) - 1;
  }

  Len = StrLen (Buf);
  DlgBegin (&Original);
  Ok = FALSE;
  for ( ; ;) {
    DlgRedrawBase ();
    DlgDrawFrame (X, Y, W, H, Title);
    FontDraw (X + DLG_PAD, Y + 64, Prompt, RGB (0xE0, 0xE0, 0xE0), FONT_SCALE);
    GfxFill (X + DLG_PAD, Y + 100, W - 2 * DLG_PAD, 40, RGB (0xF6, 0xF6, 0xF6));
    GfxRect (X + DLG_PAD, Y + 100, W - 2 * DLG_PAD, 40, C_ORANGE);
    for (Index = 0; Index < Len && Index < ARRAY_SIZE (Shown) - 1; Index++) {
      Shown[Index] = Password ? L'*' : Buf[Index];
    }

    Shown[Index] = 0;
    {
      INT32  Tw = FontDraw (X + DLG_PAD + 10, Y + 108, Shown, C_TEXT, FONT_SCALE);
      GfxFill (X + DLG_PAD + 12 + Tw, Y + 106, 2, 28, C_ORANGE);
    }

    GfxPresentRect (X, Y, W + 12, H + 12);
    if (!InputRead (&Key, MAX_UINTN)) {
      continue;
    }

    if (Key.Char == CHAR_CARRIAGE_RETURN) {
      Ok = TRUE;
      break;
    } else if (Key.Scan == SCAN_ESC) {
      break;
    } else if (Key.Char == CHAR_BACKSPACE) {
      if (Len > 0) {
        Buf[--Len] = 0;
      }
    } else if ((Key.Char >= 0x20) && (Len < MaxChars)) {
      if (NumericOnly && !((Key.Char >= L'0' && Key.Char <= L'9') || Key.Char == L'.' || Key.Char == L'-' || Key.Char == L'+')) {
        continue;
      }

      Buf[Len++] = Key.Char;
      Buf[Len]   = 0;
    }
  }

  DlgEnd (Original);
  return Ok;
}
