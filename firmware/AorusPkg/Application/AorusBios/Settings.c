/** @file
  Persistent settings ("CMOS"): the AMI setup variables plus the emulator's
  own state, stored in the OVMF NVRAM (the QEMU vars file).

  The setup variables are stored under their real names and GUIDs
  ("Setup", "CpuSetup", "SaSetup", ...), exactly like on the board.

  SPDX-License-Identifier: MIT
**/

#include "AorusBios.h"

AORUS_EMU_STATE  gEmu;
BOOLEAN          gCmosCleared;

#define EMU_VAR_NAME  L"AorusEmuState"
#define NV_ATTRS      (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)

STATIC
BOOLEAN
IsPersistent (
  CONST SDB_VAR  *V
  )
{
  // Standard UEFI globals (BootOrder, PlatformLang, ...) are owned by the
  // firmware core; only the vendor setup stores are ours to write.
  return (V->Flags & SDB_VAR_HAS_DEFAULTS) != 0 && !CompareGuid (&V->Guid, &gEfiGlobalVariableGuid);
}

STATIC
VOID
DefaultFavorites (
  VOID
  )
{
  UINT32  Index;
  UINTN   J;

  gEmu.FavCount = 0;
  for (Index = 0; Index < gSdb.StmtCount && gEmu.FavCount < MAX_FAVORITES; Index++) {
    SDB_STMT  *S = &gSdb.Stmts[Index];
    if (((S->Flags & SDB_SF_FAVORITE_DEFAULT) == 0) || (S->QuestionId == 0)) {
      continue;
    }

    for (J = 0; J < gEmu.FavCount; J++) {
      if (gEmu.Favorites[J] == S->QuestionId) {
        break;
      }
    }

    if (J == gEmu.FavCount) {
      gEmu.Favorites[gEmu.FavCount++] = S->QuestionId;
    }
  }
}

STATIC
VOID
EmuStateDefaults (
  VOID
  )
{
  ZeroMem (&gEmu, sizeof (gEmu));
  gEmu.Signature = AORUS_EMU_SIGNATURE;
  gEmu.Version   = AORUS_EMU_VERSION;
  gEmu.Language  = (UINT8)gSdb.EnLang;
  gEmu.LastMode  = 1;
  DefaultFavorites ();
}

VOID
EmuStateSave (
  VOID
  )
{
  gRT->SetVariable (EMU_VAR_NAME, &gAorusEmuVariableGuid, NV_ATTRS, sizeof (gEmu), &gEmu);
}

VOID
SettingsLoadDefaults (
  VOID
  )
{
  UINT32  Index;

  for (Index = 0; Index < gSdb.VarCount; Index++) {
    if ((gSdb.Vars[Index].Flags & SDB_VAR_HAS_DEFAULTS) != 0) {
      CopyMem (gSdb.Cur[Index], gSdb.VarDefaults + gSdb.Vars[Index].DataOffset, gSdb.Vars[Index].Size);
    }
  }
}

EFI_STATUS
SettingsLoad (
  VOID
  )
{
  UINT32      Index;
  UINTN       Size;
  EFI_STATUS  Status;
  BOOLEAN     Missing;

  Missing = FALSE;
  for (Index = 0; Index < gSdb.VarCount; Index++) {
    SDB_VAR  *V = &gSdb.Vars[Index];
    if (!IsPersistent (V)) {
      continue;
    }

    Size   = V->Size;
    Status = gRT->GetVariable (V->Name, &V->Guid, NULL, &Size, gSdb.Cur[Index]);
    if (EFI_ERROR (Status) || (Size != V->Size)) {
      CopyMem (gSdb.Cur[Index], gSdb.VarDefaults + V->DataOffset, V->Size);
      Missing = TRUE;
    }
  }

  Size   = sizeof (gEmu);
  Status = gRT->GetVariable (EMU_VAR_NAME, &gAorusEmuVariableGuid, NULL, &Size, &gEmu);
  if (EFI_ERROR (Status) || (Size != sizeof (gEmu)) || (gEmu.Signature != AORUS_EMU_SIGNATURE) ||
      (gEmu.Version != AORUS_EMU_VERSION))
  {
    EmuStateDefaults ();
    Missing = TRUE;
  }

  //
  // No setup variables = a brand new board or a cleared CMOS.
  //
  gCmosCleared = Missing;
  if (gEmu.Language < gSdb.LangCount) {
    gSdb.Lang = gEmu.Language;
  }

  SettingsSnapshot ();
  return EFI_SUCCESS;
}

