// Signal Companion - Screen "Startup": edit the tune and the light ps5-signal plays when it starts.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Two forms side by side: the tune on the left (up to 16 beeps, each a tone
// and the wait until the next), the light on the right (up to 32 steps, each
// a colour or a brightness and how long it shows). Both can be tried on the
// console and are saved together with PUT /config; the kept colour in the
// config is sent back as the service had it.
//
// Left and right edit the focused row; Triangle changes column, and so do
// left and right on a row that has no use for them (an action).

#include "concepts/concepts.hpp"

#include "concepts/companion/look.hpp"
#include "core/tween.hpp"
#include "ui/components/form.hpp"
#include "ui/components/toast.hpp"
#include "ui/widgets.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;
using namespace companion;

constexpr float kGap = 48.0f;
constexpr float kColumn = (kRight - kMargin - kGap) * 0.5f;
constexpr Rect kTuneForm{kMargin, kTop, kColumn, kBottom - kTop};
constexpr Rect kLightForm{kMargin + kColumn + kGap, kTop, kColumn, kBottom - kTop};
constexpr int kPauseStep = 50;
constexpr int kStepMsStep = 50;

enum Id
{
    kTuneOn = 1,
    kAddBeep,
    kRemoveBeep,
    kPlayTune,
    kSave,
    kRevert,
    kDefaults,
    kLightOn,
    kAddStep,
    kRemoveStep,
    kPlayLight,
    kToneBase = 100,  // + beep index
    kPauseBase = 200, // + beep index
    kStepBase = 300,  // + light step index
    kMsBase = 400,    // + light step index
};

// A light step's choice: the four names, the three brightness levels, and
// for a step that mixes channels itself, that mix.
constexpr int kNamedOptions = signal::kLedNames;
constexpr int kLevelOptions = signal::kLevels;
constexpr int kMixOption = kNamedOptions + kLevelOptions;

constexpr const char *kTechniques[] = {
    "ps5-signal plays this tune and light every time it starts",
    ("Up to 16 beeps: a tone and the wait until the next one. The service plays 6 beeps at a "
     "time, so \"Play it\" plays the first 6"),
    "Up to 32 light steps: a colour or a brightness, and how long it shows",
    "Square saves both to config.json on the console",
};

constexpr ui::Hint kHintsEdit[] = {
    {ui::Button::dpad, "Move, change"},
    {ui::Button::cross, "Choose"},
    {ui::Button::triangle, "Other column"},
    {ui::Button::square, "Save"},
};

constexpr app::TourStep kTour[] = {
    {1.2f, 0, Direction::none, "startup"},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::right},
    {0.6f, 0, Direction::right, "startup-tune"},
    {0.4f, action_bit(Action::north)},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::right},
    {0.6f, 0, Direction::down, "startup-light"},
};

