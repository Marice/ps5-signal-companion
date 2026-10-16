// Signal Companion - Screen "Status": does ps5-signal run, and what does the console show now.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The first screen. It asks the service for its status as the app opens and
// every few seconds after. When the service answers, a form lists what it
// reports and offers a short test of the beeper and the light; when it does
// not, the right half explains how to start it.

#include "concepts/concepts.hpp"

#include "concepts/companion/look.hpp"
#include "core/tween.hpp"
#include "ui/components/form.hpp"
#include "ui/components/toast.hpp"
#include "ui/widgets.hpp"

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

enum Id
{
    kSound = 1,
    kBrightness,
    kColourNow,
    kHeld,
    kPlaying,
    kBeep,
    kBlink,
    kStop,
    kCheck,
};

constexpr const char *kTechniques[] = {
    ("Checks whether ps5-signal answers on 127.0.0.1:9150 when the app opens, then every few "
     "seconds"),
    "Shows what the service reports: the beeper, the light's brightness and colour, what plays",
    "Play a short beep or blink the light to hear and see that it works",
    "Start ps5-signal.elf with your ELF loader before you open this app",
};

constexpr ui::Hint kHints[] = {
    {ui::Button::dpad, "Move"},
    {ui::Button::cross, "Choose"},
};

constexpr app::TourStep kTour[] = {
    {1.2f, 0, Direction::none, "status"},
    {0.3f, 0, Direction::down},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::down},
    {0.2f, 0, Direction::down},
    {0.6f, 0, Direction::down, "status-actions"},
};

const char *setting_text(int value, const char *const *names, int count)
{
    return value >= 0 && value < count ? names[value] : "Unknown";
}

class StatusScreen final : public app::Concept
{
  public:
    explicit StatusScreen(app::Context &context) : context_(context)
    {
        build_form();
        toasts_.style.theme = theme();
        toasts_.style.anchor = ui::ToastAnchor::bottom_right;
        toasts_.style.margin = 120.0f;
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "status",
            "Status",
            "Whether ps5-signal runs, and what the console's beeper and light do now",
            "src/concepts/status.cpp",
            audio::SoundSet::paper,
            Color::rgb(0x36d67e),
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
        signal::Link &link = signal::link();
        link.update(dt);
        restyle();
        show_status(link);

        if (form_.handle(input, feedback) == ui::Event::activated)
            act(form_.changed_id(), link);
        signal::Reply reply;
        if (request_.done(link, reply))
            report(reply);
        form_.update(dt);
        toasts_.update(dt, feedback);
        light_.target = 1.0f;
        light_.update(dt, 10.0f);
    }

    void draw(app::Frame &frame) const override
    {
        const ui::Theme &look = theme();
        begin_frame(frame, look);
        ui::Canvas canvas{frame.scene, context_.fonts, 0, clock_};
        ui::Painter paint(frame.scene, context_.fonts, look);
        const signal::Link &link = signal::link();
        frame.scene.push_opacity(tween::cubic_out(age_ / 0.3f));
        draw_header(paint, "Status", "Is the service running, and what does the console show", link,
                    clock_);
        form_.draw(canvas);
        if (link.online())
            draw_light(paint, frame.scene, link);
        else
            draw_offline_card(paint, kSide, link);
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
        form_.add_header("Now");
        form_.add_value(kSound, "Beeper", "-");
        form_.add_value(kBrightness, "Brightness", "-");
        form_.add_value(kColourNow, "Colour", "-");
        form_.add_value(kHeld, "Kept colour", "-");
        form_.add_value(kPlaying, "Playing", "-");
        form_.add_header("Try it");
        form_.add_action(kBeep, "Play a short beep").description =
            "One short beep, if the beeper is not muted in the console's settings";
        form_.add_action(kBlink, "Blink the light").description =
            "Orange and off, three times; then the light returns to its colour";
        form_.add_action(kStop, "Stop everything").description =
            "Ends a beep and a light sequence that play now";
        form_.add_action(kCheck, "Check again").description = "Asks the service for its status now";
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
        style.label_ratio = 0.34f;
        style.compact = true;
        form_.style = style;
        form_.set_bounds(kForm);
        toasts_.style.reduced_motion = reduced_;
    }

