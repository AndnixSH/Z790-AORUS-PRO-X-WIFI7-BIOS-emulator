#!/usr/bin/env python3
"""Apply the AORUS changes to an OVMF (EDK2) tree.

Kept as a script instead of a .patch so that it is easy to read and robust
against unrelated upstream changes; every edit is anchored on a snippet and
fails loudly if the anchor is missing.  Files keep their CRLF line endings.

  firmware/patch_ovmf.py /path/to/edk2
"""

import os
import sys

MARK = 'AORUS BIOS emulator'


def edit(root, rel, old, new):
    path = os.path.join(root, rel)
    with open(path, 'r', newline='') as f:
        text = f.read()
    crlf = '\r\n' in text
    if crlf:
        old = old.replace('\n', '\r\n')
        new = new.replace('\n', '\r\n')
    if new in text:
        return False                       # already applied
    if old not in text:
        sys.exit('patch_ovmf: anchor not found in %s:\n%s' % (rel, old))
    with open(path, 'w', newline='') as f:
        f.write(text.replace(old, new, 1))
    return True


DSC_OLD = """  MdeModulePkg/Application/UiApp/UiApp.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/DeviceManagerUiLib/DeviceManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootManagerUiLib/BootManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootMaintenanceManagerUiLib/BootMaintenanceManagerUiLib.inf
  }
  MdeModulePkg/Application/BootManagerMenuApp/BootManagerMenuApp.inf
"""
DSC_NEW = """  #
  # AORUS BIOS emulator: replaces UiApp (same FILE_GUID) and the boot
  # manager menu, so every way into "setup" ends up in the GIGABYTE UI.
  #
  AorusPkg/Application/AorusBios/AorusBios.inf
"""

MENU_OLD = ("gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0xdc, 0x5b, 0xc2, 0xee, 0xf2, 0x67, "
            "0x95, 0x4d, 0xb1, 0xd5, 0xf8, 0x1b, 0x20, 0x39, 0xd1, 0x1d }")
MENU_NEW = ("gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x21, 0xaa, 0x2c, 0x46, 0x14, 0x76, "
            "0x03, 0x45, 0x83, 0x6e, 0x8a, 0xb6, 0xf4, 0x66, 0x23, 0x31 }")

FDF_OLD = """INF  MdeModulePkg/Application/UiApp/UiApp.inf
INF  MdeModulePkg/Application/BootManagerMenuApp/BootManagerMenuApp.inf
"""
FDF_NEW = """#
# AORUS BIOS emulator front-end and the data extracted from the GIGABYTE
# BIOS image (tools/extract.py).  Each data file is LZMA-compressed on its
# own and only decompressed when the application reads it.
#
INF  AorusPkg/Application/AorusBios/AorusBios.inf
FILE FREEFORM = EEDBBA8A-618B-4702-B740-46D23545D0E3 {
  SECTION GUIDED EE4E5898-3914-4259-9D6E-DC7BD79403CF PROCESSING_REQUIRED = TRUE {
    SECTION RAW = AorusPkg/Data/setup.sdb
  }
}
FILE FREEFORM = 0E3C992A-A5C9-46D5-977D-018DC01B5C2C {
  SECTION GUIDED EE4E5898-3914-4259-9D6E-DC7BD79403CF PROCESSING_REQUIRED = TRUE {
    SECTION RAW = AorusPkg/Data/gui.pak
  }
}
FILE FREEFORM = 2D07B092-A565-48E5-9629-72DC2DD0D6E4 {
  SECTION GUIDED EE4E5898-3914-4259-9D6E-DC7BD79403CF PROCESSING_REQUIRED = TRUE {
    SECTION RAW = AorusPkg/Data/font.bin
  }
}
"""

BDS_INCLUDE_OLD = '#include "BdsPlatform.h"\n'
BDS_INCLUDE_NEW = '#include "BdsPlatform.h"\n#include <Protocol/LoadedImage.h>\n'

