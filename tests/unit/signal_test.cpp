// Signal Companion - Tests of the ps5-signal client: JSON, the config rules and the worker.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fake_signal.hpp"

#include "signal/autoload.hpp"
#include "signal/json.hpp"
#include "signal/model.hpp"
#include "signal/service.hpp"
#include "signal/version.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

using namespace hui::signal;
using hui::testing::FakeSignal;

// ---- JSON ---------------------------------------------------------------------

TEST(SignalJson, ReadsNestedValues)
{
    JsonValue root;
    ASSERT_TRUE(parse_json(R"( {"a": [1, -2.5, true, null], "b": {"c": "x\"y\\z\n"}} )", root));
    ASSERT_TRUE(root.is_object());
    ASSERT_EQ(root["a"].items().size(), 4u);
    EXPECT_EQ(root["a"].items()[0].as_number(), 1.0);
    EXPECT_EQ(root["a"].items()[1].as_number(), -2.5);
    EXPECT_TRUE(root["a"].items()[2].as_bool());
    EXPECT_TRUE(root["a"].items()[3].is_null());
    EXPECT_EQ(root["b"]["c"].as_string(), "x\"y\\z\n");
    EXPECT_TRUE(root["missing"].is_null());
    EXPECT_FALSE(root.has("missing"));
}

TEST(SignalJson, RefusesBrokenText)
{
    JsonValue root;
    for (const char *text :
         {"", "{", "{\"a\" 1}", "[1,]", "{\"a\": tru}", "\"open", "1 2", "{\"a\": 01x}", "-"})
        EXPECT_FALSE(parse_json(text, root)) << text;
    std::string deep(40, '[');
    deep += std::string(40, ']');
    EXPECT_FALSE(parse_json(deep, root));
}

TEST(SignalJson, WritesSafeStrings)
{
    std::string out;
    append_json_string(out, "a\"b\\c\n");
    EXPECT_EQ(out, "\"a\\\"b\\\\c\\u000a\"");
    JsonValue back;
    ASSERT_TRUE(parse_json(out, back));
    EXPECT_EQ(back.as_string(), "a\"b\\c\n");
}

// ---- the config ---------------------------------------------------------------

TEST(SignalConfig, TheDefaultSurvivesARoundTrip)
{
    const Config config = default_config();
    EXPECT_EQ(config.beeps.size(), 7u);
    EXPECT_EQ(config.leds.size(), 11u);
    EXPECT_TRUE(config.hold);
    EXPECT_EQ(config.held, (Mix{255, 80, 200}));
    EXPECT_LE(tune_ms(config.beeps), kMaxTotalMs);
    EXPECT_LE(light_ms(config.leds), kMaxTotalMs);
    Config back;
    std::string error;
    ASSERT_TRUE(parse_config(config_json(config), back, error)) << error;
    EXPECT_EQ(back, config);
}

TEST(SignalConfig, ReadsTheServicesExample)
{
    // ps5-signal's README, "Running it".
    const char *text = R"({
  "startup": {
    "beep": [
      { "tone": 1, "pause_ms": 300 }, { "tone": 1, "pause_ms": 150 }, { "tone": 1, "pause_ms": 150 },
      { "tone": 1, "pause_ms": 300 }, { "tone": 1, "pause_ms": 600 }, { "tone": 1, "pause_ms": 300 },
      { "tone": 3 }
    ],
    "led": [
      { "color": "blue", "ms": 250 }, { "color": "white", "ms": 250 }, { "color": "orange", "ms": 250 },
      { "color": "blue", "ms": 250 }, { "color": "white", "ms": 250 }, { "color": "orange", "ms": 250 },
      { "blue": 255, "white": 255, "ms": 300 }, { "white": 255, "amber": 255, "ms": 300 },
      { "blue": 255, "amber": 255, "ms": 300 }, { "color": "off", "ms": 250 },
      { "blue": 255, "white": 80, "amber": 200, "ms": 800 }
    ]
  },
  "led_color": { "blue": 255, "white": 80, "amber": 200 }
})";
    Config config;
    std::string error;
    ASSERT_TRUE(parse_config(text, config, error)) << error;
    EXPECT_EQ(config, default_config());
}

