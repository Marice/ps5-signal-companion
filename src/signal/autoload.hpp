// Signal Companion - ps5-signal in ps5-autoloader's list, so it starts with the console.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps5-autoloader reads /data/ps5_autoloader/autoload.txt: one payload file
// name per line, "!<ms>" lines wait between them. Adding a payload copies the
// app's ELF next to that list and puts its name in, 3 seconds ("!3000") after
// the payload before it. The suite's payloads keep their order: ps5-exporter,
// ps5-homeassistant, ps5-signal; one goes before the first of the others that
// comes after it, else last. The app reaches /data only when etaHEN allows
// /data in the sandbox; otherwise the list is not found and the option stays
// hidden.

#pragma once

#include <string>
#include <string_view>

namespace hui::signal
{

constexpr const char *kAutoloadDirectory = "/data/ps5_autoloader";
constexpr const char *kAutoloadName = "ps5-signal.elf";

// A payload of the suite: its file name (in the app's assets/payload/ and in
// the autoloader's folder) and its version marker.
struct AutoloadPayload
{
    const char *name;
    std::string_view marker;
};

constexpr AutoloadPayload kSignalPayload{"ps5-signal.elf", "PS5SIGNAL_VER:"};
constexpr AutoloadPayload kExporterPayload{"ps5-exporter.elf", "PS5EXPORTER_VER:"};
constexpr AutoloadPayload kHomeAssistantPayload{"ps5-homeassistant.elf", "PS5HA_VER:"};
constexpr int kAutoloadDelayMs = 3000;

struct AutoloadState
{
    bool found = false;  // autoload.txt can be read
    bool listed = false; // ps5-signal.elf is in it
};

struct AutoloadRefresh
{
    bool updated = false;
    std::string from; // the copy's version before ("" when older than 0.2.0)
    std::string to;   // the app's version
    std::string error;
};

// The list as it is now. directory: for tests; the app uses kAutoloadDirectory.
AutoloadState autoload_state(const std::string &directory = kAutoloadDirectory,
                             const AutoloadPayload &payload = kSignalPayload);
// Copies the bundled ps5-signal.elf and puts it last in the list (once).
// Returns an error, or "".
std::string autoload_add(const std::string &directory = kAutoloadDirectory,
                         const AutoloadPayload &payload = kSignalPayload);
// When ps5-signal is in the list and its copy is older than the app's (or
// missing), puts the app's in its place. A newer or the same version stays.
AutoloadRefresh autoload_refresh(const std::string &directory = kAutoloadDirectory,
                                 const AutoloadPayload &payload = kSignalPayload);
// Takes it, and the wait line before it, out of the list and removes the copy.
std::string autoload_remove(const std::string &directory = kAutoloadDirectory,
                            const AutoloadPayload &payload = kSignalPayload);

// The list's text with a payload added in its place, or taken out (for tests).
std::string autoload_with(std::string_view list, std::string_view name = kAutoloadName);
std::string autoload_without(std::string_view list, std::string_view name = kAutoloadName);

} // namespace hui::signal
