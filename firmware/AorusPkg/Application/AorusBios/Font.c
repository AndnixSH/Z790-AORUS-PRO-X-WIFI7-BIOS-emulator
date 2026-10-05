/** @file
  Text rendering with the GIGABYTE GUI font extracted from the BIOS.

  The font is a list of 24x24 8-bit alpha glyphs:
    header (0x28 bytes, starts with "FONT" at offset 8)
    { UINT16 Char; UINT16 Advance; UINT8 Alpha[24 * 24]; } ...

  SPDX-License-Identifier: MIT
**/

#include "AorusBios.h"

#define FONT_HEADER  0x28
#define GLYPH_BYTES  (FONT_CELL * FONT_CELL)

#pragma pack(1)
typedef struct {
  UINT16    Char;
  UINT16    Advance;
  UINT8     Alpha[GLYPH_BYTES];
} FONT_RECORD;
#pragma pack()

typedef struct {
  FONT_RECORD    *Rec;
  UINT8          X0, Y0, X1, Y1;      // ink bounding box (exclusive end)
} GLYPH;

STATIC GLYPH  *mGlyphs;
STATIC UINTN  mGlyphCount;
STATIC INT32  mSpace = 7;

EFI_STATUS
FontInit (
  VOID   *Data,
  UINTN  Size
  )
{
  UINTN        Index;
  FONT_RECORD  *Rec;
  UINT8        X;
  UINT8        Y;

  if ((Data == NULL) || (Size < FONT_HEADER) || (CompareMem ((UINT8 *)Data + 8, "FONT", 4) != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  mGlyphCount = (Size - FONT_HEADER) / sizeof (FONT_RECORD);
  mGlyphs     = AllocateZeroPool (mGlyphCount * sizeof (GLYPH));
  if (mGlyphs == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Rec = (FONT_RECORD *)((UINT8 *)Data + FONT_HEADER);
  for (Index = 0; Index < mGlyphCount; Index++, Rec++) {
    GLYPH  *G = &mGlyphs[Index];
    G->Rec = Rec;
    G->X0  = FONT_CELL;
    G->Y0  = FONT_CELL;
    for (Y = 0; Y < FONT_CELL; Y++) {
      for (X = 0; X < FONT_CELL; X++) {
        if (Rec->Alpha[Y * FONT_CELL + X] != 0) {
          G->X0 = MIN (G->X0, X);
          G->Y0 = MIN (G->Y0, Y);
          G->X1 = MAX (G->X1, X + 1);
          G->Y1 = MAX (G->Y1, Y + 1);
        }
      }
    }

    if (Rec->Char == L'n') {
      mSpace = (Rec->Advance * 3) / 5 + 1;
    }
  }

  return EFI_SUCCESS;
}

STATIC
GLYPH *
FindGlyph (
  CHAR16  Ch
  )
{
  UINTN  Lo;
  UINTN  Hi;
  UINTN  Mid;

  Lo = 0;
  Hi = mGlyphCount;
  while (Lo < Hi) {
    Mid = (Lo + Hi) / 2;
    if (mGlyphs[Mid].Rec->Char == Ch) {
      return &mGlyphs[Mid];
    }

    if (mGlyphs[Mid].Rec->Char < Ch) {
      Lo = Mid + 1;
    } else {
      Hi = Mid;
    }
  }

  return NULL;
}

STATIC
INT32
CharAdvance (
  CHAR16  Ch,
  UINT32  Scale
  )
{
  GLYPH  *G;

  if ((Ch == L' ') || (Ch == 0xA0)) {
    return (mSpace * (INT32)Scale) / FONT_SCALE;
  }

  if (Ch == L'\t') {
    return (mSpace * 4 * (INT32)Scale) / FONT_SCALE;
  }

  G = FindGlyph (Ch);
  if (G == NULL) {
    G = FindGlyph (L'?');
  }

  return G != NULL ? ((INT32)G->Rec->Advance * (INT32)Scale + FONT_SCALE / 2) / FONT_SCALE : 0;
}

INT32
FontWidth (
  CONST CHAR16  *Text,
  UINT32        Scale
  )
{
  INT32  W;
  INT32  Best;

  W    = 0;
  Best = 0;
  for ( ; Text != NULL && *Text != 0; Text++) {
    if ((*Text == L'\n') || (*Text == L'\r')) {
      Best = MAX (Best, W);
      W    = 0;
      continue;
    }

    W += CharAdvance (*Text, Scale);
  }

  return MAX (Best, W);
}

STATIC
VOID
DrawGlyph (
  GLYPH   *G,
  INT32   X,
  INT32   Y,
  UINT32  Color,
  UINT32  Scale
  )
{
  INT32   Row;
  INT32   Col;
  UINT32  A;
  UINT32  *P;
  INT32   Size;
  INT32   Sx;
  INT32   Sy;
  INT32   Fx;
  INT32   Fy;
  INT32   Ix;
  INT32   Iy;
  UINT32  A00;
  UINT32  A01;
  UINT32  A10;
  UINT32  A11;
  INT32   Px;
  INT32   Py;

  if (G->X1 == 0) {
    return;
  }

  if (Scale == FONT_SCALE) {
    for (Row = G->Y0; Row < G->Y1; Row++) {
      Py = Y + Row;
      if ((Py < 0) || (Py >= SCREEN_H)) {
        continue;
      }

      P = gBack + (UINTN)Py * SCREEN_W;
      for (Col = G->X0; Col < G->X1; Col++) {
        Px = X + Col;
        A  = G->Rec->Alpha[Row * FONT_CELL + Col];
        if ((A == 0) || (Px < 0) || (Px >= SCREEN_W)) {
          continue;
        }

        if (A >= 250) {
          P[Px] = Color;
        } else {
          UINT32  D  = P[Px];
          UINT32  A2 = A + (A >> 7);
          P[Px] = (((((Color & 0xFF00FF) * A2) + ((D & 0xFF00FF) * (256 - A2))) >> 8) & 0xFF00FF) |
                  (((((Color & 0x00FF00) * A2) + ((D & 0x00FF00) * (256 - A2))) >> 8) & 0x00FF00);
        }
      }
    }

    return;
  }

  //
  // Scaled glyph: bilinear sampling of the alpha map (8.8 fixed point).
  //
  Size = (FONT_CELL * (INT32)Scale) / FONT_SCALE;
  for (Row = (G->Y0 * (INT32)Scale) / FONT_SCALE; Row < MIN (Size, (G->Y1 * (INT32)Scale) / FONT_SCALE + 2); Row++) {
    Py = Y + Row;
    if ((Py < 0) || (Py >= SCREEN_H)) {
      continue;
    }

    P  = gBack + (UINTN)Py * SCREEN_W;
    Sy = (Row * FONT_SCALE * 256) / (INT32)Scale;     // source row in 8.8
    Iy = Sy >> 8;
    Fy = Sy & 0xFF;
    for (Col = (G->X0 * (INT32)Scale) / FONT_SCALE; Col < MIN (Size, (G->X1 * (INT32)Scale) / FONT_SCALE + 2); Col++) {
      Px = X + Col;
      if ((Px < 0) || (Px >= SCREEN_W)) {
        continue;
      }

      Sx  = (Col * FONT_SCALE * 256) / (INT32)Scale;
      Ix  = Sx >> 8;
      Fx  = Sx & 0xFF;
      A00 = (Ix < FONT_CELL && Iy < FONT_CELL) ? G->Rec->Alpha[Iy * FONT_CELL + Ix] : 0;
      A01 = (Ix + 1 < FONT_CELL && Iy < FONT_CELL) ? G->Rec->Alpha[Iy * FONT_CELL + Ix + 1] : 0;
      A10 = (Ix < FONT_CELL && Iy + 1 < FONT_CELL) ? G->Rec->Alpha[(Iy + 1) * FONT_CELL + Ix] : 0;
      A11 = (Ix + 1 < FONT_CELL && Iy + 1 < FONT_CELL) ? G->Rec->Alpha[(Iy + 1) * FONT_CELL + Ix + 1] : 0;
      A   = ((A00 * (256 - Fx) + A01 * Fx) * (256 - Fy) + (A10 * (256 - Fx) + A11 * Fx) * Fy) >> 16;
      if (A == 0) {
        continue;
      }

      {
        UINT32  D  = P[Px];
        UINT32  A2 = A + (A >> 7);
        P[Px] = (((((Color & 0xFF00FF) * A2) + ((D & 0xFF00FF) * (256 - A2))) >> 8) & 0xFF00FF) |
                (((((Color & 0x00FF00) * A2) + ((D & 0x00FF00) * (256 - A2))) >> 8) & 0x00FF00);
      }
    }
  }
}

INT32
FontDraw (
  INT32         X,
  INT32         Y,
  CONST CHAR16  *Text,
  UINT32        Color,
  UINT32        Scale
  )
{
  GLYPH  *G;
  INT32  X0;

  X0 = X;
  for ( ; Text != NULL && *Text != 0; Text++) {
    if ((*Text == L'\r') || (*Text == L'\n')) {
      continue;
    }

    if ((*Text != L' ') && (*Text != L'\t') && (*Text != 0xA0)) {
      G = FindGlyph (*Text);
      if (G == NULL) {
        G = FindGlyph (L'?');
      }

      if (G != NULL) {
        DrawGlyph (G, X, Y, Color, Scale);
      }
    }

    X += CharAdvance (*Text, Scale);
  }

  return X - X0;
}

INT32
FontDrawFit (
  INT32         X,
  INT32         Y,
  CONST CHAR16  *Text,
  UINT32        Color,
  UINT32        Scale,
  INT32         MaxWidth
  )
{
  CHAR16  Buf[256];
  UINTN   Len;
  INT32   Dots;

  if (FontWidth (Text, Scale) <= MaxWidth) {
    return FontDraw (X, Y, Text, Color, Scale);
  }

  Dots = FontWidth (L"...", Scale);
  Len  = 0;
  while (Text[Len] != 0 && Len < ARRAY_SIZE (Buf) - 4) {
    Buf[Len]     = Text[Len];
    Buf[Len + 1] = 0;
    if (FontWidth (Buf, Scale) + Dots > MaxWidth) {
      break;
    }

    Len++;
  }

  Buf[Len] = 0;
  StrCatS (Buf, ARRAY_SIZE (Buf), L"...");
  return FontDraw (X, Y, Buf, Color, Scale);
}

VOID
FontDrawCentered (
  INT32         Cx,
  INT32         Y,
  CONST CHAR16  *Text,
  UINT32        Color,
  UINT32        Scale
  )
{
  FontDraw (Cx - FontWidth (Text, Scale) / 2, Y, Text, Color, Scale);
}

VOID
FontDrawRight (
  INT32         Right,
  INT32         Y,
  CONST CHAR16  *Text,
  UINT32        Color,
  UINT32        Scale
  )
{
  FontDraw (Right - FontWidth (Text, Scale), Y, Text, Color, Scale);
}

STATIC
BOOLEAN
IsWide (
  CHAR16  Ch
  )
{
  return (Ch >= 0x2E80 && Ch < 0xA000) || (Ch >= 0xAC00 && Ch < 0xD7A4) || (Ch >= 0xFF00 && Ch < 0xFFF0);
}

INT32
FontDrawWrapped (
  INT32         X,
  INT32         Y,
  CONST CHAR16  *Text,
  UINT32        Color,
  UINT32        Scale,
  INT32         MaxWidth,
  INT32         LineHeight,
  INT32         MaxLines
  )
{
  CHAR16  Line[256];
  UINTN   Len;
  UINTN   LastBreak;
  UINTN   SrcAtBreak;
  UINTN   Src;
  INT32   Lines;
  INT32   W;

  if (Text == NULL) {
    return 0;
  }

  Lines      = 0;
  Len        = 0;
  W          = 0;
  LastBreak  = 0;
  SrcAtBreak = 0;
  Src        = 0;
  while (Lines < MaxLines) {
    CHAR16  Ch = Text[Src];

    if ((Ch == 0) || (Ch == L'\n')) {
      Line[Len] = 0;
      FontDraw (X, Y + Lines * LineHeight, Line, Color, Scale);
      Lines++;
      if (Ch == 0) {
        break;
      }

      Src++;
      Len       = 0;
      W         = 0;
      LastBreak = 0;
      continue;
    }

    if (Ch == L'\r') {
      Src++;
      continue;
    }

    W += CharAdvance (Ch, Scale);
    if ((W > MaxWidth) && (Len > 0)) {
      if ((LastBreak > 0) && !IsWide (Ch)) {
        Len = LastBreak;
        Src = SrcAtBreak;
      }

      Line[Len] = 0;
      FontDraw (X, Y + Lines * LineHeight, Line, Color, Scale);
      Lines++;
      Len       = 0;
      W         = 0;
      LastBreak = 0;
      while (Text[Src] == L' ') {
        Src++;
      }

      continue;
    }

    if (Len < ARRAY_SIZE (Line) - 1) {
      Line[Len++] = Ch;
    }

    if ((Ch == L' ') || IsWide (Ch) || (Ch == L'-') || (Ch == L'/')) {
      LastBreak  = Len;
      SrcAtBreak = Src + 1;
    }

    Src++;
  }

  return Lines;
}
