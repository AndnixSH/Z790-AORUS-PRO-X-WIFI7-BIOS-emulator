# Build environment for the Z790 AORUS PRO X WIFI7 BIOS emulator.
# Used by scripts/docker-build.sh; the source tree is mounted at /src.
FROM ubuntu:24.04

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential uuid-dev nasm acpica-tools git ca-certificates \
      python3 python3-pil python-is-python3 fonts-dejavu-core \
 && rm -rf /var/lib/apt/lists/*

ENV HOME=/tmp
WORKDIR /src
