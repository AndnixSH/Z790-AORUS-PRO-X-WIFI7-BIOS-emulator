# Z790 AORUS PRO X WIFI7 BIOS emulator

Power on a GIGABYTE **Z790 AORUS PRO X WIFI7** in QEMU: AORUS POST screen,
memory-training power cycles, "no OS found", and a fully interactive
GIGABYTE UEFI setup (Easy Mode and Advanced Mode) built from the **real BIOS
image** downloaded from GIGABYTE.

Work in progress - see the full write-up below once it lands.

```sh
scripts/build.sh      # downloads BIOS F9a from GIGABYTE, extracts it, builds the firmware
scripts/run.sh        # powers on the board in QEMU
```