class StartupScreen final : public app::Concept
{
  public:
    explicit StartupScreen(app::Context &context) : context_(context)
    {
        edit_ = signal::default_config();
        rebuild();
        toasts_.style.theme = theme();
        toasts_.style.anchor = ui::ToastAnchor::bottom_center;
        toasts_.style.margin = 120.0f;
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "startup",
            "Startup",
            "The tune and the light ps5-signal plays when it starts",
            "src/concepts/startup.cpp",
            audio::SoundSet::paper,
            Color::rgb(0xff5c8a),
            kTechniques,
        };
        return kInfo;
    }

    void enter() override
    {
        age_ = 0.0f;
        tune_.enter();
        light_.enter();
        signal::link().refresh();
    }

    void update(const InputFrame &input, float dt, app::Feedback &feedback) override
    {
        clock_ += dt;
        age_ += dt;
        signal::Link &link = signal::link();
        link.update(dt);
        restyle();
        load(link);

        if (input.is_pressed(Action::north))
        {
            set_column(1 - column_, feedback);
        }
        else if (input.is_pressed(Action::west))
        {
            save(link, feedback);
        }
        else
        {
            ui::Form &form = column_ == 0 ? tune_ : light_;
            const bool sideways = input.nav == Direction::left || input.nav == Direction::right;
            if (sideways && !form.uses_horizontal())
            {
                if ((input.nav == Direction::right) == (column_ == 0))
                    set_column(1 - column_, feedback);
                else
                    ui::refuse(feedback, form.style, input, edge_, form.bounds().cx());
            }
            else
            {
                const ui::Event event = form.handle(input, feedback);
                if (event == ui::Event::changed)
                    changed(form.changed_id());
                else if (event == ui::Event::activated)
                    act(form.changed_id(), link, feedback);
            }
        }
        enable(link);

        signal::Reply reply;
        if (request_.done(link, reply))
            report(reply);
        tune_.set_active(column_ == 0);
        light_.set_active(column_ == 1);
        tune_.update(dt);
        light_.update(dt);
        toasts_.update(dt, feedback);
        edge_.update(dt, 9.0f);
    }

    void draw(app::Frame &frame) const override
    {
        const ui::Theme &look = theme();
        begin_frame(frame, look);
        ui::Canvas canvas{frame.scene, context_.fonts, 0, clock_};
        ui::Painter paint(frame.scene, context_.fonts, look);
        const signal::Link &link = signal::link();
        frame.scene.push_opacity(tween::cubic_out(age_ / 0.3f));
        draw_header(paint, "Startup", "What ps5-signal plays when it starts", link, clock_);
        draw_column_titles(paint, link);
        tune_.draw(canvas);
        light_.draw(canvas);
        draw_hints(frame.scene, context_.fonts, look, kHintsEdit);
        frame.scene.pop_opacity();
        ui::Canvas above{frame.overlay, context_.fonts, 0, clock_};
        toasts_.draw(above);
    }

    std::span<const app::TourStep> tour() const override
    {
        return kTour;
    }

  private:
    // ---- the forms ------------------------------------------------------------

    void rebuild()
    {
        const int tune_focus = tune_.row_count() > 0 ? tune_.focus_id() : kTuneOn;
        const int light_focus = light_.row_count() > 0 ? light_.focus_id() : kLightOn;
        tune_.clear();
        light_.clear();

        char label[32];
        tune_.add_toggle(kTuneOn, "Play a tune", tune_on_).description =
            "Off: ps5-signal starts without a sound";
        tune_.add_header("Beeps");
        const int beeps = static_cast<int>(edit_.beeps.size());
        for (int i = 0; i < beeps; ++i)
        {
            const signal::BeepStep &step = edit_.beeps[static_cast<std::size_t>(i)];
            std::snprintf(label, sizeof(label), "Beep %d", i + 1);
            std::vector<std::string> tones;
            for (int t = 0; t < signal::kTones; ++t)
                tones.emplace_back(signal::tone_name(t));
            ui::FormRow &tone = tune_.add_choice(kToneBase + i, label, std::move(tones), step.tone);
            tone.disabled = !tune_on_;
            if (i + 1 < beeps)
            {
                ui::FormRow &pause =
                    tune_.add_stepper(kPauseBase + i, "   then wait", step.pause_ms,
                                      signal::kMinPauseMs, signal::kMaxPauseMs, kPauseStep);
                pause.stepper.set_suffix(" ms");
                pause.disabled = !tune_on_;
            }
        }
        tune_.add_action(kAddBeep, "Add a beep");
        tune_.add_action(kRemoveBeep, "Remove the last beep");
        tune_.add_action(kPlayTune, "Play it").description =
            "The service plays 6 beeps at a time: a longer tune plays its first 6";
        tune_.add_header("Config");
        tune_.add_action(kSave, "Save to the console").description =
            "Writes the tune and the light to config.json";
        tune_.add_action(kRevert, "Undo my changes").description =
            "Back to what the console has saved";
        tune_.add_action(kDefaults, "Use the default").description =
            "The service's own tune and blue, white, blue";

        light_.add_toggle(kLightOn, "Light up", light_on_).description =
            "Off: ps5-signal starts without changing the light";
        light_.add_header("Steps");
        const int steps = static_cast<int>(edit_.leds.size());
        for (int i = 0; i < steps; ++i)
        {
            const signal::LedStep &step = edit_.leds[static_cast<std::size_t>(i)];
            std::snprintf(label, sizeof(label), "Step %d", i + 1);
            std::vector<std::string> options;
            for (int n = 0; n < kNamedOptions; ++n)
                options.emplace_back(signal::led_name(static_cast<signal::LedName>(n)));
            for (int l = 0; l < kLevelOptions; ++l)
                options.emplace_back(signal::level_name(l));
            if (step.kind == signal::LedKind::mix)
                options.push_back(signal::led_step_text(step));
            ui::FormRow &kind =
                light_.add_choice(kStepBase + i, label, std::move(options), option_of(step));
            kind.disabled = !light_on_;
            ui::FormRow &ms = light_.add_stepper(kMsBase + i, "   for", step.ms, signal::kMinStepMs,
                                                 signal::kMaxStepMs, kStepMsStep);
            ms.stepper.set_suffix(" ms");
            ms.disabled = !light_on_;
        }
        light_.add_action(kAddStep, "Add a step");
        light_.add_action(kRemoveStep, "Remove the last step");
        light_.add_action(kPlayLight, "Play it").description =
            "Then the light returns to its colour";

        tune_.focus_row(tune_.row(tune_focus) != nullptr ? tune_focus : kAddBeep);
        light_.focus_row(light_.row(light_focus) != nullptr ? light_focus : kAddStep);
    }

    static int option_of(const signal::LedStep &step)
    {
        switch (step.kind)
        {
        case signal::LedKind::named:
            return static_cast<int>(step.name);
        case signal::LedKind::level:
            return kNamedOptions + step.level;
        case signal::LedKind::mix:
            break;
        }
        return kMixOption;
    }

    void restyle()
    {
        if (styled_ && reduced_ == context_.settings.reduced_motion)
            return;
        styled_ = true;
        reduced_ = context_.settings.reduced_motion;
        ui::FormStyle style;
        style.theme = theme();
        style.reduced_motion = reduced_;
        style.compact = true;
        style.label_ratio = 0.38f;
        style.control_width = 300.0f;
        style.stepper_width = 250.0f; // room for "10000 ms" between the buttons
        tune_.style = style;
        light_.style = style;
        tune_.set_bounds(kTuneForm);
        light_.set_bounds(kLightForm);
        toasts_.style.reduced_motion = reduced_;
    }

    // ---- the config -------------------------------------------------------------

    // What would be saved: the edited steps, the kept colour as the service has it.
    signal::Config result(const signal::Link &link) const
    {
        signal::Config config = edit_;
        if (!tune_on_)
            config.beeps.clear();
        if (!light_on_)
            config.leds.clear();
        config.hold = link.config().hold;
        config.held = link.config().held;
        return config;
    }

    bool unsaved(const signal::Link &link) const
    {
        return link.has_config() && result(link) != link.config();
    }

    // The saved config, when it arrives; never over changes not saved yet.
    void load(const signal::Link &link)
    {
        if (!link.has_config() || link.config_serial() == loaded_serial_ || dirty_)
            return;
        loaded_serial_ = link.config_serial();
        take(link.config());
    }

    void take(const signal::Config &config)
    {
        const signal::Config fallback = signal::default_config();
        edit_.beeps = config.beeps.empty() ? fallback.beeps : config.beeps;
        edit_.leds = config.leds.empty() ? fallback.leds : config.leds;
        tune_on_ = !config.beeps.empty();
        light_on_ = !config.leds.empty();
        rebuild();
    }

    void changed(int id)
    {
        dirty_ = true;
        if (id == kTuneOn || id == kLightOn)
        {
            tune_on_ = tune_.toggle_value(kTuneOn);
            light_on_ = light_.toggle_value(kLightOn);
            rebuild();
        }
        else if (id >= kMsBase)
        {
            edit_.leds[static_cast<std::size_t>(id - kMsBase)].ms = light_.stepper_value(id);
        }
        else if (id >= kStepBase)
        {
            signal::LedStep &step = edit_.leds[static_cast<std::size_t>(id - kStepBase)];
            const int option = light_.choice_index(id);
            if (option < kNamedOptions)
            {
                step.kind = signal::LedKind::named;
                step.name = static_cast<signal::LedName>(option);
            }
            else if (option < kMixOption)
            {
                step.kind = signal::LedKind::level;
                step.level = option - kNamedOptions;
            }
            else
            {
                step.kind = signal::LedKind::mix; // its own channels, kept as they were
            }
        }
        else if (id >= kPauseBase)
        {
            edit_.beeps[static_cast<std::size_t>(id - kPauseBase)].pause_ms =
                tune_.stepper_value(id);
        }
        else if (id >= kToneBase)
        {
            edit_.beeps[static_cast<std::size_t>(id - kToneBase)].tone = tune_.choice_index(id);
        }
    }

    void enable(const signal::Link &link)
    {
        const bool online = link.online();
        const int beeps = static_cast<int>(edit_.beeps.size());
        const int steps = static_cast<int>(edit_.leds.size());
        tune_.set_disabled(kAddBeep, !tune_on_ || beeps >= signal::kMaxConfigBeeps);
        tune_.set_disabled(kRemoveBeep, !tune_on_ || beeps <= 1);
        tune_.set_disabled(kPlayTune, !online || !tune_on_);
        tune_.set_disabled(kSave, !online || !link.has_config() || !unsaved(link));
        tune_.set_disabled(kRevert, !link.has_config() || !unsaved(link));
        light_.set_disabled(kAddStep, !light_on_ || steps >= signal::kMaxLedSteps);
        light_.set_disabled(kRemoveStep, !light_on_ || steps <= 1);
        light_.set_disabled(kPlayLight, !online || !light_on_);
    }

    void act(int id, signal::Link &link, app::Feedback &feedback)
    {
        switch (id)
        {
        case kAddBeep:
            edit_.beeps.push_back(edit_.beeps.back());
            dirty_ = true;
            rebuild();
            tune_.focus_row(kToneBase + static_cast<int>(edit_.beeps.size()) - 1);
            break;
        case kRemoveBeep:
            edit_.beeps.pop_back();
            dirty_ = true;
            rebuild();
            break;
        case kPlayTune:
            request_.start(link, signal::Call::beep, signal::beep_request(edit_.beeps));
            done_text_ = "Playing the tune";
            break;
        case kAddStep:
            edit_.leds.push_back(edit_.leds.back());
            dirty_ = true;
            rebuild();
            light_.focus_row(kStepBase + static_cast<int>(edit_.leds.size()) - 1);
            break;
        case kRemoveStep:
            edit_.leds.pop_back();
            dirty_ = true;
            rebuild();
            break;
        case kPlayLight:
            request_.start(link, signal::Call::led, signal::led_request(edit_.leds));
            done_text_ = "Playing the light";
            break;
        case kSave:
            save(link, feedback);
            break;
        case kRevert:
            dirty_ = false;
            loaded_serial_ = 0;
            break;
        case kDefaults:
            take(signal::default_config());
            dirty_ = true;
            break;
        default:
            break;
        }
    }

    void save(signal::Link &link, app::Feedback &feedback)
    {
        if (!link.online() || !link.has_config())
        {
            ui::refuse(feedback, tune_.style, InputFrame{}, edge_, kTuneForm.cx());
            toasts_.push(ui::StatusKind::warning, "Not saved",
                         "The service must run to save the startup signal");
            return;
        }
        request_.start(link, signal::Call::save_config, signal::config_json(result(link)));
        done_text_ = "Saved: plays at the next start";
        saving_ = true;
    }

    void report(const signal::Reply &reply)
    {
        const std::string problem = reply_problem(reply);
        if (problem.empty())
        {
            toasts_.push(ui::StatusKind::success, done_text_);
            if (saving_)
                dirty_ = false; // the service's copy is this one now
        }
        else
        {
            toasts_.push(ui::StatusKind::warning, saving_ ? "Not saved" : "Not done", problem);
        }
        saving_ = false;
    }

    void set_column(int column, app::Feedback &feedback)
    {
        column_ = column;
        ui::play_cue(feedback, tune_.style, tune_.style.sounds.move,
                     column_ == 0 ? kTuneForm.cx() : kLightForm.cx());
    }

    // ---- drawing ------------------------------------------------------------------

    void draw_column_titles(ui::Painter &paint, const signal::Link &link) const
    {
        const ui::Theme &look = paint.theme();
        char text[48];
        std::snprintf(text, sizeof(text), "TUNE  %zu / %d BEEPS", edit_.beeps.size(),
                      signal::kMaxConfigBeeps);
        paint.label(text, kTuneForm.x, kTop - 22.0f, 20.0f,
                    column_ == 0 ? look.text : look.text_muted);
        std::snprintf(text, sizeof(text), "LIGHT  %zu / %d STEPS", edit_.leds.size(),
                      signal::kMaxLedSteps);
        paint.label(text, kLightForm.x, kTop - 22.0f, 20.0f,
                    column_ == 1 ? look.text : look.text_muted);
        if (unsaved(link))
        {
            const char *note = "NOT SAVED YET";
            const float width = paint.label_width(note, 20.0f) + 40.0f;
            const Rect sticker{kRight - width, kTop - 54.0f, width, 44.0f};
            const Rect inside =
                paint.surface(sticker, 0.0f, look.warning, look.outline, 1.0f, 3.0f);
            paint.label(note, inside.cx(), inside.cy() + 7.0f, 20.0f, look.text,
                        gfx::Align::center);
        }
        else if (link.state() == signal::LinkState::offline)
        {
            paint.body("Start ps5-signal to try and save this (see Status)", kRight, kTop - 22.0f,
                       21.0f, look.text_muted, gfx::Align::right);
        }
    }

    app::Context &context_;
    ui::Form tune_;
    ui::Form light_;
    ui::ToastStack toasts_;
    Request request_;
    signal::Config edit_;
    bool tune_on_ = true;
    bool light_on_ = true;
    bool dirty_ = false;
    bool saving_ = false;
    unsigned loaded_serial_ = 0;
    const char *done_text_ = "";
    int column_ = 0;
    ui::Pulse edge_;
    float clock_ = 0.0f;
    float age_ = 0.0f;
    bool reduced_ = false;
    bool styled_ = false;
};

} // namespace

std::unique_ptr<app::Concept> make_startup(app::Context &context)
{
    return std::make_unique<StartupScreen>(context);
}

} // namespace hui::concepts
