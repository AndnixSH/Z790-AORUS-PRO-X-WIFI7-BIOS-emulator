# Z790 AORUS PRO X WIFI7 BIOS emulator

This project runs a GIGABYTE **Z790 AORUS PRO X WIFI7** in QEMU, rebuilt from the
real BIOS image that you download from GIGABYTE. The first boot looks like this:

1. **Memory training.** After a CMOS clear the board powers itself off and on
   several times, like a real board.
2. **POST.** The AORUS eagle and the POST banner appear, with DEL, F12 and End
   hot keys.
3. **BIOS reset prompt.** "BIOS has been reset - Please decide how to continue"
   appears after a CMOS clear.
4. **No operating system.** The board finds no OS and drops into setup.
5. **Setup.** You get the GIGABYTE UEFI setup with **Easy Mode** and **Advanced
   Mode**. It has every menu, option, default, help text and translation of BIOS
   F9a, and you can interact with all of it.

The setup has these working parts:

- Settings are saved to NVRAM.
- Options appear and grey out by the board's own rules.
- Favorites, Option Search, profiles and passwords.
- Smart Fan 6, Q-Flash and the F12 boot menu.
- F12 screenshots to a USB stick.
- It can still boot a real OS if you give it a disk.

```sh
scripts/build.sh      # downloads BIOS F9a from GIGABYTE, extracts it, builds the firmware
scripts/run.sh        # powers on the board in QEMU
```

## Why not just run the real BIOS in QEMU?

The real image doesn't get past its first few hundred instructions. QEMU
emulates a generic PC: a 2007 Q35/ICH9 or a 1996 i440FX chipset. AMI Aptio V for
Raptor Lake expects real Intel silicon. The image was tested on both QEMU
machine types:

| Machine | What the GIGABYTE image does                                                                                                                                         |
|---------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `q35`   | From the reset vector it reads PCIEXBAR (`0xCF8 ← 0x80000060`) and finds it already set, as after a warm reset. It then writes `6` to port `0xCF9` (full reset). The machine resets forever. |
| `pc`    | It gets further. It programs the MTRRs for cache-as-RAM in `TempRamInit`, then spins forever in a `CPUID` leaf 4 loop that waits for cache topology QEMU doesn't report. |

Even past those two checks, the image would still need hardware that QEMU
doesn't have:

- the Intel FSP-M DDR5 memory controller, for memory training
- the CSME / HECI handshake
- the Z790 PCH: GPIO, PMC, SPI controller, P2SB, eSPI
- the Boot Guard ACM and microcode for the exact CPU

Running the real binary would mean writing a Raptor Lake + Z790 platform model
for QEMU. That is a multi-year project.

So this project **re-creates the board from its own data**:

- An open-source UEFI (EDK2/OVMF) does the real work: PCI, USB, disks, booting.
- A new front end, written for this project, replaces the OVMF setup UI.
- That front end runs everything it shows from data extracted from the GIGABYTE
  image: the setup forms, strings, defaults, visibility rules, pictures, font
  and logos.

## Requirements

You need Linux (or WSL2) with:

- QEMU (`qemu-system-x86_64`)
- a C toolchain and NASM
- `iasl`, git and Python 3 with Pillow
- DejaVu Sans (optional; it fills in glyphs the GIGABYTE font lacks)

On Debian or Ubuntu:

```sh
sudo apt install build-essential uuid-dev nasm acpica-tools git python3 python3-pil \
                 fonts-dejavu-core qemu-system-x86
```

The build downloads about 10 MB from GIGABYTE and clones EDK2, which is over
1 GB with submodules. It then builds OVMF, which takes a few minutes. KVM
(`/dev/kvm`) makes the emulator much faster but isn't required.

**No toolchain?** If you have Docker, use `scripts/docker-build.sh` instead of
`scripts/build.sh`. It takes the same options. You still need QEMU on the host
to run the result.

## Building

```sh
scripts/build.sh                       # BIOS F9a straight from GIGABYTE
scripts/build.sh --bios ~/Downloads/Z790APROXWIFI7.F9a   # a BIOS file you already have
BIOS_VERSION=fXX scripts/build.sh      # another release from the support page (untested)
```

The build runs these steps:

1. `tools/fetch_bios.py` downloads `mb_bios_z790-a-pro-x-wifi7_8arpt709_f9a.zip`
   from GIGABYTE and checks it against a known SHA-256.
2. `tools/extract.py` parses the image and writes `build/data/`. It handles the
   flash descriptor, firmware volumes, LZMA sections, AMI NVAR defaults, and
   the HII forms and strings.
3. EDK2 is cloned into `build/edk2` (`EDK2_DIR` and `EDK2_TAG` override this).
4. `firmware/patch_ovmf.py` patches OVMF. It is idempotent and only adds things.
5. OVMF is built with the front end.

