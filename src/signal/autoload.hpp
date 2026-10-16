// Signal Companion - ps5-signal in ps5-autoloader's list, so it starts with the console.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps5-autoloader reads /data/ps5_autoloader/autoload.txt: one payload file
// name per line, "!<ms>" lines wait between them. Adding ps5-signal copies the
// app's ps5-signal.elf next to that list and appends "!3000" and its name, so
// it loads last, 3 seconds after the payload before it. The app reaches /data
// only when etaHEN allows /data in the sandbox; otherwise the list is not
// found and the option stays hidden.

#pragma once

#include <string>
#include <string_view>

namespace hui::signal
{

constexpr const char *kAutoloadDirectory = "/data/ps5_autoloader";
constexpr const char *kAutoloadName = "ps5-signal.elf";
constexpr int kAutoloadDelayMs = 3000;

struct AutoloadState
{
    bool found = false;  // autoload.txt can be read
    bool listed = false; // ps5-signal.elf is in it
};

// The list as it is now. directory: for tests; the app uses kAutoloadDirectory.
AutoloadState autoload_state(const std::string &directory = kAutoloadDirectory);
// Copies the bundled ps5-signal.elf and puts it last in the list (once).
// Returns an error, or "".
std::string autoload_add(const std::string &directory = kAutoloadDirectory);
// Takes it, and the wait line before it, out of the list and removes the copy.
std::string autoload_remove(const std::string &directory = kAutoloadDirectory);

// The list's text with ps5-signal.elf added last, or taken out (for tests).
std::string autoload_with(std::string_view list);
std::string autoload_without(std::string_view list);

} // namespace hui::signal
