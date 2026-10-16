#!/usr/bin/env bash
# Signal Companion - Puts ps5-signal.elf in the app, for its "Start ps5-signal" button.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/bundle-payload.sh [path to ps5-signal.elf]
#
# Default: ../ps5-signal/ps5-signal.elf (build it there with ./build.sh). Use
# the release build: the development build also listens on the network.
# Without this step the app still works; the button then says the build does
# not include the payload.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source=${1:-"$root/../ps5-signal/ps5-signal.elf"}
[[ -f $source ]] || { echo "bundle-payload.sh: $source not found" >&2; exit 1; }
[[ $(head -c 4 "$source" | od -An -tx1 | tr -d ' ') == 7f454c46 ]] || {
    echo "bundle-payload.sh: $source is not an ELF file" >&2; exit 1;
}
case $(basename "$source") in
    *-dev.elf) echo "bundle-payload.sh: use the release build, not $(basename "$source")" >&2; exit 1 ;;
esac
mkdir -p "$root/assets/payload"
cp "$source" "$root/assets/payload/ps5-signal.elf"
echo "bundled $(sha256sum "$root/assets/payload/ps5-signal.elf" | cut -c1-16)... ($(stat -c %s "$source") bytes)"
