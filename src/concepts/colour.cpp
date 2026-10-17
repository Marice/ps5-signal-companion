// Signal Companion - Screen "Colour": mix the light's blue, white and amber and keep it.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Three sliders and a few presets. While "Show it on the console" is on, the
// light follows the sliders through POST /color, at most twice a second (the
// service allows 120 light changes a minute). "Keep this colour" writes it to
// config.json as led_color with PUT /config, so the service sets it at every
// start; the rest of the config is sent back as the service had it.

#include "concepts/concepts.hpp"

#include "concepts/companion/look.hpp"
#include "core/tween.hpp"
#include "signal/palette.hpp"
#include "ui/components/form.hpp"
#include "ui/components/toast.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;
using namespace companion;

constexpr Rect kForm{kMargin, kTop, 860.0f, kBottom - kTop};
constexpr Rect kSide{1010.0f, kTop + 10.0f, kRight - 1010.0f, 600.0f};
constexpr float kSendEvery = 0.5f;

enum Id
{
    kBlue = 1,
    kWhite,
    kAmber,
    kPreset,
    kLive,
    kSave,
    kSystem,
    kUndo,
    kSlot,
    kStore,
    kTemperatureLed,
    kCool,
    kWarm,
};

// A change to the temperature settings is saved once they stay put this long.
constexpr float kTemperatureSaveAfter = 0.8f;

struct Preset
{
    const char *name;
    signal::Mix mix;
};

// The first is a label for "the sliders say something else".
constexpr Preset kPresets[] = {
    {"Custom", {}},
    {"Blue", {255, 0, 0}},
    {"White", {0, 255, 0}},
    {"Amber", {0, 0, 255}},
    {"Warm white", {0, 255, 120}},
    {"Ice", {255, 160, 0}},
    {"Dusk", {200, 0, 255}},
    {"Off", {0, 0, 0}},
};
constexpr int kPresetCount = static_cast<int>(std::size(kPresets));

constexpr const char *kTechniques[] = {
    ("The light has blue, white and amber LEDs: mix them for other shades. There is no free RGB "
     "colour"),
    "With \"Show it on the console\" on, the light follows the sliders while you edit",
    ("\"Keep this colour\" saves it in config.json: ps5-signal sets it at every start and when "
     "the system takes the light back"),
    "\"Use the system's colour\" lets the console decide again",
};

constexpr ui::Hint kHints[] = {
    {ui::Button::dpad, "Move, change"},
    {ui::Button::cross, "Choose"},
};

constexpr app::TourStep kTour[] = {
    {1.2f, 0, Direction::none, "colour"},
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::right},
    {0.8f, 0, Direction::down, "colour-mix"},
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::right},
    {0.2f, 0, Direction::right},
    {0.9f, 0, Direction::down, "colour-preset"},
    {0.3f, 0, Direction::down},
    {0.6f, 0, Direction::down, "colour-keep"},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::right},
    {0.8f, 0, Direction::down, "colour-mine"},
    {0.2f, 0, Direction::down},
    {0.3f, 0, Direction::right},
    {2.0f, 0, Direction::down, "colour-temperature"},
};

