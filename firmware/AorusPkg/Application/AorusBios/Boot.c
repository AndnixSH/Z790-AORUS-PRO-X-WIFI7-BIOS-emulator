/** @file
  Boot devices: the real UEFI boot options that OVMF discovered (disks,
  CD-ROMs, the EFI shell, ...), presented and ordered like an AMI BIOS.

  SPDX-License-Identifier: MIT
**/

#include "AorusBios.h"

BOOT_ENTRY  *gBootEntries;
UINTN       gBootEntryCount;

STATIC EFI_BOOT_MANAGER_LOAD_OPTION  *mOptions;
STATIC UINTN                         mOptionCount;

STATIC
BOOLEAN
IsOwnApp (
  EFI_BOOT_MANAGER_LOAD_OPTION  *Option
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  EFI_GUID                  *FileGuid;

  //
  // Hide boot options that point back into firmware setup (ourselves).
  //
  for (Node = Option->FilePath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    FileGuid = EfiGetNameGuidFromFwVolDevicePathNode ((MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *)Node);
    if ((FileGuid != NULL) && (CompareGuid (FileGuid, &gEfiCallerIdGuid) || CompareGuid (FileGuid, PcdGetPtr (PcdBootManagerMenuFile)))) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
VOID
MakeName (
  EFI_BOOT_MANAGER_LOAD_OPTION  *Option,
  CHAR16                        *Name,
  UINTN                         Size
  )
{
  CONST CHAR16  *D;
  UINTN         Len;

  D = Option->Description != NULL ? Option->Description : L"Unknown";
  //
  // OVMF names options "UEFI QEMU HARDDISK QM00001 "; AMI shows "UEFI: ...".
  //
  if (StrnCmp (D, L"UEFI ", 5) == 0) {
    UnicodeSPrint (Name, Size, L"UEFI: %s", D + 5);
  } else {
    StrCpyS (Name, Size / sizeof (CHAR16), D);
  }

  Len = StrLen (Name);
  while (Len > 0 && Name[Len - 1] == L' ') {
    Name[--Len] = 0;
  }
}

VOID
BootRefresh (
  VOID
  )
{
  UINTN       Index;
  UINTN       J;
  UINTN       Count;
  BOOT_ENTRY  Tmp;

  if (mOptions != NULL) {
    EfiBootManagerFreeLoadOptions (mOptions, mOptionCount);
    mOptions = NULL;
  }

  if (gBootEntries != NULL) {
    FreePool (gBootEntries);
    gBootEntries = NULL;
  }

  gBootEntryCount = 0;
  mOptions        = EfiBootManagerGetLoadOptions (&mOptionCount, LoadOptionTypeBoot);
  if (mOptions == NULL) {
    return;
  }

  gBootEntries = AllocateZeroPool (mOptionCount * sizeof (BOOT_ENTRY));
  if (gBootEntries == NULL) {
    return;
  }

  for (Index = 0; Index < mOptionCount; Index++) {
    if (((mOptions[Index].Attributes & LOAD_OPTION_ACTIVE) == 0) ||
        ((mOptions[Index].Attributes & LOAD_OPTION_HIDDEN) != 0) || IsOwnApp (&mOptions[Index]))
    {
      continue;
    }

    gBootEntries[gBootEntryCount].Number = (UINT16)mOptions[Index].OptionNumber;
    gBootEntries[gBootEntryCount].Option = &mOptions[Index];
    MakeName (&mOptions[Index], gBootEntries[gBootEntryCount].Name, sizeof (gBootEntries[0].Name));
    gBootEntryCount++;
  }

  //
  // Apply the user's priority (Boot tab / Easy Mode boot sequence).
  //
  Count = 0;
  for (Index = 0; Index < gEmu.BootPriorityCount; Index++) {
    for (J = Count; J < gBootEntryCount; J++) {
      if (gBootEntries[J].Number == gEmu.BootPriority[Index]) {
        Tmp                  = gBootEntries[Count];
        gBootEntries[Count]  = gBootEntries[J];
        gBootEntries[J]      = Tmp;
        Count++;
        break;
      }
    }
  }
}

VOID
BootApplyPriority (
  VOID
  )
{
  UINTN   Index;
  UINT16  *Order;
  UINTN   OrderSize;
  UINT16  *NewOrder;
  UINTN   N;
  UINTN   J;
  UINTN   K;

  gEmu.BootPriorityCount = (UINT16)MIN (gBootEntryCount, ARRAY_SIZE (gEmu.BootPriority));
  for (Index = 0; Index < gEmu.BootPriorityCount; Index++) {
    gEmu.BootPriority[Index] = gBootEntries[Index].Number;
  }

  //
  // Mirror it into the UEFI BootOrder, keeping options we do not show.
  //
  GetEfiGlobalVariable2 (L"BootOrder", (VOID **)&Order, &OrderSize);
  if (Order == NULL) {
    return;
  }

  N        = OrderSize / sizeof (UINT16);
  NewOrder = AllocatePool (OrderSize);
  if (NewOrder == NULL) {
    FreePool (Order);
    return;
  }

  K = 0;
  for (Index = 0; Index < gBootEntryCount; Index++) {
    for (J = 0; J < N; J++) {
      if (Order[J] == gBootEntries[Index].Number) {
        NewOrder[K++] = Order[J];
        Order[J]      = 0xFFFF;
        break;
      }
    }
  }

  for (J = 0; J < N && K < N; J++) {
    if (Order[J] != 0xFFFF) {
      NewOrder[K++] = Order[J];
    }
  }

  gRT->SetVariable (
         L"BootOrder",
         &gEfiGlobalVariableGuid,
         EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
         K * sizeof (UINT16),
         NewOrder
         );
  FreePool (NewOrder);
  FreePool (Order);
}

BOOLEAN
BootTry (
  IN UINTN  Index
  )
{
  EFI_BOOT_MANAGER_LOAD_OPTION  *Option;

  if (Index >= gBootEntryCount) {
    return FALSE;
  }

  Option = gBootEntries[Index].Option;
  GfxFill (0, 0, SCREEN_W, SCREEN_H, C_BLACK);
  GfxPresent ();
  gST->ConOut->ClearScreen (gST->ConOut);
  EfiBootManagerBoot (Option);
  //
  // Back here: the loader failed or returned.  Redraw is the caller's job.
  //
  return !EFI_ERROR (Option->Status);
}

BOOLEAN
BootDefault (
  VOID
  )
{
  UINTN  Index;

  BootRefresh ();
  for (Index = 0; Index < gBootEntryCount; Index++) {
    //
    // Applications (the EFI shell) only start when picked explicitly,
    // like a "Launch EFI Shell" entry on a real board.
    //
    if ((gBootEntries[Index].Option->Attributes & LOAD_OPTION_CATEGORY) == LOAD_OPTION_CATEGORY_APP) {
      continue;
    }

    if (BootTry (Index)) {
      return TRUE;
    }
  }

  return FALSE;
}
