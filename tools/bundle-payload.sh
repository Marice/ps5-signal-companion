#!/usr/bin/env bash
# Signal Companion - Puts the suite's payloads in the app, for its Start buttons and ps5-autoloader.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/bundle-payload.sh [path to ps5-signal.elf]
#
# Bundles, from the sibling folders unless given:
#   ../ps5-signal/ps5-signal.elf                 (build it there with ./build.sh)
#   ../ps5-exporter/ps5-exporter.elf             (make)
#   ../ps5-homeassistant/ps5-homeassistant.elf   (make)
# Release builds only: the development builds also listen on the network, and
# the exporter's probe build calls undocumented functions. A payload that is
# not found is left out; its Start button then says the build does not include it.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$root/assets/payload"

bundle() {
    local source=$1 name=$2 required=$3
    if [[ ! -f $source ]]; then
        if [[ $required == yes ]]; then
            echo "bundle-payload.sh: $source not found" >&2
            exit 1
        fi
        echo "bundle-payload.sh: $source not found, $name left out" >&2
        return
    fi
    [[ $(head -c 4 "$source" | od -An -tx1 | tr -d ' ') == 7f454c46 ]] || {
        echo "bundle-payload.sh: $source is not an ELF file" >&2; exit 1;
    }
    case $(basename "$source") in
        *-dev.elf|*-probe.elf) echo "bundle-payload.sh: use the release build, not $(basename "$source")" >&2; exit 1 ;;
    esac
    cp "$source" "$root/assets/payload/$name"
    echo "bundled $name $(sha256sum "$root/assets/payload/$name" | cut -c1-16)... ($(stat -c %s "$source") bytes)"
}

bundle "${1:-"$root/../ps5-signal/ps5-signal.elf"}" ps5-signal.elf yes
bundle "$root/../ps5-exporter/ps5-exporter.elf" ps5-exporter.elf no
bundle "$root/../ps5-homeassistant/ps5-homeassistant.elf" ps5-homeassistant.elf no
