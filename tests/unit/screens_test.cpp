// Signal Companion - Tests of the three screens against a ps5-signal service in memory.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concept_fixture.hpp"
#include "fake_signal.hpp"

#include "concepts/concepts.hpp"
#include "core/save_file.hpp"
#include "signal/palette.hpp"
#include "signal/service.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

namespace
{

using hui::Action;
using hui::Direction;
using hui::app::Concept;
using hui::testing::ConceptFixture;
using hui::testing::FakeSignal;
namespace signal = hui::signal;

class Screens : public ConceptFixture
{
  protected:
    Screens()
    {
        auto fake = std::make_unique<FakeSignal>();
        fake_ = fake.get();
        signal::install_link(std::move(fake));
    }
    ~Screens() override
    {
        // Back to a service that does not run, so no later test talks to this fake.
        auto idle_service = std::make_unique<FakeSignal>();
        idle_service->set_running(false);
        signal::install_link(std::move(idle_service));
    }

    // Lets the worker answer and the screen read the answers.
    void settle(Concept &design, float seconds = 0.1f)
    {
        for (int i = 0; i < 4; ++i)
        {
            signal::link().service().wait_idle();
            send(design, idle(), seconds / 4.0f);
        }
        signal::link().service().wait_idle();
        send(design, idle(), 0.02f);
    }

    void move(Concept &design, Direction direction, int times = 1)
    {
        for (int i = 0; i < times; ++i)
            send(design, nav(direction), 0.05f);
    }

    bool asked(const std::string &start) const
    {
        const std::vector<std::string> requests = fake_->requests();
        return std::any_of(requests.begin(), requests.end(),
                           [&](const std::string &r) { return r.rfind(start, 0) == 0; });
    }