The result is `build/firmware/AORUS_CODE.fd` (the "flash chip") and
`AORUS_VARS_BLANK.fd` (an empty CMOS/NVRAM).

## Running

```sh
scripts/run.sh                   # power on
scripts/run.sh --clear-cmos      # clear CMOS first: memory training + "BIOS has been reset"
scripts/run.sh --disk win11.qcow2  # with an NVMe disk; a bootable OS will boot
scripts/run.sh --cdrom ubuntu.iso
scripts/run.sh --help            # all options
```

`run.sh` takes these options:

| Option                   | Effect                                                                                                                           |
|--------------------------|----------------------------------------------------------------------------------------------------------------------------------|
| `--clear-cmos`           | Deletes `build/firmware/AORUS_VARS.fd`. The next power-on trains memory (3 power cycles) and shows the BIOS reset prompt.         |
| `--mem 32G`              | Sets the RAM size (default 8G). It appears on the DRAM pages.                                                                     |
| `--kvm` / `--no-kvm`     | Turns hardware acceleration on or off. By default it is used when `/dev/kvm` is writable.                                        |
| `--host-cpu`             | With KVM, shows your real CPU instead of an i9-14900K.                                                                            |
| `--disk FILE`            | Attaches a disk as an NVMe drive.                                                                                                 |
| `--cdrom FILE`           | Attaches an ISO.                                                                                                                  |
| `--usb DIR`              | Sets the folder presented as a FAT USB stick (default `build/usb`). F12 screenshots and Q-Flash use it.                          |
| `--fullscreen`           | Starts full screen. The UI is 1920×1080 and scales to the window.                                                                 |
| `--headless`             | Runs with no window and a QMP socket at `build/qmp.sock`, or at `$QMP_SOCK` (see *Testing*).                                      |
| `-- ARGS`                | Passes the rest of the arguments to QEMU.                                                                                         |

The NVRAM (`build/firmware/AORUS_VARS.fd`) keeps your settings between runs,
like the CMOS of a real board. The settings use the real AMI variable names and
GUIDs (`Setup`, `CpuSetup`, `SaSetup`, `PchSetup`, …).

## Using it

### Power-on

| Situation                         | What happens                                                                                     |
|-----------------------------------|--------------------------------------------------------------------------------------------------|
| First boot or CMOS cleared        | The screen stays black and the machine resets 3 times (memory training). Then the logo and the BIOS reset prompt appear. |
| Memory settings changed and saved | One training power cycle.                                                                        |
| Normal boot                       | AORUS logo and banner. The hot-key window lasts 4 s, or 2.5 s with Fast Boot.                    |

These keys work during POST. You can press them early, as people do on real
hardware.

| Key        | Action                                                          |
|------------|-----------------------------------------------------------------|
| DEL        | Enter setup                                                     |
| F12        | Boot menu ("Please select boot device:", plus "Enter Setup")    |
| End        | Q-Flash                                                         |

With no bootable device, the board enters setup on its own. If **CSM Support**
is enabled, you get the classic "Reboot and Select proper Boot device" text
screen instead. With **Full Screen LOGO Show** disabled, POST is the text-mode
screen.

### Setup keys

Most keys work in both modes. F2 switches between them.

| Key              | Action                                                                                  |
|------------------|-----------------------------------------------------------------------------------------|
| ← →              | Change tab (Advanced Mode)                                                              |
| ↑ ↓              | Move                                                                                    |
| Enter            | Open a menu or option, or confirm                                                       |
| + / - / PgUp / PgDn | Change the value                                                                     |
| ESC              | Go back, or exit without saving                                                         |
| F1               | General help                                                                            |
| F2               | Switch between Easy Mode and Advanced Mode                                              |
| F3 / F4          | Save / load a profile (8 slots, kept in NVRAM)                                          |
| F5               | Previous values                                                                         |
| F6               | Smart Fan 6                                                                             |
| F7               | Load optimized defaults                                                                 |
| F8               | Q-Flash                                                                                 |
| F9               | System information                                                                      |
| F10              | Save & exit (reboots)                                                                   |
| F11              | Favorites                                                                               |
| F12              | Screenshot: a BMP saved to the USB stick                                                |
| Insert           | Add or remove an item from Favorites                                                    |
| Alt+F            | Option Search                                                                           |
| Ctrl+S           | SPD info                                                                                |

### What's in there

- **Advanced Mode** has every tab of the real board: Favorites, Tweaker,
  Settings, System Info., Boot and Save & Exit. Each tab has the board's menus,
  options, defaults and help texts. The right-hand monitor panel shows live
  CPU, memory and voltage readings.
- **Options follow the board's rules.** Items appear and grey out by the
  `suppressif`/`grayoutif` rules in the GIGABYTE image. For example, enabling
  CSM Support reveals the legacy options. The rules also read runtime state
  that the real BIOS detects, such as the CPU's capabilities. A fixed profile
  for an i9-14900K with integrated graphics stands in for that state
  (`VOLATILE_PROFILE` in `tools/layout.py`).
