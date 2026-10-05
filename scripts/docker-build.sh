#!/usr/bin/env bash
#
# Build the firmware inside a Docker container, so the only thing you need
# on the host is Docker (plus QEMU to run the result).
#
#   scripts/docker-build.sh                 same options as scripts/build.sh
#   scripts/docker-build.sh --bios FILE
#
# Extra arguments for "docker build" / "docker run" can be passed in
# DOCKER_BUILD_ARGS / DOCKER_RUN_ARGS.
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${IMAGE:-aorus-bios-builder}"
ARGS=()

while [ $# -gt 0 ]; do
  case "$1" in
    # The container only sees the source tree, so bring the image in first.
    --bios)
      mkdir -p "$ROOT/build/bios"
      cp "$2" "$ROOT/build/bios/bios.bin.new"
      mv "$ROOT/build/bios/bios.bin.new" "$ROOT/build/bios/bios.bin"
      shift 2 ;;
    -h|--help) awk 'NR > 1 && !/^#/ { exit } NR > 1 { sub(/^# ?/, ""); print }' "$0"; exit 0 ;;
    *) ARGS+=("$1"); shift ;;
  esac
done

# A proxy listening on the host's loopback is only reachable with host networking.
NET=()
case "${https_proxy:-${HTTPS_PROXY:-}}" in
  *://127.*|*://localhost*|*://\[::1\]*) NET=(--network host) ;;
esac

# shellcheck disable=SC2086
docker build ${NET[@]+"${NET[@]}"} \
  --build-arg http_proxy --build-arg https_proxy --build-arg HTTP_PROXY --build-arg HTTPS_PROXY \
  ${DOCKER_BUILD_ARGS:-} -t "$IMAGE" "$ROOT/docker"

TTY=()
[ -t 1 ] && TTY=(-t)

# shellcheck disable=SC2086
docker run --rm ${TTY[@]+"${TTY[@]}"} ${NET[@]+"${NET[@]}"} \
  -u "$(id -u):$(id -g)" \
  -v "$ROOT:/src" -w /src \
  -e http_proxy -e https_proxy -e HTTP_PROXY -e HTTPS_PROXY -e no_proxy -e NO_PROXY \
  -e BIOS_VERSION -e EDK2_TAG -e JOBS \
  ${DOCKER_RUN_ARGS:-} \
  "$IMAGE" scripts/build.sh ${ARGS[@]+"${ARGS[@]}"}
