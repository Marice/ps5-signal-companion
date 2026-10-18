// Signal Companion - ps5-signal's version, read from an ELF file's marker, and compared.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps5-signal 0.2.0 and later carry "PS5SIGNAL_VER:<version>" in the ELF file
// itself. A copy without it is older than 0.2.0.

#pragma once

#include <string>
#include <string_view>

namespace hui::signal
{

constexpr std::string_view kVersionMarker = "PS5SIGNAL_VER:";

// The version in an ELF file's marker, or "" when it has none. Each payload has its own
// marker: PS5SIGNAL_VER:, PS5EXPORTER_VER: (ps5-exporter 0.3.0), PS5HA_VER: (ps5-homeassistant).
std::string payload_version(std::string_view elf, std::string_view marker = kVersionMarker);
// Below zero, zero or above zero as a is older than, the same as or newer
// than b. Dotted numbers ("0.10.0" is newer than "0.9.1"); "" (no marker)
// is older than any version.
int compare_versions(std::string_view a, std::string_view b);

} // namespace hui::signal
