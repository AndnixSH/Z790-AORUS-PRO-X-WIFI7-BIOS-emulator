/** @file
  Software renderer: a 1920x1080 back buffer that is blitted to the GOP.

  SPDX-License-Identifier: MIT
**/

#include "AorusBios.h"

UINT32  *gBack;

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *mGop;
STATIC UINT32                        mModeW;
STATIC UINT32                        mModeH;
STATIC UINT32                        *mScaled;  // only if the mode is not 1920x1080
STATIC RECT                          mClip = { 0, 0, SCREEN_W, SCREEN_H };

EFI_STATUS
GfxInit (
  VOID
  )
{
  EFI_STATUS                            Status;
  UINT32                                Mode;
  UINT32                                Best;
  UINT64                                BestScore;
  UINTN                                 InfoSize;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;

  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&mGop);
  if (EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  //
  // Prefer exactly 1920x1080 (the resolution GIGABYTE's GUI is drawn for),
  // otherwise the largest mode that is not bigger than that.
  //
  Best      = mGop->Mode->Mode;
  BestScore = 0;
  for (Mode = 0; Mode < mGop->Mode->MaxMode; Mode++) {
    if (EFI_ERROR (mGop->QueryMode (mGop, Mode, &InfoSize, &Info))) {
      continue;
    }

    if ((Info->HorizontalResolution == SCREEN_W) && (Info->VerticalResolution == SCREEN_H)) {
      Best      = Mode;
      BestScore = MAX_UINT64;
    } else if ((Info->HorizontalResolution <= SCREEN_W) && (Info->VerticalResolution <= SCREEN_H) &&
               ((UINT64)Info->HorizontalResolution * Info->VerticalResolution > BestScore) &&
               (BestScore != MAX_UINT64))
    {
      Best      = Mode;
      BestScore = (UINT64)Info->HorizontalResolution * Info->VerticalResolution;
    }

    FreePool (Info);
  }

  if (Best != mGop->Mode->Mode) {
    mGop->SetMode (mGop, Best);
  }

  mModeW = mGop->Mode->Info->HorizontalResolution;
  mModeH = mGop->Mode->Info->VerticalResolution;

  if (gBack == NULL) {
    gBack = AllocateZeroPool (SCREEN_W * SCREEN_H * sizeof (UINT32));
    if (gBack == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
  }

  if (((mModeW != SCREEN_W) || (mModeH != SCREEN_H)) && (mScaled == NULL)) {
    mScaled = AllocateZeroPool (mModeW * mModeH * sizeof (UINT32));
  }

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
ClipRect (
  IN OUT INT32  *X,
  IN OUT INT32  *Y,
  IN OUT INT32  *W,
  IN OUT INT32  *H
  )
{
  INT32  X1;
  INT32  Y1;

  X1 = MIN (*X + *W, mClip.X + mClip.W);
  Y1 = MIN (*Y + *H, mClip.Y + mClip.H);
  *X = MAX (*X, mClip.X);
  *Y = MAX (*Y, mClip.Y);
  *W = X1 - *X;
  *H = Y1 - *Y;
  return (*W > 0) && (*H > 0);
}

VOID
GfxSetClip (
  INT32  X,
  INT32  Y,
  INT32  W,
  INT32  H
  )
{
  mClip.X = MAX (X, 0);
  mClip.Y = MAX (Y, 0);
  mClip.W = MIN (X + W, SCREEN_W) - mClip.X;
  mClip.H = MIN (Y + H, SCREEN_H) - mClip.Y;
}

VOID
GfxResetClip (
  VOID
  )
{
  mClip.X = 0;
  mClip.Y = 0;
  mClip.W = SCREEN_W;
  mClip.H = SCREEN_H;
}

STATIC
UINT32
Mix (
  UINT32  Dst,
  UINT32  Src,
  UINT32  A
  )
{
  UINT32  Rb;
  UINT32  G;

  A  += A >> 7;                     // 0..256
  Rb  = ((((Src & 0xFF00FF) * A) + ((Dst & 0xFF00FF) * (256 - A))) >> 8) & 0xFF00FF;
  G   = ((((Src & 0x00FF00) * A) + ((Dst & 0x00FF00) * (256 - A))) >> 8) & 0x00FF00;
  return Rb | G;
}

VOID
GfxFill (
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  Color
  )
{
  INT32   Row;
  UINT32  *P;

  if (!ClipRect (&X, &Y, &W, &H)) {
    return;
  }

  for (Row = 0; Row < H; Row++) {
    P = gBack + (UINTN)(Y + Row) * SCREEN_W + X;
    SetMem32 (P, (UINTN)W * 4, Color);
  }
}

VOID
GfxBlend (
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  Color,
  UINT32  Alpha
  )
{
  INT32   Row;
  INT32   Col;
  UINT32  *P;

  if (!ClipRect (&X, &Y, &W, &H)) {
    return;
  }

  for (Row = 0; Row < H; Row++) {
    P = gBack + (UINTN)(Y + Row) * SCREEN_W + X;
    for (Col = 0; Col < W; Col++) {
      P[Col] = Mix (P[Col], Color, Alpha);
    }
  }
}

STATIC
UINT32
Lerp (
  UINT32  C0,
  UINT32  C1,
  INT32   T,
  INT32   N
  )
{
  INT32  R;
  INT32  G;
  INT32  B;

  if (N <= 0) {
    return C0;
  }

  R = (INT32)((C0 >> 16) & 0xFF) + (((INT32)((C1 >> 16) & 0xFF) - (INT32)((C0 >> 16) & 0xFF)) * T) / N;
  G = (INT32)((C0 >> 8) & 0xFF) + (((INT32)((C1 >> 8) & 0xFF) - (INT32)((C0 >> 8) & 0xFF)) * T) / N;
  B = (INT32)(C0 & 0xFF) + (((INT32)(C1 & 0xFF) - (INT32)(C0 & 0xFF)) * T) / N;
  return RGB (R, G, B);
}

VOID
GfxGradientV (
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  Top,
  UINT32  Bottom
  )
{
  INT32  Row;

  for (Row = 0; Row < H; Row++) {
    GfxFill (X, Y + Row, W, 1, Lerp (Top, Bottom, Row, H - 1));
  }
}

VOID
GfxGradientH (
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  Left,
  UINT32  Right
  )
{
  INT32  Col;

  for (Col = 0; Col < W; Col++) {
    GfxFill (X + Col, Y, 1, H, Lerp (Left, Right, Col, W - 1));
  }
}

VOID
GfxRect (
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  Color
  )
{
  GfxFill (X, Y, W, 1, Color);
  GfxFill (X, Y + H - 1, W, 1, Color);
  GfxFill (X, Y, 1, H, Color);
  GfxFill (X + W - 1, Y, 1, H, Color);
}

VOID
GfxLine (
  INT32   X0,
  INT32   Y0,
  INT32   X1,
  INT32   Y1,
  UINT32  Color,
  UINT32  Alpha
  )
{
  INT32  Dx;
  INT32  Dy;
  INT32  Sx;
  INT32  Sy;
  INT32  Err;
  INT32  E2;

  Dx  = X1 > X0 ? X1 - X0 : X0 - X1;
  Dy  = Y1 > Y0 ? Y0 - Y1 : Y1 - Y0;
  Sx  = X0 < X1 ? 1 : -1;
  Sy  = Y0 < Y1 ? 1 : -1;
  Err = Dx + Dy;
  for ( ; ;) {
    if ((X0 >= mClip.X) && (Y0 >= mClip.Y) && (X0 < mClip.X + mClip.W) && (Y0 < mClip.Y + mClip.H)) {
      UINT32  *P = gBack + (UINTN)Y0 * SCREEN_W + X0;
      *P = Mix (*P, Color, Alpha);
    }

    if ((X0 == X1) && (Y0 == Y1)) {
      break;
    }

    E2 = 2 * Err;
    if (E2 >= Dy) {
      Err += Dy;
      X0  += Sx;
    }

    if (E2 <= Dx) {
      Err += Dx;
      Y0  += Sy;
    }
  }
}

VOID
GfxParallelogram (
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  INT32   Slant,
  UINT32  Top,
  UINT32  Bottom
  )
{
  INT32  Row;
  INT32  Shift;

  //
  // Both side edges lean by Slant pixels over the full height (top row is
  // shifted right by Slant for a positive value).
  //
  for (Row = 0; Row < H; Row++) {
    Shift = (H > 1) ? (Slant * (H - 1 - Row)) / (H - 1) : 0;
    GfxFill (X + Shift, Y + Row, W, 1, Lerp (Top, Bottom, Row, H - 1));
  }
}

VOID
GfxDrawImageAlpha (
  CONST IMAGE  *Img,
  INT32        X,
  INT32        Y,
  UINT32       Alpha
  )
{
  INT32   Dx;
  INT32   Dy;
  INT32   W;
  INT32   H;
  INT32   Row;
  INT32   Col;
  UINT32  *Src;
  UINT32  *Dst;
  UINT32  Px;
  UINT32  A;

  if (Img == NULL) {
    return;
  }

  Dx = X;
  Dy = Y;
  W  = Img->Width;
  H  = Img->Height;
  if (!ClipRect (&Dx, &Dy, &W, &H)) {
    return;
  }

  for (Row = 0; Row < H; Row++) {
    Src = Img->Pixels + (UINTN)(Dy - Y + Row) * Img->Width + (Dx - X);
    Dst = gBack + (UINTN)(Dy + Row) * SCREEN_W + Dx;
    for (Col = 0; Col < W; Col++) {
      Px = Src[Col];
      A  = ((Px >> 24) * Alpha) / 255;
      if (A == 255) {
        Dst[Col] = Px & 0xFFFFFF;
      } else if (A != 0) {
        Dst[Col] = Mix (Dst[Col], Px, A);
      }
    }
  }
}

VOID
GfxDrawImage (
  CONST IMAGE  *Img,
  INT32        X,
  INT32        Y
  )
{
  GfxDrawImageAlpha (Img, X, Y, 255);
}

VOID
GfxDrawImageTinted (
  CONST IMAGE  *Img,
  INT32        X,
  INT32        Y,
  UINT32       Color
  )
{
  INT32   Dx;
  INT32   Dy;
  INT32   W;
  INT32   H;
  INT32   Row;
  INT32   Col;
  UINT32  *Src;
  UINT32  *Dst;
  UINT32  A;

  if (Img == NULL) {
    return;
  }

  Dx = X;
  Dy = Y;
  W  = Img->Width;
  H  = Img->Height;
  if (!ClipRect (&Dx, &Dy, &W, &H)) {
    return;
  }

  for (Row = 0; Row < H; Row++) {
    Src = Img->Pixels + (UINTN)(Dy - Y + Row) * Img->Width + (Dx - X);
    Dst = gBack + (UINTN)(Dy + Row) * SCREEN_W + Dx;
    for (Col = 0; Col < W; Col++) {
      A = Src[Col] >> 24;
      if (A != 0) {
        Dst[Col] = Mix (Dst[Col], Color, A);
      }
    }
  }
}

VOID
GfxDrawImageScaled (
  CONST IMAGE  *Img,
  INT32        X,
  INT32        Y,
  INT32        W,
  INT32        H
  )
{
  INT32   Row;
  INT32   Col;
  INT32   Sx;
  INT32   Sy;
  UINT32  Px;
  UINT32  A;
  UINT32  *Dst;

  if ((Img == NULL) || (W <= 0) || (H <= 0)) {
    return;
  }

  for (Row = 0; Row < H; Row++) {
    if ((Y + Row < mClip.Y) || (Y + Row >= mClip.Y + mClip.H)) {
      continue;
    }

    Sy  = (Row * Img->Height) / H;
    Dst = gBack + (UINTN)(Y + Row) * SCREEN_W;
    for (Col = 0; Col < W; Col++) {
      if ((X + Col < mClip.X) || (X + Col >= mClip.X + mClip.W)) {
        continue;
      }

      Sx = (Col * Img->Width) / W;
      Px = Img->Pixels[(UINTN)Sy * Img->Width + Sx];
      A  = Px >> 24;
      if (A == 255) {
        Dst[X + Col] = Px & 0xFFFFFF;
      } else if (A != 0) {
        Dst[X + Col] = Mix (Dst[X + Col], Px, A);
      }
    }
  }
}

VOID
GfxDrawImage3Slice (
  CONST IMAGE  *Img,
  INT32        X,
  INT32        Y,
  INT32        W,
  INT32        Cap
  )
{
  INT32   Row;
  INT32   Col;
  INT32   Sx;
  INT32   Mid;
  UINT32  Px;
  UINT32  A;
  UINT32  *Dst;

  if ((Img == NULL) || (W <= 0)) {
    return;
  }

  if (Cap * 2 >= Img->Width) {
    Cap = Img->Width / 3;
  }

  Mid = Img->Width - 2 * Cap;
  for (Row = 0; Row < Img->Height; Row++) {
    if ((Y + Row < mClip.Y) || (Y + Row >= mClip.Y + mClip.H)) {
      continue;
    }

    Dst = gBack + (UINTN)(Y + Row) * SCREEN_W;
    for (Col = 0; Col < W; Col++) {
      if ((X + Col < mClip.X) || (X + Col >= mClip.X + mClip.W)) {
        continue;
      }

      if (Col < Cap) {
        Sx = Col;
      } else if (Col >= W - Cap) {
        Sx = Img->Width - (W - Col);
      } else {
        Sx = Cap + ((Col - Cap) * Mid) / MAX (W - 2 * Cap, 1);
      }

      Px = Img->Pixels[(UINTN)Row * Img->Width + Sx];
      A  = Px >> 24;
      if (A == 255) {
        Dst[X + Col] = Px & 0xFFFFFF;
      } else if (A != 0) {
        Dst[X + Col] = Mix (Dst[X + Col], Px, A);
      }
    }
  }
}

VOID
GfxPresentRect (
  INT32  X,
  INT32  Y,
  INT32  W,
  INT32  H
  )
{
  INT32  Row;
  INT32  Col;
  INT32  Ox;
  INT32  Oy;

  if (mGop == NULL) {
    return;
  }

  X = MAX (X, 0);
  Y = MAX (Y, 0);
  W = MIN (X + W, SCREEN_W) - X;
  H = MIN (Y + H, SCREEN_H) - Y;
  if ((W <= 0) || (H <= 0)) {
    return;
  }

  if ((mModeW == SCREEN_W) && (mModeH == SCREEN_H)) {
    mGop->Blt (
            mGop,
            (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)gBack,
            EfiBltBufferToVideo,
            X,
            Y,
            X,
            Y,
            W,
            H,
            SCREEN_W * sizeof (UINT32)
            );
    return;
  }

  if ((mModeW >= SCREEN_W) && (mModeH >= SCREEN_H)) {
    Ox = (mModeW - SCREEN_W) / 2;
    Oy = (mModeH - SCREEN_H) / 2;
    mGop->Blt (
            mGop,
            (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)gBack,
            EfiBltBufferToVideo,
            X,
            Y,
            X + Ox,
            Y + Oy,
            W,
            H,
            SCREEN_W * sizeof (UINT32)
            );
    return;
  }

  //
  // Smaller screen: nearest-neighbour downscale of the whole frame.
  //
  if (mScaled == NULL) {
    return;
  }

  for (Row = 0; Row < (INT32)mModeH; Row++) {
    UINT32  *Src = gBack + (UINTN)((Row * SCREEN_H) / mModeH) * SCREEN_W;
    UINT32  *Dst = mScaled + (UINTN)Row * mModeW;
    for (Col = 0; Col < (INT32)mModeW; Col++) {
      Dst[Col] = Src[(Col * SCREEN_W) / mModeW];
    }
  }

  mGop->Blt (
          mGop,
          (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)mScaled,
          EfiBltBufferToVideo,
          0,
          0,
          0,
          0,
          mModeW,
          mModeH,
          0
          );
}

VOID
GfxPresent (
  VOID
  )
{
  GfxPresentRect (0, 0, SCREEN_W, SCREEN_H);
}

UINT32 *
GfxSave (
  VOID
  )
{
  UINT32  *Copy;

  Copy = AllocatePool (SCREEN_W * SCREEN_H * sizeof (UINT32));
  if (Copy != NULL) {
    CopyMem (Copy, gBack, SCREEN_W * SCREEN_H * sizeof (UINT32));
  }

  return Copy;
}

VOID
GfxRestore (
  UINT32  *Saved
  )
{
  if (Saved != NULL) {
    CopyMem (gBack, Saved, SCREEN_W * SCREEN_H * sizeof (UINT32));
    FreePool (Saved);
  }
}

//
// F12 "Print Screen": GIGABYTE writes MMDDhhmm.BMP to a FAT drive.
//
EFI_STATUS
GfxSaveScreenshot (
  OUT CHAR16  *Name,
  IN  UINTN   NameSize
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *Handles;
  UINTN                            Count;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *File;
  EFI_TIME                         Now;
  UINT8                            Header[54];
  UINT8                            *Line;
  UINTN                            Size;
  INT32                            Row;
  INT32                            Col;
  UINT32                           ImageSize;

  ZeroMem (&Now, sizeof (Now));
  gRT->GetTime (&Now, NULL);
  UnicodeSPrint (Name, NameSize, L"%02d%02d%02d%02d.BMP", Now.Month, Now.Day, Now.Hour, Now.Minute);

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Line      = AllocatePool (SCREEN_W * 3);
  ImageSize = SCREEN_W * 3 * SCREEN_H;
  ZeroMem (Header, sizeof (Header));
  Header[0] = 'B';
  Header[1] = 'M';
  *(UINT32 *)&Header[2]  = sizeof (Header) + ImageSize;
  *(UINT32 *)&Header[10] = sizeof (Header);
  *(UINT32 *)&Header[14] = 40;
  *(INT32 *)&Header[18]  = SCREEN_W;
  *(INT32 *)&Header[22]  = SCREEN_H;
  *(UINT16 *)&Header[26] = 1;
  *(UINT16 *)&Header[28] = 24;
  *(UINT32 *)&Header[34] = ImageSize;

  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count && Line != NULL; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs))) {
      continue;
    }

    if (EFI_ERROR (Fs->OpenVolume (Fs, &Root))) {
      continue;
    }

    Status = Root->Open (Root, &File, Name, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR (Status)) {
      Root->Close (Root);
      continue;
    }

    Size   = sizeof (Header);
    Status = File->Write (File, &Size, Header);
    for (Row = SCREEN_H - 1; Row >= 0 && !EFI_ERROR (Status); Row--) {
      for (Col = 0; Col < SCREEN_W; Col++) {
        UINT32  Px = gBack[(UINTN)Row * SCREEN_W + Col];
        Line[Col * 3 + 0] = (UINT8)Px;
        Line[Col * 3 + 1] = (UINT8)(Px >> 8);
        Line[Col * 3 + 2] = (UINT8)(Px >> 16);
      }

      Size   = SCREEN_W * 3;
      Status = File->Write (File, &Size, Line);
    }

    File->Close (File);
    Root->Close (Root);
    if (!EFI_ERROR (Status)) {
      break;
    }
  }

  if (Line != NULL) {
    FreePool (Line);
  }

  FreePool (Handles);
  return Status;
}