- **Easy Mode** shows Information, DRAM Status, PC Health, Smart Fan 6, X.M.P.,
  GIGABYTE PerfDrive, Boot Sequence, SPD Setup and Memory Boot Mode.
- **12 languages**, the same set as the board. Switch with the Language button
  in Easy Mode or with System Info. → System Language. Strings the board leaves
  untranslated stay in English, as on the real board.
- **Smart Fan 6** has the fan speed control options and the preset curve graphs.
- **Q-Flash** lists the files on the USB stick and checks that a file is a BIOS
  for this board (size and BIOS ID). It then runs a *simulated* update. Nothing
  is written.
- **Boot** shows the real UEFI boot options of the virtual machine. You can
  reorder them, use Boot Override, or boot the EFI shell. An OS you boot sees
  the board's SMBIOS identity: Gigabyte Technology Co., Ltd., Z790 AORUS PRO X
  WIFI7.
- **Passwords:** set an Administrator or User password and turn on **Security
  Option = System** to get a password prompt at POST.

## How it works

```
GIGABYTE zip ─ fetch_bios.py ─▶ bios.bin
                                   │ extract.py
                                   │  ├ uefi_image.py   descriptor, FVs, FFS, LZMA, NVAR defaults
                                   │  ├ hii.py          IFR forms + string packages
                                   │  ├ sdb.py          compile forms → setup.sdb (+ layout.py)
                                   │  └ assets.py       PNG/JPEG/BMP → gui.pak, font → font.bin
                                   ▼
               build/data: setup.sdb  gui.pak  font.bin  Logo.bmp
                                   │
EDK2 OVMF ◀── patch_ovmf.py ───────┤  data files embedded in the firmware volume
   │   BDS → LaunchAorusFrontEnd() ▼
   └─▶ AorusBios.efi (firmware/AorusPkg) — POST, setup, Q-Flash, boot menu
```

- **`setup.sdb`** is a compact compiled copy of the AMI setup:
  - forms, statements, options and strings
  - variable stores with the factory defaults
  - every visibility condition, compiled to bytecode

  GIGABYTE regroups AMI's forms into its own tabs, and that grouping is
  hard-coded in their setup binary. `tools/layout.py` re-creates it.
  `docs/ASSETS.md` describes every file pulled from the image.
- **`AorusBios.efi`** replaces OVMF's UiApp. It draws everything itself into a
  1920×1080 back buffer: the GIGABYTE pictures, the GIGABYTE font, the dialogs
  and the light theme. It stores settings in the same NVRAM variables the real
  BIOS uses.
- **OVMF** does the real firmware work: PCI, USB, NVMe, FAT, boot options,
  reset and NVRAM. The patch makes BDS start the front end with `POST` as its
  argument once the console is up. It also sets the display to 1920×1080.

`tools/explain.py "Prompt text"` shows why an item is visible, hidden or greyed
out. It evaluates the board's conditions with the values each one reads.

## Testing

`scripts/qmp_drive.py` drives a headless instance through QMP:

```sh
scripts/run.sh --headless --mem 4G &
scripts/qmp_drive.py --qmp build/qmp.sock wait:12 key:delete wait:5 shot:setup.png \
    key:f2 wait:3 key:right shot:settings.png
```

## Limitations

- **Sensors are simulated.** Temperatures, voltages and fan speeds drift
  plausibly. CPU and memory values come from QEMU: CPUID, the memory map and an
  emulated i9-14900K identity.
- **Settings don't change the virtual hardware.** Overclocking, voltages and
  the like are stored but have no effect. QEMU has nothing to overclock.
- **Q-Flash is simulated.** "Save BIOS" isn't emulated, and an update never
  writes the flash.
- **The layout is approximated** from photos and screenshots of the real board.
  Pixel positions and spacing differ slightly.
- **Some GIGABYTE tools are reduced.** Smart Fan 6 shows the preset curves but
  has no curve editor.
- **No CSM/legacy boot.** Enabling CSM changes the menus and the no-OS screen,
  but OVMF can only boot UEFI.
- **Only BIOS F9a is tested.** Other releases of this board should work, since
  the extraction is generic. `fetch_bios.py` only knows the checksum of F9a and
  prints a note for other versions.

## Legal

This repository contains **no GIGABYTE or AMI code, data or artwork**. The BIOS
is downloaded by you, from GIGABYTE, at build time. Everything taken from it
stays in your `build/` directory and in the firmware you build, which is for
personal use only; don't redistribute it. GIGABYTE, AORUS and their logos are
trademarks of GIGABYTE Technology. AMI and Aptio are trademarks of American
Megatrends International. This project is not affiliated with either company.

The code in this repository is MIT licensed (see `LICENSE`). EDK2, which the
build downloads, is BSD-2-Clause-Patent.
