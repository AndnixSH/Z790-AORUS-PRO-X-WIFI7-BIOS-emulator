#!/usr/bin/env bash
#
# Power on the emulated Z790 AORUS PRO X WIFI7 in QEMU.
#
#   scripts/run.sh [options]
#
#   --clear-cmos       clear the CMOS (wipe the NVRAM): the next boot
#                      retrains memory and asks how to continue, like a
#                      real board after a CMOS reset
#   --mem SIZE         RAM size (default 8G; the DRAM page shows this)
#   --kvm / --no-kvm   force hardware acceleration on/off (default: auto)
#   --host-cpu         show your real CPU instead of an i9-14900K (KVM only)
#   --disk FILE        attach a hard disk image (e.g. an OS to boot)
#   --cdrom FILE       attach an ISO image
#   --usb DIR          folder shown as a FAT USB stick (default build/usb);
#                      F12 screenshots and Q-Flash use it
#   --fullscreen       start full screen
#   --headless         no window; QMP socket in build/qmp.sock, or in
#                      $QMP_SOCK (for tests)
#   -- ARGS...         extra QEMU arguments
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$ROOT/build/firmware"
CODE="$FW/AORUS_CODE.fd"
VARS="$FW/AORUS_VARS.fd"
MEM=8G
KVM=auto
HOST_CPU=0
USB_DIR="$ROOT/build/usb"
DISPLAY_ARGS=(-display "gtk,zoom-to-fit=on,show-tabs=off")
EXTRA=()
MEDIA=()

while [ $# -gt 0 ]; do
  case "$1" in
    --clear-cmos) rm -f "$VARS"; echo "CMOS cleared."; shift ;;
    --mem) MEM="$2"; shift 2 ;;
    --kvm) KVM=on; shift ;;
    --no-kvm) KVM=off; shift ;;
    --host-cpu) HOST_CPU=1; shift ;;
    --disk) MEDIA+=(-drive "file=$2,if=none,id=disk0" -device "nvme,drive=disk0,serial=AORUS0001"); shift 2 ;;
    --cdrom) MEDIA+=(-drive "file=$2,media=cdrom,if=none,id=cd0" -device "ide-cd,drive=cd0"); shift 2 ;;
    --usb) USB_DIR="$2"; shift 2 ;;
    --fullscreen) DISPLAY_ARGS+=(-full-screen); shift ;;
    --headless) DISPLAY_ARGS=(-display none -qmp "unix:${QMP_SOCK:-$ROOT/build/qmp.sock},server,nowait"); shift ;;
    --) shift; EXTRA+=("$@"); break ;;
    -h|--help) awk 'NR > 1 && !/^#/ { exit } NR > 1 { sub(/^# ?/, ""); print }' "$0"; exit 0 ;;
    *) echo "unknown option $1 (see --help)" >&2; exit 1 ;;
  esac
done

if [ ! -f "$CODE" ]; then
  echo "Firmware not built yet - run scripts/build.sh first." >&2
  exit 1
fi

# A fresh NVRAM file is a board with an empty CMOS.
[ -f "$VARS" ] || cp "$FW/AORUS_VARS_BLANK.fd" "$VARS"

if [ "$KVM" = auto ]; then
  if [ -w /dev/kvm ]; then KVM=on; else KVM=off; fi
fi

# The board's CPU: an i9-14900K (Raptor Lake-S Refresh, CPUID B0671).
CPU_ID="model-id=Intel(R) Core(TM) i9-14900K,family=6,model=183,stepping=1"
if [ "$KVM" = on ]; then
  ACCEL=(-accel kvm)
  if [ "$HOST_CPU" = 1 ]; then CPU=(-cpu host); else CPU=(-cpu "host,$CPU_ID"); fi
else
  ACCEL=(-accel tcg)
  CPU=(-cpu "max,$CPU_ID")
fi

mkdir -p "$USB_DIR"

exec qemu-system-x86_64 \
  -name "GIGABYTE Z790 AORUS PRO X WIFI7" \
  -machine q35 "${ACCEL[@]}" "${CPU[@]}" -smp 8 -m "$MEM" \
  -drive "if=pflash,format=raw,unit=0,readonly=on,file=$CODE" \
  -drive "if=pflash,format=raw,unit=1,file=$VARS" \
  -smbios "type=1,manufacturer=Gigabyte Technology Co.,, Ltd.,product=Z790 AORUS PRO X WIFI7,version=-CF,serial=Default string,sku=Default string,family=Z790 MB" \
  -smbios "type=2,manufacturer=Gigabyte Technology Co.,, Ltd.,product=Z790 AORUS PRO X WIFI7,version=x.x,serial=Default string" \
  -vga std -rtc base=localtime -nic none \
  -device qemu-xhci,id=xhci \
  -drive "if=none,id=usbstick,format=raw,file=fat:rw:$USB_DIR" \
  -device usb-storage,bus=xhci.0,drive=usbstick,removable=on \
  "${MEDIA[@]}" "${DISPLAY_ARGS[@]}" "${EXTRA[@]}"
