// Signal Companion - What the ps5-signal service reports and keeps: its status and config.json.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rules here are the service's own (ps5-signal README, "API" and
// "Running it"): the companion checks them before it sends anything, so the
// player sees what is wrong next to the value instead of a refusal later.

#pragma once

#include "signal/json.hpp"

#include <string>
#include <vector>

namespace hui::signal
{

// ---- the service's limits ----
constexpr int kMaxApiBeeps = 6;     // one POST /beep
constexpr int kMaxConfigBeeps = 16; // the startup tune
constexpr int kMaxLedSteps = 32;
constexpr int kMinPauseMs = 100;
constexpr int kMaxPauseMs = 2000;
constexpr int kDefaultPauseMs = 200;
constexpr int kMinStepMs = 250;
constexpr int kMaxStepMs = 10000;
constexpr int kTones = 4;  // 0 silent, 1 short, 2 error tone, 3 long
constexpr int kLevels = 3; // 0 bright, 1 medium, 2 dim

// The LED's three channels, 0-255 each. There is no free RGB colour.
struct Mix
{
    int blue = 0;
    int white = 0;
    int amber = 0;

    bool operator==(const Mix &) const = default;
};

struct BeepStep
{
    int tone = 1;
    int pause_ms = kDefaultPauseMs; // time until the next beep

    bool operator==(const BeepStep &) const = default;
};

enum class LedKind : unsigned char
{
    named, // "color": blue, white, orange or off
    level, // "level": 0-2, brightness
    mix,   // "blue", "white", "amber": 0-255
};

enum class LedName : unsigned char
{
    blue,
    white,
    orange,
    off,
};
constexpr int kLedNames = 4;

struct LedStep
{
    LedKind kind = LedKind::named;
    LedName name = LedName::blue;
    int level = 0;
    Mix mix;
    int ms = 400;

    bool operator==(const LedStep &) const = default;
};

// /data/ps5-signal/config.json.
struct Config
{
    std::vector<BeepStep> beeps; // empty: no startup tune
    std::vector<LedStep> leds;   // empty: no startup light
    bool hold = false;           // led_color is set ...
    Mix held;                    // ... to this; otherwise the system's colour

    bool operator==(const Config &) const = default;
};

// GET /status.
struct Status
{
    std::string version;
    bool beeping = false;
    int muted = -1;  // -1 unknown, 0 no, 1 yes
    int volume = -1; // -1 unknown
    bool lighting = false;
    bool led_available = false;
    int level = -1;
    bool color_available = false;
    Mix color; // what the LED shows now (-1 per channel when unknown)
    bool hold = false;
    Mix held;
};

// The service's default when config.json does not exist.
Config default_config();

const char *tone_name(int tone);
const char *level_name(int level);
const char *led_name(LedName name);
// "Blue", "Dim", "Mix 40 / 0 / 255".
std::string led_step_text(const LedStep &step);

// Reads config.json as the service does. On failure returns false and says why.
bool parse_config(const JsonValue &root, Config &out, std::string &error);
bool parse_config(std::string_view text, Config &out, std::string &error);
bool parse_status(std::string_view text, Status &out);

// config.json, one step per line.
std::string config_json(const Config &config);
// Bodies for the API. Beeps past kMaxApiBeeps are left out.
std::string beep_request(const std::vector<BeepStep> &beeps);
std::string led_request(const std::vector<LedStep> &leds, int repeat = 1);
std::string color_request(const Mix &mix);
std::string system_color_request();
// The "error" of an error answer, or an empty string.
std::string error_text(std::string_view body);

} // namespace hui::signal