BDS_CALL_OLD = """  RemoveStaleFvFileOptions ();
  SetBootOrderFromQemu ();

  PlatformBmPrintScRegisterHandler ();
}
"""
BDS_CALL_NEW = """  RemoveStaleFvFileOptions ();
  SetBootOrderFromQemu ();

  PlatformBmPrintScRegisterHandler ();

  //
  // AORUS BIOS emulator: hand the machine to the GIGABYTE-style front-end
  // (POST screen, hot keys, boot devices and setup are handled there).
  //
  LaunchAorusFrontEnd ();
}
"""

BDS_FUNC_OLD = """/**
  Do the platform specific action after the console is ready
"""
BDS_FUNC_NEW = """/**
  AORUS BIOS emulator: start the front-end application (it takes UiApp's
  file GUID) from our own firmware volume, passing L"POST" so that it shows
  the power-on screen instead of going straight to setup.
**/
STATIC
VOID
LaunchAorusFrontEnd (
  VOID
  )
{
  EFI_STATUS                         Status;
  EFI_LOADED_IMAGE_PROTOCOL          *BdsImage;
  EFI_LOADED_IMAGE_PROTOCOL          *AppImage;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  FileNode;
  EFI_DEVICE_PATH_PROTOCOL           *DevicePath;
  EFI_HANDLE                         Handle;
  STATIC CHAR16                      PostOption[] = L"POST";

  Status = gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&BdsImage);
  if (EFI_ERROR (Status)) {
    return;
  }

  EfiInitializeFwVolDevicepathNode (&FileNode, &gUiAppFileGuid);
  DevicePath = AppendDevicePathNode (
                 DevicePathFromHandle (BdsImage->DeviceHandle),
                 (EFI_DEVICE_PATH_PROTOCOL *)&FileNode
                 );
  if (DevicePath == NULL) {
    return;
  }

  Status = gBS->LoadImage (TRUE, gImageHandle, DevicePath, NULL, 0, &Handle);
  FreePool (DevicePath);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: AORUS front-end not found: %r\\n", __func__, Status));
    return;
  }

  if (!EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiLoadedImageProtocolGuid, (VOID **)&AppImage))) {
    AppImage->LoadOptions     = PostOption;
    AppImage->LoadOptionsSize = sizeof (PostOption);
  }

  gBS->StartImage (Handle, NULL, NULL);
}

/**
  Do the platform specific action after the console is ready
"""


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = sys.argv[1]
    changed = []
    for rel, old, new in (
        ('OvmfPkg/OvmfPkgX64.dsc', DSC_OLD, DSC_NEW),
        ('OvmfPkg/OvmfPkgX64.dsc', MENU_OLD, MENU_NEW),
        ('OvmfPkg/OvmfPkgX64.fdf', FDF_OLD, FDF_NEW),
        ('OvmfPkg/Library/PlatformBootManagerLib/BdsPlatform.c', BDS_INCLUDE_OLD, BDS_INCLUDE_NEW),
        ('OvmfPkg/Library/PlatformBootManagerLib/BdsPlatform.c', BDS_FUNC_OLD, BDS_FUNC_NEW),
        ('OvmfPkg/Library/PlatformBootManagerLib/BdsPlatform.c', BDS_CALL_OLD, BDS_CALL_NEW),
    ):
        if edit(root, rel, old, new):
            changed.append(rel)
    # GIGABYTE's GUI is drawn for 1920x1080.
    for which in ('Video', 'SetupVideo'):
        for axis, old, new in (('Horizontal', '1280', '1920'), ('Vertical', '800', '1080')):
            pcd = 'gEfiMdeModulePkgTokenSpaceGuid.Pcd%s%sResolution|' % (which, axis)
            if edit(root, 'OvmfPkg/Include/Dsc/OvmfDisplayPcds.dsc.inc', pcd + old, pcd + new):
                changed.append('OvmfDisplayPcds.dsc.inc')
    print('patch_ovmf: %s' % (', '.join(sorted(set(changed))) if changed else 'already applied'))


if __name__ == '__main__':
    main()