TEST(SignalConfig, KeepsEveryKindOfLightStepAndTheColour)
{
    const char *text = R"({"startup": {"beep": [], "led": [
        {"level": 2, "ms": 250}, {"blue": 40, "amber": 255, "ms": 4000}, {"color": "off", "ms": 300}]},
        "led_color": {"blue": 10, "white": 20, "amber": 30}})";
    Config config;
    std::string error;
    ASSERT_TRUE(parse_config(text, config, error)) << error;
    EXPECT_TRUE(config.beeps.empty());
    ASSERT_EQ(config.leds.size(), 3u);
    EXPECT_EQ(config.leds[0].kind, LedKind::level);
    EXPECT_EQ(config.leds[0].level, 2);
    EXPECT_EQ(config.leds[1].kind, LedKind::mix);
    EXPECT_EQ(config.leds[1].mix, (Mix{40, 0, 255}));
    EXPECT_EQ(config.leds[2].name, LedName::off);
    EXPECT_TRUE(config.hold);
    EXPECT_EQ(config.held, (Mix{10, 20, 30}));
    EXPECT_EQ(led_step_text(config.leds[1]), "Mix 40 / 0 / 255");

    Config back;
    ASSERT_TRUE(parse_config(config_json(config), back, error)) << error;
    EXPECT_EQ(back, config);
}

TEST(SignalConfig, RefusesWhatTheServiceRefuses)
{
    std::string too_many = R"({"startup": {"beep": [)";
    for (int i = 0; i < kMaxConfigBeeps + 1; ++i)
        too_many += i == 0 ? R"({"tone": 1})" : R"(, {"tone": 1})";
    too_many += "]}}";
    const std::string cases[] = {
        "[]",
        R"({"startup": []})",
        too_many,
        // 10 seconds in all at most: the pauses of a tune, the steps of a light.
        R"({"startup": {"beep": [{"tone": 1, "pause_ms": 2000}, {"tone": 1, "pause_ms": 2000},
            {"tone": 1, "pause_ms": 2000}, {"tone": 1, "pause_ms": 2000}, {"tone": 1, "pause_ms": 2000},
            {"tone": 1, "pause_ms": 100}, {"tone": 1}]}})",
        R"({"startup": {"led": [{"level": 1, "ms": 6000}, {"level": 2, "ms": 4001}]}})",
        R"({"startup": {"beep": [{"tone": 4}]}})",
        R"({"startup": {"beep": [{"pause_ms": 200}]}})",
        R"({"startup": {"beep": [{"tone": 1, "pause_ms": 99}]}})",
        R"({"startup": {"beep": [{"tone": 1, "pause_ms": 2001}]}})",
        R"({"startup": {"led": [{"color": "blue", "ms": 249}]}})",
        R"({"startup": {"led": [{"color": "blue"}]}})",
        R"({"startup": {"led": [{"color": "purple", "ms": 400}]}})",
        R"({"startup": {"led": [{"color": "blue", "white": 4, "ms": 400}]}})",
        R"({"startup": {"led": [{"level": 3, "ms": 400}]}})",
        R"({"startup": {"led": [{"blue": 256, "ms": 400}]}})",
        R"({"led_color": {"blue": -1}})",
        R"({"led_color": "blue"})",
    };
    for (const std::string &text : cases)
    {
        Config config;
        std::string error;
        EXPECT_FALSE(parse_config(text, config, error)) << text;
        EXPECT_FALSE(error.empty()) << text;
    }
}

TEST(SignalConfig, RequestsFollowTheApi)
{
    std::vector<BeepStep> beeps(30, BeepStep{2, 150});
    JsonValue body;
    ASSERT_TRUE(parse_json(beep_request(beeps), body));
    ASSERT_EQ(body["pattern"].items().size(), static_cast<std::size_t>(kMaxApiBeeps));
    EXPECT_EQ(body["pattern"].items()[0]["pause_ms"].as_number(), 150.0);
    // The last one waits for nothing.
    EXPECT_FALSE(body["pattern"].items()[kMaxApiBeeps - 1].has("pause_ms"));

    ASSERT_TRUE(parse_json(led_request(default_config().leds, 3), body));
    EXPECT_EQ(body["repeat"].as_number(), 3.0);
    EXPECT_EQ(body["pattern"].items()[1]["color"].as_string(), "white");

    ASSERT_TRUE(parse_json(color_request({1, 2, 3}), body));
    EXPECT_EQ(body["amber"].as_number(), 3.0);
    ASSERT_TRUE(parse_json(system_color_request(), body));
    EXPECT_TRUE(body["system"].as_bool());
    EXPECT_EQ(error_text(R"({"error": "over the per-minute limit"})"), "over the per-minute limit");
    EXPECT_EQ(error_text("nope"), "");
}