    FakeSignal *fake_ = nullptr;
};

TEST_F(Screens, StatusFindsTheServiceAndTriesTheBeeper)
{
    const std::unique_ptr<Concept> design = hui::concepts::make_status(context_);
    design->enter();
    settle(*design);
    EXPECT_TRUE(signal::link().online());
    EXPECT_TRUE(asked("GET /status"));
    expect_drawable(*design);

    // The values are not focusable: the focus starts on "Play a short beep".
    send(*design, press(Action::confirm), 0.05f);
    settle(*design);
    EXPECT_TRUE(asked("POST /beep"));
    expect_drawable(*design);
}

TEST_F(Screens, StatusExplainsHowToStartTheService)
{
    fake_->set_running(false);
    const std::unique_ptr<Concept> design = hui::concepts::make_status(context_);
    design->enter();
    settle(*design);
    EXPECT_EQ(signal::link().state(), signal::LinkState::offline);
    expect_drawable(*design);
    // The tests of the console are refused; nothing reaches the service.
    send(*design, press(Action::confirm), 0.05f);
    settle(*design);
    EXPECT_FALSE(asked("POST /beep"));
}

TEST_F(Screens, ColourTriesAPresetOnTheConsoleAndKeepsIt)
{
    const std::unique_ptr<Concept> design = hui::concepts::make_colour(context_);
    design->enter();
    settle(*design);
    ASSERT_TRUE(signal::link().has_config());

    move(*design, Direction::down, 3); // Preset
    // The light shows blue (the fake's status), so the preset reads "Blue".
    move(*design, Direction::right); // "White"
    settle(*design, 0.6f);
    EXPECT_TRUE(asked("POST /color {\"blue\": 0, \"white\": 255, \"amber\": 0}"));
    expect_drawable(*design);

    move(*design, Direction::down, 2); // Keep this colour
    send(*design, press(Action::confirm), 0.05f);
    settle(*design);
    signal::Config saved;
    std::string error;
    ASSERT_TRUE(signal::parse_config(fake_->saved(), saved, error)) << error;
    EXPECT_TRUE(saved.hold);
    EXPECT_EQ(saved.held, (signal::Mix{0, 255, 0}));
    // The startup signal went back as the service had it.
    EXPECT_EQ(saved.beeps, signal::default_config().beeps);
    EXPECT_EQ(saved.leds, signal::default_config().leds);
}

TEST_F(Screens, ColourCanGiveTheLightBackToTheSystem)
{
    const std::unique_ptr<Concept> design = hui::concepts::make_colour(context_);
    design->enter();
    settle(*design);
    move(*design, Direction::down, 6); // Use the system's colour
    send(*design, press(Action::confirm), 0.05f);
    settle(*design);
    signal::Config saved;
    std::string error;
    ASSERT_TRUE(signal::parse_config(fake_->saved(), saved, error)) << error;
    EXPECT_FALSE(saved.hold);
}

TEST_F(Screens, StartupAddsABeepAndSavesBothParts)
{
    const std::unique_ptr<Concept> design = hui::concepts::make_startup(context_);
    design->enter();
    settle(*design);

    // The toggle, then 7 beeps and the 6 waits between them, then "Add a beep".
    move(*design, Direction::down, 14);
    send(*design, press(Action::confirm), 0.05f);
    expect_drawable(*design);
    send(*design, press(Action::west), 0.05f); // Square saves
    settle(*design);

    signal::Config saved;
    std::string error;
    ASSERT_TRUE(signal::parse_config(fake_->saved(), saved, error)) << error;
    EXPECT_EQ(saved.beeps.size(), 8u);
    EXPECT_EQ(saved.leds, signal::default_config().leds);
    expect_drawable(*design);
}

TEST_F(Screens, StartupCanTurnTheTuneOff)
{
    const std::unique_ptr<Concept> design = hui::concepts::make_startup(context_);
    design->enter();
    settle(*design);
    send(*design, press(Action::confirm), 0.05f); // "Play a tune" off
    send(*design, press(Action::west), 0.05f);
    settle(*design);
    signal::Config saved;
    std::string error;
    ASSERT_TRUE(signal::parse_config(fake_->saved(), saved, error)) << error;
    EXPECT_TRUE(saved.beeps.empty());
    EXPECT_EQ(saved.leds.size(), 3u);
}

TEST_F(Screens, StartupDoesNotSaveWithoutTheService)
{
    fake_->set_running(false);
    const std::unique_ptr<Concept> design = hui::concepts::make_startup(context_);
    design->enter();
    settle(*design);
    send(*design, press(Action::west), 0.05f);
    settle(*design);
    EXPECT_FALSE(asked("PUT /config"));
    expect_drawable(*design);
}

} // namespace

namespace
{

using hui::Action;
using hui::Direction;
using hui::app::Concept;
using hui::testing::FakeSignal;
namespace signal = hui::signal;

// A folder per test with an app's assets (a stand-in payload) and data.
class ScreensWithFiles : public Screens
{
  protected:
    ScreensWithFiles()
    {
        root_ = std::filesystem::temp_directory_path() /
                ("signal-companion-" +
                 std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_ / "assets" / "payload");
        std::filesystem::create_directories(root_ / "data");
        std::string elf = "\x7f"
                          "ELF";
        elf.resize(256, '\0');
        hui::save::write_atomic((root_ / "assets" / "payload" / "ps5-signal.elf").string(), elf);
        signal::set_paths((root_ / "assets").string(), (root_ / "data").string());
        // A fresh link, so it looks for the payload in this folder.
        auto fake = std::make_unique<FakeSignal>();
        fake_ = fake.get();
        signal::install_link(std::move(fake));
    }
    ~ScreensWithFiles() override
    {
        signal::set_paths("/app0/assets", "/download0/hui");
        std::filesystem::remove_all(root_);
    }

