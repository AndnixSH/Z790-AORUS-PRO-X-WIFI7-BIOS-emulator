# Where the pictures, font and menus come from

Nothing in this repository contains GIGABYTE or AMI artwork. `tools/extract.py`
pulls everything out of the BIOS image you download (`build/bios/bios.bin`) and
writes it to `build/data/`. The build then embeds those files in the firmware
volume. Both `build/` and `firmware/AorusPkg/Data/` are git-ignored.

## Firmware files used

Two kinds of files sit in the GIGABYTE image (BIOS F9a, ID `8ARPT709`):

- GIGABYTE's own files, which hold the GUI and are found by FFS GUID
- AMI modules, which are found by their UI name

| File (FFS GUID or UI name)               | Contents                                                                                   | Output                       |
|------------------------------------------|--------------------------------------------------------------------------------------------|------------------------------|
| `Setup` (largest copy)                   | HII form package (290 forms) and string packages in 12 languages                           | `setup.sdb`                  |
| `AMITSE`                                 | String package with the setup browser's own texts (Yes/No, date and time names, dialogs)   | `setup.sdb`                  |
| NVAR store, `StdDefaults`                | Factory default value of every setup variable                                              | `setup.sdb` (`VDAT`)         |
| `$BDRP` block in the BIOS region         | BIOS ID, version, build date, board name                                                   | `setup.sdb` (`INFO`)         |
| `4B2E0988-9E44-49AE-8B77-F544B1CAF03F`   | GUI picture resource: 212 PNGs, each stored as `cc 00 <id:u16> <len:u32> <png>`             | `gui.pak` (ids 100+)         |
| `8A0A450A-BEE9-42C1-9549-2BB8296AB3C3`   | GUI font: 0x28-byte header, then 580-byte glyphs `{u16 char, u16 advance, 24×24 alpha}`     | `font.bin`                   |
| `52BF7D53-79CF-4555-A517-1E8BB3042332`   | AORUS eagle boot logo (JPEG, 496×533)                                                      | `gui.pak` id 1, `Logo.bmp`   |
| `MyOemLogo1` / `MyOemLogo2`              | POST banner with the hot-key hints (JPEG, 1920×66)                                          | `gui.pak` ids 2 and 3        |
| `63819805-67BB-46EF-AA8D-1524A19A01E4`   | AMI logo (BMP)                                                                             | `gui.pak` id 4               |

The GIGABYTE font has no glyphs for some characters the translations use (mainly
Vietnamese letters with stacked accents). `merge_font()` renders those from
DejaVu Sans, matched to the cap height and baseline of the original font.

## gui.pak

Every picture is decoded at build time and stored as raw 32-bit BGRA with
straight alpha, so the firmware needs no PNG or JPEG decoder:

```
"AORUSPAK" u32 version=1 u32 count
count × { u32 id, u16 width, u16 height, u32 offset, u32 size }
pixel data
```

`IMG_GUI(n)` (`firmware/AorusPkg/Include/SdbFormat.h`) is picture `n` of the
GUI resource, counted in file order.

| n         | Picture                                         | Used for                                       |
|-----------|-------------------------------------------------|------------------------------------------------|
| 0         | AORUS logo                                      | Header, Q-Flash                                |
| 1 / 2     | "ADVANCED MODE" off / on                        | Header mode switch                             |
| 4 / 5     | "EASY MODE" off / on                            | Header mode switch                             |
| 26        | Star                                            | Favourite marker in Advanced Mode              |
| 42        | Tab background                                  | Advanced Mode tabs                             |
| 52 / 53   | Toggle switch off / on                          | Easy Mode PerfDrive and other switches         |
| 58        | Close cross                                     | Dialog title bar                               |
| 103       | Question mark                                   | Easy Mode boot sequence                        |
| 104–106   | Gauges                                          | Easy Mode PC Health                            |
| 109       | Globe                                           | Language button                                |
| 110       | Chip                                            | Q-Flash button                                 |
| 111       | Folder with star                                | Favorites tab and button                       |
| 112       | Fan                                             | Smart Fan 6                                    |
| 113       | Exit arrow                                      | Save & Exit tab and button                     |
| 114       | Circular arrow                                  | Load Defaults                                  |
| 115–118   | Gauge, power, info, gear                        | Tweaker, Boot, System Info, Settings tabs      |
| 119 / 120 | Magnifier / help                                | Search and Help buttons                        |
| 150       | Orange dot                                      | Smart Fan curve points                         |
| 184–186   | USB stick, arrow, BIOS chip                     | Q-Flash main screen                            |

## setup.sdb

`tools/sdb.py` compiles the HII forms into a compact database. The layout is
defined in `firmware/AorusPkg/Include/SdbFormat.h` and mirrored in `sdb.py`.

The database has these sections:

| Section | Contents                                                                                     |
|---------|----------------------------------------------------------------------------------------------|
| `INFO`  | Board name, BIOS version and date                                                            |
| `LANG`  | Languages                                                                                    |
| `STRD`  | Strings, NFC-normalised                                                                      |
| `VARS`  | Variable stores                                                                              |
| `VDAT`  | Default values                                                                               |
| `FORM`  | Forms                                                                                        |
| `STMT`  | Statements: one-of, numeric, checkbox, string, password, date, time, ref, text and subtitle   |
| `OPTS`  | One-of options                                                                               |
| `EXPR`  | `suppressif` / `grayoutif` conditions, compiled to RPN bytecode                              |
| `TABS`  | GIGABYTE's tab and menu layout (`tools/layout.py`)                                           |
| `SPEC`  | Items that behave specially                                                                  |
| `NSTR`  | Extra named strings                                                                          |

The firmware evaluates each condition against the live variable values, so
items appear, disappear and grey out like on the real board.

To see why a given item is shown or hidden, run `tools/explain.py "Prompt text"`.