TEST(SignalStatus, ReadsTheServicesAnswer)
{
    Status status;
    ASSERT_TRUE(parse_status(FakeSignal::status_json(true, Mix{0, 255, 40}), status));
    EXPECT_EQ(status.version, "0.1.0");
    EXPECT_EQ(status.muted, 0);
    EXPECT_EQ(status.volume, 2);
    EXPECT_EQ(status.level, 1);
    EXPECT_TRUE(status.color_available);
    EXPECT_EQ(status.color, (Mix{0, 255, 40}));
    EXPECT_TRUE(status.hold);
    EXPECT_EQ(status.held, (Mix{0, 255, 40}));

    const char *unknown = R"({"service":"ps5-signal","version":"0.1.0","beep":{"playing":true,
        "muted":null,"volume":null},"led":{"playing":false,"available":false,"level":null,
        "color":{"available":false,"blue":null,"white":null,"amber":null,"held":null}}})";
    ASSERT_TRUE(parse_status(unknown, status));
    EXPECT_TRUE(status.beeping);
    EXPECT_EQ(status.muted, -1);
    EXPECT_EQ(status.color.blue, -1);
    EXPECT_FALSE(status.hold);
    EXPECT_FALSE(parse_status(R"({"service":"something else"})", status));
}

// ---- the worker and the link ----------------------------------------------------

TEST(SignalService, AnswersArriveInALaterFrame)
{
    auto fake = std::make_unique<FakeSignal>();
    FakeSignal *signal = fake.get();
    Service service(std::move(fake));
    EXPECT_EQ(service.serial(Call::status), 0u);
    service.send(Call::status);
    service.wait_idle();
    EXPECT_EQ(service.serial(Call::status), 1u);
    EXPECT_EQ(service.outcome(Call::status).reply.status, 200);
    EXPECT_FALSE(service.pending(Call::status));
    EXPECT_EQ(signal->requests().back(), "GET /status ");
}

TEST(SignalService, AWaitingCallIsReplacedByTheNextOfItsKind)
{
    auto fake = std::make_unique<FakeSignal>();
    FakeSignal *signal = fake.get();
    signal->hold_requests(true);
    Service service(std::move(fake));
    service.send(Call::status); // being sent, held by the fake
    signal->wait_held();
    service.send(Call::color, color_request({1, 0, 0}));
    service.send(Call::color, color_request({2, 0, 0}));
    service.send(Call::color, color_request({3, 0, 0}));
    EXPECT_TRUE(service.pending(Call::color));
    signal->hold_requests(false);
    service.wait_idle();
    const std::vector<std::string> requests = signal->requests();
    ASSERT_EQ(requests.size(), 2u);
    EXPECT_EQ(requests[1], "POST /color " + color_request({3, 0, 0}));
    EXPECT_EQ(service.serial(Call::color), 1u);
}

TEST(SignalLink, FindsTheServiceAndItsConfig)
{
    auto fake = std::make_unique<FakeSignal>();
    Link link(std::move(fake));
    EXPECT_EQ(link.state(), LinkState::checking);
    link.update(0.0f);
    link.service().wait_idle();
    link.update(1.0f / 60.0f);
    EXPECT_TRUE(link.online());
    ASSERT_TRUE(link.has_config());
    EXPECT_EQ(link.config(), default_config());
    EXPECT_EQ(link.status().version, "0.1.0");
}

TEST(SignalLink, SaysWhenTheServiceDoesNotRunAndChecksAgain)
{
    auto fake = std::make_unique<FakeSignal>();
    FakeSignal *signal = fake.get();
    signal->set_running(false);
    Link link(std::move(fake));
    link.update(0.0f);
    link.service().wait_idle();
    link.update(1.0f / 60.0f);
    EXPECT_EQ(link.state(), LinkState::offline);
    EXPECT_FALSE(link.has_config());

    signal->set_running(true);
    link.update(3.1f); // the next check is due
    link.service().wait_idle();
    link.update(1.0f / 60.0f);
    EXPECT_TRUE(link.online());
    EXPECT_TRUE(link.has_config());
}