    std::filesystem::path root_;
};

TEST_F(ScreensWithFiles, StatusOffersToStartTheServiceAndStartsIt)
{
    fake_->set_running(false);
    fake_->set_loader(true, true);
    const std::unique_ptr<Concept> design = hui::concepts::make_status(context_);
    design->enter();
    settle(*design);
    ASSERT_EQ(signal::link().state(), signal::LinkState::offline);
    EXPECT_TRUE(signal::link().can_start());
    expect_drawable(*design); // the dialog is up

    send(*design, press(Action::confirm), 0.05f); // "Start it"
    settle(*design, 0.3f);
    EXPECT_TRUE(asked("PUSH 9021 256"));
    settle(*design, 1.5f);
    EXPECT_TRUE(signal::link().online());
    EXPECT_EQ(signal::link().start_state(), signal::StartState::idle);
    expect_drawable(*design);
}

TEST_F(ScreensWithFiles, AStartThatFailsSaysWhy)
{
    fake_->set_running(false);
    fake_->set_loader(false, false);
    signal::Link &link = signal::link();
    link.update(0.0f);
    link.service().wait_idle();
    link.update(0.0f);
    link.start_service();
    link.service().wait_idle();
    link.update(0.0f);
    EXPECT_EQ(link.start_state(), signal::StartState::failed);
    EXPECT_NE(link.start_error().find("9021"), std::string::npos);

    // The loader takes it, but the service never answers.
    fake_->set_loader(true, false);
    link.start_service();
    for (int i = 0; i < 20; ++i)
    {
        link.service().wait_idle();
        link.update(0.5f);
    }
    EXPECT_EQ(link.start_state(), signal::StartState::failed);
    EXPECT_NE(link.start_error().find("did not answer"), std::string::npos);
}

TEST_F(ScreensWithFiles, ColourStoresAMixAndBringsItBack)
{
    const std::unique_ptr<Concept> design = hui::concepts::make_colour(context_);
    design->enter();
    settle(*design);
    move(*design, Direction::down, 3);  // Preset
    move(*design, Direction::right, 2); // "Amber"
    // Blue, White, Amber, Preset, Show, Keep, System, Stop trying, then My colours.
    move(*design, Direction::down, 5); // the colour slot
    move(*design, Direction::right);   // slot 2
    move(*design, Direction::down);    // Store the mix here
    send(*design, press(Action::confirm), 0.05f);
    settle(*design);

    const signal::Palette palette = signal::load_palette();
    ASSERT_TRUE(palette.slots[1].has_value());
    EXPECT_EQ(*palette.slots[1], (signal::Mix{0, 0, 255}));
    expect_drawable(*design);

    // A new screen reads it back; picking the slot puts it on the sliders.
    const std::unique_ptr<Concept> again = hui::concepts::make_colour(context_);
    again->enter();
    settle(*again);
    move(*again, Direction::down, 8);
    move(*again, Direction::right);
    settle(*again, 0.6f);
    EXPECT_TRUE(asked("POST /color {\"blue\": 0, \"white\": 0, \"amber\": 255}"));
}

TEST(SignalPalette, SurvivesARoundTripAndRefusesNonsense)
{
    signal::Palette palette;
    palette.slots[0] = signal::Mix{1, 2, 3};
    palette.slots[5] = signal::Mix{255, 0, 255};
    signal::Palette back;
    ASSERT_TRUE(signal::parse_palette(signal::palette_json(palette), back));
    EXPECT_EQ(back, palette);
    for (const char *text : {"", "[]", R"({"colours": [{"blue": 256}]})", R"({"colours": [1]})"})
        EXPECT_FALSE(signal::parse_palette(text, back)) << text;
}

TEST_F(Screens, StartupDrawsTimelinesAndPlaysTheTune)
{
    const std::unique_ptr<Concept> design = hui::concepts::make_startup(context_);
    design->enter();
    settle(*design);
    move(*design, Direction::down, 14 + 2); // past "Add a beep" and "Remove", to "Play it"
    send(*design, press(Action::confirm), 0.05f);
    settle(*design, 0.3f);
    EXPECT_TRUE(asked("POST /beep"));
    expect_drawable(*design); // with the cursor on the timeline
}

} // namespace
