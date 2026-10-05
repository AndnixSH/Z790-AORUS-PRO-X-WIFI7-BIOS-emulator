/** @file
  Power-on self test screen, memory training, boot menu and boot policy.

  SPDX-License-Identifier: MIT
**/

#include "AorusBios.h"

//
// First boot / cleared CMOS: DDR5 training makes the board power-cycle a
// few times with a black screen before the logo appears.  Changing memory
// settings (XMP, multiplier, ...) retrains with fewer cycles.
//
#define TRAIN_CYCLES_FRESH    3
#define TRAIN_CYCLES_CHANGED  1
#define TRAIN_STALL_MS        2500

STATIC
VOID
Black (
  VOID
  )
{
  GfxFill (0, 0, SCREEN_W, SCREEN_H, C_BLACK);
  GfxPresent ();
}

STATIC
VOID
MemoryTraining (
  VOID
  )
{
  UINT32  Hash;
  UINT8   Cycles;

  Hash = SettingsMemoryHash ();
  if (!gCmosCleared && (gEmu.MemTrainHash == Hash)) {
    return;
  }

  Cycles = gCmosCleared ? TRAIN_CYCLES_FRESH : TRAIN_CYCLES_CHANGED;
  if (gEmu.TrainCycle < Cycles) {
    Black ();
    gEmu.TrainCycle++;
    EmuStateSave ();
    // Training takes a while; the screen stays dark like a real board.
    gBS->Stall (TRAIN_STALL_MS * 1000);
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  }

  gEmu.TrainCycle   = 0;
  gEmu.MemTrainHash = Hash;
  if (gCmosCleared) {
    //
    // Write the factory defaults (the board now has valid CMOS contents)
    // and ask the user what to do, like GIGABYTE does after a CMOS reset.
    //
    gEmu.ResetPending = 1;
    SettingsSave ();
    gCmosCleared = FALSE;
  }

  EmuStateSave ();
}

STATIC
VOID
DrawLogoScreen (
  VOID
  )
{
  CONST IMAGE  *Logo;
  CONST IMAGE  *Banner;

  GfxFill (0, 0, SCREEN_W, SCREEN_H, C_BLACK);
  Logo = DataImage (IMG_BOOT_LOGO);
  if (Logo != NULL) {
    // Same spot as BootLogoLib's centred splash, so there is no jump.
    GfxDrawImage (Logo, (SCREEN_W - Logo->Width) / 2, (SCREEN_H - Logo->Height) / 2);
  }

  Banner = DataImage (IMG_POST_BANNER);
  if (Banner != NULL) {
    GfxDrawImage (Banner, (SCREEN_W - Banner->Width) / 2, SCREEN_H - Banner->Height - 40);
  }
}

STATIC
VOID
DrawTextPost (
  VOID
  )
{
  CHAR16  Buf[160];
  CHAR16  Speed[48];

  GfxFill (0, 0, SCREEN_W, SCREEN_H, C_BLACK);
  GfxDrawImage (DataImage (IMG_AMI_LOGO), SCREEN_W - 420, 40);
  UnicodeSPrint (Buf, sizeof (Buf), L"%s  BIOS %s  (%s)", gSdb.Info->Model, gSdb.Info->BiosVersion, gSdb.Info->BiosDate);
  FontDraw (60, 60, Buf, RGB (0xD0, 0xD0, 0xD0), FONT_SCALE);
  UnicodeSPrint (Buf, sizeof (Buf), L"CPU : %s", gHw.CpuBrand);
  FontDraw (60, 120, Buf, RGB (0xD0, 0xD0, 0xD0), FONT_SCALE);
  HwDynText (SDB_DYN_CPU_SPEED, Speed, sizeof (Speed));
  UnicodeSPrint (Buf, sizeof (Buf), L"Speed : %s", Speed);
  FontDraw (60, 150, Buf, RGB (0xD0, 0xD0, 0xD0), FONT_SCALE);
  UnicodeSPrint (Buf, sizeof (Buf), L"Total Memory : %d MB (DDR5 %d MT/s)", gHw.MemSizeMb, gHw.MemMts);
  FontDraw (60, 180, Buf, RGB (0xD0, 0xD0, 0xD0), FONT_SCALE);
  FontDraw (60, SCREEN_H - 90, SdbNamed (SDB_STR_PRESS_DEL), RGB (0xD0, 0xD0, 0xD0), FONT_SCALE);
  FontDraw (60, SCREEN_H - 60, L"F12 : Boot Menu    End : Q-Flash", RGB (0xD0, 0xD0, 0xD0), FONT_SCALE);
}

STATIC
VOID
DrawPost (
  VOID
  )
{
  if (SdbSpecialValue (SDB_SPEC_FULL_LOGO, 1) != 0) {
    DrawLogoScreen ();
  } else {
    DrawTextPost ();
  }

  GfxPresent ();
}

//
// F12: "Please select boot device:" pop-up.
// Returns TRUE if a device was booted (and came back), FALSE for defaults.
//
STATIC
UINTN
BootMenu (
  VOID
  )
{
  CONST CHAR16  **Items;
  UINTN         Index;
  INTN          Pick;
  CHAR16        Title[64];

  BootRefresh ();
  Items = AllocateZeroPool ((gBootEntryCount + 1) * sizeof (CHAR16 *));
  if (Items == NULL) {
    return 0;
  }

  for (Index = 0; Index < gBootEntryCount; Index++) {
    Items[Index] = gBootEntries[Index].Name;
  }

  Items[gBootEntryCount] = SdbNamed (SDB_STR_ENTER_SETUP);
  StrCpyS (Title, ARRAY_SIZE (Title), SdbNamed (SDB_STR_SELECT_BOOT));
  DrawPost ();
  Pick = DlgMenu (Title, Items, gBootEntryCount + 1, 0, FALSE);
  FreePool (Items);
  if (Pick < 0) {
    return 0;                         // ESC: boot using defaults
  }

  if ((UINTN)Pick == gBootEntryCount) {
    return 2;                         // Enter Setup
  }

  BootTry ((UINTN)Pick);
  return 1;
}

