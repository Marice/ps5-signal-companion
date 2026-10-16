// Signal Companion - What the ps5-signal service reports and keeps: its status and config.json.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "signal/model.hpp"

#include <algorithm>
#include <cstdio>

namespace hui::signal
{

namespace
{

constexpr const char *kToneNames[kTones] = {"Silent", "Short", "Error tone", "Long"};
constexpr const char *kLevelNames[kLevels] = {"Bright", "Medium", "Dim"};
constexpr const char *kLedTitles[kLedNames] = {"Blue", "White", "Orange", "Off"};
constexpr const char *kLedKeys[kLedNames] = {"blue", "white", "orange", "off"};

// A number member within [minimum, maximum], or fallback when it is absent.
// The service truncates fractions, so this does too.
bool read_int(const JsonValue &object, const char *key, int minimum, int maximum, int fallback,
              int &out)
{
    if (!object.has(key))
    {
        out = fallback;
        return fallback >= minimum;
    }
    const JsonValue &value = object[key];
    if (!value.is_number() || value.as_number() < minimum || value.as_number() > maximum)
        return false;
    out = static_cast<int>(value.as_number());
    return true;
}

bool read_mix(const JsonValue &object, Mix &out)
{
    return read_int(object, "blue", 0, 255, 0, out.blue) &&
           read_int(object, "white", 0, 255, 0, out.white) &&
           read_int(object, "amber", 0, 255, 0, out.amber);
}

bool read_beeps(const JsonValue &array, int maximum, std::vector<BeepStep> &out)
{
    if (!array.is_array() || array.items().empty() ||
        array.items().size() > static_cast<std::size_t>(maximum))
        return false;
    for (const JsonValue &item : array.items())
    {
        BeepStep step;
        if (!item.is_object() || !read_int(item, "tone", 0, kTones - 1, -1, step.tone) ||
            !read_int(item, "pause_ms", kMinPauseMs, kMaxPauseMs, kDefaultPauseMs, step.pause_ms))
            return false;
        out.push_back(step);
    }
    return true;
}

bool read_led_step(const JsonValue &item, LedStep &step)
{
    if (!item.is_object())
        return false;
    const bool channels = item.has("blue") || item.has("white") || item.has("amber");
    if (item.has("level"))
    {
        step.kind = LedKind::level;
        if (item.has("color") || channels ||
            !read_int(item, "level", 0, kLevels - 1, -1, step.level))
            return false;
    }
    else if (item.has("color"))
    {
        step.kind = LedKind::named;
        if (channels || !item["color"].is_string())
            return false;
        bool known = false;
        for (int i = 0; i < kLedNames; ++i)
        {
            if (item["color"].as_string() == kLedKeys[i])
            {
                step.name = static_cast<LedName>(i);
                known = true;
            }
        }
        if (!known)
            return false;
    }
    else
    {
        step.kind = LedKind::mix;
        if (!read_mix(item, step.mix))
            return false;
    }
    return read_int(item, "ms", kMinStepMs, kMaxStepMs, -1, step.ms);
}

bool read_leds(const JsonValue &array, std::vector<LedStep> &out)
{
    if (!array.is_array() || array.items().empty() ||
        array.items().size() > static_cast<std::size_t>(kMaxLedSteps))
        return false;
    for (const JsonValue &item : array.items())
    {
        LedStep step;
        if (!read_led_step(item, step))
            return false;
        out.push_back(step);
    }
    return true;
}

bool empty_or_absent(const JsonValue &value)
{
    return value.is_null() || (value.is_array() && value.items().empty());
}

void append_int(std::string &out, int value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%d", value);
    out += text;
}

void append_mix(std::string &out, const Mix &mix)
{
    out += "\"blue\": ";
    append_int(out, mix.blue);
    out += ", \"white\": ";
    append_int(out, mix.white);
    out += ", \"amber\": ";
    append_int(out, mix.amber);
}

void append_beep(std::string &out, const BeepStep &step, bool last)
{
    out += "{ \"tone\": ";
    append_int(out, step.tone);
    if (!last)
    {
        out += ", \"pause_ms\": ";
        append_int(out, step.pause_ms);
    }
    out += " }";
}

void append_led(std::string &out, const LedStep &step)
{
    out += "{ ";
    switch (step.kind)
    {
    case LedKind::named:
        out += "\"color\": ";
        append_json_string(out, kLedKeys[static_cast<int>(step.name)]);
        break;
    case LedKind::level:
        out += "\"level\": ";
        append_int(out, step.level);
        break;
    case LedKind::mix:
        append_mix(out, step.mix);
        break;
    }
    out += ", \"ms\": ";
    append_int(out, step.ms);
    out += " }";
}

int read_setting(const JsonValue &value)
{
    return value.is_number() ? static_cast<int>(value.as_number()) : -1;
}

} // namespace

Config default_config()
{
    // ps5-signal's own default: "Shave and a Haircut", a run through the LEDs, then the colour it keeps.
    Config config;
    const int pauses[] = {300, 150, 150, 300, 600, 650, kDefaultPauseMs};
    for (int i = 0; i < 7; ++i)
        config.beeps.push_back({i < 5 ? 1 : 3, pauses[i]});
    const auto named = [](LedName name, int ms)
    {
        LedStep step;
        step.name = name;
        step.ms = ms;
        return step;
    };
    const auto mixed = [](Mix mix, int ms)
    {
        LedStep step;
        step.kind = LedKind::mix;
        step.mix = mix;
        step.ms = ms;
        return step;
    };
    for (int round = 0; round < 2; ++round)
    {
        config.leds.push_back(named(LedName::blue, 250));
        config.leds.push_back(named(LedName::white, 250));
        config.leds.push_back(named(LedName::orange, 250));
    }
    config.leds.push_back(mixed({255, 255, 0}, 300));
    config.leds.push_back(mixed({0, 255, 255}, 300));
    config.leds.push_back(mixed({255, 0, 255}, 300));
    config.leds.push_back(named(LedName::off, 250));
    config.leds.push_back(mixed({255, 80, 200}, 800));
    config.hold = true;
    config.held = {255, 80, 200};
    return config;
}

const char *tone_name(int tone)
{
    return tone >= 0 && tone < kTones ? kToneNames[tone] : "?";
}

const char *level_name(int level)
{
    return level >= 0 && level < kLevels ? kLevelNames[level] : "?";
}

const char *led_name(LedName name)
{
    return kLedTitles[static_cast<int>(name)];
}

std::string led_step_text(const LedStep &step)
{
    switch (step.kind)
    {
    case LedKind::named:
        return led_name(step.name);
    case LedKind::level:
        return level_name(step.level);
    case LedKind::mix:
        break;
    }
    char text[48];
    std::snprintf(text, sizeof(text), "Mix %d / %d / %d", step.mix.blue, step.mix.white,
                  step.mix.amber);
    return text;
}

bool parse_config(const JsonValue &root, Config &out, std::string &error)
{
    Config config;
    if (!root.is_object())
    {
        error = "the config must be a JSON object";
        return false;
    }
    const JsonValue &startup = root["startup"];
    if (root.has("startup"))
    {
        if (!startup.is_object())
        {
            error = "startup must be an object";
            return false;
        }
        if (!empty_or_absent(startup["beep"]) &&
            !read_beeps(startup["beep"], kMaxConfigBeeps, config.beeps))
        {
            error = "startup.beep: 1-16 steps with tone 0-3 and pause_ms 100-2000";
            return false;
        }
        if (!empty_or_absent(startup["led"]) && !read_leds(startup["led"], config.leds))
        {
            error = "startup.led: 1-32 steps with ms 250-10000 and a level, a colour or a mix";
            return false;
        }
    }
    const JsonValue &color = root["led_color"];
    if (!color.is_null())
    {
        if (!color.is_object() || !read_mix(color, config.held))
        {
            error = "led_color: null, or blue, white and amber 0-255";
            return false;
        }
        config.hold = true;
    }
    out = std::move(config);
    return true;
}

bool parse_config(std::string_view text, Config &out, std::string &error)
{
    JsonValue root;
    if (!parse_json(text, root))
    {
        error = "config.json is not valid JSON";
        return false;
    }
    return parse_config(root, out, error);
}

bool parse_status(std::string_view text, Status &out)
{
    JsonValue root;
    if (!parse_json(text, root) || !root.is_object() || root["service"].as_string() != "ps5-signal")
        return false;
    Status status;
    status.version = root["version"].as_string();
    const JsonValue &beep = root["beep"];
    status.beeping = beep["playing"].as_bool();
    status.muted = beep["muted"].is_bool() ? (beep["muted"].as_bool() ? 1 : 0) : -1;
    status.volume = read_setting(beep["volume"]);
    const JsonValue &led = root["led"];
    status.lighting = led["playing"].as_bool();
    status.led_available = led["available"].as_bool();
    status.level = read_setting(led["level"]);
    const JsonValue &color = led["color"];
    status.color_available = color["available"].as_bool();
    status.color = {read_setting(color["blue"]), read_setting(color["white"]),
                    read_setting(color["amber"])};
    const JsonValue &held = color["held"];
    if (held.is_object())
    {
        status.hold = true;
        status.held = {read_setting(held["blue"]), read_setting(held["white"]),
                       read_setting(held["amber"])};
    }
    out = std::move(status);
    return true;
}

std::string config_json(const Config &config)
{
    std::string out = "{\n  \"startup\": {\n    \"beep\": [";
    for (std::size_t i = 0; i < config.beeps.size(); ++i)
    {
        out += i == 0 ? "\n      " : ",\n      ";
        append_beep(out, config.beeps[i], i + 1 == config.beeps.size());
    }
    out += config.beeps.empty() ? "],\n    \"led\": [" : "\n    ],\n    \"led\": [";
    for (std::size_t i = 0; i < config.leds.size(); ++i)
    {
        out += i == 0 ? "\n      " : ",\n      ";
        append_led(out, config.leds[i]);
    }
    out += config.leds.empty() ? "]\n  },\n  \"led_color\": " : "\n    ]\n  },\n  \"led_color\": ";
    if (config.hold)
    {
        out += "{ ";
        append_mix(out, config.held);
        out += " }";
    }
    else
    {
        out += "null";
    }
    out += "\n}\n";
    return out;
}

std::string beep_request(const std::vector<BeepStep> &beeps)
{
    const std::size_t count = std::min(beeps.size(), static_cast<std::size_t>(kMaxApiBeeps));
    std::string out = "{\"pattern\": [";
    for (std::size_t i = 0; i < count; ++i)
    {
        if (i > 0)
            out += ", ";
        append_beep(out, beeps[i], i + 1 == count);
    }
    out += "]}";
    return out;
}

std::string led_request(const std::vector<LedStep> &leds, int repeat)
{
    std::string out = "{\"pattern\": [";
    for (std::size_t i = 0; i < leds.size(); ++i)
    {
        if (i > 0)
            out += ", ";
        append_led(out, leds[i]);
    }
    out += "], \"repeat\": ";
    append_int(out, repeat);
    out += "}";
    return out;
}

std::string color_request(const Mix &mix)
{
    std::string out = "{";
    append_mix(out, mix);
    out += "}";
    return out;
}

std::string system_color_request()
{
    return "{\"system\": true}";
}

std::string error_text(std::string_view body)
{
    JsonValue root;
    if (!parse_json(body, root))
        return {};
    return root["error"].as_string();
}

} // namespace hui::signal