TEST(SignalLink, ReadsTheConfigBackAfterASave)
{
    auto fake = std::make_unique<FakeSignal>();
    FakeSignal *signal = fake.get();
    Link link(std::move(fake));
    link.update(0.0f);
    link.service().wait_idle();
    link.update(0.0f);
    const unsigned before = link.config_serial();

    Config config = link.config();
    config.hold = true;
    config.held = {5, 6, 7};
    link.service().send(Call::save_config, config_json(config));
    link.service().wait_idle();
    link.update(0.0f); // sees the save, asks for the config again
    link.service().wait_idle();
    link.update(0.0f);
    EXPECT_GT(link.config_serial(), before);
    EXPECT_EQ(link.config(), config);
    EXPECT_EQ(signal->saved(), config_json(config));
}

// ---- versions and ps5-autoloader's copy ----------------------------------------

TEST(SignalVersion, ReadsTheMarker)
{
    const std::string elf = std::string("\x7f"
                                        "ELF") +
                            std::string(100, '\0') + "PS5SIGNAL_VER:0.2.0" + std::string(1, '\0') +
                            "more";
    EXPECT_EQ(payload_version(elf), "0.2.0");
    EXPECT_EQ(payload_version("no marker here"), "");
    EXPECT_EQ(payload_version("PS5SIGNAL_VER:"), "");
    EXPECT_EQ(payload_version("PS5SIGNAL_VER:.1"), "");
    EXPECT_EQ(payload_version("PS5SIGNAL_VER:1.2.3x"), "1.2.3");
}

TEST(SignalVersion, ComparesDottedNumbers)
{
    EXPECT_LT(compare_versions("0.1.0", "0.2.0"), 0);
    EXPECT_GT(compare_versions("0.10.0", "0.9.1"), 0);
    EXPECT_EQ(compare_versions("0.2", "0.2.0"), 0);
    EXPECT_LT(compare_versions("", "0.2.0"), 0); // older than 0.2.0: no marker
    EXPECT_GT(compare_versions("0.2.0", ""), 0);
    EXPECT_EQ(compare_versions("", ""), 0);
}

namespace
{

std::string elf_with(const std::string &version)
{
    std::string elf = std::string("\x7f"
                                  "ELF") +
                      std::string(100, 'x');
    if (!version.empty())
        elf += "PS5SIGNAL_VER:" + version + std::string(1, '\0');
    return elf;
}

void write(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream(path, std::ios::binary) << text;
}

std::string read(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// An app folder with a payload of this version, and ps5-autoloader's folder listing ps5-signal.
struct AutoloadFolders
{
    std::filesystem::path root;
    std::filesystem::path loader;

    explicit AutoloadFolders(const std::string &app_version)
    {
        root = std::filesystem::temp_directory_path() /
               ("signal-autoload-" + std::to_string(std::rand()));
        loader = root / "ps5_autoloader";
        std::filesystem::create_directories(root / "assets" / "payload");
        std::filesystem::create_directories(loader);
        write(root / "assets" / "payload" / "ps5-signal.elf", elf_with(app_version));
        write(loader / "autoload.txt", "other.elf\n!3000\nps5-signal.elf\n");
        set_paths((root / "assets").string(), (root / "data").string());
    }
    ~AutoloadFolders()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};

} // namespace

TEST(SignalAutoload, ReplacesAnOlderCopy)
{
    AutoloadFolders folders("0.2.0");
    write(folders.loader / "ps5-signal.elf", elf_with("")); // 0.1.0 had no marker
    const AutoloadRefresh refresh = autoload_refresh(folders.loader.string());
    EXPECT_TRUE(refresh.updated);
    EXPECT_EQ(refresh.from, "");
    EXPECT_EQ(refresh.to, "0.2.0");
    EXPECT_EQ(read(folders.loader / "ps5-signal.elf"), elf_with("0.2.0"));
}

TEST(SignalAutoload, KeepsTheSameOrANewerCopy)
{
    for (const char *version : {"0.2.0", "0.3.0"})
    {
        AutoloadFolders folders("0.2.0");
        write(folders.loader / "ps5-signal.elf", elf_with(version));
        EXPECT_FALSE(autoload_refresh(folders.loader.string()).updated) << version;
        EXPECT_EQ(read(folders.loader / "ps5-signal.elf"), elf_with(version));
    }
}

TEST(SignalAutoload, LeavesTheFolderAloneWhenNotListed)
{
    AutoloadFolders folders("0.2.0");
    write(folders.loader / "autoload.txt", "other.elf\n");
    write(folders.loader / "ps5-signal.elf", elf_with(""));
    EXPECT_FALSE(autoload_refresh(folders.loader.string()).updated);
    EXPECT_EQ(read(folders.loader / "ps5-signal.elf"), elf_with(""));
}

} // namespace
