#!/usr/bin/env bash
#
# Build the Z790 AORUS PRO X WIFI7 BIOS emulator firmware.
#
#   scripts/build.sh                 download BIOS F9a from GIGABYTE and build
#   scripts/build.sh --bios FILE     use a BIOS image you already have
#   BIOS_VERSION=f8a scripts/build.sh
#
# Output: build/firmware/AORUS_CODE.fd and AORUS_VARS.fd (for scripts/run.sh)
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
EDK2_TAG="${EDK2_TAG:-edk2-stable202605}"
EDK2_DIR="${EDK2_DIR:-$BUILD/edk2}"
BIOS_VERSION="${BIOS_VERSION:-f9a}"
BIOS_FILE=""
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

while [ $# -gt 0 ]; do
  case "$1" in
    --bios) BIOS_FILE="$2"; shift 2 ;;
    -h|--help) awk 'NR > 1 && !/^#/ { exit } NR > 1 { sub(/^# ?/, ""); print }' "$0"; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 1 ;;
  esac
done

say() { printf '\033[1;33m==> %s\033[0m\n' "$*"; }

# ---------------------------------------------------------------- tools
missing=()
for t in git make gcc g++ nasm iasl python3; do
  command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
python3 -c 'import PIL' 2>/dev/null || missing+=("python3-pil (Pillow)")
if [ ${#missing[@]} -ne 0 ]; then
  echo "Missing build tools: ${missing[*]}" >&2
  echo "On Debian/Ubuntu: sudo apt install build-essential uuid-dev nasm acpica-tools git python3 python3-pil fonts-dejavu-core qemu-system-x86" >&2
  echo "Or build inside Docker: scripts/docker-build.sh" >&2
  exit 1
fi

# --------------------------------------------------------------- BIOS image
mkdir -p "$BUILD/bios"
if [ -n "$BIOS_FILE" ]; then
  cp "$BIOS_FILE" "$BUILD/bios/bios.bin"
elif [ ! -f "$BUILD/bios/bios.bin" ]; then
  say "Downloading GIGABYTE BIOS $BIOS_VERSION"
  python3 "$ROOT/tools/fetch_bios.py" --version "$BIOS_VERSION" --out "$BUILD/bios"
fi

say "Extracting setup data, pictures and font from the BIOS image"
python3 "$ROOT/tools/extract.py" --bios "$BUILD/bios/bios.bin" --out "$BUILD/data"

if ! cmp -s "$BUILD/data/SdbIds.h" "$ROOT/firmware/AorusPkg/Include/SdbIds.h"; then
  echo "note: tools/layout.py changed the generated ids - updating firmware/AorusPkg/Include/SdbIds.h"
  cp "$BUILD/data/SdbIds.h" "$ROOT/firmware/AorusPkg/Include/SdbIds.h"
fi

mkdir -p "$ROOT/firmware/AorusPkg/Data"
cp "$BUILD/data/setup.sdb" "$BUILD/data/gui.pak" "$BUILD/data/font.bin" "$ROOT/firmware/AorusPkg/Data/"

# --------------------------------------------------------------------- EDK2
if [ ! -d "$EDK2_DIR/.git" ]; then
  say "Fetching EDK2 $EDK2_TAG"
  git clone --depth 1 --branch "$EDK2_TAG" https://github.com/tianocore/edk2.git "$EDK2_DIR"
  git -C "$EDK2_DIR" submodule update --init --depth 1 --jobs 8
fi

if [ ! -x "$EDK2_DIR/BaseTools/Source/C/bin/GenFw" ]; then
  say "Building EDK2 BaseTools"
  make -C "$EDK2_DIR/BaseTools" -j"$JOBS" >/dev/null
fi

say "Patching OVMF"
python3 "$ROOT/firmware/patch_ovmf.py" "$EDK2_DIR"
cp "$BUILD/data/Logo.bmp" "$EDK2_DIR/MdeModulePkg/Logo/Logo.bmp"

say "Building OVMF with the AORUS front-end"
(
  cd "$EDK2_DIR"
  export WORKSPACE="$EDK2_DIR"
  export PACKAGES_PATH="$EDK2_DIR:$ROOT/firmware"
  set +u
  . ./edksetup.sh >/dev/null
  set -u
  build -a X64 -t GCC -b RELEASE -n "$JOBS" \
        -p OvmfPkg/OvmfPkgX64.dsc \
        -D FD_SIZE_4MB -D NETWORK_ENABLE=FALSE
)

mkdir -p "$BUILD/firmware"
FV="$EDK2_DIR/Build/OvmfX64/RELEASE_GCC/FV"
cp "$FV/OVMF_CODE.fd" "$BUILD/firmware/AORUS_CODE.fd"
cp "$FV/OVMF_VARS.fd" "$BUILD/firmware/AORUS_VARS_BLANK.fd"
say "Done: $BUILD/firmware/AORUS_CODE.fd  (start it with scripts/run.sh)"
