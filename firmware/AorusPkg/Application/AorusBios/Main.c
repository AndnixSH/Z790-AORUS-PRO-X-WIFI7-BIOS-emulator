/** @file
  AORUS BIOS emulator: entry point.

  OVMF's boot manager starts this application right after the consoles and
  devices are up (see Ovmf.patch) with the load option L"POST".  It then
  plays the part of the GIGABYTE firmware front-end: POST screen, memory
  training cycles, hot keys, boot devices and the setup UI.

  The application also replaces OVMF's UiApp, so every other way into
  "firmware setup" lands here too (without the POST screen).

  SPDX-License-Identifier: MIT
**/

#include "AorusBios.h"

STATIC
BOOLEAN
LaunchedForPost (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded;

  if (EFI_ERROR (gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded))) {
    return FALSE;
  }

  return Loaded->LoadOptionsSize >= sizeof (L"POST") - sizeof (CHAR16) &&
         Loaded->LoadOptions != NULL &&
         CompareMem (Loaded->LoadOptions, L"POST", sizeof (L"POST") - sizeof (CHAR16)) == 0;
}

EFI_STATUS
EFIAPI
AorusBiosMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  BOOLEAN     ForPost;
  UINTN       Result;

  ForPost = LaunchedForPost (ImageHandle);

  // The boot manager arms a 5 minute watchdog before starting a boot
  // option; a BIOS setup screen must be able to stay up forever.
  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  Status = DataLoad ();
  if (EFI_ERROR (Status)) {
    Print (L"AORUS BIOS emulator: data files missing from the firmware (%r).\n", Status);
    Print (L"Rebuild with scripts/build.sh so that the BIOS assets get embedded.\n");
    return Status;
  }

  Status = GfxInit ();
  if (EFI_ERROR (Status)) {
    Print (L"AORUS BIOS emulator: no graphics output (%r).\n", Status);
    return Status;
  }

  HwInit ();
  SettingsLoad ();
  HwTick ();
  BootRefresh ();

  if (ForPost) {
    PostRun (TRUE);
    return EFI_SUCCESS;
  }

  Result = SetupRun ();
  if (Result == UI_EXIT_RESET) {
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  }

  return EFI_SUCCESS;
}
