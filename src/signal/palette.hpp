// Signal Companion - The player's own colours, kept by the app (not by ps5-signal).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "signal/model.hpp"

#include <array>
#include <optional>
#include <string>

namespace hui::signal
{

constexpr int kPaletteSlots = 6;

struct Palette
{
    std::array<std::optional<Mix>, kPaletteSlots> slots;

    bool operator==(const Palette &) const = default;
};

// {"colours": [null, {"blue": 0, "white": 255, "amber": 40}, ...]}
std::string palette_json(const Palette &palette);
// False (and out untouched) when the text is not such a file.
bool parse_palette(std::string_view text, Palette &out);

// data_path()/colours.json. A missing or broken file reads as empty slots.
Palette load_palette();
// Returns an error, or "" when it was written.
std::string save_palette(const Palette &palette);

} // namespace hui::signal