STATIC
BOOLEAN
CheckPassword (
  BOOLEAN  EnteringSetup
  )
{
  CHAR16  Buf[24];
  UINTN   Tries;

  if ((gEmu.AdminPassword[0] == 0) && (gEmu.UserPassword[0] == 0)) {
    return TRUE;
  }

  //
  // "Security Option": Setup = ask only for setup, System = at every boot.
  //
  if (!EnteringSetup && (SdbSpecialValue (SDB_SPEC_SECURITY_OPTION, 1) == 0)) {
    return TRUE;
  }

  for (Tries = 0; Tries < 3; Tries++) {
    Buf[0] = 0;
    if (DlgInput (SdbNamed (SDB_STR_ENTER_PASSWORD), SdbNamed (SDB_STR_ENTER_PASSWORD), Buf, 20, TRUE, FALSE)) {
      if (((gEmu.AdminPassword[0] != 0) && (StrCmp (Buf, gEmu.AdminPassword) == 0)) ||
          ((gEmu.UserPassword[0] != 0) && (StrCmp (Buf, gEmu.UserPassword) == 0)))
      {
        return TRUE;
      }
    }

    DlgMessage (SdbNamed (SDB_STR_ERROR), SdbNamed (SDB_STR_INVALID_PASSWORD));
  }

  // Three strikes: halt like the real thing.
  GfxFill (0, 0, SCREEN_W, SCREEN_H, C_BLACK);
  FontDraw (60, 60, L"System Halted", RGB (0xD0, 0xD0, 0xD0), FONT_SCALE);
  GfxPresent ();
  CpuDeadLoop ();
  return FALSE;
}

STATIC
VOID
EnterSetup (
  VOID
  )
{
  UINTN  Result;

  if (!CheckPassword (TRUE)) {
    return;
  }

  Result = SetupRun ();
  if (Result == UI_EXIT_RESET) {
    Black ();
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  }
}

STATIC
VOID
NoBootDevice (
  VOID
  )
{
  KEY  Key;

  if (SdbSpecialValue (SDB_SPEC_CSM, 0) != 0) {
    //
    // CSM enabled: the legacy BIOS message nobody wants to see.
    //
    GfxFill (0, 0, SCREEN_W, SCREEN_H, C_BLACK);
    FontDrawWrapped (8, 8, SdbNamed (SDB_STR_NO_BOOT_DEVICE), RGB (0xC0, 0xC0, 0xC0), FONT_SCALE, SCREEN_W - 16, 28, 4);
    GfxPresent ();
    InputFlush ();
    while (!InputRead (&Key, MAX_UINTN)) {
    }

    if (Key.Scan == SCAN_DELETE) {
      EnterSetup ();
    }

    Black ();
    gRT->ResetSystem (EfiResetWarm, EFI_SUCCESS, 0, NULL);
  }

  //
  // UEFI only: with nothing to boot, AMI drops straight into setup.
  //
  EnterSetup ();
}

STATIC
VOID
BiosResetPrompt (
  VOID
  )
{
  CONST CHAR16  *Items[3];
  INTN          Pick;

  gEmu.ResetPending = 0;
  EmuStateSave ();
  Items[0] = SdbNamed (SDB_STR_RESET_BOOT);
  Items[1] = SdbNamed (SDB_STR_RESET_REBOOT);
  Items[2] = SdbNamed (SDB_STR_ENTER_BIOS);
  Pick     = DlgMenu (SdbNamed (SDB_STR_BIOS_RESET), Items, 3, 0, TRUE);
  if (Pick == 1) {
    Black ();
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  } else if (Pick == 2) {
    EnterSetup ();
  }
}

VOID
PostRun (
  BOOLEAN  FromBds
  )
{
  KEY    Key;
  UINTN  Waited;
  UINTN  Window;
  UINTN  Menu;

  if (FromBds) {
    MemoryTraining ();
  }

  gEmu.BootCount++;
  EmuStateSave ();
  DrawPost ();

  if (gEmu.ResetPending) {
    BiosResetPrompt ();
    DrawPost ();
  }

  //
  // Hot key window.  Fast Boot shortens it, like on the board.
  //
  Window = SdbSpecialValue (SDB_SPEC_FAST_BOOT, 1) ? 2500 : 4000;
  InputFlush ();
  for (Waited = 0; Waited < Window; Waited += 100) {
    if (!InputRead (&Key, 100)) {
      continue;
    }

    if ((Key.Scan == SCAN_DELETE) || (Key.Scan == SCAN_F2)) {
      EnterSetup ();
      break;
    }

    if (Key.Scan == SCAN_F12) {
      Menu = BootMenu ();
      if (Menu == 2) {
        EnterSetup ();
      }

      break;
    }

    if (Key.Scan == SCAN_END) {
      QFlashRun ();
      Black ();
      gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
    }
  }

  if (!CheckPassword (FALSE)) {
    return;
  }

  //
  // Try the boot devices; when nothing boots, behave like the board does.
  //
  for ( ; ;) {
    if (BootDefault ()) {
      // An OS loader ran and returned; show the logo again and retry.
      DrawPost ();
      continue;
    }

    NoBootDevice ();
    DrawPost ();
  }
}

VOID
PostBootMenu (
  VOID
  )
{
  if (BootMenu () == 2) {
    EnterSetup ();
  }
}