class ColourScreen final : public app::Concept
{
  public:
    explicit ColourScreen(app::Context &context) : context_(context)
    {
        build_form();
        toasts_.style.theme = theme();
        toasts_.style.anchor = ui::ToastAnchor::bottom_right;
        toasts_.style.margin = 120.0f;
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "colour",
            "Colour",
            "Mix the light's blue, white and amber, try it on the console and keep it",
            "src/concepts/colour.cpp",
            audio::SoundSet::paper,
            Color::rgb(0x2f5bff),
            kTechniques,
        };
        return kInfo;
    }

    void enter() override
    {
        age_ = 0.0f;
        form_.enter();
        signal::link().refresh();
    }

    void update(const InputFrame &input, float dt, app::Feedback &feedback) override
    {
        clock_ += dt;
        age_ += dt;
        since_sent_ += dt;
        signal::Link &link = signal::link();
        link.update(dt);
        restyle();
        load(link);

        switch (form_.handle(input, feedback))
        {
        case ui::Event::changed:
            changed(form_.changed_id());
            break;
        case ui::Event::activated:
            act(form_.changed_id(), link);
            break;
        default:
            break;
        }
        enable(link);
        try_on_console(link);
        since_temperature_change_ += dt;
        if (temperature_dirty_ && since_temperature_change_ >= kTemperatureSaveAfter)
            save_temperature(link);

        signal::Reply reply;
        if (request_.done(link, reply))
            report(reply);
        form_.update(dt);
        toasts_.update(dt, feedback);
        const signal::Mix target = shown_target(link);
        const int channels[] = {target.blue, target.white, target.amber};
        for (int i = 0; i < 3; ++i)
        {
            shown_[i].target = static_cast<float>(channels[i]);
            shown_[i].update(dt, reduced_ ? 60.0f : 14.0f);
        }
    }

    void draw(app::Frame &frame) const override
    {
        const ui::Theme &look = theme();
        begin_frame(frame, look);
        ui::Canvas canvas{frame.scene, context_.fonts, 0, clock_};
        ui::Painter paint(frame.scene, context_.fonts, look);
        const signal::Link &link = signal::link();
        frame.scene.push_opacity(tween::cubic_out(age_ / 0.3f));
        draw_header(paint, "Colour", "Mix the light and keep the colour you like", link, clock_);
        form_.draw(canvas);
        draw_preview(paint, frame.scene, link);
        draw_hints(frame.scene, context_.fonts, look, kHints);
        frame.scene.pop_opacity();
        ui::Canvas above{frame.overlay, context_.fonts, 0, clock_};
        toasts_.draw(above);
    }

    std::span<const app::TourStep> tour() const override
    {
        return kTour;
    }

  private:
    void build_form()
    {
        form_.add_header("Mix");
        form_.add_slider(kBlue, "Blue", 0.0f, 0.0f, 255.0f, 5.0f);
        form_.add_slider(kWhite, "White", 0.0f, 0.0f, 255.0f, 5.0f);
        form_.add_slider(kAmber, "Amber", 0.0f, 0.0f, 255.0f, 5.0f);
        std::vector<std::string> names;
        for (const Preset &preset : kPresets)
            names.emplace_back(preset.name);
        form_.add_choice(kPreset, "Preset", std::move(names), 0);
        form_.add_toggle(kLive, "Show it on the console", true).description =
            "The light follows the sliders while you edit";
        form_.add_header("Keep");
        form_.add_action(kSave, "Keep this colour").description =
            "Saves it: ps5-signal sets it at every start";
        form_.add_action(kSystem, "Use the system's colour").description =
            "Saves no colour: the console decides again";
        form_.add_action(kUndo, "Stop trying").description =
            "The light shows the saved colour again";

        form_.add_header("My colours");
        std::vector<std::string> slots;
        for (int i = 0; i < signal::kPaletteSlots; ++i)
            slots.push_back(std::to_string(i + 1));
        ui::FormRow &slot = form_.add_choice(kSlot, "Colour", std::move(slots), 0);
        slot.description = "Left and right pick one; a stored colour goes to the sliders";
        // Each option is the stored colour as a swatch with its number.
        slot.choice.option = [this](ui::Canvas &canvas, const Rect &area, int index, float)
        { draw_slot(canvas, area, index); };
        form_.add_action(kStore, "Store the mix here").description =
            "Keeps the sliders' mix as this colour, in the app";

        form_.add_header("Temperature");
        form_.add_toggle(kTemperatureLed, "Show the temperature", false).description =
            "Blue when cool, white when normal, amber when warm";
        {
            ui::FormRow &cool = form_.add_stepper(kCool, "Blue up to", signal::kCoolDefault,
                                                  signal::kTemperatureMin,
                                                  signal::kTemperatureMax - signal::kTemperatureGap);
            cool.stepper.set_suffix(" \xC2\xB0" "C");
            cool.description = "At this temperature and below the light is blue";
            ui::FormRow &warm = form_.add_stepper(kWarm, "Amber from", signal::kWarmDefault,
                                                  signal::kTemperatureMin + signal::kTemperatureGap,
                                                  signal::kTemperatureMax);
            warm.stepper.set_suffix(" \xC2\xB0" "C");
            warm.description = "At this temperature and above the light is amber; white halfway";
        }
    }

    // The temperature settings as the form shows them.
    bool temperature_on() const
    {
        return form_.toggle_value(kTemperatureLed);
    }

    // Saves the temperature settings with the rest of the config as the service has it.
    void save_temperature(signal::Link &link)
    {
        temperature_dirty_ = false;
        if (!link.online() || !link.has_config())
            return;
        signal::Config config = link.config();
        config.temperature = temperature_on();
        config.cool = form_.stepper_value(kCool);
        config.warm = form_.stepper_value(kWarm);
        if (config == link.config())
            return;
        request_.start(link, signal::Call::save_config, signal::config_json(config));
        done_text_ = config.temperature ? "Temperature on the light" : "Colour on the light";
    }

    void draw_slot(ui::Canvas &canvas, const Rect &area, int index) const
    {
        ui::Painter paint(canvas.list, canvas.fonts, theme());
        const std::optional<signal::Mix> &stored = palette_.slots[static_cast<std::size_t>(index)];
        const Rect swatch{area.cx() - 70.0f, area.cy() - 15.0f, 30.0f, 30.0f};
        paint.fill(swatch, 0.0f, stored ? led_color(*stored) : empty_channel());
        paint.stroke(swatch, 0.0f, 3.0f, theme().outline);
        char text[24];
        std::snprintf(text, sizeof(text), "%d  %s", index + 1, stored ? "" : "EMPTY");
        paint.label(text, swatch.x + 46.0f, area.cy() + 8.0f, 22.0f, theme().text);
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
        style.label_ratio = 0.4f;
        style.control_width = 360.0f;
        form_.style = style;
        form_.set_bounds(kForm);
        toasts_.style.reduced_motion = reduced_;
    }

    int channel(int index) const
    {
        constexpr int kIds[] = {kBlue, kWhite, kAmber};
        return static_cast<int>(std::lround(form_.slider_value(kIds[index])));
    }

    signal::Mix mix() const
    {
        return {channel(0), channel(1), channel(2)};
    }

    void set_mix(const signal::Mix &value)
    {
        form_.set_slider(kBlue, static_cast<float>(value.blue));
        form_.set_slider(kWhite, static_cast<float>(value.white));
        form_.set_slider(kAmber, static_cast<float>(value.amber));
        match_preset();
    }

    void match_preset()
    {
        int index = 0;
        for (int i = 1; i < kPresetCount; ++i)
        {
            if (kPresets[i].mix == mix())
                index = i;
        }
        form_.set_choice(kPreset, index);
    }

    // The saved colour, once the config arrives (and again after a save). A
    // config that changed elsewhere does not overwrite a mix being tried.
    // What the preview shows: the temperature's colour while it is on, otherwise the sliders.
    signal::Mix shown_target(const signal::Link &link) const
    {
        const signal::Status &status = link.status();
        const int celsius = std::max(status.cpu, status.soc);
        if (temperature_on() && celsius > 0)
            return signal::temperature_mix(celsius, form_.stepper_value(kCool), form_.stepper_value(kWarm));
        return mix();
    }

    void load(const signal::Link &link)
    {
        if (link.config_serial() == loaded_serial_ || (loaded_serial_ != 0 && trying_))
            return;
        if (!link.has_config())
            return;
        loaded_serial_ = link.config_serial();
        const signal::Config &config = link.config();
        if (!temperature_dirty_)
        {
            form_.set_toggle(kTemperatureLed, config.temperature);
            form_.set_stepper(kCool, config.cool);
            form_.set_stepper(kWarm, config.warm);
        }
        signal::Mix start = config.held;
        if (!config.hold)
            start = link.status().color.blue >= 0 ? link.status().color : signal::Mix{0, 0, 0};
        set_mix(start);
        last_sent_ = start;
        trying_ = false;
    }

    void changed(int id)
    {
        if (id == kTemperatureLed || id == kCool || id == kWarm)
        {
            // Cool stays at least kTemperatureGap below warm: the other one moves along.
            const int cool = form_.stepper_value(kCool), warm = form_.stepper_value(kWarm);
            if (warm - cool < signal::kTemperatureGap)
            {
                if (id == kCool)
                    form_.set_stepper(kWarm, cool + signal::kTemperatureGap);
                else
                    form_.set_stepper(kCool, warm - signal::kTemperatureGap);
            }
            temperature_dirty_ = true;
            since_temperature_change_ = 0.0f;
            return;
        }
        if (id == kSlot)
        {
            const std::optional<signal::Mix> &stored =
                palette_.slots[static_cast<std::size_t>(form_.choice_index(kSlot))];
            if (stored)
            {
                set_mix(*stored);
                trying_ = true;
            }
        }
        else if (id == kPreset)
        {
            const int index = form_.choice_index(kPreset);
            if (index > 0)
            {
                form_.set_slider(kBlue, static_cast<float>(kPresets[index].mix.blue));
                form_.set_slider(kWhite, static_cast<float>(kPresets[index].mix.white));
                form_.set_slider(kAmber, static_cast<float>(kPresets[index].mix.amber));
            }
            trying_ = true;
        }
        else if (id == kBlue || id == kWhite || id == kAmber)
        {
            match_preset();
            trying_ = true;
        }
    }

    void enable(const signal::Link &link)
    {
        const bool ready = link.online() && link.has_config();
        // While the light shows the temperature, the service refuses colours: the mix waits.
        const bool mixing = !temperature_on();
        for (const int id : {kBlue, kWhite, kAmber, kPreset, kSlot})
            form_.set_disabled(id, !mixing);
        form_.set_disabled(kSave, !ready || !mixing);
        form_.set_disabled(kSystem, !ready || !mixing);
        form_.set_disabled(kUndo, !link.online() || !mixing);
        form_.set_disabled(kLive, !link.online() || !mixing);
        form_.set_disabled(kTemperatureLed, !ready);
        form_.set_disabled(kCool, !ready || !temperature_on());
        form_.set_disabled(kWarm, !ready || !temperature_on());
    }

    // The light follows the sliders, at most every kSendEvery seconds.
    void try_on_console(signal::Link &link)
    {
        if (!link.online() || !form_.toggle_value(kLive) || !trying_ || temperature_on())
            return;
        const signal::Mix now = mix();
        if (now == last_sent_ || since_sent_ < kSendEvery)
            return;
        link.service().send(signal::Call::color, signal::color_request(now));
        last_sent_ = now;
        since_sent_ = 0.0f;
    }

    void act(int id, signal::Link &link)
    {
        signal::Config config = link.config();
        switch (id)
        {
        case kSave:
            config.hold = true;
            config.held = mix();
            request_.start(link, signal::Call::save_config, signal::config_json(config));
            done_text_ = "Colour kept";
            trying_ = false;
            break;
        case kSystem:
            config.hold = false;
            request_.start(link, signal::Call::save_config, signal::config_json(config));
            done_text_ = "The system decides the colour again";
            trying_ = false;
            break;
        case kStore:
        {
            palette_.slots[static_cast<std::size_t>(form_.choice_index(kSlot))] = mix();
            const std::string error = signal::save_palette(palette_);
            if (error.empty())
                toasts_.push(ui::StatusKind::success, "Colour stored");
            else
                toasts_.push(ui::StatusKind::warning, "Not stored", error);
            return;
        }
        case kUndo:
            request_.start(link, signal::Call::color,
                           config.hold ? signal::color_request(config.held)
                                       : signal::system_color_request());
            done_text_ = "Showing the saved colour";
            trying_ = false;
            loaded_serial_ = 0; // back to the saved values
            break;
        default:
            break;
        }
    }

    void report(const signal::Reply &reply)
    {
        const std::string problem = reply_problem(reply);
        if (problem.empty())
            toasts_.push(ui::StatusKind::success, done_text_);
        else
            toasts_.push(ui::StatusKind::warning, "Not done", problem);
    }

    void draw_preview(ui::Painter &paint, gfx::DrawList &list, const signal::Link &link) const
    {
        const ui::Theme &look = paint.theme();
        if (link.state() == signal::LinkState::offline)
        {
            draw_offline_card(paint, kSide, link);
            return;
        }
        const Rect inside = paint.surface(kSide, 0.0f, look.surface, look.outline, 1.0f);
        const float x = inside.x + 36.0f;
        paint.label("PREVIEW", x, inside.y + 56.0f, 19.0f, look.text_muted);
        const signal::Mix shown{static_cast<int>(std::lround(shown_[0].value)),
                                static_cast<int>(std::lround(shown_[1].value)),
                                static_cast<int>(std::lround(shown_[2].value))};
        draw_light_bar(list, {x, inside.y + 90.0f, inside.w - 72.0f, 150.0f}, shown, clock_);

        float y = inside.y + 300.0f;
        if (temperature_on())
        {
            const signal::Status &status = link.status();
            paint.heading("The light shows the temperature", x, y, 30.0f, look.text);
            char text[96];
            if (status.cpu >= 0 || status.soc >= 0)
                std::snprintf(text, sizeof(text), "CPU %d \xC2\xB0" "C, SoC %d \xC2\xB0" "C: %s", status.cpu, status.soc,
                              mix_text(shown_target(link)).c_str());
            else
                std::snprintf(text, sizeof(text), "Waiting for the console's temperature");
            paint.body(text, x, y + 46.0f, 22.0f, look.text_muted);
            paint.body("Blue when cool, white when normal, amber when warm", x, y + 80.0f, 22.0f,
                       look.text_muted);
            return;
        }
        const signal::Mix now = mix();
        paint.heading(mix_text(now), x, y, 30.0f, look.text);
        y += 46.0f;
        const signal::Config &config = link.config();
        std::string saved = "Saved: ";
        if (!link.has_config())
            saved += "waiting for the service";
        else if (config.hold)
            saved += mix_text(config.held) + (config.held == now ? "  (this one)" : "");
        else
            saved += "the system's own colour";
        paint.body(saved, x, y, 22.0f, look.text_muted);
        y += 34.0f;
        const char *state = !link.online() ? "Not on the console: the service is not found"
                            : !form_.toggle_value(kLive)
                                ? "Only here: \"Show it on the console\" is off"
                            : trying_ ? "On the console now, not kept yet"
                                      : "On the console";
        paint.body(state, x, y, 22.0f, trying_ ? look.text : look.text_muted);

        // A swatch per channel, filled as far as the slider goes.
        y += 64.0f;
        const float cell = (inside.w - 72.0f - 32.0f) / 3.0f;
        constexpr const char *kNames[] = {"BLUE", "WHITE", "AMBER"};
        const signal::Mix pure[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
        for (int i = 0; i < 3; ++i)
        {
            const Rect box{x + static_cast<float>(i) * (cell + 16.0f), y, cell, 90.0f};
            paint.well(box, 0.0f, empty_channel());
            const float share = shown_[i].value / 255.0f;
            const Rect lit{box.x, box.y + box.h * (1.0f - share), box.w, box.h * share};
            if (share > 0.0f)
                paint.fill(lit, 0.0f, led_color(pure[i]));
            paint.stroke(box, 0.0f, 3.0f, look.outline);
            paint.label(kNames[i], box.x + 12.0f, box.y + box.h - 14.0f, 18.0f,
                        i == 1 || share < 0.4f ? look.text : Color::rgb(0xffffff));
        }
    }

    app::Context &context_;
    ui::Form form_;
    ui::ToastStack toasts_;
    Request request_;
    const char *done_text_ = "";
    signal::Mix last_sent_;
    signal::Palette palette_ = signal::load_palette();
    tween::Spring shown_[3];
    unsigned loaded_serial_ = 0;
    bool trying_ = false;
    bool temperature_dirty_ = false;
    float since_temperature_change_ = 0.0f;
    float since_sent_ = 10.0f;
    float clock_ = 0.0f;
    float age_ = 0.0f;
    bool reduced_ = false;
    bool styled_ = false;
};

} // namespace

std::unique_ptr<app::Concept> make_colour(app::Context &context)
{
    return std::make_unique<ColourScreen>(context);
}

} // namespace hui::concepts
