/** @file
  Keyboard input with timeouts.

  SPDX-License-Identifier: MIT
**/

#include "AorusBios.h"

STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *mInEx;
STATIC EFI_EVENT                          mTimer;

STATIC
VOID
InputInit (
  VOID
  )
{
  if (mInEx == NULL) {
    if (EFI_ERROR (gBS->HandleProtocol (gST->ConsoleInHandle, &gEfiSimpleTextInputExProtocolGuid, (VOID **)&mInEx))) {
      gBS->LocateProtocol (&gEfiSimpleTextInputExProtocolGuid, NULL, (VOID **)&mInEx);
    }
  }

  if (mTimer == NULL) {
    gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &mTimer);
  }
}

STATIC
BOOLEAN
ReadOne (
  OUT KEY  *Key
  )
{
  EFI_KEY_DATA   Data;
  EFI_INPUT_KEY  K;

  if (mInEx != NULL) {
    if (EFI_ERROR (mInEx->ReadKeyStrokeEx (mInEx, &Data))) {
      return FALSE;
    }

    Key->Scan  = Data.Key.ScanCode;
    Key->Char  = Data.Key.UnicodeChar;
    Key->Shift = (Data.KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID) ? Data.KeyState.KeyShiftState : 0;
    // Pure modifier/toggle events carry neither a scan code nor a character.
    return Key->Scan != SCAN_NULL || Key->Char != CHAR_NULL;
  }

  if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &K))) {
    return FALSE;
  }

  Key->Scan  = K.ScanCode;
  Key->Char  = K.UnicodeChar;
  Key->Shift = 0;
  return TRUE;
}

BOOLEAN
InputRead (
  OUT KEY  *Key,
  IN  UINTN TimeoutMs
  )
{
  EFI_EVENT  Events[2];
  UINTN      Index;

  InputInit ();
  ZeroMem (Key, sizeof (*Key));
  if (ReadOne (Key)) {
    return TRUE;
  }

  if (TimeoutMs == 0) {
    return FALSE;
  }

  Events[0] = mInEx != NULL ? mInEx->WaitForKeyEx : gST->ConIn->WaitForKey;
  Events[1] = mTimer;
  gBS->SetTimer (mTimer, TimerRelative, MultU64x32 (TimeoutMs, 10000));
  for ( ; ;) {
    if (EFI_ERROR (gBS->WaitForEvent (2, Events, &Index))) {
      return FALSE;
    }

    if (Index == 1) {
      return ReadOne (Key);
    }

    if (ReadOne (Key)) {
      gBS->SetTimer (mTimer, TimerCancel, 0);
      return TRUE;
    }
  }
}

VOID
InputFlush (
  VOID
  )
{
  KEY  Key;

  InputInit ();
  while (ReadOne (&Key)) {
  }
}