EFI_STATUS
SettingsSave (
  VOID
  )
{
  UINT32      Index;
  EFI_STATUS  Status;
  EFI_STATUS  Result;

  Result = EFI_SUCCESS;
  for (Index = 0; Index < gSdb.VarCount; Index++) {
    SDB_VAR  *V = &gSdb.Vars[Index];
    if (!IsPersistent (V)) {
      continue;
    }

    Status = gRT->SetVariable (V->Name, &V->Guid, NV_ATTRS, V->Size, gSdb.Cur[Index]);
    if (EFI_ERROR (Status)) {
      Result = Status;
    }
  }

  gEmu.Language = (UINT8)gSdb.Lang;
  EmuStateSave ();
  SettingsSnapshot ();
  return Result;
}

VOID
SettingsSnapshot (
  VOID
  )
{
  UINT32  Index;

  for (Index = 0; Index < gSdb.VarCount; Index++) {
    CopyMem (gSdb.Prev[Index], gSdb.Cur[Index], gSdb.Vars[Index].Size);
  }
}

VOID
SettingsRestoreSnapshot (
  VOID
  )
{
  UINT32  Index;

  for (Index = 0; Index < gSdb.VarCount; Index++) {
    CopyMem (gSdb.Cur[Index], gSdb.Prev[Index], gSdb.Vars[Index].Size);
  }
}

BOOLEAN
SettingsDirty (
  VOID
  )
{
  UINT32  Index;

  for (Index = 0; Index < gSdb.VarCount; Index++) {
    if (CompareMem (gSdb.Prev[Index], gSdb.Cur[Index], gSdb.Vars[Index].Size) != 0) {
      return TRUE;
    }
  }

  return FALSE;
}

UINT32
SettingsMemoryHash (
  VOID
  )
{
  STATIC CONST UINT16  Keys[] = {
    SDB_SPEC_XMP,          SDB_SPEC_MEM_MULTIPLIER, SDB_SPEC_XMP_BOOSTER,
    SDB_SPEC_HIGH_BANDWIDTH, SDB_SPEC_LOW_LATENCY,  SDB_SPEC_MEM_BOOT_MODE,
  };
  UINT64  Values[ARRAY_SIZE (Keys) + 1];
  UINTN   Index;
  UINT32  Crc;

  for (Index = 0; Index < ARRAY_SIZE (Keys); Index++) {
    Values[Index] = SdbSpecialValue (Keys[Index], 0);
  }

  Values[ARRAY_SIZE (Keys)] = gHw.MemSizeMb;
  Crc                       = 0;
  gBS->CalculateCrc32 (Values, sizeof (Values), &Crc);
  return Crc | 1;                     // never 0, so 0 means "never trained"
}

BOOLEAN
FavoriteHas (
  UINT16  QuestionId
  )
{
  UINTN  Index;

  for (Index = 0; Index < gEmu.FavCount; Index++) {
    if (gEmu.Favorites[Index] == QuestionId) {
      return TRUE;
    }
  }

  return FALSE;
}

VOID
FavoriteToggle (
  UINT16  QuestionId
  )
{
  UINTN  Index;

  if (QuestionId == 0) {
    return;
  }

  for (Index = 0; Index < gEmu.FavCount; Index++) {
    if (gEmu.Favorites[Index] == QuestionId) {
      CopyMem (&gEmu.Favorites[Index], &gEmu.Favorites[Index + 1], (gEmu.FavCount - Index - 1) * sizeof (UINT16));
      gEmu.FavCount--;
      EmuStateSave ();
      return;
    }
  }

  if (gEmu.FavCount < MAX_FAVORITES) {
    gEmu.Favorites[gEmu.FavCount++] = QuestionId;
    EmuStateSave ();
  }
}