    void show_status(const signal::Link &link)
    {
        const bool online = link.online();
        for (const int id : {kBeep, kBlink, kStop})
            form_.set_disabled(id, !online);
        // Without the service only "Check again" works: keep the focus there.
        if (link.state() == signal::LinkState::offline && shown_state_ != link.state())
            form_.focus_row(kCheck, false);
        shown_state_ = link.state();
        if (!online)
        {
            for (const int id : {kSound, kBrightness, kColourNow, kHeld, kPlaying})
                form_.set_value_text(id, "-");
            return;
        }
        const signal::Status &status = link.status();
        char text[48];
        if (status.muted == 1)
            std::snprintf(text, sizeof(text), "Muted in the settings");
        else if (status.muted == 0 && status.volume >= 0)
            std::snprintf(text, sizeof(text), "On, volume %d", status.volume);
        else
            std::snprintf(text, sizeof(text), "%s", status.muted == 0 ? "On" : "Unknown");
        form_.set_value_text(kSound, text);
        constexpr const char *kLevels[] = {"Bright", "Medium", "Dim"};
        form_.set_value_text(kBrightness, status.led_available
                                              ? setting_text(status.level, kLevels, 3)
                                              : "Not available");
        form_.set_value_text(kColourNow, !status.color_available ? "Not available"
                                         : status.color.blue < 0 ? "Unknown"
                                                                 : mix_text(status.color));
        form_.set_value_text(kHeld, status.hold ? mix_text(status.held) : "The system's own");
        const char *playing = status.beeping && status.lighting ? "Beeps and light"
                              : status.beeping                  ? "Beeps"
                              : status.lighting                 ? "Light"
                                                                : "Nothing";
        form_.set_value_text(kPlaying, playing);
    }

    void act(int id, signal::Link &link)
    {
        switch (id)
        {
        case kBeep:
            request_.start(link, signal::Call::beep, signal::beep_request({{1, 200}}));
            done_text_ = "Beep sent";
            break;
        case kBlink:
        {
            signal::LedStep on;
            on.name = signal::LedName::orange;
            on.ms = 400;
            signal::LedStep off = on;
            off.name = signal::LedName::off;
            request_.start(link, signal::Call::led, signal::led_request({on, off}, 3));
            done_text_ = "Blinking";
            break;
        }
        case kStop:
            request_.start(link, signal::Call::stop);
            done_text_ = "Stopped";
            break;
        default:
            link.refresh();
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

    void draw_light(ui::Painter &paint, gfx::DrawList &list, const signal::Link &link) const
    {
        const ui::Theme &look = paint.theme();
        const signal::Status &status = link.status();
        const Rect inside = paint.surface(kSide, 0.0f, look.surface, look.outline, 1.0f);
        const float x = inside.x + 36.0f;
        paint.label("ON THE CONSOLE NOW", x, inside.y + 56.0f, 19.0f, look.text_muted);
        const signal::Mix shown = status.color.blue < 0 ? signal::Mix{} : status.color;
        draw_light_bar(list, {x, inside.y + 90.0f, inside.w - 72.0f, 150.0f}, shown, clock_);
        float y = inside.y + 300.0f;
        paint.heading(status.hold ? "Kept colour" : "The system's colour", x, y, 34.0f, look.text);
        y += 44.0f;
        paint.body(status.hold ? "ps5-signal sets it again when the system takes the light back."
                               : "Choose a colour of your own on the Colour screen.",
                   x, y, 22.0f, look.text_muted);
        y += 70.0f;
        const signal::Mix channels = shown;
        const int values[] = {channels.blue, channels.white, channels.amber};
        constexpr const char *kNames[] = {"BLUE", "WHITE", "AMBER"};
        const Color tints[] = {led_color({255, 0, 0}), led_color({0, 255, 0}),
                               led_color({0, 0, 255})};
        const float bar = inside.w - 72.0f - 190.0f;
        for (int i = 0; i < 3; ++i)
        {
            paint.label(kNames[i], x, y + 8.0f, 20.0f, look.text);
            const Rect track{x + 120.0f, y - 12.0f, bar, 24.0f};
            paint.well(track, 0.0f, empty_channel());
            const float share = static_cast<float>(values[i]) / 255.0f * light_.value;
            if (share > 0.0f)
                paint.fill({track.x, track.y, track.w * share, track.h}, 0.0f,
                           i == 1 ? look.text : tints[i]);
            char text[8];
            std::snprintf(text, sizeof(text), "%d", values[i]);
            paint.label(text, track.x + track.w + 16.0f, y + 8.0f, 20.0f, look.text);
            y += 48.0f;
        }
    }

    app::Context &context_;
    ui::Form form_;
    ui::ToastStack toasts_;
    Request request_;
    const char *done_text_ = "";
    tween::Spring light_;
    float clock_ = 0.0f;
    float age_ = 0.0f;
    bool reduced_ = false;
    bool styled_ = false;
    signal::LinkState shown_state_ = signal::LinkState::checking;
};

} // namespace

std::unique_ptr<app::Concept> make_status(app::Context &context)
{
    return std::make_unique<StatusScreen>(context);
}

} // namespace hui::concepts
