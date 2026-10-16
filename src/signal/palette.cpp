// Signal Companion - The player's own colours, kept by the app (not by ps5-signal).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "signal/palette.hpp"

#include "core/save_file.hpp"
#include "signal/service.hpp"

#include <cstdio>

namespace hui::signal
{

namespace
{

std::string palette_path()
{
    return data_path() + "/colours.json";
}

bool channel(const JsonValue &object, const char *key, int &out)
{
    const JsonValue &value = object[key];
    if (!value.is_number() || value.as_number() < 0 || value.as_number() > 255)
        return false;
    out = static_cast<int>(value.as_number());
    return true;
}

} // namespace

std::string palette_json(const Palette &palette)
{
    std::string out = "{\"colours\": [";
    for (int i = 0; i < kPaletteSlots; ++i)
    {
        if (i > 0)
            out += ", ";
        const std::optional<Mix> &slot = palette.slots[static_cast<std::size_t>(i)];
        if (!slot)
        {
            out += "null";
            continue;
        }
        char text[80];
        std::snprintf(text, sizeof(text), "{\"blue\": %d, \"white\": %d, \"amber\": %d}",
                      slot->blue, slot->white, slot->amber);
        out += text;
    }
    out += "]}\n";
    return out;
}

bool parse_palette(std::string_view text, Palette &out)
{
    JsonValue root;
    if (!parse_json(text, root) || !root["colours"].is_array())
        return false;
    Palette palette;
    const auto &items = root["colours"].items();
    for (std::size_t i = 0; i < items.size() && i < palette.slots.size(); ++i)
    {
        if (items[i].is_null())
            continue;
        Mix mix;
        if (!items[i].is_object() || !channel(items[i], "blue", mix.blue) ||
            !channel(items[i], "white", mix.white) || !channel(items[i], "amber", mix.amber))
            return false;
        palette.slots[i] = mix;
    }
    out = palette;
    return true;
}

Palette load_palette()
{
    Palette palette;
    std::string text;
    if (save::read_file(palette_path(), &text, 64 * 1024))
        parse_palette(text, palette);
    return palette;
}

std::string save_palette(const Palette &palette)
{
    save::ensure_directory(data_path());
    return save::write_atomic(palette_path(), palette_json(palette));
}

} // namespace hui::signal
